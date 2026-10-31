#define _CRT_SECURE_NO_WARNINGS

#include "rtps_participant.h"

#include "cy_dds_message.h"

#define NB_LOG_TAG "test"
#include <nb/log.h>

#include <dds/dds.h>
#include <ntk.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

/* Unicast-only mode: nb and Cyclone talk over fixed ports with explicit
 * discovery peers, so the test does not depend on multicast routing. */
#define UNICAST_ONLY_NB_SPDP_PORT 47100
#define UNICAST_ONLY_NB_UNICAST_PORT 47101
#define UNICAST_ONLY_CYCLONE_SPDP_PORT 7410

static dds_entity_t g_cyclone_domain = 0;

static dds_entity_t create_cyclone_participant(int unicast_only, const char *addr)
{
    if (unicast_only) {
        char xml[512];

        (void)snprintf(xml,
                       sizeof(xml),
                       "<CycloneDDS><Domain id=\"any\"><Discovery>"
                       "<ParticipantIndex>0</ParticipantIndex>"
                       "<Peers><Peer address=\"%s:%d\"/></Peers>"
                       "</Discovery></Domain></CycloneDDS>",
                       addr,
                       UNICAST_ONLY_NB_SPDP_PORT);
        g_cyclone_domain = dds_create_domain(0, xml);
        if (g_cyclone_domain < 0) {
            nb_log_error("Cyclone rejected the unicast-only domain config: %d",
                         (int)g_cyclone_domain);
            return g_cyclone_domain;
        }
    }
    return dds_create_participant(0, NULL, NULL);
}

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            nb_log_error("FAIL %s:%d: %s", __FILE__, __LINE__, #cond);  \
            g_fail = 1;                                                 \
        }                                                               \
    } while (0)

/* Returns 1 when the builtin participant topic contains this vendor id. */
static int cyclone_sees_participant(dds_entity_t reader)
{
    void *sample = NULL;
    dds_sample_info_t info;
    dds_return_t n;
    int found = 0;

    for (;;) {
        sample = NULL;
        memset(&info, 0, sizeof(info));
        n = dds_take(reader, &sample, &info, 1, 1);
        if (n < 0) {
            nb_log_error("dds_read failed: %s", dds_strretcode(n));
            return found;
        }
        if (n == 0) {
            break;
        }
        if (info.valid_data && sample) {
            dds_builtintopic_participant_t *participant =
                (dds_builtintopic_participant_t *)sample;
            if (participant->key.v[0] == NB_RTPS_VENDOR_ID_0 &&
                participant->key.v[1] == NB_RTPS_VENDOR_ID_1) {
                found = 1;
            }
        }
        if (sample) {
            (void)dds_return_loan(reader, &sample, (size_t)n);
        }
    }
    return found;
}

static void cyclone_scan_publications(dds_entity_t reader,
                                      const nb_rtps_guid_prefix_t *prefix,
                                      int *saw_sedp,
                                      int *saw_message)
{
    void *sample = NULL;
    dds_sample_info_t info;
    dds_return_t n;

    for (;;) {
        dds_builtintopic_endpoint_t *endpoint;

        sample = NULL;
        memset(&info, 0, sizeof(info));
        n = dds_take(reader, &sample, &info, 1, 1);
        if (n < 0) {
            nb_log_error("dds_read publication failed: %s", dds_strretcode(n));
            return;
        }
        if (n == 0) {
            break;
        }
        endpoint = (dds_builtintopic_endpoint_t *)sample;
        if (info.valid_data && endpoint && endpoint->topic_name && endpoint->type_name &&
            memcmp(endpoint->participant_key.v, prefix->bytes, 12) == 0) {
            if (strcmp(endpoint->topic_name, "nb/sedp") == 0 &&
                strcmp(endpoint->type_name, "nb::Sedp") == 0) {
                *saw_sedp = 1;
            }
            if (strcmp(endpoint->topic_name, NB_RTPS_DDS_MESSAGE_TOPIC) == 0 &&
                strcmp(endpoint->type_name, NB_RTPS_DDS_MESSAGE_TYPE) == 0) {
                *saw_message = 1;
            }
        }
        if (sample) {
            (void)dds_return_loan(reader, &sample, (size_t)n);
        }
    }
}

