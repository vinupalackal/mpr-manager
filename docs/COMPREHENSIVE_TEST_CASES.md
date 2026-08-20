# Multi-Plane Runtime Manager — Comprehensive Test Cases

## 1) Scope

This document defines end-to-end test cases covering all major features and functional paths in the project:

- Startup, shutdown, and signal handling
- WRP transport and message handling
- EXEC flow and command execution safety
- Multi-plane catalog lookup and ambiguity behavior
- DESCRIBE / HEALTH / PUSH / CHANGED flows
- Metadata validation and policy enforcement
- Dynamic plugin lifecycle and dispatch
- Logging, configuration file behavior, and environment overrides
- ACL compile-time gate behavior
- Error handling, determinism, and observability

## 2) Test Case Format

- **ID**: unique case identifier
- **Feature**: functional area
- **Type**: Unit / Integration / System / Negative
- **Steps**: concrete execution steps
- **Expected**: required outcome
- **Automation**: Existing / New / Manual

---

## 3) Startup / Shutdown / Runtime Control

### TC-MPRM-BOOT-001 — Startup with default config
- **Feature**: Runtime bootstrap
- **Type**: System
- **Steps**: Start service with no explicit config path.
- **Expected**: Service initializes sockets, loads available plane catalogs, enters message loop.
- **Automation**: New

### TC-MPRM-BOOT-002 — Startup with custom catalog directory
- **Feature**: CLI override
- **Type**: Integration
- **Steps**: Start with argv[1] set to custom catalog directory.
- **Expected**: Catalog load uses supplied directory; behavior matches loaded files.
- **Automation**: New

### TC-MPRM-BOOT-003 — Graceful shutdown on SIGTERM
- **Feature**: Signal handling
- **Type**: System
- **Steps**: Send SIGTERM while serving requests.
- **Expected**: Main loop exits, sockets close, plugin manager stops, process exits cleanly.
- **Automation**: New

### TC-MPRM-BOOT-004 — Graceful shutdown on SIGINT
- **Feature**: Signal handling
- **Type**: System
- **Steps**: Send SIGINT while idle and while active.
- **Expected**: Same as SIGTERM; no crash or deadlock.
- **Automation**: New

### TC-MPRM-BOOT-005 — SIGHUP plugin one-shot reload
- **Feature**: Runtime control
- **Type**: Integration
- **Steps**: Enable plugins; change plugin directory contents; send SIGHUP.
- **Expected**: One-shot plugin rescan/reload occurs and is logged.
- **Automation**: New

### TC-MPRM-BOOT-006 — Startup with missing local endpoint path
- **Feature**: Local endpoint best-effort
- **Type**: Negative
- **Steps**: Ensure local IPC path unavailable; start service.
- **Expected**: Warning logged; public path remains active.
- **Automation**: New

---

## 4) WRP Transport / Envelope Handling

### TC-MPRM-WRP-001 — Accept and process msg_type=3 request
- **Feature**: WRP request handling
- **Type**: Integration
- **Steps**: Send valid WRP REQ envelope with valid inner payload.
- **Expected**: Request dispatched, response returned with msg_type=3.
- **Automation**: New

### TC-MPRM-WRP-002 — Keepalive ack for msg_type=10
- **Feature**: Keepalive
- **Type**: Integration
- **Steps**: Send WRP ALIVE envelope.
- **Expected**: ALIVE ack sent immediately.
- **Automation**: New

### TC-MPRM-WRP-003 — Invalid/malformed outer msgpack
- **Feature**: Robust decode
- **Type**: Negative
- **Steps**: Send malformed outer envelope bytes.
- **Expected**: Request dropped safely; process remains healthy.
- **Automation**: New

### TC-MPRM-WRP-004 — Missing required request metadata fields
- **Feature**: Input validation
- **Type**: Negative
- **Steps**: Omit required envelope fields (where applicable).
- **Expected**: Reject/drop with deterministic log behavior; no crash.
- **Automation**: New

### TC-MPRM-WRP-005 — Large payload handling within limits
- **Feature**: Buffer and payload safety
- **Type**: Integration
- **Steps**: Send max-size practical payload.
- **Expected**: Proper handling or deterministic rejection.
- **Automation**: New

