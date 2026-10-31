# RTPS Discovery

This document describes the standards-based discovery layer used by the
framework. It implements the RTPS discovery protocols (SPDP and SEDP) without
taking a dependency on a DDS implementation. The RTPS path also has a
best-effort application `DATA` path for the standard `dds::Message` type;
native traffic remains on the framework's UDP/TCP/SHM transports. The separate
Cyclone-backed `dds` transport provider is a normal data-plane transport and is
not part of the self-developed RTPS stack.

Status: the wire codec, SPDP announcement payload, SPDP participant runtime,
reliable SEDP endpoint exchange, and best-effort `dds::Message` application
`DATA` are implemented and covered by unit and interop tests. SPDP interop with
Cyclone DDS is verified on Windows and Linux. Cyclone also sees an nb SEDP
publication endpoint and receives nb `dds::Message` samples on Linux. Mapping
remote endpoints into the framework registry remains M4 work.

## 1. Scope

In scope:

- RTPS 2.4 messages over IPv4 UDP.
- Little-endian CDR and RTPS `ParameterList` encoding.
- SPDP: periodic participant announcement on the well-known multicast group.
- SEDP: reliable built-in endpoint exchange for publications and subscriptions.
- Best-effort application `DATA` carrying standard CDR_LE `dds::Message`
  (`sequence<octet> payload`) samples.
- Lease-based participant expiry.
- Mapping between RTPS endpoints and framework service records.

Out of scope (for now):

- RTPS fragmentation (`DATA_FRAG`), batching, reliable user-data delivery, and
  arbitrary IDL types. Business traffic does not use RTPS by default.
- DDS Security, TCP transport, IPv6, shared-memory locators.
- Full DDS QoS semantics. Only the QoS needed to describe framework endpoints
  is published.
- The ROS 2 node graph topic `ros_discovery_info`.

The last two points matter for interoperability expectations; see section 8.

## 2. Why SPDP and SEDP

The previous mesh discovery broadcast the full node state (`HELLO`/`BYE`) to
every peer at a fixed interval. It is simple and works for small clusters, but
it has no standard entity identity, no incremental endpoint updates, and no way
for a DDS participant to interpret what it sees.

SPDP and SEDP provide:

- stable participant and endpoint identifiers (GUIDs);
- a well-known multicast rendezvous point keyed by DDS domain;
- reliable, unicast endpoint exchange after participants meet;
- QoS metadata attached to each endpoint;
- a lease model instead of an explicit `BYE`.

They do not make discovery cheap at large scale: SPDP is still a periodic
multicast and SEDP matches endpoint pairs. Section 7 covers the limits.

## 3. Protocol subset

### 3.1 Addressing

| Item | Value |
| --- | --- |
| SPDP multicast group | `239.255.0.1` |
| SPDP port | `7400 + 250 * domain_id` |
| Metatraffic unicast | implementation-chosen port, advertised in locators |
| Domain id | configuration, default `0` |

`7400 + 250 * domain_id` is the `d0` offset from DDSI-RTPS 2.x. User-data
ports (`d1`/`d3`) are irrelevant here because the framework data plane has its
own addressing.

### 3.2 Participants (SPDP)

A participant periodically multicasts a `DATA` submessage from the built-in
SPDP writer (`0x000100c2`) to the SPDP multicast group. The serialized payload
is a `ParameterList` with at least:

| PID | Meaning |
| --- | --- |
| `PID_PROTOCOL_VERSION` | RTPS version, `2.4` |
| `PID_VENDORID` | vendor id; a registered OMG value is required before release |
| `PID_PARTICIPANT_GUID` | 16-byte participant GUID |
| `PID_PARTICIPANT_LEASE_DURATION` | lease duration |
| `PID_BUILTIN_ENDPOINT_SET` | which built-in endpoints the participant has |
| `PID_METATRAFFIC_UNICAST_LOCATOR` | unicast address for SEDP |
| `PID_METATRAFFIC_MULTICAST_LOCATOR` | multicast address, if used |
| `PID_DEFAULT_UNICAST_LOCATOR` | framework data-plane endpoint |
| `PID_ENTITY_NAME` | node name |
| `PID_USER_DATA` | compact framework summary |

