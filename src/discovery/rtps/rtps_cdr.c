#include "rtps_cdr.h"

#include <string.h>

void nb_cdr_writer_init(nb_cdr_writer_t *writer, void *buf, size_t cap)
{
    if (!writer) {
        return;
    }
    writer->data = (uint8_t *)buf;
    writer->cap = buf ? cap : 0;
    writer->len = 0;
    writer->failed = buf ? 0 : 1;
}

void nb_cdr_reader_init(nb_cdr_reader_t *reader, const void *buf, size_t len)
{
    if (!reader) {
        return;
    }
    reader->data = (const uint8_t *)buf;
    reader->len = buf ? len : 0;
    reader->pos = 0;
    reader->failed = buf ? 0 : 1;
}

bool nb_cdr_writer_ok(const nb_cdr_writer_t *writer)
{
    return writer && !writer->failed;
}

bool nb_cdr_reader_ok(const nb_cdr_reader_t *reader)
{
    return reader && !reader->failed;
}

size_t nb_cdr_writer_len(const nb_cdr_writer_t *writer)
{
    return writer ? writer->len : 0;
}

size_t nb_cdr_reader_remaining(const nb_cdr_reader_t *reader)
{
    if (!reader || reader->failed || reader->pos > reader->len) {
        return 0;
    }
    return reader->len - reader->pos;
}

static bool cdr_writer_reserve(nb_cdr_writer_t *writer, size_t n, uint8_t **out)
{
    if (!writer || writer->failed || n > writer->cap - writer->len) {
        if (writer) {
            writer->failed = 1;
        }
        return false;
    }
    *out = writer->data + writer->len;
    writer->len += n;
    return true;
}

static const uint8_t *cdr_reader_take(nb_cdr_reader_t *reader, size_t n)
{
    const uint8_t *p;

    if (!reader || reader->failed || n > reader->len - reader->pos) {
        if (reader) {
            reader->failed = 1;
        }
        return NULL;
    }
    p = reader->data + reader->pos;
    reader->pos += n;
    return p;
}

void nb_cdr_writer_align(nb_cdr_writer_t *writer, size_t alignment)
{
    size_t pad;
    uint8_t *p;

    if (!writer || alignment == 0) {
        return;
    }
    pad = (alignment - (writer->len % alignment)) % alignment;
    if (pad == 0) {
        return;
    }
    if (!cdr_writer_reserve(writer, pad, &p)) {
        return;
    }
    memset(p, 0, pad);
}

void nb_cdr_reader_align(nb_cdr_reader_t *reader, size_t alignment)
{
    size_t pad;

    if (!reader || alignment == 0) {
        return;
    }
    pad = (alignment - (reader->pos % alignment)) % alignment;
    if (pad == 0) {
        return;
    }
    (void)cdr_reader_take(reader, pad);
}

void nb_cdr_put_u8(nb_cdr_writer_t *writer, uint8_t value)
{
    uint8_t *p;

    if (cdr_writer_reserve(writer, 1, &p)) {
        p[0] = value;
    }
}

void nb_cdr_put_u16(nb_cdr_writer_t *writer, uint16_t value)
{
    uint8_t *p;

    nb_cdr_writer_align(writer, 2);
    if (cdr_writer_reserve(writer, 2, &p)) {
        p[0] = (uint8_t)(value & 0xffu);
        p[1] = (uint8_t)(value >> 8);
    }
}

void nb_cdr_put_u32(nb_cdr_writer_t *writer, uint32_t value)
{
    uint8_t *p;

    nb_cdr_writer_align(writer, 4);
    if (cdr_writer_reserve(writer, 4, &p)) {
        p[0] = (uint8_t)(value & 0xffu);
        p[1] = (uint8_t)((value >> 8) & 0xffu);
        p[2] = (uint8_t)((value >> 16) & 0xffu);
        p[3] = (uint8_t)((value >> 24) & 0xffu);
    }
}

