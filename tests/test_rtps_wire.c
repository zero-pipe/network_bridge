#include "rtps_cdr.h"
#include "rtps_data.h"
#include "rtps_message.h"
#include "rtps_param.h"
#include "rtps_spdp.h"

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

static void test_cdr_roundtrip(void)
{
    uint8_t buf[128];
    nb_cdr_writer_t writer;
    nb_cdr_reader_t reader;
    char text[16];

    nb_cdr_writer_init(&writer, buf, sizeof(buf));
    nb_cdr_put_u8(&writer, 0xabu);
    nb_cdr_put_u32(&writer, 0x11223344u);
    nb_cdr_put_u64(&writer, 0x0102030405060708ull);
    nb_cdr_put_string(&writer, "hello");
    CHECK(nb_cdr_writer_ok(&writer));
    CHECK(nb_cdr_writer_len(&writer) == 1 + 3 + 4 + 8 + 4 + 6);
    CHECK(buf[0] == 0xab);
    CHECK(buf[4] == 0x44 && buf[5] == 0x33 && buf[6] == 0x22 && buf[7] == 0x11);
    CHECK(buf[8] == 0x08 && buf[15] == 0x01);

    nb_cdr_reader_init(&reader, buf, nb_cdr_writer_len(&writer));
    CHECK(nb_cdr_get_u8(&reader) == 0xabu);
    CHECK(nb_cdr_get_u32(&reader) == 0x11223344u);
    CHECK(nb_cdr_get_u64(&reader) == 0x0102030405060708ull);
    CHECK(nb_cdr_get_string(&reader, text, sizeof(text)));
    CHECK(strcmp(text, "hello") == 0);
    CHECK(nb_cdr_reader_ok(&reader));
    CHECK(nb_cdr_reader_remaining(&reader) == 0);

    /* Reading past the end must fail without touching memory. */
    nb_cdr_reader_init(&reader, buf, 4);
    CHECK(nb_cdr_get_u64(&reader) == 0);
    CHECK(!nb_cdr_reader_ok(&reader));
}

static void test_dds_message_cdr(void)
{
    static const uint8_t bytes[] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22};
    static const uint8_t cyclone_bytes[] = {
        0x00, 0x01, 0x00, 0x01,
        0x07, 0x00, 0x00, 0x00,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x00
    };
    nb_rtps_dds_message_t message;
    nb_rtps_dds_message_t parsed;
    uint8_t buf[64];
    uint8_t small[14];
    size_t len = 0;

    CHECK(strcmp(NB_RTPS_DDS_MESSAGE_TYPE, "dds::Message") == 0);
    message.payload = bytes;
    message.payload_len = sizeof(bytes);
    CHECK(nb_rtps_dds_message_serialize(&message, buf, sizeof(buf), &len) == NB_OK);
    CHECK(len == 4u + 4u + sizeof(bytes));
    CHECK(buf[0] == 0x00 && buf[1] == 0x01 && buf[2] == 0x00 && buf[3] == 0x00);
    CHECK(buf[4] == 0x07 && buf[5] == 0x00 && buf[6] == 0x00 && buf[7] == 0x00);
    CHECK(memcmp(buf + 8, bytes, sizeof(bytes)) == 0);

    CHECK(nb_rtps_dds_message_deserialize(buf, len, &parsed) == NB_OK);
    CHECK(parsed.payload == buf + 8);
    CHECK(parsed.payload_len == sizeof(bytes));
    CHECK(memcmp(parsed.payload, bytes, sizeof(bytes)) == 0);

    /* Cyclone DDS emits option bits 0x0001 after the CDR_LE identifier. */
    CHECK(nb_rtps_dds_message_deserialize(
              cyclone_bytes, sizeof(cyclone_bytes), &parsed) == NB_OK);
    CHECK(parsed.payload == cyclone_bytes + 8);
    CHECK(parsed.payload_len == sizeof(bytes));
    CHECK(memcmp(parsed.payload, cyclone_bytes + 8, sizeof(bytes)) == 0);

    /* Truncated payload must fail. */
    CHECK(nb_rtps_dds_message_deserialize(buf, len - 1u, &parsed) != NB_OK);
    /* Bad encapsulation header must fail. */
    buf[1] = 0x00;
    CHECK(nb_rtps_dds_message_deserialize(buf, len, &parsed) == NB_ERR_NOT_SUPPORTED);
    buf[1] = 0x01;

    /* Empty sequence is valid and still points into the input buffer. */
    message.payload = NULL;
    message.payload_len = 0;
    CHECK(nb_rtps_dds_message_serialize(&message, buf, sizeof(buf), &len) == NB_OK);
    CHECK(len == 8u);
    CHECK(nb_rtps_dds_message_deserialize(buf, len, &parsed) == NB_OK);
    CHECK(parsed.payload == buf + 8);
    CHECK(parsed.payload_len == 0);

    /* Non-empty payload requires a valid pointer. */
    message.payload = NULL;
    message.payload_len = 1;
    CHECK(nb_rtps_dds_message_serialize(&message, buf, sizeof(buf), &len) != NB_OK);

    /* A buffer that cannot hold header + length + payload must fail. */
    message.payload = bytes;
    message.payload_len = sizeof(bytes);
    CHECK(nb_rtps_dds_message_serialize(
              &message, small, sizeof(small), &len) != NB_OK);
}

