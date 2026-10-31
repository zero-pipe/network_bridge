#include "rtps_message.h"

#include <string.h>

#include "rtps_param.h"

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)(value >> 8);
}

int nb_rtps_header_encode(uint8_t *buf,
                          size_t cap,
                          size_t *out_len,
                          const nb_rtps_header_t *header)
{
    nb_cdr_writer_t writer;

    if (!buf || !header || cap < NB_RTPS_HEADER_SIZE) {
        return NB_ERR_INVALID;
    }
    nb_cdr_writer_init(&writer, buf, cap);
    nb_cdr_put_bytes(&writer, "RTPS", 4);
    nb_cdr_put_u8(&writer, header->version_major);
    nb_cdr_put_u8(&writer, header->version_minor);
    nb_cdr_put_bytes(&writer, header->vendor_id, 2);
    nb_cdr_put_bytes(&writer, header->guid_prefix.bytes, NB_RTPS_GUID_PREFIX_SIZE);
    if (!nb_cdr_writer_ok(&writer) || nb_cdr_writer_len(&writer) != NB_RTPS_HEADER_SIZE) {
        return NB_ERR_TOOBIG;
    }
    if (out_len) {
        *out_len = NB_RTPS_HEADER_SIZE;
    }
    return NB_OK;
}

int nb_rtps_header_decode(const uint8_t *buf, size_t len, nb_rtps_header_t *header)
{
    if (!buf || !header || len < NB_RTPS_HEADER_SIZE) {
        return NB_ERR_BAD_MESSAGE;
    }
    if (memcmp(buf, "RTPS", 4) != 0) {
        return NB_ERR_BAD_MESSAGE;
    }
    if (buf[4] != NB_RTPS_PROTOCOL_VERSION_MAJOR) {
        return NB_ERR_NOT_SUPPORTED;
    }
    header->version_major = buf[4];
    header->version_minor = buf[5];
    header->vendor_id[0] = buf[6];
    header->vendor_id[1] = buf[7];
    memcpy(header->guid_prefix.bytes, buf + 8, NB_RTPS_GUID_PREFIX_SIZE);
    return NB_OK;
}

int nb_rtps_message_init(nb_cdr_writer_t *writer,
                         void *buf,
                         size_t cap,
                         const nb_rtps_header_t *header)
{
    if (!writer || !buf || !header) {
        return NB_ERR_INVALID;
    }
    nb_cdr_writer_init(writer, buf, cap);
    nb_cdr_put_bytes(writer, "RTPS", 4);
    nb_cdr_put_u8(writer, header->version_major);
    nb_cdr_put_u8(writer, header->version_minor);
    nb_cdr_put_bytes(writer, header->vendor_id, 2);
    nb_cdr_put_bytes(writer, header->guid_prefix.bytes, NB_RTPS_GUID_PREFIX_SIZE);
    if (!nb_cdr_writer_ok(writer) || nb_cdr_writer_len(writer) != NB_RTPS_HEADER_SIZE) {
        return NB_ERR_TOOBIG;
    }
    return NB_OK;
}

size_t nb_rtps_submessage_begin(nb_cdr_writer_t *writer, uint8_t id, uint8_t flags)
{
    size_t start;

    if (!writer || !nb_cdr_writer_ok(writer)) {
        return SIZE_MAX;
    }
    nb_cdr_writer_align(writer, 4);
    start = nb_cdr_writer_len(writer);
    nb_cdr_put_u8(writer, id);
    nb_cdr_put_u8(writer, flags);
    nb_cdr_put_u16(writer, 0);
    if (!nb_cdr_writer_ok(writer)) {
        return SIZE_MAX;
    }
    return start;
}

int nb_rtps_submessage_end(nb_cdr_writer_t *writer, size_t start_offset)
{
    size_t total;
    size_t body_len;

    if (!writer || !nb_cdr_writer_ok(writer) ||
        start_offset > nb_cdr_writer_len(writer) ||
        nb_cdr_writer_len(writer) - start_offset < 4) {
        return NB_ERR_BAD_MESSAGE;
    }
    nb_cdr_writer_align(writer, 4);
    if (!nb_cdr_writer_ok(writer)) {
        return NB_ERR_TOOBIG;
    }
    total = nb_cdr_writer_len(writer) - start_offset;
    body_len = total - 4;
    if (body_len > 0xffffu) {
        return NB_ERR_TOOBIG;
    }
    wr_u16(writer->data + start_offset + 2, (uint16_t)body_len);
    return NB_OK;
}