### TC-MPRM-WRP-006 — Concurrent requests on both public/local sockets
- **Feature**: Multi-socket loop and threading
- **Type**: System
- **Steps**: Flood both sockets with mixed requests.
- **Expected**: No starvation/deadlock; responses returned correctly.
- **Automation**: New

---

## 5) EXEC Path / Command Safety

### TC-MPRM-EXEC-001 — Known static tool execution
- **Feature**: Catalog EXEC
- **Type**: Integration
- **Steps**: Invoke valid static tool with no override.
- **Expected**: Command runs; deterministic response payload.
- **Automation**: Existing (partially via spec harness)

### TC-MPRM-EXEC-002 — Missing tool field
- **Feature**: Input validation
- **Type**: Negative
- **Steps**: Send EXEC payload without `tool`.
- **Expected**: Request rejected; warning/counter update.
- **Automation**: Existing (spec harness coverage)

### TC-MPRM-EXEC-003 — Unknown tool
- **Feature**: Catalog miss handling
- **Type**: Negative
- **Steps**: Request tool not present in any loaded catalog.
- **Expected**: Deterministic not-found response.
- **Automation**: Existing (spec harness coverage)

### TC-MPRM-EXEC-004 — Ambiguous tool across planes without `plane`
- **Feature**: Multi-plane disambiguation
- **Type**: Negative
- **Steps**: Define same tool in 2 plane catalogs; call without `plane`.
- **Expected**: Ambiguous rejection; no execution.
- **Automation**: New

### TC-MPRM-EXEC-005 — Plane-qualified lookup
- **Feature**: Multi-plane routing
- **Type**: Integration
- **Steps**: Invoke same name with explicit plane set.
- **Expected**: Correct plane tool executed.
- **Automation**: New

### TC-MPRM-EXEC-006 — Blocklisted command protection
- **Feature**: Safety gate
- **Type**: Negative
- **Steps**: Use static/dynamic paths with blocklisted program.
- **Expected**: Rejection with deterministic behavior.
- **Automation**: Existing (spec harness coverage)

### TC-MPRM-EXEC-007 — Timeout enforcement
- **Feature**: Runtime timeout
- **Type**: Integration
- **Steps**: Invoke tool intentionally exceeding timeout.
- **Expected**: Kill command and return timeout result.
- **Automation**: New

### TC-MPRM-EXEC-008 — Output size cap
- **Feature**: Response safety
- **Type**: Integration
- **Steps**: Run tool producing >64 KiB output.
- **Expected**: Output capped deterministically.
- **Automation**: New

---

## 6) Message Kind Routing (EXEC/DESCRIBE/HEALTH/PUSH/CHANGED)

### TC-MPRM-KIND-001 — Kind absent defaults to EXEC
- **Feature**: Dispatcher behavior
- **Type**: Unit/Integration
- **Steps**: Send payload with no `kind`.
- **Expected**: Routed to EXEC path.
- **Automation**: New

### TC-MPRM-KIND-002 — DESCRIBE all planes
- **Feature**: Introspection
- **Type**: Integration
- **Steps**: Send DESCRIBE without plane.
- **Expected**: Returns array for all loaded planes with tool metadata.
- **Automation**: New

### TC-MPRM-KIND-003 — DESCRIBE single plane
- **Feature**: Introspection
- **Type**: Integration
- **Steps**: Send DESCRIBE with valid plane.
- **Expected**: Returns only selected plane summary.
- **Automation**: New

### TC-MPRM-KIND-004 — HEALTH
- **Feature**: Health endpoint
- **Type**: Integration
- **Steps**: Send HEALTH.
- **Expected**: `status=ok` response.
- **Automation**: New

### TC-MPRM-KIND-005 — Unknown kind handling
- **Feature**: Defensive parsing
- **Type**: Negative
- **Steps**: Send unrecognized `kind`.
- **Expected**: Request dropped safely with warning.
- **Automation**: New

### TC-MPRM-KIND-006 — PUSH success and CHANGED notification
- **Feature**: Catalog update lifecycle
- **Type**: Integration
- **Steps**: Send valid PUSH diff/version flow.
- **Expected**: Promote succeeds, response loaded, CHANGED emitted.
- **Automation**: New

### TC-MPRM-KIND-007 — PUSH base_version mismatch reject
- **Feature**: CAS integrity
- **Type**: Negative
- **Steps**: Send PUSH with stale base version.
- **Expected**: Rejected before apply.
- **Automation**: New