static void test_param_roundtrip(void)
{
    uint8_t buf[512];
    nb_rtps_param_writer_t writer;
    nb_rtps_param_reader_t reader;
    nb_rtps_guid_t guid;
    nb_rtps_locator_t locator;
    nb_rtps_duration_t lease;
    uint16_t pid;
    const uint8_t *value;
    size_t value_len;
    uint8_t first;
    uint8_t second;
    char name[32];
    int rc;
    int seen = 0;

    memset(&guid, 0, sizeof(guid));
    guid.prefix.bytes[0] = 0x11;
    guid.prefix.bytes[11] = 0x22;
    guid.entity.bytes[3] = 0xc2;
    nb_rtps_locator_ipv4(&locator, 192, 168, 1, 10, 7400);
    lease.seconds = 20;
    lease.fraction = 0;

    nb_rtps_param_writer_init(&writer, buf, sizeof(buf));
    CHECK(nb_rtps_param_put_octets2(&writer, NB_RTPS_PID_PROTOCOL_VERSION, 2, 4) == NB_OK);
    CHECK(nb_rtps_param_put_octets2(&writer, NB_RTPS_PID_VENDORID, 0x01, 0x9a) == NB_OK);
    CHECK(nb_rtps_param_put_guid(&writer, NB_RTPS_PID_PARTICIPANT_GUID, &guid) == NB_OK);
    CHECK(nb_rtps_param_put_duration(
              &writer, NB_RTPS_PID_PARTICIPANT_LEASE_DURATION, &lease) == NB_OK);
    CHECK(nb_rtps_param_put_u32(&writer,
                                NB_RTPS_PID_BUILTIN_ENDPOINT_SET,
                                NB_RTPS_BUILTIN_ENDPOINT_PUBLICATIONS_ANNOUNCER) == NB_OK);
    CHECK(nb_rtps_param_put_locator(
              &writer, NB_RTPS_PID_METATRAFFIC_UNICAST_LOCATOR, &locator) == NB_OK);
    CHECK(nb_rtps_param_put_string(&writer, NB_RTPS_PID_ENTITY_NAME, "node-a") == NB_OK);
    CHECK(nb_rtps_param_writer_finish(&writer) == NB_OK);
    CHECK(nb_rtps_param_writer_ok(&writer));
    CHECK(nb_rtps_param_writer_len(&writer) % 4 == 0);

    nb_rtps_param_reader_init(&reader, buf, nb_rtps_param_writer_len(&writer));
    for (;;) {
        rc = nb_rtps_param_reader_next(&reader, &pid, &value, &value_len);
        if (rc <= 0) {
            CHECK(rc == 0);
            break;
        }
        CHECK(reader.pos % 4 == 0);
        if (pid == NB_RTPS_PID_PROTOCOL_VERSION) {
            CHECK(nb_rtps_param_get_octets2(value, value_len, &first, &second) == NB_OK);
            CHECK(first == 2 && second == 4);
            seen |= 1;
        } else if (pid == NB_RTPS_PID_PARTICIPANT_GUID) {
            nb_rtps_guid_t parsed;
            CHECK(nb_rtps_param_get_guid(value, value_len, &parsed) == NB_OK);
            CHECK(nb_rtps_guid_equal(&parsed, &guid));
            seen |= 2;
        } else if (pid == NB_RTPS_PID_PARTICIPANT_LEASE_DURATION) {
            nb_rtps_duration_t parsed;
            CHECK(nb_rtps_param_get_duration(value, value_len, &parsed) == NB_OK);
            CHECK(parsed.seconds == 20 && parsed.fraction == 0);
            seen |= 4;
        } else if (pid == NB_RTPS_PID_METATRAFFIC_UNICAST_LOCATOR) {
            nb_rtps_locator_t parsed;
            CHECK(nb_rtps_param_get_locator(value, value_len, &parsed) == NB_OK);
            CHECK(parsed.kind == NB_RTPS_LOCATOR_KIND_UDPV4);
            CHECK(parsed.port == 7400);
            CHECK(parsed.address[12] == 192 && parsed.address[13] == 168);
            CHECK(parsed.address[14] == 1 && parsed.address[15] == 10);
            seen |= 8;
        } else if (pid == NB_RTPS_PID_ENTITY_NAME) {
            CHECK(nb_rtps_param_get_string(value, value_len, name, sizeof(name)) == NB_OK);
            CHECK(strcmp(name, "node-a") == 0);
            seen |= 16;
        }
    }
    CHECK(nb_rtps_param_reader_ok(&reader));
    CHECK(seen == 31);

    /* Truncated value length is rejected. */
    nb_rtps_param_reader_init(&reader, buf, 5);
    rc = nb_rtps_param_reader_next(&reader, &pid, &value, &value_len);
    CHECK(rc < 0);
    CHECK(!nb_rtps_param_reader_ok(&reader));
}

