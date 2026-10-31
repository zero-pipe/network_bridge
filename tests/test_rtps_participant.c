#define _CRT_SECURE_NO_WARNINGS

#include "rtps_participant.h"
#include "rtps_message.h"

#define NB_LOG_TAG "test"
#include <nb/log.h>

#include <ntk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            nb_log_error("FAIL %s:%d: %s", __FILE__, __LINE__, #cond);  \
            g_fail = 1;                                                 \
        }                                                               \
    } while (0)

typedef struct test_state {
    ntk_mutex *lock;
    int added;
    int updated;
    int removed;
    int endpoint_added;
    int endpoint_updated;
    int endpoint_removed;
    int last_endpoint_kind;
    nb_rtps_endpoint_info_t last_endpoint;
    char last_name[NB_RTPS_SPDP_NAME_MAX];
} test_state_t;

typedef struct test_data_state {
    ntk_mutex *lock;
    int received;
    nb_rtps_guid_t reader_guid;
    nb_rtps_guid_t writer_guid;
    uint8_t payload[NB_RTPS_DATA_PAYLOAD_MAX];
    size_t payload_len;
} test_data_state_t;

typedef struct reliable_data_state {
    ntk_mutex *lock;
    int count;
    uint8_t payloads[8][8];
    size_t payload_lens[8];
    nb_rtps_guid_t reader_guids[8];
    nb_rtps_guid_t writer_guids[8];
} reliable_data_state_t;

static void on_participant(void *user,
                           nb_rtps_participant_event_t event,
                           const nb_rtps_participant_info_t *info)
{
    test_state_t *state = (test_state_t *)user;

    ntk_mutex_lock(state->lock);
    if (event == NB_RTPS_PARTICIPANT_ADDED) {
        state->added++;
        (void)snprintf(state->last_name, sizeof(state->last_name), "%s", info->name);
    } else if (event == NB_RTPS_PARTICIPANT_UPDATED) {
        state->updated++;
    } else if (event == NB_RTPS_PARTICIPANT_REMOVED) {
        state->removed++;
    }
    ntk_mutex_unlock(state->lock);
}

static void on_endpoint(void *user,
                        nb_rtps_endpoint_event_t event,
                        nb_rtps_endpoint_kind_t kind,
                        const nb_rtps_endpoint_info_t *info)
{
    test_state_t *state = (test_state_t *)user;

    ntk_mutex_lock(state->lock);
    if (event == NB_RTPS_ENDPOINT_ADDED) {
        state->endpoint_added++;
    } else if (event == NB_RTPS_ENDPOINT_UPDATED) {
        state->endpoint_updated++;
    } else if (event == NB_RTPS_ENDPOINT_REMOVED) {
        state->endpoint_removed++;
    }
    state->last_endpoint_kind = (int)kind;
    state->last_endpoint = *info;
    ntk_mutex_unlock(state->lock);
}

static void on_data(void *user,
                    const nb_rtps_guid_t *reader_guid,
                    const nb_rtps_guid_t *writer_guid,
                    const nb_rtps_dds_message_t *message)
{
    test_data_state_t *state = (test_data_state_t *)user;
    size_t copy_len;

    if (!state || !reader_guid || !writer_guid || !message) {
        return;
    }
    ntk_mutex_lock(state->lock);
    if (state->received) {
        ntk_mutex_unlock(state->lock);
        return;
    }
    state->reader_guid = *reader_guid;
    state->writer_guid = *writer_guid;
    state->payload_len = message->payload_len;
    copy_len = message->payload_len;
    if (copy_len > sizeof(state->payload)) {
        copy_len = sizeof(state->payload);
    }
    if (copy_len > 0 && message->payload) {
        memcpy(state->payload, message->payload, copy_len);
    }
    state->received = 1;
    ntk_mutex_unlock(state->lock);
}

static int data_received(test_data_state_t *state)
{
    int received;

    ntk_mutex_lock(state->lock);
    received = state->received;
    ntk_mutex_unlock(state->lock);
    return received;
}

static void on_reliable_data(void *user,
                             const nb_rtps_guid_t *reader_guid,
                             const nb_rtps_guid_t *writer_guid,
                             const nb_rtps_dds_message_t *message)
{
    reliable_data_state_t *state = (reliable_data_state_t *)user;
    int index;

    if (!state || !reader_guid || !writer_guid || !message) {
        return;
    }
    ntk_mutex_lock(state->lock);
    index = state->count;
    if (index < (int)(sizeof(state->payloads) / sizeof(state->payloads[0]))) {
        size_t copy_len = message->payload_len;

        if (copy_len > sizeof(state->payloads[0])) {
            copy_len = sizeof(state->payloads[0]);
        }
        if (copy_len > 0 && message->payload) {
            memcpy(state->payloads[index], message->payload, copy_len);
        }
        state->payload_lens[index] = message->payload_len;
        state->reader_guids[index] = *reader_guid;
        state->writer_guids[index] = *writer_guid;
    }
    state->count++;
    ntk_mutex_unlock(state->lock);
}

static int reliable_data_count(reliable_data_state_t *state)
{
    int count;

    ntk_mutex_lock(state->lock);
    count = state->count;
    ntk_mutex_unlock(state->lock);
    return count;
}

static int wait_reliable_data(reliable_data_state_t *state, int wanted, int timeout_ms)
{
    int waited = 0;

    while (waited < timeout_ms) {
        if (reliable_data_count(state) >= wanted) {
            return 1;
        }
        ntk_sleep_ms(20);
        waited += 20;
    }
    return 0;
}

/* Inject a GAP datagram from the writer's identity, as an interoperating RTPS
 * peer would when it drops samples from its history. */
static int send_gap_datagram(const nb_rtps_guid_prefix_t *dst_prefix,
                             const nb_rtps_guid_t *writer,
                             const nb_rtps_guid_t *reader,
                             uint32_t gap_start,
                             uint32_t gap_base,
                             uint32_t gap_bits,
                             uint32_t gap_bitmap,
                             uint16_t port)
{
    ntk_socket *sock;
    uint8_t message[256];
    nb_rtps_header_t header;
    nb_rtps_gap_t gap;
    nb_cdr_writer_t cdr;
    size_t message_len;
    int rc = NB_ERR_IO;

    sock = ntk_socket_create();
    if (!sock) {
        return NB_ERR_NOMEM;
    }
    if (ntk_socket_bind_udp(sock, "127.0.0.1", 0, 0) != NTK_OK) {
        ntk_socket_destroy(sock);
        return NB_ERR_IO;
    }
    memset(&header, 0, sizeof(header));
    header.version_major = NB_RTPS_PROTOCOL_VERSION_MAJOR;
    header.version_minor = NB_RTPS_PROTOCOL_VERSION_MINOR;
    header.vendor_id[0] = NB_RTPS_VENDOR_ID_0;
    header.vendor_id[1] = NB_RTPS_VENDOR_ID_1;
    header.guid_prefix = writer->prefix;
    memset(&gap, 0, sizeof(gap));
    gap.reader_id = reader->entity;
    gap.writer_id = writer->entity;
    gap.gap_start.high = 0;
    gap.gap_start.low = gap_start;
    gap.base.high = 0;
    gap.base.low = gap_base;
    gap.num_bits = gap_bits;
    gap.bitmap[0] = gap_bitmap;
    if (nb_rtps_message_init(&cdr, message, sizeof(message), &header) == NB_OK &&
        nb_rtps_info_dst(&cdr, dst_prefix) == NB_OK &&
        nb_rtps_gap(&cdr, &gap) == NB_OK && nb_cdr_writer_ok(&cdr)) {
        message_len = nb_cdr_writer_len(&cdr);
        if (ntk_socket_sendto(sock, message, message_len, "127.0.0.1", port) > 0) {
            rc = NB_OK;
        }
    }
    ntk_socket_destroy(sock);
    return rc;
}

