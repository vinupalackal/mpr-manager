# Multi-Plane Runtime Manager Metadata Field Support Requirements

Version: 1.0  
Date: 2026-08-19  
Status: Proposed  
Scope: Add support for request metadata fields currently ignored by runtime

Related design documents:
- [METADATA_FIELDS_HLD.md](METADATA_FIELDS_HLD.md)
- [METADATA_FIELDS_LLD.md](METADATA_FIELDS_LLD.md)

## 1. Purpose

Define requirements to implement and validate metadata fields in Multi-Plane Runtime Manager request processing. The current baseline only parses a minimal set of envelope and payload fields and ignores additional metadata such as `plane`, `plane_type`, `static`, and `dynamic`.

## 2. Background

Current behavior in runtime:
- Outer WRP decode extracts only: `msg_type`, `source`, `dest`, `transaction_uuid`, `payload`.
- Inner payload decode extracts only: `tool`, `command`.
- Extra metadata fields are ignored and not returned in response.

## 3. Goals

1. Parse and validate metadata fields from incoming requests.
2. Apply metadata in policy/authorization and execution decisions.
3. Include metadata handling results in observability logs.
4. Preserve backward compatibility for legacy clients.

## 4. Non-Goals

- Redesign of transport protocol.
- Cloud-side UI or orchestration behavior.
- Changing existing required request keys (`msg_type`, `source`, `dest`, `transaction_uuid`, `payload`, `tool`).

## 5. Field Definitions (Target)

### 5.1 Outer Envelope Metadata

| Field | Type | Required | Allowed Values | Notes |
|---|---|---:|---|---|
| `plane` | string | No | `data`, `control`, `ops`, `diagnostic` | High-level request plane selector. |
| `plane_type` | string | No | `read`, `write`, `exec`, `observe` | Semantic subtype within plane. |
| `request_type` | string | No | implementation-defined | Optional cloud classification. |
| `request_sub_type` | string | No | implementation-defined | Optional subtype detail. |

### 5.2 Inner Payload Metadata

| Field | Type | Required | Allowed Values | Notes |
|---|---|---:|---|---|
| `static` | bool | No | `true`, `false` | Indicates static (catalog-driven) request path. |
| `dynamic` | bool | No | `true`, `false` | Indicates dynamic (caller-override) request path. |
| `metadata` | map | No | key/value | Optional extension map for future tags. |

### 5.3 Conflict Rules

- If both `static=true` and `dynamic=true`, request is invalid.
- If both `static` and `dynamic` are omitted, behavior defaults to current path:
  - command override allowed if present,
  - otherwise catalog command fallback.
- If `dynamic=false` and `command` is provided, command override must be rejected.

## 6. Functional Requirements

### FR-META-001 Parse Envelope Metadata
The service shall parse optional envelope fields `plane`, `plane_type`, `request_type`, and `request_sub_type` when present.

Acceptance:
- Valid strings are captured into request context.
- Unknown extra keys remain tolerated (forward compatibility).

### FR-META-002 Parse Payload Metadata
The service shall parse optional payload fields `static`, `dynamic`, and `metadata` when present.

Acceptance:
- Boolean values parse correctly.
- Non-boolean `static`/`dynamic` values are rejected with validation error response.

### FR-META-003 Metadata Validation
The service shall validate metadata consistency before command execution.

Acceptance:
- Conflicting flags (`static=true` and `dynamic=true`) are rejected.
- Invalid `plane`/`plane_type` values are rejected (if strict mode enabled).

### FR-META-004 Execution Policy Enforcement
The service shall apply metadata to command resolution rules.

Acceptance:
- `dynamic=false` disallows request command override.
- `static=true` forces catalog command path.
- `dynamic=true` allows command override subject to existing safety gates.

### FR-META-005 Response Metadata Echo (Optional)
The service should echo accepted metadata in response payload under `metadata_applied`.

Acceptance:
- Response payload includes normalized metadata values when feature is enabled.

### FR-META-006 Error Handling
The service shall return deterministic error payloads for metadata validation failures.

Acceptance:
- `exit_code` non-zero.
- `stdout` includes structured error token, e.g., `ERR_METADATA_VALIDATION`.

### FR-META-007 Backward Compatibility
The service shall remain compatible with legacy requests that omit all new metadata fields.

