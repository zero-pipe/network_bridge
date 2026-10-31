#ifndef NB_CLIENT_H
#define NB_CLIENT_H

/*
 * Network bridge client API.
 *
 * A proxy is the high-level interface with service discovery and routing; a
 * stub is the transport-independent low-level call primitive. Synchronous
 * calls are part of the proxy API and are declared in proxy.h.
 */

#include <nb/runtime.h>
#include <nb/proxy.h>
#include <nb/publish.h>
#include <nb/stub.h>

#endif /* NB_CLIENT_H */
