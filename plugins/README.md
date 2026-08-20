# Multi-Plane Runtime Manager Plugin Author Guide

This plugin ABI follows an rpcd-style model:
- host process loads `.so` at runtime using `dlopen`
- plugin exports fixed symbol contract
- plugin registers one or more tool names
- host dispatches by requested tool name

## Required symbols

- `int diag_plugin_get_api_version(void)`
- `int diag_plugin_init(const diag_host_api_t *host, diag_plugin_ctx_t **ctx)`
- `size_t diag_plugin_get_tool_count(diag_plugin_ctx_t *ctx)`
- `int diag_plugin_get_tool(diag_plugin_ctx_t *ctx, size_t idx, diag_tool_def_t *out)`
- `int diag_plugin_invoke(diag_plugin_ctx_t *ctx, const diag_invoke_req_t *req, diag_invoke_resp_t *resp)`
- `void diag_plugin_deinit(diag_plugin_ctx_t *ctx)`

See [plugin_api.h](../plugin_api.h).

## Example plugin walkthrough

Reference file:
- [sample_multi_plane_runtime_manager_plugin.c](sample_multi_plane_runtime_manager_plugin.c)

Key behaviors in the sample:
- Exports two tools: `plugin_echo`, `plugin_version`
- `plugin_version` returns a fixed version text (`sample-plugin-v1`)
- `plugin_echo` returns request-source-tagged output (`plugin-echo:<source>`)
- Demonstrates use of host logger callback in init

How to extend:
1. Add new tool name to the tool table.
2. Expose it in `diag_plugin_get_tool()`.
3. Add branch handling in `diag_plugin_invoke()`.
4. Return stable output and deterministic exit codes.

## Plugin update flow (developer)

1. Modify plugin code.
2. Build and validate in Linux container.
3. Deploy `.so` to target plane plugin directory.
4. Trigger reload (poll/notify/manual `SIGHUP`).
5. Validate from cloud with explicit `plane` and `tool`.

Companion guide:
- [../docs/guides/PLUGIN_DEVELOPER_UPDATE_GUIDE.md](../docs/guides/PLUGIN_DEVELOPER_UPDATE_GUIDE.md)

## Invocation contract

`diag_plugin_invoke()` should:
- return `0` on success
- set `resp->exit_code`
- set `resp->out` + `resp->out_len` (text/bytes)

For failure paths:
- return non-zero to let host map to deterministic error response.

## Safety notes

- Plugins should be thread-safe for concurrent invocations.
- Keep `diag_plugin_deinit()` idempotent and fast.
- Do not assume ownership of host memory passed in request fields.
- Keep outputs bounded and avoid global mutable state unless synchronized.
