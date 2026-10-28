# 配置参考

network bridge framework 使用 JSON 配置。配置可以来自文件或内存字符串：

```c
nb_config_doc_t *cfg = NULL;
nb_config_error_t err;

int rc = nb_config_parse_file("server.json", &cfg, &err);
if (rc != NB_OK) {
    printf("path=%s reason=%s\n", err.path, err.reason);
    return rc;
}

rc = nb_node_create_from_config(cfg, &node);
if (rc == NB_OK) {
    const nb_node_config_t *parsed = nb_config_node(cfg);  /* 只读视图 */
    (void)parsed;
}

nb_config_doc_destroy(cfg);  /* 节点已复制所需字段，可以立即释放 */
```

对应接口：

- `nb_config_parse` / `nb_config_parse_file`（第三个参数 `nb_config_error_t *` 可为 NULL）
- `nb_node_create_from_config`
- `nb_config_node`（取只读的 `nb_node_config_t` 视图，用于检查解析结果）
- `nb_config_doc_destroy`（释放文档；必须与 `nb_config_parse*` 配对）

`nb_config_doc_t` 是不透明句柄，由 `nb_config_parse*` 分配并拥有解析后的字符串，不能按值
复制或在栈上构造。`nb_node_create_from_config` 会把节点需要的字段复制到节点内部，因此文档
只需活到该调用返回，随后即可销毁。已知字段类型错误、越界或 URI 非法时返回
`NB_ERR_INVALID`，传入 error 输出参数时会给出精确到字段的 `path`（如
`runtime.io_threads`、`routes[0].endpoint`、`transport.bindings[1].qos.depth`）和 `reason`。

未知字段按前向兼容策略忽略，但 `nb_config_parse` 会为每个未知字段打印一条
`unknown config key ignored: <path>` 警告，避免拼写错误静默生效。

字符串枚举值（`mode`、`transport.kind`、`discovery.mode`、`intra_process`、`log.level`、
QoS 取值）均为小写且区分大小写。

完整 JSON Schema 见 [`docs/config.schema.json`](../config.schema.json)。

## 最小服务端

```json
{
  "name": "echo-server",
  "mode": "static",
  "transport": {
    "bindings": ["tcp://127.0.0.1:42420"]
  },
  "runtime": {
    "io_threads": 2
  }
}
```

## 最小客户端

```json
{
  "name": "echo-client",
  "mode": "static",
  "routes": [
    { "service": "EchoService", "endpoint": "tcp://127.0.0.1:42420" }
  ]
}
```

## 顶层字段

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `name` | string | `"rpc"` | 节点名，最长 `NB_NAME_MAX - 1` |
| `mode` | string | `"mesh"` | `mesh` 或 `static`；`static` 等价于关闭服务发现 |
| `runtime` | object | 见下表 | 线程、并发与限流 |
| `transport` | object | 见下表 | 传输与监听端点 |
| `discovery` | object | 见下表 | 服务发现 |
| `metrics` | object | 见下表 | 指标采集 |
| `log` | object | 见下表 | 日志 |
| `routes` | array | 空 | 静态路由 |

## runtime

| 字段 | 类型 | 默认值 | 范围 | 说明 |
| --- | --- | ---: | --- | --- |
| `io_threads` | uint | 硬件并发数 | `0..8` | poller 线程数，上限 `NB_TRANSPORT_MAX_IO` |
| `handler_threads` | uint | `0` | `0..1024` | WORKER handler 线程数，`0` 表示关闭 |
| `handler_max_concurrency` | uint | `0` | `0..N` | worker/executor 未完成任务上限，`0` 表示不限 |
| `max_inflight` | uint | `65536` | `0..N` | 客户端未完成调用上限 |
| `max_streams` | uint | `64` | `0..64` | 并发接收流上限 |
| `peer_rate_limit_pps` | uint | `0` | `0..N` | 每对端每秒请求上限，`0` 表示不限 |
| `send_lanes` | uint | `0` | `0..64` | 专用发送通道线程数，与接收/分发的 io poller 隔离；`0` 表示关闭 |

