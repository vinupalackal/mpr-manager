#include <stdio.h>
#include <string.h>

#include "../src/plugin_manager.h"
#include "../src/tool_registry.h"

static void host_log(int level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

static const char *host_cfg(const char *key)
{
    (void)key;
    return NULL;
}

static int catalog_exists_cb(const char *tool_name, void *ctx)
{
    (void)ctx;
    return strcmp(tool_name, "catalog_tool") == 0 ? 1 : 0;
}

#define TASSERT(x) do { if (!(x)) { fprintf(stderr, "assert failed: %s\n", #x); return 1; } } while (0)

int main(void)
{
    plugin_manager_t *pm = NULL;
    plugin_cfg_t cfg;
    diag_host_api_t host;
    tool_binding_t b;

    memset(&cfg, 0, sizeof(cfg));
    memset(&host, 0, sizeof(host));

    host.log_fn = host_log;
    host.get_config_fn = host_cfg;

    cfg.enabled = 0;
    cfg.plugin_dir = "/tmp/multi-plane-runtime-manager-plugins-none";
    cfg.poll_interval_sec = 1;
    cfg.conflict_policy = 0;
    cfg.verify_mode = "off";
    cfg.catalog_tool_exists_cb = catalog_exists_cb;

    TASSERT(plugin_manager_init(&pm, &cfg, &host) == 0);
    TASSERT(pm != NULL);
    TASSERT(plugin_manager_start(pm) == 0);
    TASSERT(plugin_manager_scan(pm) == 0);
    TASSERT(plugin_manager_stop(pm) == 0);

    TASSERT(tool_registry_init() == 0);
    TASSERT(tool_registry_bind_plugin_tool("plugin_tool", (void *)0x1, 0) == 0);
    TASSERT(tool_registry_lookup("plugin_tool", &b) == 0);
    TASSERT(b.provider == TOOL_PROVIDER_PLUGIN);
    TASSERT(tool_registry_bind_plugin_tool("plugin_tool", (void *)0x2, 0) != 0);
    TASSERT(tool_registry_bind_plugin_tool("plugin_tool", (void *)0x2, 1) == 0);
    TASSERT(tool_registry_unbind_plugin_tools((void *)0x2) == 0);
    TASSERT(tool_registry_lookup("plugin_tool", &b) != 0);
    tool_registry_destroy();

    TASSERT(plugin_manager_destroy(pm) == 0);

    printf("dynamic_plugins_unit: PASS\n");
    return 0;
}