### TC-MPRM-KIND-008 — capability_sync.updated notification on promote
- **Feature**: Outbound notification
- **Type**: Integration
- **Steps**: Successful PUSH; capture outbound cloud notification.
- **Expected**: JSON-RPC notification emitted with updated capabilities/version.
- **Automation**: New

---

## 7) Metadata Features

### TC-MPRM-META-001 — Metadata disabled mode
- **Feature**: Runtime gate
- **Type**: Unit
- **Steps**: Set metadata enable off; send metadata-rich request.
- **Expected**: Metadata checks bypassed.
- **Automation**: Existing

### TC-MPRM-META-002 — Metadata compat mode allow path
- **Feature**: Validation/policy
- **Type**: Unit
- **Steps**: Valid metadata in compat mode.
- **Expected**: Accept and process request.
- **Automation**: Existing

### TC-MPRM-META-003 — Metadata strict mode reject path
- **Feature**: Strict policy
- **Type**: Unit
- **Steps**: Send metadata violating strict constraints.
- **Expected**: Deterministic reject token + exit code mapping.
- **Automation**: Existing

### TC-MPRM-META-004 — Type conflict detection
- **Feature**: Validation
- **Type**: Unit
- **Steps**: Create static/dynamic conflicts in metadata flags.
- **Expected**: Conflict rejection.
- **Automation**: Existing

### TC-MPRM-META-005 — Policy rejection token determinism
- **Feature**: Error mapping
- **Type**: Unit
- **Steps**: Trigger policy deny variants.
- **Expected**: Stable token and exit mapping.
- **Automation**: Existing

### TC-MPRM-META-006 — Metadata echo response option
- **Feature**: Observability
- **Type**: Integration
- **Steps**: Enable metadata echo; send valid metadata.
- **Expected**: `metadata_applied` echoed in response.
- **Automation**: Existing/Partial

### TC-MPRM-META-007 — Byte-level msgpack metadata vectors
- **Feature**: Protocol compatibility
- **Type**: Vector
- **Steps**: Run native/fallback vector suite.
- **Expected**: Decoder and policy outcomes match expected vectors.
- **Automation**: Existing

---

## 8) Dynamic Plugin Features

### TC-MPRM-PLUG-001 — Plugin manager disabled
- **Feature**: Runtime gate
- **Type**: Unit
- **Steps**: Plugin enable=0; invoke plugin tool name.
- **Expected**: Plugin path bypassed; no plugin invocation.
- **Automation**: Existing

### TC-MPRM-PLUG-002 — Load valid plugin ABI
- **Feature**: Plugin lifecycle
- **Type**: Integration
- **Steps**: Place valid plugin `.so`; start/scan.
- **Expected**: Plugin loaded and tools registered.
- **Automation**: Existing

### TC-MPRM-PLUG-003 — Reject invalid ABI symbols/version
- **Feature**: Safety
- **Type**: Negative
- **Steps**: Load plugin with missing/invalid symbols/API version.
- **Expected**: Rejected with deterministic failure metrics/logs.
- **Automation**: Existing/Partial

### TC-MPRM-PLUG-004 — Invoke plugin tool success
- **Feature**: Dispatch
- **Type**: Integration
- **Steps**: Call registered plugin tool.
- **Expected**: Response from plugin path; deterministic logs.
- **Automation**: Existing

### TC-MPRM-PLUG-005 — Plugin invocation error mapping
- **Feature**: Error handling
- **Type**: Negative
- **Steps**: Force plugin invoke error/unavailable/api mismatch.
- **Expected**: ERR_PLUGIN_* mappings are stable.
- **Automation**: Existing

### TC-MPRM-PLUG-006 — Conflict policy reject-plugin-tool
- **Feature**: Registration policy
- **Type**: Unit
- **Steps**: Conflict with existing tool under reject policy.
- **Expected**: Plugin tool rejected; existing binding unchanged.
- **Automation**: Existing

### TC-MPRM-PLUG-007 — Conflict policy plugin-priority
- **Feature**: Registration policy
- **Type**: Unit
- **Steps**: Conflict under plugin-priority policy.
- **Expected**: Plugin binding takes precedence.
- **Automation**: Existing

### TC-MPRM-PLUG-008 — Hot add/remove with notify/poll/manual reload
- **Feature**: Runtime reload
- **Type**: Integration
- **Steps**: Add/remove plugin file under each reload mode.
- **Expected**: Auto/manual reload updates registry correctly.
- **Automation**: Existing/Partial

