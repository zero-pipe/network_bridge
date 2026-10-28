#ifndef NB_TYPES_H
#define NB_TYPES_H

/*
 * Public base types for the network bridge SDK.
 *
 * Layering:
 *   - The transport layer only moves bytes and never interprets application
 *     semantics.
 *   - Application payloads are opaque to the framework (see nb_payload_t).
 *   - Client response callbacks and INLINE handlers run on an ntk poller
 *     thread and must not block. WORKER / EXECUTOR handlers run on their
 *     executor thread (see service.h).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <error.h>
#include <buf.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Message flags ---- */
/*
 * When set, a response carries a framework error code in its status field
 * (for example NB_ERR_AGAIN) instead of an application status. Application
 * status codes are always delivered through nb_response_t.status.
 */
#define NB_MSG_FLAG_FRAMEWORK_STATUS 0x01u

/* ---- Metadata (gRPC-style string key + binary value) ---- */
#define NB_METADATA_MAX_ENTRIES 16
#define NB_METADATA_KEY_MAX 64
#define NB_METADATA_VALUE_MAX 256
#define NB_METADATA_WIRE_MAX 4096
#define NB_STATUS_MSG_MAX 512

typedef struct nb_metadata_entry {
    char key[NB_METADATA_KEY_MAX];
    uint8_t value[NB_METADATA_VALUE_MAX];
    uint16_t value_len;
} nb_metadata_entry_t;

typedef struct nb_metadata {
    uint16_t count;
    nb_metadata_entry_t entries[NB_METADATA_MAX_ENTRIES];
} nb_metadata_t;

/* All metadata helpers return NB_OK or an error code; getters return NULL when
 * the key is absent. Values are binary-safe; a string stored with set_string
 * includes its trailing NUL in value_len. */
void nb_metadata_init(nb_metadata_t *metadata);
int nb_metadata_set(nb_metadata_t *metadata, const char *key, const void *value, size_t len);
int nb_metadata_set_string(nb_metadata_t *metadata, const char *key, const char *value);
const void *nb_metadata_get(const nb_metadata_t *metadata, const char *key, size_t *len);
const char *nb_metadata_get_string(const nb_metadata_t *metadata, const char *key);
int nb_metadata_remove(nb_metadata_t *metadata, const char *key);
int nb_metadata_encode(const nb_metadata_t *metadata, void *buf, size_t cap, size_t *out_len);
int nb_metadata_decode(nb_metadata_t *metadata, const void *buf, size_t len);

/* ---- Opaque handles ---- */
typedef struct nb_node nb_node_t;
typedef struct nb_service nb_service_t;
typedef struct nb_proxy nb_proxy_t;
typedef struct nb_call nb_call_t;
typedef struct nb_stream nb_stream_t;

/* ---- Stream flags and chunks ---- */
#define NB_STREAM_FIRST 0x01u
#define NB_STREAM_LAST 0x02u
#define NB_STREAM_CANCEL 0x04u

/* One-way data chunk. data is valid only for the duration of the callback or
 * send call that receives it. */
typedef struct nb_chunk {
    const void *data;
    size_t len;
    uint64_t offset;
    const nb_metadata_t *metadata; /* may be NULL; callback scope only */
    const char *status_msg;        /* may be NULL */
    uint32_t flags;                /* NB_STREAM_FIRST / NB_STREAM_LAST / NB_STREAM_CANCEL */
    uint8_t encoding;
} nb_chunk_t;

/* Stream metadata carried end-to-end by transports; NULL/absent on unary
 * messages. */
typedef struct nb_stream_meta {
    uint32_t flags; /* NB_STREAM_FIRST / NB_STREAM_LAST */
    uint32_t stream_id;
    uint64_t offset;
    uint64_t total_size;
} nb_stream_meta_t;

/* ---- Transport and node mode ---- */
/* Built-in transport kinds. The default binding uses UDP; explicit bindings
 * may select a different built-in transport by scheme. Provider-backed schemes
 * (shm, dds, ...) map to NB_TRANSPORT_CUSTOM. */
typedef enum nb_transport_kind {
    NB_TRANSPORT_UDP = 0,
    NB_TRANSPORT_TCP = 1,
    NB_TRANSPORT_CUSTOM = 255
} nb_transport_kind_t;

/* Optional transport-local fast path for event/pub-sub delivery. */
typedef enum nb_intra_process {
    NB_INTRA_OFF = 0,
    NB_INTRA_AUTO = 1,
    NB_INTRA_ONLY = 2
} nb_intra_process_t;

/* Node discovery mode: MESH uses broadcast discovery; STATIC uses only the
 * static route table and starts no discovery. */
typedef enum nb_node_mode {
    NB_MODE_MESH = 0,
    NB_MODE_STATIC = 1
} nb_node_mode_t;

