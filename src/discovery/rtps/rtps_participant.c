#include "rtps_participant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ntk.h>

#include "rtps_message.h"
#include "rtps_param.h"
#include "rtps_reliable.h"
#include "rtps_sedp.h"
#include "rtps_spdp.h"

#define NB_RTPS_SPDP_BASE_PORT 7400u
#define NB_RTPS_SPDP_DOMAIN_GAIN 250u
#define NB_RTPS_DEFAULT_ANNOUNCE_MS 3000u
#define NB_RTPS_DEFAULT_LEASE_MS 20000u
#define NB_RTPS_MIN_ANNOUNCE_MS 100u
#define NB_RTPS_MIN_LEASE_MS 1000u
#define NB_RTPS_MAX_LEASE_MS 3600000u
#define NB_RTPS_SWEEP_INTERVAL_MS 200u
#define NB_RTPS_PACKET_MAX 1400u
#define NB_RTPS_SPDP_PAYLOAD_MAX 1200u
#define NB_RTPS_DEFAULT_MAX_LOCAL_ENDPOINTS 1024u
#define NB_RTPS_DEFAULT_MAX_REMOTE_ENDPOINTS 4096u
#define NB_RTPS_MAX_LOCAL_ENDPOINTS 4096u
#define NB_RTPS_MAX_REMOTE_ENDPOINTS 4096u
#define NB_RTPS_MAX_DATA_TARGETS 16u
#define NB_RTPS_DEFAULT_SEDP_HEARTBEAT_MS 1000u
#define NB_RTPS_MIN_SEDP_HEARTBEAT_MS 100u

typedef struct nb_rtps_sedp_reader_state {
    uint32_t next_expected;
    uint32_t received[NB_RTPS_ACKNACK_MAX_WORDS];
    uint32_t ack_count;
} nb_rtps_sedp_reader_state_t;

typedef struct nb_rtps_local_endpoint {
    int used;
    nb_rtps_endpoint_kind_t kind;
    nb_rtps_endpoint_info_t info;
    uint32_t sn;
    uint32_t data_sn;
} nb_rtps_local_endpoint_t;

typedef struct nb_rtps_remote_endpoint {
    int used;
    nb_rtps_endpoint_kind_t kind;
    nb_rtps_endpoint_info_t info;
    uint64_t last_seen_ms;
} nb_rtps_remote_endpoint_t;

typedef struct nb_rtps_remote_participant {
    int used;
    uint64_t last_seen_ms;
    uint32_t lease_ms;
    nb_rtps_participant_info_t info;
    char source_host[NB_RTPS_PEER_HOST_MAX];
    uint16_t source_port;
    nb_rtps_sedp_reader_state_t publications_reader;
    nb_rtps_sedp_reader_state_t subscriptions_reader;
} nb_rtps_remote_participant_t;

struct nb_rtps_participant {
    char bind_host[NB_RTPS_PEER_HOST_MAX];
    char advertise_host[NB_RTPS_PEER_HOST_MAX];
    char multicast_group[NB_RTPS_PEER_HOST_MAX];
    char name[NB_RTPS_SPDP_NAME_MAX];
    uint32_t domain_id;
    uint16_t spdp_port;
    uint16_t unicast_port;
    uint32_t announce_interval_ms;
    uint32_t lease_ms;
    nb_rtps_locator_t default_unicast;
    nb_rtps_peer_t peers[NB_RTPS_MAX_PEERS];
    size_t peer_count;
    uint32_t max_remotes;

    int multicast_enabled;
    uint8_t multicast_addr[4];
    uint8_t advertise_addr[4];
    nb_rtps_guid_t local_guid;
    nb_rtps_locator_t metatraffic_unicast;
    nb_rtps_locator_t metatraffic_multicast;

    ntk_socket *rx_sock;
    ntk_socket *unicast_sock;
    ntk_poller *poller;
    ntk_thread *thread;
    ntk_mutex *lock;
    nb_rtps_remote_participant_t *remotes;
    nb_rtps_participant_callback_t callback;
    void *callback_user;
    nb_rtps_endpoint_callback_t endpoint_callback;
    void *endpoint_user;
    nb_rtps_data_callback_t data_callback;
    void *data_user;
    nb_rtps_local_endpoint_t *local_endpoints;
    size_t max_local_endpoints;
    size_t local_endpoint_count;
    uint32_t next_entity_key;
    nb_rtps_remote_endpoint_t *remote_endpoints;
    size_t max_remote_endpoints;
    nb_rtps_reliable_writer_t *publications_writer;
    nb_rtps_reliable_writer_t *subscriptions_writer;
    uint32_t publications_hb_count;
    uint32_t subscriptions_hb_count;
    uint32_t sedp_heartbeat_ms;
    volatile int stop;
    int started;
    uint32_t rng;
    uint32_t spdp_sn;
    uint64_t next_announce_ms;
    uint64_t next_sweep_ms;
    uint64_t next_sedp_heartbeat_ms;
};

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    (void)snprintf(dst, cap, "%s", src);
}

static int parse_ipv4(const char *host, uint8_t out[4])
{
    unsigned long parts[4];
    const char *cursor = host;
    int i;

    if (!host || host[0] == '\0') {
        return NB_ERR_INVALID;
    }
    for (i = 0; i < 4; i++) {
        char *end = NULL;
        parts[i] = strtoul(cursor, &end, 10);
        if (end == cursor || parts[i] > 255u) {
            return NB_ERR_INVALID;
        }
        if (i < 3) {
            if (*end != '.') {
                return NB_ERR_INVALID;
            }
            cursor = end + 1;
        } else if (*end != '\0') {
            return NB_ERR_INVALID;
        }
    }
    for (i = 0; i < 4; i++) {
        out[i] = (uint8_t)parts[i];
    }
    return NB_OK;
}

static uint32_t lease_from_info(const nb_rtps_participant_info_t *info, uint32_t fallback)
{
    uint64_t ms;

    if (info->lease_duration.seconds <= 0) {
        return fallback;
    }
    ms = (uint64_t)(uint32_t)info->lease_duration.seconds * 1000u;
    ms += ((uint64_t)info->lease_duration.fraction * 1000u) >> 32;
    if (ms < NB_RTPS_MIN_LEASE_MS) {
        ms = NB_RTPS_MIN_LEASE_MS;
    }
    if (ms > NB_RTPS_MAX_LEASE_MS) {
        ms = NB_RTPS_MAX_LEASE_MS;
    }
    return (uint32_t)ms;
}

static void make_guid(nb_rtps_participant_t *participant, uint64_t now)
{
    uint32_t low = (uint32_t)(now & 0xffffffffu);
    uint32_t high = (uint32_t)((now >> 32) & 0xffffffffu) ^ (uint32_t)(uintptr_t)participant;
    uint8_t *p = participant->local_guid.prefix.bytes;

    p[0] = NB_RTPS_VENDOR_ID_0;
    p[1] = NB_RTPS_VENDOR_ID_1;
    p[2] = 0x4e; /* 'N' */
    p[3] = 0x42; /* 'B' */
    p[4] = (uint8_t)(low & 0xffu);
    p[5] = (uint8_t)((low >> 8) & 0xffu);
    p[6] = (uint8_t)((low >> 16) & 0xffu);
    p[7] = (uint8_t)((low >> 24) & 0xffu);
    p[8] = (uint8_t)(high & 0xffu);
    p[9] = (uint8_t)((high >> 8) & 0xffu);
    p[10] = (uint8_t)((high >> 16) & 0xffu);
    p[11] = (uint8_t)((high >> 24) & 0xffu);
    participant->local_guid.entity = nb_rtps_eid_participant;
}

static void fill_local_info(const nb_rtps_participant_t *participant,
                            nb_rtps_participant_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->vendor_id[0] = NB_RTPS_VENDOR_ID_0;
    info->vendor_id[1] = NB_RTPS_VENDOR_ID_1;
    info->guid = participant->local_guid;
    info->guid.entity = nb_rtps_eid_participant;
    info->lease_duration.seconds = (int32_t)(participant->lease_ms / 1000u);
    info->lease_duration.fraction =
        (uint32_t)(((uint64_t)(participant->lease_ms % 1000u) << 32) / 1000u);
    info->builtin_endpoints = NB_RTPS_BUILTIN_ENDPOINT_PARTICIPANT_ANNOUNCER |
                              NB_RTPS_BUILTIN_ENDPOINT_PARTICIPANT_DETECTOR |
                              NB_RTPS_BUILTIN_ENDPOINT_PUBLICATIONS_ANNOUNCER |
                              NB_RTPS_BUILTIN_ENDPOINT_PUBLICATIONS_DETECTOR |
                              NB_RTPS_BUILTIN_ENDPOINT_SUBSCRIPTIONS_ANNOUNCER |
                              NB_RTPS_BUILTIN_ENDPOINT_SUBSCRIPTIONS_DETECTOR;
    info->metatraffic_unicast = participant->metatraffic_unicast;
    if (participant->multicast_enabled) {
        info->metatraffic_multicast = participant->metatraffic_multicast;
    }
    info->default_unicast = participant->default_unicast;
    copy_str(info->name, sizeof(info->name), participant->name);
}

static void notify(nb_rtps_participant_t *participant,
                   nb_rtps_participant_event_t event,
                   const nb_rtps_participant_info_t *info)
{
    nb_rtps_participant_callback_t callback;
    void *user;

    ntk_mutex_lock(participant->lock);
    callback = participant->callback;
    user = participant->callback_user;
    ntk_mutex_unlock(participant->lock);
    if (callback) {
        callback(user, event, info);
    }
}

