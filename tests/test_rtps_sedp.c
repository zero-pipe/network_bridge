#include "rtps_param.h"
#include "rtps_sedp.h"

#define NB_LOG_TAG "test"
#include <nb/log.h>

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            nb_log_error("FAIL %s:%d: %s", __FILE__, __LINE__, #cond);  \
            g_fail = 1;                                                 \
        }                                                               \
    } while (0)

static void check_reliability_parameter(const uint8_t *buf, size_t len, uint32_t kind)
{
    nb_rtps_param_reader_t reader;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    uint16_t pid = 0;
    int found = 0;
    int rc;

    nb_rtps_param_reader_init(&reader, buf + 4, len - 4);
    for (;;) {
        rc = nb_rtps_param_reader_next(&reader, &pid, &value, &value_len);
        if (rc <= 0) {
            break;
        }
        if (pid == NB_RTPS_PID_RELIABILITY) {
            found = 1;
            CHECK(value_len == 12);
            CHECK(value && value[0] == (uint8_t)(kind & 0xffu));
            CHECK(value && value[1] == (uint8_t)((kind >> 8) & 0xffu));
            CHECK(value && value[4] == 0 && value[5] == 0 && value[6] == 0 && value[7] == 0);
            CHECK(value && value[8] == 0 && value[9] == 0 && value[10] == 0 && value[11] == 0);
        }
    }
    CHECK(found);
}

static void check_locator_parameters(const uint8_t *buf,
                                     size_t len,
                                     uint16_t wanted_pid,
                                     int wanted_count)
{
    nb_rtps_param_reader_t reader;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    uint16_t pid = 0;
    int found = 0;
    int rc;

    nb_rtps_param_reader_init(&reader, buf + 4, len - 4);
    for (;;) {
        rc = nb_rtps_param_reader_next(&reader, &pid, &value, &value_len);
        if (rc <= 0) {
            break;
        }
        if (pid == wanted_pid) {
            found++;
            CHECK(value_len == 24);
            CHECK(value != NULL);
        }
    }
    CHECK(found == wanted_count);
}

static void test_endpoint_roundtrip(void)
{
    uint8_t buf[1024];
    nb_rtps_endpoint_info_t info;
    nb_rtps_endpoint_info_t parsed;
    size_t len = 0;

    memset(&info, 0, sizeof(info));
    info.endpoint_guid.prefix.bytes[0] = 0x12;
    info.endpoint_guid.prefix.bytes[11] = 0x34;
    info.endpoint_guid.entity = nb_rtps_eid_sedp_publications_writer;
    memcpy(info.topic_name, "nb/test", 8);
    memcpy(info.type_name, "nb::Test", 9);
    info.reliability = NB_RTPS_RELIABILITY_RELIABLE;
    info.durability = NB_RTPS_DURABILITY_TRANSIENT_LOCAL;
    info.history_kind = NB_RTPS_HISTORY_KEEP_LAST;
    info.history_depth = 4;
    memcpy(info.partition, "part-a", 7);
    info.unicast_count = 2;
    nb_rtps_locator_ipv4(&info.unicast[0], 192, 168, 1, 10, 7400);
    nb_rtps_locator_ipv4(&info.unicast[1], 192, 168, 1, 11, 7401);
    info.multicast_count = 1;
    nb_rtps_locator_spdp_multicast(&info.multicast[0], 7400);
    info.user_data[0] = 0xde;
    info.user_data[1] = 0xad;
    info.user_data[2] = 0xbe;
    info.user_data[3] = 0xef;
    info.user_data_len = 4;

    CHECK(nb_rtps_sedp_build(&info, buf, sizeof(buf), &len) == NB_OK);
    CHECK(len >= 4 && len % 4 == 0);
    CHECK(buf[0] == 0x00);
    CHECK(buf[1] == (uint8_t)NB_RTPS_ENCAPSULATION_PL_CDR_LE);
    CHECK(buf[2] == 0x00 && buf[3] == 0x00);
    check_reliability_parameter(buf, len, NB_RTPS_RELIABILITY_RELIABLE);
    check_locator_parameters(buf, len, NB_RTPS_PID_UNICAST_LOCATOR, 2);
    check_locator_parameters(buf, len, NB_RTPS_PID_MULTICAST_LOCATOR, 1);

    CHECK(nb_rtps_sedp_parse(buf, len, &parsed) == NB_OK);
    CHECK(nb_rtps_guid_equal(&parsed.endpoint_guid, &info.endpoint_guid));
    CHECK(strcmp(parsed.topic_name, "nb/test") == 0);
    CHECK(strcmp(parsed.type_name, "nb::Test") == 0);
    CHECK(parsed.reliability == NB_RTPS_RELIABILITY_RELIABLE);
    CHECK(parsed.durability == NB_RTPS_DURABILITY_TRANSIENT_LOCAL);
    CHECK(parsed.history_kind == NB_RTPS_HISTORY_KEEP_LAST);
    CHECK(parsed.history_depth == 4);
    CHECK(strcmp(parsed.partition, "part-a") == 0);
    CHECK(parsed.unicast_count == 2);
    CHECK(parsed.unicast[0].port == 7400);
    CHECK(parsed.unicast[0].address[12] == 192);
    CHECK(parsed.unicast[0].address[15] == 10);
    CHECK(parsed.unicast[1].port == 7401);
    CHECK(parsed.unicast[1].address[12] == 192);
    CHECK(parsed.unicast[1].address[15] == 11);
    CHECK(parsed.multicast_count == 1);
    CHECK(parsed.multicast[0].port == 7400);
    CHECK(parsed.multicast[0].address[12] == 239);
    CHECK(parsed.user_data_len == 4);
    CHECK(memcmp(parsed.user_data, info.user_data, 4) == 0);

    /* Truncated payload is rejected. */
    CHECK(nb_rtps_sedp_parse(buf, len - 2, &parsed) != NB_OK);
}

