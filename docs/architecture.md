# 架构与抽象

network bridge 的目标是把“调用语义”和“链路实现”彻底分开。上层只描述
`service.method + payload + metadata`，下层只负责搬运统一消息；地址、QoS、
分片、回程 token 都由框架的公共抽象承载。

## 分层

```text
应用
  service / proxy / stub / stream       调用形态与生命周期
  codec / message_view                  payload 编解码与视图
core
  node / dispatch / pending / dedup    关联、超时、重试、线程调度
  router / registry / discovery        服务解析与 mesh 控制面
transport SPI
  endpoint + message + QoS             传输无关契约
providers
  udp / tcp / shm / dds / custom       链路实现
```

依赖方向只能向下。transport provider 不感知 service/proxy，也不解析 payload；
core 不感知具体 scheme 的私有地址格式。

## Endpoint

`nb_endpoint_t` 是唯一地址表示，字段固定为：

| 字段 | 含义 |
| --- | --- |
| `scheme` | provider 选择器，如 `udp` / `tcp` / `shm` / `dds` |
| `host` | provider authority：IP、DDS domain 或 SHM channel |
| `port` | provider service id：UDP/TCP 端口、SHM node id、DDS 占位 |
| `path` | 可选 provider 子路径 |
| `transport` | 内置传输种类；自定义 provider 为 `NB_TRANSPORT_CUSTOM` |
| `uri` | 规范化 URI，供路由、发现、日志和跨进程传输使用 |

不变量：

- 路由表、registry、discovery 帧、proxy 覆盖端点、provider send 目标全部使用
  `nb_endpoint_t`，不再各自保存 `ip/scheme/port` 三元组。
- `nb_endpoint_parse` 负责规范化；`nb_endpoint_refresh` 在字段被修改后重建 URI。
- 只有 UDP/TCP 的 authority 是网络地址。discovery 收到 HELLO 时只对
  `nb_endpoint_is_network()` 的端点用报文源 IP 改写 host；SHM channel 和 DDS
  domain 必须原样保留。

## Message

`nb_message_t` 同时是 core 内部消息和公开 provider SPI 消息（公开头
`include/message.h`）。此前 provider 侧另有一份结构并在 `transport_factory.c`
手工逐字段复制，新增字段容易漏传；现在只有一份定义：

- 入站：provider 填 `peer`、route、payload、metadata、stream，
  `frame_owner` 可延长零拷贝缓冲生命周期。
- 出站：core 填 `target`、route、payload；UDP/TCP 使用派生的
  `peer_host/peer_port` 快路径。
- `reply_token` 是 provider 私有回程标识，只用于 reply。

## QoS

`nb_qos_t`（`include/qos.h`）对齐 DDS 策略，字段缺省为 INHERIT，避免覆盖
provider 默认值。当前映射：

- reliability / durability / history / depth
- deadline / lifespan / latency budget
- liveliness / lease duration / ownership / partition

DDS provider 在创建 reader/writer 时统一套用 binding QoS；UDP/TCP/SHM 保留
配置供应用读取，但不改变报文语义。binding 未配置时继承 `transport.qos`。

## Mesh 与 SHM

mesh 控制面仍通过 UDP HELLO/BYE 传播服务与 binding。HELLO v4 携带每个
binding 的完整 URI，而不是 `scheme + port`，因此 SHM channel、DDS domain 等
provider authority 不会丢失。

SHM 段布局采用“每节点一个入站队列”：

- 每个节点以 `shm://<channel>:<node-id>` 加入同一段，`node-id` 为 `1..9`。
- 任意节点可以向任意目标节点投递 RPC 响应或事件，不再区分固定
  server/client 角色。
- 旧 `push_request/push_response` API 保留为兼容包装：request 等价于发往
  node 1，response 等价于发往指定 client。
- 事件按 route 扇出到订阅节点；chunk 引用计数保证零拷贝。
- 段由首个创建者初始化，最后一个关闭者 unlink。

这不要求专门启动“SHM server”：mesh 中每个节点都是对等节点，但同一 channel
内 node-id 必须唯一。

## 能力矩阵

| 能力 | UDP | TCP | SHM | DDS |
| --- | :-: | :-: | :-: | :-: |
| unary RPC | 是 | 是 | 是 | 是 |
| 事件/pub-sub | 是 | 是 | 是 | 是 |
| 流式 | 是 | 是 | 是 | 是 |
| mesh 发现 | 是 | 是 | 是 | 是 |
| 零拷贝接收 | 否 | 否 | 是 | loan 期间 |
| QoS | 保留 | 保留 | 保留 | 映射 |

## 后续边界

- TLS/DTLS/鉴权仍由自定义 provider 或 service interceptor 承担。
- DDS loan 的跨线程所有权转移需要把 loan 生命周期扩展到异步消费者，目前
  worker 路径仍由 core 深拷贝。
- SHM 段异常退出后的残留依赖最后一次关闭或下一次空段重建；跨机部署仍应
  使用 UDP/TCP/DDS。
