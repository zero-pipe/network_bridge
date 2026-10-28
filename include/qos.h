#ifndef NB_QOS_H
#define NB_QOS_H

/*
 * Transport-neutral QoS policy, modelled after the DDS QoS policies that
 * Cyclone DDS exposes. The framework maps these fields where a provider has a
 * native equivalent (dds), and treats them as advisory/inherit for providers
 * that do not (udp/tcp/shm).
 *
 * NB_QOS_*_INHERIT (0) means "do not override the provider default". This
 * keeps existing behaviour when no QoS is configured and lets a binding
 * override only the policies it cares about.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NB_QOS_PARTITION_MAX 128

typedef enum nb_qos_reliability {
    NB_QOS_RELIABILITY_INHERIT = 0,
    NB_QOS_BEST_EFFORT = 1,
    NB_QOS_RELIABLE = 2
} nb_qos_reliability_t;

typedef enum nb_qos_durability {
    NB_QOS_DURABILITY_INHERIT = 0,
    NB_QOS_VOLATILE = 1,
    NB_QOS_TRANSIENT_LOCAL = 2
} nb_qos_durability_t;

typedef enum nb_qos_history {
    NB_QOS_HISTORY_INHERIT = 0,
    NB_QOS_KEEP_LAST = 1,
    NB_QOS_KEEP_ALL = 2
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
    uint8_t reliability; /* nb_qos_reliability_t */
    uint8_t durability;  /* nb_qos_durability_t */
    uint8_t history;     /* nb_qos_history_t */
    uint8_t liveliness;  /* nb_qos_liveliness_t */
    uint8_t ownership;   /* nb_qos_ownership_t */
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
int nb_qos_is_default(const nb_qos_t *qos);
int nb_qos_validate(const nb_qos_t *qos);
const char *nb_qos_validate_reason(const nb_qos_t *qos);

#ifdef __cplusplus
}
#endif

#endif /* NB_QOS_H */
