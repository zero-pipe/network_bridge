#ifndef NB_RTPS_SEDP_H
#define NB_RTPS_SEDP_H

/*
 * SEDP endpoint discovery payload.
 *
 * This module builds and parses the ParameterList carried by a publication or
 * subscription DATA sample. Reliable writer state, heartbeat/acknack handling,
 * and endpoint bookkeeping live in the participant runtime.
 */

#include <stddef.h>
#include <stdint.h>

#include <nb/error.h>

#include "rtps_types.h"

#define NB_RTPS_SEDP_TOPIC_MAX 128
#define NB_RTPS_SEDP_TYPE_MAX 128
#define NB_RTPS_SEDP_PARTITION_MAX 64
#define NB_RTPS_SEDP_USER_DATA_MAX 256
#define NB_RTPS_SEDP_LOCATORS_MAX 4
#define NB_RTPS_SEDP_PAYLOAD_MAX 1200

/* QoS values used by PID_RELIABILITY, PID_DURABILITY, and PID_HISTORY. */
#define NB_RTPS_RELIABILITY_BEST_EFFORT 1u
#define NB_RTPS_RELIABILITY_RELIABLE 2u

#define NB_RTPS_DURABILITY_VOLATILE 0u
#define NB_RTPS_DURABILITY_TRANSIENT_LOCAL 1u
#define NB_RTPS_DURABILITY_TRANSIENT 2u
#define NB_RTPS_DURABILITY_PERSISTENT 3u

#define NB_RTPS_HISTORY_KEEP_LAST 0u
#define NB_RTPS_HISTORY_KEEP_ALL 1u

typedef struct nb_rtps_endpoint_info {
    nb_rtps_guid_t endpoint_guid;
    char topic_name[NB_RTPS_SEDP_TOPIC_MAX];
    char type_name[NB_RTPS_SEDP_TYPE_MAX];
    uint32_t reliability;
    uint32_t durability;
    uint32_t history_kind;
    uint32_t history_depth;
    char partition[NB_RTPS_SEDP_PARTITION_MAX];
    nb_rtps_locator_t unicast[NB_RTPS_SEDP_LOCATORS_MAX];
    size_t unicast_count;
    nb_rtps_locator_t multicast[NB_RTPS_SEDP_LOCATORS_MAX];
    size_t multicast_count;
    uint8_t user_data[NB_RTPS_SEDP_USER_DATA_MAX];
    size_t user_data_len;
} nb_rtps_endpoint_info_t;

/* Build the serialized ParameterList for a SEDP DATA payload. */
int nb_rtps_sedp_build(const nb_rtps_endpoint_info_t *info,
                       void *buf,
                       size_t cap,
                       size_t *out_len);

/* Parse a SEDP payload. Endpoint GUID, topic name, and type name are required;
 * unknown parameters (including vendor-specific ones) are ignored. */
int nb_rtps_sedp_parse(const void *payload, size_t len, nb_rtps_endpoint_info_t *out);

/* Parse the endpoint GUID from a key-only dispose sample. */
int nb_rtps_sedp_parse_key(const void *payload, size_t len, nb_rtps_guid_t *out);

#endif /* NB_RTPS_SEDP_H */
