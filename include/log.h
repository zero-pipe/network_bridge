#ifndef NB_LOG_H
#define NB_LOG_H

/*
 * Thin logging macros over elog. nb_log_* short-circuit when logging is
 * disabled; nb_node_create installs the configured switch and level.
 */

#include <elog.h>

#include <types.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize elog on first use; thread-safe and idempotent. */
void nb_log_init(void);

/*
 * Runtime switch: when enabled is false every nb_log_* macro short-circuits
 * without formatting or output. level is an nb_log_level_t (0=assert ..
 * 5=verbose). nb_node_create calls this once from the log configuration.
 */
void nb_log_configure(bool enabled, nb_log_level_t level);

/* Whether logging is currently enabled; the first call initializes elog. */
bool nb_log_enabled(void);

#ifdef __cplusplus
}
#endif

#ifndef NB_LOG_TAG
#define NB_LOG_TAG "rpc"
#endif

#define nb_log_debug(...)                            \
    do {                                              \
        if (nb_log_enabled()) {                      \
            elog_debug(NB_LOG_TAG, __VA_ARGS__);     \
        }                                             \
    } while (0)

#define nb_log_info(...)                            \
    do {                                             \
        if (nb_log_enabled()) {                     \
            elog_info(NB_LOG_TAG, __VA_ARGS__);     \
        }                                            \
    } while (0)

#define nb_log_warn(...)                            \
    do {                                             \
        if (nb_log_enabled()) {                     \
            elog_warn(NB_LOG_TAG, __VA_ARGS__);     \
        }                                            \
    } while (0)

#define nb_log_error(...)                           \
    do {                                             \
        if (nb_log_enabled()) {                     \
            elog_error(NB_LOG_TAG, __VA_ARGS__);    \
        }                                            \
    } while (0)

#endif /* NB_LOG_H */
