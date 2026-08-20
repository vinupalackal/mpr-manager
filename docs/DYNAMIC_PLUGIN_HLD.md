# Multi-Plane Runtime Manager Dynamic Plugin Loading High-Level Design (HLD)

Version: 1.0  
Date: 2026-08-19  
Related requirements: [DYNAMIC_PLUGIN_REQUIREMENTS.md](DYNAMIC_PLUGIN_REQUIREMENTS.md)

> **Implementation gap closure (2026-08-19)**
>
> This HLD now declares the v1 implementation strategy used by code:
> - Plugin lifecycle and watcher are implemented as one subsystem
>   (single plugin manager with internal poll thread).
> - Runtime discovery uses periodic rescan (poll mode) in v1.
> - Architecture and API shape follow OpenWrt `rpcd` plugin conventions:
>   dynamic symbol contract, method/tool registration at load time,
>   and dispatch by resolved tool binding.

## 1. Objective

Add runtime plugin loading so Multi-Plane Runtime Manager can discover shared libraries (`.so`), register one or more plugin-provided tools, and serve those tools without process restart.

## 2. Design Goals

1. Runtime extensibility without daemon restart.
2. Safe hot load/unload/reload under concurrent requests.
3. Backward compatibility with existing catalog command execution path.
4. Clear plugin ABI contract with version compatibility checks.
5. Robust observability and failure isolation.

## 3. System Context

Current server supports:
- WRP request decode
- tool lookup via catalog
- shell command execution path

Target architecture adds:
- Plugin Manager subsystem
- Tool Registry abstraction (catalog + plugin tools)
- Filesystem Watcher / periodic scanner

## 4. High-Level Architecture

## 4.1 Components

1. **Plugin Manager**
- Discovers plugin files in configured directory.
- Loads/unloads plugins via runtime linker.
- Validates API version and symbols.
- Maintains plugin lifecycle state.

2. **Tool Registry**
- Unified lookup table for tool name -> execution provider.
- Supports provider types:
  - catalog provider
  - plugin provider

3. **Plugin Watcher**
- Detects plugin add/remove/modify events.
- Triggers load/unload/reload workflows.

v1 realization: implemented as an internal poll loop in Plugin Manager
(not a separate daemon/service).

4. **Dispatch Layer**
- For each request `tool`, resolves provider from registry.
- Invokes either existing catalog path or plugin invoke path.

5. **Observability Layer**
- Structured logs and counters for plugin lifecycle and invocation outcomes.

## 4.2 Runtime Flows

### A) Startup Flow
1. Initialize plugin manager.
2. Scan plugin directory for `.so` files.
3. Validate + load each plugin.
4. Register plugin-exposed tools.
5. Start watcher.

### B) Request Dispatch Flow
1. Receive request with `tool`.
2. Lookup tool in unified registry.
3. If provider is plugin: call plugin invoke API.
4. Else use existing catalog command execution path.
5. Return response using existing response contract.

### C) Hot Add Flow
1. New `.so` detected.
2. Validate file/security checks.
3. `dlopen` + symbol binding + version check.
4. Initialize plugin.
5. Register all exposed tools atomically.

v1 note: detection trigger is poll tick.

### D) Hot Remove/Update Flow
1. File remove/replace detected.
2. Mark plugin as draining.
3. Wait until in-flight refs drop to zero.
4. Unregister tools, deinit plugin, `dlclose`.
5. If replace: load new binary and re-register.

## 5. Plugin Lifecycle State Machine

States:
- DISCOVERED
- LOADING
- ACTIVE
- DRAINING
- UNLOADED
- FAILED

Transitions:
- DISCOVERED -> LOADING -> ACTIVE
- ACTIVE -> DRAINING -> UNLOADED
- LOADING -> FAILED
- ACTIVE -> FAILED (runtime fatal plugin errors)

## 6. Tool Conflict Policy

Supported strategies:
1. `reject-plugin-tool` (default): keep existing tool binding.
2. `plugin-priority`: plugin tool overrides existing binding.

Policy must be configurable and logged.

## 7. Concurrency Strategy

- Global plugin/registry read-write lock:
  - read lock for request lookup
  - write lock for registry mutations
- Per-plugin atomic refcount for in-flight invocations.
- Unload waits for refcount drain with timeout/failsafe.

## 8. Security Strategy

- Load only from configured absolute directory.
- Enforce file ownership/mode checks before load.
- Optional signature/hash verification.
- Reject symlink escapes and path traversal.

## 9. Failure Handling

- Plugin load failure does not stop daemon.
- Plugin invoke failure maps to deterministic response.
- Watcher failures degrade to periodic rescan mode.

## 10. Configuration Model

Proposed config keys:
- `plugin.enable` (bool)
- `plugin.dir` (string)
- `plugin.discovery_mode` (`event`|`poll`)
- `plugin.poll_interval_sec` (int)
- `plugin.conflict_policy` (`reject-plugin-tool`|`plugin-priority`)
- `plugin.verify_mode` (`off`|`hash`|`signature`)

v1 note: `hash`/`signature` are declared configuration values; only
`off` is enforced functionally in v1.

## 11. Rollout Plan

1. Phase 1: Plugin manager + startup load + rpcd-style ABI contract.
2. Phase 2: Poll-thread runtime add/remove/update detection.
3. Phase 3: Safe reload + conflict policy + verification hooks.
4. Phase 4: Stress and soak validation in target environment.

## 12. Acceptance Summary

HLD is satisfied when:
- Plugin architecture integrates with existing request/response path.
- Hot add/remove/update flows are defined and safe.
- Security, concurrency, and observability controls are included.