static int state_count(test_state_t *state, int removed)
{
    int value;

    ntk_mutex_lock(state->lock);
    value = removed ? state->removed : state->added;
    ntk_mutex_unlock(state->lock);
    return value;
}

static int wait_count(test_state_t *state, int removed, int wanted, int timeout_ms)
{
    int waited = 0;

    while (waited < timeout_ms) {
        if (state_count(state, removed) >= wanted) {
            return 1;
        }
        ntk_sleep_ms(20);
        waited += 20;
    }
    return 0;
}

static int endpoint_count(test_state_t *state, int removed)
{
    int value;

    ntk_mutex_lock(state->lock);
    value = removed ? state->endpoint_removed : state->endpoint_added;
    ntk_mutex_unlock(state->lock);
    return value;
}

static int wait_endpoint_count(test_state_t *state, int removed, int wanted, int timeout_ms)
{
    int waited = 0;

    while (waited < timeout_ms) {
        if (endpoint_count(state, removed) >= wanted) {
            return 1;
        }
        ntk_sleep_ms(20);
        waited += 20;
    }
    return 0;
}

static void fill_config(nb_rtps_participant_config_t *config, const char *name)
{
    memset(config, 0, sizeof(*config));
    config->domain_id = 0;
    config->bind_host = "127.0.0.1";
    config->advertise_host = "127.0.0.1";
    config->multicast_group = ""; /* unicast peers only */
    config->spdp_port = 0;        /* ephemeral */
    config->unicast_port = 0;
    config->announce_interval_ms = 100;
    config->lease_ms = 1200;
    config->name = name;
    config->max_remote_participants = 8;
}

