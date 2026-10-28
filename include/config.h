#ifndef NB_CONFIG_H
#define NB_CONFIG_H

/*
 * Single configuration entry point for network bridge.
 *
 * Configuration is split into per-module sub-structs; a node only assembles
 * them, and each module reads only its own section. All sub-structs are
 * aggregated here, so callers only need to include <config.h>.
 *
 * {
 *   "name": "echo-server",
 *   "mode": "static",
 *   "runtime": {
 *     "io_threads": 2,
 *     "handler_threads": 4,
 *     "handler_max_concurrency": 64,
 *     "max_inflight": 65536,
 *     "max_streams": 64,
 *     "peer_rate_limit_pps": 0,
 *     "send_lanes": 0
 *   },
 *   "transport": {
 *     "kind": "tcp",
 *     "bind_host": "0.0.0.0",
 *     "data_port": 42420,
 *     "frag_bytes": 1440,
 *     "max_msg_bytes": 1048576,
 *     "backpressure_bytes": 4194304,
 *     "rtcp_interval_ms": 1000,
 *     "disable_rtcp": false,
 *     "bindings": ["tcp://127.0.0.1:42420"]
 *   },
 *   "discovery": {
 *     "mode": "mesh",
 *     "broadcast": "255.255.255.255:41953",
 *     "hello_interval_ms": 3000,
 *     "node_ttl_ms": 9000,
 *     "peers": ["127.0.0.1:41953"]
 *   },
 *   "metrics": { "disable": false, "disable_latency_histogram": false, "sample_every": 1 },
 *   "log":     { "disable": false, "level": "info" },
 *   "routes":  [ { "service": "EchoService", "endpoint": "tcp://127.0.0.1:42420" } ]
 * }
 *
 * A field of the wrong type, out of range, or an invalid URI returns
 * NB_ERR_INVALID; unknown fields are ignored. Pass a non-NULL error output to
 * locate the failing field and its reason.
 *
 * Ownership: nb_config_doc_t owns all strings. nb_node_create /
 * nb_node_create_from_config copy the strings and endpoints they need into the
 * node, so a document only has to outlive the create call.
 *
 * Do not copy an nb_config_doc_t by value: its internal pointers point into
 * its own buffers.
 */

#include <endpoint.h>
#include <qos.h>
#include <service_table.h>
#include <types.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Capacity ---- */
#define NB_CONFIG_MAX_ROUTES 32
#define NB_CONFIG_MAX_PEERS 32
#define NB_CONFIG_MAX_BINDINGS NB_MAX_BINDINGS

/* ---- Bindings ---- */
typedef struct nb_binding_config {
    const char *name;                 /* provider/binding name, e.g. "tcp" */
    const char *endpoint;             /* URI, e.g. tcp://127.0.0.1:8080 */
    nb_intra_process_t intra_process; /* provider-local event fast path */
    /* Provider busy-poll window in microseconds; 0 blocks on wakeup. Honored by
     * the shm provider: after draining, keep polling this long before falling
     * back to a blocking wait. Trades one core's idle for lower tail latency. */
    uint32_t poll_us;
    nb_qos_t qos;                     /* provider QoS; zero = inherit */
} nb_binding_config_t;

/* ---- Per-module configuration ---- */

/* Threads, concurrency, and flow-control policy. 0 selects the module default. */
typedef struct nb_node_runtime_config {
    unsigned io_threads;              /* poller thread count; 0 = hardware concurrency */
    unsigned handler_threads;         /* worker pool size; 0 = disabled */
    unsigned handler_max_concurrency; /* max in-flight worker jobs; 0 = unlimited */
    uint32_t max_inflight;            /* max pending client calls; 0 = 65536 */
    uint32_t max_streams;             /* max concurrent inbound streams; 0 = 64 */
    uint32_t peer_rate_limit_pps;     /* per-peer requests/sec cap; 0 = unlimited */
    unsigned send_lanes;              /* dedicated send-lane poller threads; 0 = none */
} nb_node_runtime_config_t;

/* Transport and link tuning. */
typedef struct nb_node_transport_config {
    nb_transport_kind_t kind;            /* transport for the default binding; default UDP */
    const char *bind_host;               /* bind address; default "0.0.0.0" */
    uint16_t data_port;                  /* data-plane port; 0 = auto-assign */
    uint16_t frag_bytes;                 /* UDP fragment size; 0 = 1440 */
    uint32_t max_msg_bytes;              /* max encoded envelope; 0 = 1 MiB */
    uint32_t backpressure_bytes;         /* outbound high-water mark; 0 = 4 MiB */
    uint32_t rtcp_interval_ms;           /* UDP RTCP interval; 0 = 1000 ms */
    bool disable_rtcp;                   /* true disables UDP RTCP */
    const nb_binding_config_t *bindings; /* non-empty starts every listed binding */
    size_t binding_count;
    nb_qos_t qos;                        /* QoS for the default binding */
} nb_node_transport_config_t;

/* Service discovery. */
typedef struct nb_node_discovery_config {
    bool disable;               /* true disables discovery (equivalent to mode=static) */
    const char *broadcast_host; /* broadcast/discovery address; default 255.255.255.255 */
    uint16_t port;              /* discovery port; 0 = NB_DISCOVERY_PORT */
    uint32_t hello_interval_ms; /* HELLO interval; 0 = 3000 */
    uint32_t node_ttl_ms;       /* peer expiry; 0 = 9000 */
} nb_node_discovery_config_t;