int nb_rtps_info_ts(nb_cdr_writer_t *writer, const nb_rtps_time_t *ts, bool invalidate)
{
    size_t start;
    uint8_t flags = NB_RTPS_FLAG_ENDIAN_LITTLE;

    if (!writer || (!ts && !invalidate)) {
        return NB_ERR_INVALID;
    }
    if (invalidate) {
        flags |= 0x02u;
    }
    start = nb_rtps_submessage_begin(writer, NB_RTPS_SUBMSG_INFO_TS, flags);
    if (start == SIZE_MAX) {
        return NB_ERR_TOOBIG;
    }
    if (!invalidate) {
        nb_cdr_put_i32(writer, ts->seconds);
        nb_cdr_put_u32(writer, ts->fraction);
    }
    return nb_rtps_submessage_end(writer, start);
}

int nb_rtps_info_dst(nb_cdr_writer_t *writer, const nb_rtps_guid_prefix_t *prefix)
{
    size_t start;

    if (!writer || !prefix) {
        return NB_ERR_INVALID;
    }
    start = nb_rtps_submessage_begin(writer, NB_RTPS_SUBMSG_INFO_DST,
                                     NB_RTPS_FLAG_ENDIAN_LITTLE);
    if (start == SIZE_MAX) {
        return NB_ERR_TOOBIG;
    }
    nb_cdr_put_bytes(writer, prefix->bytes, NB_RTPS_GUID_PREFIX_SIZE);
    return nb_rtps_submessage_end(writer, start);
}

