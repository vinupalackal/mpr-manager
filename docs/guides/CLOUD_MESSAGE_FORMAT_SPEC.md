# Cloud Message Format Specification (Cloud-Side + MsgPack Verification)

Version: 2.0  
Date: 2026-08-21  
Applies to: `src/multi-plane-runtime-manager.c`

## 1) Purpose

This document is the implementation-aligned contract for cloud/client integrations. It defines:

- all supported request kinds,
- exact logical JSON payload shape,
- expected msgpack wire representation,
- per-field type behavior used by the service parser.

## 2) Encoding and transport

### 2.1 WRP envelope

All request/response traffic uses a WRP map (msgpack encoded).

For diagnostics APIs, `msg_type` is `3`.

### 2.2 Inner payload

- WRP field `payload` carries the inner request/response body.
- For request kinds `EXEC`, `DESCRIBE`, `HEALTH`, `PUSH`: payload bytes are msgpack-encoded maps.
- For outbound `capability_sync.updated`: payload bytes are JSON text (`content_type = application/json`).

### 2.3 Message type reference

| `msg_type` | Direction | Purpose |
|---:|---|---|
| 3 | Cloud/Parodus -> Service | Request (`EXEC`, `DESCRIBE`, `HEALTH`, optionally `PUSH`) |
| 3 | Service -> Cloud/Parodus | Response |
| 3 | Service -> Local endpoint | `CHANGED` notification after successful `PUSH` |
| 3 | Service -> Cloud/Parodus | `capability_sync.updated` notification |
| 9 | Service -> Parodus | Registration |
| 10 | Parodus -> Service | Keepalive ping |
| 10 | Service -> Parodus | Keepalive ack |

## 3) Common type-3 request envelope

Required request keys:

| Field | Type | Required | Behavior |
|---|---|---:|---|
| `msg_type` | integer (positive) | Yes | Must be `3`. |
| `source` | string | Yes | Request origin route. |
| `dest` | string | Yes | Usually `dns:multi-plane-runtime-manager`. |
| `transaction_uuid` | string | Yes | Correlation ID. |
| `payload` | bin or string | Yes | Raw bytes of msgpack inner payload map. |

Optional envelope metadata keys:

| Field | Type |
|---|---|
| `plane` | string |
| `plane_type` | string |
| `request_type` | string |
| `request_sub_type` | string |

Notes:
- Unknown envelope keys are ignored.
- Metadata keys with wrong type can trigger metadata rejection when metadata checks are enabled.

## 4) Request kinds and payload format

### 4.1 `EXEC` (default kind)

`EXEC` is selected when:
- `kind` is absent, or
- `kind` is `"EXEC"`.

Inner payload JSON shape:

```json
{
  "tool": "device_uptime",
  "command": "",
  "plane": "triage",
  "static": true,
  "dynamic": false,
  "metadata": {
    "ticket": "INC-12345"
  }
}
```

Field rules:

| Field | Type | Required | Parser behavior |
|---|---|---:|---|
| `tool` | string | Yes | Missing => request dropped (no command execution). |
| `command` | string | No | Optional override. |
| `plane` | string | No | Optional plane pin for lookup. |
| `static` | boolean | No | Parsed when metadata logic enabled. |
| `dynamic` | boolean | No | Parsed when metadata logic enabled. |
| `metadata` | map | No | Must be map if present. |

### 4.2 `DESCRIBE`

Inner payload JSON shape:

```json
{ "kind": "DESCRIBE", "plane": "triage" }
```

- `plane` is optional.
- If omitted, service returns all loaded planes.

### 4.3 `HEALTH`

Inner payload JSON shape:

```json
{ "kind": "HEALTH" }
```

No required fields besides `kind`.

### 4.4 `PUSH`

Inner payload JSON shape:

```json
{
  "kind": "PUSH",
  "plane": "triage",
  "base_version": 12,
  "target_version": 13,
  "diff": {
    "added": {},
    "removed": [],
    "modified": {}
  },
  "auth_token": "optional-shared-token"
}
```

Field rules:

| Field | Type | Required | Parser behavior |
|---|---|---:|---|
| `kind` | string | Yes | Must be `"PUSH"`. |
| `plane` | string | Yes | Missing => malformed push reject. |
| `base_version` | integer | No | Defaults to `0` if absent. |
| `target_version` | integer | No | Defaults to `0` if absent. |
| `diff` | map | No | If absent, treated as empty object. |
| `auth_token` | string | No | Required only when server configured with expected token. |

Transport/security behavior:
- By default, `PUSH` is accepted only from local endpoint.
- Cloud/public `PUSH` is rejected unless local-only policy is explicitly disabled.
- Rate-limit and payload-size guardrails can reject based on runtime config.

## 5) Type-3 response envelope

Response envelope keys:

| Field | Type | Value |
|---|---|---|
| `msg_type` | integer | `3` |
| `source` | string | Request `dest` (fallback `dns:multi-plane-runtime-manager`) |
| `dest` | string | Request `source` |
| `transaction_uuid` | string | Echoed from request |
| `content_type` | string | `application/msgpack` |
| `payload` | bin | Msgpack bytes of response payload |

## 6) Response payloads by kind

### 6.1 `EXEC` response payload

```json
{
  "tool": "device_uptime",
  "exit_code": 0,
  "stdout": "<binary-or-text-output>"
}
```

