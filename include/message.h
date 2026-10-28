#ifndef NB_MESSAGE_H
#define NB_MESSAGE_H

/*
 * Unified message context shared by core and transport providers.
 *
 * A message crosses the transport/core boundary exactly once in this shape:
 * inbound transports fill peer + payload/route, outbound core fills target and
 * payload. This is the public provider SPI message type as well, so adding a
 * field no longer requires a parallel adapter struct that can silently drift.
 */

#include <buf.h>
#include <endpoint.h>
#include <types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *nb_peer_token_t;

typedef struct nb_message {
    uint8_t kind;        /* KIND_REQUEST / RESPONSE / EVENT */
    uint8_t flags;       /* MSG_FLAG_*；随 envelope 透传 */
    int status;          /* RESPONSE：应用状态码，或带框架标志时的框架错误码 */
    uint32_t request_id; /* 请求/响应关联 ID */
    uint32_t timeout_ms; /* 请求剩余超时毫秒；0 = 无 deadline */
    const void *metadata;
    uint16_t metadata_len;
    const char *status_msg;
    uint16_t status_msg_len;
    const char *route; /* "service.method"；可为 NULL */
    nb_payload_t payload;
    nb_stream_meta_t stream;
    int has_stream;

    /* 入站来源（由 provider 设置，仅回调期间有效）。 */
    const nb_endpoint_t *peer;
    /* 出站目标（由 core 设置，仅本次发送期间有效）。 */
    const nb_endpoint_t *target;

    /*
     * 兼容/热路径快照：由 peer 派生，供限速、取消登记和内置 UDP/TCP 传输
     * 直接使用。新代码应优先读取 peer。
     */
    const char *peer_ip;
    uint16_t peer_port;
    nb_peer_token_t peer_token;
    nb_buf_t *frame_owner;
} nb_message_t;

#ifdef __cplusplus
}
#endif

#endif /* NB_MESSAGE_H */
