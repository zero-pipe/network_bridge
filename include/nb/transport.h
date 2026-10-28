#ifndef NB_TRANSPORT_H
#define NB_TRANSPORT_H

/*
 * Transport and endpoint extension API.
 *
 * endpoint parses/formats scheme://host:port; a transport provider plugs in a
 * custom transport. The UDP/TCP implementations are not part of this public
 * header.
 *
 * Security boundary: the built-in UDP/TCP transports are plaintext and
 * unauthenticated. The envelope only checks magic/version/CRC integrity
 * (against bit errors and cross-packet mixing), provides no confidentiality or
 * identity guarantees, and must not be exposed directly to an untrusted
 * network. For TLS/DTLS or application-level authentication:
 *   - carry an encrypted channel through a custom transport provider (see
 *     transport_provider.h), or
 *   - validate credentials in metadata inside nb_service_set_interceptor / a
 *     handler, and use peer_rate_limit_pps / max_streams for basic flow
 *     control and resource caps.
 */

#include <nb/endpoint.h>
#include <nb/transport_provider.h>

#endif /* NB_TRANSPORT_H */
