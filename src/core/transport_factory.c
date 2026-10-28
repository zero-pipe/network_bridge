/* 传输装配：创建并启动一个 binding 的传输，登记进 node。 */
#include "transport_factory.h"

#include "node_internal.h"
#include "transport_internal.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nb_transport_t *nb_create_transport(nb_transport_kind_t kind)
{
    return kind == NB_TRANSPORT_TCP ? nb_tcp_transport_create() : nb_udp_transport_create();
}

/* 自定义 provider 收到消息后，适配回 node 的统一分发入口。 */
static void nb_provider_on_receive(void *receiver,
                                   void *reply_token,
                                   const nb_transport_message_t *message)
{
    nb_transport_t *transport = (nb_transport_t *)receiver;
    nb_message_t msg;
    if (!transport || !transport->node || !message) {
        return;
    }
    msg = *message;
    if (message->peer) {
        msg.peer_ip = message->peer->host;
        msg.peer_port = message->peer->port;
    }
    /* peer/target point at provider stack storage; keep only borrowed values. */
    msg.peer = NULL;
    msg.target = NULL;
    msg.peer_token = reply_token;
    nb_node_on_message(transport->node, transport, &msg);
}

/*
 * provider 适配层：把外部 provider SPI 适配成统一的 nb_transport_ops_t，
 * 使 node/upper 层只面对单一传输接口（不再有 provider/ops 双分支）。
 */
static int nb_provider_op_start(nb_transport_t *t)
{
    return t->provider->ops->start(t->provider_instance);
}

static void nb_provider_op_stop(nb_transport_t *t)
{
    t->provider->ops->stop(t->provider_instance);
}

static int nb_provider_op_send(nb_transport_t *t, const nb_message_t *message)
{
    nb_endpoint_t target;
    if (!t || !t->provider || !message) {
        return NB_ERR_INVALID;
    }
    if (nb_endpoint_from_parts(&target,
                               t->scheme,
                               message->peer_ip,
                               message->peer_port,
                               nb_transport_kind_from_scheme(t->scheme)) != NB_OK) {
        return NB_ERR_INVALID;
    }
    return t->provider->ops->send(t->provider_instance, &target, message);
}

static int nb_provider_op_reply(nb_transport_t *t, const nb_message_t *message)
{
    nb_endpoint_t peer;
    if (!t || !t->provider || !message) {
        return NB_ERR_INVALID;
    }
    if (nb_endpoint_from_parts(&peer,
                               t->scheme,
                               message->peer_ip,
                               message->peer_port,
                               nb_transport_kind_from_scheme(t->scheme)) != NB_OK) {
        return NB_ERR_INVALID;
    }
    return t->provider->ops->reply(t->provider_instance,
                                   message->peer_token,
                                   &peer,
                                   message);
}

static int nb_provider_op_peer_stats(nb_transport_t *t,
                                  const char *ip,
                                  uint16_t port,
                                  nb_peer_stats_t *out)
{
    (void)t;
    (void)ip;
    (void)port;
    (void)out;
    return 0;
}

static const nb_transport_ops_t nb_g_provider_ops = {
    nb_provider_op_start, nb_provider_op_stop,       nb_provider_op_send,
    nb_provider_op_reply, nb_provider_op_peer_stats,
};

