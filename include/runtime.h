#ifndef NB_RUNTIME_H
#define NB_RUNTIME_H

/*
 * rpc runtime API.
 *
 * This layer owns node lifecycle, thread scheduling, service discovery,
 * routing, and runtime metrics. Business code usually builds on top of it via
 * server.h or client.h.
 */

#include <types.h>
#include <metrics.h>
#include <node.h>

#endif /* NB_RUNTIME_H */
