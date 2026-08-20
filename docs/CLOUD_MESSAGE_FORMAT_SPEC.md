# Cloud Message Format Specification

Version: 1.0  
Date: 2026-08-19  
Applies to source baseline: `multi-plane-runtime-manager.c` + `metadata_fields.c`

## 1. Scope

This document defines the cloud-facing request/response wire format currently implemented by Multi-Plane Runtime Manager.

Transport path:
- Cloud -> Parodus -> Multi-Plane Runtime Manager (request)
- Multi-Plane Runtime Manager -> Parodus -> Cloud (response)

Encoding:
- Outer WRP envelope: msgpack map
- Inner payload: msgpack map

## 2. Feature Modes

Metadata behavior depends on build/runtime flags:

1. Metadata feature OFF (default build)
- Build option: `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=OFF`
- Metadata keys are ignored.

2. Metadata feature ON (enhanced)
- Build option: `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=ON`
- Metadata keys are parsed/validated/policy-applied.
- Strict enum validation is controlled by env var:
  - `MULTI_PLANE_RUNTIME_MANAGER_METADATA_STRICT=1|true|yes|on`

## 3. Request Format

### 3.1 Outer WRP Request Envelope (required keys)

| Field | Type | Required | Behavior |
|---|---|---:|---|
| `msg_type` | integer | Yes | Must be `3` for diagnostic requests. |
| `source` | string | Yes | Caller identity/route. |
| `dest` | string | Yes | Destination route (typically `dns:multi-plane-runtime-manager`). |
| `transaction_uuid` | string | Yes | Correlation ID. |
| `payload` | bin or string | Yes | Msgpack bytes of inner request payload. |

Notes:
- Unknown outer keys are tolerated.
- `content_type` is not required on inbound requests.

### 3.2 Outer Metadata Keys (optional)

These keys are only interpreted when metadata feature is ON:

| Field | Type | Required | Rules |
|---|---|---:|---|
| `plane` | string | No | In strict mode: one of `data`, `control`, `ops`, `diagnostic`. |
| `plane_type` | string | No | In strict mode: one of `read`, `write`, `exec`, `observe`. |
| `request_type` | string | No | Free-form, type-checked as string. |
| `request_sub_type` | string | No | Free-form, type-checked as string. |

Type mismatch on these fields triggers metadata validation failure when metadata feature is ON.

### 3.3 Inner Request Payload (required/optional keys)

| Field | Type | Required | Behavior |
|---|---|---:|---|
| `tool` | string | Yes | Tool key to resolve in catalog. |
| `command` | string | No | Optional override command. If missing/empty, catalog command is used. |

If `tool` is missing, request is dropped and no tool execution occurs.

### 3.4 Inner Payload Metadata Keys (optional)

These keys are only interpreted when metadata feature is ON:

| Field | Type | Required | Rules |
|---|---|---:|---|
| `static` | boolean | No | `true` disables command override (catalog command path). |
| `dynamic` | boolean | No | `false` rejects explicit command override. |
| `metadata` | map | No | Presence supported; non-map type is invalid. |

Conflict rule:
- `static=true` and `dynamic=true` => invalid request.

## 4. Policy and Validation Behavior (Metadata ON)

Validation stages:
1. Type checks on metadata fields.
2. Conflict checks (`static=true` and `dynamic=true`).
3. Optional strict enum checks for `plane`, `plane_type`.
4. Policy checks for command override behavior.

Policy outcomes:
- `static=true`: force catalog command path.
- `dynamic=false` with explicit `command`: reject.
- `dynamic=true`: allow explicit `command` (still subject to existing blocked-command checks).

## 5. Response Format

### 5.1 Outer WRP Response Envelope

| Field | Type | Value/Behavior |
|---|---|---|
| `msg_type` | integer | Always `3` |
| `source` | string | Request `dest` (fallback `dns:multi-plane-runtime-manager`) |
| `dest` | string | Request `source` |
| `transaction_uuid` | string | Echo of request UUID |
| `content_type` | string | `application/msgpack` |
| `payload` | bin | Msgpack bytes of inner response payload |

### 5.2 Inner Response Payload

| Field | Type | Behavior |
|---|---|---|
| `tool` | string | Tool name or `unknown` fallback |
| `exit_code` | integer | Command exit or metadata/policy error code |
| `stdout` | bin | Command output or error token text bytes |

`stdout` is capped to 64 KiB for command execution output.

## 6. Metadata Error Contract (Metadata ON)