int nb_node_transport_add(nb_node_t *node,
                          const char *scheme,
                          const char *host,
                          uint16_t port,
                          const char *path,
                          nb_intra_process_t intra_process,
                          const nb_qos_t *qos)
{
    nb_transport_t *transport;
    nb_transport_kind_t kind;
    const nb_transport_provider_t *provider;
    nb_endpoint_t endpoint;
    if (!node || !scheme || !host || !host[0] || node->transport_count >= NB_MAX_BINDINGS) {
        return NB_ERR_INVALID;
    }
    if (nb_endpoint_from_parts(&endpoint, scheme, host, port, nb_transport_kind_from_scheme(scheme)) !=
        NB_OK) {
        return NB_ERR_INVALID;
    }
    if (path && path[0]) {
        if (strlen(path) >= sizeof(endpoint.path)) {
            return NB_ERR_INVALID;
        }
        strcpy(endpoint.path, path);
        if (nb_endpoint_refresh(&endpoint) != NB_OK) {
            return NB_ERR_INVALID;
        }
    }
    kind = nb_transport_kind_from_scheme(scheme);
    provider = nb_transport_provider_find(scheme);
    if (kind != NB_TRANSPORT_TCP && kind != NB_TRANSPORT_UDP && !provider) {
        return NB_ERR_NOTFOUND; /* 未注册的 scheme */
    }
    transport =
        provider ? (nb_transport_t *)calloc(1, sizeof(*transport)) : nb_create_transport(kind);
    if (!transport) {
        return NB_ERR_NOMEM;
    }
    transport->node = node;
    nb_copy_str(transport->scheme, sizeof(transport->scheme), scheme, "udp");
    nb_copy_str(transport->bind_host, sizeof(transport->bind_host), host, "0.0.0.0");
    transport->port = port;
    nb_copy_str(transport->endpoint_uri, sizeof(transport->endpoint_uri), endpoint.uri, "");
    /* 传输层只读的运行期配置快照（见 nb_transport.h）。 */
    transport->frag_bytes = node->frag_bytes;
    transport->max_msg_bytes = node->max_msg_bytes;
    transport->bp_high_water = node->bp_high_water;
    transport->drop_percent = node->drop_percent;
    transport->rtcp_interval_ms = node->rtcp_interval_ms;
    transport->rtcp_disabled = node->rtcp_disabled;
    transport->io_threads = node->io_threads;
    transport->pool = node->pool;
    transport->poller = node->poller;
    transport->metrics = &node->metrics;
    if (provider) {
        nb_transport_provider_options_t options;
        nb_transport_instance_t *instance = NULL;
        nb_qos_t effective_qos = node->default_qos;
        transport->kind = NB_TRANSPORT_CUSTOM;
        transport->ops = &nb_g_provider_ops;
        transport->provider = provider;
        memset(&options, 0, sizeof(options));
        options.scheme = scheme;
        options.endpoint = endpoint.uri;
        options.receiver = transport;
        options.on_receive = nb_provider_on_receive;
        options.intra_process = intra_process;
        nb_qos_inherit(&effective_qos, qos);
        options.qos = effective_qos;
        if (provider->ops->create(&options, &instance) != NB_OK || !instance) {
            free(transport);
            return NB_ERR_IO;
        }
        transport->provider_instance = instance;
    }
    if (nb_transport_start(transport) != NB_OK) {
        nb_transport_destroy(transport);
        return NB_ERR_IO;
    }
    /* start() may assign an ephemeral port (port=0 input). Refresh the URI. */
    if (transport->port != port) {
        nb_endpoint_t actual;
        if (nb_endpoint_from_parts(&actual,
                                   transport->scheme,
                                   transport->bind_host,
                                   transport->port,
                                   transport->kind) == NB_OK) {
            if (path && path[0]) {
                strcpy(actual.path, path);
                (void)nb_endpoint_refresh(&actual);
            }
            nb_copy_str(transport->endpoint_uri,
                        sizeof(transport->endpoint_uri),
                        actual.uri,
                        "");
        }
    }
    node->transports[node->transport_count++] = transport;
    return NB_OK;
}

int nb_node_transport_add_binding(nb_node_t *node, const nb_binding_config_t *binding)
{
    nb_endpoint_t endpoint;

    if (!node || !binding || !binding->endpoint) {
        return NB_ERR_INVALID;
    }
    if (nb_endpoint_parse(binding->endpoint, &endpoint) != NB_OK) {
        return NB_ERR_INVALID;
    }
    return nb_node_transport_add(node,
                                 endpoint.scheme,
                                 endpoint.host,
                                 endpoint.port,
                                 endpoint.path,
                                 binding->intra_process,
                                 &binding->qos);
}
