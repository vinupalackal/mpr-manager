# Multi-Plane Runtime Manager Dynamic Plugin Loading Requirements

Version: 1.0  
Date: 2026-08-19  
Status: Proposed

> **Implementation profile update (2026-08-19)**
>
> To close design-to-code gaps before implementation, this document now
> defines a concrete **v1 implementation profile**:
> - Runtime detection mode in v1 is **poll-based scanning** with a
>   background thread (FR-PLUG-004/005). Event/inotify mode remains a
>   future optimization, not a v1 requirement.
> - Security checks implemented in v1 are: absolute-path confinement,
>   `realpath()` prefix validation, owner/mode checks, and symlink-escape
>   prevention (FR-PLUG-016/017). Integrity verification mode
>   (`hash`/`signature`) is supported as configuration surface but is
>   **stubbed/not enforced** in v1 (FR-PLUG-018 deferred).
> - Tool conflict handling in v1 is deterministic and runtime-configured:
>   `reject-plugin-tool` or `plugin-priority` (FR-PLUG-009/010/025).
> - Implementation follows an OpenWrt `rpcd`-style plugin model:
>   `dlopen`/`dlsym` symbol contract, plugin-owned tool list, dynamic
>   dispatch by method/tool name, and failure isolation per plugin.

Related design documents:
- [DYNAMIC_PLUGIN_HLD.md](DYNAMIC_PLUGIN_HLD.md)
- [DYNAMIC_PLUGIN_LLD.md](DYNAMIC_PLUGIN_LLD.md)

## 1. Purpose

Define requirements for adding dynamic plugin loading to Multi-Plane Runtime Manager so plugins (`.so`) can expose one or more toolsets and be loaded/unloaded at runtime.

## 2. Scope

In scope:
- Runtime discovery of shared libraries from a configured plugin directory.
- Dynamic load of plugins without process restart.
- Plugin API for exposing one or more tools.
- Tool invocation through existing request flow.
- Hot detection of newly added `.so` files.

Out of scope:
- Cloud-side orchestration UX.
- Non-Linux plugin formats.
- Kernel/module-level plugins.

## 3. Definitions

- Plugin: Shared object (`.so`) loaded via runtime linker.
- Toolset: One or more tools exported by a plugin.
- Tool: Named executable action callable by Multi-Plane Runtime Manager.
- Plugin manager: Runtime subsystem handling discovery, load/unload, and dispatch.

## 4. High-Level Behavior

1. Multi-Plane Runtime Manager monitors a plugin directory.
2. When a new `.so` appears, Multi-Plane Runtime Manager validates and loads it.
3. Plugin registers one or more tools with Multi-Plane Runtime Manager.
4. Incoming requests can invoke those plugin tools.
5. If plugin file is removed/updated, Multi-Plane Runtime Manager safely unloads/reloads.

## 5. Plugin Directory and Discovery

### FR-PLUG-001 Configurable plugin path
Multi-Plane Runtime Manager shall support a configurable plugin directory path.

Default proposal:
- `/usr/lib/multi-plane-runtime-manager/plugins`

### FR-PLUG-002 File filtering
Multi-Plane Runtime Manager shall only consider files ending with `.so`.

### FR-PLUG-003 Initial scan
At startup, Multi-Plane Runtime Manager shall scan the plugin directory and attempt to load all valid plugins.

### FR-PLUG-004 Dynamic detection of new plugins
Multi-Plane Runtime Manager shall detect newly added `.so` files at runtime and load them without restart.

Acceptance options (implementation choice):
- Preferred: filesystem events (e.g., inotify on Linux).
- Fallback: periodic rescan with configurable interval.

v1 profile: periodic rescan is mandatory; event mode optional.

### FR-PLUG-005 Dynamic detection of removed/updated plugins
Multi-Plane Runtime Manager shall detect plugin removal or replacement and safely unload/reload associated toolsets.

v1 profile: removal/update detection is performed by periodic rescans.

## 6. Plugin ABI/API Contract

### FR-PLUG-006 Required exported symbols
Each plugin shall export required entry points:
- `diag_plugin_get_api_version()`
- `diag_plugin_init(const diag_host_api_t *host, diag_plugin_ctx_t **ctx)`
- `diag_plugin_get_tool_count(diag_plugin_ctx_t *ctx)`
- `diag_plugin_get_tool(diag_plugin_ctx_t *ctx, size_t idx, diag_tool_def_t *out)`
- `diag_plugin_invoke(diag_plugin_ctx_t *ctx, const diag_invoke_req_t *req, diag_invoke_resp_t *resp)`
- `diag_plugin_deinit(diag_plugin_ctx_t *ctx)`

### FR-PLUG-007 API version compatibility
Multi-Plane Runtime Manager shall validate plugin API version before accepting plugin.

### FR-PLUG-008 One-or-more toolset support
A plugin shall be able to expose one or more tools.

### FR-PLUG-009 Unique tool naming
Tool names must be unique globally across built-in and plugin tools.
Conflict policy shall be deterministic (configurable):
- reject plugin tool, or
- allow override by priority.

## 7. Runtime Dispatch

