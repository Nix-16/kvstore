#include "../../src/buffer.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

/* 设置 fd 为非阻塞 */
static void set_nonblocking_or_die(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

static void test_init_free_basic(void) {
    struct buffer b;
    memset(&b, 0xAB, sizeof(b)); /* 确保 init 确实初始化字段 */

    assert(buffer_init(&b, 0) == 0);
    assert(b.data != NULL);
    assert(b.capacity >= BUFFER_DEFAULT_SIZE);
    assert(buffer_readable_bytes(&b) == 0);
    assert(buffer_writable_bytes(&b) == b.capacity);

    buffer_free(&b);
    assert(b.data == NULL);
    assert(b.capacity == 0);
    assert(b.read_index == 0);
    assert(b.write_index == 0);

    /* 重复 free 不应崩 */
    buffer_free(&b);
}

static void test_append_peek_retrieve(void) {
    struct buffer b;
    assert(buffer_init(&b, 16) == 0);

    const char *s = "hello";
    assert(buffer_append(&b, s, strlen(s)) == 0);
    assert(buffer_readable_bytes(&b) == 5);

    const char *p = buffer_peek(&b);
    assert(p != NULL);
    assert(memcmp(p, "hello", 5) == 0);

    /* retrieve 2 -> 剩 "llo" */
    buffer_retrieve(&b, 2);
    assert(buffer_readable_bytes(&b) == 3);
    p = buffer_peek(&b);
    assert(memcmp(p, "llo", 3) == 0);

    /* retrieve_all -> 清空 */
    buffer_retrieve_all(&b);
    assert(buffer_readable_bytes(&b) == 0);
    assert(b.read_index == 0 && b.write_index == 0);

    buffer_free(&b);
}

static void test_compact_path(void) {
    struct buffer b;
    assert(buffer_init(&b, 16) == 0);

    /* 写 12 字节 'A' */
    char a[12];
    memset(a, 'A', sizeof(a));
    assert(buffer_append(&b, a, sizeof(a)) == 0);
    assert(buffer_readable_bytes(&b) == 12);

    /* 消费 10 字节 -> 剩 2 字节 'A' */
    buffer_retrieve(&b, 10);
    assert(buffer_readable_bytes(&b) == 2);

    /* 再追加 10 字节 'X'，尾部空间不够 -> 触发 compact(memmove) */
    char x[10];
    memset(x, 'X', sizeof(x));
    assert(buffer_append(&b, x, sizeof(x)) == 0);

    /* 期望可读为 2('A') + 10('X') */
    assert(buffer_readable_bytes(&b) == 12);

    const char *p = buffer_peek(&b);
    assert(p != NULL);
    assert(p[0] == 'A' && p[1] == 'A');
    assert(memcmp(p + 2, x, 10) == 0);

    buffer_free(&b);
}

static void test_expand_path(void) {
    struct buffer b;
    assert(buffer_init(&b, 8) == 0);

    /* 追加 1000 字节，必触发扩容 */
    char big[1000];
    for (int i = 0; i < (int)sizeof(big); i++) {
        big[i] = (char)('a' + (i % 26));
    }

    assert(buffer_append(&b, big, sizeof(big)) == 0);
    assert(buffer_readable_bytes(&b) == sizeof(big));
    assert(b.capacity >= sizeof(big));

    const char *p = buffer_peek(&b);
    assert(p != NULL);
    assert(memcmp(p, big, sizeof(big)) == 0);

    buffer_free(&b);
}

static void test_cap_enforced_and_no_corruption(void) {
    struct buffer b;
    assert(buffer_init(&b, 16) == 0);

    /* 放入已知数据 */
    assert(buffer_append(&b, "TAG", 3) == 0);

    size_t before_readable = buffer_readable_bytes(&b);
    size_t before_cap = b.capacity;
    size_t before_r = b.read_index;
    size_t before_w = b.write_index;

    /* 触发超过上限：write_index + huge > BUFFER_MAX_CAPACITY */
    size_t huge = (size_t)BUFFER_MAX_CAPACITY;
    int rc = buffer_ensure_writable_bytes(&b, huge);
    assert(rc == -1);

    /* 验证失败不破坏状态/内容 */
    assert(buffer_readable_bytes(&b) == before_readable);
    assert(b.capacity == before_cap);
    assert(b.read_index == before_r);
    assert(b.write_index == before_w);

    const char *p = buffer_peek(&b);
    assert(p != NULL);
    assert(memcmp(p, "TAG", 3) == 0);

    buffer_free(&b);
}

static void test_read_fd_pipe_nonblocking(void) {
    int pfd[2];
    assert(pipe(pfd) == 0);
    int rfd = pfd[0], wfd = pfd[1];

    set_nonblocking_or_die(rfd);

    struct buffer b;
    assert(buffer_init(&b, 16) == 0);

    /* 1) 写入一些数据 */
    const char *msg = "hello_pipe";
    assert(write(wfd, msg, (int)strlen(msg)) == (ssize_t)strlen(msg));

    int saved = 0;
    ssize_t n = buffer_read_fd(&b, rfd, &saved);
    assert(n == (ssize_t)strlen(msg));
    assert(buffer_readable_bytes(&b) == (size_t)strlen(msg));
    assert(memcmp(buffer_peek(&b), msg, strlen(msg)) == 0);

    /* 2) 再读一次：无更多数据 -> EAGAIN，按你的实现返回 0 */
    saved = 0;
    n = buffer_read_fd(&b, rfd, &saved);
    assert(n == 0);

    /* 3) 关闭写端 -> EOF：此时读到 0（total==0） */
    close(wfd);
    saved = 0;
    n = buffer_read_fd(&b, rfd, &saved);
    assert(n == 0);

    close(rfd);
    buffer_free(&b);
}

int main(void) {
    test_init_free_basic();
    test_append_peek_retrieve();
    test_compact_path();
    test_expand_path();
    test_cap_enforced_and_no_corruption();
    test_read_fd_pipe_nonblocking();

    puts("ALL buffer tests PASSED");
    return 0;
}