static void allocate_local_entity(nb_rtps_participant_t *participant,
                                  nb_rtps_endpoint_kind_t kind,
                                  nb_rtps_entity_id_t *out)
{
    uint32_t key = ++participant->next_entity_key;

    if (key == 0) {
        key = ++participant->next_entity_key;
    }
    out->bytes[0] = (uint8_t)(key & 0xffu);
    out->bytes[1] = (uint8_t)((key >> 8) & 0xffu);
    out->bytes[2] = (uint8_t)((key >> 16) & 0xffu);
    /* All locally created endpoints are keyless: user-defined writer without
     * key (0x03) and user-defined reader without key (0x04). */
    out->bytes[3] = kind == NB_RTPS_ENDPOINT_PUBLICATION ? 0x03u : 0x04u;
}

static int locator_to_host(const nb_rtps_locator_t *locator,
                           char *host,
                           size_t cap,
                           uint16_t *port)
{
    if (!locator || !host || cap == 0 || !port ||
        locator->kind != NB_RTPS_LOCATOR_KIND_UDPV4 || locator->port == 0 ||
        locator->port > 65535u) {
        return NB_ERR_INVALID;
    }
    if (snprintf(host,
                 cap,
                 "%u.%u.%u.%u",
                 (unsigned)locator->address[12],
                 (unsigned)locator->address[13],
                 (unsigned)locator->address[14],
                 (unsigned)locator->address[15]) <= 0) {
        return NB_ERR_INVALID;
    }
    *port = (uint16_t)locator->port;
    return NB_OK;
}

static int remote_target(const nb_rtps_remote_participant_t *remote,
                         char *host,
                         size_t cap,
                         uint16_t *port)
{
    if (!remote || !remote->used) {
        return NB_ERR_INVALID;
    }
    if (locator_to_host(&remote->info.metatraffic_unicast, host, cap, port) == NB_OK) {
        return NB_OK;
    }
    if (remote->source_port == 0) {
        return NB_ERR_INVALID;
    }
    copy_str(host, cap, remote->source_host);
    *port = remote->source_port;
    return NB_OK;
}

static void sedp_reader_reset(nb_rtps_sedp_reader_state_t *state, uint32_t first_sn)
{
    state->next_expected = first_sn;
    memset(state->received, 0, sizeof(state->received));
    state->ack_count = 0;
}

static int sedp_reader_mark_received(nb_rtps_sedp_reader_state_t *state, uint32_t sn)
{
    uint32_t offset;
    uint32_t i;

    if (sn < state->next_expected) {
        return 0;
    }
    offset = sn - state->next_expected;
    if (offset >= NB_RTPS_ACKNACK_MAX_BITS) {
        return 0;
    }
    state->received[offset / 32u] |= 1u << (offset % 32u);
    while ((state->received[0] & 1u) != 0) {
        for (i = 0; i + 1u < NB_RTPS_ACKNACK_MAX_WORDS; i++) {
            state->received[i] =
                (state->received[i] >> 1) | (state->received[i + 1u] << 31);
        }
        state->received[NB_RTPS_ACKNACK_MAX_WORDS - 1u] >>= 1;
        state->next_expected++;
    }
    return 1;
}

static void init_header(const nb_rtps_participant_t *participant, nb_rtps_header_t *header)
{
    memset(header, 0, sizeof(*header));
    header->version_major = NB_RTPS_PROTOCOL_VERSION_MAJOR;
    header->version_minor = NB_RTPS_PROTOCOL_VERSION_MINOR;
    header->vendor_id[0] = NB_RTPS_VENDOR_ID_0;
    header->vendor_id[1] = NB_RTPS_VENDOR_ID_1;
    header->guid_prefix = participant->local_guid.prefix;
}

static nb_rtps_local_endpoint_t *find_local_endpoint(nb_rtps_participant_t *participant,
                                                     const nb_rtps_guid_t *guid)
{
    size_t i;

    for (i = 0; i < participant->max_local_endpoints; i++) {
        if (participant->local_endpoints[i].used &&
            nb_rtps_guid_equal(&participant->local_endpoints[i].info.endpoint_guid, guid)) {
            return &participant->local_endpoints[i];
        }
    }
    return NULL;
}

static nb_rtps_remote_endpoint_t *find_remote_endpoint(nb_rtps_participant_t *participant,
                                                       const nb_rtps_guid_t *guid)
{
    size_t i;

    for (i = 0; i < participant->max_remote_endpoints; i++) {
        if (participant->remote_endpoints[i].used &&
            nb_rtps_guid_equal(&participant->remote_endpoints[i].info.endpoint_guid, guid)) {
            return &participant->remote_endpoints[i];
        }
    }
    return NULL;
}

static void notify_endpoint(nb_rtps_participant_t *participant,
                            nb_rtps_endpoint_event_t event,
                            nb_rtps_endpoint_kind_t kind,
                            const nb_rtps_endpoint_info_t *info)
{
    nb_rtps_endpoint_callback_t callback;
    void *user;

    ntk_mutex_lock(participant->lock);
    callback = participant->endpoint_callback;
    user = participant->endpoint_user;
    ntk_mutex_unlock(participant->lock);
    if (callback) {
        callback(user, event, kind, info);
    }
}

static int send_sedp_data(nb_rtps_participant_t *participant,
                          nb_rtps_endpoint_kind_t kind,
                          uint32_t sn,
                          const uint8_t *payload,
                          size_t payload_len,
                          int dispose,
                          const nb_rtps_guid_prefix_t *dst,
                          const char *host,
                          uint16_t port)
{
    uint8_t message[NB_RTPS_PACKET_MAX];
    nb_rtps_header_t header;
    nb_rtps_time_t timestamp;
    nb_rtps_data_t data;
    nb_cdr_writer_t writer;
    uint64_t now;
    size_t message_len;

    if (!participant || !payload || !host || host[0] == '\0' || port == 0 ||
        payload_len > NB_RTPS_SEDP_PAYLOAD_MAX) {
        return NB_ERR_INVALID;
    }
    init_header(participant, &header);
    now = ntk_wall_ms();
    timestamp.seconds = (int32_t)(now / 1000u);
    timestamp.fraction = (uint32_t)(((now % 1000u) << 32) / 1000u);
    memset(&data, 0, sizeof(data));
    data.reader_id = nb_rtps_eid_unknown;
    data.writer_id = kind == NB_RTPS_ENDPOINT_PUBLICATION
                         ? nb_rtps_eid_sedp_publications_writer
                         : nb_rtps_eid_sedp_subscriptions_writer;
    data.writer_sn.high = 0;
    data.writer_sn.low = sn;
    data.payload = payload;
    data.payload_len = payload_len;
    data.dispose = dispose ? 1 : 0;

    if (nb_rtps_message_init(&writer, message, sizeof(message), &header) != NB_OK ||
        nb_rtps_info_ts(&writer, &timestamp, false) != NB_OK ||
        (dst && nb_rtps_info_dst(&writer, dst) != NB_OK) ||
        nb_rtps_data(&writer, &data) != NB_OK ||
        !nb_cdr_writer_ok(&writer)) {
        return NB_ERR_BAD_MESSAGE;
    }
    message_len = nb_cdr_writer_len(&writer);
    if (ntk_socket_sendto(participant->unicast_sock, message, message_len, host, port) < 0) {
        return NB_ERR_IO;
    }
    return NB_OK;
}

static int send_user_data(nb_rtps_participant_t *participant,
                          const nb_rtps_guid_t *writer_guid,
                          const nb_rtps_guid_t *reader_guid,
                          uint32_t sn,
                          const void *payload,
                          size_t payload_len,
                          const char *host,
                          uint16_t port)
{
    uint8_t message[NB_RTPS_PACKET_MAX];
    nb_rtps_header_t header;
    nb_rtps_time_t timestamp;
    nb_rtps_data_t data;
    nb_cdr_writer_t writer;
    uint64_t now;
    size_t message_len;

    if (!participant || !writer_guid || !reader_guid || !payload ||
        payload_len == 0 || payload_len > NB_RTPS_DATA_PAYLOAD_MAX ||
        !host || host[0] == '\0' || port == 0) {
        return NB_ERR_INVALID;
    }
    init_header(participant, &header);
    now = ntk_wall_ms();
    timestamp.seconds = (int32_t)(now / 1000u);
    timestamp.fraction = (uint32_t)(((now % 1000u) << 32) / 1000u);
    memset(&data, 0, sizeof(data));
    data.reader_id = reader_guid->entity;
    data.writer_id = writer_guid->entity;
    data.writer_sn.high = 0;
    data.writer_sn.low = sn;
    data.payload = (const uint8_t *)payload;
    data.payload_len = payload_len;

    if (nb_rtps_message_init(&writer, message, sizeof(message), &header) != NB_OK ||
        nb_rtps_info_ts(&writer, &timestamp, false) != NB_OK ||
        nb_rtps_info_dst(&writer, &reader_guid->prefix) != NB_OK ||
        nb_rtps_data(&writer, &data) != NB_OK ||
        !nb_cdr_writer_ok(&writer)) {
        return NB_ERR_BAD_MESSAGE;
    }
    message_len = nb_cdr_writer_len(&writer);
    if (ntk_socket_sendto(participant->unicast_sock, message, message_len, host, port) < 0) {
        return NB_ERR_IO;
    }
    return NB_OK;
}

static void send_local_endpoint_to_remote(nb_rtps_participant_t *participant,
                                          const nb_rtps_local_endpoint_t *local,
                                          const nb_rtps_guid_prefix_t *dst,
                                          const char *host,
                                          uint16_t port)
{
    uint8_t payload[NB_RTPS_SEDP_PAYLOAD_MAX];
    size_t payload_len = 0;

    if (nb_rtps_sedp_build(&local->info, payload, sizeof(payload), &payload_len) == NB_OK) {
        (void)send_sedp_data(participant,
                             local->kind,
                             local->sn,
                             payload,
                             payload_len,
                             0,
                             dst,
                             host,
                             port);
    }
}

