#include "kvs_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    if (*s == 0)
        return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return s;
}

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static int parse_allocator(kvs_config_t *cfg, const char *v)
{
    if (streq(v, "system"))
        cfg->allocator = KVS_ALLOC_SYSTEM;
    else if (streq(v, "jemalloc"))
        cfg->allocator = KVS_ALLOC_JEMALLOC;
    else if (streq(v, "mypool"))
        cfg->allocator = KVS_ALLOC_MYPOOL;
    else
        return -1;
    return 0;
}

static int parse_network(kvs_config_t *cfg, const char *v)
{
    if (streq(v, "reactor"))
        cfg->network = KVS_NET_REACTOR;
    else if (streq(v, "proactor"))
        cfg->network = KVS_NET_PROACTOR;
    else if (streq(v, "ntyco"))
        cfg->network = KVS_NET_NTYCO;
    else
        return -1;
    return 0;
}

static int parse_appendonly(kvs_config_t *cfg, const char *v)
{
    if (streq(v, "yes"))
        cfg->appendonly = 1;
    else if (streq(v, "no"))
        cfg->appendonly = 0;
    else
        return -1;
    return 0;
}

static int parse_appendfsync(kvs_config_t *cfg, const char *v)
{
    if (streq(v, "no"))
        cfg->appendfsync = KVS_AOF_FSYNC_NO;
    else if (streq(v, "always"))
        cfg->appendfsync = KVS_AOF_FSYNC_ALWAYS;
    else if (streq(v, "everysec"))
        cfg->appendfsync = KVS_AOF_FSYNC_EVERYSEC;
    else
        return -1;
    return 0;
}

int kvs_config_load_file(kvs_config_t *cfg, const char *path)
{
    if (!cfg || !path)
        return -1;

    /* 默认值 */
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->bind_ip, sizeof(cfg->bind_ip), "%s", "127.0.0.1");
    cfg->port = 6380;
    cfg->allocator = KVS_ALLOC_SYSTEM;
    cfg->network = KVS_NET_REACTOR;
    cfg->appendonly = 0;
    snprintf(cfg->appendfilename, sizeof(cfg->appendfilename), "%s", "appendonly.aof");
    cfg->appendfsync = KVS_AOF_FSYNC_ALWAYS;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char line[512];
    int lineno = 0;

    while (fgets(line, sizeof(line), fp))
    {
        lineno++;

        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';

        char *p = trim(line);
        if (*p == '\0')
            continue;

        char *key = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            continue;
        *p++ = '\0';

        char *val = trim(p);
        if (*val == '\0')
            continue;

        if (streq(key, "bind"))
        {
            snprintf(cfg->bind_ip, sizeof(cfg->bind_ip), "%s", val);
        }
        else if (streq(key, "port"))
        {
            cfg->port = atoi(val);
        }
        else if (streq(key, "allocator"))
        {
            if (parse_allocator(cfg, val) != 0)
            {
                fclose(fp);
                return -2;
            }
        }
        else if (streq(key, "network"))
        {
            if (parse_network(cfg, val) != 0)
            {
                fclose(fp);
                return -3;
            }
        }
        else if (streq(key, "appendonly"))
        {
            if (parse_appendonly(cfg, val) != 0)
            {
                fclose(fp);
                return -4;
            }
        }
        else if (streq(key, "appendfilename"))
        {
            snprintf(cfg->appendfilename, sizeof(cfg->appendfilename), "%s", val);
        }
        else if (streq(key, "appendfsync"))
        {
            if (parse_appendfsync(cfg, val) != 0)
            {
                fclose(fp);
                return -5;
            }
        }
        else
        {
            /* 未识别配置项：先忽略 */
        }
    }

    fclose(fp);
    return 0;
}

// int kvs_config_load_file(kvs_config_t *cfg, const char *path)
// {
//     FILE *fp = fopen(path, "r");
//     if (!fp)
//         return -1;

//     char line[512];
//     int lineno = 0;

//     while (fgets(line, sizeof(line), fp))
//     {
//         lineno++;

//         // 去掉行内注释（# 后面）
//         char *hash = strchr(line, '#');
//         if (hash)
//             *hash = '\0';

//         char *p = trim(line);
//         if (*p == '\0')
//             continue;

//         // key value（中间允许多个空格/Tab）
//         char *key = p;
//         while (*p && !isspace((unsigned char)*p))
//             p++;
//         if (*p == '\0')
//             continue;
//         *p++ = '\0';

//         char *val = trim(p);
//         if (*val == '\0')
//             continue;

//         if (streq(key, "bind"))
//         {
//             snprintf(cfg->bind_ip, sizeof(cfg->bind_ip), "%s", val);
//         }
//         else if (streq(key, "port"))
//         {
//             cfg->port = atoi(val);
//         }
//         else if (streq(key, "allocator"))
//         {
//             if (parse_allocator(cfg, val) != 0)
//             {
//                 fclose(fp);
//                 return -2;
//             }
//         }
//         else if (streq(key, "network"))
//         {
//             if (parse_network(cfg, val) != 0)
//             {
//                 fclose(fp);
//                 return -3;
//             }
//         }
//         else
//         {
//             // 未识别 key：建议“忽略但可日志提示”，这里先忽略
//         }
//     }

//     fclose(fp);
//     return 0;
// }