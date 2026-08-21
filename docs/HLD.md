# Multi-Plane Runtime Manager — High-Level Design (Unified)

## Consolidation notice

This file is the unified HLD source for the project.

It consolidates and supersedes:

- DYNAMIC_PLUGIN_HLD.md
- CAPABILITY_PUBLICATION_HLD.md
- HYBRID_WATCHER_HLD.md
- METADATA_FIELDS_HLD.md
- METADATA_PLANE_TAXONOMY_HLD.md

## 1. System context

The runtime manager serves cloud/device diagnostic requests over WRP (msgpack),
routes operations across plane-scoped catalogs, executes approved tools, and
supports dynamic plugin extension with runtime publication and metadata policy
controls.

Core architecture layers:

1. Transport and envelope layer (nanomsg PUSH/PULL + WRP framing).
2. Request dispatch layer (`EXEC`, `DESCRIBE`, `HEALTH`, `PUSH`, `CHANGED`).
3. Catalog and execution policy layer.
4. Dynamic plugin lifecycle layer.
5. Capability publication and synchronization layer.
6. Metadata validation/policy layer.
7. Guardrail and observability layer.

## 2. Functional subsystems

### 2.1 Dynamic plugin subsystem

- Discovers and loads `.so` plugins from configured directories.
- Performs safety checks (path confinement, owner/mode, ABI contract).
- Registers plugin tools into a registry with conflict policy.
- Supports reload/unload with in-flight invoke draining.

### 2.2 Hybrid watcher subsystem

- Supports `poll`, `notify`, and `hybrid` modes.
- Enforces periodic reconcile as authoritative state recovery.
- Handles filesystem churn with debounce/coalescing semantics.
- Provides callback hooks for registry change publication triggers.

### 2.3 Capability publication subsystem

- Builds unified capability snapshots from catalog and dynamic sources.
- Applies deterministic conflict policy to merged view.
- Reuses shared snapshot path for `DESCRIBE` and `capability_sync.updated`.
- Publishes at startup and after relevant runtime changes.

### 2.4 Metadata subsystem

- Decodes metadata fields from requests.
- Applies strict/compat validation and policy decisions.
- Normalizes legacy plane aliases into canonical planes.
- Produces deterministic reject tokens and logs.

### 2.5 PUSH security/availability subsystem

- Enforces local-only transport policy by default.
- Optionally enforces shared-token authorization.
- Applies payload-size and request-rate guardrails.
- Supports rollout-safe modes (`enforce`, `monitor`, `off`).

### 2.6 Observability subsystem

- Emits structured logs for accept/reject and policy outcomes.
- Exposes health-time guardrail posture and counters.
- Tracks accepted/rejected/observed PUSH guardrail events.

## 3. Data and control flows

1. Inbound WRP request decoded.
2. `kind` dispatch selects protocol path.
3. Policy gates run before state mutation or execution.
4. Request outcome returned on source-correct reply socket.
5. Side-channel notifications emitted on successful updates.
6. Metrics/health surfaces updated for operations visibility.

## 4. Concurrency model

- Short critical sections for request-path responsiveness.
- Scan/reload operations separated into discovery vs commit phases.
- Guardrail and counter updates protected by dedicated mutexes.
- Plugin in-flight invoke tracking protects unload/reload safety.

## 5. Reliability and rollback posture

- Guardrails can start in `monitor` mode for canary rollout.
- Health counters support live rollback decisioning.
- Enforced policy mode can be reverted by config without rebuild.

## 6. Validation strategy (HLD-level)

- Unit/spec vectors for policy decisions and clamping.
- Integration tests for plugin lifecycle and churn safety.
- End-to-end CI with dynamic plugin live tests and publication checks.

## 7. Related artifacts

- [REQUIREMENTS.md](REQUIREMENTS.md)
- [LLD.md](LLD.md)
- [COMPREHENSIVE_TEST_CASES.md](COMPREHENSIVE_TEST_CASES.md)
- Phase implementation notes:
  - PHASES_IMPLEMENTATION.md

## 8. Lightweight catalog database mode (Draft, 2026-08-21)

### 8.1 Motivation

Current startup parses catalog JSON into resident in-memory structures. For
resource-constrained targets, a database-backed mode can bound resident catalog
memory by hot working set instead of total catalog footprint.

### 8.2 Proposed architecture

New storage stack:

1. LMDB storage layer (source of truth for tool catalog records).
2. Lookup service layer (composite key resolution by `plane` + `tool`).
3. Bounded LRU cache layer (hot key acceleration).
4. Reload coordinator (watcher + periodic fallback poll + generation bump).

Core key/value model:

- Key: `<plane>:<tool>`
- Value: command execution metadata (`command`, `timeout`, `type`, optional
  execution flags and version fields)

### 8.3 Request flow (database mode)

1. Request decoder extracts `plane` and `tool`.
2. Lookup checks LRU cache.
3. On miss, perform LMDB read transaction for `<plane>:<tool>`.
4. Apply unchanged command/policy validation and execute.
5. Update cache and telemetry counters.

### 8.4 Update/reload flow

1. Catalog update writer commits a new LMDB state atomically.
2. Watcher signal (inotify or platform-equivalent) and/or periodic poll
  detects version/generation change.
3. Runtime invalidates stale cache entries (or bumps generation for lazy
  invalidation).
4. Subsequent requests resolve from new LMDB state.

### 8.5 Reliability and operability posture

- LMDB reader path supports high-concurrency read access without a DB daemon.
- Bounded cache prevents unbounded heap growth.
- Version/generation telemetry enables rollout and troubleshooting.
- Fallback rollout strategy supports staged enablement and rollback.
