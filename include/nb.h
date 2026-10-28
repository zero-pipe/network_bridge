#ifndef NB_H
#define NB_H

/*
 * Optional canonical SDK entry point.
 *
 * Applications that want the complete API can include <nb.h>. Projects that
 * prefer a minimal include set should keep using the role headers:
 * <nb/runtime.h>, <nb/server.h>, <nb/client.h>, <nb/transport.h>, <nb/extensions.h>.
 *
 * Compatibility: headers are kept source-compatible across releases, but
 * struct layouts are not guaranteed to be binary-compatible; link consumers
 * against the same library version and rebuild when upgrading. NB_VERSION_*
 * in types.h identifies the header API level.
 */

#include <nb/runtime.h>
#include <nb/server.h>
#include <nb/client.h>
#include <nb/transport.h>
#include <nb/extensions.h>
#include <nb/config.h>
#include <nb/message.h>
#include <nb/qos.h>
#include <nb/log.h>
#include <nb/publish.h>
#include <nb/subscribe.h>
#include <nb/dds.h>
#include <nb/buf.h>
#include <nb/alloc.h>
#include <nb/poolset.h>
#include <nb/slotpool.h>

#endif /* NB_H */