static void test_message_data(void)
{
    uint8_t payload[256];
    uint8_t message[512];
    nb_rtps_param_writer_t params;
    nb_rtps_header_t header;
    nb_rtps_header_t parsed_header;
    nb_rtps_time_t ts;
    nb_rtps_guid_t guid;
    nb_rtps_data_t data;
    nb_cdr_writer_t writer;
    nb_rtps_submessage_view_t view;
    nb_rtps_data_view_t data_view;
    size_t offset;
    size_t payload_len;
    int rc;
    int submessage_count = 0;
    int saw_ts = 0;
    int saw_data = 0;

    memset(&guid, 0, sizeof(guid));
    guid.prefix.bytes[0] = 0xaa;
    guid.entity = nb_rtps_eid_spdp_writer;
    nb_rtps_param_writer_init(&params, payload, sizeof(payload));
    CHECK(nb_rtps_param_put_octets2(&params, NB_RTPS_PID_PROTOCOL_VERSION, 2, 4) == NB_OK);
    CHECK(nb_rtps_param_put_guid(&params, NB_RTPS_PID_PARTICIPANT_GUID, &guid) == NB_OK);
    CHECK(nb_rtps_param_writer_finish(&params) == NB_OK);
    payload_len = nb_rtps_param_writer_len(&params);

    memset(&header, 0, sizeof(header));
    header.version_major = NB_RTPS_PROTOCOL_VERSION_MAJOR;
    header.version_minor = NB_RTPS_PROTOCOL_VERSION_MINOR;
    header.vendor_id[0] = NB_RTPS_VENDOR_ID_0;
    header.vendor_id[1] = NB_RTPS_VENDOR_ID_1;
    header.guid_prefix = guid.prefix;
    ts.seconds = 1234;
    ts.fraction = 0x80000000u;
    memset(&data, 0, sizeof(data));
    data.reader_id = nb_rtps_eid_unknown;
    data.writer_id = nb_rtps_eid_spdp_writer;
    data.writer_sn.high = 0;
    data.writer_sn.low = 7;
    data.payload = payload;
    data.payload_len = payload_len;

    rc = nb_rtps_message_init(&writer, message, sizeof(message), &header);
    CHECK(rc == NB_OK);
    CHECK(nb_rtps_info_ts(&writer, &ts, false) == NB_OK);
    CHECK(nb_rtps_data(&writer, &data) == NB_OK);
    CHECK(nb_cdr_writer_ok(&writer));

    CHECK(nb_rtps_message_begin(message, nb_cdr_writer_len(&writer), &parsed_header, &offset) ==
          NB_OK);
    CHECK(parsed_header.version_major == 2 && parsed_header.version_minor == 4);
    CHECK(nb_rtps_guid_prefix_equal(&parsed_header.guid_prefix, &guid.prefix));
    while ((rc = nb_rtps_message_next(message, nb_cdr_writer_len(&writer), &offset, &view)) == 1) {
        submessage_count++;
        if (view.id == NB_RTPS_SUBMSG_INFO_TS) {
            CHECK(view.body_len == 8);
            saw_ts = 1;
        } else if (view.id == NB_RTPS_SUBMSG_DATA) {
            CHECK(nb_rtps_data_decode(view.flags, view.body, view.body_len, &data_view) == NB_OK);
            CHECK((view.flags & NB_RTPS_FLAG_DATA) != 0);
            CHECK((view.flags & NB_RTPS_FLAG_KEY) == 0);
            CHECK(!data_view.dispose);
            CHECK(!data_view.key);
            CHECK(nb_rtps_entity_id_equal(&data_view.writer_id, &nb_rtps_eid_spdp_writer));
            CHECK(nb_rtps_entity_id_equal(&data_view.reader_id, &nb_rtps_eid_unknown));
            CHECK(data_view.writer_sn.high == 0 && data_view.writer_sn.low == 7);
            CHECK(data_view.payload_len >= payload_len);
            CHECK(memcmp(data_view.payload, payload, payload_len) == 0);
            saw_data = 1;
        }
    }
    CHECK(rc == 0);
    CHECK(submessage_count == 2);
    CHECK(saw_ts && saw_data);

    /* A truncated message is rejected. */
    CHECK(nb_rtps_message_begin(message, 12, &parsed_header, &offset) != NB_OK);
    offset = 20;
    CHECK(nb_rtps_message_next(message, 24, &offset, &view) < 0);
}

