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
| Public API | Frozen: the existing participant, endpoint and callback signatures stay; new capability must fit behind them |
| Framework discovery | DDS discovery is self-contained (SPDP/SEDP inside the DDS stack). The framework keeps its own broadcast; there is no RTPS-to-registry mapping (M4 dropped) |
| Serialization | Move the CDR layer onto the vendored Micro-CDR 2.0.2 instead of the hand-written `nb_cdr_*` byte packing |
| RPC | Service discovery and the TCP RPC stay on the nb broadcast; modelling RPC as DDS topics is possible later but is not part of this plan |

## Status

- M0 design: done (`docs/discovery.md`)
- M1 wire codec: done
- M2 SPDP runtime: done
- M3 SEDP: done (runtime, reliable exchange, endpoint callbacks and tests)
- M4 integration: dropped by decision (self-contained DDS discovery)
- M5 scale: pending
- M6 Cyclone interop: partial (SPDP/SEDP and bidirectional `dds::Message`
  delivery verified nb-to-nb and against Cyclone on Windows and Linux, in
  multicast and unicast-only modes; only the automated CI job is pending)
- M7 packaging: pending
- M8 serialization on Micro-CDR: done (vendored subset, no submodule)
- M9 `DATA_FRAG`: done (application samples up to 64 KiB both ways, including
  Cyclone interop in both directions; SEDP stays inline by construction)
- M10 generic type support: done (raw CDR body delivery keyed by `type_name`;
  no registry and no new public API)
- M11 QoS semantics: partial (`TRANSIENT_LOCAL` late joiners, `KEEP_ALL`,
  partition lists and wildcards done; lifespan, deadline, ownership and
  resource limits open)

## Priority order

The DDS stack is the deliverable; framework service discovery is not part of
it. Work is ordered so each step removes a real interop limit.

| Order | Track | Why |
| --- | --- | --- |
| P0 | Cyclone custom-type interop | Prove the generic-type path against a second implementation, not only nb-to-nb |
| P1 | M11 remainder | Lifespan, deadline, ownership, resource limits |
| P2 | M5: scale and scoping | Unicast peer scoping, discovery server, 1000-participant measurements |
| P3 | M6/M7: CI and second implementation | Fast DDS interop and an automated Linux job |

## M1: wire codec (done)

- [x] GUID, entity id, locator, sequence number types
- [x] Little-endian CDR writer/reader with bounds checks
- [x] ParameterList writer, iterator, typed getters
- [x] RTPS header and submessage framing
- [x] DATA, HEARTBEAT, ACKNACK, GAP, INFO_TS, INFO_DST
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

## M4: dropped

RTPS/`dds::Message` endpoints are not mapped into the framework registry and no
discovery-provider selection is added. DDS already discovers itself through
SPDP/SEDP, so a registry mapping would only couple the two layers without
adding DDS capability. Framework service discovery keeps using the nb
broadcast; the TCP RPC keeps using that path as well.

Nothing in the RTPS code depends on this decision: the existing endpoint
callback (`nb_rtps_participant_set_endpoint_callback`) is enough to build a
thin registry adapter later if the framework ever needs one, without any
protocol or API change.

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
- [x] Rate-limit ACKNACK/HEARTBEAT traffic, bound history by bytes, and define
      the reader-window capacity policy
- [x] Verify reliable `dds::Message` with Cyclone (RELIABLE QoS on both sides,
      Windows, 5 consecutive runs)
- [x] Explicit unicast peer plus fixed locator mode for the Cyclone test
      (`NB_TEST_RTPS_UNICAST_ONLY=1`)
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
`e2e.rtps_participant` passed 10 repeated runs.

The earlier Windows Cyclone failure was not multicast: the nb participant
advertised `127.0.0.1` because `bind_host` was wildcard and no advertise host
was set, and Cyclone binds its sockets to a real interface, so every write to
the loopback locator failed with `ddsi_udp_conn_write ... retcode -3`. With
`NB_TEST_RTPS_ADVERTISE_HOST=192.168.1.4` the interop test passes, and with
RELIABLE QoS on both sides it passes 5/5 repeated runs. `AllowMulticast` is not
recognised by Cyclone 0.10.5, so the unicast-only mode relies on `<Peers>` plus
a fixed participant index instead.

## M8: serialization on Micro-CDR (P0)

- [x] Vendor `3rdparty/micro-cdr` as plain files (headers, sources, a minimal
      `CMakeLists.txt` and `LICENSE`; no submodule, no upstream tests, examples,
      CI, packaging or docs) and link `microcdr` into the RTPS target