static void test_endpoint_defaults(void)
{
    uint8_t buf[512];
    nb_rtps_endpoint_info_t info;
    nb_rtps_endpoint_info_t parsed;
    size_t len = 0;

    memset(&info, 0, sizeof(info));
    info.endpoint_guid.prefix.bytes[0] = 0x56;
    info.endpoint_guid.entity = nb_rtps_eid_sedp_subscriptions_reader;
    memcpy(info.topic_name, "nb/minimal", 11);
    memcpy(info.type_name, "nb::Minimal", 12);

    CHECK(nb_rtps_sedp_build(&info, buf, sizeof(buf), &len) == NB_OK);
    CHECK(nb_rtps_sedp_parse(buf, len, &parsed) == NB_OK);
    CHECK(parsed.reliability == NB_RTPS_RELIABILITY_BEST_EFFORT);
    CHECK(parsed.durability == NB_RTPS_DURABILITY_VOLATILE);
    CHECK(parsed.history_kind == NB_RTPS_HISTORY_KEEP_LAST);
    CHECK(parsed.history_depth == 1);
    CHECK(parsed.partition[0] == '\0');
    CHECK(parsed.unicast_count == 0);
    CHECK(parsed.multicast_count == 0);
    CHECK(parsed.user_data_len == 0);
}

static void test_endpoint_missing_required(void)
{
    uint8_t buf[256];
    nb_rtps_param_writer_t writer;
    nb_rtps_endpoint_info_t parsed;

    nb_rtps_param_writer_init(&writer, buf, sizeof(buf));
    CHECK(nb_rtps_param_put_encapsulation(&writer) == NB_OK);
    CHECK(nb_rtps_param_put_string(&writer, NB_RTPS_PID_TOPIC_NAME, "only-topic") == NB_OK);
    CHECK(nb_rtps_param_writer_finish(&writer) == NB_OK);
    CHECK(nb_rtps_sedp_parse(buf, nb_rtps_param_writer_len(&writer), &parsed) ==
          NB_ERR_BAD_MESSAGE);
}

int main(void)
{
    test_endpoint_roundtrip();
    test_endpoint_defaults();
    test_endpoint_missing_required();
    if (g_fail) {
        nb_log_error("rtps sedp tests failed");
        return 1;
    }
    printf("rtps sedp tests passed\n");
    return 0;
}
