#include "reactor.h"

#include "kvs_alloc.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include "kvs_aof.h"

/* ----------------------------- 工具函数：non-blocking ----------------------------- */

/* 设置 fd 为非阻塞：成功返回 0，失败返回 -1 */
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    return 0;
}

/* ----------------------------- reactor 内部结构 ----------------------------- */

struct reactor
{
    int epfd;
    int running;

    struct epoll_event *evlist;
    int max_events;

    /* fd -> connection* 映射（动态数组，避免 fd 超出固定数组） */
    struct connection **conns;
    int conns_cap;

    /* 单端口：只保留一个监听 fd */
    int listen_fd;

    on_message_cb on_msg;
    on_close_cb on_close;
    void *cb_user_data;
};

/* ----------------------------- fdmap 管理 ----------------------------- */

/* 确保 conns 数组容量至少能容纳 fd */
static int ensure_conns_cap(struct reactor *r, int fd)
{
    if (!r || fd < 0)
        return -1;

    if (fd < r->conns_cap)
        return 0;

    int new_cap = r->conns_cap ? r->conns_cap : 1024;
    while (new_cap <= fd)
    {
        if (new_cap > (1 << 27))
            return -1; /* 防止扩太大 */
        new_cap *= 2;
    }

    struct connection **new_arr =
        (struct connection **)kvs_realloc(r->conns, sizeof(struct connection *) * (size_t)new_cap);
    if (!new_arr)
        return -1;

    /* 新扩展出来的区域清零 */
    memset(new_arr + r->conns_cap, 0,
           sizeof(struct connection *) * (size_t)(new_cap - r->conns_cap));

    r->conns = new_arr;
    r->conns_cap = new_cap;
    return 0;
}

/* ----------------------------- epoll 事件更新 ----------------------------- */

/* 更新 fd 的关注事件（内部自动 ADD/MOD/DEL） */
static int epoll_update(struct reactor *r, int fd, uint32_t new_events)
{
    if (!r || fd < 0)
        return -1;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = new_events;
    ev.data.fd = fd;

    if (new_events == 0)
    {
        /* DEL：删除监听 */
        epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
        return 0;
    }

    /* 优先 MOD，若 fd 不存在再 ADD（避免额外状态记录） */
    if (epoll_ctl(r->epfd, EPOLL_CTL_MOD, fd, &ev) == 0)
    {
        return 0;
    }
    if (errno == ENOENT)
    {
        if (epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev) == 0)
        {
            return 0;
        }
    }
    return -1;
}

/* ----------------------------- connection 生命周期 ----------------------------- */

static struct connection *connection_create(int fd)
{
    struct connection *c = (struct connection *)kvs_calloc(1, sizeof(struct connection));
    if (!c)
        return NULL;

    c->fd = fd;
    c->events = 0;
    c->closed = 0;
    c->user_data = NULL;

    /* 初始化 in/out buffer */
    if (buffer_init(&c->in, 0) != 0)
    {
        kvs_free(c);
        return NULL;
    }
    if (buffer_init(&c->out, 0) != 0)
    {
        buffer_free(&c->in);
        kvs_free(c);
        return NULL;
    }

    return c;
}

static void connection_destroy(struct connection *c)
{
    if (!c)
        return;

    buffer_free(&c->in);
    buffer_free(&c->out);
    kvs_free(c);
}

/* 关闭连接（epoll 删除 + close(fd) + 回调 + 释放） */
void connection_close(struct reactor *r, struct connection *c)
{
    if (!r || !c)
        return;
    if (c->closed)
        return;

    c->closed = 1;

    /* 从 epoll 移除 */
    epoll_update(r, c->fd, 0);

    /* 关闭 fd */
    close(c->fd);

    /* 回调通知业务层 */
    if (r->on_close)
    {
        r->on_close(c, r->cb_user_data);
    }

    /* fdmap 清理 */
    if (c->fd >= 0 && c->fd < r->conns_cap)
    {
        r->conns[c->fd] = NULL;
    }

    connection_destroy(c);
}

/* 业务层发送：追加到 out，并确保监听 EPOLLOUT */
int connection_send(struct reactor *r, struct connection *c, const void *data, size_t len)
{
    if (!r || !c || c->closed)
        return -1;
    if (!data && len != 0)
        return -1;

    if (buffer_append(&c->out, data, len) != 0)
    {
        return -1;
    }

    /* out 非空则需要关注 EPOLLOUT；同时保留 EPOLLIN */
    uint32_t want = (c->events | EPOLLIN | EPOLLOUT);
    if (want != c->events)
    {
        if (epoll_update(r, c->fd, want) != 0)
            return -1;
        c->events = want;
    }
    return 0;
}

