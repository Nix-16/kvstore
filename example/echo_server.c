#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "reactor.h"
#include "buffer.h"

static int on_message(struct connection *c, void *user_data)
{
    struct reactor *r = (struct reactor *)user_data;

    size_t n = buffer_readable_bytes(&c->in);
    if (n == 0) return 0;

    printf("[fd=%d] on_message called, in_bytes=%zu\n", c->fd, n);

    const char *p = buffer_peek(&c->in);

    /* 回写 */
    if (connection_send(r, c, p, n) != 0) {
        fprintf(stderr, "[fd=%d] connection_send failed\n", c->fd);
        return -1;
    }

    /* 消费输入 */
    buffer_retrieve(&c->in, n);

    return 0;
}

static void on_close(struct connection *c, void *user_data)
{
    (void)user_data;
    printf("[fd=%d] connection closed\n", c->fd);
}

int main(int argc, char **argv)
{
    uint16_t port = (argc >= 2) ? (uint16_t)atoi(argv[1]) : 6379;

    struct reactor *r = reactor_create(1024);
    if (!r) {
        fprintf(stderr, "reactor_create failed\n");
        return 1;
    }

    /* 把 reactor 指针作为 user_data 传给回调 */
    reactor_set_callbacks(r, on_message, on_close, r);

    if (reactor_listen(r, "0.0.0.0", port, 128) != 0) {
        fprintf(stderr, "reactor_listen failed on port %u\n", port);
        reactor_destroy(r);
        return 1;
    }

    printf("echo_server listening on 0.0.0.0:%u\n", port);
    fflush(stdout);

    reactor_run(r);
    reactor_destroy(r);
    return 0;
}