SPDP is best effort and has no `BYE`. A participant disappears when its lease
expires.

### 3.3 Endpoints (SEDP)

After a remote participant is discovered, both sides create reliable built-in
writers and readers:

| Built-in endpoint | Entity id |
| --- | --- |
| Publications writer / reader | `0x000003c2` / `0x000003c7` |
| Subscriptions writer / reader | `0x000004c2` / `0x000004c7` |

Local endpoints are published as `DATA` samples on the built-in writers. The
payload is a `ParameterList` with `PID_ENDPOINT_GUID`, `PID_TOPIC_NAME`,
`PID_TYPE_NAME`, `PID_RELIABILITY`, `PID_DURABILITY`, `PID_HISTORY`,
`PID_PARTITION`, and locator PIDs.

`SEDP` is reliable: the writer keeps a small history, sends `HEARTBEAT`, and
retransmits samples requested by `ACKNACK`. Endpoint removal uses `DATA` with
the dispose flag. The initial implementation keeps the history in memory and
caps it; there is no `GAP` support until endpoint churn requires it.

Endpoint locators use one `PID_UNICAST_LOCATOR` or `PID_MULTICAST_LOCATOR`
parameter per locator, each carrying exactly 24 bytes. They are not encoded as
a length-prefixed sequence. `PID_RELIABILITY` is 12 bytes: kind,
`max_blocking_time.seconds`, and `max_blocking_time.fraction`.

`ACKNACK` bitmaps are MSB-first: bit 0 is `0x80000000`. Sending an ACKNACK with
the FINAL flag avoids the peer treating it as a request for an immediate
HEARTBEAT.

### 3.4 Timers

| Timer | Default | Notes |
| --- | --- | --- |
| SPDP announcement | 3 s | jittered per participant |
| Participant lease | 20 s | remote entry expires after this |
| SEDP heartbeat | 1 s | per reliable writer while unacknowledged |
| Lease sweep | 200 ms | runtime tick; leases are checked against wall-clock time |

Defaults are configuration values, not constants in the protocol.

## 4. Framework mapping

### 4.1 Nodes and services

One framework node maps to one RTPS participant. The participant GUID prefix is
12 bytes derived from the vendor id, wall-clock time, and the participant
address, so two participants in one process never share a prefix. The
participant GUID uses the standard `ENTITYID_PARTICIPANT` (`0x000001c1`); the
SPDP writer remains `0x000100c2`.

Every provided service maps to one SEDP publication endpoint:

| SEDP field | Value |
| --- | --- |
| topic name | `nb/<service>` (native), optionally `rt/<topic>` for ROS 2 |
| type name | `nb::Service` (native), configured for interop |
| reliability | reliable for request/response services, best effort for events |
| durability | volatile by default |
| locator | the service's UDP data-plane endpoint |
| partition | configured partition, empty by default |

The complete framework record (node name, methods, all transport endpoints,
version) is carried in the vendor parameter `NB_RTPS_PID_NB_RECORD` (`0x8001`).
A DDS implementation ignores unknown vendor parameters; a framework peer parses
them to rebuild the service record.

### 4.2 Endpoints that are not UDP

RTPS locators can only describe UDP/IP endpoints. TCP and SHM bindings are
carried in the vendor record as endpoint URIs. Native peers use the vendor
record; DDS peers see a UDP locator only when the service has one.

## 5. Wire layer

Implemented in `src/discovery/rtps/`:

| File | Contents |
| --- | --- |
| `rtps_types.h/.c` | GUIDs, entity ids, locators, sequence numbers |
| `rtps_cdr.h/.c` | bounds-checked little-endian CDR writer/reader |
| `rtps_param.h/.c` | `ParameterList` writer, iterator, typed getters |
| `rtps_message.h/.c` | RTPS header, submessage framing, `DATA`, `HEARTBEAT`, `ACKNACK`, `INFO_TS`, `INFO_DST` |
| `rtps_spdp.h/.c` | SPDP announcement payload builder and parser |
| `rtps_sedp.h/.c` | SEDP publication/subscription endpoint payload builder and parser |
| `rtps_reliable.h/.c` | Bounded reliable writer history and ACKNACK sample retransmit |
| `rtps_participant.h/.c` | SPDP/SEDP runtime: sockets, thread, timers, participant and endpoint tables |