Acceptance:
- Existing request examples continue to execute unchanged.

### FR-META-008 Logging and Observability
The service shall log parsed metadata and decisions.

Acceptance:
- Logs include `transaction_uuid`, `tool`, `plane`, `plane_type`, `static`, `dynamic`, and policy decision.

### FR-META-009 Feature Flag
Metadata enforcement shall be protected by a compile-time or runtime feature flag.

Acceptance:
- Disabled mode keeps current behavior (ignore metadata).
- Enabled mode enforces parsing and validation requirements.

## 7. Non-Functional Requirements

### NFR-META-001 Performance
Metadata parsing and validation should add negligible overhead (<1 ms typical per request on target hardware).

### NFR-META-002 Reliability
Invalid metadata must not crash process or leak memory.

### NFR-META-003 Security
Metadata shall not bypass existing blocked-command checks or tool allowlist/catalog checks.

### NFR-META-004 Maintainability
Implementation should isolate metadata parsing/validation into dedicated helper functions and structs.

## 8. Data Model Additions

Recommended request context additions:
- `char *plane`
- `char *plane_type`
- `char *request_type`
- `char *request_sub_type`
- `int has_static`
- `int has_dynamic`
- `int static_flag`
- `int dynamic_flag`
- `cJSON *metadata` (or equivalent map representation)

## 9. API/Schema Behavior

### 9.1 Validation Modes
- **Compatibility mode (default)**: parse metadata if present; only enforce critical conflicts.
- **Strict mode**: enforce full allowlist for `plane` and `plane_type`, type validation, and conflict rules.

### 9.2 Error Contract (Recommended)
For metadata validation errors, include:
- `tool`
- `exit_code` = non-zero
- `stdout` containing compact structured token:
  - `ERR_METADATA_TYPE`
  - `ERR_METADATA_CONFLICT`
  - `ERR_METADATA_POLICY`

## 10. Implementation Plan (High Level)

1. Extend decoded request struct with metadata fields.
2. Extend outer envelope decoder for `plane`, `plane_type`, `request_type`, `request_sub_type`.
3. Extend payload decoder for `static`, `dynamic`, and optional `metadata` map.
4. Add validation helper (`validate_metadata()`), returning code and reason.
5. Integrate metadata policy checks into command resolution path.
6. Add response error mapping and optional metadata echo.
7. Add logs and counters for metadata parse/validation outcomes.
8. Gate behavior with feature flag (compile-time and/or runtime config).

## 11. Verification Matrix

| Requirement | Verification Method | Evidence | Test Cases |
|---|---|---|---|
| FR-META-001 | Unit tests (envelope decode) | Parsed fields present in request context | TC-META-001 |
| FR-META-002 | Unit tests (payload decode) | Boolean parse pass/fail cases | TC-META-002 |
| FR-META-003 | Negative tests | Conflict and invalid-type errors returned | TC-META-003, TC-META-004 |
| FR-META-004 | Integration tests | Command path changes by metadata flags | TC-META-005, TC-META-006, TC-META-007 |
| FR-META-005 | Integration tests | Response includes `metadata_applied` | TC-META-008 |
| FR-META-006 | Contract tests | Error tokens and non-zero exit codes | TC-META-002, TC-META-003, TC-META-004 |
| FR-META-007 | Regression tests | Legacy payload behavior unchanged | TC-META-009 |
| FR-META-008 | Log validation tests | Expected metadata log fields present | TC-META-010 |
| FR-META-009 | Feature-flag tests | Enabled vs disabled behavior differs as expected | TC-META-011 |
| NFR-META-001 | Perf benchmark | Mean latency delta within threshold | TC-META-012 |
| NFR-META-002 | Soak/fuzz tests | No crash/leak with malformed metadata | TC-META-012 |
| NFR-META-003 | Security tests | Blocklist and catalog checks still enforced | TC-META-007 |

## 12. Detailed Test Cases

### TC-META-001 Envelope metadata parsing (valid)
- Requirements: FR-META-001
- Input:
  - Envelope contains `plane=diagnostic`, `plane_type=exec`, `request_type=triage`, `request_sub_type=manual`.
  - Valid payload with known tool.
