#!/usr/bin/env python3
import asyncio
import os
import random
import statistics
import time
from dataclasses import dataclass

HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", "6380"))

# 例：OPS=HSET,HGET 或 OPS=RSET,RGET 或 OPS=SET,GET
OPS = [x.strip().upper() for x in os.getenv("OPS", "SET,GET").split(",") if x.strip()]
CONNS = int(os.getenv("CONNS", "100"))          # 并发连接数（连接=客户端）
DURATION = float(os.getenv("DURATION", "10"))   # 压测时长（秒）
PIPELINE = int(os.getenv("PIPELINE", "16"))     # pipeline 深度（每次写入的请求条数）
KEYSPACE = int(os.getenv("KEYSPACE", "100000")) # key 空间大小
VALUE_LEN = int(os.getenv("VALUE_LEN", "16"))   # value 长度
READ_RATIO = float(os.getenv("READ_RATIO", "0.5"))  # GET 类操作比例（0~1）

def resp_array(parts):
    """Encode RESP Array of Bulk Strings."""
    out = [f"*{len(parts)}\r\n".encode()]
    for p in parts:
        b = p.encode()
        out.append(f"${len(b)}\r\n".encode())
        out.append(b)
        out.append(b"\r\n")
    return b"".join(out)

def rand_value(n=VALUE_LEN):
    alphabet = "abcdefghijklmnopqrstuvwxyz0123456789"
    return "".join(random.choice(alphabet) for _ in range(n))

async def read_one_reply(reader: asyncio.StreamReader):
    """
    Minimal RESP reply reader:
      +simple\r\n
      -error\r\n
      :int\r\n
      $len\r\ndata\r\n  (len can be -1)
    """
    first = await reader.readexactly(1)
    if first in (b'+', b'-', b':'):
        await reader.readuntil(b"\r\n")
        return
    if first == b'$':
        line = await reader.readuntil(b"\r\n")
        n = int(line[:-2])
        if n == -1:
            return
        await reader.readexactly(n + 2)  # data + \r\n
        return
    if first == b'*':
        # 目前你的 server 不会返回 array；简单跳过 header 行
        await reader.readuntil(b"\r\n")
        return
    # fallback: read line
    await reader.readuntil(b"\r\n")

def pick_op():
    """根据 READ_RATIO 在 SET/GET 或 HSET/HGET 或 RSET/RGET 中选"""
    # 若用户传的 OPS 里包含 >2 个，随机选其一；否则按 READ_RATIO 在 GET/SET 对里选
    if len(OPS) >= 3:
        return random.choice(OPS)

    # 常见两元组：SET/GET, HSET/HGET, RSET/RGET
    if len(OPS) == 2:
        a, b = OPS[0], OPS[1]
        # 让含 GET 的那个按 READ_RATIO 出现
        if "GET" in a and "GET" not in b:
            return a if random.random() < READ_RATIO else b
        if "GET" in b and "GET" not in a:
            return b if random.random() < READ_RATIO else a
        # 若都不是 GET（比如 DEL/EXISTS），就均匀随机
        return random.choice([a, b])

    return OPS[0] if OPS else "PING"

@dataclass
class Stats:
    ops: int
    lat_ms: list

async def worker(worker_id: int, deadline: float, stats: Stats):
    reader, writer = await asyncio.open_connection(HOST, PORT)
    try:
        # warmup ping
        writer.write(resp_array(["PING"]))
        await writer.drain()
        await read_one_reply(reader)

        while time.perf_counter() < deadline:
            batch = []
            # 构建 pipeline 批
            for _ in range(PIPELINE):
                op = pick_op()
                k = f"k{random.randrange(KEYSPACE)}"
                if op.endswith("SET"):
                    v = rand_value()
                    batch.append(resp_array([op, k, v]))
                elif op.endswith("GET") or op.endswith("DEL") or op.endswith("EXISTS"):
                    batch.append(resp_array([op, k]))
                else:
                    # 兜底
                    batch.append(resp_array(["PING"]))

            payload = b"".join(batch)

            t0 = time.perf_counter()
            writer.write(payload)
            await writer.drain()

            for _ in range(PIPELINE):
                await read_one_reply(reader)

            t1 = time.perf_counter()

            stats.ops += PIPELINE
            stats.lat_ms.append((t1 - t0) * 1000.0 / PIPELINE)

    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass

def pct(sorted_lats, p):
    if not sorted_lats:
        return float("nan")
    i = int((p / 100.0) * (len(sorted_lats) - 1))
    return sorted_lats[i]

async def main():
    random.seed(42)

    deadline = time.perf_counter() + DURATION
    all_stats = [Stats(ops=0, lat_ms=[]) for _ in range(CONNS)]

    tasks = [asyncio.create_task(worker(i, deadline, all_stats[i])) for i in range(CONNS)]
    await asyncio.gather(*tasks)

    total_ops = sum(s.ops for s in all_stats)
    qps = total_ops / DURATION

    lats = [x for s in all_stats for x in s.lat_ms]
    lats.sort()

    print(f"HOST={HOST} PORT={PORT}")
    print(f"OPS={OPS} READ_RATIO={READ_RATIO}")
    print(f"CONNS={CONNS} PIPELINE={PIPELINE} DURATION={DURATION}s KEYSPACE={KEYSPACE} VALUE_LEN={VALUE_LEN}")
    print(f"Total ops: {total_ops}")
    print(f"QPS: {qps:.2f}")
    if lats:
        print(
            "Latency(ms): "
            f"avg={statistics.mean(lats):.4f} "
            f"p50={pct(lats,50):.4f} "
            f"p95={pct(lats,95):.4f} "
            f"p99={pct(lats,99):.4f} "
            f"max={lats[-1]:.4f}"
        )

if __name__ == "__main__":
    asyncio.run(main())