Encoding rules:

- The RTPS header is 20 bytes: `RTPS`, version, vendor id, GUID prefix.
- Each submessage starts with a 4-byte header (`id`, `flags`,
  `octetsToNextHeader`) and is padded to a 4-byte boundary.
- A `ParameterList` entry is `pid` (u16), `length` (u16), value, then zero
  padding to 4 bytes. The stored length includes the padding and is always a
  multiple of 4, which is what Cyclone DDS expects. The list ends with
  `PID_SENTINEL`.
- SPDP payloads are prefixed with the 4-byte `PL_CDR_LE` encapsulation header
  (`00 03 00 00`), as required by DDSI discovery serialization.
- Parameter values are CDR streams whose alignment starts at the first value
  byte. This matches how implementations align `ParameterList` values.
- A locator is `kind` (u32), `port` (u32), and a 16-byte address. For IPv4,
  the address is stored in bytes 12-15 of the address field; bytes 0-11 are
  zero.
- SEDP endpoint locator PIDs are single 24-byte locators repeated for each
  address, not a sequence. `PID_RELIABILITY` is 12 bytes.
- `DATA` uses `octetsToInlineQos = 16`; the payload starts after the inline QoS
  parameter list (or immediately after the fixed fields when the inline QoS
  flag is clear).
- `ACKNACK` uses RTPS sequence-number sets with MSB-first bitmaps. The FINAL
  flag is set on generated ACKNACKs to suppress the peer's heartbeat response.
- DATA flags follow RTPS 2.4: `D` (`0x04`) means a serialized payload is
  present and `K` (`0x08`) means it contains a key. Dispose is encoded as
  key-only (`K` set, `D` clear).

The codec is fuzz-friendly: every read is bounds checked and malformed input
returns `NB_ERR_BAD_MESSAGE` without partial state.

## 6. Runtime design

Implemented in `src/discovery/rtps/rtps_participant.h/.c`. `create()` binds
sockets and initializes state, so callers can learn ephemeral ports before
`start()`; `start()` launches one discovery thread; `stop()` joins it;
`destroy()` releases all resources.

### 6.1 Configuration

| Field | Default | Notes |
| --- | --- | --- |
| `bind_host` | `0.0.0.0` | interface used for binds and multicast join |
| `advertise_host` | bind host, else `127.0.0.1` | address placed in locators |
| `multicast_group` | `239.255.0.1` | empty string disables multicast |
| `spdp_port` | 0 | domain port when multicast is enabled, ephemeral otherwise |
| `unicast_port` | 0 | ephemeral; advertised as the metatraffic unicast locator |
| `announce_interval_ms` | 3000 | minimum 100 |
| `lease_ms` | 20000 | range 1000 to 3600000 |
| `peers` | empty | unicast SPDP destinations in addition to multicast |
| `max_remote_participants` | 1024 | bounded table; oldest entry is evicted |

### 6.2 Sockets and thread

- The multicast receive socket binds to `bind_host:spdp_port` with address
  reuse, joins the SPDP group, and sets TTL 1 and multicast loopback on.
- The unicast receive socket binds to `bind_host:unicast_port`. It is reserved
  for SEDP and participant messages; SPDP itself is sent from the multicast
  socket so the source port is the SPDP port.
- One poller and one thread drive both sockets. The poll timeout is 100 ms and
  `stop()` wakes the poller before joining.
- An announcement is sent immediately on start, then every interval with
  approximately 10% jitter. Lease expiry is checked every 200 ms.
- Timestamps use `ntk_wall_ms()`, matching RTPS lease semantics. A backwards
  wall-clock jump can extend a lease until the clock catches up.

### 6.3 Receive path

