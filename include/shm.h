#ifndef NB_SHM_H
#define NB_SHM_H

/*
 * POSIX shared-memory transport provider (same host).
 *
 * Register the provider before creating any node, then use a SHM binding:
 *
 *   server: shm://<channel>:1
 *   client: shm://<channel>:2
 *
 * 一个 channel = 一个共享内存段；server（端口 1）是段的所有者并持有生命周期，
 * 客户端用端口 2..(NB_SHM_MAX_CLIENTS+1) 区分，支持多客户端共用同一段。
 * 客户端 proxy endpoint 指向 shm://<channel>:1。
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
