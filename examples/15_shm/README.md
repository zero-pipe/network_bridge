# 15：同主机 SHM RPC

服务端和客户端在同一台 Linux 主机上，通过 POSIX 共享内存完成 RPC，不经过
UDP/TCP 协议栈，也不需要服务发现。这个示例展示 STATIC 直连；同一套 provider
也支持 MESH（见文末）。

```bash
./example_15_shm_server
# 另一个终端
./example_15_shm_client
```

端点约定：

- 服务端：`shm://zrpc-example:1`
- 客户端：`shm://zrpc-example:2`（第 3 个客户端用 `:3`，依次到 `:9`）
- 客户端 proxy 目标：`shm://zrpc-example:1`

实现要点（iceoryx 风格零拷贝）：

- 一个 channel 一个共享内存段，段内按 size class 分档的 chunk 池，队列无锁；
- 发送方直接在段内编码 envelope，接收方以 `frame_owner`（external `nb_buf`）
  持有 chunk，大包跨线程零拷贝，不复制消息体；
- 服务端（端口 1）持有段生命周期，最多 8 个客户端共用同一段。

## MESH 模式

SHM 不再要求固定 server/client 角色。同一 channel 内每个节点用自己的 node id
加入段，mesh 通过 UDP HELLO 广播完整 binding URI（`shm://<channel>:<node-id>`），
服务端和客户端都可以是任意节点：

```c
/* 节点 A: shm://orders:1，节点 B: shm://orders:2，两者模式均为 MESH */
binding.endpoint = "shm://orders:2";
cfg.mode = NB_MODE_MESH;
```

只要两个节点的 discovery 端口互通（同广播域或互加 `discovery.peers`），客户端
即可按服务名发现 `shm://orders:2` 并直接调用，不需要 `nb_proxy_set_endpoint_uri`。

- 同一 channel 的 node id 必须唯一，范围 `1..9`。
- 段由首个创建者初始化，最后一个关闭者 unlink；不再要求 node 1 常驻。
- 事件仍按 `服务名.方法名` 扇出给所有订阅节点，发布者自身不回显。

发布/订阅（同 DDS 用法）：

- 订阅者是“方法名即主题”的普通 service：`nb_service_add_method(sub, "chat", ...)`；
  注册时会把 `服务名.方法名` 写入段内订阅表。
- 发布者用 `nb_proxy_call_event(pub, "服务名", "chat", &payload, NULL)` 发送事件，
  endpoint 指向同一 channel（如 `shm://zrpc-example:1`）。
- 事件按 route 零拷贝扇出给所有订阅节点（发布者自身不回显）；chunk 带引用计数，
  最后一个消费者用完才归还池。
- 订阅者按自身角色复用入站队列：服务端走请求队列，客户端走自己的响应队列，
  事件与响应共用一个队列、由框架按消息类型分发。

限制与调优：

- 仅 Linux（Windows/其他平台返回 `NB_ERR_NOT_SUPPORTED`）。
- 单条 encoded envelope 上限 1 MiB（最大 size class）。
- 大包并发上限受该档块数约束；缺块时发送返回 `NB_ERR_AGAIN`（可重试）。
  用 `NB_SHM_BYTES` 调大段（默认 32 MiB，下限 2 MiB）；同一 channel 的
  各进程必须使用相同值。
- 每节点最多订阅 16 个主题（route），同一 channel 最多 9 个节点。
- 个别订阅者队列满时该条事件对该订阅者丢弃（其余订阅者不受影响）。
- 客户端先启动也可以，但服务端重建段时会清掉上一个世代的残留消息与订阅；
  订阅/发布场景建议先启动服务端，让客户端注册到干净的段上。

实现位于 `src/transport/shm/`，公共注册接口见 `include/shm.h`。
