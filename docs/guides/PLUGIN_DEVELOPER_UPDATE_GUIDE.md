# Plugin Developer Update Guide (New Developers)

This guide explains how to create, update, deploy, and validate dynamic plugins in Multi-Plane Runtime Manager.

## 1) Start from the sample plugin

Reference implementation:
- `plugins/sample_multi_plane_runtime_manager_plugin.c`

Sample plugin exports two tools:
- `plugin_echo`
- `plugin_version`

What it demonstrates:
- ABI compatibility declaration
- Plugin init/deinit lifecycle
- Tool enumeration for registration
- Tool invocation and output contract

## 2) Required ABI symbols (must keep exact names)

- `diag_plugin_get_api_version()`
- `diag_plugin_init()`
- `diag_plugin_get_tool_count()`
- `diag_plugin_get_tool()`
- `diag_plugin_invoke()`
- `diag_plugin_deinit()`

If symbols are missing or API version mismatches, host returns deterministic plugin error tokens.

## 3) Coding model

### 3.1 Init phase

- Store `diag_host_api_t` pointer in plugin context if needed.
- Allocate plugin context once in `diag_plugin_init()`.

### 3.2 Tool registration phase

- Return number of tools via `diag_plugin_get_tool_count()`.
- Return each tool definition via `diag_plugin_get_tool()`.

### 3.3 Invocation phase

- Branch by `req->tool` in `diag_plugin_invoke()`.
- Set:
  - `resp->exit_code`
  - `resp->out`
  - `resp->out_len`
- Return `0` on success, non-zero on failure.

### 3.4 Deinit phase

- Free context resources in `diag_plugin_deinit()`.
- Keep deinit idempotent and safe.

## 4) Plane-aware deployment strategy

Per-plane plugin directories can isolate operational risk:
- `.../plugins/triage`
- `.../plugins/management`
- `.../plugins/control`
- `.../plugins/config-apply`

Recommended placement:
- Observability helpers → `triage`
- Routine ops helpers → `management`
- Runtime behavior toggles → `control`
- Configuration mutation helpers → `config-apply`

## 5) Plugin update workflow

1. Update plugin source.
2. Build shared object in Linux environment.
3. Deploy to target plane directory.
4. Trigger plugin reload:
   - poll interval-based,
   - notify-based,
   - or manual `SIGHUP`.
5. Validate from cloud with explicit `plane` in request.
6. Monitor logs for invoke result and error mapping.

## 6) Testing workflow

Run full suite:

```bash
docker compose run --rm mprm-ci
```

Run daemon smoke workflow:

```bash
docker compose run --rm mprm-dev bash -lc "./scripts/container-smoke-daemon.sh"
```

## 7) Error mapping and troubleshooting

Common deterministic tokens:
- `ERR_PLUGIN_INVOKE`
- `ERR_PLUGIN_UNAVAILABLE`
- `ERR_PLUGIN_API_VERSION`

Checks:
- ABI version matches host expectation
- Tool names exported correctly
- Plugin file path is in configured plane directory
- Conflict policy allows plugin tool ownership

## 8) Best practices

- Keep plugin logic small and deterministic.
- Avoid side effects in `diag_plugin_get_tool()`.
- Bound output sizes and validate pointers.
- Treat `config-apply` plugins as high-risk and gate tightly.
- Prefer explicit cloud `plane` targeting for all plugin tools.

## 9) First-week onboarding checklist (new developers)

### Day 1 — Environment and baseline

- [ ] Build and run Linux container CI:
  - `docker compose run --rm mprm-ci`
- [ ] Run daemon smoke path:
  - `docker compose run --rm mprm-dev bash -lc "./scripts/container-smoke-daemon.sh"`
- [ ] Read plugin ABI contract in `src/plugin_api.h`.

### Day 2 — Understand sample plugin

- [ ] Walk through `plugins/sample_multi_plane_runtime_manager_plugin.c`.
- [ ] Trace lifecycle calls: init → enumerate tools → invoke → deinit.
- [ ] Verify sample tools behavior:
  - `plugin_version`
  - `plugin_echo`

### Day 3 — Build first custom plugin tool

- [ ] Clone sample plugin and add one custom tool.
- [ ] Register tool via `diag_plugin_get_tool_count()` and `diag_plugin_get_tool()`.
- [ ] Implement tool logic in `diag_plugin_invoke()` with stable output.
- [ ] Validate deterministic `exit_code` behavior for success/failure.

### Day 4 — Plane-aware deployment

- [ ] Select target plane (`triage|management|control|config-apply`).
- [ ] Deploy plugin to target plane directory.
- [ ] Trigger plugin reload (poll/notify/manual `SIGHUP`).
- [ ] Verify cloud request uses explicit `plane`.

### Day 5 — Hardening and review

- [ ] Add thread-safety review for shared/static state.
- [ ] Validate output bounds and memory ownership assumptions.
- [ ] Verify conflict policy behavior with existing catalog tools.
- [ ] Capture rollback plan (remove `.so`, reload, confirm fallback).

### Week-1 exit criteria

- [ ] CI green in Linux container.
- [ ] Custom plugin tool runs successfully end-to-end from cloud request.
- [ ] Tool is mapped to correct plane and documented in team runbook.
- [ ] Failure modes tested (`ERR_PLUGIN_*` path observed and understood).
