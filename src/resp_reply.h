#ifndef RESP_REPLY_H
#define RESP_REPLY_H

#include <stddef.h>
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
 * RESP 回包工具（写入 out buffer）
 * 约定：函数成功返回 0，失败返回 <0
 * 失败的原因通常是 buffer_append/扩容失败
 * ========================= */

/* +OK\r\n、+PONG\r\n 这种简单字符串 */
int resp_reply_simple(struct buffer *out, const char *s);

/* -ERR xxx\r\n 这种错误字符串（函数内部会自动加 "ERR " 前缀更像 Redis） */
int resp_reply_error(struct buffer *out, const char *msg);

/* :123\r\n 整数回复（DEL/EXISTS 等常用） */
int resp_reply_integer(struct buffer *out, long long v);

/* $<len>\r\n<data>\r\n
 * data 允许包含 '\0'（二进制安全），len 必须由调用者提供
 */
int resp_reply_bulk(struct buffer *out, const void *data, size_t len);

/* $-1\r\n 空 Bulk（Redis 用于 GET miss） */
int resp_reply_nil(struct buffer *out);

/* *<n>\r\n 仅写数组头（后续你可以继续写 bulk/integers 组合）
 * 用于返回数组/多元素（如 MGET、LRANGE 等）
 */
int resp_reply_array_header(struct buffer *out, long long n);

#ifdef __cplusplus
}
#endif

#endif /* RESP_REPLY_H */