/* ----------------------------- 监听 socket 创建 ----------------------------- */

static int create_listen_fd(const char *ip, uint16_t port, int backlog)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (!ip || ip[0] == '\0')
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
        {
            close(fd);
            return -1;
        }
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) != 0)
    {
        close(fd);
        return -1;
    }

    if (set_nonblocking(fd) != 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}

/* ----------------------------- accept/read/write 处理 ----------------------------- */

/* accept：循环 accept 到 EAGAIN（一次 epoll 事件尽量 accept 干净） */
static void handle_accept(struct reactor *r, int listenfd)
{
    for (;;)
    {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);

        int cfd = accept(listenfd, (struct sockaddr *)&cli, &len);
        if (cfd < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            /* accept 失败通常只影响当前回合，直接退出即可 */
            break;
        }

        if (set_nonblocking(cfd) != 0)
        {
            close(cfd);
            continue;
        }

        if (ensure_conns_cap(r, cfd) != 0)
        {
            close(cfd);
            continue;
        }

        struct connection *c = connection_create(cfd);
        if (!c)
        {
            close(cfd);
            continue;
        }

        r->conns[cfd] = c;

        /* 新连接默认只监听读事件 */
        uint32_t ev = EPOLLIN;
        if (epoll_update(r, cfd, ev) == 0)
        {
            c->events = ev;
        }
        else
        {
            r->conns[cfd] = NULL;
            connection_destroy(c);
            close(cfd);
        }
    }
}

/* 写：把 out buffer 写到 fd，支持部分写，写到 EAGAIN */
static int flush_out_buffer(struct connection *c)
{
    while (buffer_readable_bytes(&c->out) > 0) {
        const char *p = buffer_peek(&c->out);
        size_t nleft = buffer_readable_bytes(&c->out);

        /* 直接写 nleft 即可（非阻塞下 write 可能只写一部分） */
        ssize_t n = write(c->fd, p, nleft);
        if (n > 0) {
            buffer_retrieve(&c->out, (size_t)n);
            continue;
        }

        if (n == 0) {
            /* write 返回 0 很少见，按“暂时写不动”处理 */
            return 0;
        }

        /* n < 0 */
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* 内核发送缓冲满了：等待下次 EPOLLOUT */
            return 0;
        }

        /* 其他错误：需要关闭连接 */
        return -1;
    }

    return 0;
}

static int handle_read(struct reactor *r, struct connection *c)
{
    int saved_errno = 0;
    ssize_t n = buffer_read_fd(&c->in, c->fd, &saved_errno);

    if (n < 0)
    {
        return -1;
    }

    if (n == 0)
    {
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
        {
            return 0; /* 暂时没数据 */
        }
        return -1; /* EOF */
    }

    if (r->on_msg)
    {
        /* 防止业务层写错导致死循环：最多尝试 N 轮 */
        const int kMaxRounds = 16;

        for (int round = 0; round < kMaxRounds; round++)
        {
            size_t before = buffer_readable_bytes(&c->in);

            int rc = r->on_msg(c, r->cb_user_data);
            if (rc < 0)
                return -1;

            size_t after = buffer_readable_bytes(&c->in);

            /* 业务层没有消费输入（说明不够解析/或者没处理），就停 */
            if (after >= before)
                break;

            /* 已经没有数据了，也停 */
            if (after == 0)
                break;
        }
    }

    return 0;
}

/* ----------------------------- public API ----------------------------- */

struct reactor *reactor_create(int max_events)
{
    if (max_events <= 0)
        max_events = 1024;

    struct reactor *r = (struct reactor *)kvs_calloc(1, sizeof(struct reactor));
    if (!r)
        return NULL;

    r->epfd = epoll_create1(0);
    if (r->epfd < 0)
    {
        kvs_free(r);
        return NULL;
    }

    r->evlist = (struct epoll_event *)kvs_calloc((size_t)max_events, sizeof(struct epoll_event));
    if (!r->evlist)
    {
        close(r->epfd);
        kvs_free(r);
        return NULL;
    }

    r->max_events = max_events;
    r->running = 0;

    r->conns = NULL;
    r->conns_cap = 0;

    r->listen_fd = -1; /* 关键：-1 表示还没监听 */

    r->on_msg = NULL;
    r->on_close = NULL;
    r->cb_user_data = NULL;

    return r;
}

