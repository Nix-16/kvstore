#include "resp_reply.h"

#include <stdio.h>
#include <string.h>

/* 内部小工具：追加常量字符串 */
static int append_cstr(struct buffer *out, const char *s)
{
    if (!out || !s) return -1;
    return buffer_append(out, s, strlen(s));
}

/* 内部小工具：追加 \r\n */
static int append_crlf(struct buffer *out)
{
    return buffer_append(out, "\r\n", 2);
}

int resp_reply_simple(struct buffer *out, const char *s)
{
    /* +<s>\r\n */
    if (!out || !s) return -1;

    if (buffer_append(out, "+", 1) != 0) return -1;
    if (buffer_append(out, s, strlen(s)) != 0) return -1;
    if (append_crlf(out) != 0) return -1;

    return 0;
}

int resp_reply_error(struct buffer *out, const char *msg)
{
    /* -ERR <msg>\r\n
     * 你也可以改成 -<msg>\r\n，但加 ERR 更像 Redis
     */
    if (!out || !msg) return -1;

    if (buffer_append(out, "-", 1) != 0) return -1;
    if (buffer_append(out, "ERR ", 4) != 0) return -1;
    if (buffer_append(out, msg, strlen(msg)) != 0) return -1;
    if (append_crlf(out) != 0) return -1;

    return 0;
}

int resp_reply_integer(struct buffer *out, long long v)
{
    /* :<v>\r\n */
    if (!out) return -1;

    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), ":%lld\r\n", v);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) return -1;

    return buffer_append(out, tmp, (size_t)n);
}

int resp_reply_bulk(struct buffer *out, const void *data, size_t len)
{
    /* $<len>\r\n<data>\r\n
     * data 允许 NULL 但 len 必须是 0（表示空字符串）
     */
    if (!out) return -1;
    if (!data && len != 0) return -1;

    char head[64];
    int n = snprintf(head, sizeof(head), "$%zu\r\n", len);
    if (n <= 0 || (size_t)n >= sizeof(head)) return -1;

    if (buffer_append(out, head, (size_t)n) != 0) return -1;
    if (len > 0) {
        if (buffer_append(out, data, len) != 0) return -1;
    }
    if (append_crlf(out) != 0) return -1;

    return 0;
}

int resp_reply_nil(struct buffer *out)
{
    /* $-1\r\n */
    if (!out) return -1;
    return append_cstr(out, "$-1\r\n");
}

int resp_reply_array_header(struct buffer *out, long long n)
{
    /* *<n>\r\n */
    if (!out) return -1;

    char tmp[64];
    int k = snprintf(tmp, sizeof(tmp), "*%lld\r\n", n);
    if (k <= 0 || (size_t)k >= sizeof(tmp)) return -1;

    return buffer_append(out, tmp, (size_t)k);
}