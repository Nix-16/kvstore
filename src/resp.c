#include "resp.h"
#include "kvs_alloc.h"

#include <string.h>
#include <stdlib.h>

/* 在 [p, p+n) 中找 "\r\n"，返回位置偏移，找不到返回 -1 */
static long find_crlf(const char *p, size_t n)
{
    if (!p || n < 2) return -1;
    for (size_t i = 0; i + 1 < n; i++) {
        if (p[i] == '\r' && p[i + 1] == '\n') return (long)i;
    }
    return -1;
}

static int parse_long(const char *s, size_t len, long *out)
{
    if (!s || !out) return -1;
    /* 复制到临时 buffer，保证 \0 结尾 */
    char tmp[64];
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, s, len);
    tmp[len] = '\0';

    char *end = NULL;
    long v = strtol(tmp, &end, 10);
    if (end == tmp || *end != '\0') return -1;
    *out = v;
    return 0;
}

void resp_cmd_free(struct resp_cmd *cmd)
{
    if (!cmd) return;
    for (int i = 0; i < cmd->argc; i++) {
        if (cmd->argv[i]) kvs_free(cmd->argv[i]);
        cmd->argv[i] = NULL;
        cmd->argv_len[i] = 0;
    }
    cmd->argc = 0;
}

/* 解析一条 RESP Array-of-BulkStrings 命令：
 * - 不够数据：返回 0（不消费）
 * - 够：分配 argv 并消费
 */
int resp_try_parse(struct buffer *in, struct resp_cmd *cmd)
{
    if (!in || !cmd) return -1;

    resp_cmd_free(cmd);

    size_t nread = buffer_readable_bytes(in);
    if (nread == 0) return 0;

    const char *p = buffer_peek(in);
    if (!p) return 0;

    /* 必须以 '*' 开头（暂不支持 inline command） */
    if (p[0] != '*') return -1;

    /* 先解析第一行：*<argc>\r\n（只 peek，不消费） */
    long pos = find_crlf(p, nread);
    if (pos < 0) return 0; /* 没有完整行 */

    /* 行内容为 p[0..pos-1]，其中 p[0]=='*' */
    if (pos < 2) return -1;
    long argc = 0;
    if (parse_long(p + 1, (size_t)pos - 1, &argc) != 0) return -1;
    if (argc <= 0 || argc > RESP_MAX_ARGC) return -1;

    /* 从 offset 开始扫描后续元素，判断是否够一整条命令 */
    size_t off = (size_t)pos + 2; /* 跳过 "*n\r\n" */

    for (long i = 0; i < argc; i++) {
        if (off >= nread) return 0;

        /* 解析 $<len>\r\n */
        if (p[off] != '$') return -1;

        long pos2 = find_crlf(p + off, nread - off);
        if (pos2 < 0) return 0; /* 还没完整一行 */

        /* '$' 后面是长度 */
        if (pos2 < 2) return -1;
        long blen = 0;
        if (parse_long(p + off + 1, (size_t)pos2 - 1, &blen) != 0) return -1;
        if (blen < 0) return -1; /* 命令参数先不接受 $-1 */

        off += (size_t)pos2 + 2; /* 跳过 "$len\r\n" */

        /* 需要 data + "\r\n" */
        size_t need = (size_t)blen + 2;
        if (nread - off < need) return 0;

        /* data 末尾必须是 \r\n */
        if (p[off + (size_t)blen] != '\r' || p[off + (size_t)blen + 1] != '\n') return -1;

        off += need; /* 跳过 data + "\r\n" */
    }

    /* 到这里说明：buffer 里够一整条命令，开始真正分配 argv 并消费 */
    cmd->argc = (int)argc;

    /* 重新按同样方式解析一遍，但这次生成 argv */
    size_t consume = 0;
    /* 先消费第一行 */
    consume = (size_t)pos + 2;

    size_t off2 = consume;
    for (int i = 0; i < cmd->argc; i++) {
        /* $len 行 */
        long pos3 = find_crlf(p + off2, nread - off2);
        /* 这里一定存在（前面检查过），省略 0 分支 */
        long blen = 0;
        if (parse_long(p + off2 + 1, (size_t)pos3 - 1, &blen) != 0) return -1;
        off2 += (size_t)pos3 + 2; /* 跳过 "$len\r\n" */

        char *arg = (char *)kvs_malloc((size_t)blen + 1);
        if (!arg) {
            resp_cmd_free(cmd);
            return -1;
        }
        memcpy(arg, p + off2, (size_t)blen);
        arg[blen] = '\0';

        cmd->argv[i] = arg;
        cmd->argv_len[i] = (int)blen;

        off2 += (size_t)blen + 2; /* data + "\r\n" */
    }

    /* 一次性消费整条命令 */
    buffer_retrieve(in, off2);
    return 1;
}