### TC-MPRM-PLUG-009 — Ownership/mode/path confinement checks
- **Feature**: Plugin hardening
- **Type**: Negative
- **Steps**: Use wrong owner, writable group/world, path escape.
- **Expected**: Plugin load denied safely.
- **Automation**: New

### TC-MPRM-PLUG-010 — Verify mode unsupported path (v1)
- **Feature**: Feature gating
- **Type**: Negative
- **Steps**: Set verify_mode != off.
- **Expected**: Load reject with explicit unsupported signal.
- **Automation**: Existing/Partial

---

## 9) Configuration and Environment Overrides

### TC-MPRM-CFG-001 — Config file default discovery order
- **Feature**: Config resolution
- **Type**: Integration
- **Steps**: Validate `MULTI_PLANE_RUNTIME_MANAGER_CONFIG_FILE` -> `/etc/...` -> `./...`.
- **Expected**: Correct source selected/logged.
- **Automation**: New

### TC-MPRM-CFG-002 — Env overrides config file
- **Feature**: Override precedence
- **Type**: Integration
- **Steps**: Set conflicting values in file and env.
- **Expected**: Env values effective.
- **Automation**: New

### TC-MPRM-CFG-003 — Plugin defaults when keys absent
- **Feature**: Safe defaults
- **Type**: Unit
- **Steps**: Omit plugin keys from config.
- **Expected**: Built-in defaults applied.
- **Automation**: Existing/Partial

### TC-MPRM-CFG-004 — Logging defaults and file sink creation
- **Feature**: Logging config
- **Type**: Integration
- **Steps**: Start with default logging settings.
- **Expected**: Log file created at configured path; entries written.
- **Automation**: New

### TC-MPRM-CFG-005 — Log master switch off
- **Feature**: Logging gate
- **Type**: Negative
- **Steps**: `MULTI_PLANE_RUNTIME_MANAGER_LOG_ENABLE=0`.
- **Expected**: No runtime logs emitted via wrapper.
- **Automation**: New

### TC-MPRM-CFG-006 — API/data trace toggles
- **Feature**: Logging granularity
- **Type**: Integration
- **Steps**: Toggle `LOG_TRACE_API` and `LOG_TRACE_DATA_IO` independently.
- **Expected**: Corresponding log classes enable/disable correctly.
- **Automation**: New

---

## 10) ACL Compile-Time Gating

### TC-MPRM-ACL-001 — Build with ACL OFF (default)
- **Feature**: Compile gate
- **Type**: Build
- **Steps**: Build with `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL=OFF`.
- **Expected**: ACL types/calls compiled out; build succeeds without backend.
- **Automation**: Existing

### TC-MPRM-ACL-002 — Build with ACL ON without backend
- **Feature**: Integration dependency check
- **Type**: Build Negative
- **Steps**: Build with ACL ON and no `acl_policy_store_query` implementation.
- **Expected**: Link failure is explicit and expected.
- **Automation**: New

### TC-MPRM-ACL-003 — Runtime deny behavior when backend is available
- **Feature**: Access control
- **Type**: Integration
- **Steps**: Provide backend mock returning deny.
- **Expected**: EXEC denied with `exit_code=126` and stable output token.
- **Automation**: New

---

## 11) Existing Automated Coverage Mapping

Currently automated through CTest targets:

1. `multi-plane-runtime-manager-requirements-spec-tests`
2. `multi-plane-runtime-manager-plugin-integration-tests` (environment-driven)
3. `multi-plane-runtime-manager-metadata-unit-tests`
4. `multi-plane-runtime-manager-metadata-flow-vectors`
5. `multi-plane-runtime-manager-metadata-msgpack-vectors`
6. `multi-plane-runtime-manager-dynamic-plugins-unit-tests`
7. `multi-plane-runtime-manager-dynamic-plugins-live-tests`
8. `multi-plane-runtime-manager-feature-matrix-spec-tests`

## 12) Completion Criteria

Feature-complete test readiness is achieved when:

- All test cases above are implemented (automated or justified manual),
- Every new feature/bugfix links to at least one test case ID,
- CI runs all non-environment-blocked automated cases,
- Integration/manual-only cases have reproducible runbooks and evidence capture.
