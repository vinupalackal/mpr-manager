#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include "plugin_api.h"

typedef struct plugin_manager plugin_manager_t;

typedef struct {
    const char *plugin_dir; /* single dir or comma-separated directory list */
    int enabled;
    int poll_interval_sec; /* periodic reconcile interval in seconds (default 60; v1 mandatory when enabled) */
    const char *discovery_mode; /* notify|poll|hybrid */
    int debounce_ms; /* debounce window before loading changed plugin files */
    int conflict_policy; /* 0 reject-plugin-tool, 1 plugin-priority */
    const char *verify_mode; /* off/hash/signature (v1 enforces off) */

    int (*catalog_tool_exists_cb)(const char *tool_name, void *ctx);
    void *catalog_ctx;

    void (*on_registry_changed_cb)(void *ctx);
    void *registry_changed_ctx;
} plugin_cfg_t;

typedef struct {
    uint64_t plugins_loaded_total;
    uint64_t plugins_failed_total;
    uint64_t tools_registered_total;
    uint64_t plugin_invocations_total;
    uint64_t plugin_invocation_errors_total;
    uint64_t plugin_reload_total;
} plugin_metrics_t;

typedef enum {
    PLUGIN_INVOKE_NOT_FOUND = 0,
    PLUGIN_INVOKE_OK = 1,
    PLUGIN_INVOKE_ERR_INVOKE = -1,
    PLUGIN_INVOKE_ERR_UNAVAILABLE = -2,
    PLUGIN_INVOKE_ERR_API_VERSION = -3
} plugin_invoke_result_t;

int plugin_manager_init(plugin_manager_t **pm,
                        const plugin_cfg_t *cfg,
                        const diag_host_api_t *host);
int plugin_manager_start(plugin_manager_t *pm);
int plugin_manager_stop(plugin_manager_t *pm);
int plugin_manager_scan(plugin_manager_t *pm);
int plugin_manager_destroy(plugin_manager_t *pm);

plugin_invoke_result_t plugin_manager_invoke(plugin_manager_t *pm,
                                             const char *tool,
                                             const diag_invoke_req_t *req,
                                             diag_invoke_resp_t *resp);

void plugin_manager_get_metrics(plugin_manager_t *pm, plugin_metrics_t *out);

int plugin_manager_get_tool_publish_meta(plugin_manager_t *pm,
                                         const char *tool_name,
                                         char *plugin_path_out,
                                         size_t plugin_path_out_len,
                                         int *timeout_out);

#endif
