# Multi-Plane Runtime Manager — Low-Level Design (Unified)

## Consolidation notice

This file is the unified LLD source for the project.

It consolidates and supersedes:

- DYNAMIC_PLUGIN_LLD.md
- CAPABILITY_PUBLICATION_LLD.md
- HYBRID_WATCHER_LLD.md
- METADATA_FIELDS_LLD.md
- METADATA_PLANE_TAXONOMY_LLD.md

## 1. Module map

Primary implementation units:

- `src/multi-plane-runtime-manager.c`
  - WRP decode/encode, kind dispatch, catalog execution path
  - metadata policy integration
  - PUSH handling, guardrails, HEALTH payload, notifications
- `src/plugin_manager.c/.h`
  - plugin discovery/load/reload/unload/invoke
  - watcher modes and lock-scope behavior
- `src/tool_registry.c/.h`
  - plugin tool binding/lookup/snapshot
- `src/capability_publication.c/.h`
  - catalog+dynamic snapshot merge and conflict resolution
- `src/metadata_fields.c/.h`
  - metadata parsing/validation/policy/canonicalization

## 2. Request-path implementation details

### 2.1 Protocol dispatch

`handle_local_request()` routes by optional `kind`:

- default/`EXEC` → `handle_request()`
- `DESCRIBE` → `handle_describe_request()`
- `HEALTH` → `handle_health_request()`
- `PUSH` → `handle_push_request()`

### 2.2 EXEC path

- Decode request payload.
- Apply metadata gates (if enabled).
- Attempt plugin invoke path first when dynamic plugin manager is enabled.
- Fallback to catalog lookup/command execution with safety restrictions.

### 2.3 DESCRIBE path

- Builds per-plane capability snapshots.
- Uses merged catalog+dynamic entries with deterministic conflict policy.

### 2.4 HEALTH path

- Returns `status=ok`.
- Exposes `push_guardrails` object with effective modes, limits, counters,
  and recent-attempt timestamp.

### 2.5 PUSH path

Pre-apply ordering:

1. transport policy gate
2. payload-size guard
3. rate-limit guard
4. payload decode
5. authorization token gate
6. catalog apply/promote

Guard modes:

- `enforce`: reject violating request
- `monitor`: allow request, increment observed counters, emit warning logs
- `off`: bypass guardrail entirely

## 3. Dynamic plugin internals

- Candidate collection and normalization done outside hot invoke lock path.
- Commit-stage mutations done under plugin-manager lock.
- Unload/reload waits release lock between drain polls.
- Scan/stop serialization lock avoids mutation race windows.

## 4. Capability publication internals

- Snapshot structure contains normalized per-tool metadata fields.
- Conflict policy applied during merge (`reject`/`plugin-priority`).
- Shared output used by both `DESCRIBE` and cloud capability sync.

## 5. Metadata internals

- Canonical planes: `triage`, `management`, `control`, `config-apply`.
- Legacy aliases mapped before strict-policy validation.
- Deterministic errors mapped to stable response tokens and exit codes.

## 6. Guardrail telemetry internals

Counters maintained for:

- attempts, accepted
- rejected_transport
- rejected_unauthorized
- rejected_rate_limit
- rejected_payload_too_large
- rejected_other
- observed_rate_limit
- observed_payload_too_large

Synchronization:

- counters and last-attempt timestamp protected by `g_push_guard_mutex`.

## 7. Configuration knobs

Key runtime controls:

- `MULTI_PLANE_RUNTIME_MANAGER_PUSH_REQUIRE_LOCAL_ONLY`
- `MULTI_PLANE_RUNTIME_MANAGER_PUSH_AUTH_TOKEN`
- `MULTI_PLANE_RUNTIME_MANAGER_PUSH_MAX_PAYLOAD_BYTES`
- `MULTI_PLANE_RUNTIME_MANAGER_PUSH_MIN_INTERVAL_MS`
- `MULTI_PLANE_RUNTIME_MANAGER_PUSH_RATE_LIMIT_MODE`
- `MULTI_PLANE_RUNTIME_MANAGER_PUSH_PAYLOAD_LIMIT_MODE`

