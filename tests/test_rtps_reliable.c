#include "rtps_reliable.h"

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

typedef struct resend_state {
    int count;
    uint32_t sn[8];
    char payload[8][8];
    int dispose[8];
} resend_state_t;

static int on_resend(void *user,
                     uint32_t sn,
                     const uint8_t *payload,
                     size_t len,
                     int dispose)
{
    resend_state_t *state = (resend_state_t *)user;

    if (state->count >= (int)(sizeof(state->sn) / sizeof(state->sn[0]))) {
        return NB_ERR_TOOBIG;
    }
    state->sn[state->count] = sn;
    if (len >= sizeof(state->payload[0])) {
        len = sizeof(state->payload[0]) - 1;
    }
    if (len > 0) {
        memcpy(state->payload[state->count], payload, len);
    }
    state->payload[state->count][len] = '\0';
    state->dispose[state->count] = dispose;
    state->count++;
    return NB_OK;
}

static void test_history_and_acknack(void)
{
    nb_rtps_reliable_writer_t *writer = nb_rtps_reliable_writer_create(2);
    nb_rtps_acknack_t ack;
    resend_state_t state;
    uint32_t sn = 0;
    uint32_t first = 0;
    uint32_t last = 0;
    size_t count = 0;
    const uint8_t *payload = NULL;
    size_t len = 0;
    int dispose = 0;

    CHECK(writer != NULL);
    if (!writer) {
        return;
    }
    CHECK(nb_rtps_reliable_add(writer, "a", 1, 0, &sn) == NB_OK && sn == 1);
    CHECK(nb_rtps_reliable_add(writer, "b", 1, 0, &sn) == NB_OK && sn == 2);
    CHECK(nb_rtps_reliable_add(writer, "c", 1, 0, &sn) == NB_OK && sn == 3);
    nb_rtps_reliable_state(writer, &first, &last, &count);
    CHECK(first == 2 && last == 3 && count == 2);
    CHECK(nb_rtps_reliable_get(writer, 1, &payload, &len, &dispose) == NB_ERR_NOTFOUND);
    CHECK(nb_rtps_reliable_get(writer, 2, &payload, &len, &dispose) == NB_OK);
    CHECK(len == 1 && payload[0] == 'b' && !dispose);
    CHECK(nb_rtps_reliable_get(writer, 3, &payload, &len, &dispose) == NB_OK);
    CHECK(len == 1 && payload[0] == 'c');

    memset(&ack, 0, sizeof(ack));
    ack.base.low = 2;
    ack.num_bits = 2;
    ack.bitmap[0] = 0x1u;
    memset(&state, 0, sizeof(state));
    CHECK(nb_rtps_reliable_process_acknack(writer, &ack, on_resend, &state, &count) == NB_OK);
    CHECK(count == 1 && state.count == 1u && state.sn[0] == 2);
    CHECK(strcmp(state.payload[0], "b") == 0);

    ack.bitmap[0] = 0x2u;
    memset(&state, 0, sizeof(state));
    CHECK(nb_rtps_reliable_process_acknack(writer, &ack, on_resend, &state, &count) == NB_OK);
    CHECK(count == 1 && state.count == 1u && state.sn[0] == 3);
    CHECK(strcmp(state.payload[0], "c") == 0);

    ack.bitmap[0] = 0x3u;
    memset(&state, 0, sizeof(state));
    CHECK(nb_rtps_reliable_process_acknack(writer, &ack, on_resend, &state, &count) == NB_OK);
    CHECK(count == 2 && state.count == 2u);
    CHECK(state.sn[0] == 2 && state.sn[1] == 3);

    ack.base.low = 4;
    ack.num_bits = 0;
    memset(&state, 0, sizeof(state));
    CHECK(nb_rtps_reliable_process_acknack(writer, &ack, on_resend, &state, &count) == NB_OK);
    CHECK(count == 0 && state.count == 0);

    CHECK(nb_rtps_reliable_add(writer, NULL, 0, 1, &sn) == NB_OK && sn == 4);
    nb_rtps_reliable_state(writer, &first, &last, &count);
    CHECK(first == 3 && last == 4 && count == 2);
    CHECK(nb_rtps_reliable_get(writer, 4, &payload, &len, &dispose) == NB_OK);
    CHECK(len == 0 && dispose == 1);

    nb_rtps_reliable_writer_destroy(writer);
}

int main(void)
{
    test_history_and_acknack();
    if (g_fail) {
        nb_log_error("rtps reliable tests failed");
        return 1;
    }
    printf("rtps reliable tests passed\n");
    return 0;
}
