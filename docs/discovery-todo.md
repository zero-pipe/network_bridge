# Discovery Work Plan

Durable checklist for the RTPS discovery work. Update this file when a task
changes state so a later session can resume without reading the whole history.

## Decisions

| Topic | Decision |
| --- | --- |
| Architecture | Route A: implement RTPS discovery ourselves; no DDS dependency in the discovery path |
| Data plane | RTPS adds best-effort and reliable `dds::Message` interoperability paths; native traffic uses UDP/TCP/SHM, and the separate Cyclone-backed `dds` provider remains the general DDS transport |
| Vendor id | Placeholder `0x01,0x9a` until an OMG-assigned value exists |
| First interop target | Bundled Cyclone DDS; ROS 2 is out of scope for now |
| Scale target | About 1000 participants, which requires unicast peers/scoping; plain multicast is not enough |
| Public API | Deferred until the SPDP/SEDP runtime is stable (M4) |

## Status

- M0 design: done (`docs/discovery.md`)
- M1 wire codec: done
- M2 SPDP runtime: done
- M3 SEDP: done (runtime, reliable exchange, endpoint callbacks and tests)
- M4 integration: pending
- M5 scale: pending
- M6 Cyclone interop: partial (SPDP/SEDP and bidirectional `dds::Message`
  delivery verified on Linux; reliable application DATA verified nb-to-nb on
  Windows; Cyclone reliable interop and the CI job pending)
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

- [x] Endpoint payload codec: endpoint GUID, topic, type, reliability,
      durability, history, partition, locators, user data
- [x] Reliable writer history (bounded, keep last N)
- [x] ACKNACK sample retransmit
- [x] HEARTBEAT timer and runtime retransmit wiring
- [x] Built-in publications writer/reader (`0x000003c2` / `0x000003c7`)
- [x] Built-in subscriptions writer/reader (`0x000004c2` / `0x000004c7`)
- [x] Endpoint table and add/update/remove callbacks
- [x] Dispose on endpoint removal
- [ ] Map endpoints to framework service records (M4)
- [x] Unit and loopback tests

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
- [x] Verify we see Cyclone SEDP endpoints (the Cyclone writer is discovered
      and matched by the nb subscription)
- [x] Verify Cyclone sees our SEDP publication endpoint
- [x] Standard CDR_LE `dds::Message` (`sequence<octet> payload`) DATA encoding
- [x] Verify a Cyclone reader receives and deserializes nb RTPS DATA on Linux
- [x] Verify nb receives and deserializes Cyclone RTPS DATA on Linux
- [x] Verify nb-to-nb `dds::Message` DATA through the self-developed RTPS stack
- [x] Verify an independent Cyclone reader receives data from the separate
      Cyclone-backed `dds` provider (`e2e.dds_remote`)
- [x] Reliable application `DATA`: writer history, HEARTBEAT/ACKNACK, GAP
- [ ] `DATA_FRAG`
- [x] Vendor id note for `0x01,0x10` quirks
- [x] Interop test in `tests/` or `tools/`

Manual verification: Cyclone DDS 0.10.5 on Windows and the bundled 0.11.0 on
Linux. The Linux capture used `ens1` (`192.168.54.14`) and `udp port 7400`;
Cyclone and nb both used `PL_CDR_LE`, and the locator address was in the last
four bytes of the 16-byte address field. The Cyclone SEDP test creates an nb
publication and checks that Cyclone's `DCPSPublication` reader sees `nb/sedp`
and `nb::Sedp`. The final Linux capture showed 55 RTPS packets across three
passing runs, instead of the earlier ACKNACK/HEARTBEAT storm.

The `dds::Message` data-plane verification ran on Linux (`192.168.54.14`) on
2026-10-31. `e2e.rtps_cyclone` passed 10 consecutive runs and the full `-L rtps`
set passed 5/5 with `NB_TEST_RTPS_ADVERTISE_HOST=192.168.54.14`. Cyclone emits
CDR_LE encapsulation option bits `00 01`; the reader now ignores option bits
and accepts trailing submessage padding.

Reliable application `DATA` was verified locally on Windows on 2026-10-31:
`-L rtps` passed with the new late-join replay and injected-GAP cases, and
`e2e.rtps_participant` passed 10 repeated runs. Cyclone could not be exercised
in that environment (`e2e.rtps_cyclone` fails the same way on the parent
commit because the Cyclone participant never discovers the nb participant), so
reliable Cyclone interop still needs the Linux runner.

