# Reactor 设计与实现思路（epoll + Buffer）

本项目实现一个**单线程、基于 epoll(LT) 的 Reactor 网络框架**，并将网络 I/O 与业务逻辑（例如 RESP/KV 命令处理）解耦。目标是：网络层负责 **accept/读写/事件分发/连接管理**，业务层只负责 **解析字节流并生成响应**。

---

## 设计目标

- **单线程事件循环**：所有连接由一个 epoll loop 管理
- **非阻塞 I/O**：listenfd / clientfd 全部设置为 non-blocking，避免卡死事件循环
- **可扩展的输入/输出缓冲**：每个连接维护 `in/out` 动态 buffer
- **支持半包/粘包**：业务层采用增量解析（buffer 中数据不够则等待下一次 EPOLLIN）
- **正确处理部分写**：写到 EAGAIN，未写完的数据保留在 out buffer
- **按需订阅 EPOLLOUT**：仅当 out buffer 非空时才监听写事件，避免 CPU 空转
- **统一的连接关闭路径**：集中处理 epoll del / close / 回调 / 资源释放，减少泄漏和重复关闭

---

## 模块划分（职责边界）

### 网络层（Reactor）
网络层只负责：
- `epoll_wait()` 获取就绪事件
- `accept()` 新连接
- 读：循环读到 EAGAIN，将数据追加到连接的 `in buffer`
- 写：循环写到 EAGAIN，从连接的 `out buffer` 消费已发送字节
- 连接生命周期管理：创建 / 关闭 / 释放
- 事件订阅管理：EPOLLIN / EPOLLOUT 的 ADD/MOD/DEL

网络层**不解析协议、不执行 KV 逻辑**。

### 业务层（RESP/KV Handler）
业务层只负责：
- 从 `c->in` 中解析命令（增量解析：不够就返回等待更多数据）
- 执行 KV 操作（数组/哈希/红黑树等）
- 将响应写入 `c->out`（例如 `+OK\r\n`、`$-1\r\n` 等）

业务层**不直接操作 epoll，也不直接 recv/send**。

---

## 核心数据结构

### connection（每个连接一个）
每个连接对象绑定一个 socket fd，并携带输入输出缓冲与状态：

- `fd`：socket 文件描述符
- `in buffer`：输入缓冲，读事件把数据追加到这里
- `out buffer`：输出缓冲，业务层写响应到这里，写事件负责发送
- `events`：当前关注的 epoll 事件掩码（EPOLLIN/EPOLLOUT）
- `closed`：关闭标记
- `user_data`：业务层可挂自己的上下文（解析状态、会话等）

### fdmap（fd -> connection*）
使用动态数组 `conns[fd] = connection*` 做映射：
- 避免固定 `conn_list[1024]` 可能越界的问题
- fd 变大时 `realloc` 扩容，新增区域 `memset` 清零
- O(1) 查找与访问

---

## 事件循环与分发流程

### 主循环（reactor_run）
1. 调用 `epoll_wait()` 阻塞等待事件
2. 遍历就绪事件：
   - 若是 **listenfd**：处理 accept（循环 accept 到 EAGAIN）
   - 若是 **clientfd**：
     - 处理错误事件（EPOLLERR/EPOLLHUP/EPOLLRDHUP）→ 关闭连接
     - `EPOLLIN`：读数据到 `in buffer`，调用 `on_message`
     - `EPOLLOUT`：从 `out buffer` 发送数据，写完则关闭 EPOLLOUT

---

## accept 策略（handle_accept）

- listenfd 设置为 non-blocking
- 在一次 EPOLLIN 中反复 `accept()`：
  - accept 成功：设置 clientfd non-blocking，创建 connection，注册 EPOLLIN
  - accept 返回 EAGAIN/EWOULDBLOCK：说明本轮 accept 完成，退出循环
  - accept 被 EINTR 中断：继续 accept

这保证一次触发尽量“收割”当前积压的连接请求，减少 epoll 往返。

---

## 读策略：循环读到 EAGAIN（handle_read）

读事件的正确做法是：
- non-blocking `read()` 需要循环读取，直到：
  - `EAGAIN/EWOULDBLOCK`（内核收包缓冲读空）
  - 或 `read()==0`（对端关闭）
  - 或不可恢复错误（关闭连接）

读到的字节追加到 `in buffer`。

然后调用业务回调 `on_message(connection*)`：
- 业务层在 `in buffer` 上进行增量解析
- 成功解析命令后，消费已解析的字节：`buffer_retrieve(in, consumed)`
- 把响应写入 `out buffer`

---

## 写策略：支持部分写（flush_out_buffer）

写事件必须支持“部分写”：
- socket 可写不等于一次写完；`write()` 可能只写一部分
- 采用循环写：
  - 写成功 n 字节 → `buffer_retrieve(out, n)` 消费
  - EAGAIN/EWOULDBLOCK → 本轮写到头，等待下次 EPOLLOUT
  - 其他错误 → 关闭连接

这样可以保证：
- 不丢数据
- 慢客户端只会让自己的 out buffer 堆积，不会卡死整个 reactor

---

## 为什么只在 out 非空时监听 EPOLLOUT？

如果一直监听 EPOLLOUT：
- 大部分时间 socket 都“可写”
- epoll 会持续返回 EPOLLOUT → event loop 空转，CPU 飙升

因此采用按需订阅策略：
- 默认只监听 EPOLLIN
- 当 `out buffer` 非空时，打开 EPOLLOUT
- 当 `out buffer` 发送完毕后，关闭 EPOLLOUT，仅保留 EPOLLIN

这是高性能网络服务器的基本优化。

---

## 统一关闭路径（connection_close）

关闭连接涉及多步操作，应集中管理，避免资源泄漏与重复关闭：

1. epoll DEL
2. close(fd)
3. fdmap 清理：`conns[fd]=NULL`
4. 调用 `on_close` 通知业务层
5. 释放 connection 对象与 in/out buffer

统一关闭路径可显著降低 bug 密度。

---

## 与 RESP/KV 的结合方式（增量解析天然适配）

Reactor 只提供字节流；RESP 属于典型的“半包/粘包”场景：
- 每个连接维护 `in buffer`，未解析完的数据保留其中
- 业务回调 `on_message` 典型写法：

```c
while (try_parse_one_command(&c->in, &cmd)) {
    exec_kv(cmd);
    write_resp(&c->out, result);
}