`send_lanes > 0` 时用 `nb_node_post_send()` 提交发布循环，循环内
`nb_proxy_call_event()` 会走 inline 快路径，省掉每条事件的跨线程入队与唤醒；
适合高吞吐 pub 端。总线程数 = `io_threads + send_lanes`，可按需把一部分核从
`io_threads` 挪给 `send_lanes`。

未配置 `handler_threads` 时，WORKER 方法退化为 INLINE 并告警。`handler_max_concurrency`
超限时新请求返回 `NB_ERR_OVERLOAD` 或 `NB_ERR_AGAIN`（按调用路径）。

## transport

| 字段 | 类型 | 默认值 | 范围 | 说明 |
| --- | --- | --- | --- | --- |
| `kind` | string | `"udp"` | `udp` / `tcp` | 默认传输，仅在没有显式 `bindings` 时生效 |
| `bind_host` | string | `"0.0.0.0"` | — | 默认绑定地址，仅在没有显式 `bindings` 时生效 |
| `data_port` | uint | `0` | `0..65535` | 默认数据端口，`0` 表示自动分配 |
| `bindings` | array | 空 | 最多 8 项 | 监听端点列表，元素见下 |
| `frag_bytes` | uint | `1440` | `0` 或 `256..8192` | UDP 每分片字节；`0` 使用默认值 |
| `max_msg_bytes` | uint | `1048576` | `0..67108864` | 单条消息上限；不得小于 `frag_bytes` |
| `backpressure_bytes` | uint | `4194304` | `0..N` | 出站高水位，`0` 使用默认值 |
| `rtcp_interval_ms` | uint | `1000` | `0..N` | UDP RTCP 上报周期 |
| `disable_rtcp` | bool | `false` | — | `true` 关闭 RTCP |
| `qos` | object | 继承 | — | 默认 binding 的 provider QoS，字段见下 |

`bindings` 还支持 `intra_process` 字段（字符串：`off` / `auto` / `only`），
用于 DDS 事件发布订阅的进程内快路径。`only` 不创建 DDS event writer/reader，
直接把消息指针投递给同 provider 的本地订阅者；`auto` 本地投递后仍走 DDS。
每个 binding 也可以带自己的 `qos` 对象；未设置时继承 `transport.qos`。

`bindings` 还支持 `poll_us` 字段（uint，单位微秒），只对 `shm` provider 生效：
排空本地队列后继续自旋轮询这么久，空闲超时才退回阻塞等待。`0`（默认）始终阻塞
在唤醒上。适合低尾延迟场景，代价是占用一个核的 idle 时间，通常需要配合 `send_lanes`
或把 `io_threads` 少配一个。运行期可用环境变量 `NB_SHM_POLL_US` 覆盖各 binding 的
配置，便于压测/临时调参。

`bindings` 元素支持两种写法：

```json
{
  "transport": {
    "bindings": [
      "tcp://127.0.0.1:42420",
      { "name": "udp", "endpoint": "udp://0.0.0.0:42421" }
    ]
  }
}
```

字符串形式会根据 URI scheme 选择 UDP/TCP。对象形式的 `name` 是 binding 名称（最长
`NB_SCHEME_MAX - 1`），省略时使用 scheme。端口 `0` 表示由系统分配临时端口。

Cyclone DDS 使用 `dds://<domain>`，host 是 DDS domain；端口可省略（provider 会忽略，
旧的 `dds://<domain>:1` 写法仍然兼容）。非网络 scheme（dds/shm 等）允许省略端口，
tcp/udp 仍然必须带端口。共享内存使用 `shm://<channel>:<node-id>`，mesh 模式下
同一 channel 的节点必须使用不同的 `node-id`（`1..9`）；shm 的 `node-id` 端口是
必填的，省略会返回 `NB_ERR_INVALID`。