static void broadcast_sedp_sample(nb_rtps_participant_t *participant,
                                  nb_rtps_endpoint_kind_t kind,
                                  uint32_t sn,
                                  const uint8_t *payload,
                                  size_t payload_len,
                                  int dispose)
{
    char host[NB_RTPS_PEER_HOST_MAX];
    uint16_t port;
    uint32_t i;

    ntk_mutex_lock(participant->lock);
    for (i = 0; i < participant->max_remotes; i++) {
        nb_rtps_remote_participant_t *remote = &participant->remotes[i];
        if (!remote->used || remote_target(remote, host, sizeof(host), &port) != NB_OK) {
            continue;
        }
        (void)send_sedp_data(participant,
                             kind,
                             sn,
                             payload,
                             payload_len,
                             dispose,
                             &remote->info.guid.prefix,
                             host,
                             port);
    }
    ntk_mutex_unlock(participant->lock);
}

static void send_local_endpoint_to_remotes(nb_rtps_participant_t *participant,
                                           const nb_rtps_local_endpoint_t *local)
{
    uint8_t payload[NB_RTPS_SEDP_PAYLOAD_MAX];
    size_t payload_len = 0;

    if (nb_rtps_sedp_build(&local->info, payload, sizeof(payload), &payload_len) == NB_OK) {
        broadcast_sedp_sample(
            participant, local->kind, local->sn, payload, payload_len, 0);
    }
}

static void send_all_local_endpoints_to_remote(nb_rtps_participant_t *participant,
                                               const nb_rtps_guid_prefix_t *dst,
                                               const char *host,
                                               uint16_t port)
{
    size_t i;

    ntk_mutex_lock(participant->lock);
    for (i = 0; i < participant->max_local_endpoints; i++) {
        if (participant->local_endpoints[i].used) {
            send_local_endpoint_to_remote(
                participant, &participant->local_endpoints[i], dst, host, port);
        }
    }
    ntk_mutex_unlock(participant->lock);
}

static void send_sedp_heartbeat(nb_rtps_participant_t *participant,
                                nb_rtps_endpoint_kind_t kind,
                                const nb_rtps_remote_participant_t *remote)
{
    uint8_t message[NB_RTPS_PACKET_MAX];
    nb_rtps_header_t header;
    nb_rtps_time_t timestamp;
    nb_rtps_heartbeat_t heartbeat;
    nb_cdr_writer_t writer;
    nb_rtps_reliable_writer_t *history = kind == NB_RTPS_ENDPOINT_PUBLICATION
                                             ? participant->publications_writer
                                             : participant->subscriptions_writer;
    uint32_t first_sn = 0;
    uint32_t last_sn = 0;
    size_t sample_count = 0;
    char host[NB_RTPS_PEER_HOST_MAX];
    uint16_t port;
    uint64_t now;
    size_t message_len;

    nb_rtps_reliable_state(history, &first_sn, &last_sn, &sample_count);
    if (sample_count == 0 || remote_target(remote, host, sizeof(host), &port) != NB_OK) {
        return;
    }
    if (kind == NB_RTPS_ENDPOINT_PUBLICATION) {
        heartbeat.count = ++participant->publications_hb_count;
    } else {
        heartbeat.count = ++participant->subscriptions_hb_count;
    }

    init_header(participant, &header);
    now = ntk_wall_ms();
    timestamp.seconds = (int32_t)(now / 1000u);
    timestamp.fraction = (uint32_t)(((now % 1000u) << 32) / 1000u);
    heartbeat.reader_id = kind == NB_RTPS_ENDPOINT_PUBLICATION
                              ? nb_rtps_eid_sedp_publications_reader
                              : nb_rtps_eid_sedp_subscriptions_reader;
    heartbeat.writer_id = kind == NB_RTPS_ENDPOINT_PUBLICATION
                              ? nb_rtps_eid_sedp_publications_writer
                              : nb_rtps_eid_sedp_subscriptions_writer;
    heartbeat.first_sn.high = 0;
    heartbeat.first_sn.low = first_sn;
    heartbeat.last_sn.high = 0;
    heartbeat.last_sn.low = last_sn;

    if (nb_rtps_message_init(&writer, message, sizeof(message), &header) != NB_OK ||
        nb_rtps_info_ts(&writer, &timestamp, false) != NB_OK ||
        nb_rtps_info_dst(&writer, &remote->info.guid.prefix) != NB_OK ||
        nb_rtps_heartbeat(&writer, &heartbeat) != NB_OK ||
        !nb_cdr_writer_ok(&writer)) {
        return;
    }
    message_len = nb_cdr_writer_len(&writer);
    (void)ntk_socket_sendto(participant->unicast_sock, message, message_len, host, port);
}

static void send_sedp_heartbeats(nb_rtps_participant_t *participant)
{
    uint32_t i;

    ntk_mutex_lock(participant->lock);
    for (i = 0; i < participant->max_remotes; i++) {
        if (participant->remotes[i].used) {
            send_sedp_heartbeat(
                participant, NB_RTPS_ENDPOINT_PUBLICATION, &participant->remotes[i]);
            send_sedp_heartbeat(
                participant, NB_RTPS_ENDPOINT_SUBSCRIPTION, &participant->remotes[i]);
        }
    }
    ntk_mutex_unlock(participant->lock);
}

static void send_sedp_acknack(nb_rtps_participant_t *participant,
                              nb_rtps_endpoint_kind_t kind,
                              const nb_rtps_guid_prefix_t *dst,
                              const char *host,
                              uint16_t port,
                              uint32_t base,
                              uint32_t num_bits,
                              const uint32_t *bitmap,
                              uint32_t count)
{
    uint8_t message[NB_RTPS_PACKET_MAX];
    nb_rtps_header_t header;
    nb_rtps_time_t timestamp;
    nb_rtps_acknack_t acknack;
    nb_cdr_writer_t writer;
    uint64_t now;
    size_t message_len;

    init_header(participant, &header);
    now = ntk_wall_ms();
    timestamp.seconds = (int32_t)(now / 1000u);
    timestamp.fraction = (uint32_t)(((now % 1000u) << 32) / 1000u);
    memset(&acknack, 0, sizeof(acknack));
    acknack.reader_id = kind == NB_RTPS_ENDPOINT_PUBLICATION
                            ? nb_rtps_eid_sedp_publications_reader
                            : nb_rtps_eid_sedp_subscriptions_reader;
    acknack.writer_id = kind == NB_RTPS_ENDPOINT_PUBLICATION
                            ? nb_rtps_eid_sedp_publications_writer
                            : nb_rtps_eid_sedp_subscriptions_writer;
    acknack.base.high = 0;
    acknack.base.low = base;
    acknack.num_bits = num_bits;
    acknack.count = count;
    if (bitmap) {
        memcpy(acknack.bitmap, bitmap, sizeof(acknack.bitmap));
    }
    if (nb_rtps_message_init(&writer, message, sizeof(message), &header) != NB_OK ||
        nb_rtps_info_ts(&writer, &timestamp, false) != NB_OK ||
        (dst && nb_rtps_info_dst(&writer, dst) != NB_OK) ||
        nb_rtps_acknack(&writer, &acknack) != NB_OK ||
        !nb_cdr_writer_ok(&writer)) {
        return;
    }
    message_len = nb_cdr_writer_len(&writer);
    (void)ntk_socket_sendto(participant->unicast_sock, message, message_len, host, port);
}

static int send_spdp(nb_rtps_participant_t *participant)
{
    nb_rtps_participant_info_t info;
    uint8_t payload[NB_RTPS_SPDP_PAYLOAD_MAX];
    uint8_t message[NB_RTPS_PACKET_MAX];
    nb_rtps_header_t header;
    nb_rtps_time_t timestamp;
    nb_rtps_data_t data;
    nb_cdr_writer_t writer;
    size_t payload_len = 0;
    size_t message_len;
    uint32_t sn;
    size_t i;
    int sent = 0;
    int targets = 0;
    uint64_t now;

    fill_local_info(participant, &info);
    if (nb_rtps_spdp_build(&info, payload, sizeof(payload), &payload_len) != NB_OK) {
        return NB_ERR_BAD_MESSAGE;
    }
    memset(&header, 0, sizeof(header));
    header.version_major = NB_RTPS_PROTOCOL_VERSION_MAJOR;
    header.version_minor = NB_RTPS_PROTOCOL_VERSION_MINOR;
    header.vendor_id[0] = NB_RTPS_VENDOR_ID_0;
    header.vendor_id[1] = NB_RTPS_VENDOR_ID_1;
    header.guid_prefix = participant->local_guid.prefix;

    now = ntk_wall_ms();
    timestamp.seconds = (int32_t)(now / 1000u);
    timestamp.fraction = (uint32_t)(((now % 1000u) << 32) / 1000u);

    ntk_mutex_lock(participant->lock);
    sn = ++participant->spdp_sn;
    ntk_mutex_unlock(participant->lock);

    memset(&data, 0, sizeof(data));
    data.reader_id = nb_rtps_eid_unknown;
    data.writer_id = nb_rtps_eid_spdp_writer;
    data.writer_sn.high = 0;
    data.writer_sn.low = sn;
    data.payload = payload;
    data.payload_len = payload_len;

    if (nb_rtps_message_init(&writer, message, sizeof(message), &header) != NB_OK ||
        nb_rtps_info_ts(&writer, &timestamp, false) != NB_OK ||
        nb_rtps_data(&writer, &data) != NB_OK ||
        !nb_cdr_writer_ok(&writer)) {
        return NB_ERR_BAD_MESSAGE;
    }
    message_len = nb_cdr_writer_len(&writer);

    if (participant->multicast_enabled) {
        targets++;
        if (ntk_socket_sendto(participant->rx_sock,
                              message,
                              message_len,
                              participant->multicast_group,
                              participant->spdp_port) >= 0) {
            sent++;
        }
    }
    for (i = 0; i < participant->peer_count; i++) {
        targets++;
        if (ntk_socket_sendto(participant->rx_sock,
                              message,
                              message_len,
                              participant->peers[i].host,
                              participant->peers[i].port) >= 0) {
            sent++;
        }
    }
    if (targets == 0) {
        return NB_OK;
    }
    return sent > 0 ? NB_OK : NB_ERR_IO;
}