void reactor_destroy(struct reactor *r)
{
    if (!r)
        return;

    /* 关闭所有 client 连接 */
    if (r->conns)
    {
        for (int fd = 0; fd < r->conns_cap; fd++)
        {
            if (r->conns[fd])
            {
                connection_close(r, r->conns[fd]);
            }
        }
        kvs_free(r->conns);
    }

    /* 关闭 listenfd */
    if (r->listen_fd >= 0)
    {
        epoll_update(r, r->listen_fd, 0);
        close(r->listen_fd);
        r->listen_fd = -1;
    }

    kvs_free(r->evlist);
    close(r->epfd);
    kvs_free(r);
}

void reactor_set_callbacks(struct reactor *r, on_message_cb on_msg, on_close_cb on_close, void *user_data)
{
    if (!r)
        return;
    r->on_msg = on_msg;
    r->on_close = on_close;
    r->cb_user_data = user_data;
}

int reactor_listen(struct reactor *r, const char *ip, uint16_t port, int backlog)
{
    if (!r)
        return -1;
    if (backlog <= 0)
        backlog = 128;

    /* 单端口：禁止重复 listen */
    if (r->listen_fd >= 0)
    {
        return -2;
    }

    int listenfd = create_listen_fd(ip, port, backlog);
    if (listenfd < 0)
        return -1;

    /* 注册到 epoll */
    if (epoll_update(r, listenfd, EPOLLIN) != 0)
    {
        close(listenfd);
        return -1;
    }

    r->listen_fd = listenfd;
    return 0;
}

int reactor_run(struct reactor *r)
{
    if (!r)
        return -1;

    r->running = 1;

    while (r->running)
    {
        int n = epoll_wait(r->epfd, r->evlist, r->max_events, 100);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }

        for (int i = 0; i < n; i++)
        {
            int fd = r->evlist[i].data.fd;
            uint32_t ev = r->evlist[i].events;

            /* 监听 fd：accept */
            if (fd == r->listen_fd)
            {
                if (ev & EPOLLIN)
                {
                    handle_accept(r, fd);
                }
                /* 监听 fd 出错：这里先忽略（也可扩展为重建 listenfd） */
                continue;
            }

            /* clientfd：错误优先处理 */
            if (ev & (EPOLLERR | EPOLLHUP))
            {
                if (fd >= 0 && fd < r->conns_cap && r->conns[fd])
                {
                    connection_close(r, r->conns[fd]);
                }
                continue;
            }

#ifdef EPOLLRDHUP
            if (ev & EPOLLRDHUP)
            {
                if (fd >= 0 && fd < r->conns_cap && r->conns[fd])
                {
                    connection_close(r, r->conns[fd]);
                }
                continue;
            }
#endif

            if (fd < 0 || fd >= r->conns_cap)
                continue;
            struct connection *c = r->conns[fd];
            if (!c || c->closed)
                continue;

            /* 读事件 */
            if (ev & EPOLLIN)
            {
                if (handle_read(r, c) != 0)
                {
                    connection_close(r, c);
                    continue;
                }
            }

            /* 写事件 */
            if (ev & EPOLLOUT)
            {
                if (flush_out_buffer(c) != 0)
                {
                    connection_close(r, c);
                    continue;
                }

                /* out 发空：取消 EPOLLOUT，仅保留 EPOLLIN */
                if (buffer_readable_bytes(&c->out) == 0)
                {
                    uint32_t want = (c->events & ~EPOLLOUT) | EPOLLIN;
                    if (want != c->events)
                    {
                        if (epoll_update(r, c->fd, want) == 0)
                        {
                            c->events = want;
                        }
                        else
                        {
                            connection_close(r, c);
                            continue;
                        }
                    }
                }
            }

            /* 业务层若写了响应(out 非空)，确保开启 EPOLLOUT */
            if (!c->closed && buffer_readable_bytes(&c->out) > 0)
            {
                uint32_t want = c->events | EPOLLOUT | EPOLLIN;
                if (want != c->events)
                {
                    if (epoll_update(r, c->fd, want) == 0)
                    {
                        c->events = want;
                    }
                    else
                    {
                        connection_close(r, c);
                        continue;
                    }
                }
            }
        }

                /* 后台周期性处理 AOF everysec */
        if (kvs_aof_maybe_fsync() != 0)
        {
            fprintf(stderr, "kvs_aof_maybe_fsync failed\n");
        }
    }

    return 0;
}

void reactor_stop(struct reactor *r)
{
    if (!r)
        return;
    r->running = 0;
}