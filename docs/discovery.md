# RTPS Discovery

This document describes the standards-based discovery layer used by the
framework. It implements the RTPS discovery protocols (SPDP and SEDP) without
taking a dependency on a DDS implementation. The data plane remains the
framework's own UDP/TCP/SHM transports.

Status: the wire codec and the SPDP announcement payload are implemented and
covered by unit tests. The participant runtime (sockets, timers, lease table)
and the SEDP endpoint exchange are the next milestones.

## 1. Scope

In scope:

- RTPS 2.4 messages over IPv4 UDP.
- Little-endian CDR and RTPS `ParameterList` encoding.
- SPDP: periodic participant announcement on the well-known multicast group.
- SEDP: reliable built-in endpoint exchange for publications and subscriptions.
- Lease-based participant expiry.
- Mapping between RTPS endpoints and framework service records.

Out of scope (for now):

- The RTPS data plane (`DATA` carrying application payloads, `DATA_FRAG`,
  batching). Business traffic never uses RTPS.
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

### 3.4 Timers

| Timer | Default | Notes |
| --- | --- | --- |
| SPDP announcement | 3 s | jittered per participant |
| Participant lease | 20 s | remote entry expires after this |
| SEDP heartbeat | 1 s | per reliable writer while unacknowledged |
| Lease sweep | 1 s | runtime tick |

Defaults are configuration values, not constants in the protocol.

## 4. Framework mapping

### 4.1 Nodes and services

One framework node maps to one RTPS participant. The participant GUID prefix is
12 bytes derived from a vendor prefix, process identity, and a per-process
participant counter.

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

Encoding rules:

- The RTPS header is 20 bytes: `RTPS`, version, vendor id, GUID prefix.
- Each submessage starts with a 4-byte header (`id`, `flags`,
  `octetsToNextHeader`) and is padded to a 4-byte boundary.
- A `ParameterList` entry is `pid` (u16), `length` (u16), value, then zero
  padding to 4 bytes. The list ends with `PID_SENTINEL`.
- Parameter values are CDR streams whose alignment starts at the first value
  byte. This matches how implementations align `ParameterList` values.
- `DATA` uses `octetsToInlineQos = 16`; the payload starts after the inline QoS
  parameter list (or immediately after the fixed fields when the inline QoS
  flag is clear).

The codec is fuzz-friendly: every read is bounds checked and malformed input
returns `NB_ERR_BAD_MESSAGE` without partial state.

## 6. Runtime design

The participant runtime will be added in `src/discovery/rtps/participant.c`:

- one multicast receive socket joined to the SPDP group, with address reuse;
- one unicast receive socket for SEDP and participant messages;
- one send socket, multicast TTL 1;
- a dedicated discovery thread with a monotonic-clock timer wheel;
- bounded tables for remote participants and endpoints;
- no heap allocation in the packet receive path after warm-up.

State:

| Table | Key | Contents |
| --- | --- | --- |
| participants | GUID prefix | locators, lease deadline, endpoint set, name |
| endpoints | endpoint GUID | participant, topic, type, QoS, locators, vendor record |
| writers | local writer id | sequence number, small history for retransmit |

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

- A DDS tool sees the framework participant and its SEDP endpoints.
- A framework node sees DDS participants and endpoints, including topic and
  type names.

Data-level:

- A DDS participant that matches a framework endpoint will try to send RTPS
  `DATA` to the advertised locator. The framework data plane does not speak
  RTPS, so payloads do not flow unless a gateway translates them.
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

- `unit.rtps_wire`: CDR round trips, alignment, `ParameterList` round trips,
  `DATA`/`HEARTBEAT`/`ACKNACK` framing, truncation handling.

Planned:

- SPDP loopback: two participants in one process discover each other.
- Cross-implementation: Cyclone DDS and Fast DDS in Docker; verify participant
  and endpoint visibility with packet capture and a discovery dump.
- Leases: abrupt process exit expires the participant within the lease window.
- Scale: 100/500/1000 simulated participants, measuring packets per second,
  memory, and discovery latency.
- Fuzzing: `ParameterList`, submessage headers, and `DATA` inline QoS.

## 10. Milestones

| Milestone | Deliverable |
| --- | --- |
| M1 (done) | RTPS wire codec and unit tests |
| M2 | SPDP participant runtime: sockets, announce timer, lease expiry, participant table |
| M3 | SEDP: reliable built-in writers, heartbeat/acknack, endpoint mapping |
| M4 | Public discovery API and provider seam; mesh broadcast becomes one provider |
| M5 | Standalone `libnb_discovery` target, examples, interop and scale tests |

M2 and M3 are the first point at which two framework processes can discover
each other over RTPS without changing the data plane.

## 11. Open decisions

- Register an OMG vendor id, or keep a private placeholder and document it.
- Native-only deployment, or ROS 2 naming/interop mode from the start.
- Whether a data-plane gateway is in scope for DDS/ROS 2 payload exchange.
- Target participant count and whether unicast peers or a discovery server must
  ship with the first release.