Optional key (when metadata echo enabled):

```json
{
  "metadata_applied": {
    "plane": "ops",
    "plane_type": "exec",
    "request_type": "diagnostic",
    "request_sub_type": "on-demand",
    "static": true,
    "dynamic": false
  }
}
```

### 6.2 `DESCRIBE` response payload

For one plane (`plane` provided):

```json
{
  "plane": "triage",
  "version": 13,
  "tools": [
    { "name": "device_uptime", "type": "static", "plane": "triage", "timeout": 10 }
  ]
}
```

For all planes (`plane` omitted):

```json
[
  {
    "plane": "triage",
    "version": 13,
    "tools": [
      { "name": "device_uptime", "type": "static", "plane": "triage", "timeout": 10 }
    ]
  }
]
```

### 6.3 `HEALTH` response payload

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
    "rejected_transport": 0,
    "rejected_unauthorized": 0,
    "rejected_rate_limit": 0,
    "rejected_payload_too_large": 0,
    "rejected_other": 0,
    "observed_rate_limit": 0,
    "observed_payload_too_large": 0,
    "last_attempt_ms": 0
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

### 6.4 `PUSH` response payload

Success:

```json
{ "status": "loaded", "plane": "triage", "version": 13 }
```

Failure:

```json
{ "status": "rejected", "plane": "triage", "reason": "PUSH authorization failed" }
```

### 6.5 Example output formats (what cloud receives)

This section shows practical output examples for each message flow, including
how `stdout` should be interpreted by cloud clients.

#### 6.5.1 EXEC success (text stdout)

Inner response payload (decoded view):

```json
{
  "tool": "device_uptime",
  "exit_code": 0,
  "stdout": "up 12 days, 01:22"
}
```

Wire note:
- `stdout` is encoded as msgpack **bin** in the payload.
- Cloud client may decode/display it as UTF-8 text when bytes are printable.

#### 6.5.2 EXEC success (binary stdout)

Inner response payload (decoded view):

```json
{
  "tool": "dump_blob",
  "exit_code": 0,
  "stdout": "<raw bytes>"
}
```

Wire note:
- `stdout` remains msgpack **bin**.
- Cloud client should treat bytes as opaque/binary unless tool contract says text.

#### 6.5.3 EXEC failure (tool/runtime error)

Inner response payload (decoded view):

```json
{
  "tool": "unknown_tool",
  "exit_code": 1,
  "stdout": "tool not in catalog"
}
```

Interpretation:
- non-zero `exit_code` means request failed.
- `stdout` carries diagnostic/error text bytes when available.

#### 6.5.4 Metadata policy/type conflict outputs

Examples:

```json
{ "tool": "device_uptime", "exit_code": 2, "stdout": "ERR_METADATA_TYPE" }
```

```json
{ "tool": "device_uptime", "exit_code": 3, "stdout": "ERR_METADATA_CONFLICT" }
```

```json
{ "tool": "device_uptime", "exit_code": 4, "stdout": "ERR_METADATA_POLICY" }
```

#### 6.5.5 DESCRIBE / HEALTH / PUSH output examples

DESCRIBE:

```json
{ "plane": "triage", "version": 13, "tools": [ { "name": "device_uptime", "type": "static", "plane": "triage", "timeout": 10 } ] }
```

HEALTH:

```json
{ "status": "ok", "push_guardrails": { "attempts": 10, "accepted": 9 }, "catalog_db": { "backend_effective": "lmdb", "generation": 5 } }
```

PUSH success:

```json
{ "status": "loaded", "plane": "triage", "version": 14 }
```

PUSH reject:

```json
{ "status": "rejected", "plane": "triage", "reason": "PUSH rate-limited" }
```

## 7) Unsolicited notifications

### 7.1 `CHANGED` (msgpack payload)

Emitted after successful `PUSH` promote:

```json
{ "kind": "CHANGED", "plane": "triage", "version": 13 }
```

Sent as WRP type-3 with msgpack payload.

### 7.2 `capability_sync.updated` (JSON payload)

Sent to cloud via Parodus as WRP type-3 with `content_type = application/json`.

JSON payload shape:

```json
{
  "jsonrpc": "2.0",
  "method": "capability_sync.updated",
  "params": {
    "toolset": "diagnostics",
    "version": "13",
    "capabilities": [
      { "name": "device_uptime", "type": "static", "plane": "triage", "timeout": 10 }
    ]
  }
}
```

## 8) MsgPack cross-verification checklist

For cloud-side implementation, verify before send:

1. Outer envelope is a msgpack map.
2. `msg_type` is encoded as positive integer `3`.
3. `source`, `dest`, `transaction_uuid` are strings.
4. `payload` contains bytes of a msgpack map (for request kinds).
5. Inner key names are exact and case-sensitive: `kind`, `tool`, `command`, `plane`, `base_version`, `target_version`, `diff`, `auth_token`.
6. Integer fields are msgpack integers.
7. Boolean fields (`static`, `dynamic`) are msgpack booleans.
8. `diff` is msgpack map when present.

## 9) Compatibility notes

- Unknown keys are ignored by request parsers.
- Legacy EXEC clients (no `kind`, only `tool`/`command`) remain supported.
- `PUSH` availability depends on runtime policy and endpoint origin.
