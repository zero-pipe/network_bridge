# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Apache-2.0 license, NOTICE, CONTRIBUTING, SECURITY and CODE_OF_CONDUCT.
- GitHub Actions CI for Linux (GCC, Release) and Windows (MSVC, Debug).
- `.clang-format`, `.editorconfig` and `.gitattributes` for consistent style and
  line endings.
- Canonical umbrella header `<nb/nb.h>`; `<nb.h>` remains a forwarding header.
- `NB_DEPRECATED` macro and a documented deprecation policy.
- `src/base/` for dependency-free primitives (alloc, buf, poolset, slotpool,
  crc32), matching the `nb_base` library boundary.

### Changed

- **Breaking:** the CMake library target is now `nb` (was `rpc`). The alias
  `rpc` and the imported name `nb::nb` remain available during the transition.
- **Breaking:** CMake options use the `NB_` prefix (`NB_BUILD_TESTS`,
  `NB_BUILD_TOOLS`, `NB_BUILD_EXAMPLES`, `NB_BUILD_GTESTS`,
  `NB_BUILD_BENCHMARKS`, `NB_BUILD_FUZZERS`). Legacy names still work but warn.
- **Breaking:** the sanitizer option is `NB_SANITIZE` (was
  `ZERO_RPC_SANITIZE`); the old name is still accepted with a warning.
- **Breaking:** enum constants now follow `NB_<TYPE>_<VALUE>`:
  - `NB_MODE_*` -> `NB_NODE_MODE_*`
  - `NB_INTRA_*` -> `NB_INTRA_PROCESS_*`
  - `NB_TRANSPORT_*` -> `NB_TRANSPORT_KIND_*`
  - `NB_STREAM_*` -> `NB_STREAM_FLAG_*`
  - `NB_METHOD_*` -> `NB_METHOD_KIND_*`
  - `NB_SERVICE_EXEC_*` -> `NB_SERVICE_EXEC_MODE_*`
  - `NB_STUB_*` -> `NB_STUB_KIND_*`
  - `NB_METRIC_COUNTER/GAUGE/HISTOGRAM` -> `NB_METRIC_TYPE_*`

  Old spellings remain as macros and can be hidden with
  `NB_NO_DEPRECATED_API`.
- Public function signatures use the `nb_`-prefixed callback typedefs
  (`nb_response_fn`, `nb_handler_fn`, ...). The flat typedefs remain for
  compatibility.
- Default node name and log tag changed from `"rpc"` to `"nb"`; default POSIX
  shared-memory names use the `nb-shm-` prefix instead of `zrpc-shm-`.
- Internal include guards are consistently `NB_*_H`.

### Fixed

- README linked a non-existent `docs/BENCH-STATUS.md`; it now points at
  `docs/performance.md`.

## [0.2.0] - 2026-01-01

Initial public snapshot of the heterogeneous network framework: node/service/
proxy abstractions, UDP/TCP/SHM/Cyclone DDS transports, RPC, pub/sub,
streaming, routing, discovery, QoS, metrics and the unified quality gate.

[Unreleased]: https://github.com/zero-pipe/network_bridge/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/zero-pipe/network_bridge/releases/tag/v0.2.0
