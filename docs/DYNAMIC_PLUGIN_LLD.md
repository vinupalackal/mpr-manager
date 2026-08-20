# Multi-Plane Runtime Manager Dynamic Plugin Loading Low-Level Design (LLD)

Version: 1.0  
Date: 2026-08-19  
Related requirements: [DYNAMIC_PLUGIN_REQUIREMENTS.md](DYNAMIC_PLUGIN_REQUIREMENTS.md)  
Related HLD: [DYNAMIC_PLUGIN_HLD.md](DYNAMIC_PLUGIN_HLD.md)

> **Implementation gap closure (2026-08-19)**
>
> To avoid unnecessary module fragmentation for v1, watcher behavior is
> implemented inside `plugin_manager.c` as an internal poll thread.
> `plugin_watcher.*` remains optional for later refactor only.

## 1. Scope

This LLD defines code-level design to implement runtime `.so` discovery, load/unload/reload, plugin tool registration, and plugin dispatch.

## 2. Proposed File Additions

1. [plugin_api.h](../src/plugin_api.h)
2. [plugin_manager.h](../src/plugin_manager.h)
3. [plugin_manager.c](../src/plugin_manager.c)
4. [tool_registry.h](../src/tool_registry.h)
5. [tool_registry.c](../src/tool_registry.c)
6. [plugins/README.md](../plugins/README.md) (plugin author guide)

## 3. Proposed File Updates

1. [multi-plane-runtime-manager.c](../src/multi-plane-runtime-manager.c)
2. [CMakeLists.txt](CMakeLists.txt)
3. [README.md](../README.md)

## 4. Core Data Structures

## 4.1 Plugin API Types (`plugin_api.h`)

```c
#define DIAG_PLUGIN_API_VERSION 1

typedef struct diag_plugin_ctx diag_plugin_ctx_t;

typedef struct {
    void (*log_fn)(int level, const char *fmt, ...);
    const char *(*get_config_fn)(const char *key);
} diag_host_api_t;

typedef struct {
    const char *tool_name;
    const char *tool_desc;
    uint32_t flags;
} diag_tool_def_t;

typedef struct {
    const char *transaction_uuid;
    const char *source;
    const char *tool;
    const void *payload;
    size_t payload_len;
} diag_invoke_req_t;

typedef struct {
    int exit_code;
    const void *out;
    size_t out_len;
} diag_invoke_resp_t;
```

Required plugin symbols:

```c
int diag_plugin_get_api_version(void);
int diag_plugin_init(const diag_host_api_t *host, diag_plugin_ctx_t **ctx);
size_t diag_plugin_get_tool_count(diag_plugin_ctx_t *ctx);
int diag_plugin_get_tool(diag_plugin_ctx_t *ctx, size_t idx, diag_tool_def_t *out);
int diag_plugin_invoke(diag_plugin_ctx_t *ctx, const diag_invoke_req_t *req, diag_invoke_resp_t *resp);
void diag_plugin_deinit(diag_plugin_ctx_t *ctx);
```

## 4.2 Plugin Record (`plugin_manager.c`)

```c
typedef enum {
    PLUGIN_DISCOVERED,
    PLUGIN_LOADING,
    PLUGIN_ACTIVE,
    PLUGIN_DRAINING,
    PLUGIN_UNLOADED,
    PLUGIN_FAILED
} plugin_state_t;

typedef struct {
    char *path;
    char *id;
    void *dl_handle;
    plugin_state_t state;
    uint32_t api_version;

    diag_plugin_ctx_t *ctx;

    int (*get_api_version)(void);
    int (*init)(const diag_host_api_t *, diag_plugin_ctx_t **);
    size_t (*get_tool_count)(diag_plugin_ctx_t *);
    int (*get_tool)(diag_plugin_ctx_t *, size_t, diag_tool_def_t *);
    int (*invoke)(diag_plugin_ctx_t *, const diag_invoke_req_t *, diag_invoke_resp_t *);
    void (*deinit)(diag_plugin_ctx_t *);

    _Atomic uint32_t in_flight;
    uint64_t mtime_ns;
} plugin_record_t;
```

## 4.3 Tool Registry

```c
typedef enum {
    TOOL_PROVIDER_CATALOG,
    TOOL_PROVIDER_PLUGIN
} tool_provider_t;

typedef struct {
    char *tool_name;
    tool_provider_t provider;
    plugin_record_t *plugin; /* null for catalog */
} tool_binding_t;
```

Registry container:
- hash map keyed by `tool_name`
- guarded by `pthread_rwlock_t`

## 5. Module APIs

## 5.1 Plugin Manager (`plugin_manager.h`)

```c
typedef struct plugin_manager plugin_manager_t;

typedef struct {
    const char *plugin_dir;
    int enabled;
    int use_inotify;
    int poll_interval_sec;
    int conflict_policy; /* 0 reject-plugin-tool, 1 plugin-priority */
} plugin_cfg_t;

int plugin_manager_init(plugin_manager_t **pm, const plugin_cfg_t *cfg, const diag_host_api_t *host);
int plugin_manager_start(plugin_manager_t *pm);
int plugin_manager_stop(plugin_manager_t *pm);
int plugin_manager_scan(plugin_manager_t *pm);
int plugin_manager_destroy(plugin_manager_t *pm);
```