/* Fill a dds::Message endpoint description with the framework defaults. */
static void init_data_endpoint(nb_rtps_endpoint_info_t *info, int reliable)
{
    memset(info, 0, sizeof(*info));
    (void)snprintf(info->topic_name,
                   sizeof(info->topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(info->type_name,
                   sizeof(info->type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    info->reliability =
        reliable ? NB_RTPS_RELIABILITY_RELIABLE : NB_RTPS_RELIABILITY_BEST_EFFORT;
    info->durability = NB_RTPS_DURABILITY_VOLATILE;
    info->history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    info->history_depth = 8;
}

static void test_unicast_peers(void)
{
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *a;
    nb_rtps_participant_t *b;
    test_state_t state_a;
    test_state_t state_b;
    uint16_t port_a;
    uint16_t port_b;

    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    fill_config(&config, "node-a");
    a = nb_rtps_participant_create(&config);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    port_a = nb_rtps_participant_spdp_port(a);
    CHECK(port_a != 0);

    fill_config(&config, "node-b");
    config.peers = NULL;
    config.peer_count = 0;
    b = nb_rtps_participant_create(&config);
    CHECK(b != NULL);
    if (!b) {
        nb_rtps_participant_destroy(a);
        return;
    }
    port_b = nb_rtps_participant_spdp_port(b);
    CHECK(port_b != 0);
    CHECK(port_a != port_b);
    CHECK(nb_rtps_participant_add_peer(a, "127.0.0.1", port_b) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(b, "127.0.0.1", port_a) == NB_OK);

    state_a.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    state_b.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(state_a.lock != NULL);
    CHECK(state_b.lock != NULL);
    if (!state_a.lock || !state_b.lock) {
        nb_rtps_participant_destroy(a);
        nb_rtps_participant_destroy(b);
        if (state_a.lock) {
            ntk_mutex_destroy(state_a.lock);
        }
        if (state_b.lock) {
            ntk_mutex_destroy(state_b.lock);
        }
        return;
    }
    nb_rtps_participant_set_callback(a, on_participant, &state_a);
    nb_rtps_participant_set_callback(b, on_participant, &state_b);

    CHECK(nb_rtps_participant_start(a) == NB_OK);
    CHECK(nb_rtps_participant_start(b) == NB_OK);

    CHECK(wait_count(&state_a, 0, 1, 5000));
    CHECK(wait_count(&state_b, 0, 1, 5000));
    CHECK(strcmp(state_a.last_name, "node-b") == 0);
    CHECK(strcmp(state_b.last_name, "node-a") == 0);
    CHECK(nb_rtps_participant_remote_count(a) == 1);
    CHECK(nb_rtps_participant_remote_count(b) == 1);

    nb_rtps_participant_stop(b);
    CHECK(wait_count(&state_a, 1, 1, 5000));
    CHECK(nb_rtps_participant_remote_count(a) == 0);

    nb_rtps_participant_stop(a);
    nb_rtps_participant_destroy(a);
    nb_rtps_participant_destroy(b);
    ntk_mutex_destroy(state_a.lock);
    ntk_mutex_destroy(state_b.lock);
}

static void test_create_destroy(void)
{
    nb_rtps_participant_config_t config;
    int i;

    fill_config(&config, "short-lived");
    for (i = 0; i < 3; i++) {
        nb_rtps_participant_t *participant = nb_rtps_participant_create(&config);
        CHECK(participant != NULL);
        if (!participant) {
            return;
        }
        CHECK(nb_rtps_participant_spdp_port(participant) != 0);
        nb_rtps_participant_destroy(participant);
    }
}

static void test_sedp_endpoints(void)
{
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *a;
    nb_rtps_participant_t *b;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    nb_rtps_participant_info_t info_a;
    test_state_t state_a;
    test_state_t state_b;
    uint16_t port_a;
    uint16_t port_b;

    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));
    fill_config(&config, "sedp-a");
    a = nb_rtps_participant_create(&config);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    port_a = nb_rtps_participant_spdp_port(a);
    fill_config(&config, "sedp-b");
    b = nb_rtps_participant_create(&config);
    CHECK(b != NULL);
    if (!b) {
        nb_rtps_participant_destroy(a);
        return;
    }
    port_b = nb_rtps_participant_spdp_port(b);
    CHECK(nb_rtps_participant_add_peer(a, "127.0.0.1", port_b) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(b, "127.0.0.1", port_a) == NB_OK);

    state_a.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    state_b.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(state_a.lock != NULL);
    CHECK(state_b.lock != NULL);
    if (!state_a.lock || !state_b.lock) {
        nb_rtps_participant_destroy(a);
        nb_rtps_participant_destroy(b);
        if (state_a.lock) {
            ntk_mutex_destroy(state_a.lock);
        }
        if (state_b.lock) {
            ntk_mutex_destroy(state_b.lock);
        }
        return;
    }
    nb_rtps_participant_set_callback(a, on_participant, &state_a);
    nb_rtps_participant_set_callback(b, on_participant, &state_b);
    nb_rtps_participant_set_endpoint_callback(a, on_endpoint, &state_a);
    nb_rtps_participant_set_endpoint_callback(b, on_endpoint, &state_b);
    CHECK(nb_rtps_participant_start(a) == NB_OK);
    CHECK(nb_rtps_participant_start(b) == NB_OK);
    CHECK(wait_count(&state_a, 0, 1, 5000));
    CHECK(wait_count(&state_b, 0, 1, 5000));

    memset(&publication, 0, sizeof(publication));
    (void)snprintf(publication.topic_name, sizeof(publication.topic_name), "%s", "nb/sedp");
    (void)snprintf(publication.type_name, sizeof(publication.type_name), "%s", "nb::Sedp");
    publication.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    publication.durability = NB_RTPS_DURABILITY_VOLATILE;
    publication.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    publication.history_depth = 4;
    CHECK(nb_rtps_participant_add_endpoint(
              a, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);
    CHECK(nb_rtps_participant_local_endpoint_count(a) == 1);
    CHECK(wait_endpoint_count(&state_b, 0, 1, 5000));
    CHECK(state_b.last_endpoint_kind == NB_RTPS_ENDPOINT_PUBLICATION);
    CHECK(strcmp(state_b.last_endpoint.topic_name, "nb/sedp") == 0);
    CHECK(strcmp(state_b.last_endpoint.type_name, "nb::Sedp") == 0);
    CHECK(nb_rtps_participant_remote_endpoint_count(b) == 1);

    memset(&subscription, 0, sizeof(subscription));
    (void)snprintf(subscription.topic_name, sizeof(subscription.topic_name), "%s", "nb/sedp");
    (void)snprintf(subscription.type_name, sizeof(subscription.type_name), "%s", "nb::Sedp");
    subscription.reliability = NB_RTPS_RELIABILITY_BEST_EFFORT;
    subscription.durability = NB_RTPS_DURABILITY_VOLATILE;
    CHECK(nb_rtps_participant_add_endpoint(
              b, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(nb_rtps_participant_local_endpoint_count(b) == 1);
    CHECK(wait_endpoint_count(&state_a, 0, 1, 5000));
    CHECK(state_a.last_endpoint_kind == NB_RTPS_ENDPOINT_SUBSCRIPTION);
    CHECK(strcmp(state_a.last_endpoint.topic_name, "nb/sedp") == 0);
    CHECK(nb_rtps_participant_remote_endpoint_count(a) == 1);

    CHECK(nb_rtps_participant_remote_count(a) == 1);
    CHECK(nb_rtps_participant_local_info(a, &info_a) == NB_OK);
    CHECK(nb_rtps_guid_prefix_equal(&state_b.last_endpoint.endpoint_guid.prefix,
                                    &info_a.guid.prefix));

    CHECK(nb_rtps_participant_remove_endpoint(a, &publication.endpoint_guid) == NB_OK);
    CHECK(nb_rtps_participant_local_endpoint_count(a) == 0);
    CHECK(wait_endpoint_count(&state_b, 1, 1, 5000));
    CHECK(nb_rtps_participant_remote_endpoint_count(b) == 0);

    nb_rtps_participant_stop(a);
    nb_rtps_participant_stop(b);
    nb_rtps_participant_destroy(a);
    nb_rtps_participant_destroy(b);
    ntk_mutex_destroy(state_a.lock);
    ntk_mutex_destroy(state_b.lock);
}

static void test_data_roundtrip(void)
{
    static const uint8_t expected[] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22};
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher;
    nb_rtps_participant_t *subscriber;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    nb_rtps_participant_info_t publisher_info;
    nb_rtps_dds_message_t message;
    test_state_t publisher_state;
    test_state_t subscriber_state;
    test_data_state_t data_state;
    uint16_t publisher_port;
    uint16_t subscriber_port;
    int waited;
    int received = 0;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&subscriber_state, 0, sizeof(subscriber_state));
    memset(&data_state, 0, sizeof(data_state));

    fill_config(&config, "data-publisher");
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    fill_config(&config, "data-subscriber");
    subscriber = nb_rtps_participant_create(&config);
    CHECK(subscriber != NULL);
    if (!subscriber) {
        nb_rtps_participant_destroy(publisher);
        return;
    }
    subscriber_port = nb_rtps_participant_spdp_port(subscriber);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", subscriber_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(subscriber, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    subscriber_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    data_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock != NULL);
    CHECK(subscriber_state.lock != NULL);
    CHECK(data_state.lock != NULL);
    if (!publisher_state.lock || !subscriber_state.lock || !data_state.lock) {
        nb_rtps_participant_destroy(publisher);
        nb_rtps_participant_destroy(subscriber);
        if (publisher_state.lock) {
            ntk_mutex_destroy(publisher_state.lock);
        }
        if (subscriber_state.lock) {
            ntk_mutex_destroy(subscriber_state.lock);
        }
        if (data_state.lock) {
            ntk_mutex_destroy(data_state.lock);
        }
        return;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(subscriber, on_participant, &subscriber_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_endpoint_callback(subscriber, on_endpoint, &subscriber_state);
    nb_rtps_participant_set_data_callback(subscriber, on_data, &data_state);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(subscriber) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 1, 5000));
    CHECK(wait_count(&subscriber_state, 0, 1, 5000));

    memset(&publication, 0, sizeof(publication));
    (void)snprintf(publication.topic_name,
                   sizeof(publication.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(publication.type_name,
                   sizeof(publication.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    publication.reliability = NB_RTPS_RELIABILITY_BEST_EFFORT;
    publication.durability = NB_RTPS_DURABILITY_VOLATILE;
    publication.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    publication.history_depth = 1;
    CHECK(nb_rtps_participant_add_endpoint(
              publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);

    memset(&subscription, 0, sizeof(subscription));
    (void)snprintf(subscription.topic_name,
                   sizeof(subscription.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(subscription.type_name,
                   sizeof(subscription.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    subscription.reliability = NB_RTPS_RELIABILITY_BEST_EFFORT;
    subscription.durability = NB_RTPS_DURABILITY_VOLATILE;
    subscription.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    subscription.history_depth = 1;
    CHECK(nb_rtps_participant_add_endpoint(
              subscriber, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(subscription.unicast_count > 0);
    CHECK(wait_endpoint_count(&subscriber_state, 0, 1, 5000));
    CHECK(wait_endpoint_count(&publisher_state, 0, 1, 5000));

    CHECK(nb_rtps_participant_local_info(publisher, &publisher_info) == NB_OK);
    message.payload = expected;
    message.payload_len = sizeof(expected);
    for (waited = 0; waited < 5000 && !received; waited += 50) {
        if (nb_rtps_participant_publish_dds_message(
                publisher, &publication.endpoint_guid, &message) == NB_OK) {
            received = data_received(&data_state);
        }
        if (!received) {
            ntk_sleep_ms(50);
        }
    }
    CHECK(received);
    CHECK(data_state.payload_len == sizeof(expected));
    CHECK(memcmp(data_state.payload, expected, sizeof(expected)) == 0);
    CHECK(nb_rtps_guid_equal(&data_state.reader_guid, &subscription.endpoint_guid));
    CHECK(nb_rtps_guid_prefix_equal(&data_state.writer_guid.prefix,
                                    &publisher_info.guid.prefix));
    CHECK(nb_rtps_entity_id_equal(&data_state.writer_guid.entity,
                                  &publication.endpoint_guid.entity));

    nb_rtps_participant_stop(publisher);
    nb_rtps_participant_stop(subscriber);
    nb_rtps_participant_destroy(publisher);
    nb_rtps_participant_destroy(subscriber);
    ntk_mutex_destroy(publisher_state.lock);
    ntk_mutex_destroy(subscriber_state.lock);
    ntk_mutex_destroy(data_state.lock);
}

/*
 * Fragmentation path: samples above NB_RTPS_DATA_PAYLOAD_MAX are split into
 * DATA_FRAG submessages and must be reassembled byte for byte. RELIABLE QoS is
 * used because a partly received sample stays missing and has to be replayed.
 */
typedef struct frag_state {
    ntk_mutex *lock;
    int received;
    size_t len;
    uint32_t hash;
} frag_state_t;

static uint32_t payload_hash(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void on_frag_data(void *user,
                         const nb_rtps_guid_t *reader_guid,
                         const nb_rtps_guid_t *writer_guid,
                         const nb_rtps_dds_message_t *message)
{
    frag_state_t *state = (frag_state_t *)user;
    uint32_t hash;

    (void)reader_guid;
    (void)writer_guid;
    if (!state || !message || !message->payload) {
        return;
    }
    hash = payload_hash(message->payload, message->payload_len);
    ntk_mutex_lock(state->lock);
    if (!state->received) {
        state->received = 1;
        state->len = message->payload_len;
        state->hash = hash;
    }
    ntk_mutex_unlock(state->lock);
}

static int frag_data_received(frag_state_t *state)
{
    int received;

    ntk_mutex_lock(state->lock);
    received = state->received;
    ntk_mutex_unlock(state->lock);
    return received;
}

/* Publish `sample` until the subscriber reports exactly `expected_len` bytes
 * whose hash matches (or the attempt times out). */
static int send_raw_sample(nb_rtps_participant_t *publisher,
                           const nb_rtps_guid_t *writer_guid,
                           const uint8_t *sample,
                           size_t sample_len,
                           const uint8_t *expected,
                           size_t expected_len,
                           frag_state_t *state)
{
    uint32_t expected_hash = payload_hash(expected, expected_len);
    int waited;
    int ok = 0;

    for (waited = 0; waited < 5000; waited += 50) {
        ntk_mutex_lock(state->lock);
        state->received = 0;
        ntk_mutex_unlock(state->lock);
        if (nb_rtps_participant_publish(publisher, writer_guid, sample, sample_len) == NB_OK) {
            ntk_sleep_ms(50);
            ntk_mutex_lock(state->lock);
            ok = state->received && state->len == expected_len &&
                 state->hash == expected_hash;
            ntk_mutex_unlock(state->lock);
            if (ok) {
                break;
            }
        } else {
            ntk_sleep_ms(50);
        }
    }
    return ok;
}

static int send_frag_sample(nb_rtps_participant_t *publisher,
                            const nb_rtps_endpoint_info_t *publication,
                            const uint8_t *payload,
                            size_t payload_len,
                            frag_state_t *state)
{
    nb_rtps_dds_message_t message;
    uint8_t *serialized;
    size_t serialized_len = 0;
    int ok;
    int rc;

    serialized = (uint8_t *)malloc(payload_len + 8u);
    if (!serialized) {
        return 0;
    }
    message.payload = payload;
    message.payload_len = payload_len;
    rc = nb_rtps_dds_message_serialize(
        &message, serialized, payload_len + 8u, &serialized_len);
    if (rc != NB_OK) {
        free(serialized);
        return 0;
    }

    ok = send_raw_sample(publisher,
                         &publication->endpoint_guid,
                         serialized,
                         serialized_len,
                         payload,
                         payload_len,
                         state);
    free(serialized);
    return ok;
}

static void test_fragmented_data(void)
{
    static const size_t sizes[] = {4096u, 60000u};
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher;
    nb_rtps_participant_t *subscriber;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    test_state_t publisher_state;
    test_state_t subscriber_state;
    frag_state_t frag_state;
    uint16_t publisher_port;
    uint16_t subscriber_port;
    size_t s;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&subscriber_state, 0, sizeof(subscriber_state));
    memset(&frag_state, 0, sizeof(frag_state));

    fill_config(&config, "frag-publisher");
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    fill_config(&config, "frag-subscriber");
    subscriber = nb_rtps_participant_create(&config);
    CHECK(subscriber != NULL);
    if (!subscriber) {
        nb_rtps_participant_destroy(publisher);
        return;
    }
    subscriber_port = nb_rtps_participant_spdp_port(subscriber);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", subscriber_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(subscriber, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    subscriber_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    frag_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock && subscriber_state.lock && frag_state.lock);
    if (!publisher_state.lock || !subscriber_state.lock || !frag_state.lock) {
        goto cleanup;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(subscriber, on_participant, &subscriber_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_endpoint_callback(subscriber, on_endpoint, &subscriber_state);
    nb_rtps_participant_set_data_callback(subscriber, on_frag_data, &frag_state);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(subscriber) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 1, 5000));
    CHECK(wait_count(&subscriber_state, 0, 1, 5000));

    memset(&publication, 0, sizeof(publication));
    (void)snprintf(publication.topic_name,
                   sizeof(publication.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(publication.type_name,
                   sizeof(publication.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    publication.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    publication.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    publication.history_depth = 4;
    CHECK(nb_rtps_participant_add_endpoint(
              publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);

    memset(&subscription, 0, sizeof(subscription));
    (void)snprintf(subscription.topic_name,
                   sizeof(subscription.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(subscription.type_name,
                   sizeof(subscription.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    subscription.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    subscription.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    subscription.history_depth = 4;
    CHECK(nb_rtps_participant_add_endpoint(
              subscriber, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(wait_endpoint_count(&subscriber_state, 0, 1, 5000));
    CHECK(wait_endpoint_count(&publisher_state, 0, 1, 5000));

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        size_t len = sizes[s];
        uint8_t *payload = (uint8_t *)malloc(len);
        size_t i;

        CHECK(payload != NULL);
        if (!payload) {
            continue;
        }
        for (i = 0; i < len; i++) {
            payload[i] = (uint8_t)((i * 31u + 7u) & 0xffu);
        }
        CHECK(send_frag_sample(publisher, &publication, payload, len, &frag_state));
        free(payload);
    }

cleanup:
    if (publisher) {
        nb_rtps_participant_stop(publisher);
    }
    if (subscriber) {
        nb_rtps_participant_stop(subscriber);
    }
    if (publisher) {
        nb_rtps_participant_destroy(publisher);
    }
    if (subscriber) {
        nb_rtps_participant_destroy(subscriber);
    }
    if (publisher_state.lock) {
        ntk_mutex_destroy(publisher_state.lock);
    }
    if (subscriber_state.lock) {
        ntk_mutex_destroy(subscriber_state.lock);
    }
    if (frag_state.lock) {
        ntk_mutex_destroy(frag_state.lock);
    }
}

/*
 * Generic application types: a subscription whose type name is not
 * "dds::Message" receives the raw CDR body of the sample, so an application can
 * plug in any codec (Micro-CDR generated code included) without a registry.
 */
static void test_generic_type_delivery(void)
{
    static const size_t body_sizes[] = {64u, 3000u};
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher;
    nb_rtps_participant_t *subscriber;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    test_state_t publisher_state;
    test_state_t subscriber_state;
    frag_state_t frag_state;
    uint16_t publisher_port;
    uint16_t subscriber_port;
    size_t s;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&subscriber_state, 0, sizeof(subscriber_state));
    memset(&frag_state, 0, sizeof(frag_state));

    fill_config(&config, "generic-publisher");
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    fill_config(&config, "generic-subscriber");
    subscriber = nb_rtps_participant_create(&config);
    CHECK(subscriber != NULL);
    if (!subscriber) {
        nb_rtps_participant_destroy(publisher);
        return;
    }
    subscriber_port = nb_rtps_participant_spdp_port(subscriber);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", subscriber_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(subscriber, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    subscriber_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    frag_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock && subscriber_state.lock && frag_state.lock);
    if (!publisher_state.lock || !subscriber_state.lock || !frag_state.lock) {
        goto cleanup;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(subscriber, on_participant, &subscriber_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_endpoint_callback(subscriber, on_endpoint, &subscriber_state);
    nb_rtps_participant_set_data_callback(subscriber, on_frag_data, &frag_state);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(subscriber) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 1, 5000));
    CHECK(wait_count(&subscriber_state, 0, 1, 5000));

    memset(&publication, 0, sizeof(publication));
    (void)snprintf(publication.topic_name, sizeof(publication.topic_name), "test/Blob");
    (void)snprintf(publication.type_name, sizeof(publication.type_name), "test::Blob");
    publication.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    publication.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    publication.history_depth = 4;
    CHECK(nb_rtps_participant_add_endpoint(
              publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);

    memset(&subscription, 0, sizeof(subscription));
    (void)snprintf(subscription.topic_name, sizeof(subscription.topic_name), "test/Blob");
    (void)snprintf(subscription.type_name, sizeof(subscription.type_name), "test::Blob");
    subscription.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    subscription.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    subscription.history_depth = 4;
    CHECK(nb_rtps_participant_add_endpoint(
              subscriber, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(wait_endpoint_count(&subscriber_state, 0, 1, 5000));
    CHECK(wait_endpoint_count(&publisher_state, 0, 1, 5000));

    for (s = 0; s < sizeof(body_sizes) / sizeof(body_sizes[0]); s++) {
        size_t body_len = body_sizes[s];
        uint8_t *sample = (uint8_t *)malloc(body_len + NB_RTPS_CDR_HEADER_SIZE);
        size_t i;

        CHECK(sample != NULL);
        if (!sample) {
            continue;
        }
        sample[0] = 0x00;
        sample[1] = 0x01; /* CDR_LE */
        sample[2] = 0x00;
        sample[3] = 0x00;
        for (i = 0; i < body_len; i++) {
            sample[NB_RTPS_CDR_HEADER_SIZE + i] = (uint8_t)((i * 17u + 5u) & 0xffu);
        }
        CHECK(send_raw_sample(publisher,
                              &publication.endpoint_guid,
                              sample,
                              body_len + NB_RTPS_CDR_HEADER_SIZE,
                              sample + NB_RTPS_CDR_HEADER_SIZE,
                              body_len,
                              &frag_state));
        free(sample);
    }

cleanup:
    if (publisher) {
        nb_rtps_participant_stop(publisher);
    }
    if (subscriber) {
        nb_rtps_participant_stop(subscriber);
    }
    if (publisher) {
        nb_rtps_participant_destroy(publisher);
    }
    if (subscriber) {
        nb_rtps_participant_destroy(subscriber);
    }
    if (publisher_state.lock) {
        ntk_mutex_destroy(publisher_state.lock);
    }
    if (subscriber_state.lock) {
        ntk_mutex_destroy(subscriber_state.lock);
    }
    if (frag_state.lock) {
        ntk_mutex_destroy(frag_state.lock);
    }
}

/*
 * Partition matching follows DDS rules: names are comma-separated lists and
 * '*' is a wildcard, so "alpha,team-*" matches "team-blue" but not "other".
 */
static void test_partition_matching(void)
{
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher = NULL;
    nb_rtps_participant_t *matched = NULL;
    nb_rtps_participant_t *unmatched = NULL;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    test_state_t publisher_state;
    test_state_t matched_state;
    test_state_t unmatched_state;
    frag_state_t matched_data;
    frag_state_t unmatched_data;
    nb_rtps_dds_message_t message;
    uint8_t payload[3] = {0x21, 0x22, 0x23};
    uint8_t serialized[16];
    size_t serialized_len = 0;
    uint16_t publisher_port;
    uint16_t matched_port;
    int i;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&matched_state, 0, sizeof(matched_state));
    memset(&unmatched_state, 0, sizeof(unmatched_state));
    memset(&matched_data, 0, sizeof(matched_data));
    memset(&unmatched_data, 0, sizeof(unmatched_data));

    fill_config(&config, "part-publisher");
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    fill_config(&config, "part-matched");
    matched = nb_rtps_participant_create(&config);
    fill_config(&config, "part-unmatched");
    unmatched = nb_rtps_participant_create(&config);
    CHECK(matched != NULL && unmatched != NULL);
    if (!matched || !unmatched) {
        goto cleanup;
    }
    matched_port = nb_rtps_participant_spdp_port(matched);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", matched_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(matched, "127.0.0.1", publisher_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(
              publisher,
              "127.0.0.1",
              nb_rtps_participant_spdp_port(unmatched)) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(unmatched, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    matched_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    unmatched_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    matched_data.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    unmatched_data.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock && matched_state.lock && unmatched_state.lock);
    CHECK(matched_data.lock && unmatched_data.lock);
    if (!publisher_state.lock || !matched_state.lock || !unmatched_state.lock ||
        !matched_data.lock || !unmatched_data.lock) {
        goto cleanup;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(matched, on_participant, &matched_state);
    nb_rtps_participant_set_callback(unmatched, on_participant, &unmatched_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_data_callback(matched, on_frag_data, &matched_data);
    nb_rtps_participant_set_data_callback(unmatched, on_frag_data, &unmatched_data);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(matched) == NB_OK);
    CHECK(nb_rtps_participant_start(unmatched) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 2, 5000));
    CHECK(wait_count(&matched_state, 0, 1, 5000));
    CHECK(wait_count(&unmatched_state, 0, 1, 5000));

    init_data_endpoint(&publication, 0);
    (void)snprintf(publication.partition, sizeof(publication.partition), "alpha,team-*");
    CHECK(nb_rtps_participant_add_endpoint(
              publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);

    init_data_endpoint(&subscription, 0);
    (void)snprintf(subscription.partition, sizeof(subscription.partition), "team-blue");
    CHECK(nb_rtps_participant_add_endpoint(
              matched, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    (void)snprintf(subscription.partition, sizeof(subscription.partition), "other");
    CHECK(nb_rtps_participant_add_endpoint(
              unmatched, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(wait_endpoint_count(&publisher_state, 0, 2, 5000));

    message.payload = payload;
    message.payload_len = sizeof(payload);
    CHECK(nb_rtps_dds_message_serialize(
              &message, serialized, sizeof(serialized), &serialized_len) == NB_OK);
    for (i = 0; i < 60 && !frag_data_received(&matched_data); i++) {
        (void)nb_rtps_participant_publish(
            publisher, &publication.endpoint_guid, serialized, serialized_len);
        ntk_sleep_ms(50);
    }
    CHECK(frag_data_received(&matched_data));
    ntk_sleep_ms(500);
    CHECK(!frag_data_received(&unmatched_data));
    if (frag_data_received(&unmatched_data)) {
        nb_log_error("a subscription in partition \"other\" received a sample");
    }

cleanup:
    if (publisher) {
        nb_rtps_participant_stop(publisher);
    }
    if (matched) {
        nb_rtps_participant_stop(matched);
    }
    if (unmatched) {
        nb_rtps_participant_stop(unmatched);
    }
    if (publisher) {
        nb_rtps_participant_destroy(publisher);
    }
    if (matched) {
        nb_rtps_participant_destroy(matched);
    }
    if (unmatched) {
        nb_rtps_participant_destroy(unmatched);
    }
    if (publisher_state.lock) {
        ntk_mutex_destroy(publisher_state.lock);
    }
    if (matched_state.lock) {
        ntk_mutex_destroy(matched_state.lock);
    }
    if (unmatched_state.lock) {
        ntk_mutex_destroy(unmatched_state.lock);
    }
    if (matched_data.lock) {
        ntk_mutex_destroy(matched_data.lock);
    }
    if (unmatched_data.lock) {
        ntk_mutex_destroy(unmatched_data.lock);
    }
}

static void test_multicast(void)
{
    const char *enabled = getenv("NB_TEST_RTPS_MULTICAST");
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *a;
    nb_rtps_participant_t *b;
    test_state_t state_a;
    test_state_t state_b;

    if (!enabled || strcmp(enabled, "1") != 0) {
        nb_log_info("multicast test skipped; set NB_TEST_RTPS_MULTICAST=1");
        return;
    }
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));
    fill_config(&config, "mcast-a");
    config.bind_host = "0.0.0.0";
    config.multicast_group = "239.255.0.1";
    config.spdp_port = 47123;
    config.lease_ms = 3000;
    a = nb_rtps_participant_create(&config);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    config.name = "mcast-b";
    b = nb_rtps_participant_create(&config);
    CHECK(b != NULL);
    if (!b) {
        nb_rtps_participant_destroy(a);
        return;
    }
    state_a.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    state_b.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(state_a.lock != NULL);
    CHECK(state_b.lock != NULL);
    if (!state_a.lock || !state_b.lock) {
        nb_rtps_participant_destroy(a);
        nb_rtps_participant_destroy(b);
        if (state_a.lock) {
            ntk_mutex_destroy(state_a.lock);
        }
        if (state_b.lock) {
            ntk_mutex_destroy(state_b.lock);
        }
        return;
    }
    nb_rtps_participant_set_callback(a, on_participant, &state_a);
    nb_rtps_participant_set_callback(b, on_participant, &state_b);
    CHECK(nb_rtps_participant_start(a) == NB_OK);
    CHECK(nb_rtps_participant_start(b) == NB_OK);
    CHECK(wait_count(&state_a, 0, 1, 5000));
    CHECK(wait_count(&state_b, 0, 1, 5000));
    nb_rtps_participant_stop(a);
    nb_rtps_participant_stop(b);
    nb_rtps_participant_destroy(a);
    nb_rtps_participant_destroy(b);
    ntk_mutex_destroy(state_a.lock);
    ntk_mutex_destroy(state_b.lock);
}

/*
 * A reliable publication that sent samples before the subscriber existed must
 * replay them once the endpoints match: the writer HEARTBEATs its history and
 * the reader ACKNACKs the missing samples.
 */
static void test_reliable_late_joiner(void)
{
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher;
    nb_rtps_participant_t *subscriber;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    nb_rtps_participant_info_t publisher_info;
    nb_rtps_dds_message_t message;
    test_state_t publisher_state;
    test_state_t subscriber_state;
    reliable_data_state_t data_state;
    static const uint8_t expected[] = {1, 2, 3, 4};
    uint16_t publisher_port;
    uint16_t subscriber_port;
    int i;
    int sent;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&subscriber_state, 0, sizeof(subscriber_state));
    memset(&data_state, 0, sizeof(data_state));

    fill_config(&config, "rel-publisher");
    config.data_heartbeat_ms = 100;
    config.data_history_depth = 8;
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    fill_config(&config, "rel-subscriber");
    config.data_heartbeat_ms = 100;
    config.data_history_depth = 8;
    subscriber = nb_rtps_participant_create(&config);
    CHECK(subscriber != NULL);
    if (!subscriber) {
        nb_rtps_participant_destroy(publisher);
        return;
    }
    subscriber_port = nb_rtps_participant_spdp_port(subscriber);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", subscriber_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(subscriber, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    subscriber_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    data_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock != NULL);
    CHECK(subscriber_state.lock != NULL);
    CHECK(data_state.lock != NULL);
    if (!publisher_state.lock || !subscriber_state.lock || !data_state.lock) {
        nb_rtps_participant_destroy(publisher);
        nb_rtps_participant_destroy(subscriber);
        if (publisher_state.lock) {
            ntk_mutex_destroy(publisher_state.lock);
        }
        if (subscriber_state.lock) {
            ntk_mutex_destroy(subscriber_state.lock);
        }
        if (data_state.lock) {
            ntk_mutex_destroy(data_state.lock);
        }
        return;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(subscriber, on_participant, &subscriber_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_endpoint_callback(subscriber, on_endpoint, &subscriber_state);
    nb_rtps_participant_set_data_callback(subscriber, on_reliable_data, &data_state);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(subscriber) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 1, 5000));
    CHECK(wait_count(&subscriber_state, 0, 1, 5000));

    memset(&publication, 0, sizeof(publication));
    (void)snprintf(publication.topic_name,
                   sizeof(publication.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(publication.type_name,
                   sizeof(publication.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    publication.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    publication.durability = NB_RTPS_DURABILITY_VOLATILE;
    publication.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    publication.history_depth = 4;
    CHECK(nb_rtps_participant_add_endpoint(
              publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);

    /* No matching subscription yet: the samples stay in the writer history. */
    for (i = 0; i < 3; i++) {
        message.payload = &expected[i];
        message.payload_len = 1;
        (void)nb_rtps_participant_publish_dds_message(
            publisher, &publication.endpoint_guid, &message);
    }

    memset(&subscription, 0, sizeof(subscription));
    (void)snprintf(subscription.topic_name,
                   sizeof(subscription.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(subscription.type_name,
                   sizeof(subscription.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    subscription.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    subscription.durability = NB_RTPS_DURABILITY_VOLATILE;
    subscription.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    subscription.history_depth = 4;
    CHECK(nb_rtps_participant_add_endpoint(
              subscriber, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(wait_endpoint_count(&subscriber_state, 0, 1, 5000));
    CHECK(wait_endpoint_count(&publisher_state, 0, 1, 5000));

    CHECK(wait_reliable_data(&data_state, 3, 5000));

    /* Steady state: a new sample arrives once, while the replayed history is
     * not delivered again. */
    message.payload = &expected[3];
    message.payload_len = 1;
    sent = 0;
    for (i = 0; i < 100 && !sent; i++) {
        if (nb_rtps_participant_publish_dds_message(
                publisher, &publication.endpoint_guid, &message) == NB_OK) {
            sent = 1;
        } else {
            ntk_sleep_ms(20);
        }
    }
    CHECK(sent);
    CHECK(wait_reliable_data(&data_state, 4, 5000));
    ntk_sleep_ms(300); /* allow any duplicate retransmit to show up */
    CHECK(reliable_data_count(&data_state) == 4);

    CHECK(nb_rtps_participant_local_info(publisher, &publisher_info) == NB_OK);
    for (i = 0; i < 4; i++) {
        CHECK(data_state.payload_lens[i] == 1);
        CHECK(data_state.payloads[i][0] == expected[i]);
        CHECK(nb_rtps_guid_equal(&data_state.reader_guids[i],
                                 &subscription.endpoint_guid));
        CHECK(nb_rtps_guid_prefix_equal(&data_state.writer_guids[i].prefix,
                                        &publisher_info.guid.prefix));
        CHECK(nb_rtps_entity_id_equal(&data_state.writer_guids[i].entity,
                                      &publication.endpoint_guid.entity));
    }

    nb_rtps_participant_stop(publisher);
    nb_rtps_participant_stop(subscriber);
    nb_rtps_participant_destroy(publisher);
    nb_rtps_participant_destroy(subscriber);
    ntk_mutex_destroy(publisher_state.lock);
    ntk_mutex_destroy(subscriber_state.lock);
    ntk_mutex_destroy(data_state.lock);
}

/*
 * Late-joiner durability. A TRANSIENT_LOCAL publication keeps its history and
 * replays it when a matching reader appears later, even when the pair is
 * best effort. A VOLATILE publication keeps nothing for a reader that was not
 * matched when the sample was written.
 */
static void run_late_joiner_case(const char *prefix, uint32_t durability, int expect_count)
{
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher = NULL;
    nb_rtps_participant_t *subscriber = NULL;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    nb_rtps_dds_message_t message;
    test_state_t publisher_state;
    test_state_t subscriber_state;
    reliable_data_state_t data_state;
    static const uint8_t samples[] = {0xa1, 0xb2};
    char name[64];
    uint16_t publisher_port;
    uint16_t subscriber_port;
    int i;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&subscriber_state, 0, sizeof(subscriber_state));
    memset(&data_state, 0, sizeof(data_state));

    (void)snprintf(name, sizeof(name), "%s-publisher", prefix);
    fill_config(&config, name);
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    (void)snprintf(name, sizeof(name), "%s-subscriber", prefix);
    fill_config(&config, name);
    subscriber = nb_rtps_participant_create(&config);
    CHECK(subscriber != NULL);
    if (!subscriber) {
        nb_rtps_participant_destroy(publisher);
        return;
    }
    subscriber_port = nb_rtps_participant_spdp_port(subscriber);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", subscriber_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(subscriber, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    subscriber_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    data_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock && subscriber_state.lock && data_state.lock);
    if (!publisher_state.lock || !subscriber_state.lock || !data_state.lock) {
        goto cleanup;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(subscriber, on_participant, &subscriber_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_endpoint_callback(subscriber, on_endpoint, &subscriber_state);
    nb_rtps_participant_set_data_callback(subscriber, on_reliable_data, &data_state);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(subscriber) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 1, 5000));
    CHECK(wait_count(&subscriber_state, 0, 1, 5000));

    init_data_endpoint(&publication, 0);
    publication.durability = durability;
    CHECK(nb_rtps_participant_add_endpoint(
              publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);

    /* Written before any reader matches. */
    for (i = 0; i < (int)(sizeof(samples)); i++) {
        message.payload = &samples[i];
        message.payload_len = 1;
        (void)nb_rtps_participant_publish_dds_message(
            publisher, &publication.endpoint_guid, &message);
    }
    ntk_sleep_ms(100);

    init_data_endpoint(&subscription, 0);
    subscription.durability = durability;
    CHECK(nb_rtps_participant_add_endpoint(
              subscriber, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(wait_endpoint_count(&subscriber_state, 0, 1, 5000));
    CHECK(wait_endpoint_count(&publisher_state, 0, 1, 5000));

    if (expect_count > 0) {
        CHECK(wait_reliable_data(&data_state, expect_count, 5000));
        ntk_sleep_ms(300);
        CHECK(reliable_data_count(&data_state) == expect_count);
        for (i = 0; i < expect_count; i++) {
            CHECK(data_state.payload_lens[i] == 1);
            CHECK(data_state.payloads[i][0] == samples[i]);
        }
    } else {
        ntk_sleep_ms(1200);
        CHECK(reliable_data_count(&data_state) == 0);
    }

cleanup:
    if (publisher) {
        nb_rtps_participant_stop(publisher);
    }
    if (subscriber) {
        nb_rtps_participant_stop(subscriber);
    }
    if (publisher) {
        nb_rtps_participant_destroy(publisher);
    }
    if (subscriber) {
        nb_rtps_participant_destroy(subscriber);
    }
    if (publisher_state.lock) {
        ntk_mutex_destroy(publisher_state.lock);
    }
    if (subscriber_state.lock) {
        ntk_mutex_destroy(subscriber_state.lock);
    }
    if (data_state.lock) {
        ntk_mutex_destroy(data_state.lock);
    }
}

static void test_late_joiner_durability(void)
{
    run_late_joiner_case("tl", NB_RTPS_DURABILITY_TRANSIENT_LOCAL, 2);
    run_late_joiner_case("vol", NB_RTPS_DURABILITY_VOLATILE, 0);
}

/*
 * Reader-side GAP handling: once the writer declares sequence numbers as
 * permanently unavailable, samples that reuse those numbers must be dropped
 * instead of delivered late.
 */
static void test_reliable_gap(void)
{
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher;
    nb_rtps_participant_t *subscriber;
    nb_rtps_endpoint_info_t publication;
    nb_rtps_endpoint_info_t subscription;
    nb_rtps_participant_info_t publisher_info;
    nb_rtps_dds_message_t message;
    test_state_t publisher_state;
    test_state_t subscriber_state;
    reliable_data_state_t data_state;
    static const uint8_t expected[] = {1, 2, 3, 4};
    uint16_t publisher_port;
    uint16_t subscriber_port;
    uint16_t data_port;
    int i;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&subscriber_state, 0, sizeof(subscriber_state));
    memset(&data_state, 0, sizeof(data_state));

    fill_config(&config, "gap-publisher");
    config.data_heartbeat_ms = 100;
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    fill_config(&config, "gap-subscriber");
    config.data_heartbeat_ms = 100;
    subscriber = nb_rtps_participant_create(&config);
    CHECK(subscriber != NULL);
    if (!subscriber) {
        nb_rtps_participant_destroy(publisher);
        return;
    }
    subscriber_port = nb_rtps_participant_spdp_port(subscriber);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", subscriber_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(subscriber, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    subscriber_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    data_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock != NULL);
    CHECK(subscriber_state.lock != NULL);
    CHECK(data_state.lock != NULL);
    if (!publisher_state.lock || !subscriber_state.lock || !data_state.lock) {
        nb_rtps_participant_destroy(publisher);
        nb_rtps_participant_destroy(subscriber);
        if (publisher_state.lock) {
            ntk_mutex_destroy(publisher_state.lock);
        }
        if (subscriber_state.lock) {
            ntk_mutex_destroy(subscriber_state.lock);
        }
        if (data_state.lock) {
            ntk_mutex_destroy(data_state.lock);
        }
        return;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(subscriber, on_participant, &subscriber_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_endpoint_callback(subscriber, on_endpoint, &subscriber_state);
    nb_rtps_participant_set_data_callback(subscriber, on_reliable_data, &data_state);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(subscriber) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 1, 5000));
    CHECK(wait_count(&subscriber_state, 0, 1, 5000));

    memset(&publication, 0, sizeof(publication));
    (void)snprintf(publication.topic_name,
                   sizeof(publication.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(publication.type_name,
                   sizeof(publication.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    publication.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    publication.durability = NB_RTPS_DURABILITY_VOLATILE;
    publication.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    publication.history_depth = 8;
    CHECK(nb_rtps_participant_add_endpoint(
              publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publication) == NB_OK);

    memset(&subscription, 0, sizeof(subscription));
    (void)snprintf(subscription.topic_name,
                   sizeof(subscription.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(subscription.type_name,
                   sizeof(subscription.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    subscription.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    subscription.durability = NB_RTPS_DURABILITY_VOLATILE;
    subscription.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    subscription.history_depth = 8;
    CHECK(nb_rtps_participant_add_endpoint(
              subscriber, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription) == NB_OK);
    CHECK(wait_endpoint_count(&subscriber_state, 0, 1, 5000));
    CHECK(wait_endpoint_count(&publisher_state, 0, 1, 5000));

    message.payload = &expected[0];
    message.payload_len = 1;
    CHECK(nb_rtps_participant_publish_dds_message(
              publisher, &publication.endpoint_guid, &message) == NB_OK);
    CHECK(wait_reliable_data(&data_state, 1, 5000));

    CHECK(subscription.unicast_count > 0);
    data_port = (uint16_t)subscription.unicast[0].port;
    CHECK(data_port != 0);
    CHECK(nb_rtps_participant_local_info(publisher, &publisher_info) == NB_OK);

    /* Announce that sequence numbers 2 and 3 will never be delivered. */
    CHECK(send_gap_datagram(&publisher_info.guid.prefix,
                            &publication.endpoint_guid,
                            &subscription.endpoint_guid,
                            2,
                            3,
                            1,
                            0x80000000u,
                            data_port) == NB_OK);
    ntk_sleep_ms(200);

    /* The next two samples reuse the gapped sequence numbers and must be
     * dropped; the sample after that is delivered normally. */
    for (i = 1; i < 4; i++) {
        message.payload = &expected[i];
        message.payload_len = 1;
        CHECK(nb_rtps_participant_publish_dds_message(
                  publisher, &publication.endpoint_guid, &message) == NB_OK);
        ntk_sleep_ms(50);
    }
    CHECK(wait_reliable_data(&data_state, 2, 5000));
    ntk_sleep_ms(400);
    CHECK(reliable_data_count(&data_state) == 2);
    CHECK(data_state.payloads[0][0] == expected[0]);
    CHECK(data_state.payloads[1][0] == expected[3]);
    CHECK(data_state.payload_lens[0] == 1);
    CHECK(data_state.payload_lens[1] == 1);

    nb_rtps_participant_stop(publisher);
    nb_rtps_participant_stop(subscriber);
    nb_rtps_participant_destroy(publisher);
    nb_rtps_participant_destroy(subscriber);
    ntk_mutex_destroy(publisher_state.lock);
    ntk_mutex_destroy(subscriber_state.lock);
    ntk_mutex_destroy(data_state.lock);
}

/*
 * The reader window table is capped by max_remote_endpoints. With more active
 * (writer, reader) pairs than that, the extra pairs run without a window:
 * delivery must still work, memory stays bounded, and the degraded pairs must
 * not trigger a retransmission storm for the pairs that do have a window.
 */
static void test_reliable_reader_capacity(void)
{
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *publisher;
    nb_rtps_participant_t *subscriber;
    nb_rtps_endpoint_info_t publications[2];
    nb_rtps_endpoint_info_t subscriptions[3];
    nb_rtps_dds_message_t message;
    test_state_t publisher_state;
    test_state_t subscriber_state;
    reliable_data_state_t data_state;
    static const uint8_t payloads[2] = {0xa1, 0xb2};
    uint16_t publisher_port;
    uint16_t subscriber_port;
    int round;
    size_t i;

    memset(&publisher_state, 0, sizeof(publisher_state));
    memset(&subscriber_state, 0, sizeof(subscriber_state));
    memset(&data_state, 0, sizeof(data_state));

    fill_config(&config, "evict-publisher");
    config.data_heartbeat_ms = 100;
    config.max_remote_endpoints = 4;
    publisher = nb_rtps_participant_create(&config);
    CHECK(publisher != NULL);
    if (!publisher) {
        return;
    }
    publisher_port = nb_rtps_participant_spdp_port(publisher);
    fill_config(&config, "evict-subscriber");
    config.data_heartbeat_ms = 100;
    config.max_remote_endpoints = 4;
    subscriber = nb_rtps_participant_create(&config);
    CHECK(subscriber != NULL);
    if (!subscriber) {
        nb_rtps_participant_destroy(publisher);
        return;
    }
    subscriber_port = nb_rtps_participant_spdp_port(subscriber);
    CHECK(nb_rtps_participant_add_peer(publisher, "127.0.0.1", subscriber_port) == NB_OK);
    CHECK(nb_rtps_participant_add_peer(subscriber, "127.0.0.1", publisher_port) == NB_OK);

    publisher_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    subscriber_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    data_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(publisher_state.lock != NULL);
    CHECK(subscriber_state.lock != NULL);
    CHECK(data_state.lock != NULL);
    if (!publisher_state.lock || !subscriber_state.lock || !data_state.lock) {
        nb_rtps_participant_destroy(publisher);
        nb_rtps_participant_destroy(subscriber);
        if (publisher_state.lock) {
            ntk_mutex_destroy(publisher_state.lock);
        }
        if (subscriber_state.lock) {
            ntk_mutex_destroy(subscriber_state.lock);
        }
        if (data_state.lock) {
            ntk_mutex_destroy(data_state.lock);
        }
        return;
    }
    nb_rtps_participant_set_callback(publisher, on_participant, &publisher_state);
    nb_rtps_participant_set_callback(subscriber, on_participant, &subscriber_state);
    nb_rtps_participant_set_endpoint_callback(publisher, on_endpoint, &publisher_state);
    nb_rtps_participant_set_endpoint_callback(subscriber, on_endpoint, &subscriber_state);
    nb_rtps_participant_set_data_callback(subscriber, on_reliable_data, &data_state);
    CHECK(nb_rtps_participant_start(publisher) == NB_OK);
    CHECK(nb_rtps_participant_start(subscriber) == NB_OK);
    CHECK(wait_count(&publisher_state, 0, 1, 5000));
    CHECK(wait_count(&subscriber_state, 0, 1, 5000));

    /* Two writers and three readers make six pairs against four slots. */
    for (i = 0; i < sizeof(publications) / sizeof(publications[0]); i++) {
        init_data_endpoint(&publications[i], 1);
        CHECK(nb_rtps_participant_add_endpoint(
                  publisher, NB_RTPS_ENDPOINT_PUBLICATION, &publications[i]) == NB_OK);
    }
    for (i = 0; i < sizeof(subscriptions) / sizeof(subscriptions[0]); i++) {
        init_data_endpoint(&subscriptions[i], 1);
        CHECK(nb_rtps_participant_add_endpoint(
                  subscriber, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscriptions[i]) == NB_OK);
    }
    CHECK(wait_endpoint_count(&subscriber_state, 0, 2, 5000));
    CHECK(wait_endpoint_count(&publisher_state, 0, 3, 5000));

    /* Every reader must still receive every writer at least once. */
    for (round = 0; round < 4; round++) {
        for (i = 0; i < sizeof(publications) / sizeof(publications[0]); i++) {
            message.payload = &payloads[i];
            message.payload_len = 1;
            CHECK(nb_rtps_participant_publish_dds_message(
                      publisher, &publications[i].endpoint_guid, &message) == NB_OK);
        }
        ntk_sleep_ms(50);
    }
    CHECK(wait_reliable_data(&data_state, 6, 5000));
    ntk_sleep_ms(300);
    /* A pair without a window must not turn into a retransmission storm. */
    CHECK(reliable_data_count(&data_state) <= 64);

    nb_rtps_participant_stop(publisher);
    nb_rtps_participant_stop(subscriber);
    nb_rtps_participant_destroy(publisher);
    nb_rtps_participant_destroy(subscriber);
    ntk_mutex_destroy(publisher_state.lock);
    ntk_mutex_destroy(subscriber_state.lock);
    ntk_mutex_destroy(data_state.lock);
}

int main(void)
{
    test_create_destroy();
    test_unicast_peers();
    test_sedp_endpoints();
    test_data_roundtrip();
    test_fragmented_data();
    test_generic_type_delivery();
    test_partition_matching();
    test_reliable_late_joiner();
    test_late_joiner_durability();
    test_reliable_gap();
    test_reliable_reader_capacity();
    test_multicast();
    if (g_fail) {
        nb_log_error("rtps participant tests failed");
        return 1;
    }
    nb_log_info("rtps participant tests passed");
    return 0;
}
