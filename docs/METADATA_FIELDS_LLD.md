# Multi-Plane Runtime Manager Metadata Fields Low-Level Design (LLD)

Version: 1.0  
Date: 2026-08-19  
Related Requirements: [METADATA_FIELDS_REQUIREMENTS.md](METADATA_FIELDS_REQUIREMENTS.md)  
Related HLD: [METADATA_FIELDS_HLD.md](METADATA_FIELDS_HLD.md)

## 1. Scope

This LLD specifies concrete code-level changes for implementing metadata-field parsing, validation, policy enforcement, and response behavior.

Target source baseline: [multi-plane-runtime-manager.c](../src/multi-plane-runtime-manager.c)

## 2. Proposed File Changes

1. Update [multi-plane-runtime-manager.c](../src/multi-plane-runtime-manager.c)
2. Add [metadata_fields.h](../src/metadata_fields.h)
3. Add [metadata_fields.c](../src/metadata_fields.c)
4. Update [CMakeLists.txt](CMakeLists.txt) to include new source and compile flag

## 3. Data Structures

### 3.1 Request Metadata Structure

```c
typedef struct {
    char *plane;
    char *plane_type;
    char *request_type;
    char *request_sub_type;

    int has_static;
    int static_flag;

    int has_dynamic;
    int dynamic_flag;

    /* optional extension map in raw msgpack/json form; nullable */
    cJSON *metadata_obj;
} req_metadata_t;
```

### 3.2 Extended Request Container

```c
typedef struct {
    int    msg_type;
    char  *source;
    char  *dest;
    char  *transaction_uuid;
    void  *payload;
    size_t payload_len;

    req_metadata_t meta;
} wrp_req_t;
```

## 4. Public Internal APIs (New)

In [metadata_fields.h](../src/metadata_fields.h):

```c
typedef enum {
    META_OK = 0,
    META_ERR_TYPE,
    META_ERR_CONFLICT,
    META_ERR_POLICY
} meta_status_t;

typedef struct {
    int strict_mode;
} meta_cfg_t;

void meta_init(req_metadata_t *m);
void meta_free(req_metadata_t *m);

void meta_decode_envelope_kv(req_metadata_t *m,
                             const msgpack_object_kv *kv);

void meta_decode_payload_kv(req_metadata_t *m,
                            const msgpack_object_kv *kv);

meta_status_t meta_validate(const req_metadata_t *m,
                            const meta_cfg_t *cfg,
                            char *reason,
                            size_t reason_len);

meta_status_t meta_apply_policy(const req_metadata_t *m,
                                const char *incoming_command,
                                int *allow_override,
                                char *reason,
                                size_t reason_len);

const char *meta_error_token(meta_status_t st);
```

## 5. Decode Path Changes

### 5.1 Outer Decoder (`decode_wrp()`)

After existing key checks, add branches for:
- `plane`
- `plane_type`
- `request_type`
- `request_sub_type`

Behavior:
- Accept string values only.
- Ignore unknown keys.
- On type mismatch: mark metadata type error indicator.

### 5.2 Inner Decoder (`decode_request_payload()`)

After existing `tool` and `command` extraction, parse:
- `static` (bool)
- `dynamic` (bool)
- `metadata` (map/object; optional)

Behavior:
- If `static`/`dynamic` are non-boolean, set type error.
- Preserve existing behavior for missing fields.

## 6. Validation and Policy Logic

### 6.1 Validation (`meta_validate()`)

Checks:
1. Type validity flags from decode.
2. Conflict rule: `static=true` and `dynamic=true` => `META_ERR_CONFLICT`.
3. Strict mode enum checks:
   - `plane` in {`data`,`control`,`ops`,`diagnostic`}
   - `plane_type` in {`read`,`write`,`exec`,`observe`}

### 6.2 Policy (`meta_apply_policy()`)

Command-override decision:
- `static=true`: force catalog command (`allow_override=0`).
- `dynamic=false` and incoming command present: `META_ERR_POLICY`.
- `dynamic=true`: `allow_override=1`.
- Unspecified flags: preserve current behavior.

## 7. Request Handling Integration

In `handle_request()` sequence:
1. Decode payload (including metadata).
2. Run `meta_validate()`.
3. If validation fails:
   - build failure response (`exit_code=2`)
   - `stdout=meta_error_token(...)`
   - skip command execution.
4. Run `meta_apply_policy()` before command selection.
5. Continue existing catalog/blocklist/execute flow.

## 8. Error Mapping

| Condition | Status | `exit_code` | `stdout` |
|---|---|---:|---|
| Type mismatch | `META_ERR_TYPE` | 2 | `ERR_METADATA_TYPE` |
| Conflict | `META_ERR_CONFLICT` | 3 | `ERR_METADATA_CONFLICT` |
| Policy violation | `META_ERR_POLICY` | 4 | `ERR_METADATA_POLICY` |

## 9. Logging Changes

Add metadata context to existing logs (where available):
- `plane=%s`
- `plane_type=%s`
- `static=%d/na`
- `dynamic=%d/na`
- `meta_decision=%s`

Add counters (globals):
- `g_meta_type_errors`
- `g_meta_conflicts`
- `g_meta_policy_rejects`

## 10. Memory Management

- `meta_init()` zero-initializes pointers/flags.
- `meta_free()` frees all allocated strings and optional object.
- `wrp_req_free()` must call `meta_free(&req->meta)`.

## 11. Build and Config

### 11.1 CMake

Add option:
- `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS` (default `OFF`)

If ON:
- compile `metadata_fields.c`
- define `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=1`

If OFF:
- retain current behavior (ignore fields).

## 12. Test Design

### 12.1 Unit Tests

- `meta_validate()` truth table coverage.
- `meta_apply_policy()` combinations of flags and command presence.

### 12.2 Integration Tests

1. Legacy request (no metadata) succeeds.
2. `static=true` with command override uses catalog command.
3. `dynamic=false` with command override rejected.
4. `static=true` + `dynamic=true` rejected.
5. strict mode invalid `plane` rejected.

### 12.3 Regression Tests

- Existing blocked-command behavior unchanged.
- Existing tool-not-found behavior unchanged.
- Response envelope fields unchanged.

## 13. Sequence (Low-Level)

1. `nn_recv()` message.
2. `decode_wrp()` populates base fields + envelope metadata.
3. `decode_request_payload()` populates `tool`/`command` + payload metadata.
4. `meta_validate()`.
5. `meta_apply_policy()`.
6. Existing catalog and safety checks.
7. Execute or return metadata error.
8. Build/send response.
9. Free request + metadata memory.

## 14. Implementation Notes

- Keep parser tolerant of unknown keys for forward compatibility.
- Do not use metadata to weaken existing security gates.
- Keep `compat` default until cloud contracts finalize.

## 15. Definition of Done

- Code compiles warning-free with metadata feature OFF and ON.
- All tests in section 12 pass.
- Docs updated: [README.md](../README.md), [METADATA_FIELDS_REQUIREMENTS.md](METADATA_FIELDS_REQUIREMENTS.md), [METADATA_FIELDS_HLD.md](METADATA_FIELDS_HLD.md), [METADATA_FIELDS_LLD.md](METADATA_FIELDS_LLD.md).
