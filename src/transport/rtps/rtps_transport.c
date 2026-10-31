/*
 * RTPS transport provider.
 *
 * Unlike the Cyclone provider this one is backed by the in-tree RTPS
 * implementation (src/rtps), so a deployment can use the framework's own DDS
 * data plane without linking a third-party DDS stack.
 *
 * The provider carries the framework's encoded v2 frame as an opaque RTPS
 * sample whose type name is not "dds::Message", which makes the receive path
 * hand back the raw CDR body. Samples are therefore published with a four-byte
 * XCDR1 encapsulation header in front of the frame and the header is stripped
 * before the frame is decoded.
 *
 * Topics follow the same scheme as the Cyclone provider so both providers can
 * coexist in one process:
 *   request  -> <Service>_<method>_req
 *   response -> <Service>_<method>_res
 *   event    -> <Service>_<method>_evt
 *
 * Binding URIs:
 *   rtps://<domain>                      multicast discovery, ephemeral ports
 *   rtps://<domain>:<spdp-port>          unicast only, fixed discovery port
 *   rtps://<domain>:<port>/<host>:<port>,... unicast peers, discovery disabled
 *
 * A port or a peer list turns multicast off, because both are used to pin
 * discovery to explicit peers.
 */
#include "transport_internal.h"

#include "envelope.h"
#include "util.h"

#include <nb/log.h>

#include "rtps_participant.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NB_RTPS_TRANSPORT_MAX_ENDPOINTS 32
#define NB_RTPS_TRANSPORT_TOPIC_MAX 160
#define NB_RTPS_TRANSPORT_MAX_PEERS 16
/* Non-"dds::Message" so the participant delivers the raw CDR body. */
#define NB_RTPS_TRANSPORT_TYPE_NAME "nb::Frame"
#define NB_RTPS_TRANSPORT_ENCAP_BYTES 4u
#define NB_RTPS_TRANSPORT_DEFAULT_HEARTBEAT_MS 250u

typedef struct nb_rtps_transport nb_rtps_transport_t;

typedef struct nb_rtps_transport_endpoint {
    int used;
    char method[NB_GLOBAL_MAX_ROUTE_BYTES];
    char topic_req[NB_RTPS_TRANSPORT_TOPIC_MAX];
    char topic_res[NB_RTPS_TRANSPORT_TOPIC_MAX];
    char topic_evt[NB_RTPS_TRANSPORT_TOPIC_MAX];
    nb_rtps_guid_t wr_req;
    nb_rtps_guid_t rd_req;
    nb_rtps_guid_t wr_res;
    nb_rtps_guid_t rd_res;
    nb_rtps_guid_t wr_evt;
    nb_rtps_guid_t rd_evt;
    int have_responder;
    int have_requester;
    int have_publisher;
} nb_rtps_transport_endpoint_t;

struct nb_rtps_transport {
    nb_transport_provider_options_t options;
    nb_rtps_participant_t *participant;
    char authority[NB_HOST_MAX];
    ntk_mutex *lock;
    nb_rtps_transport_endpoint_t endpoints[NB_RTPS_TRANSPORT_MAX_ENDPOINTS];
    size_t endpoint_count;
    uint32_t reliability;
    uint32_t durability;
    uint32_t history_kind;
    uint32_t history_depth;
    char partition[NB_QOS_PARTITION_MAX];
    int started;
};

