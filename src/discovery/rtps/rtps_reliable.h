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
#define NB_RTPS_RELIABLE_MAX_CAPACITY 256u

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

#endif /* NB_RTPS_RELIABLE_H */