- [x] Re-implement `rtps_cdr.{c,h}` as a thin wrapper over `ucdrBuffer` without
      changing the `nb_cdr_*` signatures, so codec call sites stay untouched
- [x] Initialise buffers with an explicit little-endian origin
      (`ucdr_init_buffer_origin_offset_endian`). Version 2.0.2 has no
      `ucdr_offset`: report the written length as
      `ucdr_buffer_size() - ucdr_buffer_remaining()`
- [x] Map `nb_cdr_*_align` onto `ucdr_buffer_alignment` + `ucdr_align_to`,
      keeping the alignment origin at the buffer passed to init (the byte after
      the 4-byte encapsulation header for the message body, the value start for
      a ParameterList value). Micro-CDR only advances over implicit padding, so
      the wrapper zero-fills it first: encoded frames stay deterministic and
      never leak uninitialised stack bytes
- [x] Keep PL_CDR semantics: a parameter value is read as a view whose origin
      is the parameter-list start (`ucdr_init_buffer_origin`), which is what
      keeps SPDP/SEDP byte-identical to today
- [x] Golden-vector test `unit.rtps_cdr` pins the byte layout (u8/u16/u32/u64,
      string, octets, alignment padding zero-filled, zero-copy `get_bytes`,
      bounds failures) using a poisoned buffer
- [x] Keep Cyclone interop green in reliable multicast and unicast-only modes
- [ ] Optional: replace the `dds::Message` octet sequence with
      `ucdr_serialize_sequence_uint8_t` / `ucdr_deserialize_sequence_uint8_t`
      (same wire bytes today: u32 length + octets)
- [ ] Optional: rebuild the SPDP/SEDP value codecs (GUID, locator, string,
      reliability, history, partition, user data) on typed Micro-CDR calls.
      They already serialise through the Micro-CDR backed `nb_cdr_*` layer, so
      this is a readability cleanup, not a wire change
- [ ] Optional: centralise the encapsulation header (CDR_LE `00 01`,
      PL_CDR_LE `00 03`) in one helper instead of open-coding the four bytes at
      each site

## M9: DATA_FRAG (P1)

- [x] Fragment outgoing samples larger than the MTU; keep one sequence number
      per sample. `nb_rtps_participant_publish()` now accepts up to
      `NB_RTPS_SAMPLE_MAX` (64 KiB) and splits anything above
      `NB_RTPS_DATA_PAYLOAD_MAX` (1200 bytes) into `DATA_FRAG` submessages of
      `NB_RTPS_FRAGMENT_SIZE` (1024 bytes), one fragment per datagram
- [x] Reassemble incoming fragments with a bounded buffer, a timeout and
      duplicate handling; deliver only complete samples to the callback.
      Four concurrent samples, a 256 KiB byte budget, and incomplete samples
      expire after 2 s or on budget pressure (oldest first)
- [x] HEARTBEAT/ACKNACK interaction: a partly received sample stays missing;
      retransmits replay the whole sample, which re-fragments through the same
      send path. Per-fragment `NACK_FRAG` is still not implemented
- [x] Tests: nb-to-nb 4 KiB and 60 KiB payloads over RELIABLE QoS, plus a
      `DATA_FRAG` encode/decode wire test with malformed-input cases
- [x] SEDP over fragments: not needed under the frozen public limits. A SEDP
      sample with every `nb_rtps_endpoint_info_t` buffer full (128-byte topic
      and type names, 64-byte partition, 256 bytes of user data, four unicast
      and four multicast locators) serialises to 900 bytes, so it always fits
      in one DATA submessage. `test_sedp_payload_bound` keeps that invariant.
      The receive side still reassembles SEDP fragments and routes them by
      writer id, so a peer with larger limits interoperates
- [x] Cyclone sending `DATA_FRAG` to nb and nb sending `DATA_FRAG` to a Cyclone
      reader. `dds::Message` carries a `sequence<octet>`, so it is not bounded
      after all; the interop test now exchanges an 8 KiB payload in both
      directions. Two bugs surfaced and were fixed: a submessage may carry
      several fragments (Cyclone packs them) and every covered fragment must be
      marked, and the receive buffer was sized to the 1400-byte send budget, so
      larger peer datagrams were truncated

## M10: generic type support (P2)

- [x] No registry and no new public API. `nb_rtps_participant_publish()` already
      takes opaque bytes, and the receive path now picks the interpretation from
      the matched local subscription's `type_name`: "dds::Message" is parsed as
      before, every other type is delivered as its raw CDR body (the sample
      minus the four-byte encapsulation header). The application owns the codec,
      which is exactly what Micro-CDR generated code provides
