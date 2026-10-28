#ifndef NB_SUBSCRIBE_H
#define NB_SUBSCRIBE_H

/*
 * Subscriber-facing aliases. A subscriber is an ordinary service whose method
 * names a topic. Topic-based transports deliver inbound samples as EVENT
 * messages to that method.
 */

#include <nb/service.h>

typedef nb_service_t nb_subscriber_t;
typedef nb_handler_fn nb_subscriber_handler_fn;

/* Thin inline wrappers instead of macros, so these keep function-pointer and
 * debugger-friendly identities while preserving the old names. */
static inline int nb_subscriber_create(nb_node_t *node,
                                       const char *name,
                                       nb_subscriber_t **out)
{
    return nb_service_create(node, name, out);
}

static inline void nb_subscriber_destroy(nb_subscriber_t *sub)
{
    nb_service_destroy(sub);
}

static inline int nb_subscriber_subscribe(nb_subscriber_t *sub,
                                          const char *topic,
                                          nb_subscriber_handler_fn handler,
                                          void *user)
{
    return nb_service_add_method(sub, topic, handler, user);
}

#endif /* NB_SUBSCRIBE_H */
