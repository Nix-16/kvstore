# kvstore 设计文档

本文档描述 kvstore 的总体架构、持久化格式、恢复流程与数据结构选型，便于维护与面试讲解。

---

## 1. 总体架构

```
                    ┌─────────────────────────────────────────────────┐
                    │                   kvstore_server                 │
                    │  (kvstore.c: main, handle_cmd, kvs_on_message)  │
                    └─────────────────────┬───────────────────────────┘
                                          │
         ┌────────────────────────────────┼────────────────────────────────┐
         │                                │                                │
         ▼                                ▼                                ▼
┌─────────────────┐            ┌─────────────────┐            ┌─────────────────┐
│     reactor      │            │   RESP 解析/回复  │            │    kvs_config    │
│  (epoll, 非阻塞)  │            │  resp.c / resp_  │            │   kvs.conf 解析   │
│  单 listen_fd    │            │  reply.c        │            │   bind/port/AOF  │
└────────┬────────┘            └────────┬────────┘            └─────────────────┘
         │                              │
         │    connection (buffer in/out) │
         └──────────────────────────────┘
                          │
                          ▼
         ┌─────────────────────────────────────────────────┐
         │              三种存储命名空间（独立）              │
         │  global_array   global_hash   global_rbtree      │
         │  (SET/GET/...)  (HSET/HGET)   (RSET/RGET)       │
         └─────────────────────────────────────────────────┘
                          │
         ┌────────────────┼────────────────┐
         ▼                ▼                ▼
   kvs_array.c      kvs_hash.c      kvs_rbtree.c
   (线性表+洞位)     (链式哈希)       (红黑树)
         │                │                │
         └────────────────┼────────────────┘
                          │
                          ▼
                  kvs_alloc.c (system / jemalloc)
```

- **网络**：单线程 Reactor + epoll，每个连接维护 `buffer` 收发包；协议为 RESP，解析出命令后进入 `handle_cmd` 分发。
- **存储**：三套命名空间互不共享 key，各自一套 SET/GET/DEL/EXISTS 语义；写操作在非 AOF 加载阶段会追加到 AOF。
- **配置**：`kvs.conf` 由 `kvs_config.c` 解析，支持 bind、port、allocator、AOF、快照等；`network` 目前仅使用 reactor，proactor/ntyco 待实现。

---

## 2. 持久化

### 2.1 AOF（Append Only File）

- **格式**：与 Redis 类似，每条写命令以 **RESP 数组** 形式追加到文件末尾。
  - 例如一次 `SET k v` 写入：`*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n`
  - 支持的命令：SET、DEL、HSET、HDEL、RSET、RDEL（与当前写路径一致）。
- **fsync 策略**（由配置 `appendfsync` 决定）：
  - `always`：每次追加后立即 `fsync`，耐久性最强，延迟最高。
  - `everysec`：后台每秒调用一次 `fsync`（在 reactor 事件循环中通过 `kvs_aof_maybe_fsync` 触发）。
  - `no`：由内核决定刷盘，性能最好，宕机可能丢一段数据。
- **回放**：启动时 `kvs_aof_load()` 将整个 AOF 读入内存 buffer，再复用 RESP 解析器逐条解析并**直接调用** `kvs_set` / `kvs_del` 等（不经过网络）。回放期间 `g_loading_aof = 1`，写操作不会再次追加到 AOF，避免重复与死循环。

### 2.2 快照（Snapshot）

- **格式**：二进制文件，与 AOF 独立。
  - **文件头**（固定长度）：
    - `magic[4]` = `"KVS1"`
    - `version`（uint32）= 1
    - `array_count` / `hash_count` / `rbtree_count`（uint32）：三个命名空间各自的键数量
  - **数据区**：按 array → hash → rbtree 顺序，每个键值对为：
    - `klen`（uint32）、`vlen`（uint32）、`key`（klen 字节）、`value`（vlen 字节），无分隔符。