### QoS

`qos` 对齐 Cyclone DDS 的同名策略；值为 `0`/缺省表示继承 provider 默认值，
只写入显式配置的字段。当前映射到 DDS reader/writer：

| 字段 | 取值 | 对应 DDS |
| --- | --- | --- |
| `reliability` | `best_effort` / `reliable` | ReliabilityQosPolicy |
| `durability` | `volatile` / `transient_local` | DurabilityQosPolicy |
| `history` | `keep_last` / `keep_all` | HistoryQosPolicy |
| `depth` | uint | History depth |
| `deadline_ms` | uint | DeadlineQosPolicy |
| `lifespan_ms` | uint | LifespanQosPolicy |
| `latency_budget_ms` | uint | LatencyBudgetQosPolicy |
| `liveliness` | `automatic` / `manual_by_participant` / `manual_by_topic` | LivelinessQosPolicy |
| `lease_duration_ms` | uint | Liveliness lease duration |
| `ownership` | `shared` / `exclusive` | OwnershipQosPolicy |
| `partition` | string（最长 127） | PartitionQosPolicy |

数值字段必须是整数；`1.5` 这类小数会被拒绝而不是截断。

UDP/TCP/SHM 没有一一对应的链路策略，字段会保留在配置中供应用/自定义 provider
读取；内置实现不会据此改变报文语义。

## discovery

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `mode` | string | `"mesh"` | `mesh` 开启发现；`static` / `off` / `none` 关闭 |
| `broadcast` | string | `"255.255.255.255:41953"` | 广播/发现地址，格式 `host:port` |
| `peers` | array | 空 | 额外单播 HELLO 目标，元素为 `"host:port"`，最多 32 项 |
| `hello_interval_ms` | uint | `3000` | HELLO 周期 |
| `node_ttl_ms` | uint | `9000` | 对端过期时间 |

顶层 `mode = "static"` 与 `discovery.mode = "static"`（或 `off` / `none`）都关闭服务发现；
两者同时出现时以“关闭”为准。`nb_node_config_normalize` 会把 `mode` 与
`discovery.disable` 收敛为一致状态（关闭发现时 `mode` 变为 `STATIC`）。

MESH 模式通常需要两端使用同一个发现端口或互相配置单播 `peers`。

## metrics

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `disable` | bool | `false` | 关闭全部指标采集 |
| `disable_latency_histogram` | bool | `false` | 只关闭延迟直方图 |
| `sample_every` | uint | `1` | `1` 全采；`N` 每 N 次采样一次 |

关闭指标后热路径只保留一次全局开关判断，不做原子累加。

`metrics` 与 `log` 是进程级开关：`nb_node_create` 会把它们应用到共享的指标/日志运行时，
因此多节点场景以最后创建的节点为准，节点销毁时不会恢复。日志可在运行期用
`nb_log_configure` 调整；指标的开关只在建节点时生效。

## log

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `disable` | bool | `false` | 关闭日志输出 |
| `level` | string | `"info"` | `assert` / `error` / `warn` / `info` / `debug` / `verbose` |

运行期也可以用 `nb_log_configure(enabled, level)` 修改开关和级别。

代码构造配置时，`log.level = NB_LOG_LEVEL_DEFAULT` 表示"未指定"，归一化后使用
`info`；越界数值在归一化时按 `info` 处理（`nb_node_config_validate` 会直接报错）。

## routes

```json
{
  "routes": [
    { "service": "OrderService", "endpoint": "tcp://127.0.0.1:42420" },
    { "service": "EchoService", "endpoint": "udp://127.0.0.1:42421" }
  ]
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | :---: | --- |
| `service` | string | 是 | 目标服务名 |
| `endpoint` | string | 是 | `scheme://host:port` |

静态路由跳过服务发现。最多 `NB_CONFIG_MAX_ROUTES` 条；超过上限返回 `NB_ERR_INVALID`。