### FR-PLUG-010 Tool resolution order
For incoming request `tool`, resolver shall check:
1. Plugin tool registry
2. Existing catalog tool (or configurable reverse order)

### FR-PLUG-011 Plugin tool invocation
If tool belongs to plugin, Multi-Plane Runtime Manager shall invoke plugin API instead of shell command path.

### FR-PLUG-012 Response mapping
Plugin invocation result shall map to existing response contract:
- `tool`
- `exit_code`
- `stdout` (binary/text bytes)

## 8. Hot Reload and Concurrency

### FR-PLUG-013 Thread-safe registry
Plugin registry and tool registry shall be thread-safe for concurrent request handling.

### FR-PLUG-014 Safe unload semantics
Plugin unload shall not invalidate in-flight requests.
Required behavior:
- reference counting or grace period before `dlclose()`.

### FR-PLUG-015 Atomic swap on update
When a plugin is updated, server shall atomically replace old version with new version and keep service available.

## 9. Security and Trust

### FR-PLUG-016 Path confinement
Only load plugins from configured directory; no relative path traversal.

### FR-PLUG-017 Ownership and permissions checks
Before load, verify file owner and mode satisfy policy.

### FR-PLUG-018 Optional signature/hash verification
System should support optional integrity verification of plugin binaries.

v1 profile: configuration keys and logging are present; hash/signature
validation is deferred and reported as `verify_mode=off` unless extended.

### FR-PLUG-019 Capability boundaries
Plugin host API shall expose minimal required functions; no unrestricted host internals.

## 10. Fault Isolation and Reliability

### FR-PLUG-020 Load failure isolation
If one plugin fails to load, Multi-Plane Runtime Manager shall continue running and process remaining plugins.

### FR-PLUG-021 Invocation failure handling
Plugin invocation errors shall return deterministic failure response and not crash process.

### FR-PLUG-022 Crash mitigation (target)
Target architecture should support stronger isolation (e.g., subprocess sandbox) for untrusted plugins.

## 11. Observability

### FR-PLUG-023 Lifecycle logging
Log plugin discovery, load success/failure, unload, reload, and symbol/version errors.

### FR-PLUG-024 Metrics/counters
Expose counters:
- plugins_loaded
- plugins_failed
- tools_registered
- plugin_invocations
- plugin_errors
- reload_events

## 12. Configuration Requirements

### FR-PLUG-025 Runtime configuration keys
Configurable values shall include:
- plugin directory path
- discovery mode (event/poll)
- poll interval
- conflict policy
- integrity verification mode
- plugin feature enable/disable flag

### FR-PLUG-026 Feature flag
Dynamic plugin loading shall be controllable by compile-time and runtime flags.

## 13. Non-Functional Requirements

### NFR-PLUG-001 Performance
Plugin tool dispatch overhead should be minimal relative to request processing.

### NFR-PLUG-002 Availability
Hot load/reload shall not block main receive loop for extended durations.

### NFR-PLUG-003 Memory safety
No leaks on repeated load/unload cycles.

### NFR-PLUG-004 Portability baseline
Primary target: Linux-based RDK runtime.

## 14. Proposed Data Structures (Informative)

- `plugin_record_t`: path, handle (`dlopen`), api version, state, refcount, tool list.
- `tool_record_t`: tool name, plugin id, function bindings.
- `plugin_manager_t`: registry maps + lock + watcher state.

## 15. Verification Matrix

| Requirement Area | Verification | Test Cases |
|---|---|---|
| Startup discovery | Integration test with preloaded plugin dir | TC-PLUG-001 |
| Dynamic add `.so` | Runtime test adding plugin file post-start | TC-PLUG-002 |
| Dynamic remove/update | Runtime test for unload/reload behavior | TC-PLUG-003, TC-PLUG-004 |
| ABI mismatch handling | Negative test with wrong API version | TC-PLUG-001 |
| Multi-tool plugin | Test plugin exposing N tools | TC-PLUG-005 |
| Concurrency safety | Parallel invoke + reload stress test | TC-PLUG-009 |
| Fault isolation | Plugin init/invoke failure tests | TC-PLUG-008 |
| Security checks | Permission/ownership/integrity tests | TC-PLUG-007 |
| Leak safety | Repeated load/unload soak + leak scan | TC-PLUG-009 |

## 16. Detailed Test Cases

### TC-PLUG-001 Startup scan loads valid plugins
- Requirements: FR-PLUG-002, FR-PLUG-003, FR-PLUG-020, FR-PLUG-023
- Precondition: plugin directory contains `diag_ok.so`, `readme.txt`, `diag_bad_abi.so`.
- Steps:
	1. Start Multi-Plane Runtime Manager with plugin feature enabled.
	2. Observe startup logs and tool registry.
- Expected:
	- `diag_ok.so` loads and registers its tools.
	- `readme.txt` is ignored.
	- `diag_bad_abi.so` is rejected without process crash.
	- Lifecycle logs show one success and one failure with reason.