static nb_rtps_remote_participant_t *find_remote(nb_rtps_participant_t *participant,
                                                 const nb_rtps_guid_prefix_t *prefix)
{
    uint32_t i;

    for (i = 0; i < participant->max_remotes; i++) {
        if (participant->remotes[i].used &&
            nb_rtps_guid_prefix_equal(&participant->remotes[i].info.guid.prefix, prefix)) {
            return &participant->remotes[i];
        }
    }
    return NULL;
}

static int endpoint_data_target(nb_rtps_participant_t *participant,
                                const nb_rtps_remote_endpoint_t *endpoint,
                                char *host,
                                size_t cap,
                                uint16_t *port)
{
    nb_rtps_remote_participant_t *remote;
    size_t i;

    if (!participant || !endpoint || !host || cap == 0 || !port) {
        return NB_ERR_INVALID;
    }
    for (i = 0; i < endpoint->info.unicast_count; i++) {
        const nb_rtps_locator_t *locator = &endpoint->info.unicast[i];

        /* A 0.0.0.0 locator is not routable; fall back to the participant's
         * default locator or the SEDP source address below. */
        if (locator->address[12] == 0 && locator->address[13] == 0 &&
            locator->address[14] == 0 && locator->address[15] == 0) {
            continue;
        }
        if (locator_to_host(locator, host, cap, port) == NB_OK) {
            return NB_OK;
        }
    }

    remote = find_remote(participant, &endpoint->info.endpoint_guid.prefix);
    if (remote) {
        if (locator_to_host(&remote->info.default_unicast, host, cap, port) == NB_OK) {
            return NB_OK;
        }
        if (locator_to_host(&remote->info.metatraffic_unicast, host, cap, port) == NB_OK) {
            return NB_OK;
        }
        if (remote->source_port != 0) {
            copy_str(host, cap, remote->source_host);
            *port = remote->source_port;
            return NB_OK;
        }
    }
    return NB_ERR_NOTFOUND;
}

static void update_remote_endpoint(nb_rtps_participant_t *participant,
                                   nb_rtps_endpoint_kind_t kind,
                                   const nb_rtps_endpoint_info_t *info)
{
    nb_rtps_remote_endpoint_t *slot = NULL;
    nb_rtps_remote_endpoint_t copy;
    nb_rtps_endpoint_event_t event = NB_RTPS_ENDPOINT_UPDATED;
    uint64_t now = ntk_wall_ms();
    size_t i;

    ntk_mutex_lock(participant->lock);
    slot = find_remote_endpoint(participant, &info->endpoint_guid);
    if (slot) {
        if (memcmp(&slot->info, info, sizeof(*info)) == 0 && slot->kind == kind) {
            slot->last_seen_ms = now;
            ntk_mutex_unlock(participant->lock);
            return;
        }
    } else {
        for (i = 0; i < participant->max_remote_endpoints; i++) {
            if (!participant->remote_endpoints[i].used) {
                slot = &participant->remote_endpoints[i];
                break;
            }
        }
        if (!slot) {
            slot = &participant->remote_endpoints[0];
            for (i = 1; i < participant->max_remote_endpoints; i++) {
                if (participant->remote_endpoints[i].last_seen_ms < slot->last_seen_ms) {
                    slot = &participant->remote_endpoints[i];
                }
            }
        }
        event = NB_RTPS_ENDPOINT_ADDED;
    }
    slot->used = 1;
    slot->kind = kind;
    slot->info = *info;
    slot->last_seen_ms = now;
    copy = *slot;
    ntk_mutex_unlock(participant->lock);

    notify_endpoint(participant, event, copy.kind, &copy.info);
}

static int remove_remote_endpoint(nb_rtps_participant_t *participant,
                                  const nb_rtps_guid_t *guid,
                                  nb_rtps_endpoint_kind_t *kind,
                                  nb_rtps_endpoint_info_t *info)
{
    nb_rtps_remote_endpoint_t *slot;
    int found = 0;

    ntk_mutex_lock(participant->lock);
    slot = find_remote_endpoint(participant, guid);
    if (slot) {
        if (kind) {
            *kind = slot->kind;
        }
        if (info) {
            *info = slot->info;
        }
        slot->used = 0;
        found = 1;
    }
    ntk_mutex_unlock(participant->lock);
    return found;
}

static void remove_remote_endpoints_for_prefix(nb_rtps_participant_t *participant,
                                               const nb_rtps_guid_prefix_t *prefix)
{
    for (;;) {
        nb_rtps_remote_endpoint_t copy;
        size_t i;
        int found = 0;

        ntk_mutex_lock(participant->lock);
        for (i = 0; i < participant->max_remote_endpoints; i++) {
            nb_rtps_remote_endpoint_t *endpoint = &participant->remote_endpoints[i];
            if (endpoint->used &&
                nb_rtps_guid_prefix_equal(&endpoint->info.endpoint_guid.prefix, prefix)) {
                copy = *endpoint;
                endpoint->used = 0;
                found = 1;
                break;
            }
        }
        ntk_mutex_unlock(participant->lock);
        if (!found) {
            break;
        }
        notify_endpoint(participant, NB_RTPS_ENDPOINT_REMOVED, copy.kind, &copy.info);
    }
}

static void update_remote(nb_rtps_participant_t *participant,
                          const nb_rtps_participant_info_t *info,
                          const char *source_host,
                          uint16_t source_port)
{
    nb_rtps_remote_participant_t *slot = NULL;
    nb_rtps_participant_info_t copy;
    nb_rtps_participant_event_t event = NB_RTPS_PARTICIPANT_UPDATED;
    uint64_t now = ntk_wall_ms();
    uint32_t i;
    int notify_event = 0;
    int send_endpoints = 0;
    nb_rtps_guid_prefix_t evicted_prefix;
    int evicted = 0;

    ntk_mutex_lock(participant->lock);
    slot = find_remote(participant, &info->guid.prefix);
    if (slot) {
        notify_event = memcmp(&slot->info, info, sizeof(*info)) != 0;
        event = NB_RTPS_PARTICIPANT_UPDATED;
        if (memcmp(&slot->info.metatraffic_unicast,
                   &info->metatraffic_unicast,
                   sizeof(info->metatraffic_unicast)) != 0) {
            send_endpoints = 1;
        }
    } else {
        for (i = 0; i < participant->max_remotes; i++) {
            if (!participant->remotes[i].used) {
                slot = &participant->remotes[i];
                break;
            }
        }
        if (!slot) {
            /* Deterministic eviction: the oldest lease wins. */
            slot = &participant->remotes[0];
            for (i = 1; i < participant->max_remotes; i++) {
                if (participant->remotes[i].last_seen_ms < slot->last_seen_ms) {
                    slot = &participant->remotes[i];
                }
            }
            evicted_prefix = slot->info.guid.prefix;
            evicted = slot->used;
        }
        memset(slot, 0, sizeof(*slot));
        slot->used = 1;
        sedp_reader_reset(&slot->publications_reader, 1);
        sedp_reader_reset(&slot->subscriptions_reader, 1);
        event = NB_RTPS_PARTICIPANT_ADDED;
        notify_event = 1;
        send_endpoints = 1;
    }
    slot->info = *info;
    slot->last_seen_ms = now;
    slot->lease_ms = lease_from_info(info, participant->lease_ms);
    copy_str(slot->source_host, sizeof(slot->source_host), source_host);
    slot->source_port = source_port;
    copy = slot->info;
    ntk_mutex_unlock(participant->lock);

    if (notify_event) {
        notify(participant, event, &copy);
    }
    if (evicted) {
        remove_remote_endpoints_for_prefix(participant, &evicted_prefix);
    }
    if (send_endpoints) {
        char host[NB_RTPS_PEER_HOST_MAX];
        uint16_t port = 0;
        if (locator_to_host(&copy.metatraffic_unicast, host, sizeof(host), &port) != NB_OK) {
            copy_str(host, sizeof(host), source_host);
            port = source_port;
        }
        if (port != 0 && host[0] != '\0') {
            send_all_local_endpoints_to_remote(
                participant, &copy.guid.prefix, host, port);
        }
    }
}

static nb_rtps_sedp_reader_state_t *sedp_reader_state(nb_rtps_remote_participant_t *remote,
                                                      nb_rtps_endpoint_kind_t kind)
{
    return kind == NB_RTPS_ENDPOINT_PUBLICATION ? &remote->publications_reader
                                                : &remote->subscriptions_reader;
}

static void handle_sedp_data(nb_rtps_participant_t *participant,
                             nb_rtps_endpoint_kind_t kind,
                             const nb_rtps_data_view_t *data,
                             const nb_rtps_guid_prefix_t *source_prefix)
{
    nb_rtps_remote_participant_t *remote;
    nb_rtps_sedp_reader_state_t *state;
    nb_rtps_endpoint_info_t info;
    nb_rtps_guid_t key;

    if (data->writer_sn.high != 0) {
        return;
    }
    if (data->dispose) {
        if (nb_rtps_sedp_parse_key(data->payload, data->payload_len, &key) != NB_OK ||
            !nb_rtps_guid_prefix_equal(&key.prefix, source_prefix)) {
            return;
        }
        ntk_mutex_lock(participant->lock);
        remote = find_remote(participant, source_prefix);
        if (remote) {
            state = sedp_reader_state(remote, kind);
            (void)sedp_reader_mark_received(state, data->writer_sn.low);
        }
        ntk_mutex_unlock(participant->lock);
        if (remote) {
            nb_rtps_endpoint_kind_t removed_kind;
            if (remove_remote_endpoint(participant, &key, &removed_kind, &info)) {
                notify_endpoint(
                    participant, NB_RTPS_ENDPOINT_REMOVED, removed_kind, &info);
            }
        }
        return;
    }
    if (nb_rtps_sedp_parse(data->payload, data->payload_len, &info) != NB_OK ||
        !nb_rtps_guid_prefix_equal(&info.endpoint_guid.prefix, source_prefix)) {
        return;
    }
    ntk_mutex_lock(participant->lock);
    remote = find_remote(participant, source_prefix);
    if (remote) {
        state = sedp_reader_state(remote, kind);
        (void)sedp_reader_mark_received(state, data->writer_sn.low);
    }
    ntk_mutex_unlock(participant->lock);
    if (remote) {
        update_remote_endpoint(participant, kind, &info);
    }
}