static int nb_rtps_guid_matches(const nb_rtps_guid_t *a, const nb_rtps_guid_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

/* Route -> identifier-safe method, matching the Cyclone provider's rule so the
 * same service maps to the same topic name on both transports. */
static void nb_rtps_transport_method(const char *route, char *out, size_t cap)
{
    size_t i = 0;
    size_t j = 0;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!route) {
        return;
    }
    if (route[0] >= '0' && route[0] <= '9') {
        out[j++] = '_';
    }
    for (i = 0; route[i] != '\0' && j + 1 < cap; ++i) {
        char c = route[i];

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_') {
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

static uint32_t nb_rtps_transport_reliability(const nb_qos_t *qos)
{
    if (qos && qos->reliability == NB_QOS_RELIABILITY_BEST_EFFORT) {
        return NB_RTPS_RELIABILITY_BEST_EFFORT;
    }
    return NB_RTPS_RELIABILITY_RELIABLE;
}

static uint32_t nb_rtps_transport_durability(const nb_qos_t *qos)
{
    if (qos && qos->durability == NB_QOS_DURABILITY_TRANSIENT_LOCAL) {
        return NB_RTPS_DURABILITY_TRANSIENT_LOCAL;
    }
    return NB_RTPS_DURABILITY_VOLATILE;
}

static nb_rtps_transport_endpoint_t *nb_rtps_transport_endpoint_init(
    nb_rtps_transport_t *inst,
    const char *method)
{
    nb_rtps_transport_endpoint_t *ep;

    if (inst->endpoint_count >= NB_RTPS_TRANSPORT_MAX_ENDPOINTS) {
        return NULL;
    }
    ep = &inst->endpoints[inst->endpoint_count++];
    memset(ep, 0, sizeof(*ep));
    ep->used = 1;
    nb_copy_str(ep->method, sizeof(ep->method), method, "");
    (void)snprintf(ep->topic_req, sizeof(ep->topic_req), "%s_req", method);
    (void)snprintf(ep->topic_res, sizeof(ep->topic_res), "%s_res", method);
    (void)snprintf(ep->topic_evt, sizeof(ep->topic_evt), "%s_evt", method);
    return ep;
}

static int nb_rtps_transport_add_endpoint(nb_rtps_transport_t *inst,
                                          nb_rtps_endpoint_kind_t kind,
                                          const char *topic,
                                          nb_rtps_guid_t *guid)
{
    nb_rtps_endpoint_info_t info;
    int rc;

    memset(&info, 0, sizeof(info));
    nb_copy_str(info.topic_name, sizeof(info.topic_name), topic, "");
    nb_copy_str(info.type_name, sizeof(info.type_name), NB_RTPS_TRANSPORT_TYPE_NAME, "");
    nb_copy_str(info.partition, sizeof(info.partition), inst->partition, "");
    info.reliability = inst->reliability;
    info.durability = inst->durability;
    info.history_kind = inst->history_kind;
    info.history_depth = inst->history_depth;
    rc = nb_rtps_participant_add_endpoint(inst->participant, kind, &info);
    if (rc != NB_OK) {
        return rc;
    }
    *guid = info.endpoint_guid;
    return NB_OK;
}

static nb_rtps_transport_endpoint_t *nb_rtps_transport_ensure(
    nb_rtps_transport_t *inst,
    const char *method,
    int roles)
{
    nb_rtps_transport_endpoint_t *ep = NULL;
    size_t i;
    int rc;

    if (!inst || !method || !method[0]) {
        return NULL;
    }
    ntk_mutex_lock(inst->lock);
    for (i = 0; i < inst->endpoint_count; ++i) {
        if (inst->endpoints[i].used && strcmp(inst->endpoints[i].method, method) == 0) {
            ep = &inst->endpoints[i];
            break;
        }
    }
    if (!ep) {
        ep = nb_rtps_transport_endpoint_init(inst, method);
        if (!ep) {
            ntk_mutex_unlock(inst->lock);
            return NULL;
        }
    }
    if ((roles & 1) && !ep->have_responder) { /* responder */
        rc = nb_rtps_transport_add_endpoint(
            inst, NB_RTPS_ENDPOINT_SUBSCRIPTION, ep->topic_req, &ep->rd_req);
        if (rc == NB_OK) {
            rc = nb_rtps_transport_add_endpoint(
                inst, NB_RTPS_ENDPOINT_SUBSCRIPTION, ep->topic_evt, &ep->rd_evt);
        }
        if (rc == NB_OK) {
            rc = nb_rtps_transport_add_endpoint(
                inst, NB_RTPS_ENDPOINT_PUBLICATION, ep->topic_res, &ep->wr_res);
        }
        if (rc != NB_OK) {
            ntk_mutex_unlock(inst->lock);
            return NULL;
        }
        ep->have_responder = 1;
    }
    if ((roles & 2) && !ep->have_requester) { /* requester */
        rc = nb_rtps_transport_add_endpoint(
            inst, NB_RTPS_ENDPOINT_PUBLICATION, ep->topic_req, &ep->wr_req);
        if (rc == NB_OK) {
            rc = nb_rtps_transport_add_endpoint(
                inst, NB_RTPS_ENDPOINT_SUBSCRIPTION, ep->topic_res, &ep->rd_res);
        }
        if (rc != NB_OK) {
            ntk_mutex_unlock(inst->lock);
            return NULL;
        }
        ep->have_requester = 1;
    }
    if ((roles & 4) && !ep->have_publisher) { /* event publisher */
        rc = nb_rtps_transport_add_endpoint(
            inst, NB_RTPS_ENDPOINT_PUBLICATION, ep->topic_evt, &ep->wr_evt);
        if (rc != NB_OK) {
            ntk_mutex_unlock(inst->lock);
            return NULL;
        }
        ep->have_publisher = 1;
    }
    ntk_mutex_unlock(inst->lock);
    return ep;
}

/*
 * Publish a frame. The RTPS sample is a four-byte XCDR1 encapsulation header
 * followed by the encoded frame; the receive path strips the header again.
 */
static int nb_rtps_transport_publish(nb_rtps_transport_t *inst,
                                     const nb_rtps_guid_t *writer,
                                     const nb_transport_message_t *message)
{
    nb_envelope_t env;
    uint8_t inline_buf[1200];
    uint8_t *buf;
    size_t frame_len;
    size_t total;
    int heap = 0;
    int rc;

    if (!inst || !writer || !message) {
        return NB_ERR_INVALID;
    }
    memset(&env, 0, sizeof(env));
    env.kind = message->kind;
    env.encoding = message->payload.encoding;
    env.flags = message->flags;
    env.status = message->status;
    env.request_id = message->request_id;
    env.timeout_ms = message->timeout_ms;
    env.route = message->route;
    env.metadata = message->metadata;
    env.metadata_len = message->metadata_len;
    env.status_msg = message->status_msg;
    env.status_msg_len = message->status_msg_len;
    env.payload = message->payload.data;
    env.payload_len = message->payload.len;
    if (message->has_stream) {
        env.stream_flags = (uint8_t)message->stream.flags;
        env.stream_id = message->stream.stream_id;
        env.stream_offset = message->stream.offset;
        env.stream_total_size = message->stream.total_size;
    }
    frame_len = nb_envelope_size(&env);
    if (frame_len == 0) {
        return NB_ERR_INVALID;
    }
    total = frame_len + NB_RTPS_TRANSPORT_ENCAP_BYTES;
    if (total > NB_RTPS_SAMPLE_MAX) {
        return NB_ERR_TOOBIG;
    }
    if (total <= sizeof(inline_buf)) {
        buf = inline_buf;
    } else {
        buf = (uint8_t *)malloc(total);
        if (!buf) {
            return NB_ERR_NOMEM;
        }
        heap = 1;
    }
    buf[0] = 0x00;
    buf[1] = 0x01; /* CDR_LE */
    buf[2] = 0x00;
    buf[3] = 0x00;
    if (nb_envelope_encode(buf + NB_RTPS_TRANSPORT_ENCAP_BYTES,
                           frame_len,
                           &env) != frame_len) {
        if (heap) {
            free(buf);
        }
        return NB_ERR_INVALID;
    }
    rc = nb_rtps_participant_publish(inst->participant, writer, buf, total);
    if (heap) {
        free(buf);
    }
    return rc;
}

static void nb_rtps_transport_local_deliver(nb_rtps_transport_t *inst,
                                            const nb_transport_message_t *message,
                                            int want_response_reader)
{
    size_t i;

    if (!inst->options.on_receive) {
        return;
    }
    ntk_mutex_lock(inst->lock);
    for (i = 0; i < inst->endpoint_count; ++i) {
        nb_rtps_transport_endpoint_t *ep = &inst->endpoints[i];

        if (!ep->used) {
            continue;
        }
        if (want_response_reader ? ep->have_requester : ep->have_responder) {
            inst->options.on_receive(inst->options.user, ep, message);
        }
    }
    ntk_mutex_unlock(inst->lock);
}

/*
 * RTPS samples are padded to a four-byte boundary, so the body handed to the
 * reader may carry up to three trailing filler bytes. Trim them and retry when
 * the exact length does not decode.
 */
static int nb_rtps_transport_decode_frame(const uint8_t *buf,
                                          size_t len,
                                          nb_envelope_t *env)
{
    size_t trim;

    if (nb_envelope_decode(buf, len, env) == NB_OK) {
        return NB_OK;
    }
    for (trim = 1; trim <= 3 && trim < len; trim++) {
        if (nb_envelope_decode(buf, len - trim, env) == NB_OK) {
            return NB_OK;
        }
    }
    return NB_ERR_INVALID;
}

static void nb_rtps_transport_on_data(void *user,
                                      const nb_rtps_guid_t *reader_guid,
                                      const nb_rtps_guid_t *writer_guid,
                                      const nb_rtps_dds_message_t *message)
{
    nb_rtps_transport_t *inst = (nb_rtps_transport_t *)user;
    nb_rtps_transport_endpoint_t *ep = NULL;
    nb_envelope_t env;
    nb_transport_message_t msg;
    nb_endpoint_t peer;
    char route_buf[NB_GLOBAL_MAX_ROUTE_BYTES];
    size_t i;

    (void)writer_guid;
    if (!inst || !message || !message->payload || message->payload_len == 0) {
        return;
    }
    ntk_mutex_lock(inst->lock);
    for (i = 0; i < inst->endpoint_count; ++i) {
        nb_rtps_transport_endpoint_t *candidate = &inst->endpoints[i];

        if (!candidate->used) {
            continue;
        }
        if (nb_rtps_guid_matches(reader_guid, &candidate->rd_req) ||
            nb_rtps_guid_matches(reader_guid, &candidate->rd_res) ||
            nb_rtps_guid_matches(reader_guid, &candidate->rd_evt)) {
            ep = candidate;
            break;
        }
    }
    ntk_mutex_unlock(inst->lock);
    if (!ep || !inst->options.on_receive) {
        return;
    }
    if (nb_rtps_transport_decode_frame(
            message->payload, message->payload_len, &env) != NB_OK) {
        return;
    }
    memset(&msg, 0, sizeof(msg));
    memset(route_buf, 0, sizeof(route_buf));
    nb_envelope_to_message(&env, route_buf, sizeof(route_buf), &msg);
    if (nb_endpoint_from_parts(&peer,
                               "rtps",
                               inst->authority,
                               0,
                               NB_TRANSPORT_KIND_CUSTOM) == NB_OK) {
        msg.peer = &peer;
        msg.peer_host = peer.host;
        msg.peer_port = peer.port;
    }
    inst->options.on_receive(inst->options.user, ep, &msg);
}

/* ---- provider ops ---- */

static void nb_rtps_transport_parse_peers(nb_rtps_transport_t *inst,
                                          const char *path,
                                          nb_rtps_peer_t *peers,
                                          size_t *peer_count,
                                          uint32_t *path_peers)
{
    const char *p = path;
    size_t count = 0;

    *path_peers = 0;
    while (p && *p == '/') {
        p++;
    }
    while (p && *p && count < NB_RTPS_TRANSPORT_MAX_PEERS) {
        const char *end = strchr(p, ',');
        const char *colon;
        size_t host_len;
        unsigned long port;
        char port_text[8];
        size_t port_len;

        if (!end) {
            end = p + strlen(p);
        }
        colon = (const char *)memchr(p, ':', (size_t)(end - p));
        if (!colon) {
            break;
        }
        host_len = (size_t)(colon - p);
        port_len = (size_t)(end - colon - 1);
        if (host_len == 0 || host_len >= sizeof(peers[count].host) ||
            port_len == 0 || port_len >= sizeof(port_text)) {
            break;
        }
        memcpy(peers[count].host, p, host_len);
        peers[count].host[host_len] = '\0';
        memcpy(port_text, colon + 1, port_len);
        port_text[port_len] = '\0';
        port = strtoul(port_text, NULL, 10);
        if (port == 0 || port > 65535) {
            break;
        }
        peers[count].port = (uint16_t)port;
        count++;
        p = *end ? end + 1 : end;
    }
    *peer_count = count;
    *path_peers = (uint32_t)count;
    (void)inst;
}

static int nb_rtps_op_create(const nb_transport_provider_options_t *options,
                             nb_transport_instance_t **out)
{
    nb_rtps_transport_t *inst;
    nb_rtps_participant_config_t config;
    nb_rtps_peer_t *peers = NULL;
    const char *p;
    const char *authority;
    const char *auth_end;
    char domain_text[16];
    char port_text[8];
    size_t n = 0;
    uint32_t peer_count = 0;
    size_t path_peers = 0;
    uint16_t spdp_port = 0;
    int domain = 0;
    int rc;

    if (!options || !out) {
        return NB_ERR_INVALID;
    }
    *out = NULL;
    inst = (nb_rtps_transport_t *)calloc(1, sizeof(*inst));
    if (!inst) {
        return NB_ERR_NOMEM;
    }
    inst->options = *options;
    peers = (nb_rtps_peer_t *)calloc(NB_RTPS_TRANSPORT_MAX_PEERS, sizeof(*peers));
    if (!peers) {
        free(inst);
        return NB_ERR_NOMEM;
    }

    authority = options->endpoint ? strstr(options->endpoint, "://") : NULL;
    authority = authority ? authority + 3 : "";
    auth_end = strchr(authority, '/');
    if (!auth_end) {
        auth_end = authority + strlen(authority);
    }
    while (authority + n < auth_end && authority[n] && authority[n] != ':' &&
           n + 1 < sizeof(domain_text)) {
        domain_text[n] = authority[n];
        n++;
    }
    domain_text[n] = '\0';
    if (domain_text[0]) {
        domain = (int)strtol(domain_text, NULL, 10);
    }
    nb_copy_str(inst->authority, sizeof(inst->authority), domain_text[0] ? domain_text : "0", "0");

    /* Optional explicit SPDP port: rtps://<domain>:<port>[...] */
    p = (const char *)memchr(authority, ':', (size_t)(auth_end - authority));
    if (p && p + 1 < auth_end) {
        const char *q = p + 1;
        size_t digits = 0;

        while (q + digits < auth_end && digits + 1 < sizeof(port_text)) {
            port_text[digits] = q[digits];
            digits++;
        }
        port_text[digits] = '\0';
        spdp_port = (uint16_t)strtoul(port_text, NULL, 10);
    }
    if (auth_end && *auth_end == '/') {
        nb_rtps_transport_parse_peers(inst, auth_end, peers, &path_peers, &peer_count);
    }

    inst->reliability = nb_rtps_transport_reliability(&options->qos);
    inst->durability = nb_rtps_transport_durability(&options->qos);
    inst->history_kind = options->qos.history == NB_QOS_HISTORY_KEEP_ALL
                             ? NB_RTPS_HISTORY_KEEP_ALL
                             : NB_RTPS_HISTORY_KEEP_LAST;
    inst->history_depth = options->qos.depth ? options->qos.depth : 8u;
    nb_copy_str(inst->partition,
                sizeof(inst->partition),
                options->qos.partition,
                "");

    memset(&config, 0, sizeof(config));
    config.domain_id = (uint32_t)domain;
    config.bind_host = "0.0.0.0";
    config.advertise_host = NULL;
    /* Multicast is the default; an explicit port or peer list pins discovery
     * to unicast peers instead. */
    config.multicast_group = (spdp_port != 0 || path_peers > 0) ? "" : NULL;
    config.spdp_port = spdp_port;
    config.unicast_port = 0;
    /* Discovery latency matters more than wire chatter for a transport: a new
     * peer must be found within about a second, not three. */
    config.announce_interval_ms = 1000;
    config.lease_ms = 0;
    config.name = "nb-rtps";
    config.peers = peers;
    config.peer_count = path_peers;
    config.data_heartbeat_ms = NB_RTPS_TRANSPORT_DEFAULT_HEARTBEAT_MS;
    config.data_history_depth = 0;

    inst->participant = nb_rtps_participant_create(&config);
    free(peers);
    if (!inst->participant) {
        nb_log_error("rtps participant create failed for endpoint %s",
                     options->endpoint ? options->endpoint : "(null)");
        free(inst);
        return NB_ERR_IO;
    }
    inst->lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    if (!inst->lock) {
        nb_rtps_participant_destroy(inst->participant);
        free(inst);
        return NB_ERR_NOMEM;
    }
    nb_rtps_participant_set_data_callback(
        inst->participant, nb_rtps_transport_on_data, inst);
    *out = (nb_transport_instance_t *)inst;
    (void)rc;
    (void)peer_count;
    return NB_OK;
}

static int nb_rtps_op_start(nb_transport_instance_t *instance)
{
    nb_rtps_transport_t *inst = (nb_rtps_transport_t *)instance;

    if (!inst || !inst->participant) {
        return NB_ERR_INVALID;
    }
    if (inst->started) {
        return NB_OK;
    }
    if (nb_rtps_participant_start(inst->participant) != NB_OK) {
        return NB_ERR_IO;
    }
    inst->started = 1;
    return NB_OK;
}

static void nb_rtps_op_stop(nb_transport_instance_t *instance)
{
    nb_rtps_transport_t *inst = (nb_rtps_transport_t *)instance;

    if (!inst || !inst->started) {
        return;
    }
    nb_rtps_participant_stop(inst->participant);
    inst->started = 0;
}

static void nb_rtps_op_destroy(nb_transport_instance_t *instance)
{
    nb_rtps_transport_t *inst = (nb_rtps_transport_t *)instance;

    if (!inst) {
        return;
    }
    nb_rtps_op_stop(instance);
    if (inst->participant) {
        nb_rtps_participant_destroy(inst->participant);
    }
    if (inst->lock) {
        ntk_mutex_destroy(inst->lock);
    }
    free(inst);
}

static int nb_rtps_op_send(nb_transport_instance_t *instance,
                           const nb_endpoint_t *target,
                           const nb_transport_message_t *message)
{
    nb_rtps_transport_t *inst = (nb_rtps_transport_t *)instance;
    nb_rtps_transport_endpoint_t *ep;
    char method[NB_GLOBAL_MAX_ROUTE_BYTES];

    (void)target;
    if (!inst || !message) {
        return NB_ERR_INVALID;
    }
    nb_rtps_transport_method(message->route, method, sizeof(method));
    if (!method[0]) {
        return NB_ERR_INVALID;
    }
    if (message->kind == KIND_EVENT) {
        if (inst->options.intra_process != NB_INTRA_PROCESS_OFF) {
            nb_rtps_transport_local_deliver(inst, message, 0);
            if (inst->options.intra_process == NB_INTRA_PROCESS_ONLY) {
                return NB_OK;
            }
        }
        ep = nb_rtps_transport_ensure(inst, method, 4);
        if (!ep) {
            return NB_ERR_NOMEM;
        }
        return nb_rtps_transport_publish(inst, &ep->wr_evt, message);
    }
    ep = nb_rtps_transport_ensure(inst, method, 2);
    if (!ep) {
        return NB_ERR_NOMEM;
    }
    return nb_rtps_transport_publish(inst, &ep->wr_req, message);
}

static int nb_rtps_op_reply(nb_transport_instance_t *instance,
                            void *reply_token,
                            const nb_endpoint_t *peer,
                            const nb_transport_message_t *message)
{
    nb_rtps_transport_t *inst = (nb_rtps_transport_t *)instance;
    nb_rtps_transport_endpoint_t *ep = (nb_rtps_transport_endpoint_t *)reply_token;

    (void)peer;
    if (!inst || !ep || !message) {
        return NB_ERR_INVALID;
    }
    if (!ep->have_responder) {
        return NB_ERR_STATE;
    }
    return nb_rtps_transport_publish(inst, &ep->wr_res, message);
}

static int nb_rtps_op_register_method(nb_transport_instance_t *instance,
                                      const char *service,
                                      const char *method)
{
    nb_rtps_transport_t *inst = (nb_rtps_transport_t *)instance;
    char route[NB_GLOBAL_MAX_ROUTE_BYTES];
    char name[NB_GLOBAL_MAX_ROUTE_BYTES];
    int n;

    if (!inst || !service || !service[0] || !method || !method[0]) {
        return NB_ERR_INVALID;
    }
    n = snprintf(route, sizeof(route), "%s.%s", service, method);
    if (n < 0 || (size_t)n >= sizeof(route)) {
        return NB_ERR_TOOBIG;
    }
    nb_rtps_transport_method(route, name, sizeof(name));
    return nb_rtps_transport_ensure(inst, name, 1) ? NB_OK : NB_ERR_NOMEM;
}

static void nb_rtps_op_unregister_method(nb_transport_instance_t *instance,
                                         const char *service,
                                         const char *method)
{
    (void)instance;
    (void)service;
    (void)method;
}

static const nb_transport_provider_ops_t nb_g_rtps_ops = {
    .abi_version = NB_TRANSPORT_PROVIDER_ABI_VERSION,
    .struct_size = sizeof(nb_transport_provider_ops_t),
    .create = nb_rtps_op_create,
    .start = nb_rtps_op_start,
    .stop = nb_rtps_op_stop,
    .destroy = nb_rtps_op_destroy,
    .send = nb_rtps_op_send,
    .reply = nb_rtps_op_reply,
    .register_method = nb_rtps_op_register_method,
    .unregister_method = nb_rtps_op_unregister_method,
};

static const nb_transport_provider_t nb_g_rtps_provider = {
    .scheme = "rtps",
    .ops = &nb_g_rtps_ops,
};

const nb_transport_provider_t *nb_rtps_transport_provider(void)
{
    return &nb_g_rtps_provider;
}

int nb_rtps_transport_register(void)
{
    if (nb_transport_provider_find("rtps")) {
        return NB_OK;
    }
    return nb_transport_provider_register(&nb_g_rtps_provider);
}