## Follow-ups after bidirectional DATA

- [x] Reliable application `DATA`: writer history, HEARTBEAT/ACKNACK, GAP
- [ ] Verify reliable `dds::Message` against Cyclone on the Linux runner
- [ ] `DATA_FRAG` for samples larger than the MTU
- [ ] Generic IDL/topic-type support; the RTPS path is fixed to `dds::Message`
- [ ] Per-endpoint subscription callbacks; the current callback is global and
      dispatches at most `NB_RTPS_MAX_DATA_TARGETS` matching local readers
- [ ] Assert Cyclone endpoint metadata explicitly (topic, type, QoS, locators)
- [ ] Automate the Linux Cyclone interop run in CI
- [ ] Map RTPS endpoints into registry service records (M4)

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
- The RTPS data plane covers best-effort and reliable `dds::Message`, without
  fragmentation. Arbitrary IDL and ROS 2 topic/service exchange still need a
  gateway or a generic RTPS data plane.
- `ros_discovery_info` and ROS 2 node graph visibility are explicitly deferred.

## Resume notes

- Branch: `master`; build directory: `build-review` (Visual Studio 16 2019).
- Build: `cmake --build build-review --config Debug`
- Unit tests: `cd build-review; ctest -C Debug -L unit`
- Single RTPS test: `ctest -C Debug -R rtps`
- DDS cross-process test: `ctest -C Debug -R dds_remote`
- Linux build: `build-linux` (Ninja); Cyclone test needs
  `NB_TEST_RTPS_CYCLONE=1` and usually
  `NB_TEST_RTPS_ADVERTISE_HOST=<local-ip>`. Run `ctest` from inside
  `build-linux`; the installed CTest does not support `--test-dir`.
- Do not commit `build-review/`, `artifacts/`, `Testing/` or other build output.
- Commit identity: `OldChen <825988516@qq.com>` (repository-local config).
- Next task: M4 framework integration: map RTPS endpoints to registry service
  records and add the discovery provider selection seam. The bidirectional
  `dds::Message` path is functional in both best-effort and reliable modes;
  `DATA_FRAG` and generic DATA remain separate follow-ups.

## Change log

- 2026-10-31: M1 wire codec and SPDP payload codec completed; plan created.
- 2026-10-31: M2 SPDP runtime, unicast-peer and multicast loopback tests
  completed.
- 2026-10-31: SPDP interop with Cyclone DDS 0.10.5 verified on Windows and
  Linux. Fixed parameter padding, `PL_CDR_LE` encapsulation, DATA flags,
  IPv4 locator layout, and the default unicast locator fallback.
- 2026-10-31: SEDP endpoint payload codec and unit tests added; reliable
  writer and runtime integration are next.
- 2026-10-31: Bounded reliable writer history and ACKNACK retransmit added;
  HEARTBEAT timer and runtime wiring remain.
- 2026-10-31: SEDP runtime completed: endpoint table, publication/subscription
  built-in writers and readers, HEARTBEAT/ACKNACK retransmit, dispose and
  callbacks. Cyclone DDS 0.10.5 sees the nb publication. Fixed 12-byte
  `PID_RELIABILITY`, repeated single 24-byte locator PIDs, MSB-first ACKNACK
  bitmaps, the ACKNACK FINAL flag, and ACK/retransmit addressing to the
  metatraffic locator.
- 2026-10-31: Added `e2e.dds_remote`, a two-process test in which an independent
  Cyclone DDS reader receives an event from the `dds` transport provider with
  `intra_process=off`. This verifies the provider data plane; self-developed
  RTPS discovery still carries no application `DATA`.
- 2026-10-31: Completed bidirectional RTPS application DATA. Added the
  subscription data callback, remote publication/local subscription matching,
  nb-to-nb delivery, and Cyclone-to-nb delivery. Fixed CDR_LE option-bit
  handling (`00 01 00 01` from Cyclone) and accepted trailing padding.
- 2026-10-31: Added reliable application DATA. Reliable publications keep a
  bounded writer history, emit HEARTBEATs, retransmit ACKNACK requests, and
  emit GAP for evicted samples; readers keep a per-(writer, reader) window,
  deduplicate by sequence number, answer HEARTBEATs, and honour GAP. Added
  `GAP` codec, `data_heartbeat_ms`/`data_history_depth` config, unit tests and
  nb-to-nb late-join/GAP end-to-end tests. SEDP now shares the reader-window
  helpers. Cyclone reliable interop is still to be run on Linux.