static void test_heartbeat_acknack(void)
{
    uint8_t buf[256];
    nb_cdr_writer_t writer;
    nb_rtps_header_t header;
    nb_rtps_heartbeat_t heartbeat;
    nb_rtps_heartbeat_t parsed_heartbeat;
    nb_rtps_acknack_t acknack;
    nb_rtps_acknack_t parsed_acknack;
    nb_rtps_submessage_view_t view;
    size_t offset;
    int rc;
    int count = 0;

    memset(&header, 0, sizeof(header));
    header.version_major = NB_RTPS_PROTOCOL_VERSION_MAJOR;
    header.version_minor = NB_RTPS_PROTOCOL_VERSION_MINOR;
    header.vendor_id[0] = NB_RTPS_VENDOR_ID_0;
    header.vendor_id[1] = NB_RTPS_VENDOR_ID_1;
    memset(&heartbeat, 0, sizeof(heartbeat));
    heartbeat.reader_id = nb_rtps_eid_unknown;
    heartbeat.writer_id = nb_rtps_eid_sedp_publications_writer;
    heartbeat.first_sn.low = 1;
    heartbeat.last_sn.low = 9;
    heartbeat.count = 3;
    memset(&acknack, 0, sizeof(acknack));
    acknack.reader_id = nb_rtps_eid_sedp_publications_reader;
    acknack.writer_id = nb_rtps_eid_sedp_publications_writer;
    acknack.base.low = 1;
    acknack.num_bits = 33;
    acknack.bitmap[0] = 0xffffffffu;
    acknack.bitmap[1] = 0x00000001u;
    acknack.count = 11;

    CHECK(nb_rtps_message_init(&writer, buf, sizeof(buf), &header) == NB_OK);
    CHECK(nb_rtps_heartbeat(&writer, &heartbeat) == NB_OK);
    CHECK(nb_rtps_acknack(&writer, &acknack) == NB_OK);
    CHECK(nb_rtps_message_begin(buf, nb_cdr_writer_len(&writer), &header, &offset) == NB_OK);
    while ((rc = nb_rtps_message_next(buf, nb_cdr_writer_len(&writer), &offset, &view)) == 1) {
        count++;
        if (view.id == NB_RTPS_SUBMSG_HEARTBEAT) {
            CHECK(nb_rtps_heartbeat_decode(view.body, view.body_len, &parsed_heartbeat) == NB_OK);
            CHECK(nb_rtps_entity_id_equal(&parsed_heartbeat.writer_id,
                                          &nb_rtps_eid_sedp_publications_writer));
            CHECK(parsed_heartbeat.first_sn.low == 1);
            CHECK(parsed_heartbeat.last_sn.low == 9);
            CHECK(parsed_heartbeat.count == 3);
        } else if (view.id == NB_RTPS_SUBMSG_ACKNACK) {
            CHECK((view.flags & NB_RTPS_ACKNACK_FLAG_FINAL) != 0);
            CHECK(nb_rtps_acknack_decode(view.body, view.body_len, &parsed_acknack) == NB_OK);
            CHECK(parsed_acknack.num_bits == 33);
            CHECK(parsed_acknack.bitmap[0] == 0xffffffffu);
            CHECK(parsed_acknack.bitmap[1] == 0x00000001u);
            CHECK(parsed_acknack.count == 11);
        }
    }
    CHECK(rc == 0);
    CHECK(count == 2);
}

