#ifndef NB_SHM_H
#define NB_SHM_H

/*
 * POSIX shared-memory transport provider (same host).
 *
 * Register the provider before creating any node, then use a SHM binding:
 *
 *   node A: shm://<channel>:1
 *   node B: shm://<channel>:2
 *
 * 一个 channel = 一个共享内存段；端口是节点在该段内的唯一 id（1..9），
 * 不再固定 server/client 角色。任意节点都可以接收请求、回复和事件；mesh
 * 模式下 discovery 会广播完整的 shm://channel:id URI，因此也支持按服务名发现。
 * 直连时 proxy endpoint 指向目标节点，例如 shm://<channel>:2。
 *
 * 传输层采用 iceoryx 风格的分档 chunk 池 + 无锁队列 + 偏移寻址：发送方直接
 * 在段内编码 envelope，接收方把 chunk 作为 frame_owner 零拷贝交给 core，大包
 * 跨线程不再复制。单条消息上限由最大 size class 决定（见 NB_SHM_MAX_MESSAGE_BYTES）。
 *
 * 段大小由 NB_SHM_BYTES 环境变量控制（默认 32MB，下限 2MB）；同一 channel 的
 * 各进程必须用相同的段大小。大包并发能力受对应 size class 的块数限制：段里
 * 暂时没有空闲块时发送返回 NB_ERR_AGAIN（可重试），调大 NB_SHM_BYTES 即可提升。
 *
 * 发布/订阅：与 DDS 相同，订阅者就是“方法名即主题”的普通 service，发布者用
 * nb_proxy_call_event 发送 KIND_EVENT。事件按 route（"service.method"）在段内
 * 零拷贝扇出给所有订阅节点（发布者自身除外）；每个订阅者最多 NB_SHM_TOPICS_PER_NODE
 * 个主题，同一 channel 最多 NB_SHM_MAX_NODES 个节点。
 */

#include <transport_provider.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单条消息（编码后 envelope）上限 = 最大 size class 容量。 */
#define NB_SHM_MAX_MESSAGE_BYTES (1024u * 1024u)

/* Idempotently register the built-in "shm" provider. */
int nb_shm_register(void);

/* Static provider descriptor, for callers that prefer explicit registration. */
const nb_transport_provider_t *nb_shm_provider(void);

#ifdef __cplusplus
}
#endif

#endif /* NB_SHM_H */