## 5.2 Tool Registry (`tool_registry.h`)

```c
int tool_registry_init(void);
int tool_registry_bind_plugin_tool(const char *tool_name, plugin_record_t *plugin, int conflict_policy);
int tool_registry_unbind_plugin_tools(plugin_record_t *plugin);
int tool_registry_lookup(const char *tool_name, tool_binding_t *out);
void tool_registry_destroy(void);
```

## 6. Load / Unload / Reload Algorithms

## 6.1 Load Plugin

1. Validate path is under configured plugin dir.
2. Validate owner/mode policy.
3. `dlopen(path, RTLD_NOW | RTLD_LOCAL)`.
4. Resolve required symbols via `dlsym`.
5. Validate `diag_plugin_get_api_version() == DIAG_PLUGIN_API_VERSION`.
6. Call `diag_plugin_init(...)`.
7. Enumerate tools and bind into registry atomically.
8. Mark plugin ACTIVE.

Error path:
- roll back partial registry inserts
- deinit if needed
- `dlclose`
- state FAILED

## 6.2 Unload Plugin

1. Mark plugin DRAINING.
2. Unbind plugin tools from registry.
3. Wait until `in_flight == 0` (bounded timeout).
4. Call `diag_plugin_deinit(ctx)`.
5. `dlclose(handle)`.
6. Mark UNLOADED.

## 6.3 Reload Plugin

1. Trigger unload existing record.
2. Load updated file as new record.
3. If new load fails, keep service running and log error.

## 7. Request Dispatch Integration (multi-plane-runtime-manager.c)

In `handle_request()` after tool extraction:

1. `tool_registry_lookup(tool, &binding)`.
2. If provider == plugin:
   - increment `binding.plugin->in_flight`
   - call `binding.plugin->invoke(...)`
   - decrement `in_flight`
   - map plugin response to existing payload (`tool`, `exit_code`, `stdout`)
3. Else fallback to existing catalog/shell flow.

## 8. Watcher Design

## 8.1 Event mode (Linux)
- Use inotify for create/delete/move/write-close events in plugin dir.
- Debounce events (e.g., 200–500 ms) before reload/load to avoid partial write race.

## 8.2 Poll mode (fallback)
- Periodic scan with `stat` map (`path -> mtime/size`).
- Detect add/remove/changed files and trigger corresponding action.

v1 implementation profile:
- Poll mode is implemented and enabled.
- Event/inotify mode is deferred.

## 9. Error Mapping for Plugin Invocation

Recommended response mapping:
- plugin invoke success -> `exit_code` from plugin
- plugin invoke internal failure -> `exit_code=70`, `stdout="ERR_PLUGIN_INVOKE"`
- plugin not loaded/unavailable -> `exit_code=71`, `stdout="ERR_PLUGIN_UNAVAILABLE"`
- plugin API mismatch -> `exit_code=72`, `stdout="ERR_PLUGIN_API_VERSION"`

## 10. CMake Changes

Add option:
- `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS` (default OFF)

When ON:
- compile `plugin_manager.c`, `tool_registry.c`, `plugin_watcher.c`
- define `MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=1`
- link `dl` on Linux (`${CMAKE_DL_LIBS}`)

## 11. Threading and Locks

- `tool_registry`: RW lock
- `plugin_manager` mutable plugin list: mutex
- lock order rule: plugin_manager mutex -> tool_registry write lock
- request path takes only registry read lock + atomic refcount increments

## 12. Resource Management Rules

- Every `dlopen` must have exactly one `dlclose`.
- Every successful `plugin_init` must have exactly one `plugin_deinit`.
- Tool bindings removed before unload finalization.
- Free plugin strings and dynamic arrays on destroy.

## 13. Tests

## 13.1 Unit Tests
- symbol resolution and version check handling
- conflict policy behavior
- registry bind/unbind/lookup correctness

## 13.2 Integration Tests
- startup load with 0, 1, N plugins
- add `.so` at runtime and invoke its tool
- remove plugin during idle and during active requests
- replace plugin binary and verify new behavior

## 13.3 Stress/Soak
- repeated load/unload cycles (e.g., 10k iterations)
- concurrent request + reload races

## 14. Logging and Metrics

Log events:
- `plugin_discovered`, `plugin_loaded`, `plugin_failed`, `plugin_unloaded`, `plugin_reloaded`

Counters:
- `plugins_loaded_total`
- `plugins_failed_total`
- `plugin_invocations_total`
- `plugin_invocation_errors_total`
- `plugin_reload_total`

## 15. Migration and Compatibility

- If dynamic plugin feature is OFF, behavior remains unchanged.
- Catalog command path remains functional regardless of plugin feature state.

## 16. Definition of Done

1. Build succeeds with dynamic plugin feature ON/OFF.
2. Runtime add/remove/update of `.so` works without restart.
3. Plugin with multiple tools is invokable via existing request format.
4. No crashes/leaks in concurrent reload/invoke stress tests.
5. Documentation updated with plugin ABI and operator guide.