1. Drain each non-blocking socket until `NTK_ERR_AGAIN`.
2. Parse the RTPS header, drop self announcements by GUID prefix, and walk
   submessages.
3. Accept `DATA` from the SPDP writer and parse its `ParameterList`. The
   participant GUID prefix in the payload must match the message header.
4. Accept SEDP `DATA`, `HEARTBEAT`, and `ACKNACK` from the built-in
   publication/subscription endpoints; decode endpoints and mark samples
   received.
5. Key remotes by GUID prefix. The callback fires on first sight (`ADDED`), on
   a changed record (`UPDATED`), and when the lease expires (`REMOVED`).
6. Callbacks run outside the table lock. The table holds 1024 entries by
   default; when full, the entry with the oldest `last_seen` is replaced.

No packet-path allocation occurs after initialization.

### 6.4 Unicast peers

`nb_rtps_participant_add_peer()` adds a unicast SPDP destination before start.
Peers are used in addition to multicast. Setting `multicast_group=""` gives a
pure unicast mode with an ephemeral or explicit SPDP port; this is the mode used
by the CI test because it does not depend on multicast routing.

Peer lists are not transitive. Scoping for 1000 participants (peer groups,
superpeer/relay, or a discovery server) is still open; see section 7.

### 6.5 Current limits

- SPDP and SEDP endpoints are exchanged, but remote endpoints are not yet
  mapped into framework service records (M4).
- Application `DATA` is best-effort and limited to `dds::Message`; no
  fragmentation, reliable user data, security, TCP, or IPv6.
- On multi-host deployments `advertise_host` must be set explicitly. With a
  wildcard bind and no advertise host, the runtime falls back to `127.0.0.1`.
- `stop()` clears the local remote table without emitting `REMOVED` callbacks.

State:

| Table | Key | Status |
| --- | --- | --- |
| participants | GUID prefix | implemented, bounded |
| endpoints | endpoint GUID | implemented, bounded |
| writers | local writer id | implemented, bounded |

The registry receives records through the existing
`nb_registry_backend_ops_t` seam (`start` / `stop` / `tick` / `announce`), so
the rest of the framework does not depend on RTPS types.

## 7. Scale

SPDP cost is `O(N^2)` packets per period: each participant multicasts once and
every participant receives every announcement.

| Participants | Expected behavior |
| --- | --- |
| up to ~100 | fine on a wired LAN |
| ~100-500 | tune the announcement interval, reduce the broadcast domain, watch Wi-Fi |
| 1000+ | use unicast peers or a discovery server; plain multicast does not scale |

Mitigations, in order of implementation cost:

1. Configurable announcement interval and jitter.
2. Unicast SPDP peers (`discovery.peers`) for routed or large deployments.
3. Bounded endpoint tables with a deterministic eviction policy.
4. A discovery-server mode later, if deployments require it.

SEDP adds pairwise endpoint state. The runtime caps remote endpoints per
participant and per process; dropping the oldest entry is preferable to
unbounded growth.

## 8. Interoperability expectations

Discovery-level:

- Cyclone DDS 0.10.5 sees the framework participant and its SPDP endpoint, and
  the framework sees Cyclone participants. On Linux, Cyclone's
  `DCPSPublication` reader also sees the nb `nb/sedp` publication endpoint.
  Verification used Cyclone on Windows and Linux; the Linux capture showed
  both sides using `PL_CDR_LE` and IPv4 locators in the last four address
  bytes.
- A framework node decodes Cyclone SEDP endpoints through the same SEDP path
  covered by the loopback test. An explicit Cyclone-to-nb endpoint assertion
  remains in M6.

Data-level:

- A DDS participant that matches a framework endpoint can receive RTPS `DATA`
  from the best-effort path. The serialized payload is standard CDR_LE
  `dds::Message`: `00 01 00 00`, a `uint32` length, then the raw payload bytes.
- `e2e.rtps_cyclone` creates an nb publication of type `dds::Message` and an
  independent Cyclone reader, publishes a byte sequence, and verifies that
  Cyclone receives and deserializes it.