static void handle_sedp_heartbeat(nb_rtps_participant_t *participant,
                                  nb_rtps_endpoint_kind_t kind,
                                  const nb_rtps_heartbeat_t *heartbeat,
                                  const nb_rtps_guid_prefix_t *source_prefix,
                                  const char *source_host,
                                  uint16_t source_port)
{
    nb_rtps_remote_participant_t *remote;
    nb_rtps_sedp_reader_state_t *state;
    char target_host[NB_RTPS_PEER_HOST_MAX];
    uint16_t target_port = 0;
    uint32_t base;
    uint32_t num_bits = 0;
    uint32_t bitmap[NB_RTPS_ACKNACK_MAX_WORDS];
    uint32_t count;
    uint32_t first_sn;
    uint32_t last_sn;
    uint32_t i;

    if (heartbeat->first_sn.high != 0 || heartbeat->last_sn.high != 0) {
        return;
    }
    first_sn = heartbeat->first_sn.low ? heartbeat->first_sn.low : 1u;
    last_sn = heartbeat->last_sn.low;
    memset(bitmap, 0, sizeof(bitmap));

    ntk_mutex_lock(participant->lock);
    remote = find_remote(participant, source_prefix);
    if (!remote) {
        ntk_mutex_unlock(participant->lock);
        return;
    }
    if (remote_target(remote, target_host, sizeof(target_host), &target_port) != NB_OK) {
        copy_str(target_host, sizeof(target_host), source_host);
        target_port = source_port;
    }
    state = sedp_reader_state(remote, kind);
    if (first_sn > state->next_expected) {
        sedp_reader_reset(state, first_sn);
    } else if (last_sn + 1u < state->next_expected) {
        /* The writer restarted and reused sequence numbers. */
        sedp_reader_reset(state, first_sn);
    }
    base = state->next_expected;
    if (last_sn >= base) {
        uint64_t span = (uint64_t)last_sn - (uint64_t)base + 1u;
        num_bits = span > NB_RTPS_ACKNACK_MAX_BITS
                       ? NB_RTPS_ACKNACK_MAX_BITS
                       : (uint32_t)span;
        for (i = 0; i < num_bits; i++) {
            if ((state->received[i / 32u] & (1u << (i % 32u))) == 0) {
                bitmap[i / 32u] |= 1u << (31u - (i % 32u));
            }
        }
    }
    count = ++state->ack_count;
    ntk_mutex_unlock(participant->lock);

    send_sedp_acknack(participant,
                      kind,
                      source_prefix,
                      target_host,
                      target_port,
                      base,
                      num_bits,
                      bitmap,
                      count);
}

typedef struct sedp_resend_context {
    nb_rtps_participant_t *participant;
    nb_rtps_endpoint_kind_t kind;
    const nb_rtps_guid_prefix_t *dst;
    const char *host;
    uint16_t port;
} sedp_resend_context_t;

static int sedp_resend_callback(void *user,
                                uint32_t sn,
                                const uint8_t *payload,
                                size_t len,
                                int dispose)
{
    sedp_resend_context_t *context = (sedp_resend_context_t *)user;

    return send_sedp_data(context->participant,
                          context->kind,
                          sn,
                          payload,
                          len,
                          dispose,
                          context->dst,
                          context->host,
                          context->port);
}

static void handle_sedp_acknack(nb_rtps_participant_t *participant,
                                nb_rtps_endpoint_kind_t kind,
                                const nb_rtps_acknack_t *acknack,
                                const nb_rtps_guid_prefix_t *source_prefix,
                                const char *source_host,
                                uint16_t source_port)
{
    nb_rtps_reliable_writer_t *history = kind == NB_RTPS_ENDPOINT_PUBLICATION
                                             ? participant->publications_writer
                                             : participant->subscriptions_writer;
    nb_rtps_remote_participant_t *remote;
    char target_host[NB_RTPS_PEER_HOST_MAX];
    uint16_t target_port = source_port;
    sedp_resend_context_t context;

    copy_str(target_host, sizeof(target_host), source_host);
    ntk_mutex_lock(participant->lock);
    remote = find_remote(participant, source_prefix);
    if (remote &&
        remote_target(remote, target_host, sizeof(target_host), &target_port) != NB_OK) {
        copy_str(target_host, sizeof(target_host), source_host);
        target_port = source_port;
    }
    ntk_mutex_unlock(participant->lock);

    context.participant = participant;
    context.kind = kind;
    context.dst = source_prefix;
    context.host = target_host;
    context.port = target_port;
    (void)nb_rtps_reliable_process_acknack(
        history, acknack, sedp_resend_callback, &context, NULL);
}

static int data_endpoint_matches(const nb_rtps_local_endpoint_t *local,
                                 const nb_rtps_remote_endpoint_t *remote)
{
    uint32_t local_reliability;
    uint32_t remote_reliability;

    if (!local || !remote || local->kind != NB_RTPS_ENDPOINT_SUBSCRIPTION ||
        remote->kind != NB_RTPS_ENDPOINT_PUBLICATION) {
        return 0;
    }
    if (strcmp(local->info.topic_name, remote->info.topic_name) != 0 ||
        strcmp(local->info.type_name, remote->info.type_name) != 0 ||
        strcmp(local->info.partition, remote->info.partition) != 0) {
        return 0;
    }
    local_reliability =
        local->info.reliability ? local->info.reliability : NB_RTPS_RELIABILITY_BEST_EFFORT;
    remote_reliability =
        remote->info.reliability ? remote->info.reliability : NB_RTPS_RELIABILITY_BEST_EFFORT;
    if (local_reliability == NB_RTPS_RELIABILITY_RELIABLE &&
        remote_reliability != NB_RTPS_RELIABILITY_RELIABLE) {
        return 0;
    }
    if (remote->info.durability > local->info.durability) {
        return 0;
    }
    return 1;
}

static void handle_user_data(nb_rtps_participant_t *participant,
                             const nb_rtps_data_view_t *data,
                             const nb_rtps_guid_prefix_t *source_prefix)
{
    nb_rtps_dds_message_t message;
    nb_rtps_guid_t writer_guid;
    nb_rtps_guid_t readers[NB_RTPS_MAX_DATA_TARGETS];
    nb_rtps_data_callback_t callback;
    void *user;
    nb_rtps_remote_endpoint_t *remote;
    size_t reader_count = 0;
    size_t i;

    if (!participant || !data || !source_prefix || data->dispose || data->key) {
        return;
    }
    if (nb_rtps_dds_message_deserialize(
            data->payload, data->payload_len, &message) != NB_OK) {
        return;
    }
    writer_guid.prefix = *source_prefix;
    writer_guid.entity = data->writer_id;

    ntk_mutex_lock(participant->lock);
    callback = participant->data_callback;
    user = participant->data_user;
    if (callback) {
        remote = find_remote_endpoint(participant, &writer_guid);
        if (remote) {
            for (i = 0; i < participant->max_local_endpoints; i++) {
                nb_rtps_local_endpoint_t *local = &participant->local_endpoints[i];

                if (!local->used ||
                    !data_endpoint_matches(local, remote) ||
                    (!nb_rtps_entity_id_equal(&data->reader_id, &nb_rtps_eid_unknown) &&
                     !nb_rtps_entity_id_equal(&data->reader_id,
                                              &local->info.endpoint_guid.entity))) {
                    continue;
                }
                if (reader_count == NB_RTPS_MAX_DATA_TARGETS) {
                    break;
                }
                readers[reader_count++] = local->info.endpoint_guid;
            }
        }
    }
    ntk_mutex_unlock(participant->lock);

    for (i = 0; i < reader_count; i++) {
        callback(user, &readers[i], &writer_guid, &message);
    }
}

