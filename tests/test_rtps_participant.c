#define _CRT_SECURE_NO_WARNINGS

#include "rtps_participant.h"

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

int main(void)
{
    test_create_destroy();
    test_unicast_peers();
    test_sedp_endpoints();
    test_data_roundtrip();
    test_multicast();
    if (g_fail) {
        nb_log_error("rtps participant tests failed");
        return 1;
    }
    nb_log_info("rtps participant tests passed");
    return 0;
}
