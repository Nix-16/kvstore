# kvstore

基于 C 语言实现的内存 KV 存储服务，使用 [RESP](https://redis.io/docs/reference/protocol-spec/) 协议与客户端通信，支持多种底层数据结构与持久化。

## 功能概览

- **三种独立命名空间**：默认（数组）、Hash（哈希表）、RBTree（红黑树），各自一套 SET/GET/DEL/EXISTS 风格命令
- **持久化**：AOF 增量日志 + 全量快照（snapshot），启动时先加载快照再回放 AOF
- **可选 jemalloc**：通过配置选择系统分配器或 jemalloc
- **Reactor 网络模型**：当前仅实现 reactor；proactor / ntyco 已在配置中预留，待后续实现

详细架构与持久化设计见 **[docs/design.md](docs/design.md)**。

## 依赖与编译

- 需要 **gcc**、**make**
- jemalloc 以 git submodule 形式位于 `third_party/jemalloc`，首次编译会自动构建

```bash
# 若有 submodule，先初始化
git submodule update --init --recursive

# 编译（会先构建 jemalloc，再编译 lib + 服务端）
make all

# 生成：bin/kvstore_server、build/libkvstore.a
```

## 运行

```bash
# 使用默认配置 kvs.conf（端口 6380）
make run-kvs
# 或
./bin/kvstore_server
```

服务默认监听 `0.0.0.0:6380`，使用前请确保同目录下存在 `kvs.conf`（可复制仓库自带的 `kvs.conf`）。

## 配置说明（kvs.conf）

| 配置项 | 说明 | 示例 |
|--------|------|------|
| `bind` | 监听地址 | `0.0.0.0` |
| `port` | 监听端口 | `6380` |
| `network` | 网络模型：`reactor`（已实现）；`proactor`、`ntyco` 待实现 | `reactor` |
| `allocator` | 内存分配器：`system`、`jemalloc` | `jemalloc` |
| `appendonly` | 是否开启 AOF | `yes` / `no` |
| `appendfilename` | AOF 文件名 | `appendonly.aof` |
| `appendfsync` | fsync 策略：`always`、`everysec`、`no` | `everysec` |
| `snapshot_file` | 快照文件名 | `dump.kvs` |
| `snapshot_enabled` | 是否启用快照 | `yes` / `no` |

## 支持的命令（RESP）

- **通用**：`PING`、`PING <msg>`；`SAVE`（做一次全量快照并重置 AOF）
- **默认命名空间（数组）**：`SET key value`、`GET key`、`DEL key`、`EXISTS key`
- **Hash 命名空间**：`HSET key value`、`HGET key`、`HDEL key`、`HEXISTS key`
- **RBTree 命名空间**：`RSET key value`、`RGET key`、`RDEL key`、`REXISTS key`

命令与参数错误时会返回 RESP 错误回复；未知命令返回 `unknown command`。

## 持久化与恢复

- **AOF**：写命令（SET/DEL、HSET/HDEL、RSET/RDEL）在非加载阶段会追加到 AOF；`appendfsync` 控制刷盘策略
- **快照**：`SAVE` 将当前内存数据写入 `snapshot_file`，并调用 `kvs_aof_reset()` 清空/重置 AOF，便于下次启动以快照为主
- **启动顺序**：先 `kvs_snapshot_load()`，再 `kvs_aof_load()` 回放 AOF
- **优雅退出**：收到 SIGINT/SIGTERM 后停止接受新请求，若启用快照则执行一次 SAVE（快照 + 重置 AOF），关闭前对 AOF 做一次 fsync 再退出，避免丢数据

## 测试

```bash
# 单元测试（buffer、array、hash、rbtree）
make test

# 集成压测（需先启动 bin/kvstore_server）
# 默认：SET/GET，10 秒，100 连接，pipeline 16
python3 test/intergration/bench_kvstore.py

# 可选环境变量示例
OPS=HSET,HGET CONNS=50 DURATION=5 python3 test/intergration/bench_kvstore.py
```

## 性能基准（参考）

以下由 `test/intergration/bench_kvstore.py` 在本机一次跑测得出，**仅供参考**；实际 QPS 与 CPU、磁盘、`kvs.conf`、系统负载强相关。

### 测试环境

| 项目 | 说明 |
|------|------|
| 机器 | Linux 5.15，**2 核**，**aarch64**，客户端与服务端同机 **`127.0.0.1` 回环** |
| 构建 | `make all`（`-O2 -g`） |
| 配置 | 默认 `kvs.conf`：**jemalloc**，AOF 开启，`appendfsync everysec`，快照开启 |
| 压测参数 | `CONNS=100`，`PIPELINE=16`，`DURATION=15`（秒），`READ_RATIO=0.5`，`KEYSPACE=100000`，`VALUE_LEN=16` |

### 结果（读写各约一半）

| 命令空间 | QPS（约） | 延迟 avg / P50 / P99（ms） |
|----------|-----------|-----------------------------|
| `SET` / `GET`（数组） | 25.5 万 | 0.39 / 0.38 / 0.48 |
| `HSET` / `HGET`（哈希） | 12.8 万 | 0.78 / 0.71 / 1.64 |
| `RSET` / `RGET`（红黑树） | 14.4 万 | 0.69 / 0.68 / 1.05 |

说明：**延迟**为 pipeline 批内摊薄后的单条平均耗时（整批耗时 ÷ `PIPELINE`），与单条命令串行往返时间不同。若主要对比内存与协议栈，可临时将 `appendonly` 设为 `no` 再测一轮。

## 清理

```bash
make clean   # 删除 build/、bin/、appendonly.aof、dump.kvs
```

## 项目结构（简要）

- `kvstore.c` / `kvstore.h`：服务入口、命令分发、main
- `src/`：reactor、RESP 解析与回复、buffer；kvs_array / kvs_hash / kvs_rbtree；kvs_config、kvs_aof、kvs_snapshot、kvs_alloc
- `docs/design.md`：**设计文档**（架构、持久化格式、恢复顺序、三种结构选型与复杂度）
- `test/unit/`：单元测试；`test/intergration/`：Python 压测脚本
- `kvs.conf`：默认配置文件

## 待实现 / 说明

- **网络模型**：配置中的 `proactor`、`ntyco` 已解析，但服务端目前仅使用 **reactor**，其余两种待后续实现。
- 无集群、主从、ACL、多 DB 等能力，定位为单机内存 KV + 持久化。

---