static int cyclone_takes_message(dds_entity_t reader,
                                 const uint8_t *expected,
                                 size_t expected_len)
{
    void *sample = NULL;
    dds_sample_info_t info;
    dds_return_t n;
    int found = 0;

    memset(&info, 0, sizeof(info));
    n = dds_take(reader, &sample, &info, 1, 1);
    if (n < 0) {
        nb_log_error("dds_take message failed: %s", dds_strretcode(n));
        return 0;
    }
    if (n > 0) {
        dds_Message *received = (dds_Message *)sample;
        if (info.valid_data && received &&
            received->payload._length == (uint32_t)expected_len &&
            (expected_len == 0 ||
             (received->payload._buffer != NULL &&
              memcmp(received->payload._buffer, expected, expected_len) == 0))) {
            found = 1;
        }
        (void)dds_return_loan(reader, &sample, (size_t)n);
    }
    return found;
}

typedef struct test_inbound_state {
    ntk_mutex *lock;
    int received;
    nb_rtps_guid_t reader_guid;
    nb_rtps_guid_t writer_guid;
    uint8_t payload[NB_RTPS_DATA_PAYLOAD_MAX];
    size_t payload_len;
} test_inbound_state_t;

static void on_inbound_data(void *user,
                            const nb_rtps_guid_t *reader_guid,
                            const nb_rtps_guid_t *writer_guid,
                            const nb_rtps_dds_message_t *message)
{
    test_inbound_state_t *state = (test_inbound_state_t *)user;
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

static int inbound_received(test_inbound_state_t *state)
{
    int received;

    ntk_mutex_lock(state->lock);
    received = state->received;
    ntk_mutex_unlock(state->lock);
    return received;
}

static void test_cyclone_spdp(void)
{
    const char *enabled = getenv("NB_TEST_RTPS_CYCLONE");
    const char *advertise_host = getenv("NB_TEST_RTPS_ADVERTISE_HOST");
    const char *unicast_only_env = getenv("NB_TEST_RTPS_UNICAST_ONLY");
    int unicast_only = unicast_only_env && strcmp(unicast_only_env, "1") == 0;
    const char *addr =
        (advertise_host && advertise_host[0]) ? advertise_host : "127.0.0.1";
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *nb;
    dds_entity_t cyclone;
    dds_entity_t reader;
    dds_entity_t publication_reader;
    dds_entity_t message_topic;
    dds_entity_t message_reader;
    dds_entity_t message_writer;
    nb_rtps_endpoint_info_t endpoint;
    nb_rtps_endpoint_info_t message_endpoint;
    nb_rtps_endpoint_info_t subscription_endpoint;
    nb_rtps_participant_info_t local_info;
    test_inbound_state_t inbound_state;
    int saw_cyclone = 0;
    int saw_nb = 0;
    int saw_publication = 0;
    int saw_message_publication = 0;
    int saw_message = 0;
    int saw_writer_match = 0;
    int waited = 0;

    if (!enabled || strcmp(enabled, "1") != 0) {
        nb_log_info("Cyclone interop test skipped; set NB_TEST_RTPS_CYCLONE=1");
        return;
    }

    cyclone = create_cyclone_participant(unicast_only, addr);
    CHECK(cyclone > 0);
    if (cyclone <= 0) {
        return;
    }
    reader = dds_create_reader(cyclone, DDS_BUILTIN_TOPIC_DCPSPARTICIPANT, NULL, NULL);
    CHECK(reader > 0);
    if (reader <= 0) {
        (void)dds_delete(cyclone);
        return;
    }
    publication_reader =
        dds_create_reader(cyclone, DDS_BUILTIN_TOPIC_DCPSPUBLICATION, NULL, NULL);
    CHECK(publication_reader > 0);
    if (publication_reader <= 0) {
        (void)dds_delete(reader);
        (void)dds_delete(cyclone);
        return;
    }
    message_topic = dds_create_topic(
        cyclone, &dds_Message_desc, NB_RTPS_DDS_MESSAGE_TOPIC, NULL, NULL);
    CHECK(message_topic > 0);
    if (message_topic <= 0) {
        (void)dds_delete(publication_reader);
        (void)dds_delete(reader);
        (void)dds_delete(cyclone);
        return;
    }
    {
        dds_qos_t *reader_qos = dds_create_qos();
        CHECK(reader_qos != NULL);
        if (!reader_qos) {
            (void)dds_delete(message_topic);
            (void)dds_delete(publication_reader);
            (void)dds_delete(reader);
            (void)dds_delete(cyclone);
            return;
        }
        dds_qset_reliability(reader_qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
        message_reader = dds_create_reader(cyclone, message_topic, reader_qos, NULL);
        dds_delete_qos(reader_qos);
    }
    CHECK(message_reader > 0);
    if (message_reader <= 0) {
        (void)dds_delete(message_topic);
        (void)dds_delete(publication_reader);
        (void)dds_delete(reader);
        (void)dds_delete(cyclone);
        return;
    }
    {
        dds_qos_t *writer_qos = dds_create_qos();
        CHECK(writer_qos != NULL);
        if (!writer_qos) {
            (void)dds_delete(message_reader);
            (void)dds_delete(message_topic);
            (void)dds_delete(publication_reader);
            (void)dds_delete(reader);
            (void)dds_delete(cyclone);
            return;
        }
        dds_qset_reliability(writer_qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
        message_writer = dds_create_writer(cyclone, message_topic, writer_qos, NULL);
        dds_delete_qos(writer_qos);
    }
    CHECK(message_writer > 0);
    if (message_writer <= 0) {
        (void)dds_delete(message_reader);
        (void)dds_delete(message_topic);
        (void)dds_delete(publication_reader);
        (void)dds_delete(reader);
        (void)dds_delete(cyclone);
        return;
    }

    memset(&config, 0, sizeof(config));
    config.domain_id = 0;
    config.bind_host = "0.0.0.0";
    config.advertise_host = addr;
    if (unicast_only) {
        config.multicast_group = "";
        config.spdp_port = UNICAST_ONLY_NB_SPDP_PORT;
        config.unicast_port = UNICAST_ONLY_NB_UNICAST_PORT;
    } else {
        config.multicast_group = "239.255.0.1";
        config.spdp_port = 0; /* 7400 + 250 * 0 */
    }
    config.announce_interval_ms = 200;
    config.lease_ms = 3000;
    config.name = "nb-rtps-interop";
    config.max_remote_participants = 32;
    nb = nb_rtps_participant_create(&config);
    CHECK(nb != NULL);
    if (!nb) {
        (void)dds_delete(message_writer);
        (void)dds_delete(message_reader);
        (void)dds_delete(message_topic);
        (void)dds_delete(reader);
        (void)dds_delete(publication_reader);
        (void)dds_delete(cyclone);
        return;
    }
    if (unicast_only) {
        CHECK(nb_rtps_participant_add_peer(
                  nb, addr, UNICAST_ONLY_CYCLONE_SPDP_PORT) == NB_OK);
    }
    memset(&inbound_state, 0, sizeof(inbound_state));
    inbound_state.lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    CHECK(inbound_state.lock != NULL);
    if (!inbound_state.lock) {
        nb_rtps_participant_destroy(nb);
        (void)dds_delete(message_writer);
        (void)dds_delete(message_reader);
        (void)dds_delete(message_topic);
        (void)dds_delete(reader);
        (void)dds_delete(publication_reader);
        (void)dds_delete(cyclone);
        return;
    }
    nb_rtps_participant_set_data_callback(nb, on_inbound_data, &inbound_state);
    CHECK(nb_rtps_participant_local_info(nb, &local_info) == NB_OK);
    memset(&endpoint, 0, sizeof(endpoint));
    (void)snprintf(endpoint.topic_name, sizeof(endpoint.topic_name), "%s", "nb/sedp");
    (void)snprintf(endpoint.type_name, sizeof(endpoint.type_name), "%s", "nb::Sedp");
    endpoint.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    endpoint.durability = NB_RTPS_DURABILITY_VOLATILE;
    endpoint.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    endpoint.history_depth = 1;
    CHECK(nb_rtps_participant_add_endpoint(
              nb, NB_RTPS_ENDPOINT_PUBLICATION, &endpoint) == NB_OK);

    memset(&message_endpoint, 0, sizeof(message_endpoint));
    (void)snprintf(message_endpoint.topic_name,
                   sizeof(message_endpoint.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(message_endpoint.type_name,
                   sizeof(message_endpoint.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    message_endpoint.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    message_endpoint.durability = NB_RTPS_DURABILITY_VOLATILE;
    message_endpoint.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    message_endpoint.history_depth = 1;
    CHECK(nb_rtps_participant_add_endpoint(
              nb, NB_RTPS_ENDPOINT_PUBLICATION, &message_endpoint) == NB_OK);

    memset(&subscription_endpoint, 0, sizeof(subscription_endpoint));
    (void)snprintf(subscription_endpoint.topic_name,
                   sizeof(subscription_endpoint.topic_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TOPIC);
    (void)snprintf(subscription_endpoint.type_name,
                   sizeof(subscription_endpoint.type_name),
                   "%s",
                   NB_RTPS_DDS_MESSAGE_TYPE);
    subscription_endpoint.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    subscription_endpoint.durability = NB_RTPS_DURABILITY_VOLATILE;
    subscription_endpoint.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    subscription_endpoint.history_depth = 1;
    CHECK(nb_rtps_participant_add_endpoint(
              nb, NB_RTPS_ENDPOINT_SUBSCRIPTION, &subscription_endpoint) == NB_OK);
    CHECK(subscription_endpoint.unicast_count > 0);
    CHECK(nb_rtps_participant_start(nb) == NB_OK);

    while (waited < 10000 &&
           (!saw_nb || !saw_cyclone || !saw_publication || !saw_message_publication)) {
        if (!saw_nb) {
            saw_nb = cyclone_sees_participant(reader);
        }
        if (!saw_cyclone) {
            saw_cyclone = nb_rtps_participant_remote_count(nb) > 0;
        }
        if (!saw_publication || !saw_message_publication) {
            cyclone_scan_publications(publication_reader,
                                      &local_info.guid.prefix,
                                      &saw_publication,
                                      &saw_message_publication);
        }
        if (!saw_nb || !saw_cyclone || !saw_publication || !saw_message_publication) {
            ntk_sleep_ms(100);
            waited += 100;
        }
    }
    CHECK(saw_nb);
    CHECK(saw_cyclone);
    CHECK(saw_publication);
    CHECK(saw_message_publication);
    if (!saw_nb || !saw_cyclone || !saw_publication || !saw_message_publication) {
        nb_log_error("Cyclone interop failed after %d ms (nb=%d cyclone=%d publication=%d message_publication=%d)",
                     waited,
                     saw_nb,
                     saw_cyclone,
                     saw_publication,
                     saw_message_publication);
    }

    {
        dds_instance_handle_t matched[8];
        dds_return_t matched_count =
            dds_get_matched_publications(message_reader, matched, 8);
        CHECK(matched_count > 0);
        if (matched_count <= 0) {
            nb_log_error("Cyclone message reader did not match the RTPS publication");
        }
    }

    for (waited = 0;
         waited < 10000 && nb_rtps_participant_remote_endpoint_count(nb) == 0;
         waited += 50) {
        ntk_sleep_ms(50);
    }
    CHECK(nb_rtps_participant_remote_endpoint_count(nb) > 0);
    if (nb_rtps_participant_remote_endpoint_count(nb) == 0) {
        nb_log_error("nb participant did not discover the Cyclone subscription");
    }

    for (waited = 0; waited < 10000 && !saw_writer_match; waited += 50) {
        dds_instance_handle_t matched[8];
        dds_return_t matched_count =
            dds_get_matched_subscriptions(message_writer, matched, 8);
        if (matched_count > 0) {
            saw_writer_match = 1;
        } else {
            ntk_sleep_ms(50);
        }
    }
    CHECK(saw_writer_match);
    if (!saw_writer_match) {
        nb_log_error("Cyclone message writer did not match the RTPS subscription after %d ms",
                     waited);
    }

    for (waited = 0; waited < 10000 && !saw_message; waited += 50) {
        static const uint8_t expected[] = {
            0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22
        };
        nb_rtps_dds_message_t message;
        int rc;

        message.payload = expected;
        message.payload_len = sizeof(expected);
        rc = nb_rtps_participant_publish_dds_message(
            nb, &message_endpoint.endpoint_guid, &message);
        if (rc != NB_OK && (waited % 1000) == 0) {
            nb_log_info("publish rc=%d remote_endpoints=%zu at %d ms",
                        rc,
                        nb_rtps_participant_remote_endpoint_count(nb),
                        waited);
        }
        if (rc == NB_OK) {
            saw_message = cyclone_takes_message(
                message_reader, expected, sizeof(expected));
        }
        if (!saw_message) {
            ntk_sleep_ms(50);
        }
    }
    CHECK(saw_message);
    if (!saw_message) {
        nb_log_error("Cyclone did not receive/deserialize RTPS DATA after %d ms", waited);
    }

    {
        static uint8_t inbound_expected[] = {
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
        };
        dds_Message outgoing;

        memset(&outgoing, 0, sizeof(outgoing));
        outgoing.payload._length = (uint32_t)sizeof(inbound_expected);
        outgoing.payload._maximum = (uint32_t)sizeof(inbound_expected);
        outgoing.payload._buffer = inbound_expected;
        outgoing.payload._release = false;
        for (waited = 0;
             waited < 10000 && !inbound_received(&inbound_state);
             waited += 50) {
            dds_return_t rc = dds_write(message_writer, &outgoing);
            if (rc < 0 && (waited % 1000) == 0) {
                nb_log_info("Cyclone dds_write rc=%d at %d ms",
                            (int)rc,
                            waited);
            }
            if (!inbound_received(&inbound_state)) {
                ntk_sleep_ms(50);
            }
        }
        CHECK(inbound_received(&inbound_state));
        if (!inbound_received(&inbound_state)) {
            nb_log_error("RTPS subscription did not receive Cyclone DATA after %d ms",
                         waited);
        }
        CHECK(inbound_state.payload_len == sizeof(inbound_expected));
        CHECK(memcmp(inbound_state.payload,
                     inbound_expected,
                     sizeof(inbound_expected)) == 0);
        CHECK(nb_rtps_guid_equal(&inbound_state.reader_guid,
                                 &subscription_endpoint.endpoint_guid));
        CHECK(!nb_rtps_entity_id_equal(&inbound_state.writer_guid.entity,
                                       &nb_rtps_eid_unknown));
    }

    nb_rtps_participant_stop(nb);
    nb_rtps_participant_destroy(nb);
    ntk_mutex_destroy(inbound_state.lock);
    (void)dds_delete(message_writer);
    (void)dds_delete(message_reader);
    (void)dds_delete(message_topic);
    (void)dds_delete(reader);
    (void)dds_delete(publication_reader);
    (void)dds_delete(cyclone);
    if (g_cyclone_domain > 0) {
        (void)dds_delete(g_cyclone_domain);
        g_cyclone_domain = 0;
    }
}

int main(void)
{
    test_cyclone_spdp();
    if (g_fail) {
        nb_log_error("rtps Cyclone interop test failed");
        return 1;
    }
    nb_log_info("rtps Cyclone interop test passed");
    return 0;
}
