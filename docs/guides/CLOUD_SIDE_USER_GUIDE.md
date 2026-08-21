# Cloud-Side User Guide

## 1) Purpose

This guide explains how cloud callers should use Multi-Plane Runtime Manager APIs safely and predictably.

The transport payload on the wire is msgpack, but request/response examples below are shown as JSON for readability.

---

## 2) Cloud request model

A cloud request reaches the service as WRP `msg_type=3` with inner payload.

Common request fields:
- `tool` (string): tool name for `EXEC`
- `command` (string, optional): meaningful for dynamic tools
- `plane` (string, optional but recommended): `triage|management|control|config-apply`
- `kind` (string): optional discriminator (`DESCRIBE|HEALTH|PUSH`), absent means `EXEC`

---

## 3) API usage patterns

### 3.1 EXEC (default kind)

Request example:

```json
{
  "tool": "device_uptime",
  "plane": "triage"
}
```

Response example:

```json
{
  "tool": "device_uptime",
  "exit_code": 0,
  "stdout": "12345.67 54321.00\n"
}
```

Guidance:
- Always include `plane` from cloud side to avoid cross-plane ambiguity.
- Use `command` only for tools intentionally marked dynamic.

### 3.2 DESCRIBE

Request:

```json
{
  "kind": "DESCRIBE",
  "plane": "triage"
}
```

Response:
- Returns one plane description if `plane` is provided
- Returns array of available planes if omitted

Use case:
- Cloud capability discovery and UI population.

### 3.3 HEALTH

Request:

```json
{
  "kind": "HEALTH"
}
```

Response:

```json
{
  "status": "ok",
  "push_guardrails": {
    "local_only_required": true,
    "auth_token_configured": false,
    "rate_limit_mode": "enforce",
    "payload_limit_mode": "enforce",
    "min_interval_ms": 250,
    "max_payload_bytes": 262144,
    "attempts": 0,
    "accepted": 0,
    "observed_rate_limit": 0,
    "observed_payload_too_large": 0
  },
  "catalog_db": {
    "backend_requested": "lmdb",
    "backend_effective": "lmdb",
    "lmdb_build_enabled": true,
    "fallback_to_json": false,
    "lmdb_ready": true,
    "generation": 1,
    "reload_events": 0,
    "reload_poll_sec": 10,
    "cache_entries": 0,
    "cache_max_entries": 256,
    "cache_hits": 0,
    "cache_misses": 0,
    "cache_evictions": 0
  }
}
```

Use case:
- Liveness checks from cloud orchestration.

### 3.4 PUSH (catalog update)

Request:

```json
{
  "kind": "PUSH",
  "plane": "triage",
  "base_version": 12,
  "target_version": 13,
  "auth_token": "optional-if-configured",
  "diff": {
    "added": {},
    "removed": [],
    "modified": {}
  }
}
```

Response status typically indicates `loaded` or `rejected` with reason.

Notes:
- Default posture is local-only PUSH transport.
- If `MULTI_PLANE_RUNTIME_MANAGER_PUSH_AUTH_TOKEN` is configured on device,
  `auth_token` must match exactly or the request is rejected.
- Oversized PUSH payloads are rejected by `MULTI_PLANE_RUNTIME_MANAGER_PUSH_MAX_PAYLOAD_BYTES`.
- Rapid PUSH bursts may be rejected by `MULTI_PLANE_RUNTIME_MANAGER_PUSH_MIN_INTERVAL_MS`.

Use case:
- Controlled roll-forward of plane tool definitions.

---

## 4) Async events cloud can observe

After successful push:
- `CHANGED` notification payload (plane/version)
- `capability_sync.updated` JSON-RPC notification

Use these to refresh cloud-side caches.

---

## 5) Error handling guidance

Recommended cloud handling by response token:
- `tool not in catalog`: mark capability miss, do not retry blindly
- `command blocked or missing`: catalog issue or policy restriction
- `access denied`: ACL policy denial (when ACL enabled)
- `ERR_PLUGIN_UNAVAILABLE|ERR_PLUGIN_INVOKE|ERR_PLUGIN_API_VERSION`: plugin path issue

Retry policy:
- Safe to retry transient transport failures
- Do not retry policy/schema/version rejections without correction

---

## 6) Cloud best practices

- Always send explicit `plane`
- Keep idempotent client behavior for `DESCRIBE` and `HEALTH`
- Version and audit every `PUSH`
- Treat catalog updates like deployment operations (approval + rollback plan)
