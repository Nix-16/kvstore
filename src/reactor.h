#ifndef REACTOR_H
#define REACTOR_H

#include <stdint.h>
#include <sys/types.h>

#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct reactor;
struct connection;

typedef int (*on_message_cb)(struct connection *c, void *user_data);
typedef void (*on_close_cb)(struct connection *c, void *user_data);

struct connection {
    int fd;

    struct buffer in;   /* 输入缓冲：读事件追加到这里 */
    struct buffer out;  /* 输出缓冲：业务层写响应到这里，写事件发送 */

    uint32_t events;    /* 当前关注的 epoll 事件掩码（EPOLLIN/EPOLLOUT） */
    int closed;         /* 是否已关闭 */

    void *user_data;    /* 给业务层挂上下文（例如解析器状态） */
};

/* 创建/销毁 reactor */
struct reactor *reactor_create(int max_events);
void reactor_destroy(struct reactor *r);

/* 设置回调（user_data 是“全局上下文”） */
void reactor_set_callbacks(struct reactor *r, on_message_cb on_msg, on_close_cb on_close, void *user_data);

/*
 * 监听一个端口（本实现只支持一个 listenfd）
 * @return:
 *   0  success
 *  -1  参数/系统调用失败
 *  -2  已经监听过一个端口，禁止重复 listen
 */
int reactor_listen(struct reactor *r, const char *ip, uint16_t port, int backlog);

/* 运行事件循环（阻塞） */
int reactor_run(struct reactor *r);
void reactor_stop(struct reactor *r);

/* 业务层发送：追加到 out buffer，并确保监听 EPOLLOUT */
int connection_send(struct reactor *r, struct connection *c, const void *data, size_t len);

/* 主动关闭连接 */
void connection_close(struct reactor *r, struct connection *c);

#ifdef __cplusplus
}
#endif

#endif /* REACTOR_H */