int nb_rtps_data(nb_cdr_writer_t *writer, const nb_rtps_data_t *data)
{
    size_t start;
    uint8_t flags = NB_RTPS_FLAG_ENDIAN_LITTLE;

    if (!writer || !data) {
        return NB_ERR_INVALID;
    }
    if (data->inline_qos && data->inline_qos_len > 0) {
        flags |= NB_RTPS_FLAG_INLINE_QOS;
    }
    if (data->key || data->dispose) {
        flags |= NB_RTPS_FLAG_KEY;
    } else {
        flags |= NB_RTPS_FLAG_DATA;
    }
    start = nb_rtps_submessage_begin(writer, NB_RTPS_SUBMSG_DATA, flags);
    if (start == SIZE_MAX) {
        return NB_ERR_TOOBIG;
    }
    nb_cdr_put_u16(writer, 0);  /* extraFlags */
    nb_cdr_put_u16(writer, 16); /* octetsToInlineQos, relative to field end */
    nb_cdr_put_bytes(writer, data->reader_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_bytes(writer, data->writer_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_i32(writer, data->writer_sn.high);
    nb_cdr_put_u32(writer, data->writer_sn.low);
    if (flags & NB_RTPS_FLAG_INLINE_QOS) {
        nb_cdr_put_bytes(writer, data->inline_qos, data->inline_qos_len);
    }
    if (data->payload && data->payload_len > 0) {
        nb_cdr_put_bytes(writer, data->payload, data->payload_len);
    }
    return nb_rtps_submessage_end(writer, start);
}

static int data_decode_inline_qos(const uint8_t *body,
                                  size_t body_len,
                                  size_t offset,
                                  size_t *after_qos)
{
    nb_rtps_param_reader_t reader;
    uint16_t pid = 0;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    int rc;

    if (offset > body_len) {
        return NB_ERR_BAD_MESSAGE;
    }
    nb_rtps_param_reader_init(&reader, body + offset, body_len - offset);
    for (;;) {
        rc = nb_rtps_param_reader_next(&reader, &pid, &value, &value_len);
        if (rc < 0) {
            return rc;
        }
        if (rc == 0) {
            break;
        }
        (void)pid;
        (void)value;
        (void)value_len;
    }
    *after_qos = offset + reader.pos;
    return NB_OK;
}

int nb_rtps_data_decode(uint8_t flags,
                        const uint8_t *body,
                        size_t body_len,
                        nb_rtps_data_view_t *out)
{
    uint16_t extra_flags;
    uint16_t octets_to_inline_qos;
    size_t inline_qos_offset;
    size_t payload_offset;
    int rc;

    if (!body || !out || body_len < 20) {
        return NB_ERR_BAD_MESSAGE;
    }
    if ((flags & NB_RTPS_FLAG_ENDIAN_LITTLE) == 0) {
        return NB_ERR_NOT_SUPPORTED;
    }
    extra_flags = rd_u16(body);
    octets_to_inline_qos = rd_u16(body + 2);
    (void)extra_flags;
    if (octets_to_inline_qos < 16) {
        return NB_ERR_BAD_MESSAGE;
    }
    inline_qos_offset = 4u + (size_t)octets_to_inline_qos;
    if (inline_qos_offset > body_len) {
        return NB_ERR_BAD_MESSAGE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->reader_id.bytes, body + 4, NB_RTPS_ENTITY_ID_SIZE);
    memcpy(out->writer_id.bytes, body + 8, NB_RTPS_ENTITY_ID_SIZE);
    out->writer_sn.high = (int32_t)rd_u32(body + 12);
    out->writer_sn.low = rd_u32(body + 16);
    out->key = (flags & NB_RTPS_FLAG_KEY) ? 1 : 0;
    out->dispose = ((flags & NB_RTPS_FLAG_KEY) && !(flags & NB_RTPS_FLAG_DATA)) ? 1 : 0;
    payload_offset = inline_qos_offset;
    if (flags & NB_RTPS_FLAG_INLINE_QOS) {
        if (inline_qos_offset > body_len) {
            return NB_ERR_BAD_MESSAGE;
        }
        out->inline_qos = body + inline_qos_offset;
        rc = data_decode_inline_qos(body, body_len, inline_qos_offset, &payload_offset);
        if (rc != NB_OK) {
            return rc;
        }
        out->inline_qos_len = payload_offset - inline_qos_offset;
    }
    out->payload = body + payload_offset;
    out->payload_len = body_len - payload_offset;
    return NB_OK;
}

int nb_rtps_heartbeat(nb_cdr_writer_t *writer, const nb_rtps_heartbeat_t *heartbeat)
{
    size_t start;

    if (!writer || !heartbeat) {
        return NB_ERR_INVALID;
    }
    start = nb_rtps_submessage_begin(writer, NB_RTPS_SUBMSG_HEARTBEAT,
                                     NB_RTPS_FLAG_ENDIAN_LITTLE);
    if (start == SIZE_MAX) {
        return NB_ERR_TOOBIG;
    }
    nb_cdr_put_bytes(writer, heartbeat->reader_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_bytes(writer, heartbeat->writer_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_i32(writer, heartbeat->first_sn.high);
    nb_cdr_put_u32(writer, heartbeat->first_sn.low);
    nb_cdr_put_i32(writer, heartbeat->last_sn.high);
    nb_cdr_put_u32(writer, heartbeat->last_sn.low);
    nb_cdr_put_u32(writer, heartbeat->count);
    return nb_rtps_submessage_end(writer, start);
}

int nb_rtps_heartbeat_decode(const uint8_t *body, size_t body_len, nb_rtps_heartbeat_t *out)
{
    if (!body || !out || body_len < 28) {
        return NB_ERR_BAD_MESSAGE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->reader_id.bytes, body, NB_RTPS_ENTITY_ID_SIZE);
    memcpy(out->writer_id.bytes, body + 4, NB_RTPS_ENTITY_ID_SIZE);
    out->first_sn.high = (int32_t)rd_u32(body + 8);
    out->first_sn.low = rd_u32(body + 12);
    out->last_sn.high = (int32_t)rd_u32(body + 16);
    out->last_sn.low = rd_u32(body + 20);
    out->count = rd_u32(body + 24);
    return NB_OK;
}

int nb_rtps_acknack(nb_cdr_writer_t *writer, const nb_rtps_acknack_t *acknack)
{
    size_t start;
    uint32_t word_count;
    uint32_t i;

    if (!writer || !acknack || acknack->num_bits > NB_RTPS_ACKNACK_MAX_BITS) {
        return NB_ERR_INVALID;
    }
    word_count = (acknack->num_bits + 31u) / 32u;
    start = nb_rtps_submessage_begin(writer, NB_RTPS_SUBMSG_ACKNACK,
                                     NB_RTPS_FLAG_ENDIAN_LITTLE | NB_RTPS_ACKNACK_FLAG_FINAL);
    if (start == SIZE_MAX) {
        return NB_ERR_TOOBIG;
    }
    nb_cdr_put_bytes(writer, acknack->reader_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_bytes(writer, acknack->writer_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_i32(writer, acknack->base.high);
    nb_cdr_put_u32(writer, acknack->base.low);
    nb_cdr_put_u32(writer, acknack->num_bits);
    for (i = 0; i < word_count; i++) {
        nb_cdr_put_u32(writer, acknack->bitmap[i]);
    }
    nb_cdr_put_u32(writer, acknack->count);
    return nb_rtps_submessage_end(writer, start);
}

int nb_rtps_acknack_decode(const uint8_t *body, size_t body_len, nb_rtps_acknack_t *out)
{
    uint32_t num_bits;
    uint32_t word_count;
    uint32_t i;
    size_t offset;

    if (!body || !out || body_len < 20) {
        return NB_ERR_BAD_MESSAGE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->reader_id.bytes, body, NB_RTPS_ENTITY_ID_SIZE);
    memcpy(out->writer_id.bytes, body + 4, NB_RTPS_ENTITY_ID_SIZE);
    out->base.high = (int32_t)rd_u32(body + 8);
    out->base.low = rd_u32(body + 12);
    num_bits = rd_u32(body + 16);
    if (num_bits > NB_RTPS_ACKNACK_MAX_BITS) {
        return NB_ERR_TOOBIG;
    }
    word_count = (num_bits + 31u) / 32u;
    offset = 20;
    if (word_count > (body_len - offset) / 4u) {
        return NB_ERR_BAD_MESSAGE;
    }
    for (i = 0; i < word_count; i++) {
        out->bitmap[i] = rd_u32(body + offset);
        offset += 4;
    }
    if (body_len - offset < 4) {
        return NB_ERR_BAD_MESSAGE;
    }
    out->count = rd_u32(body + offset);
    out->num_bits = num_bits;
    return NB_OK;
}

int nb_rtps_gap(nb_cdr_writer_t *writer, const nb_rtps_gap_t *gap)
{
    size_t start;
    uint32_t word_count;
    uint32_t i;

    if (!writer || !gap || gap->num_bits > NB_RTPS_ACKNACK_MAX_BITS) {
        return NB_ERR_INVALID;
    }
    word_count = (gap->num_bits + 31u) / 32u;
    start = nb_rtps_submessage_begin(writer, NB_RTPS_SUBMSG_GAP,
                                     NB_RTPS_FLAG_ENDIAN_LITTLE);
    if (start == SIZE_MAX) {
        return NB_ERR_TOOBIG;
    }
    nb_cdr_put_bytes(writer, gap->reader_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_bytes(writer, gap->writer_id.bytes, NB_RTPS_ENTITY_ID_SIZE);
    nb_cdr_put_i32(writer, gap->gap_start.high);
    nb_cdr_put_u32(writer, gap->gap_start.low);
    nb_cdr_put_i32(writer, gap->base.high);
    nb_cdr_put_u32(writer, gap->base.low);
    nb_cdr_put_u32(writer, gap->num_bits);
    for (i = 0; i < word_count; i++) {
        nb_cdr_put_u32(writer, gap->bitmap[i]);
    }
    return nb_rtps_submessage_end(writer, start);
}

int nb_rtps_gap_decode(const uint8_t *body, size_t body_len, nb_rtps_gap_t *out)
{
    uint32_t num_bits;
    uint32_t word_count;
    uint32_t i;
    size_t offset;

    if (!body || !out || body_len < 28) {
        return NB_ERR_BAD_MESSAGE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->reader_id.bytes, body, NB_RTPS_ENTITY_ID_SIZE);
    memcpy(out->writer_id.bytes, body + 4, NB_RTPS_ENTITY_ID_SIZE);
    out->gap_start.high = (int32_t)rd_u32(body + 8);
    out->gap_start.low = rd_u32(body + 12);
    out->base.high = (int32_t)rd_u32(body + 16);
    out->base.low = rd_u32(body + 20);
    num_bits = rd_u32(body + 24);
    if (num_bits > NB_RTPS_ACKNACK_MAX_BITS) {
        return NB_ERR_TOOBIG;
    }
    word_count = (num_bits + 31u) / 32u;
    offset = 28;
    if (word_count > (body_len - offset) / 4u) {
        return NB_ERR_BAD_MESSAGE;
    }
    for (i = 0; i < word_count; i++) {
        out->bitmap[i] = rd_u32(body + offset);
        offset += 4;
    }
    out->num_bits = num_bits;
    return NB_OK;
}

int nb_rtps_message_begin(const uint8_t *buf,
                          size_t len,
                          nb_rtps_header_t *header,
                          size_t *offset)
{
    int rc = nb_rtps_header_decode(buf, len, header);

    if (rc != NB_OK) {
        return rc;
    }
    if (offset) {
        *offset = NB_RTPS_HEADER_SIZE;
    }
    return NB_OK;
}

int nb_rtps_message_next(const uint8_t *buf,
                         size_t len,
                         size_t *offset,
                         nb_rtps_submessage_view_t *view)
{
    size_t pos;
    size_t body_len;
    size_t next;
    uint16_t octets;

    if (!buf || !offset || !view) {
        return NB_ERR_INVALID;
    }
    pos = *offset;
    if (pos == len) {
        return 0;
    }
    if (pos > len || len - pos < 4) {
        return NB_ERR_BAD_MESSAGE;
    }
    octets = rd_u16(buf + pos + 2);
    if (octets == 0) {
        body_len = len - pos - 4;
        next = len;
    } else {
        if ((size_t)octets > len - pos - 4) {
            return NB_ERR_BAD_MESSAGE;
        }
        body_len = octets;
        next = pos + 4 + (size_t)octets;
    }
    view->id = buf[pos];
    view->flags = buf[pos + 1];
    view->body = buf + pos + 4;
    view->body_len = body_len;
    *offset = next;
    return 1;
}