- [x] `NB_RTPS_CDR_HEADER_SIZE` moved to `rtps_data.h` so both paths share one
      definition
- [x] End-to-end test with a custom `test::Blob` type: a 64-byte body inline and
      a 3000-byte body through `DATA_FRAG`, both verified by payload hash
- [ ] Codec coverage: nested structs, fixed arrays, sequences, strings, enums,
      optional members (Micro-CDR generated code). Nothing in the runtime needs
      to change for these; the work is an example codec plus tests
- [ ] Verify Cyclone matches a custom type by type name and exchanges samples
      in both directions. Needs a Cyclone-side type larger than `dds::Message`,
      which also unblocks the `DATA_FRAG` interop item in M9

## M11: QoS semantics (P3)

- [x] `TRANSIENT_LOCAL` durability: a publication with that durability (or
      higher) keeps its history and replays it when a matching reader is
      discovered later, including best-effort pairs. `VOLATILE` publications
      keep nothing for a reader that was not matched, which the test also
      asserts. Known deviation: a reliable `VOLATILE` writer still serves its
      unacknowledged history through ACKNACK, because a late-joining reader
      starts with an empty window; the wire result is interoperable, the policy
      is just more generous than DDS requires
- [x] `KEEP_ALL` history: capacity is decoupled from the `KEEP_LAST` depth and
      uses the 1024-sample ceiling, with the 256 KiB per-writer byte budget as
      the real bound. Test: a KEEP_ALL publication with depth 2 still replays
      five retained samples to a late joiner
- [x] Partition matching: names are comma-separated lists and `*` is a
      wildcard, so "alpha,team-*" matches "team-blue" but not "other". An
      endpoint matches when any name on either side matches any name on the
      other; the default partition is the empty name. Not yet verified against
      Cyclone with an actual partition mismatch
- [ ] Lifespan and deadline (optional, after the above)
- [x] Document the implemented QoS matrix (see below)

Implemented QoS matrix:

| Policy | Support |
| --- | --- |
| Reliability | `BEST_EFFORT` and `RELIABLE`; reliable pairs use writer history, HEARTBEAT/ACKNACK, per-reader windows and GAP |
| Durability | `VOLATILE` and `TRANSIENT_LOCAL` (replay to late joiners). `TRANSIENT`/`PERSISTENT` are accepted and behave like `TRANSIENT_LOCAL`; nothing is persisted across process restarts |
| History | `KEEP_LAST` with a depth, `KEEP_ALL` up to 1024 samples and 256 KiB per writer |
| Partition | comma-separated name lists with `*` wildcards |
| Lifespan, deadline, ownership, resource limits | not implemented |

## Follow-ups after bidirectional DATA

- [x] Reliable application `DATA`: writer history, HEARTBEAT/ACKNACK, GAP
- [ ] Verify reliable `dds::Message` against Cyclone on the Linux runner
      (Windows done; run `tools/verify-rtps-interop.sh` on 192.168.54.14)
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
- A loopback `advertise_host` breaks same-host interop with peers that bind to
  a real interface. Defaulting to `127.0.0.1` on a wildcard bind is convenient
  for loopback tests but must not be used against Cyclone or across hosts.
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
- Linux interop sweep: `cd <repo> && tools/verify-rtps-interop.sh` builds,
  runs `-L "unit|rtps"`, repeats `e2e.rtps_participant` three times, and runs
  the Cyclone test in multicast and unicast-only modes. It picks the advertise
  address from the default route unless `NB_RTPS_ADVERTISE_HOST` is set.
- Do not commit `build-review/`, `artifacts/`, `Testing/` or other build output.
- Commit identity: `OldChen <825988516@qq.com>` (repository-local config).
- Remote Linux verification host: `pengze@192.168.54.14`, repository at
  `/home/pengze/chenqw/nb`. It is usually the better place to run the Cyclone
  interop sweep. Password is intentionally not stored in the repository; keep
  it in a local, git-ignored file (for example `.local/linux-env.txt`). `ssh`,
  `git` and `cmake`/Ninja are installed; there is no key-based login yet, so
  from Windows either run the commands by hand or install `plink`.
- Remote run: `cd /home/pengze/chenqw/nb && git pull &&
  tools/verify-rtps-interop.sh`.
- The host accepts password SSH only, but OpenSSH 9.5 on Windows drives it
  non-interactively with an `SSH_ASKPASS` helper and
  `SSH_ASKPASS_REQUIRE=force`, so an agent can sync a tree and run the script
  without a terminal.