## 8. Test mapping (LLD-level)

- Policy/spec checks: `tests/feature_matrix_spec_tests.c`
- Plugin lifecycle/concurrency checks:
  - `tests/dynamic_plugins_unit.c`
  - `tests/dynamic_plugins_live.c`
  - `tests/plugin_lock_scope_live.c`
  - `tests/plugin_unload_wait_lock_scope_live.c`
- Capability merge correctness: `tests/capability_publication_unit.c`

## 9. Related artifacts

- [REQUIREMENTS.md](REQUIREMENTS.md)
- [HLD.md](HLD.md)
- [COMPREHENSIVE_TEST_CASES.md](COMPREHENSIVE_TEST_CASES.md)

## 10. Lightweight catalog database mode (Draft, 2026-08-21)

### 10.1 Proposed module additions

- `catalog_db_lmdb.c/.h`
  - LMDB environment open/close
  - get by composite key
  - optional write/import utility hooks
- `catalog_cache_lru.c/.h`
  - fixed-capacity LRU for resolved records
  - hit/miss accounting
- `catalog_reload.c/.h`
  - watcher callbacks + periodic polling fallback
  - generation/version management and cache invalidation

### 10.2 Data layout

LMDB key:

- `<plane>:<tool>` (UTF-8 string key)

LMDB value (serialized record):

- `command` (string)
- `timeout_sec` (uint32)
- `type` (`static`/`dynamic` enum or canonical string)
- optional fields currently present in catalog behavior
  (`suppress_stderr`, `count_lines_matching`, metadata tags)
- `catalog_version`/`generation` (uint64)

### 10.3 Lookup path details

1. Build canonical key from request `plane` and `tool`.
2. Probe LRU cache with the full key.
3. On miss, open LMDB read transaction and fetch key.
4. Deserialize value into runtime record.
5. Insert record into LRU (evict oldest when full).
6. Continue through existing policy and execution path unchanged.

### 10.4 Cache and memory policy

- Cache capacity controlled by config (example: 128/256/512 entries).
- Per-entry memory bounded by command and metadata field limits.
- No full-catalog parse/retention required in database mode.

### 10.5 Reload/version behavior

- Maintain `g_catalog_generation` (monotonic integer).
- On watcher/poll detection of catalog update:
  - increment generation,
  - invalidate all cache entries (or mark generation mismatch for lazy
    refresh),
  - increment reload metrics counter.
- If watcher fails or is unavailable, periodic poll remains authoritative
  fallback.

### 10.6 Concurrency notes

- LMDB read transactions are per-request and short-lived.
- Cache access guarded by lightweight mutex or RW-lock.
- Reload path serialized to prevent interleaved invalidation races.
- Existing execution-path locks remain unchanged.

### 10.7 Configuration additions (proposed)

- `MULTI_PLANE_RUNTIME_MANAGER_CATALOG_BACKEND=json|lmdb`
- `MULTI_PLANE_RUNTIME_MANAGER_LMDB_PATH=<path>`
- `MULTI_PLANE_RUNTIME_MANAGER_LRU_MAX_ENTRIES=<N>`
- `MULTI_PLANE_RUNTIME_MANAGER_CATALOG_RELOAD_POLL_SEC=<N>`

### 10.8 Telemetry additions (proposed)

- `catalog_backend`
- `catalog_generation`
- `catalog_cache_hits`
- `catalog_cache_misses`
- `catalog_cache_entries`
- `catalog_reload_events`

### 10.9 Test strategy additions (LLD-level)

- Unit tests:
  - key format and parser (`plane:tool`)
  - LRU insertion/eviction/order
  - deserialization and validation behavior
- Integration tests:
  - all-plane lookup correctness
  - hot-key cache behavior under repeated requests
  - update detection and cache invalidation correctness
- Regression checks:
  - response parity with existing JSON mode for equivalent data
  - no policy regressions in blocked/static/dynamic handling