/* Log severity; values match the elog levels used by log.h. */
typedef enum nb_log_level {
    NB_NODE_LOG_ASSERT = 0,
    NB_NODE_LOG_ERROR = 1,
    NB_NODE_LOG_WARN = 2,
    NB_NODE_LOG_INFO = 3,
    NB_NODE_LOG_DEBUG = 4,
    NB_NODE_LOG_VERBOSE = 5
} nb_log_level_t;

/* ---- Payload views ---- */
/*
 * Borrowed application payload (opaque to the framework).
 *
 * data/len are raw bytes; encoding is an application-defined tag (0 = raw
 * bytes). The framework only forwards payloads and never serializes them.
 * Applications may register a codec (see codec.h) to convert business objects
 * to and from payloads.
 *
 * Lifetime: payloads passed to callbacks are valid only for that callback.
 * Copy them if they must outlive it.
 *
 * Ownership on send: nb_proxy_call_async / nb_stub_call_async /
 * nb_proxy_call_event copy the payload before returning (including
 * cross-thread calls), so the caller keeps ownership of its buffer and may
 * free it immediately. The framework never frees caller memory. Server-side
 * nb_reply() likewise only borrows the response payload for the duration of
 * the call.
 */
typedef struct nb_payload {
    const void *data;
    size_t len;
    uint8_t encoding;
} nb_payload_t;

/*
 * Owned payload: buf holds reference-counted memory and offset/len select a
 * view into it. Owned-payload APIs take ownership of buf at entry, even when
 * they return an error; the caller must not release it afterwards.
 */
typedef struct nb_owned_payload {
    nb_buf_t *buf;
    size_t offset;
    size_t len;
    uint8_t encoding;
} nb_owned_payload_t;

/* Bind a payload to a view into buf; does not add a reference. */
void nb_owned_payload_init(nb_owned_payload_t *payload,
                           nb_buf_t *buf,
                           size_t offset,
                           size_t len,
                           uint8_t encoding);
/* Detach and return the buffer, clearing payload; NULL when already empty. */
nb_buf_t *nb_owned_payload_take(nb_owned_payload_t *payload);
/* Release the held buffer (if any) and clear the payload. */
void nb_owned_payload_release(nb_owned_payload_t *payload);
/* Borrowed view of the current bytes; valid only while the payload holds them. */
nb_payload_t nb_owned_payload_view(const nb_owned_payload_t *payload);

/*
 * Request/response views (opaque to the framework). A request carries routing
 * and peer information; a response carries an application status and payload.
 * Both share the same nb_payload_t view.
 *
 * Lifetime: views passed to callbacks (including their payloads) are valid
 * only for that callback.
 */
typedef struct nb_request {
    const char *service;
    const char *method;
    nb_payload_t payload;          /* opaque application bytes */
    const char *peer_host;
    uint16_t peer_port;
    /*
     * Request deadline in absolute monotonic milliseconds (0 = none), derived
     * from the client timeout_ms at receive time (gRPC-style deadline
     * propagation). Handlers may use it to abandon work the client will
     * already discard; the framework skips requests that expired before the
     * handler started.
     */
    uint64_t deadline_ms;
    const nb_metadata_t *metadata; /* may be NULL; callback scope only */
} nb_request_t;

typedef struct nb_response {
    int status;                    /* application-defined; 0 is recommended for success */
    const char *status_msg;        /* may be NULL; callback scope only */
    nb_payload_t payload;
    const nb_metadata_t *metadata; /* may be NULL; callback scope only */
} nb_response_t;

/* ---- Callbacks ---- */
/* Client response callback. status is NB_OK or an error code; resp is valid
 * only for the duration of the callback. */
typedef void (*response_fn)(int status, const nb_response_t *resp, void *user);
typedef void (*response_chunk_fn)(int status, const nb_chunk_t *chunk, void *user);
typedef void (*stream_done_fn)(int status, void *user);

/* ---- Defaults and limits ---- */
#ifndef NB_DISCOVERY_PORT
#define NB_DISCOVERY_PORT 41953
#endif

#ifndef NB_DEFAULT_TIMEOUT_MS
#define NB_DEFAULT_TIMEOUT_MS 5000
#endif

#define NB_NAME_MAX 64
#define NB_SERVICE_NAME_MAX 64
#define NB_METHOD_NAME_MAX 64
#define NB_HOST_MAX 64 /* max host / IP text length, excluding the NUL */
#define NB_SCHEME_MAX 16
#define NB_MAX_BINDINGS 8
#define NB_MAX_ENDPOINTS 32 /* max endpoints held by the static router */

#ifdef __cplusplus
}
#endif

#endif /* NB_TYPES_H */