### TC-PLUG-002 Runtime add of plugin file
- Requirements: FR-PLUG-004, FR-PLUG-023, FR-PLUG-024
- Precondition: server running; directory initially has no `diag_hot.so`.
- Steps:
	1. Copy `diag_hot.so` into plugin directory.
	2. Wait for watcher/poll interval.
	3. Invoke one tool exported by `diag_hot.so`.
- Expected:
	- New plugin is detected and loaded without restart.
	- Tool invocation succeeds via plugin path.
	- `reload_events`/`plugins_loaded` counters increment.

### TC-PLUG-003 Runtime remove of plugin file
- Requirements: FR-PLUG-005, FR-PLUG-014, FR-PLUG-023
- Precondition: plugin `diag_hot.so` loaded and serving tool `hot_tool`.
- Steps:
	1. Start a long-running invocation of `hot_tool`.
	2. Remove `diag_hot.so` from plugin directory during in-flight call.
	3. Issue another request for `hot_tool` after removal handling completes.
- Expected:
	- In-flight call completes deterministically (success/failure, no crash).
	- Plugin unload is deferred until references drop to zero.
	- New requests fail with tool-not-found (or configured equivalent).

### TC-PLUG-004 Plugin update atomic swap
- Requirements: FR-PLUG-005, FR-PLUG-015, NFR-PLUG-002
- Precondition: v1 plugin loaded exporting `version_tool` => `v1`.
- Steps:
	1. Replace plugin binary with v2 exporting `version_tool` => `v2`.
	2. Continuously invoke `version_tool` during replacement.
- Expected:
	- Requests return either v1 or v2 outputs, never partial/corrupted output.
	- No service outage or receive-loop stall.
	- Reload event logged once per successful swap.

### TC-PLUG-005 Multi-tool plugin registration
- Requirements: FR-PLUG-008, FR-PLUG-010, FR-PLUG-011, FR-PLUG-012
- Precondition: plugin exports 3 tools with distinct names.
- Steps:
	1. Load plugin.
	2. Invoke all 3 tools.
	3. Validate response schema.
- Expected:
	- All tools resolve to plugin dispatch.
	- Each response contains `tool`, `exit_code`, `stdout`.

### TC-PLUG-006 Name conflict policy behavior
- Requirements: FR-PLUG-009, FR-PLUG-010, FR-PLUG-025
- Precondition: one plugin tool name matches existing catalog tool.
- Steps:
	1. Run with conflict policy `reject` and load plugin.
	2. Invoke conflicting tool.
	3. Run with conflict policy `plugin-priority` and repeat.
- Expected:
	- In `reject` mode: plugin tool registration fails deterministically.
	- In `plugin-priority` mode: resolver consistently picks plugin tool.
	- Policy decision appears in logs.

### TC-PLUG-007 Security gate checks
- Requirements: FR-PLUG-016, FR-PLUG-017, FR-PLUG-018
- Precondition: test plugins with bad permissions, wrong owner, bad signature/hash.
- Steps:
	1. Place each invalid plugin in watched directory.
	2. Trigger discovery.
- Expected:
	- Each invalid plugin is rejected before `dlopen`/activation.
	- Rejection reason is explicit (path/owner/mode/integrity).

### TC-PLUG-008 Failure isolation in init and invoke
- Requirements: FR-PLUG-020, FR-PLUG-021, FR-PLUG-023
- Precondition: `diag_fail_init.so` returns init error; `diag_fail_invoke.so` returns invoke error.
- Steps:
	1. Start server and load both plugins.
	2. Invoke tool from `diag_fail_invoke.so`.
	3. Invoke tool from healthy plugin.
- Expected:
	- Init failure plugin is skipped; server keeps running.
	- Invoke failure maps to deterministic non-zero response.
	- Healthy plugin remains functional.

### TC-PLUG-009 Concurrency stress with reload
- Requirements: FR-PLUG-013, FR-PLUG-014, NFR-PLUG-001, NFR-PLUG-002, NFR-PLUG-003
- Precondition: load at least 2 plugins, each with callable tools.
- Steps:
	1. Run parallel request load (e.g., 64 concurrent clients).
	2. Simultaneously trigger periodic plugin add/remove/update.
	3. Continue for soak window (e.g., 30-60 min).
- Expected:
	- No deadlock, crash, or use-after-free symptoms.
	- Main loop remains responsive.
	- Memory usage remains bounded; leak scan passes.

### TC-PLUG-010 Feature flag behavior
- Requirements: FR-PLUG-026, FR-PLUG-025
- Precondition: plugin binaries present in plugin directory.
- Steps:
	1. Start with plugin feature disabled.
	2. Verify plugins are not loaded.
	3. Restart with feature enabled and verify load.
- Expected:
	- Disabled mode ignores plugin directory and uses baseline behavior.
	- Enabled mode performs discovery/load according to config.

## 17. Acceptance Criteria

Feature is accepted when:
1. New `.so` added to configured plugin path is detected and loaded without restart.
2. Plugin exposing multiple tools can serve those tools through normal request path.
3. Remove/update of plugin is handled safely without crashing service.
4. Thread-safe behavior is demonstrated under concurrent request load.
5. Required logs/metrics and failure handling are in place.