## 默认值与归一化

通过代码直接构造 `nb_node_config_t` 时，先调用 `nb_node_config_default`，或在创建节点前
调用 `nb_node_config_normalize`。`nb_node_create` 会对配置副本做一次归一化。

`nb_node_config_validate` 是严格校验版本（不夹取、不填默认）：用于希望把越界字段
直接报错的调用方，返回 `NB_ERR_INVALID` / `NB_ERR_TOOBIG`。

`nb_node_config_t` 自带 `routes`/`route_count` 与 `peers`/`peer_count`，
`nb_node_create` 会在返回前应用它们；任何一条路由或 peer 注册失败都会销毁半成品节点并
返回错误，不再静默忽略。JSON 配置里的 `routes`/`peers` 解析后同样落到这两个字段。

解析与归一化的分工：

- `nb_config_parse` 对用户输入做**严格校验**，超出上表的范围直接返回 `NB_ERR_INVALID`。
- `nb_node_config_normalize` 服务于代码直接构造的配置，负责填默认值并把越界值**夹到**
  有效范围内。

归一化规则：

- `name == NULL` 使用 `"rpc"`。
- `transport.bind_host == NULL` 使用 `"0.0.0.0"`。
- `discovery.broadcast_host == NULL` 使用 `"255.255.255.255"`。
- `mode` 与 `discovery.disable` 互相收敛：任一表示“关闭发现”时两者都置为关闭。
- `runtime.io_threads == 0` 使用硬件并发数，并夹到 `1..NB_TRANSPORT_MAX_IO`。
- `runtime.handler_threads` 夹到 `0..NB_MAX_HANDLER_THREADS`。
- `runtime.send_lanes` 夹到 `0..NB_MAX_SEND_LANES`。
- `frag_bytes == 0` 使用 `1440`；否则夹到 `256..8192`。
- `max_msg_bytes == 0` 使用 `1 MiB`；夹到 `frag_bytes..64 MiB`。
- `backpressure_bytes == 0` 使用 `4 MiB`。
- `discovery.port == 0` 使用 `41953`。
- `metrics.sample_every == 0` 使用 `1`。

## 常见错误

| 错误路径 | 原因 |
| --- | --- |
| `name` | 不是字符串或过长 |
| `mode` | 不是 `mesh` / `static` |
| `runtime.io_threads` | 超过 `NB_TRANSPORT_MAX_IO`（8） |
| `runtime.handler_threads` | 超过 `NB_MAX_HANDLER_THREADS`（1024） |
| `runtime.send_lanes` | 超过 `NB_MAX_SEND_LANES`（64） |
| `runtime.max_streams` | 超过 `NB_STREAM_MAX_RECV_STREAMS`（64） |
| `transport.kind` | 不是 `udp` / `tcp` |
| `transport.frag_bytes` | 不是 `0`，也不在 `256..8192` |
| `transport.max_msg_bytes` | 超过 64 MiB 或小于 `frag_bytes` |
| `transport.bindings[i].endpoint` | 元素不是 string/object、URI 非法或数量过多 |
| `transport.bindings[i].intra_process` | 不是 `off` / `auto` / `only` |
| `transport.bindings[i].qos.<field>` | QoS 字段类型或取值非法 |
| `discovery.mode` | 不是 `mesh` / `static` / `off` / `none` |
| `discovery.broadcast` | 不是 `host:port` |
| `discovery.peers[i]` | 不是有效的 `host:port` |
| `routes[i].service` | 缺失或过长 |
| `routes[i].endpoint` | 缺失、URI 非法或过长 |
| `log.level` | 不是支持的级别 |
| `transport.qos.<field>` | QoS 字段类型或取值非法 |

`nb_config_parse_file` 读取超过 `NB_CONFIG_MAX_FILE_BYTES`（1 MiB）的文件时返回
`NB_ERR_INVALID`，避免误指向大文件时整读进内存。
