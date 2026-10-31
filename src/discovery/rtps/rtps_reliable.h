#ifndef NB_RTPS_RELIABLE_H
#define NB_RTPS_RELIABLE_H

/*
 * Bounded reliable writer history for SEDP.
 *
 * The writer keeps the most recent samples and retransmits samples requested
 * by an ACKNACK. It has no per-reader state: ACKNACK retransmissions go back to
 * the sender address supplied by the caller.
 */

#include <stddef.h>
#include <stdint.h>

#include <nb/error.h>

#include "rtps_message.h"

#define NB_RTPS_RELIABLE_DEFAULT_CAPACITY 8u
#define NB_RTPS_RELIABLE_MAX_CAPACITY 4096u

typedef struct nb_rtps_reliable_writer nb_rtps_reliable_writer_t;

/* Called while the writer lock is held. Return NB_OK to continue. */
typedef int (*nb_rtps_reliable_resend_fn)(void *user,
                                          uint32_t sn,
                                          const uint8_t *payload,
                                          size_t len,
                                          int dispose);

nb_rtps_reliable_writer_t *nb_rtps_reliable_writer_create(size_t capacity);
void nb_rtps_reliable_writer_destroy(nb_rtps_reliable_writer_t *writer);

/* Append a sample. The sequence number starts at 1. When the history is full,
 * the oldest sample is dropped. */
int nb_rtps_reliable_add(nb_rtps_reliable_writer_t *writer,
                         const void *payload,
                         size_t len,
                         int dispose,
                         uint32_t *out_sn);

/* Return a sample. The pointer is valid until the next add or destroy. */
int nb_rtps_reliable_get(const nb_rtps_reliable_writer_t *writer,
                         uint32_t sn,
                         const uint8_t **payload,
                         size_t *len,
                         int *dispose);

/* first_sn > last_sn when the history is empty. */
void nb_rtps_reliable_state(const nb_rtps_reliable_writer_t *writer,
                            uint32_t *first_sn,
                            uint32_t *last_sn,
                            size_t *sample_count);

/* Retransmit each sequence number whose bitmap bit is set. Sequence numbers
 * that have already been evicted are ignored. */
int nb_rtps_reliable_process_acknack(nb_rtps_reliable_writer_t *writer,
                                     const nb_rtps_acknack_t *acknack,
                                     nb_rtps_reliable_resend_fn resend,
                                     void *user,
                                     size_t *resent);

/*
 * Reader-side receive window.
 *
 * Tracks the next expected sequence number plus a 256-sample bitmap of
 * samples already seen. The window advances over contiguous received
 * samples, so a HEARTBEAT can be answered with an ACKNACK that requests only
 * the samples that are still missing.
 */
typedef struct nb_rtps_reader_state {
    uint32_t next_expected;
    uint32_t received[NB_RTPS_ACKNACK_MAX_WORDS];
    uint32_t ack_count;
} nb_rtps_reader_state_t;

/* Restart the window at first_sn (normally 1). */
void nb_rtps_reader_state_reset(nb_rtps_reader_state_t *state, uint32_t first_sn);

/*
 * Record a received sample. Returns 1 when the caller should deliver it, and 0
 * for duplicates, samples older than the window, and samples too far ahead to
 * track. A sample too far ahead is reported as new but is not recorded; the
 * reader falls back to asking for it again through the next ACKNACK.
 */
int nb_rtps_reader_on_sample(nb_rtps_reader_state_t *state, uint32_t sn);

/* Mark the samples announced by a GAP as received without delivering them. */
void nb_rtps_reader_on_gap(nb_rtps_reader_state_t *state, const nb_rtps_gap_t *gap);

/*
 * Fill base, bitmap and count for an ACKNACK covering up to last_sn. Bits are
 * set for samples that are still missing. Returns the number of missing
 * samples; a zero return still carries a valid, empty ACKNACK.
 */
uint32_t nb_rtps_reader_build_acknack(nb_rtps_reader_state_t *state,
                                      uint32_t last_sn,
                                      nb_rtps_acknack_t *acknack);

#endif /* NB_RTPS_RELIABLE_H */
