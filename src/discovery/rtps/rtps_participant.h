#ifndef NB_RTPS_PARTICIPANT_H
#define NB_RTPS_PARTICIPANT_H

/*
 * SPDP participant runtime.
 *
 * One instance owns one RTPS participant: it announces itself on the SPDP
 * multicast group (optional) and to configured unicast peers, receives
 * announcements from other participants, and tracks their leases.
 *
 * The runtime is discovery-only. It does not send or receive application
 * data; SEDP endpoint exchange is layered on top of it later.
 */

#include <stddef.h>
#include <stdint.h>

#include <nb/error.h>

#include "rtps_spdp.h"
#include "rtps_types.h"

#define NB_RTPS_PEER_HOST_MAX 64
#define NB_RTPS_MAX_PEERS 64
#define NB_RTPS_DEFAULT_MAX_REMOTES 1024u

typedef enum nb_rtps_participant_event {
    NB_RTPS_PARTICIPANT_ADDED = 0,
    NB_RTPS_PARTICIPANT_UPDATED = 1,
    NB_RTPS_PARTICIPANT_REMOVED = 2
} nb_rtps_participant_event_t;

typedef struct nb_rtps_peer {
    char host[NB_RTPS_PEER_HOST_MAX];
    uint16_t port;
} nb_rtps_peer_t;

typedef struct nb_rtps_participant_config {
    uint32_t domain_id; /* 0 = default domain */
    /* Interface to bind. NULL or "0.0.0.0" binds every interface. */
    const char *bind_host;
    /* Address advertised in locators. NULL = bind_host when it is a concrete
     * IPv4 address, otherwise 127.0.0.1. Set this explicitly in production. */
    const char *advertise_host;
    /* SPDP multicast group. NULL = 239.255.0.1; "" disables multicast. */
    const char *multicast_group;
    /* SPDP port. 0 = 7400 + 250 * domain_id when multicast is enabled, or an
     * ephemeral port when multicast is disabled. */
    uint16_t spdp_port;
    /* Metatraffic unicast port. 0 = ephemeral; the chosen value is advertised. */
    uint16_t unicast_port;
    uint32_t announce_interval_ms; /* 0 = 3000, minimum 100 */
    uint32_t lease_ms;             /* 0 = 20000, range [1000, 3600000] */
    const char *name;
    /* Optional framework data-plane locator advertised as default unicast. */
    nb_rtps_locator_t default_unicast;
    const nb_rtps_peer_t *peers;
    size_t peer_count;
    uint32_t max_remote_participants; /* 0 = 1024 */
} nb_rtps_participant_config_t;

typedef struct nb_rtps_participant nb_rtps_participant_t;

typedef void (*nb_rtps_participant_callback_t)(void *user,
                                               nb_rtps_participant_event_t event,
                                               const nb_rtps_participant_info_t *info);

/* Bind sockets and initialize state. Does not start the discovery thread. */
nb_rtps_participant_t *nb_rtps_participant_create(const nb_rtps_participant_config_t *config);
void nb_rtps_participant_destroy(nb_rtps_participant_t *participant);

int nb_rtps_participant_start(nb_rtps_participant_t *participant);
void nb_rtps_participant_stop(nb_rtps_participant_t *participant);

/* Set before start when possible; safe to change while running. */
void nb_rtps_participant_set_callback(nb_rtps_participant_t *participant,
                                      nb_rtps_participant_callback_t callback,
                                      void *user);

/* Add a unicast SPDP destination. Only valid before start. */
int nb_rtps_participant_add_peer(nb_rtps_participant_t *participant,
                                 const char *host,
                                 uint16_t port);

/* Port actually bound, useful when spdp_port was 0. */
uint16_t nb_rtps_participant_spdp_port(const nb_rtps_participant_t *participant);
size_t nb_rtps_participant_remote_count(const nb_rtps_participant_t *participant);

/* Send one announcement now. */
int nb_rtps_participant_announce(nb_rtps_participant_t *participant);

/* Current local participant record. */
int nb_rtps_participant_local_info(const nb_rtps_participant_t *participant,
                                   nb_rtps_participant_info_t *out);

#endif /* NB_RTPS_PARTICIPANT_H */
