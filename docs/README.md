# Network Bridge Documentation

- [SDK API](api/sdk-api.md): object model, lifetime, threading and ownership.
- [Architecture](architecture.md): endpoint / message / QoS / mesh / provider
  layering.
- [Discovery](discovery.md): RTPS SPDP/SEDP subset, wire format and roadmap.
- [Discovery plan](discovery-todo.md): milestone checklist and resume notes.
- [Configuration](api/config.md): JSON fields, defaults and validation.
- [Error codes](api/error-codes.md): stable values, names, descriptions and
  retry semantics.
- [Examples](examples.md): runnable programs indexed by scenario.
- [Testing](testing.md): the unified unit / e2e / perf / stability / fuzz gates.
- [Low latency and spikes](performance.md): latency methodology, spike
  attribution experiments, CPU pinning and inline dispatch results.

Editor validation for configuration files uses
[`config.schema.json`](config.schema.json).

Design rationale and layering trade-offs are summarised in the repository
[README](../README.md).
