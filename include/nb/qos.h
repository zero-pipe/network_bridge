#ifndef NB_QOS_H
#define NB_QOS_H

/*
 * Transport-neutral QoS policy, modelled after the DDS QoS policies that
 * Cyclone DDS exposes. The framework maps these fields where a provider has a
 * native equivalent (dds) and treats them as advisory/inherit for providers
 * that do not (udp/tcp/shm).
 *
 * NB_QOS_*_INHERIT (0) means "do not override the provider default". This
 * keeps existing behaviour when no QoS is configured and lets a binding
 * override only the policies it cares about.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NB_QOS_PARTITION_MAX 128

typedef enum nb_qos_reliability {
    NB_QOS_RELIABILITY_INHERIT = 0,
    NB_QOS_RELIABILITY_BEST_EFFORT = 1,
    NB_QOS_RELIABILITY_RELIABLE = 2,
    /* Deprecated spellings. */
    NB_QOS_BEST_EFFORT = NB_QOS_RELIABILITY_BEST_EFFORT,
    NB_QOS_RELIABLE = NB_QOS_RELIABILITY_RELIABLE
} nb_qos_reliability_t;

typedef enum nb_qos_durability {
    NB_QOS_DURABILITY_INHERIT = 0,
    NB_QOS_DURABILITY_VOLATILE = 1,
    NB_QOS_DURABILITY_TRANSIENT_LOCAL = 2,
    NB_QOS_VOLATILE = NB_QOS_DURABILITY_VOLATILE,
    NB_QOS_TRANSIENT_LOCAL = NB_QOS_DURABILITY_TRANSIENT_LOCAL
} nb_qos_durability_t;

typedef enum nb_qos_history {
    NB_QOS_HISTORY_INHERIT = 0,
    NB_QOS_HISTORY_KEEP_LAST = 1,
    NB_QOS_HISTORY_KEEP_ALL = 2,
    NB_QOS_KEEP_LAST = NB_QOS_HISTORY_KEEP_LAST,
    NB_QOS_KEEP_ALL = NB_QOS_HISTORY_KEEP_ALL
} nb_qos_history_t;

typedef enum nb_qos_liveliness {
    NB_QOS_LIVELINESS_INHERIT = 0,
    NB_QOS_LIVELINESS_AUTOMATIC = 1,
    NB_QOS_LIVELINESS_MANUAL_BY_PARTICIPANT = 2,
    NB_QOS_LIVELINESS_MANUAL_BY_TOPIC = 3
} nb_qos_liveliness_t;

typedef enum nb_qos_ownership {
    NB_QOS_OWNERSHIP_INHERIT = 0,
    NB_QOS_OWNERSHIP_SHARED = 1,
    NB_QOS_OWNERSHIP_EXCLUSIVE = 2
} nb_qos_ownership_t;

typedef struct nb_qos {
    nb_qos_reliability_t reliability;
    nb_qos_durability_t durability;
    nb_qos_history_t history;
    nb_qos_liveliness_t liveliness;
    nb_qos_ownership_t ownership;
    uint32_t depth;      /* keep_last depth; 0 = provider default */
    uint32_t deadline_ms;
    uint32_t lifespan_ms;
    uint32_t lease_duration_ms; /* liveliness lease */
    uint32_t latency_budget_ms;
    char partition[NB_QOS_PARTITION_MAX];
} nb_qos_t;

void nb_qos_init(nb_qos_t *qos);
/* Fill fields that are INHERIT/empty in qos from parent (field-wise). */
void nb_qos_inherit(nb_qos_t *qos, const nb_qos_t *parent);
/* True when no policy is set (all fields INHERIT/zero/empty). */
bool nb_qos_is_default(const nb_qos_t *qos);
/* Check enum ranges and the partition NUL terminator. */
int nb_qos_validate(const nb_qos_t *qos);
const char *nb_qos_validate_reason(const nb_qos_t *qos);

#ifdef __cplusplus
}
#endif

#endif /* NB_QOS_H */