static void handle_datagram(nb_rtps_participant_t *participant,
                            const uint8_t *buf,
                            size_t len,
                            const char *source_host,
                            uint16_t source_port)
{
    nb_rtps_header_t header;
    nb_rtps_submessage_view_t view;
    size_t offset = 0;
    int rc;

    if (nb_rtps_message_begin(buf, len, &header, &offset) != NB_OK) {
        return;
    }
    if (nb_rtps_guid_prefix_equal(&header.guid_prefix, &participant->local_guid.prefix)) {
        return;
    }
    while ((rc = nb_rtps_message_next(buf, len, &offset, &view)) == 1) {
        if (view.id == NB_RTPS_SUBMSG_DATA) {
            nb_rtps_data_view_t data;
            nb_rtps_participant_info_t info;

            if (nb_rtps_data_decode(view.flags, view.body, view.body_len, &data) != NB_OK) {
                continue;
            }
            if (nb_rtps_entity_id_equal(&data.writer_id, &nb_rtps_eid_spdp_writer)) {
                if (nb_rtps_spdp_parse(data.payload, data.payload_len, &info) == NB_OK &&
                    nb_rtps_guid_prefix_equal(&info.guid.prefix, &header.guid_prefix)) {
                    update_remote(participant, &info, source_host, source_port);
                }
            } else if (nb_rtps_entity_id_equal(&data.writer_id,
                                               &nb_rtps_eid_sedp_publications_writer)) {
                handle_sedp_data(
                    participant, NB_RTPS_ENDPOINT_PUBLICATION, &data, &header.guid_prefix);
            } else if (nb_rtps_entity_id_equal(&data.writer_id,
                                               &nb_rtps_eid_sedp_subscriptions_writer)) {
                handle_sedp_data(
                    participant, NB_RTPS_ENDPOINT_SUBSCRIPTION, &data, &header.guid_prefix);
            } else {
                handle_user_data(participant, &data, &header.guid_prefix);
            }
        } else if (view.id == NB_RTPS_SUBMSG_HEARTBEAT) {
            nb_rtps_heartbeat_t heartbeat;

            if (nb_rtps_heartbeat_decode(view.body, view.body_len, &heartbeat) != NB_OK) {
                continue;
            }
            if (nb_rtps_entity_id_equal(&heartbeat.writer_id,
                                        &nb_rtps_eid_sedp_publications_writer)) {
                handle_sedp_heartbeat(participant,
                                      NB_RTPS_ENDPOINT_PUBLICATION,
                                      &heartbeat,
                                      &header.guid_prefix,
                                      source_host,
                                      source_port);
            } else if (nb_rtps_entity_id_equal(&heartbeat.writer_id,
                                               &nb_rtps_eid_sedp_subscriptions_writer)) {
                handle_sedp_heartbeat(participant,
                                      NB_RTPS_ENDPOINT_SUBSCRIPTION,
                                      &heartbeat,
                                      &header.guid_prefix,
                                      source_host,
                                      source_port);
            }
        } else if (view.id == NB_RTPS_SUBMSG_ACKNACK) {
            nb_rtps_acknack_t acknack;

            if (nb_rtps_acknack_decode(view.body, view.body_len, &acknack) != NB_OK) {
                continue;
            }
            if (nb_rtps_entity_id_equal(&acknack.writer_id,
                                        &nb_rtps_eid_sedp_publications_writer)) {
                handle_sedp_acknack(participant,
                                    NB_RTPS_ENDPOINT_PUBLICATION,
                                    &acknack,
                                    &header.guid_prefix,
                                    source_host,
                                    source_port);
            } else if (nb_rtps_entity_id_equal(&acknack.writer_id,
                                               &nb_rtps_eid_sedp_subscriptions_writer)) {
                handle_sedp_acknack(participant,
                                    NB_RTPS_ENDPOINT_SUBSCRIPTION,
                                    &acknack,
                                    &header.guid_prefix,
                                    source_host,
                                    source_port);
            }
        }
    }
}

static void drain_socket(nb_rtps_participant_t *participant, ntk_socket *socket)
{
    uint8_t buf[NB_RTPS_PACKET_MAX];
    char host[NB_RTPS_PEER_HOST_MAX];
    uint16_t port;

    for (;;) {
        ntk_ssize_t n = ntk_socket_recvfrom(
            socket, buf, sizeof(buf), host, sizeof(host), &port);
        if (n <= 0) {
            break;
        }
        handle_datagram(participant, buf, (size_t)n, host, port);
    }
}

static void sweep(nb_rtps_participant_t *participant, uint64_t now)
{
    for (;;) {
        nb_rtps_participant_info_t info;
        int found = 0;
        uint32_t i;

        ntk_mutex_lock(participant->lock);
        for (i = 0; i < participant->max_remotes; i++) {
            nb_rtps_remote_participant_t *remote = &participant->remotes[i];
            if (remote->used && now >= remote->last_seen_ms &&
                now - remote->last_seen_ms > remote->lease_ms) {
                info = remote->info;
                remote->used = 0;
                found = 1;
                break;
            }
        }
        ntk_mutex_unlock(participant->lock);
        if (!found) {
            break;
        }
        notify(participant, NB_RTPS_PARTICIPANT_REMOVED, &info);
        remove_remote_endpoints_for_prefix(participant, &info.guid.prefix);
    }
}

static uint64_t next_announce_delay(nb_rtps_participant_t *participant)
{
    uint32_t window = participant->announce_interval_ms / 5u;
    int64_t delay;
    uint32_t random;

    if (window == 0) {
        return participant->announce_interval_ms;
    }
    participant->rng = participant->rng * 1103515245u + 12345u;
    random = participant->rng % (window * 2u + 1u);
    delay = (int64_t)participant->announce_interval_ms + (int64_t)random - (int64_t)window;
    if (delay < (int64_t)(participant->announce_interval_ms / 2u)) {
        delay = participant->announce_interval_ms / 2u;
    }
    if (delay < (int64_t)NB_RTPS_MIN_ANNOUNCE_MS) {
        delay = NB_RTPS_MIN_ANNOUNCE_MS;
    }
    return (uint64_t)delay;
}

static void thread_main(void *user)
{
    nb_rtps_participant_t *participant = (nb_rtps_participant_t *)user;

    while (!participant->stop) {
        int rc = ntk_poller_poll(participant->poller, 100);
        uint64_t now;

        if (rc > 0) {
            drain_socket(participant, participant->rx_sock);
            drain_socket(participant, participant->unicast_sock);
        }
        now = ntk_wall_ms();
        if (now >= participant->next_announce_ms) {
            (void)send_spdp(participant);
            participant->next_announce_ms = now + next_announce_delay(participant);
        }
        if (now >= participant->next_sweep_ms) {
            sweep(participant, now);
            participant->next_sweep_ms = now + NB_RTPS_SWEEP_INTERVAL_MS;
        }
        if (now >= participant->next_sedp_heartbeat_ms) {
            send_sedp_heartbeats(participant);
            participant->next_sedp_heartbeat_ms = now + participant->sedp_heartbeat_ms;
        }
    }
}

static void free_participant(nb_rtps_participant_t *participant)
{
    if (!participant) {
        return;
    }
    if (participant->started) {
        nb_rtps_participant_stop(participant);
    }
    if (participant->poller) {
        ntk_poller_destroy(participant->poller);
    }
    if (participant->rx_sock) {
        ntk_socket_destroy(participant->rx_sock);
    }
    if (participant->unicast_sock) {
        ntk_socket_destroy(participant->unicast_sock);
    }
    if (participant->lock) {
        ntk_mutex_destroy(participant->lock);
    }
    nb_rtps_reliable_writer_destroy(participant->publications_writer);
    nb_rtps_reliable_writer_destroy(participant->subscriptions_writer);
    free(participant->local_endpoints);
    free(participant->remote_endpoints);
    free(participant->remotes);
    free(participant);
}

