# Multi-Plane Runtime Manager Metadata Fields High-Level Design (HLD)

Version: 1.0  
Date: 2026-08-19  
Related Requirements: [METADATA_FIELDS_REQUIREMENTS.md](METADATA_FIELDS_REQUIREMENTS.md)

## 1. Objective

Implement metadata-field support for incoming requests while preserving backward compatibility.

New/target metadata fields:
- Envelope: `plane`, `plane_type`, `request_type`, `request_sub_type`
- Payload: `static`, `dynamic`, `metadata` (map)

## 2. Design Principles

1. **Backward-compatible by default**
   - Existing request flow continues unchanged when metadata fields are absent.
2. **Feature-gated enforcement**
   - Parsing can be always-on; strict validation/policy enforcement controlled by flag.
3. **Fail-safe behavior**
   - Invalid metadata yields deterministic non-zero response; no process crash.
4. **Separation of concerns**
   - Decode, validation, policy, and response mapping handled in distinct modules.

## 3. Architecture Overview

### 3.1 Current Flow (Baseline)
1. Receive WRP message.
2. Decode outer envelope.
3. Decode inner payload (`tool`, `command`).
4. Resolve command from request/catalog.
5. Execute command and respond.

### 3.2 Target Flow (With Metadata)
1. Receive WRP message.
2. Decode outer envelope + optional metadata fields.
3. Decode inner payload + optional metadata fields.
4. Run metadata validation.
5. Run metadata policy application to command-resolution strategy.
6. Execute (or reject) and send response with deterministic outcome.

## 4. Component-Level Design

### 4.1 Decoder Layer
- Extends outer and inner decode logic to parse metadata fields.
- Stores parsed values in a unified request context.

### 4.2 Validation Layer
- Validates data types and conflict rules:
  - invalid type for `static`/`dynamic`
  - mutually true `static=true` and `dynamic=true`
- Optional strict enum checks for `plane` and `plane_type`.

### 4.3 Policy Layer
- Applies metadata to command resolution:
  - `static=true` → force catalog command
  - `dynamic=false` + command override provided → reject override
  - `dynamic=true` → allow override, still pass existing blocked-command checks

### 4.4 Response Mapper
- Maps metadata errors to deterministic failure payload (`exit_code != 0`, standardized `stdout` token).
- Optional response metadata echo (feature option).

### 4.5 Observability Layer
- Emits structured log entries containing:
  - `transaction_uuid`, `tool`, `plane`, `plane_type`, `static`, `dynamic`, decision result.
- Tracks counters for parse errors, validation failures, policy rejects.

## 5. Feature Flags and Modes

### 5.1 Compile-Time Flag
- `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS` (proposed)

### 5.2 Runtime Mode
- `compat` (default): minimal enforcement, preserve current behavior.
- `strict`: enforce enums/type/conflict/policy rules.

## 6. Data Contracts (Logical)

### 6.1 Envelope Additions
- `plane`: string
- `plane_type`: string
- `request_type`: string
- `request_sub_type`: string

### 6.2 Payload Additions
- `static`: bool
- `dynamic`: bool
- `metadata`: map<string, value>

## 7. Error Handling Strategy

Standardized tokens in response `stdout`:
- `ERR_METADATA_TYPE`
- `ERR_METADATA_CONFLICT`
- `ERR_METADATA_POLICY`

All metadata failures:
- Non-zero `exit_code`
- Valid WRP response envelope retained
- `transaction_uuid` preserved for correlation

## 8. Security Considerations

- Metadata must not bypass existing catalog allowlist or blocked-command checks.
- Unknown metadata keys tolerated (forward compatibility) but not trusted for authorization.
- Strict mode should rely on explicit allowlists for enum fields.

## 9. Performance Considerations

- Metadata parsing/validation is O(number of map fields).
- No additional network round trips.
- Expected latency overhead: negligible in normal payload sizes.

## 10. Rollout Plan

1. Add decode support + logging in compatibility mode.
2. Add validation helper and policy application logic.
3. Add strict mode and feature flag controls.
4. Run regression tests for legacy payloads.
5. Enable strict mode per environment after cloud alignment.

## 11. Acceptance Summary

HLD is accepted when:
- All new metadata fields have defined ownership in decode/validate/policy/response layers.
- Backward compatibility and gating are explicitly defined.
- Rollout path supports phased enablement.
