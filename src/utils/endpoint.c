#include <endpoint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nb_copy_part(char *out, size_t cap, const char *begin, size_t len)
{
    if (!out || cap == 0 || len == 0 || len >= cap) {
        return NB_ERR_INVALID;
    }
    memcpy(out, begin, len);
    out[len] = '\0';
    return NB_OK;
}

static int nb_endpoint_format_raw(const nb_endpoint_t *endpoint, char *out, size_t cap)
{
    int n;

    if (!endpoint || !out || cap == 0 || !endpoint->scheme[0] || !endpoint->host[0]) {
        return NB_ERR_INVALID;
    }
    if (strchr(endpoint->host, ':')) {
        n = snprintf(
            out, cap, "%s://[%s]:%u", endpoint->scheme, endpoint->host, (unsigned)endpoint->port);
    } else {
        n = snprintf(
            out, cap, "%s://%s:%u", endpoint->scheme, endpoint->host, (unsigned)endpoint->port);
    }
    if (n < 0 || (size_t)n >= cap) {
        return NB_ERR_TOOBIG;
    }
    if (endpoint->path[0]) {
        const char *separator = endpoint->path[0] == '/' ? "" : "/";
        int m = snprintf(out + n, cap - (size_t)n, "%s%s", separator, endpoint->path);
        if (m < 0 || (size_t)m >= cap - (size_t)n) {
            return NB_ERR_TOOBIG;
        }
    }
    return NB_OK;
}

/* ---- Scheme 与传输映射 ---- */

nb_transport_kind_t nb_transport_kind_from_scheme(const char *scheme)
{
    if (!scheme) {
        return (nb_transport_kind_t)-1;
    }
    if (strcmp(scheme, "tcp") == 0 || strcmp(scheme, "TCP") == 0) {
        return NB_TRANSPORT_TCP;
    }
    if (strcmp(scheme, "udp") == 0 || strcmp(scheme, "UDP") == 0) {
        return NB_TRANSPORT_UDP;
    }
    return NB_TRANSPORT_CUSTOM;
}

/* ---- 解析与格式化 ---- */

int nb_endpoint_parse(const char *uri, nb_endpoint_t *out)
{
    const char *scheme_end;
    const char *authority;
    const char *port_sep;
    const char *path_sep;
    char port_text[8];
    char *port_end;
    unsigned long port;
    size_t host_len;

    if (!uri || !out) {
        return NB_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    scheme_end = strstr(uri, "://");
    if (!scheme_end || scheme_end == uri) {
        return NB_ERR_INVALID;
    }
    if (nb_copy_part(out->scheme, sizeof(out->scheme), uri, (size_t)(scheme_end - uri)) != NB_OK) {
        return NB_ERR_INVALID;
    }
    authority = scheme_end + 3;
    if (!*authority) {
        return NB_ERR_INVALID;
    }
    path_sep = strchr(authority, '/');

    if (*authority == '[') {
        const char *close = strchr(authority, ']');
        if (!close || close[1] != ':' || (path_sep && close > path_sep)) {
            return NB_ERR_INVALID;
        }
        host_len = (size_t)(close - authority - 1);
        if (nb_copy_part(out->host, sizeof(out->host), authority + 1, host_len) != NB_OK) {
            return NB_ERR_INVALID;
        }
        port_sep = close + 2;
    } else {
        const char *auth_end = path_sep ? path_sep : authority + strlen(authority);
        const char *p;

        port_sep = NULL;
        for (p = authority; p < auth_end; p++) {
            if (*p == ':') {
                port_sep = p;
            }
        }
        if (!port_sep || port_sep == authority) {
            return NB_ERR_INVALID;
        }
        host_len = (size_t)(port_sep - authority);
        if (nb_copy_part(out->host, sizeof(out->host), authority, host_len) != NB_OK) {
            return NB_ERR_INVALID;
        }
    }
    if (*port_sep == ':') {
        port_sep++;
    }
    if (!*port_sep || (path_sep && port_sep > path_sep)) {
        return NB_ERR_INVALID;
    }
    {
        size_t digits = path_sep ? (size_t)(path_sep - port_sep) : strlen(port_sep);

        if (digits == 0 || digits >= sizeof(port_text)) {
            return NB_ERR_INVALID;
        }
        memcpy(port_text, port_sep, digits);
        port_text[digits] = '\0';
    }
    port = strtoul(port_text, &port_end, 10);
    if (*port_end != '\0' || port > 65535) {
        return NB_ERR_INVALID;
    }
    out->port = (uint16_t)port;
    out->transport = nb_transport_kind_from_scheme(out->scheme);
    if (path_sep) {
        size_t path_len = strlen(path_sep);

        if (path_len >= sizeof(out->path)) {
            return NB_ERR_INVALID;
        }
        memcpy(out->path, path_sep, path_len + 1u);
    }
    if (nb_endpoint_refresh(out) != NB_OK) {
        memset(out, 0, sizeof(*out));
        return NB_ERR_INVALID;
    }
    return NB_OK;
}

int nb_endpoint_format(const nb_endpoint_t *endpoint, char *out, size_t cap)
{
    return nb_endpoint_format_raw(endpoint, out, cap);
}

int nb_endpoint_refresh(nb_endpoint_t *endpoint)
{
    if (!endpoint) {
        return NB_ERR_INVALID;
    }
    return nb_endpoint_format_raw(endpoint, endpoint->uri, sizeof(endpoint->uri));
}

int nb_endpoint_from_parts(nb_endpoint_t *out,
                           const char *scheme,
                           const char *host,
                           uint16_t port,
                           nb_transport_kind_t transport)
{
    if (!out || !scheme || !scheme[0] || !host || !host[0]) {
        return NB_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (nb_copy_part(out->scheme, sizeof(out->scheme), scheme, strlen(scheme)) != NB_OK ||
        nb_copy_part(out->host, sizeof(out->host), host, strlen(host)) != NB_OK) {
        memset(out, 0, sizeof(*out));
        return NB_ERR_INVALID;
    }
    out->port = port;
    out->transport = transport;
    return nb_endpoint_refresh(out);
}

bool nb_endpoint_is_network(const nb_endpoint_t *endpoint)
{
    if (!endpoint) {
        return false;
    }
    return endpoint->transport == NB_TRANSPORT_UDP || endpoint->transport == NB_TRANSPORT_TCP ||
           strcmp(endpoint->scheme, "udp") == 0 || strcmp(endpoint->scheme, "tcp") == 0;
}

bool nb_endpoint_equal(const nb_endpoint_t *a, const nb_endpoint_t *b)
{
    if (!a || !b) {
        return false;
    }
    return a->port == b->port && strcmp(a->scheme, b->scheme) == 0 &&
           strcmp(a->host, b->host) == 0 && strcmp(a->path, b->path) == 0;
}
