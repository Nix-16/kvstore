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

## 求职项目扩展建议

以下扩展项按**优先级**排列，做完前几项即可在简历/面试中把「设计取舍、持久化、可观测性、工程化」讲清楚；有余力再做深度扩展。

### 高优先级（简历亮点 + 面试必问）

| 扩展项 | 说明 | 面试可讲点 |
|--------|------|------------|
| **设计文档** | 新增 `docs/`：架构图、持久化格式（AOF/快照）、恢复顺序与一致性、三种结构的选型与复杂度 | 为什么先 snapshot 再 AOF？array/hash/rbtree 的 trade-off |
| **INFO 命令** | 返回服务端基本信息：版本、运行时长、键数量（三空间分别或合计）、内存占用（可选）、AOF/快照状态 | 可观测性、简单运维 |
| **优雅退出** | SIGTERM/SIGINT 时先停止接受新请求，执行一次 SAVE（或至少 fsync AOF），再退出 | 生产可用性、数据安全 |
| **基准数据上 README** | 用现有 `bench_kvstore.py` 跑一轮，把 QPS、延迟（P99）写进 README | 用数据说话，体现性能意识 |

### 中优先级（工程化 + 可信度）

| 扩展项 | 说明 | 面试可讲点 |
|--------|------|------------|
| **CI** | GitHub Actions：`make test`、可选 `make all`，PR 时自动跑 | 工程习惯、回归保障 |
| **恢复正确性测试** | 写数据 → SAVE / 停服务 → 重启 → 校验 GET 结果一致 | 持久化与恢复的可靠性 |
| **Dockerfile** | 基于 Alpine/Ubuntu 构建并运行 `kvstore_server`，方便他人一键跑 | 交付与部署 |
| **日志与错误码** | 关键路径打日志（启动、加载 AOF/快照、命令错误），或统一错误码便于排查 | 可运维性 |

### 深度扩展（选做，突出差异化）

| 扩展项 | 说明 | 面试可讲点 |
|--------|------|------------|
| **实现 proactor / ntyco** | 按配置切换网络模型，与现有 reactor 对比压测 | IO 多路复用、异步模型 |
| **TTL / EXPIRE** | 键级过期，惰性删除或定时扫描 + 写入 AOF | 数据结构、持久化如何表达 TTL |
| **多线程** | 多 reactor 或 IO 线程 + 工作线程池执行命令 | 锁、无锁、线程模型 |
| **更多命令** | KEYS pattern、MGET/MSET、或某命名空间的 SCAN | 协议与实现复杂度 |

### 可选（锦上添花）

- **clang-format / 代码规范**：统一风格，便于协作与展示。
- **README 性能小节**：单独一节贴 benchmark 结果与测试环境（CPU、内存、并发数）。
- **简短「项目亮点」列表**：在 README 开头用 3～5 条概括，方便面试官快速抓重点。

建议顺序：先做「设计文档 + INFO + 优雅退出 + README 基准数据」，再上 CI 与恢复测试，最后按兴趣选做 proactor/ntyco 或 TTL。
