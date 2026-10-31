#include "rtps_participant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ntk.h>

#include "rtps_message.h"
#include "rtps_param.h"
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

typedef struct nb_rtps_remote_participant {
    int used;
    uint64_t last_seen_ms;
    uint32_t lease_ms;
    nb_rtps_participant_info_t info;
    char source_host[NB_RTPS_PEER_HOST_MAX];
    uint16_t source_port;
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
    volatile int stop;
    int started;
    uint32_t rng;
    uint32_t spdp_sn;
    uint64_t next_announce_ms;
    uint64_t next_sweep_ms;
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
    /* SEDP is not implemented yet, so do not claim its endpoints. */
    info->builtin_endpoints = NB_RTPS_BUILTIN_ENDPOINT_PARTICIPANT_ANNOUNCER |
                              NB_RTPS_BUILTIN_ENDPOINT_PARTICIPANT_DETECTOR;
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

    ntk_mutex_lock(participant->lock);
    slot = find_remote(participant, &info->guid.prefix);
    if (slot) {
        notify_event = memcmp(&slot->info, info, sizeof(*info)) != 0;
        event = NB_RTPS_PARTICIPANT_UPDATED;
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
        }
        memset(slot, 0, sizeof(*slot));
        slot->used = 1;
        event = NB_RTPS_PARTICIPANT_ADDED;
        notify_event = 1;
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
        nb_rtps_data_view_t data;
        nb_rtps_participant_info_t info;

        if (view.id != NB_RTPS_SUBMSG_DATA) {
            continue;
        }
        if (nb_rtps_data_decode(view.flags, view.body, view.body_len, &data) != NB_OK) {
            continue;
        }
        if (!nb_rtps_entity_id_equal(&data.writer_id, &nb_rtps_eid_spdp_writer)) {
            continue;
        }
        if (nb_rtps_spdp_parse(data.payload, data.payload_len, &info) != NB_OK) {
            continue;
        }
        if (!nb_rtps_guid_prefix_equal(&info.guid.prefix, &header.guid_prefix)) {
            continue;
        }
        update_remote(participant, &info, source_host, source_port);
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
    if (!participant->lock || !participant->remotes) {
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
