#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <sys/types.h>

/*
 * 可增长字节缓冲区
 *
 * 内存布局（下标区间）：
 *   [0 ... read_index)              ：已消费/已丢弃的数据
 *   [read_index ... write_index)    ：当前可读数据（有效数据）
 *   [write_index ... capacity)      ：当前可写空间
 *
 * 说明：
 * - 本实现不是环形缓冲区。空间不足时会先“压缩”(memmove 把未读数据搬到开头)，
 *   若仍不足再扩容(realloc)。
 * - 为了防止慢客户端/恶意输入导致无限扩容，设置了最大容量上限。
 */

#define BUFFER_DEFAULT_SIZE 4096
#define BUFFER_MAX_CAPACITY (64u * 1024u * 1024u) /* 64MB 最大上限，可按需调整 */

/* 缓冲区对象 */
struct buffer {
    char   *data;        /* 分配的内存指针 */
    size_t  read_index;  /* 可读区起始下标 */
    size_t  write_index; /* 可读区结束下标/可写区起始下标 */
    size_t  capacity;    /* 当前已分配容量（字节） */
};

/* 初始化缓冲区，初始容量 initial_size（为 0 则使用默认值 BUFFER_DEFAULT_SIZE）
 * 返回：0 成功；-1 失败（参数/内存/超过上限等）
 */
int  buffer_init(struct buffer *buf, size_t initial_size);

/* 释放缓冲区内存并重置索引（可重复调用） */
void buffer_free(struct buffer *buf);

/* 返回当前可读字节数：write_index - read_index（参数非法返回 0） */
size_t buffer_readable_bytes(const struct buffer *buf);

/* 返回当前可写字节数：capacity - write_index（参数非法返回 0） */
size_t buffer_writable_bytes(const struct buffer *buf);

/* 确保至少有 len 字节可写空间，必要时压缩/扩容
 * 返回：0 成功；-1 失败（超过最大容量、内存不足、size_t 溢出、参数错误等）
 */
int buffer_ensure_writable_bytes(struct buffer *buf, size_t len);

/* 追加数据到缓冲区尾部
 * 返回：0 成功；-1 失败（参数错误/内存不足/超过上限等）
 */
int buffer_append(struct buffer *buf, const void *data, size_t len);

/* 消费（丢弃）前 len 字节可读数据（参数非法则不做任何事） */
void buffer_retrieve(struct buffer *buf, size_t len);

/* 丢弃所有可读数据，读写索引清零（参数非法则不做任何事） */
void buffer_retrieve_all(struct buffer *buf);

/* 返回指向当前可读数据起始位置的指针（只读视图；参数非法返回 NULL） */
const char *buffer_peek(const struct buffer *buf);

/* 从非阻塞 fd 读取数据并追加到缓冲区（循环读到 EAGAIN/EWOULDBLOCK）
 * 返回：
 *   >0  本次累计读取字节数
 *    0  EOF（对端关闭且累计为 0）或 EAGAIN（本次无数据且累计为 0）
 *  -1  出错（saved_errno 写入 errno）
 */
ssize_t buffer_read_fd(struct buffer *buf, int fd, int *saved_errno);

#endif 