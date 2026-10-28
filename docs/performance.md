# 低延迟与毫秒级尖峰治理

本文回答三个问题：延迟尖峰怎么**发现**、怎么**归因**、怎么**消除**，以及哪些做法看似有效但实测无效。
所有数字都可以用 `tools/loadgen`（新增 `--spike-log`）和下文命令复现。

## 结论摘要

1. 尖峰与 payload 大小、传输实现无关：64B 与 1KB 的 p99/max 处于同一水平。
2. 可消除的部分来自**线程调度**：绑核 + 内联分发后，均值降 40%~50%，p99 降 30%~50%，吞吐提升 60% 以上。
3. 残余尖峰（1~8ms）只占 0.003%~0.01%，随机分布、无固定周期，且**纯忙等程序在空载机器上也会出现**，属于 vCPU 抢占/宿主调度停顿，进程内无法根除，需要 cpuset 隔离与实时调度配合。
4. 在同步等待路径上加自旋**实测无效甚至更差**（p50 38us → 46~58us），因为投递回调的 poller 线程通常和调用方共享同一个核；该改法已验证后放弃。

## 测量方法

统一口径：loopback、TCP、1KB 请求/响应、`--window 1`（严格一收一发）、服务端 `bench_server`，
客户端/服务端分别绑到不同物理核；构建为 `CMAKE_BUILD_TYPE=Debug`（相对差异比绝对值更可信）。

```bash
# 服务端
taskset -c 0 build-linux/tools/bench_server --transport tcp --bind 127.0.0.1 --data-port 9001 \
  --mode static --io-threads 6 --handler-threads 2 --response-bytes 1024

# 客户端（--spike-log 会把超过阈值的样本连同发生时刻落盘）
taskset -c 2 build-linux/tools/loadgen --transport tcp --target 127.0.0.1:9001 \
  --mode static --service OrderService --method get --duration-seconds 30 \
  --payload-bytes 1024 --expect-response-bytes 1024 --window 1 \
  --spike-log spikes.csv --spike-threshold-us 1000 --output result.json
```

`result.json` 新增 `slow_samples`（超过 `--spike-threshold-us` 的样本数）与 `slow_threshold_us`，
便于门禁直接判定尾延迟；`spikes.csv` 记录 `t_us,latency_us`，用于判断尖峰是否周期性出现。

## 实测数据

5 秒一轮（同步 API 路径，1KB，Debug）：

| 配置 | QPS | mean | p50 | p99 | max |
| --- | --- | --- | --- | --- | --- |
| worker 线程池 + 不绑核 | 10,664 | 93.4us | 80us | 235us | 4.08ms |
| worker 线程池 + 绑核 | 14,955 | 66.7us | 50us | 172us | 2.30ms |
| 内联分发 + 绑核 | 18,135 | 54.9us | 44us | 130us | 3.39ms |

10 秒一轮（`--window 1` 异步路径，1KB，Debug）：

| 配置 | QPS | mean | p50 | p99 | p999 | 超过 1ms |
| --- | --- | --- | --- | --- | --- | --- |
| 内联 + 绑核 | 21,162 | 47.1us | 41us | 107us | 272us | 11 / 211,625（0.005%） |
| worker + 绑核 | 16,681 | 59.8us | 51us | 152us | 344us | 12 / 166,809（0.007%） |
| 内联 + 不绑核 | 18,173 | 54.8us | 51us | 141us | 348us | 7 / 181,736（0.004%） |

结论：换掉 worker 线程池可省一次跨线程唤醒，绑核可消除迁移与缓存抖动；两者叠加收益接近线性。

30 秒长跑（内联 + 绑核，1KB，默认 `TICK_INTERVAL_MS=2`）：

| 轮次 | QPS | mean | p99 | max | 超过 1ms |
| --- | --- | --- | --- | --- | --- |
| 第 1 轮 | 23,759 | 41.9us | 103us | 2.72ms | 26 / 712,770（0.0036%） |
| 第 2 轮 | 22,438 | 44.4us | 100us | 7.78ms | 37 / 673,130（0.0055%） |
| `TICK_INTERVAL_MS=20` | 22,727 | 43.8us | 98us | 3.62ms | 22 / 681,814（0.0032%） |

## 尖峰归因实验

| 实验 | 结果 | 说明 |
| --- | --- | --- |
| 64B vs 1KB | p99 296us vs 279us，max 2.3ms vs 2.6ms | payload 不是尖峰来源 |
| `TICK_INTERVAL_MS=20` vs 默认 2ms | 0.81/s vs 0.87/s、1.30/s | 定时器 tick 不是尖峰来源 |
| 30 秒窗口统计 | 0.0032%~0.0055% 样本 >1ms | 数量级稳定 |
| 周期性检验（相位集中度 + 蒙特卡洛 p 值） | 最高集中度仅 0.50~0.53（真周期应接近 1.0），p=0.37 / 0.63 | 无稳定周期，间隔分布接近泊松随机 |
| 纯忙等基线（无任何 RPC） | 60 秒内出现 2 次 ≥1ms 停顿（最坏 2.76ms） | 宿主自身存在调度停顿 |
| 限速 2000 QPS（30s） | 9 / 60,001 样本 >1ms（0.015%） | 尖峰按时间出现，与吞吐无关 |

倒数第二条是决定性证据：一个只做 100us 忙等的单线程程序（不涉及任何网络与 RPC），
在空载机器上 60 秒内同样出现了 1~3ms 的抢占停顿。
因此进程内再怎么优化热路径，也无法消除这部分尾延迟。

## 消除与验证

按收益从高到低：

1. **绑核/隔离 CPU**：`taskset -c` 或 cgroup `cpuset` 给 server 与 client 各自独占核；实测 QPS +40% 以上，p99 降 30% 左右。生产环境可进一步用 `isolcpus=` + `nohz_full=` 并配合 IRQ affinity。
2. **延迟敏感方法用内联分发**：`handler_threads=0`（或对应配置关闭 worker 池），省掉一次线程切换；实测 mean -18%，p99 -20%。
3. **提升调度优先级**：`SCHED_FIFO` + `mlockall` 可消除因 CFS 抢占造成的毫秒级停顿；需要 root，本次验证环境无此权限。
4. **用窗口换尾延迟**：`window>1` 时单次停顿只影响一帧，吞吐可到 8 万 QPS 以上，代价是单请求被排队放大。

### 已验证无效的做法

在 `nb_proxy_call_sync` 的 `ntk_sem_timedwait` 前加入 50us 有界自旋，期望省掉 futex 唤醒：

| 配置 | 无自旋 p50 / mean | 50us 自旋 p50 / mean |
| --- | --- | --- |
| 内联 + 单核绑核 | 44us / 54.9us | 58us / 69.5us |
| 内联 + 双核绑核 | 45us / 53.3us | 53us / 66.3us |

原因：同一进程内的 poller 线程负责投递完成事件，自旋会把同核（或同 cgroup 配额）上的 poller 线程饿住，唤醒反而更晚。
该改动已回滚，不在代码中保留。

## 复现与门禁

```bash
python3 tools/gate.py perf --perf-duration 5 --payload-bytes 1024
```

需要定位尖峰时，用 `loadgen --spike-log <csv> --spike-threshold-us 1000` 采集，再按 `t_us` 分析间隔分布与周期性。
门禁可以直接读取 `result.json` 里的 `slow_samples`，为尾延迟设置预算（例如 1KB/5s 场景不超过 10）。
