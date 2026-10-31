#define _CRT_SECURE_NO_WARNINGS

#include "rtps_participant.h"

#define NB_LOG_TAG "test"
#include <nb/log.h>

#include <dds/dds.h>
#include <ntk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

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

static void test_cyclone_spdp(void)
{
    const char *enabled = getenv("NB_TEST_RTPS_CYCLONE");
    const char *advertise_host = getenv("NB_TEST_RTPS_ADVERTISE_HOST");
    nb_rtps_participant_config_t config;
    nb_rtps_participant_t *nb;
    dds_entity_t cyclone;
    dds_entity_t reader;
    int saw_cyclone = 0;
    int saw_nb = 0;
    int waited = 0;

    if (!enabled || strcmp(enabled, "1") != 0) {
        nb_log_info("Cyclone interop test skipped; set NB_TEST_RTPS_CYCLONE=1");
        return;
    }

    cyclone = dds_create_participant(0, NULL, NULL);
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

    memset(&config, 0, sizeof(config));
    config.domain_id = 0;
    config.bind_host = "0.0.0.0";
    config.advertise_host =
        (advertise_host && advertise_host[0] != '\0') ? advertise_host
                                                      : "127.0.0.1";
    config.multicast_group = "239.255.0.1";
    config.spdp_port = 0; /* 7400 + 250 * 0 */
    config.announce_interval_ms = 200;
    config.lease_ms = 3000;
    config.name = "nb-rtps-interop";
    config.max_remote_participants = 32;
    nb = nb_rtps_participant_create(&config);
    CHECK(nb != NULL);
    if (!nb) {
        (void)dds_delete(reader);
        (void)dds_delete(cyclone);
        return;
    }
    CHECK(nb_rtps_participant_start(nb) == NB_OK);

    while (waited < 10000 && (!saw_nb || !saw_cyclone)) {
        if (!saw_nb) {
            saw_nb = cyclone_sees_participant(reader);
        }
        if (!saw_cyclone) {
            saw_cyclone = nb_rtps_participant_remote_count(nb) > 0;
        }
        if (!saw_nb || !saw_cyclone) {
            ntk_sleep_ms(100);
            waited += 100;
        }
    }
    CHECK(saw_nb);
    CHECK(saw_cyclone);
    if (!saw_nb || !saw_cyclone) {
        nb_log_error("Cyclone SPDP interop failed after %d ms (nb=%d cyclone=%d)",
                     waited,
                     saw_nb,
                     saw_cyclone);
    }

    nb_rtps_participant_stop(nb);
    nb_rtps_participant_destroy(nb);
    (void)dds_delete(reader);
    (void)dds_delete(cyclone);
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
