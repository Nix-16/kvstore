#include "buffer.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

/* 统一检查 buffer 状态是否基本合法 */
static int buffer_is_valid(const struct buffer *buf)
{
    if (!buf)
        return 0;

    /* capacity 为 0 时，data 可以为 NULL；否则 data 必须非 NULL */
    if (buf->capacity == 0)
    {
        if (buf->data != NULL)
            return 0;
        if (buf->read_index != 0 || buf->write_index != 0)
            return 0;
        return 1;
    }

    if (!buf->data)
        return 0;

    /* 索引必须满足 0 <= read <= write <= cap */
    if (buf->read_index > buf->write_index)
        return 0;
    if (buf->write_index > buf->capacity)
        return 0;
    return 1;
}

/* 扩容到至少 new_capacity（调用方已做上限/溢出校验）
 * realloc 失败不会释放旧指针，因此失败时内容不变。
 */
static int buffer_expand(struct buffer *buf, size_t new_capacity)
{
    if (!buffer_is_valid(buf))
        return -1;
    if (new_capacity == 0)
        return -1;
    if (new_capacity < buf->capacity)
        return -1;
    if (new_capacity > (size_t)BUFFER_MAX_CAPACITY)
        return -1;

    char *new_data = (char *)realloc(buf->data, new_capacity);
    if (!new_data)
        return -1;

    buf->data = new_data;
    buf->capacity = new_capacity;
    return 0;
}

int buffer_init(struct buffer *buf, size_t initial_size)
{
    if (!buf)
        return -1;

    if (initial_size == 0)
        initial_size = BUFFER_DEFAULT_SIZE;
    if (initial_size > (size_t)BUFFER_MAX_CAPACITY)
        return -1;

    buf->data = (char *)malloc(initial_size);
    if (!buf->data)
    {
        /* 保持一个可识别的“未初始化状态” */
        buf->capacity = 0;
        buf->read_index = 0;
        buf->write_index = 0;
        return -1;
    }

    buf->read_index = 0;
    buf->write_index = 0;
    buf->capacity = initial_size;
    return 0;
}

void buffer_free(struct buffer *buf)
{
    if (!buf)
        return;

    free(buf->data);
    buf->data = NULL;
    buf->read_index = 0;
    buf->write_index = 0;
    buf->capacity = 0;
}

size_t buffer_readable_bytes(const struct buffer *buf)
{
    if (!buffer_is_valid(buf))
        return 0;
    return buf->write_index - buf->read_index;
}

size_t buffer_writable_bytes(const struct buffer *buf)
{
    if (!buffer_is_valid(buf))
        return 0;
    return buf->capacity - buf->write_index;
}

int buffer_ensure_writable_bytes(struct buffer *buf, size_t len)
{
    if (!buffer_is_valid(buf))
        return -1;

    if (len == 0)
        return 0;

    /* 上限/溢出保护：确保 write_index + len 不溢出且不超过最大容量 */
    if (buf->write_index > (size_t)BUFFER_MAX_CAPACITY - len)
        return -1;

    /* 快路径：尾部可写空间足够 */
    if (buffer_writable_bytes(buf) >= len)
        return 0;

    /* 1) 压缩：回收头部空间 */
    size_t readable = buffer_readable_bytes(buf);
    if (buf->read_index > 0)
    {
        memmove(buf->data, buf->data + buf->read_index, readable);
        buf->read_index = 0;
        buf->write_index = readable;
        /* 压缩后仍需保证状态有效 */
        if (!buffer_is_valid(buf))
            return -1;
    }

    if (buffer_writable_bytes(buf) >= len)
        return 0;

    /* 2) 扩容：需要 capacity >= write_index + len */
    size_t required = buf->write_index + len;
    if (required > (size_t)BUFFER_MAX_CAPACITY)
        return -1;

    size_t new_capacity = buf->capacity ? buf->capacity : (size_t)BUFFER_DEFAULT_SIZE;

    /* 2 倍增长直到满足 required，检查 size_t 溢出，并夹到最大上限 */
    while (new_capacity < required)
    {
        if (new_capacity > SIZE_MAX / 2)
            return -1; /* 倍增会溢出 */
        new_capacity *= 2;

        if (new_capacity > (size_t)BUFFER_MAX_CAPACITY)
        {
            new_capacity = (size_t)BUFFER_MAX_CAPACITY;
            break;
        }
    }

    if (new_capacity < required)
        return -1;

    return buffer_expand(buf, new_capacity);
}

int buffer_append(struct buffer *buf, const void *data, size_t len)
{
    if (!buffer_is_valid(buf))
        return -1;
    if (!data && len != 0)
        return -1;

    /* 先做上限/溢出检查，保证失败时不改变状态 */
    if (len != 0 && buf->write_index > (size_t)BUFFER_MAX_CAPACITY - len)
        return -1;

    if (buffer_ensure_writable_bytes(buf, len) != 0)
        return -1;

    if (len != 0)
    {
        memcpy(buf->data + buf->write_index, data, len);
        buf->write_index += len;
        if (!buffer_is_valid(buf))
            return -1;
    }

    return 0;
}

void buffer_retrieve(struct buffer *buf, size_t len)
{
    if (!buffer_is_valid(buf))
        return;

    size_t readable = buffer_readable_bytes(buf);
    if (len >= readable)
    {
        buffer_retrieve_all(buf);
        return;
    }

    buf->read_index += len;
    (void)buffer_is_valid(buf);
}

void buffer_retrieve_all(struct buffer *buf)
{
    if (!buffer_is_valid(buf))
        return;

    buf->read_index = 0;
    buf->write_index = 0;
    (void)buffer_is_valid(buf);
}

const char *buffer_peek(const struct buffer *buf)
{
    if (!buffer_is_valid(buf))
        return NULL;
    return buf->data + buf->read_index;
}

ssize_t buffer_read_fd(struct buffer *buf, int fd, int *saved_errno)
{
    if (!buffer_is_valid(buf))
    {
        if (saved_errno)
            *saved_errno = EINVAL;
        return -1;
    }

    ssize_t total = 0;

    for (;;)
    {
        char tmp[8192];

        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n > 0)
        {
            if (buffer_append(buf, tmp, (size_t)n) != 0)
            {
                if (saved_errno)
                    *saved_errno = ENOMEM;
                return -1;
            }
            total += n;
            continue;
        }

        if (n == 0)
        {
            /* EOF：对端关闭写端 */
            if (saved_errno)
                *saved_errno = 0; /* 明确表示 EOF/无错误 */
            return total;
        }

        /* n < 0 */
        if (errno == EINTR)
        {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            /* 非阻塞：当前无更多数据
             * 如果 total==0，需要告诉上层这是“EAGAIN”，避免被误判为 EOF
             */
            if (saved_errno)
            {
                *saved_errno = (total == 0) ? EAGAIN : 0;
            }
            break;
        }

        if (saved_errno)
            *saved_errno = errno;
        return -1;
    }

    return total;
}