static void test_spdp_roundtrip(void)
{
    uint8_t buf[512];
    nb_rtps_participant_info_t info;
    nb_rtps_participant_info_t parsed;
    nb_rtps_param_writer_t writer;
    nb_rtps_duration_t lease;
    size_t len = 0;

    memset(&info, 0, sizeof(info));
    info.vendor_id[0] = NB_RTPS_VENDOR_ID_0;
    info.vendor_id[1] = NB_RTPS_VENDOR_ID_1;
    info.guid.prefix.bytes[0] = 0x12;
    info.guid.prefix.bytes[11] = 0x34;
    info.guid.entity = nb_rtps_eid_spdp_writer;
    info.lease_duration.seconds = 20;
    info.builtin_endpoints = NB_RTPS_BUILTIN_ENDPOINT_PARTICIPANT_ANNOUNCER |
                             NB_RTPS_BUILTIN_ENDPOINT_PUBLICATIONS_ANNOUNCER;
    nb_rtps_locator_ipv4(&info.metatraffic_unicast, 192, 168, 1, 10, 7410);
    nb_rtps_locator_ipv4(&info.default_unicast, 192, 168, 1, 10, 42420);
    memcpy(info.name, "node-a", 7);
    info.user_data[0] = 0xde;
    info.user_data[1] = 0xad;
    info.user_data_len = 2;

    CHECK(nb_rtps_spdp_build(&info, buf, sizeof(buf), &len) == NB_OK);
    CHECK(len > 0 && len % 4 == 0);
    CHECK(len >= 4);
    CHECK(buf[0] == 0x00);
    CHECK(buf[1] == (uint8_t)NB_RTPS_ENCAPSULATION_PL_CDR_LE);
    CHECK(buf[2] == 0x00 && buf[3] == 0x00);
    CHECK(nb_rtps_spdp_parse(buf, len, &parsed) == NB_OK);
    CHECK(parsed.vendor_id[0] == NB_RTPS_VENDOR_ID_0);
    CHECK(nb_rtps_guid_equal(&parsed.guid, &info.guid));
    CHECK(parsed.lease_duration.seconds == 20);
    CHECK(parsed.builtin_endpoints == info.builtin_endpoints);
    CHECK(parsed.metatraffic_unicast.port == 7410);
    CHECK(parsed.metatraffic_unicast.address[12] == 192);
    CHECK(parsed.metatraffic_unicast.address[13] == 168);
    CHECK(parsed.metatraffic_unicast.address[14] == 1);
    CHECK(parsed.metatraffic_unicast.address[15] == 10);
    CHECK(parsed.default_unicast.port == 42420);
    CHECK(strcmp(parsed.name, "node-a") == 0);
    CHECK(parsed.user_data_len == 2);
    CHECK(parsed.user_data[0] == 0xde && parsed.user_data[1] == 0xad);

    /* Missing mandatory participant GUID. */
    lease.seconds = 20;
    lease.fraction = 0;
    nb_rtps_param_writer_init(&writer, buf, sizeof(buf));
    CHECK(nb_rtps_param_put_octets2(&writer, NB_RTPS_PID_PROTOCOL_VERSION, 2, 4) == NB_OK);
    CHECK(nb_rtps_param_put_duration(
              &writer, NB_RTPS_PID_PARTICIPANT_LEASE_DURATION, &lease) == NB_OK);
    CHECK(nb_rtps_param_put_u32(&writer, NB_RTPS_PID_BUILTIN_ENDPOINT_SET, 1) == NB_OK);
    CHECK(nb_rtps_param_put_locator(
              &writer, NB_RTPS_PID_METATRAFFIC_UNICAST_LOCATOR,
              &info.metatraffic_unicast) == NB_OK);
    CHECK(nb_rtps_param_writer_finish(&writer) == NB_OK);
    CHECK(nb_rtps_spdp_parse(buf, nb_rtps_param_writer_len(&writer), &parsed) ==
          NB_ERR_BAD_MESSAGE);

    /* Truncated payload. */
    CHECK(nb_rtps_spdp_build(&info, buf, sizeof(buf), &len) == NB_OK);
    CHECK(nb_rtps_spdp_parse(buf, len - 2, &parsed) != NB_OK);
}

int main(void)
{
    test_cdr_roundtrip();
    test_dds_message_cdr();
    test_param_roundtrip();
    test_message_data();
    test_heartbeat_acknack();
    test_spdp_roundtrip();
    if (g_fail) {
        nb_log_error("rtps wire tests failed");
        return 1;
    }
    printf("rtps wire tests passed\n");
    return 0;
}