- The built-in Cyclone-backed `dds` transport provider is a separate data plane
  and does carry application frames over DDS. `e2e.dds_remote` starts an
  independent Cyclone reader and verifies that it receives an nb event sample
  when `intra_process` is `off`; this does not exercise the self-developed RTPS
  discovery endpoints.
- ROS 2 topic/service data exchange therefore needs either an RTPS data-plane
  implementation for those endpoints or a bridge. This document does not
  promise direct ROS 2 RPC.

ROS 2 graph:

- ROS 2 builds its node graph from `ros_discovery_info`, not from SPDP/SEDP
  alone. A non-ROS participant will not appear in `ros2 node list` without
  publishing that topic.
- Topic/type matching for actual traffic is a data-plane concern; see above.

Native framework deployment does not depend on any of this: discovery is used
only to populate the registry, and traffic uses the framework transports.

## 9. Testing

Current:

- `unit.rtps_wire`: CDR round trips, alignment, `dds::Message` serialization
  (header + length + raw octets, zero-copy parse), `ParameterList` round trips,
  `DATA`/`HEARTBEAT`/`ACKNACK` framing, truncation handling.
- `unit.rtps_sedp`: endpoint payload round trips, defaults, repeated
  24-byte locator parameters, 12-byte reliability, partition, user data, and
  missing-field rejection.
- `unit.rtps_reliable`: history trimming, sequence-number state, ACKNACK bitmap
  retransmit, and dispose samples.
- `e2e.rtps_participant`: two participants discover each other over unicast
  peers, exchange SEDP publications/subscriptions, dispose endpoints, and
  expire leases. The test also covers repeated create/destroy.
- The multicast path has an opt-in loopback test:
  `NB_TEST_RTPS_MULTICAST=1 ctest -C Debug -R rtps`.
- Cyclone SPDP/SEDP interop is opt-in:
  `NB_TEST_RTPS_CYCLONE=1 NB_TEST_RTPS_ADVERTISE_HOST=<local-ip>
  ctest -C Debug -R rtps_cyclone`. Without the environment variable the test
  skips successfully. The test checks participant visibility, nb publication
  visibility through Cyclone's built-in publication reader, and end-to-end
  `dds::Message` data delivery to a Cyclone reader.
- `e2e.dds_remote` covers the separate Cyclone-backed DDS provider: the parent
  publishes through `dds://61` with the local fast path disabled, and a spawned
  Cyclone reader must receive the encoded event sample.

Planned:

- Cross-implementation: Cyclone DDS SPDP and nb-to-Cyclone SEDP publication
  visibility are verified; Fast DDS and explicit Cyclone-to-nb SEDP endpoint
  assertions remain.
- Scale: 100/500/1000 simulated participants, measuring packets per second,
  memory, and discovery latency.
- Fuzzing: `ParameterList`, submessage headers, and `DATA` inline QoS.

## 10. Milestones

| Milestone | Deliverable |
| --- | --- |
| M1 (done) | RTPS wire codec and unit tests |
| M2 (done) | SPDP participant runtime: sockets, announce timer, lease expiry, participant table |
| M3 (done) | SEDP: reliable built-in writers, heartbeat/acknack, endpoint callbacks |
| M4 | Public discovery API and provider seam; mesh broadcast becomes one provider |
| M5 | Scale to about 1000 participants: unicast peer scoping, table caps, load tests |
| M6 (partial) | Cyclone DDS SPDP/SEDP visibility and best-effort `dds::Message` DATA verified; CI job pending |
| M7 | Standalone `libnb_discovery` target, examples, packaging |

M2 and M3 are the first point at which two framework processes can discover
each other over RTPS without changing the data plane.

## 11. Open decisions

- Vendor id: use the placeholder `0x01,0x9a` for now; replace it with an
  OMG-assigned value before claiming conformance.
- ROS 2 naming, node graph visibility, and `ros_discovery_info` are deferred.
- A general DDS/ROS 2 data-plane gateway and arbitrary IDL types are out of
  scope; the current RTPS data path is fixed to `dds::Message`.
- Scale target is about 1000 participants. The scoping model (peer groups,
  superpeer/relay, or discovery server) is not chosen yet.