- Expected:
  - Request context captures all envelope metadata fields.
  - Unknown unrelated envelope fields do not break processing.

### TC-META-002 Payload metadata parsing (type checks)
- Requirements: FR-META-002, FR-META-006
- Input variants:
  1. `static=true`, `dynamic=false`
  2. `static="yes"` (invalid type)
  3. `dynamic=1` (invalid type)
- Expected:
  - Variant 1 parses successfully.
  - Variants 2 and 3 return non-zero `exit_code` with `ERR_METADATA_TYPE`.

### TC-META-003 Conflict validation
- Requirements: FR-META-003, FR-META-006
- Input: payload sets `static=true` and `dynamic=true` together.
- Expected:
  - Request is rejected before command execution.
  - Response contains non-zero `exit_code` and `ERR_METADATA_CONFLICT`.

### TC-META-004 Strict-mode enum enforcement
- Requirements: FR-META-003, FR-META-009
- Precondition: metadata strict mode enabled.
- Input: envelope uses invalid `plane=foo`, `plane_type=bar`.
- Expected:
  - Request rejected with `ERR_METADATA_POLICY` (or strict validation token).
  - Same request in compatibility mode is tolerated unless critical conflict exists.

### TC-META-005 `dynamic=false` blocks override
- Requirements: FR-META-004
- Input: payload includes `dynamic=false` and `command="cat /proc/version"`.
- Expected:
  - Override is rejected (or ignored per final policy) and deterministic decision is logged.
  - Command execution follows non-dynamic policy path.

### TC-META-006 `static=true` forces catalog path
- Requirements: FR-META-004
- Input: payload includes `static=true` and a different override command.
- Expected:
  - Effective command comes from catalog tool definition.
  - Caller override does not alter execution target.

### TC-META-007 `dynamic=true` allows override with existing safety gates
- Requirements: FR-META-004, NFR-META-003
- Input:
  1. `dynamic=true`, command is non-blocklisted.
  2. `dynamic=true`, command is blocklisted (e.g., `reboot`).
- Expected:
  - Variant 1 enters dynamic override path.
  - Variant 2 is still rejected by blocklist/catalog safety checks.

### TC-META-008 Optional metadata echo
- Requirements: FR-META-005
- Precondition: metadata echo feature enabled.
- Input: valid metadata request.
- Expected:
  - Response payload includes `metadata_applied` with normalized values.
  - Disabled mode omits `metadata_applied`.

### TC-META-009 Backward compatibility regression
- Requirements: FR-META-007, FR-META-009
- Input: legacy request with only baseline fields (`tool`, optional `command`).
- Expected:
  - Behavior matches pre-feature baseline in compatibility mode.
  - No new mandatory fields introduced.

### TC-META-010 Metadata logging completeness
- Requirements: FR-META-008
- Input: valid request with all metadata fields set.
- Expected:
  - Logs include `transaction_uuid`, `tool`, `plane`, `plane_type`, `static`, `dynamic`, and decision outcome.

### TC-META-011 Feature-flag mode switching
- Requirements: FR-META-009
- Steps:
  1. Run with enforcement disabled.
  2. Replay invalid metadata corpus.
  3. Run with enforcement enabled and replay same corpus.
- Expected:
  - Disabled mode preserves ignore-compatible behavior.
  - Enabled mode enforces parse/validate policy and returns deterministic errors.

### TC-META-012 Performance and robustness
- Requirements: NFR-META-001, NFR-META-002
- Steps:
  1. Run request benchmark with and without metadata fields.
  2. Run malformed metadata fuzz/soak workload.
- Expected:
  - Mean latency increase remains within target threshold.
  - No crash, no unbounded memory growth, no leaks.

## 13. Risks and Open Questions

1. Canonical enum values for `plane` and `plane_type` require cloud/platform alignment.
2. Whether metadata should be echoed in response may impact payload size/privacy.
3. Strict mode rollout may reject legacy clients using non-standard values.
4. Runtime config source for strict/compat mode must be defined.

## 14. Acceptance Criteria for Delivery

The feature is complete when:
- Metadata fields are parsed and validated per this document.
- Backward compatibility is preserved in default mode.
- A feature flag can disable enforcement.
- Tests cover positive, negative, and regression scenarios.
- Documentation is updated with final field schema and examples.
