#ifndef NB_H
#define NB_H

/*
 * Optional canonical SDK entry point.
 *
 * Applications that want the complete API can include <nb.h>. Projects that
 * prefer a minimal include set should keep using the role headers:
 * <runtime.h>, <server.h>, <client.h>, <transport.h>, <extensions.h>.
 */

#include <runtime.h>
#include <server.h>
#include <client.h>
#include <transport.h>
#include <extensions.h>
#include <config.h>
#include <message.h>
#include <qos.h>
#include <log.h>
#include <publish.h>
#include <subscribe.h>
#include <dds.h>
#include <buf.h>
#include <alloc.h>
#include <poolset.h>
#include <slotpool.h>

#endif /* NB_H */
