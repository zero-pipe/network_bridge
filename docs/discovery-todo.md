# Discovery Work Plan

Durable checklist for the RTPS discovery work. Update this file when a task
changes state so a later session can resume without reading the whole history.

## Decisions

| Topic | Decision |
| --- | --- |
| Architecture | Route A: implement RTPS discovery ourselves; no DDS dependency in the discovery path |
| Data plane | Unchanged: framework UDP/TCP/SHM transports; RTPS is discovery-only |
| Vendor id | Placeholder `0x01,0x9a` until an OMG-assigned value exists |
| First interop target | Bundled Cyclone DDS; ROS 2 is out of scope for now |
| Scale target | About 1000 participants, which requires unicast peers/scoping; plain multicast is not enough |
| Public API | Deferred until the SPDP/SEDP runtime is stable (M4) |

## Status

- M0 design: done (`docs/discovery.md`)
- M1 wire codec: done
- M2 SPDP runtime: in progress
- M3 SEDP: pending
- M4 integration: pending
- M5 scale: pending
- M6 Cyclone interop: pending
- M7 packaging: pending

## M1: wire codec (done)

- [x] GUID, entity id, locator, sequence number types
- [x] Little-endian CDR writer/reader with bounds checks
- [x] ParameterList writer, iterator, typed getters
- [x] RTPS header and submessage framing
- [x] DATA, HEARTBEAT, ACKNACK, INFO_TS, INFO_DST
- [x] SPDP announcement payload build/parse
- [x] Unit tests for round trips and truncated input

## M2: SPDP runtime

- [ ] Participant config and GUID prefix generation
- [ ] UDP sockets: multicast receive, unicast receive, send
- [ ] Multicast join, loopback and TTL settings
- [ ] Discovery thread with poller timeout
- [ ] Announce timer with jitter; immediate announce on start
- [ ] SPDP send to multicast and configured unicast peers
- [ ] Receive path: header, submessage walk, DATA decode, SPDP parse
- [ ] Ignore self by GUID prefix; update remote participant table
- [ ] Lease expiry sweep and add/update/remove callbacks
- [ ] Bounded participant table with eviction policy
- [ ] Loopback test using unicast peers (no multicast dependency)
- [ ] Optional multicast loopback test
- [ ] Start/stop without leaks; repeated create/destroy test

## M3: SEDP

- [ ] Endpoint payload codec: endpoint GUID, topic, type, reliability,
      durability, history, partition, locators, user data
- [ ] Reliable writer history (bounded, keep last N)
- [ ] HEARTBEAT timer and ACKNACK retransmit
- [ ] Built-in publications writer/reader (`0x000003c2` / `0x000003c7`)
- [ ] Built-in subscriptions writer/reader (`0x000004c2` / `0x000004c7`)
- [ ] Endpoint table and add/update/remove callbacks
- [ ] Dispose on endpoint removal
- [ ] Map endpoints to framework service records
- [ ] Unit and loopback tests

## M4: framework integration

- [ ] Registry backend adapter for the RTPS participant
- [ ] Config field to select the discovery provider (`mesh`, `rtps`, `static`)
- [ ] Keep the existing mesh broadcast behavior as one provider
- [ ] End-to-end test: two nodes discover services over RTPS
- [ ] Shutdown ordering and no threads left behind

## M5: scale to 1000

- [ ] Unicast peer mode with configurable peer lists
- [ ] Decide scoping model: peer groups, superpeer/relay, or discovery server
- [ ] Table caps and deterministic eviction
- [ ] Simulated 100/500/1000 participants
- [ ] Measure packets/s, memory, discovery latency and CPU
- [ ] Document the supported operating envelope

## M6: Cyclone DDS interop

- [ ] Docker or CI job with the bundled Cyclone DDS version
- [ ] Verify Cyclone sees our participant and SPDP endpoint
- [ ] Verify we see Cyclone participants and SEDP endpoints
- [ ] Vendor id note for `0x01,0x10` quirks
- [ ] Interop test in `tests/` or `tools/`

## M7: packaging

- [ ] Standalone `libnb_discovery` target
- [ ] Public `include/nb/discovery.h` and provider registration
- [ ] Examples: two processes discovering each other
- [ ] API reference and architecture update

## Known risks

- SPDP is `O(N^2)` multicast; SEDP adds pairwise endpoint state. 1000
  participants need peers/scoping or a server; do not assume plain multicast.
- Windows multicast binding and firewall behavior differ from Linux.
- Cyclone DDS vendor-specific behavior must be verified, not assumed.
- DDS/ROS 2 data exchange is not provided by discovery alone; a gateway or an
  RTPS data plane would be required.
- `ros_discovery_info` and ROS 2 node graph visibility are explicitly deferred.

## Resume notes

- Branch: `master`; build directory: `build-review` (Visual Studio 16 2019).
- Build: `cmake --build build-review --config Debug`
- Unit tests: `cd build-review; ctest -C Debug -L unit`
- Single RTPS test: `ctest -C Debug -R rtps`
- Do not commit `build-review/`, `artifacts/`, `Testing/` or other build output.
- Commit identity: `OldChen <825988516@qq.com>` (repository-local config).
- Next task: implement `src/discovery/rtps/rtps_participant.c` (M2), starting
  with the unicast-peer loopback test so CI does not depend on multicast.

## Change log

- 2026-10-31: M1 wire codec and SPDP payload codec completed; plan created.
