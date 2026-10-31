#ifndef NB_SERVER_H
#define NB_SERVER_H

/*
 * Network bridge server API.
 *
 * Includes the node runtime and service registration/reply interfaces.
 * Client-only programs may include client.h instead, keeping server-side types
 * out of their public headers.
 */

#include <nb/runtime.h>
#include <nb/message_view.h>
#include <nb/service.h>

#endif /* NB_SERVER_H */
