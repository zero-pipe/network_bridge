#ifndef NB_RTPS_MESSAGE_H
#define NB_RTPS_MESSAGE_H

/*
 * RTPS message framing for the discovery subset.
 *
 * Messages are written through an nb_cdr_writer_t that starts at the message
 * header, so submessage lengths and 4-byte alignment are handled in one place.
 * Only little-endian messages are produced and accepted.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nb/error.h>

#include "rtps_cdr.h"
#include "rtps_types.h"

#define NB_RTPS_HEADER_SIZE 20u

/* Submessage ids. */
#define NB_RTPS_SUBMSG_ACKNACK 0x06u
#define NB_RTPS_SUBMSG_HEARTBEAT 0x07u
#define NB_RTPS_SUBMSG_GAP 0x08u
#define NB_RTPS_SUBMSG_INFO_TS 0x09u
#define NB_RTPS_SUBMSG_INFO_SRC 0x0cu
#define NB_RTPS_SUBMSG_INFO_DST 0x0eu
#define NB_RTPS_SUBMSG_INFO_REPLY 0x0fu
#define NB_RTPS_SUBMSG_DATA 0x15u
#define NB_RTPS_SUBMSG_DATA_FRAG 0x16u

/* Submessage flags. */
#define NB_RTPS_FLAG_ENDIAN_LITTLE 0x01u
#define NB_RTPS_FLAG_INLINE_QOS 0x02u
#define NB_RTPS_FLAG_DATA 0x04u
#define NB_RTPS_FLAG_KEY 0x08u

/* ACKNACK submessage flag: no heartbeat response is required. */
#define NB_RTPS_ACKNACK_FLAG_FINAL 0x02u

typedef struct nb_rtps_header {
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t vendor_id[2];
    nb_rtps_guid_prefix_t guid_prefix;
} nb_rtps_header_t;

typedef struct nb_rtps_submessage_view {
    uint8_t id;
    uint8_t flags;
    const uint8_t *body;
    size_t body_len;
} nb_rtps_submessage_view_t;

typedef struct nb_rtps_data {
    nb_rtps_entity_id_t reader_id;
    nb_rtps_entity_id_t writer_id;
    nb_rtps_sequence_number_t writer_sn;
    const uint8_t *inline_qos;
    size_t inline_qos_len;
    const uint8_t *payload;
    size_t payload_len;
    int dispose;
    int key;
} nb_rtps_data_t;

typedef struct nb_rtps_data_view {
    nb_rtps_entity_id_t reader_id;
    nb_rtps_entity_id_t writer_id;
    nb_rtps_sequence_number_t writer_sn;
    const uint8_t *inline_qos;
    size_t inline_qos_len;
    const uint8_t *payload;
    size_t payload_len;
    int dispose;
    int key;
} nb_rtps_data_view_t;

#define NB_RTPS_ACKNACK_MAX_BITS 256u
#define NB_RTPS_ACKNACK_MAX_WORDS (NB_RTPS_ACKNACK_MAX_BITS / 32u)

typedef struct nb_rtps_heartbeat {
    nb_rtps_entity_id_t reader_id;
    nb_rtps_entity_id_t writer_id;
    nb_rtps_sequence_number_t first_sn;
    nb_rtps_sequence_number_t last_sn;
    uint32_t count;
} nb_rtps_heartbeat_t;

typedef struct nb_rtps_acknack {
    nb_rtps_entity_id_t reader_id;
    nb_rtps_entity_id_t writer_id;
    nb_rtps_sequence_number_t base;
    uint32_t num_bits;
    uint32_t bitmap[NB_RTPS_ACKNACK_MAX_WORDS];
    uint32_t count;
} nb_rtps_acknack_t;

/*
 * GAP announces samples that the writer will never send to a reader. gap_start
 * is always part of the gap; the sequence-number set (base + bitmap) lists
 * additional gap samples. As in ACKNACK, bit 0 of a word is the most
 * significant bit.
 */
typedef struct nb_rtps_gap {
    nb_rtps_entity_id_t reader_id;
    nb_rtps_entity_id_t writer_id;
    nb_rtps_sequence_number_t gap_start;
    nb_rtps_sequence_number_t base;
    uint32_t num_bits;
    uint32_t bitmap[NB_RTPS_ACKNACK_MAX_WORDS];
} nb_rtps_gap_t;

int nb_rtps_header_encode(uint8_t *buf,
                          size_t cap,
                          size_t *out_len,
                          const nb_rtps_header_t *header);
int nb_rtps_header_decode(const uint8_t *buf, size_t len, nb_rtps_header_t *header);

/* Write the 20-byte message header into writer. */
int nb_rtps_message_init(nb_cdr_writer_t *writer,
                         void *buf,
                         size_t cap,
                         const nb_rtps_header_t *header);

/* Reserve a submessage header and return its offset, or SIZE_MAX on error. */
size_t nb_rtps_submessage_begin(nb_cdr_writer_t *writer, uint8_t id, uint8_t flags);
/* Pad the submessage to 4 bytes and fill octetsToNextHeader. */
int nb_rtps_submessage_end(nb_cdr_writer_t *writer, size_t start_offset);

int nb_rtps_info_ts(nb_cdr_writer_t *writer, const nb_rtps_time_t *ts, bool invalidate);
int nb_rtps_info_dst(nb_cdr_writer_t *writer, const nb_rtps_guid_prefix_t *prefix);

int nb_rtps_data(nb_cdr_writer_t *writer, const nb_rtps_data_t *data);
int nb_rtps_data_decode(uint8_t flags,
                        const uint8_t *body,
                        size_t body_len,
                        nb_rtps_data_view_t *out);

int nb_rtps_heartbeat(nb_cdr_writer_t *writer, const nb_rtps_heartbeat_t *heartbeat);
int nb_rtps_heartbeat_decode(const uint8_t *body, size_t body_len, nb_rtps_heartbeat_t *out);

int nb_rtps_acknack(nb_cdr_writer_t *writer, const nb_rtps_acknack_t *acknack);
int nb_rtps_acknack_decode(const uint8_t *body, size_t body_len, nb_rtps_acknack_t *out);

int nb_rtps_gap(nb_cdr_writer_t *writer, const nb_rtps_gap_t *gap);
int nb_rtps_gap_decode(const uint8_t *body, size_t body_len, nb_rtps_gap_t *out);

int nb_rtps_message_begin(const uint8_t *buf, size_t len, nb_rtps_header_t *header,
                          size_t *offset);
/* Returns 1 for a submessage, 0 at the end of the message, or NB_ERR_* on
 * malformed input. */
int nb_rtps_message_next(const uint8_t *buf,
                         size_t len,
                         size_t *offset,
                         nb_rtps_submessage_view_t *view);

#endif /* NB_RTPS_MESSAGE_H */