- Next task: M8, move the CDR layer onto Micro-CDR 2.0.2 (P0), then M9
  `DATA_FRAG`, then M10 generic types. (M8 is done; M9 is next.) M4
  (RTPS-to-registry mapping) is dropped: DDS discovery is self-contained and
  framework service discovery keeps using the nb broadcast.

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
- 2026-10-31: M8 done. The CDR layer (`rtps_cdr.{c,h}`) now runs on vendored
  Micro-CDR 2.0.2 behind the unchanged `nb_cdr_*` API, with zero-filled
  alignment padding, zero-copy `nb_cdr_get_bytes`, and a pinned byte-layout
  test (`unit.rtps_cdr`). Cyclone interop stays green in reliable multicast and
  unicast-only modes, so the wire format is unchanged.
- 2026-10-31: Micro-CDR is vendored as plain in-tree files (headers, sources,
  a trimmed `CMakeLists.txt` and the Apache-2.0 `LICENSE`); the Git submodule
  and `.gitmodules` entry are gone, and the upstream tests, examples, CI and
  packaging directories were removed.
- 2026-10-31: Linux verification passed on `192.168.54.14` from a clean
  `git archive` of HEAD: fresh Ninja build, 18 unit/RTPS tests,
  `e2e.rtps_participant` repeated three times, and Cyclone interop in both
  multicast and unicast-only modes.
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
- 2026-10-31: Reliable data-plane hardening. Reader ACKNACK suppression
  (20 ms), writer resend pacing (10 ms) and a 64-sample per-ACKNACK cap,
  256 KiB per-writer history byte budget, and a reader window table capped at
  `max_remote_endpoints` (4096) with a documented degraded mode instead of
  window recycling. Added `unit`/`e2e` tests and the Cyclone unicast-only mode
  (fixed ports plus explicit `dds_create_domain()` peers). Cyclone interop now
  runs with RELIABLE QoS on both sides and passes on Windows in both modes;
  `tools/verify-rtps-interop.sh` runs the same sweep on Linux.
- 2026-11-01: Dropped M4 (RTPS endpoints to registry service records, discovery
  provider selection). DDS discovery is self-contained; framework service
  discovery and the TCP RPC stay on the nb broadcast. Re-ordered the plan
  around the DDS stack: M8 serialization on Micro-CDR 2.0.2 (P0), M9
  `DATA_FRAG` (P1), M10 generic types (P2), M11 QoS (P3), then scale and CI.
- 2026-11-01: M9 `DATA_FRAG` for application samples. `publish()` accepts up to
  64 KiB and fragments anything above 1200 bytes into 1024-byte fragments, one
  per datagram; the receiver reassembles with a four-slot, 256 KiB budget and a
  2 s timeout and only then delivers the sample (or routes it to SEDP). A
  partly received sample stays missing for ACKNACK, so retransmits replay the
  whole sample. Tests: wire encode/decode plus nb-to-nb 4 KiB and 60 KiB over
  RELIABLE QoS.
- 2026-11-01: Linux re-verified after M9 on `192.168.54.14`: fresh build,
  18 unit/RTPS tests, `e2e.rtps_participant` (including the new fragmented-data
  case) three times, and Cyclone interop in multicast and unicast-only modes.
- 2026-11-01: M10 generic types without a registry. The receive path picks the
  interpretation from the matched subscription's `type_name`: `dds::Message` is
  decoded as before, anything else reaches the callback as its raw CDR body
  (sample minus the encapsulation header). `publish()` already accepted opaque
  bytes, so no public API changed. Tests: `test::Blob` with a 64-byte inline
  body and a 3000-byte body over `DATA_FRAG`.
- 2026-11-01: M11 `TRANSIENT_LOCAL` late joiners. A publication with that
  durability keeps a history (best effort included) and replays it to a reader
  that is discovered afterwards, plus a heartbeat for reliable pairs. `VOLATILE`
  publications keep nothing for unmatched readers. Tests: the same late-joiner
  scenario with `TRANSIENT_LOCAL` (two samples delivered) and with `VOLATILE`
  (none delivered).
- 2026-11-01: M11 partition matching. Partition strings are treated as
  comma-separated name lists with `*` wildcards instead of exact string
  equality, which matches the DDS rule and the way Cyclone and Fast DDS parse
  partitions. Test: a publication in "alpha,team-*" delivers to a subscriber in
  "team-blue" and not to one in "other".
- 2026-11-01: M11 `KEEP_ALL` bounded by the byte budget instead of the
  `KEEP_LAST` depth, and the implemented QoS matrix documented. Linux
  re-verified after the M11 batch on `192.168.54.14`: fresh build, unit/RTPS
  tests, `e2e.rtps_participant` three times, and Cyclone interop in multicast
  and unicast-only modes.