nb_rtps_participant_t *nb_rtps_participant_create(const nb_rtps_participant_config_t *config)
{
    nb_rtps_participant_t *participant;
    uint32_t domain;
    uint32_t spdp_port;
    uint32_t unicast_port;
    uint64_t now;
    size_t i;
    char local_ip[NB_RTPS_PEER_HOST_MAX];
    uint16_t local_port = 0;

    if (!config) {
        return NULL;
    }
    participant = (nb_rtps_participant_t *)calloc(1, sizeof(*participant));
    if (!participant) {
        return NULL;
    }
    participant->domain_id = config->domain_id;
    copy_str(participant->bind_host,
             sizeof(participant->bind_host),
             config->bind_host && config->bind_host[0] ? config->bind_host : "0.0.0.0");
    copy_str(participant->name, sizeof(participant->name), config->name);
    participant->announce_interval_ms =
        config->announce_interval_ms ? config->announce_interval_ms : NB_RTPS_DEFAULT_ANNOUNCE_MS;
    if (participant->announce_interval_ms < NB_RTPS_MIN_ANNOUNCE_MS) {
        participant->announce_interval_ms = NB_RTPS_MIN_ANNOUNCE_MS;
    }
    participant->lease_ms = config->lease_ms ? config->lease_ms : NB_RTPS_DEFAULT_LEASE_MS;
    if (participant->lease_ms < NB_RTPS_MIN_LEASE_MS) {
        participant->lease_ms = NB_RTPS_MIN_LEASE_MS;
    }
    if (participant->lease_ms > NB_RTPS_MAX_LEASE_MS) {
        participant->lease_ms = NB_RTPS_MAX_LEASE_MS;
    }
    participant->max_remotes =
        config->max_remote_participants ? config->max_remote_participants
                                        : NB_RTPS_DEFAULT_MAX_REMOTES;
    if (participant->max_remotes > 65536u) {
        participant->max_remotes = 65536u;
    }
    participant->max_local_endpoints =
        config->max_local_endpoints ? config->max_local_endpoints
                                    : NB_RTPS_DEFAULT_MAX_LOCAL_ENDPOINTS;
    if (participant->max_local_endpoints > NB_RTPS_MAX_LOCAL_ENDPOINTS) {
        participant->max_local_endpoints = NB_RTPS_MAX_LOCAL_ENDPOINTS;
    }
    participant->max_remote_endpoints =
        config->max_remote_endpoints ? config->max_remote_endpoints
                                     : NB_RTPS_DEFAULT_MAX_REMOTE_ENDPOINTS;
    if (participant->max_remote_endpoints > NB_RTPS_MAX_REMOTE_ENDPOINTS) {
        participant->max_remote_endpoints = NB_RTPS_MAX_REMOTE_ENDPOINTS;
    }
    participant->sedp_heartbeat_ms =
        config->sedp_heartbeat_ms ? config->sedp_heartbeat_ms
                                  : NB_RTPS_DEFAULT_SEDP_HEARTBEAT_MS;
    if (participant->sedp_heartbeat_ms < NB_RTPS_MIN_SEDP_HEARTBEAT_MS) {
        participant->sedp_heartbeat_ms = NB_RTPS_MIN_SEDP_HEARTBEAT_MS;
    }
    participant->default_unicast = config->default_unicast;
    if (config->peer_count > 0 && !config->peers) {
        free_participant(participant);
        return NULL;
    }
    if (config->peer_count > NB_RTPS_MAX_PEERS) {
        free_participant(participant);
        return NULL;
    }
    for (i = 0; i < config->peer_count; i++) {
        copy_str(participant->peers[i].host,
                 sizeof(participant->peers[i].host),
                 config->peers[i].host);
        participant->peers[i].port = config->peers[i].port;
    }
    participant->peer_count = config->peer_count;

    domain = config->domain_id;
    if (domain > 232u) {
        free_participant(participant);
        return NULL;
    }
    copy_str(participant->multicast_group,
             sizeof(participant->multicast_group),
             config->multicast_group ? config->multicast_group : "239.255.0.1");
    participant->multicast_enabled = participant->multicast_group[0] != '\0';
    if (config->multicast_group && config->multicast_group[0] == '\0') {
        participant->multicast_enabled = 0;
    }
    if (participant->multicast_enabled) {
        if (parse_ipv4(participant->multicast_group, participant->multicast_addr) != NB_OK) {
            free_participant(participant);
            return NULL;
        }
    }
    if (config->spdp_port) {
        spdp_port = config->spdp_port;
    } else if (participant->multicast_enabled) {
        spdp_port = NB_RTPS_SPDP_BASE_PORT + NB_RTPS_SPDP_DOMAIN_GAIN * domain;
    } else {
        spdp_port = 0;
    }
    if (spdp_port > 65535u) {
        free_participant(participant);
        return NULL;
    }
    unicast_port = config->unicast_port;

    if (!config->advertise_host || !config->advertise_host[0]) {
        if (strcmp(participant->bind_host, "0.0.0.0") != 0) {
            copy_str(participant->advertise_host,
                     sizeof(participant->advertise_host),
                     participant->bind_host);
        } else {
            copy_str(participant->advertise_host,
                     sizeof(participant->advertise_host),
                     "127.0.0.1");
        }
    } else {
        copy_str(participant->advertise_host,
                 sizeof(participant->advertise_host),
                 config->advertise_host);
    }
    if (parse_ipv4(participant->advertise_host, participant->advertise_addr) != NB_OK) {
        free_participant(participant);
        return NULL;
    }

    participant->lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    participant->remotes =
        (nb_rtps_remote_participant_t *)calloc(participant->max_remotes,
                                               sizeof(nb_rtps_remote_participant_t));
    participant->local_endpoints =
        (nb_rtps_local_endpoint_t *)calloc(participant->max_local_endpoints,
                                           sizeof(nb_rtps_local_endpoint_t));
    participant->remote_endpoints =
        (nb_rtps_remote_endpoint_t *)calloc(participant->max_remote_endpoints,
                                            sizeof(nb_rtps_remote_endpoint_t));
    participant->publications_writer =
        nb_rtps_reliable_writer_create(participant->max_local_endpoints);
    participant->subscriptions_writer =
        nb_rtps_reliable_writer_create(participant->max_local_endpoints);
    if (!participant->lock || !participant->remotes || !participant->local_endpoints ||
        !participant->remote_endpoints || !participant->publications_writer ||
        !participant->subscriptions_writer) {
        free_participant(participant);
        return NULL;
    }

    participant->rx_sock = ntk_socket_create();
    if (!participant->rx_sock) {
        free_participant(participant);
        return NULL;
    }
    if (ntk_socket_bind_udp(participant->rx_sock,
                            participant->bind_host,
                            (uint16_t)spdp_port,
                            1) != NTK_OK) {
        free_participant(participant);
        return NULL;
    }
    local_ip[0] = '\0';
    if (ntk_socket_get_local(participant->rx_sock,
                             local_ip,
                             sizeof(local_ip),
                             &local_port) != NTK_OK ||
        local_port == 0) {
        free_participant(participant);
        return NULL;
    }
    participant->spdp_port = local_port;
    if (participant->multicast_enabled) {
        const char *local_if =
            strcmp(participant->bind_host, "0.0.0.0") == 0 ? NULL : participant->bind_host;
        if (ntk_socket_join_multicast(participant->rx_sock,
                                      participant->multicast_group,
                                      local_if) != NTK_OK) {
            free_participant(participant);
            return NULL;
        }
        (void)ntk_socket_set_multicast_ttl(participant->rx_sock, 1);
        (void)ntk_socket_set_multicast_loop(participant->rx_sock, 1);
    }

    participant->unicast_sock = ntk_socket_create();
    if (!participant->unicast_sock) {
        free_participant(participant);
        return NULL;
    }
    if (ntk_socket_bind_udp(participant->unicast_sock,
                            participant->bind_host,
                            (uint16_t)unicast_port,
                            0) != NTK_OK) {
        free_participant(participant);
        return NULL;
    }
    local_ip[0] = '\0';
    local_port = 0;
    if (ntk_socket_get_local(participant->unicast_sock,
                             local_ip,
                             sizeof(local_ip),
                             &local_port) != NTK_OK ||
        local_port == 0) {
        free_participant(participant);
        return NULL;
    }
    participant->unicast_port = local_port;
    nb_rtps_locator_ipv4(&participant->metatraffic_unicast,
                         participant->advertise_addr[0],
                         participant->advertise_addr[1],
                         participant->advertise_addr[2],
                         participant->advertise_addr[3],
                         participant->unicast_port);
    if (participant->default_unicast.kind == NB_RTPS_LOCATOR_KIND_INVALID) {
        /* SPDP peers such as Cyclone DDS require a default unicast locator in
         * addition to the metatraffic locator before they create a proxy
         * participant. With no RTPS data plane yet, reuse the discovery
         * unicast endpoint. */
        participant->default_unicast = participant->metatraffic_unicast;
    }
    if (participant->multicast_enabled) {
        nb_rtps_locator_ipv4(&participant->metatraffic_multicast,
                             participant->multicast_addr[0],
                             participant->multicast_addr[1],
                             participant->multicast_addr[2],
                             participant->multicast_addr[3],
                             participant->spdp_port);
    }

    now = ntk_wall_ms();
    make_guid(participant, now);
    participant->rng = (uint32_t)(now ^ (uint32_t)(uintptr_t)participant);
    if (participant->rng == 0) {
        participant->rng = 1;
    }
    return participant;
}

int nb_rtps_participant_start(nb_rtps_participant_t *participant)
{
    if (!participant || participant->started) {
        return NB_ERR_STATE;
    }
    participant->poller = ntk_poller_create();
    if (!participant->poller) {
        return NB_ERR_NOMEM;
    }
    if (ntk_poller_add(participant->poller,
                       ntk_socket_fd(participant->rx_sock),
                       NTK_POLL_IN,
                       NULL,
                       NULL) != NTK_OK ||
        ntk_poller_add(participant->poller,
                       ntk_socket_fd(participant->unicast_sock),
                       NTK_POLL_IN,
                       NULL,
                       NULL) != NTK_OK) {
        ntk_poller_destroy(participant->poller);
        participant->poller = NULL;
        return NB_ERR_IO;
    }
    participant->stop = 0;
    participant->next_announce_ms = 0;
    participant->next_sweep_ms = 0;
    participant->next_sedp_heartbeat_ms = 0;
    participant->thread = ntk_thread_create(thread_main, participant);
    if (!participant->thread) {
        ntk_poller_destroy(participant->poller);
        participant->poller = NULL;
        return NB_ERR_NOMEM;
    }
    participant->started = 1;
    return NB_OK;
}

void nb_rtps_participant_stop(nb_rtps_participant_t *participant)
{
    uint32_t i;

    if (!participant || !participant->started) {
        return;
    }
    participant->stop = 1;
    if (participant->poller) {
        (void)ntk_poller_wake(participant->poller);
    }
    if (participant->thread) {
        (void)ntk_thread_join(participant->thread);
        ntk_thread_destroy(participant->thread);
        participant->thread = NULL;
    }
    if (participant->poller) {
        ntk_poller_destroy(participant->poller);
        participant->poller = NULL;
    }
    participant->started = 0;
    ntk_mutex_lock(participant->lock);
    for (i = 0; i < participant->max_remotes; i++) {
        participant->remotes[i].used = 0;
    }
    for (i = 0; i < participant->max_remote_endpoints; i++) {
        participant->remote_endpoints[i].used = 0;
    }
    ntk_mutex_unlock(participant->lock);
}

void nb_rtps_participant_destroy(nb_rtps_participant_t *participant)
{
    free_participant(participant);
}

void nb_rtps_participant_set_callback(nb_rtps_participant_t *participant,
                                      nb_rtps_participant_callback_t callback,
                                      void *user)
{
    if (!participant) {
        return;
    }
    ntk_mutex_lock(participant->lock);
    participant->callback = callback;
    participant->callback_user = user;
    ntk_mutex_unlock(participant->lock);
}

void nb_rtps_participant_set_endpoint_callback(nb_rtps_participant_t *participant,
                                               nb_rtps_endpoint_callback_t callback,
                                               void *user)
{
    if (!participant) {
        return;
    }
    ntk_mutex_lock(participant->lock);
    participant->endpoint_callback = callback;
    participant->endpoint_user = user;
    ntk_mutex_unlock(participant->lock);
}

void nb_rtps_participant_set_data_callback(nb_rtps_participant_t *participant,
                                           nb_rtps_data_callback_t callback,
                                           void *user)
{
    if (!participant) {
        return;
    }
    ntk_mutex_lock(participant->lock);
    participant->data_callback = callback;
    participant->data_user = user;
    ntk_mutex_unlock(participant->lock);
}

