#ifndef RESP_H
#define RESP_H

#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RESP_MAX_ARGC 16

struct resp_cmd {
    int argc;
    char *argv[RESP_MAX_ARGC];     /* malloc 出来的 C 字符串 */
    int  argv_len[RESP_MAX_ARGC];
};

/* 返回值：
 *  1：成功解析出一条命令，并消费 input
 *  0：数据不够（半包），不消费
 * -1：协议错误
 */
int resp_try_parse(struct buffer *in, struct resp_cmd *cmd);

/* 释放 argv */
void resp_cmd_free(struct resp_cmd *cmd);

#ifdef __cplusplus
}
#endif

#endif