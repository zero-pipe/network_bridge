#ifndef NB_ENDPOINT_H
#define NB_ENDPOINT_H

#include <types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NB_ENDPOINT_URI_MAX 256
#define NB_ENDPOINT_PATH_MAX 128

/* ---- 数据结构 ---- */
/*
 * Transport-neutral endpoint. scheme selects a provider, host holds the
 * provider authority (IP/hostname, DDS domain or SHM channel), port is the
 * provider service id, and uri is the canonical wire/storage form. All
 * framework paths (route/registry/discovery/proxy/provider) use this type so
 * provider-specific addressing is never rebuilt from an ip:port pair.
 */
typedef struct nb_endpoint {
    char scheme[NB_SCHEME_MAX];
    char host[NB_ENDPOINT_MAX];
    char path[NB_ENDPOINT_PATH_MAX];
    uint16_t port;
    nb_transport_kind_t transport;
    char uri[NB_ENDPOINT_URI_MAX];
} nb_endpoint_t;

/* ---- Scheme 与传输映射 ---- */
/* Map a provider scheme to a built-in transport kind, if any. */
nb_transport_kind_t nb_transport_kind_from_scheme(const char *scheme);

/* ---- 解析与格式化 ---- */
/* Parse scheme://host:port. The input is not retained. */
int nb_endpoint_parse(const char *uri, nb_endpoint_t *out);

/* Build an endpoint from parts and fill its canonical URI. */
int nb_endpoint_from_parts(nb_endpoint_t *out,
                           const char *scheme,
                           const char *host,
                           uint16_t port,
                           nb_transport_kind_t transport);

/* Format an endpoint URI into out (canonical form, ignores endpoint->uri). */
int nb_endpoint_format(const nb_endpoint_t *endpoint, char *out, size_t cap);

/* Refresh endpoint->uri after fields were changed. */
int nb_endpoint_refresh(nb_endpoint_t *endpoint);

/* True for UDP/TCP, whose host is a network address discovery may rewrite. */
int nb_endpoint_is_network(const nb_endpoint_t *endpoint);

int nb_endpoint_equal(const nb_endpoint_t *a, const nb_endpoint_t *b);

#ifdef __cplusplus
}
#endif

#endif /* NB_ENDPOINT_H */
