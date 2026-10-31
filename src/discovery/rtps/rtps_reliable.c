#include "rtps_reliable.h"

#include <stdlib.h>
#include <string.h>

#include <ntk.h>

typedef struct nb_rtps_reliable_sample {
    uint32_t sn;
    uint8_t *payload;
    size_t len;
    int dispose;
} nb_rtps_reliable_sample_t;

struct nb_rtps_reliable_writer {
    ntk_mutex *lock;
    nb_rtps_reliable_sample_t *samples;
    size_t capacity;
    size_t count;
    size_t head;
    uint32_t next_sn;
};

static nb_rtps_reliable_sample_t *find_sample(nb_rtps_reliable_writer_t *writer, uint32_t sn)
{
    size_t i;

    for (i = 0; i < writer->count; i++) {
        nb_rtps_reliable_sample_t *sample =
            &writer->samples[(writer->head + i) % writer->capacity];
        if (sample->sn == sn) {
            return sample;
        }
    }
    return NULL;
}

nb_rtps_reliable_writer_t *nb_rtps_reliable_writer_create(size_t capacity)
{
    nb_rtps_reliable_writer_t *writer;

    if (capacity == 0) {
        capacity = NB_RTPS_RELIABLE_DEFAULT_CAPACITY;
    }
    if (capacity > NB_RTPS_RELIABLE_MAX_CAPACITY) {
        capacity = NB_RTPS_RELIABLE_MAX_CAPACITY;
    }
    writer = (nb_rtps_reliable_writer_t *)calloc(1, sizeof(*writer));
    if (!writer) {
        return NULL;
    }
    writer->samples =
        (nb_rtps_reliable_sample_t *)calloc(capacity, sizeof(*writer->samples));
    writer->lock = ntk_mutex_create(NTK_MUTEX_NORMAL);
    if (!writer->samples || !writer->lock) {
        if (writer->lock) {
            ntk_mutex_destroy(writer->lock);
        }
        free(writer->samples);
        free(writer);
        return NULL;
    }
    writer->capacity = capacity;
    return writer;
}

void nb_rtps_reliable_writer_destroy(nb_rtps_reliable_writer_t *writer)
{
    size_t i;

    if (!writer) {
        return;
    }
    for (i = 0; i < writer->capacity; i++) {
        free(writer->samples[i].payload);
    }
    if (writer->lock) {
        ntk_mutex_destroy(writer->lock);
    }
    free(writer->samples);
    free(writer);
}

int nb_rtps_reliable_add(nb_rtps_reliable_writer_t *writer,
                         const void *payload,
                         size_t len,
                         int dispose,
                         uint32_t *out_sn)
{
    nb_rtps_reliable_sample_t *sample;
    uint8_t *copy = NULL;
    size_t index;

    if (!writer || (len > 0 && !payload)) {
        return NB_ERR_INVALID;
    }
    if (len > 0) {
        copy = (uint8_t *)malloc(len);
        if (!copy) {
            return NB_ERR_NOMEM;
        }
        memcpy(copy, payload, len);
    }

    ntk_mutex_lock(writer->lock);
    if (writer->count < writer->capacity) {
        index = (writer->head + writer->count) % writer->capacity;
        writer->count++;
    } else {
        index = writer->head;
        free(writer->samples[index].payload);
        writer->head = (writer->head + 1) % writer->capacity;
    }
    sample = &writer->samples[index];
    sample->sn = ++writer->next_sn;
    sample->payload = copy;
    sample->len = len;
    sample->dispose = dispose ? 1 : 0;
    if (out_sn) {
        *out_sn = sample->sn;
    }
    ntk_mutex_unlock(writer->lock);
    return NB_OK;
}

int nb_rtps_reliable_get(const nb_rtps_reliable_writer_t *writer,
                         uint32_t sn,
                         const uint8_t **payload,
                         size_t *len,
                         int *dispose)
{
    nb_rtps_reliable_sample_t *sample;

    if (!writer) {
        return NB_ERR_INVALID;
    }
    ntk_mutex_lock(writer->lock);
    sample = find_sample((nb_rtps_reliable_writer_t *)writer, sn);
    if (!sample) {
        ntk_mutex_unlock(writer->lock);
        return NB_ERR_NOTFOUND;
    }
    if (payload) {
        *payload = sample->payload;
    }
    if (len) {
        *len = sample->len;
    }
    if (dispose) {
        *dispose = sample->dispose;
    }
    ntk_mutex_unlock(writer->lock);
    return NB_OK;
}

void nb_rtps_reliable_state(const nb_rtps_reliable_writer_t *writer,
                            uint32_t *first_sn,
                            uint32_t *last_sn,
                            size_t *sample_count)
{
    uint32_t first;
    uint32_t last;
    size_t count;

    if (!writer) {
        return;
    }
    ntk_mutex_lock(writer->lock);
    count = writer->count;
    if (count == 0) {
        first = writer->next_sn + 1u;
        last = writer->next_sn;
    } else {
        first = writer->samples[writer->head].sn;
        last = writer->samples[(writer->head + count - 1) % writer->capacity].sn;
    }
    ntk_mutex_unlock(writer->lock);

    if (first_sn) {
        *first_sn = first;
    }
    if (last_sn) {
        *last_sn = last;
    }
    if (sample_count) {
        *sample_count = count;
    }
}

int nb_rtps_reliable_process_acknack(nb_rtps_reliable_writer_t *writer,
                                     const nb_rtps_acknack_t *acknack,
                                     nb_rtps_reliable_resend_fn resend,
                                     void *user,
                                     size_t *resent)
{
    uint32_t i;
    size_t sent = 0;
    int rc = NB_OK;

    if (!writer || !acknack || !resend) {
        return NB_ERR_INVALID;
    }
    if (acknack->num_bits > NB_RTPS_ACKNACK_MAX_BITS ||
        acknack->base.high != 0) {
        return NB_ERR_NOT_SUPPORTED;
    }

    ntk_mutex_lock(writer->lock);
    for (i = 0; i < acknack->num_bits; i++) {
        nb_rtps_reliable_sample_t *sample;
        uint32_t sn;

        if ((acknack->bitmap[i / 32u] & (1u << (31u - (i % 32u)))) == 0) {
            continue;
        }
        sn = acknack->base.low + i;
        sample = find_sample(writer, sn);
        if (!sample) {
            continue;
        }
        rc = resend(user, sample->sn, sample->payload, sample->len, sample->dispose);
        if (rc != NB_OK) {
            break;
        }
        sent++;
    }
    ntk_mutex_unlock(writer->lock);
    if (resent) {
        *resent = sent;
    }
    return rc;
}