/* Metrics collection. Zero-initialized means enabled. */
typedef struct nb_node_metrics_config {
    bool disable;                    /* true disables all metric collection */
    bool disable_latency_histogram;  /* true disables the latency histogram */
    uint32_t sample_every;           /* 0/1 = every sample; N = every Nth sample */
    bool disable_per_peer_stats;     /* true disables per-peer statistics */
} nb_node_metrics_config_t;

/*
 * Logging. Zero-initialized means enabled. The sink fields are reserved and
 * currently have no effect on the backend.
 */
typedef enum nb_node_log_sink {
    NB_LOG_SINK_CONSOLE = 0,
    NB_LOG_SINK_FILE = 1,
    NB_LOG_SINK_SYSLOG = 2
} nb_node_log_sink_t;

typedef struct nb_node_log_config {
    bool disable;             /* true disables logging */
    nb_log_level_t level;     /* default NB_LOG_LEVEL_INFO */
    nb_node_log_sink_t sink_kind; /* reserved: no effect yet */
    const char *file;         /* reserved: log file path */
} nb_node_log_config_t;

/* Composite node configuration: identity plus per-module sub-configs. */
typedef struct nb_node_config {
    const char *name;                        /* node name (logs/discovery); may be NULL */
    nb_node_mode_t mode;                     /* MESH (default) / STATIC */
    const nb_service_table_t *service_table; /* optional shared declaration table; static lifetime */
    nb_node_runtime_config_t runtime;
    nb_node_transport_config_t transport;
    nb_node_discovery_config_t discovery;
    nb_node_metrics_config_t metrics;
    nb_node_log_config_t log;
} nb_node_config_t;

/* ---- Parse results ---- */
/* One static route entry (service -> endpoint URI). */
typedef struct nb_static_route {
    char service[NB_SERVICE_NAME_MAX];
    char endpoint[NB_ENDPOINT_URI_MAX];
} nb_static_route_t;

/* One explicit discovery peer (unicast HELLO target). */
typedef struct nb_peer_endpoint {
    char host[NB_HOST_MAX];
    uint16_t port;
} nb_peer_endpoint_t;

/* One configured binding: a naming label plus its endpoint URI. */
typedef struct nb_binding_endpoint {
    char name[NB_SCHEME_MAX];
    char endpoint[NB_ENDPOINT_URI_MAX];
} nb_binding_endpoint_t;

/*
 * A parsed configuration document. It owns all strings, and the pointers in
 * `node` point into the buffers below, so it must not be copied by value.
 */
typedef struct nb_config_doc {
    nb_node_config_t node; /* pointers refer to buffers in this document */
    char name[NB_NAME_MAX];
    char bind_host[NB_HOST_MAX];
    char discovery_host[NB_HOST_MAX];
    char log_file[NB_HOST_MAX];
    nb_static_route_t routes[NB_CONFIG_MAX_ROUTES];
    size_t route_count;
    nb_peer_endpoint_t peers[NB_CONFIG_MAX_PEERS];
    size_t peer_count;
    nb_binding_endpoint_t bindings[NB_CONFIG_MAX_BINDINGS];
    nb_binding_config_t binding_configs[NB_CONFIG_MAX_BINDINGS];
    size_t binding_count;
} nb_config_doc_t;

/* Structured parse error: path looks like "transport.frag_bytes" or
 * "routes[0].endpoint". */
typedef struct nb_config_error {
    int code; /* NB_ERR_* */
    char path[96];
    char reason[128];
} nb_config_error_t;

/* ---- Defaults ---- */
void nb_node_runtime_config_default(nb_node_runtime_config_t *config);
void nb_node_transport_config_default(nb_node_transport_config_t *config);
void nb_node_discovery_config_default(nb_node_discovery_config_t *config);
void nb_node_metrics_config_default(nb_node_metrics_config_t *config);
void nb_node_log_config_default(nb_node_log_config_t *config);
/* Fill composite defaults; for callers that build nb_node_config_t directly. */
void nb_node_config_default(nb_node_config_t *config);
void nb_binding_config_default(nb_binding_config_t *config);
/*
 * Normalize: replace 0/NULL fields with defaults and clamp values to their
 * valid range. nb_node_create applies this to a copy of the incoming config,
 * so callers that build nb_node_config_t directly need no fallback of their
 * own. log.level == 0 is a valid NB_LOG_LEVEL_ASSERT and is not rewritten.
 */
void nb_node_config_normalize(nb_node_config_t *config);

/* ---- Load and create ---- */
/* Parse a JSON string; fills out and returns NB_OK on success. error may be
 * NULL; when non-NULL it receives the failing field path and reason. */
int nb_config_parse(const char *json, nb_config_doc_t *out, nb_config_error_t *error);
/* Read a file and parse it. error may be NULL. */
int nb_config_parse_file(const char *path, nb_config_doc_t *out, nb_config_error_t *error);

/* Create a node from a parsed document and apply static routes and explicit
 * discovery peers. */
int nb_node_create_from_config(const nb_config_doc_t *config, nb_node_t **out);

#ifdef __cplusplus
}
#endif

#endif /* NB_CONFIG_H */