- **写入**：`kvs_snapshot_save()` 先写临时文件 `snapshot_file.tmp`，遍历三套结构写入所有 key/value，然后 `fflush` + `fsync`，最后 `rename` 为正式 `snapshot_file`，保证崩溃时不会出现半写文件。若目标快照文件已存在，此次 SAVE 会**原子覆盖**（先写 .tmp 再 rename，不破坏原有文件直到新快照写全）。
- **读取**：`kvs_snapshot_load()` 校验 magic 与 version 后，按 `array_count` / `hash_count` / `rbtree_count` 依次 `read_one_kv` 并调用 `kvs_array_set` / `kvs_hash_set` / `kvs_rbtree_set` 写回内存。

### 2.3 恢复顺序与一致性

- **启动顺序**（`main` 中）：
  1. 加载配置 → 选择 allocator → `kvs_init()` 初始化三套空结构。
  2. **先** `kvs_snapshot_load()`：若快照存在且合法，内存已恢复到**做快照那一刻**的状态；若文件不存在则返回 1，视为正常（首次启动）。
  3. **再** `kvs_aof_init()` + `kvs_aof_load()`：在快照基础上按顺序回放 AOF，得到**快照时间点之后**的增量，从而得到当前一致状态。
- **为什么先快照再 AOF**：快照是某一时刻的全量，AOF 是该时刻之后的写操作日志。先恢复全量再重放增量，与 Redis 的 RDB + AOF 组合思路一致；若顺序反了，快照会覆盖掉 AOF 中已恢复的更新，导致数据倒退。
- **SAVE 命令**：执行 `kvs_snapshot_save()` 生成新快照，再 `kvs_aof_reset()` 清空/截断 AOF。这样下次启动只需加载该快照即可，AOF 从空开始重新积累，避免 AOF 无限增长（当前实现下 SAVE 后 AOF 被重置，具体行为以代码为准）。

---

## 3. 三种存储结构选型与复杂度

| 结构       | 实现           | SET/HSET/RSET | GET/HGET/RGET | DEL/... | EXISTS/... | 遍历/COUNT | 适用场景           |
|------------|----------------|---------------|---------------|---------|------------|------------|--------------------|
| **Array**  | 线性表 + 洞位复用 | O(n) 查找 + O(1) 写洞位 | O(n)         | O(n)     | O(n)       | O(n)       | 小数据、简单、实现简单 |
| **Hash**   | 链式哈希（固定槽数） | O(1) 均摊     | O(1) 均摊     | O(1) 均摊 | O(1) 均摊  | O(n)       | 通用 KV，大 key 空间 |
| **RBTree** | 红黑树         | O(log n)      | O(log n)      | O(log n) | O(log n)   | O(n)       | 需要范围/有序时可扩展 |

- **Array**：`kvs_array` 使用连续数组 + 洞位标记，插入时复用空位，查找/删除为线性扫描。实现简单，适合键数较少或对性能要求不高的命名空间。
- **Hash**：`kvs_hash` 固定槽数（如 1024），冲突用链表拉链。平均 O(1)，最坏 O(n)。适合默认需要较好读写性能的场景。
- **RBTree**：`kvs_rbtree` 按 key 字典序有序，便于后续扩展范围查询（如 RANGE/SCAN）。当前只暴露点查，但结构上已支持有序遍历。

**设计取舍**：三套命名空间隔离，避免 key 冲突，且可针对不同业务选不同结构；同一进程内无多线程写，无需在存储层加锁。

---

## 4. 网络与请求路径

- **Reactor**：单线程 epoll，监听 `listen_fd` 与所有客户端 fd；可读时从 `connection->in` 读入，交给 `kvs_on_message`。
- **请求路径**：`kvs_on_message` 内循环调用 `resp_try_parse` 解析出一条完整 RESP 命令 → `handle_cmd` 根据 `argv[0]` 分发到对应 `kvs_*` 接口 → 通过 `resp_reply_*` 写入 `connection->out`，由 reactor 在可写时发回客户端。
- **Proactor / ntyco**：配置中已解析，当前仅使用 reactor；其余两种模型待实现，计划作为可选网络层替换。

---

## 5. 配置与可观测性

- **配置**：见 README 中 kvs.conf 说明；`network`、`allocator`、AOF、快照等均在 `kvs_config.c` 中解析并存入 `global_config`。
- **可观测性**：当前无内置 INFO 命令与指标；建议后续增加 `INFO` 返回运行时长、三空间键数、AOF/快照状态等，便于运维与面试展示。

---