When metadata validation/policy fails, response uses:

| Condition | `exit_code` | `stdout` token |
|---|---:|---|
| Metadata type error | 2 | `ERR_METADATA_TYPE` |
| Metadata conflict | 3 | `ERR_METADATA_CONFLICT` |
| Metadata policy reject | 4 | `ERR_METADATA_POLICY` |

## 7. Message Type Reference

| `msg_type` | Direction | Purpose |
|---:|---|---|
| 3 | Cloud/Parodus -> Multi-Plane Runtime Manager | Diagnostic request |
| 3 | Multi-Plane Runtime Manager -> Parodus/Cloud | Diagnostic response |
| 9 | Multi-Plane Runtime Manager -> Parodus | Service registration |
| 10 | Parodus -> Multi-Plane Runtime Manager | Keepalive |
| 10 | Multi-Plane Runtime Manager -> Parodus | Keepalive acknowledgment |

## 8. Examples for Every Message Type

All examples are logical JSON views for readability. On wire they are msgpack maps.

### 8.1 Type 9: Service registration (Multi-Plane Runtime Manager -> Parodus)

```json
{
  "msg_type": 9,
  "service_name": "multi-plane-runtime-manager",
  "url": "tcp://127.0.0.1:6669"
}
```

### 8.2 Type 10: Keepalive ping (Parodus -> Multi-Plane Runtime Manager)

```json
{
  "msg_type": 10
}
```

### 8.3 Type 10: Keepalive ack (Multi-Plane Runtime Manager -> Parodus)

```json
{
  "msg_type": 10
}
```

### 8.4 Type 3: Diagnostic request (minimal/legacy)

Outer envelope:

```json
{
  "msg_type": 3,
  "source": "mac:001122334455/ops",
  "dest": "dns:multi-plane-runtime-manager",
  "transaction_uuid": "550e8400-e29b-41d4-a716-446655440000",
  "payload": "<msgpack bytes of inner payload>"
}
```

Inner payload:

```json
{
  "tool": "device_uptime",
  "command": ""
}
```

### 8.5 Type 3: Diagnostic request (metadata enabled)

Outer envelope:

```json
{
  "msg_type": 3,
  "source": "mac:001122334455/ops",
  "dest": "dns:multi-plane-runtime-manager",
  "transaction_uuid": "550e8400-e29b-41d4-a716-446655440001",
  "plane": "ops",
  "plane_type": "exec",
  "request_type": "diagnostic",
  "request_sub_type": "on-demand",
  "payload": "<msgpack bytes of inner payload>"
}
```

Inner payload:

```json
{
  "tool": "device_uptime",
  "command": "",
  "static": true,
  "dynamic": false,
  "metadata": {
    "ticket": "INC-12345"
  }
}
```

### 8.6 Type 3: Success response (Multi-Plane Runtime Manager -> Parodus/Cloud)

Outer envelope:

```json
{
  "msg_type": 3,
  "source": "dns:multi-plane-runtime-manager",
  "dest": "mac:001122334455/ops",
  "transaction_uuid": "550e8400-e29b-41d4-a716-446655440000",
  "content_type": "application/msgpack",
  "payload": "<msgpack bytes of inner response payload>"
}
```

Inner payload:

```json
{
  "tool": "device_uptime",
  "exit_code": 0,
  "stdout": "<binary stdout bytes>"
}
```

### 8.7 Type 3: Metadata type error response

```json
{
  "tool": "device_uptime",
  "exit_code": 2,
  "stdout": "ERR_METADATA_TYPE"
}
```

### 8.8 Type 3: Metadata conflict error response

```json
{
  "tool": "device_uptime",
  "exit_code": 3,
  "stdout": "ERR_METADATA_CONFLICT"
}
```

### 8.9 Type 3: Metadata policy error response

```json
{
  "tool": "device_uptime",
  "exit_code": 4,
  "stdout": "ERR_METADATA_POLICY"
}
```

### 8.10 Type 3: Tool-not-found response example

```json
{
  "tool": "unknown_tool",
  "exit_code": 1,
  "stdout": "tool not in catalog"
}
```

## 9. Backward Compatibility

Legacy clients remain compatible:
- Requests with only `msg_type/source/dest/transaction_uuid/payload` and `tool/command` continue to work.
- Extra unknown keys remain tolerated.

## 10. Security Notes

Metadata does not bypass existing security checks:
- Tool must resolve in catalog.
- Existing blocked-command gate still applies.
- Output cap remains enforced.