int nb_rtps_participant_add_endpoint(nb_rtps_participant_t *participant,
                                     nb_rtps_endpoint_kind_t kind,
                                     nb_rtps_endpoint_info_t *info)
{
    nb_rtps_local_endpoint_t local;
    nb_rtps_reliable_writer_t *history;
    uint8_t payload[NB_RTPS_SEDP_PAYLOAD_MAX];
    size_t payload_len = 0;
    size_t slot = 0;
    size_t i;
    uint32_t sn = 0;
    int rc;

    if (!participant || !info ||
        (kind != NB_RTPS_ENDPOINT_PUBLICATION && kind != NB_RTPS_ENDPOINT_SUBSCRIPTION) ||
        info->topic_name[0] == '\0' || info->type_name[0] == '\0') {
        return NB_ERR_INVALID;
    }
    memset(&local, 0, sizeof(local));
    local.used = 1;
    local.kind = kind;
    local.info = *info;
    local.info.endpoint_guid.prefix = participant->local_guid.prefix;

    ntk_mutex_lock(participant->lock);
    for (i = 0; i < participant->max_local_endpoints; i++) {
        if (!participant->local_endpoints[i].used) {
            slot = i;
            break;
        }
        if (i + 1u == participant->max_local_endpoints) {
            ntk_mutex_unlock(participant->lock);
            return NB_ERR_TOOBIG;
        }
    }
    allocate_local_entity(participant, kind, &local.info.endpoint_guid.entity);
    if (local.info.unicast_count == 0) {
        const nb_rtps_locator_t *fallback = NULL;

        if (participant->default_unicast.kind == NB_RTPS_LOCATOR_KIND_UDPV4 &&
            participant->default_unicast.port != 0) {
            fallback = &participant->default_unicast;
        } else if (participant->metatraffic_unicast.kind == NB_RTPS_LOCATOR_KIND_UDPV4 &&
                   participant->metatraffic_unicast.port != 0) {
            fallback = &participant->metatraffic_unicast;
        }
        if (fallback) {
            local.info.unicast[0] = *fallback;
            local.info.unicast_count = 1;
        }
    }
    if (nb_rtps_sedp_build(&local.info, payload, sizeof(payload), &payload_len) != NB_OK) {
        ntk_mutex_unlock(participant->lock);
        return NB_ERR_BAD_MESSAGE;
    }
    history = kind == NB_RTPS_ENDPOINT_PUBLICATION ? participant->publications_writer
                                                   : participant->subscriptions_writer;
    rc = nb_rtps_reliable_add(history, payload, payload_len, 0, &sn);
    if (rc != NB_OK) {
        ntk_mutex_unlock(participant->lock);
        return rc;
    }
    local.sn = sn;
    participant->local_endpoints[slot] = local;
    participant->local_endpoint_count++;
    *info = local.info;
    ntk_mutex_unlock(participant->lock);

    send_local_endpoint_to_remotes(participant, &local);
    return NB_OK;
}

int nb_rtps_participant_remove_endpoint(nb_rtps_participant_t *participant,
                                        const nb_rtps_guid_t *endpoint_guid)
{
    nb_rtps_local_endpoint_t local;
    nb_rtps_reliable_writer_t *history;
    nb_rtps_local_endpoint_t *slot;
    uint8_t payload[NB_RTPS_SEDP_PAYLOAD_MAX];
    size_t payload_len = 0;
    uint32_t sn = 0;
    int rc;

    if (!participant || !endpoint_guid) {
        return NB_ERR_INVALID;
    }
    ntk_mutex_lock(participant->lock);
    slot = find_local_endpoint(participant, endpoint_guid);
    if (!slot) {
        ntk_mutex_unlock(participant->lock);
        return NB_ERR_NOTFOUND;
    }
    local = *slot;
    slot->used = 0;
    if (participant->local_endpoint_count > 0) {
        participant->local_endpoint_count--;
    }
    ntk_mutex_unlock(participant->lock);

    if (nb_rtps_sedp_build(&local.info, payload, sizeof(payload), &payload_len) != NB_OK) {
        return NB_ERR_BAD_MESSAGE;
    }
    history = local.kind == NB_RTPS_ENDPOINT_PUBLICATION ? participant->publications_writer
                                                         : participant->subscriptions_writer;
    rc = nb_rtps_reliable_add(history, payload, payload_len, 1, &sn);
    if (rc != NB_OK) {
        return rc;
    }
    broadcast_sedp_sample(
        participant, local.kind, sn, payload, payload_len, 1);
    return NB_OK;
}

int nb_rtps_participant_publish(nb_rtps_participant_t *participant,
                                const nb_rtps_guid_t *writer_guid,
                                const void *payload,
                                size_t payload_len)
{
    nb_rtps_local_endpoint_t *local;
    uint32_t sn;
    uint32_t local_reliability;
    size_t i;
    int targets = 0;
    int sent = 0;
    int last_rc = NB_ERR_IO;

    if (!participant || !writer_guid || !payload || payload_len == 0 ||
        payload_len > NB_RTPS_DATA_PAYLOAD_MAX) {
        return NB_ERR_INVALID;
    }

    ntk_mutex_lock(participant->lock);
    local = find_local_endpoint(participant, writer_guid);
    if (!local || local->kind != NB_RTPS_ENDPOINT_PUBLICATION) {
        ntk_mutex_unlock(participant->lock);
        return NB_ERR_NOTFOUND;
    }
    sn = local->data_sn + 1u;
    if (sn == 0) {
        sn = 1;
    }
    local->data_sn = sn;
    local_reliability = local->info.reliability
                            ? local->info.reliability
                            : NB_RTPS_RELIABILITY_BEST_EFFORT;

    for (i = 0; i < participant->max_remote_endpoints; i++) {
        nb_rtps_remote_endpoint_t *remote = &participant->remote_endpoints[i];
        char host[NB_RTPS_PEER_HOST_MAX];
        uint16_t port = 0;
        uint32_t remote_reliability;

        if (!remote->used || remote->kind != NB_RTPS_ENDPOINT_SUBSCRIPTION) {
            continue;
        }
        if (strcmp(remote->info.topic_name, local->info.topic_name) != 0 ||
            strcmp(remote->info.type_name, local->info.type_name) != 0 ||
            strcmp(remote->info.partition, local->info.partition) != 0) {
            continue;
        }
        remote_reliability = remote->info.reliability
                                 ? remote->info.reliability
                                 : NB_RTPS_RELIABILITY_BEST_EFFORT;
        if (remote_reliability == NB_RTPS_RELIABILITY_RELIABLE &&
            local_reliability != NB_RTPS_RELIABILITY_RELIABLE) {
            continue;
        }
        if (remote->info.durability > local->info.durability) {
            continue;
        }
        if (endpoint_data_target(participant, remote, host, sizeof(host), &port) != NB_OK) {
            continue;
        }
        targets++;
        last_rc = send_user_data(participant,
                                 &local->info.endpoint_guid,
                                 &remote->info.endpoint_guid,
                                 sn,
                                 payload,
                                 payload_len,
                                 host,
                                 port);
        if (last_rc == NB_OK) {
            sent++;
        }
    }
    ntk_mutex_unlock(participant->lock);

    if (targets == 0) {
        return NB_ERR_NOTFOUND;
    }
    return sent > 0 ? NB_OK : last_rc;
}

int nb_rtps_participant_publish_dds_message(
    nb_rtps_participant_t *participant,
    const nb_rtps_guid_t *writer_guid,
    const nb_rtps_dds_message_t *message)
{
    uint8_t payload[NB_RTPS_DATA_PAYLOAD_MAX];
    size_t payload_len = 0;
    int rc;

    if (!message) {
        return NB_ERR_INVALID;
    }
    rc = nb_rtps_dds_message_serialize(
        message, payload, sizeof(payload), &payload_len);
    if (rc != NB_OK) {
        return rc;
    }
    return nb_rtps_participant_publish(
        participant, writer_guid, payload, payload_len);
}

int nb_rtps_participant_add_peer(nb_rtps_participant_t *participant,
                                 const char *host,
                                 uint16_t port)
{
    if (!participant || !host || host[0] == '\0' || port == 0) {
        return NB_ERR_INVALID;
    }
    if (participant->started) {
        return NB_ERR_STATE;
    }
    if (participant->peer_count >= NB_RTPS_MAX_PEERS) {
        return NB_ERR_TOOBIG;
    }
    ntk_mutex_lock(participant->lock);
    copy_str(participant->peers[participant->peer_count].host,
             sizeof(participant->peers[participant->peer_count].host),
             host);
    participant->peers[participant->peer_count].port = port;
    participant->peer_count++;
    ntk_mutex_unlock(participant->lock);
    return NB_OK;
}

uint16_t nb_rtps_participant_spdp_port(const nb_rtps_participant_t *participant)
{
    return participant ? participant->spdp_port : 0;
}

size_t nb_rtps_participant_remote_count(const nb_rtps_participant_t *participant)
{
    size_t count = 0;
    uint32_t i;

    if (!participant) {
        return 0;
    }
    ntk_mutex_lock(participant->lock);
    for (i = 0; i < participant->max_remotes; i++) {
        if (participant->remotes[i].used) {
            count++;
        }
    }
    ntk_mutex_unlock(participant->lock);
    return count;
}

size_t nb_rtps_participant_local_endpoint_count(const nb_rtps_participant_t *participant)
{
    size_t count;

    if (!participant) {
        return 0;
    }
    ntk_mutex_lock(participant->lock);
    count = participant->local_endpoint_count;
    ntk_mutex_unlock(participant->lock);
    return count;
}

size_t nb_rtps_participant_remote_endpoint_count(const nb_rtps_participant_t *participant)
{
    size_t count = 0;
    size_t i;

    if (!participant) {
        return 0;
    }
    ntk_mutex_lock(participant->lock);
    for (i = 0; i < participant->max_remote_endpoints; i++) {
        if (participant->remote_endpoints[i].used) {
            count++;
        }
    }
    ntk_mutex_unlock(participant->lock);
    return count;
}

int nb_rtps_participant_announce(nb_rtps_participant_t *participant)
{
    if (!participant || !participant->started) {
        return NB_ERR_STATE;
    }
    return send_spdp(participant);
}

int nb_rtps_participant_local_info(const nb_rtps_participant_t *participant,
                                   nb_rtps_participant_info_t *out)
{
    if (!participant || !out) {
        return NB_ERR_INVALID;
    }
    fill_local_info(participant, out);
    return NB_OK;
}
