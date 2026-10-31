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
- M2 SPDP runtime: done
- M3 SEDP: pending
- M4 integration: pending
- M5 scale: pending
- M6 Cyclone interop: partial (SPDP verified; SEDP waits for M3)
- M7 packaging: pending

## M1: wire codec (done)

- [x] GUID, entity id, locator, sequence number types
- [x] Little-endian CDR writer/reader with bounds checks
- [x] ParameterList writer, iterator, typed getters
- [x] RTPS header and submessage framing
- [x] DATA, HEARTBEAT, ACKNACK, INFO_TS, INFO_DST
- [x] SPDP announcement payload build/parse
- [x] Unit tests for round trips and truncated input

## M2: SPDP runtime (done)

- [x] Participant config and GUID prefix generation
- [x] UDP sockets: multicast receive, unicast receive, send
- [x] Multicast join, loopback and TTL settings
- [x] Discovery thread with poller timeout
- [x] Announce timer with jitter; immediate announce on start
- [x] SPDP send to multicast and configured unicast peers
- [x] Receive path: header, submessage walk, DATA decode, SPDP parse
- [x] Ignore self by GUID prefix; update remote participant table
- [x] Lease expiry sweep and add/update/remove callbacks
- [x] Bounded participant table with eviction policy
- [x] Loopback test using unicast peers (no multicast dependency)
- [x] Optional multicast loopback test (`NB_TEST_RTPS_MULTICAST=1`)
- [x] Start/stop without leaks; repeated create/destroy test

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
- [x] Verify Cyclone sees our participant and SPDP endpoint
- [x] Verify we see Cyclone participants (SPDP)
- [ ] Verify we see Cyclone SEDP endpoints (after M3)
- [x] Vendor id note for `0x01,0x10` quirks
- [x] Interop test in `tests/` or `tools/`

Manual verification: Cyclone DDS 0.10.5 on Windows and Linux. The Linux
capture used `ens1` (`192.168.54.14`) and `udp port 7400`; Cyclone and nb both
used `PL_CDR_LE`, and the locator address was in the last four bytes of the
16-byte address field.

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
- Linux build: `build-linux` (Ninja); Cyclone test needs
  `NB_TEST_RTPS_CYCLONE=1` and usually
  `NB_TEST_RTPS_ADVERTISE_HOST=<local-ip>`. Run `ctest` from inside
  `build-linux`; the installed CTest does not support `--test-dir`.
- Do not commit `build-review/`, `artifacts/`, `Testing/` or other build output.
- Commit identity: `OldChen <825988516@qq.com>` (repository-local config).
- Next task: implement M3 SEDP: endpoint payload codec, reliable history,
  HEARTBEAT/ACKNACK, endpoint table, and loopback tests.

## Change log

- 2026-10-31: M1 wire codec and SPDP payload codec completed; plan created.
- 2026-10-31: M2 SPDP runtime, unicast-peer and multicast loopback tests
  completed.
- 2026-10-31: SPDP interop with Cyclone DDS 0.10.5 verified on Windows and
  Linux. Fixed parameter padding, `PL_CDR_LE` encapsulation, DATA flags,
  IPv4 locator layout, and the default unicast locator fallback.