void nb_cdr_put_u64(nb_cdr_writer_t *writer, uint64_t value)
{
    uint8_t *p;

    nb_cdr_writer_align(writer, 8);
    if (cdr_writer_reserve(writer, 8, &p)) {
        p[0] = (uint8_t)(value & 0xffu);
        p[1] = (uint8_t)((value >> 8) & 0xffu);
        p[2] = (uint8_t)((value >> 16) & 0xffu);
        p[3] = (uint8_t)((value >> 24) & 0xffu);
        p[4] = (uint8_t)((value >> 32) & 0xffu);
        p[5] = (uint8_t)((value >> 40) & 0xffu);
        p[6] = (uint8_t)((value >> 48) & 0xffu);
        p[7] = (uint8_t)((value >> 56) & 0xffu);
    }
}

void nb_cdr_put_i32(nb_cdr_writer_t *writer, int32_t value)
{
    nb_cdr_put_u32(writer, (uint32_t)value);
}

void nb_cdr_put_bytes(nb_cdr_writer_t *writer, const void *data, size_t len)
{
    uint8_t *p;

    if (len == 0) {
        return;
    }
    if (!data) {
        if (writer) {
            writer->failed = 1;
        }
        return;
    }
    if (cdr_writer_reserve(writer, len, &p)) {
        memcpy(p, data, len);
    }
}

void nb_cdr_put_string(nb_cdr_writer_t *writer, const char *value)
{
    size_t len;

    if (!writer || writer->failed) {
        return;
    }
    if (!value) {
        value = "";
    }
    len = strlen(value);
    if (len >= 0xffffffffu) {
        writer->failed = 1;
        return;
    }
    nb_cdr_put_u32(writer, (uint32_t)(len + 1));
    nb_cdr_put_bytes(writer, value, len);
    nb_cdr_put_u8(writer, 0);
}

uint8_t nb_cdr_get_u8(nb_cdr_reader_t *reader)
{
    const uint8_t *p = cdr_reader_take(reader, 1);
    return p ? p[0] : 0;
}

uint16_t nb_cdr_get_u16(nb_cdr_reader_t *reader)
{
    const uint8_t *p;

    nb_cdr_reader_align(reader, 2);
    p = cdr_reader_take(reader, 2);
    if (!p) {
        return 0;
    }
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t nb_cdr_get_u32(nb_cdr_reader_t *reader)
{
    const uint8_t *p;

    nb_cdr_reader_align(reader, 4);
    p = cdr_reader_take(reader, 4);
    if (!p) {
        return 0;
    }
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint64_t nb_cdr_get_u64(nb_cdr_reader_t *reader)
{
    const uint8_t *p;
    uint64_t value = 0;
    int i;

    nb_cdr_reader_align(reader, 8);
    p = cdr_reader_take(reader, 8);
    if (!p) {
        return 0;
    }
    for (i = 7; i >= 0; i--) {
        value = (value << 8) | p[i];
    }
    return value;
}

int32_t nb_cdr_get_i32(nb_cdr_reader_t *reader)
{
    return (int32_t)nb_cdr_get_u32(reader);
}

const uint8_t *nb_cdr_get_bytes(nb_cdr_reader_t *reader, size_t len)
{
    return cdr_reader_take(reader, len);
}

bool nb_cdr_get_string(nb_cdr_reader_t *reader, char *out, size_t cap)
{
    uint32_t wire_len;
    const uint8_t *p;
    size_t copy_len;

    if (!reader || reader->failed) {
        return false;
    }
    wire_len = nb_cdr_get_u32(reader);
    if (!reader->failed && wire_len == 0) {
        reader->failed = 1;
    }
    if (reader->failed || (size_t)wire_len > reader->len - reader->pos) {
        reader->failed = 1;
        return false;
    }
    p = cdr_reader_take(reader, (size_t)wire_len);
    if (!p) {
        return false;
    }
    copy_len = (size_t)wire_len - 1;
    if (out && cap > 0) {
        if (copy_len >= cap) {
            reader->failed = 1;
            return false;
        }
        if (copy_len > 0) {
            memcpy(out, p, copy_len);
        }
        out[copy_len] = '\0';
    } else if (copy_len > 0) {
        reader->failed = 1;
        return false;
    }
    return true;
}
