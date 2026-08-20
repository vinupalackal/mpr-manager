#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../src/plugin_manager.h"

#define TASSERT(x) do { if (!(x)) { fprintf(stderr, "assert failed: %s\n", #x); return 1; } } while (0)

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

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[4096];
    size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

int main(void)
{
    const char *plugin_so = getenv("MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO");
    char tmp_template[] = "/tmp/diag-plugin-live-XXXXXX";
    char *tmpdir;
    char dst_plugin[512];
    plugin_manager_t *pm = NULL;
    plugin_cfg_t cfg;
    diag_host_api_t host;
    diag_invoke_req_t req;
    diag_invoke_resp_t resp;
    plugin_invoke_result_t rc;

    if (!plugin_so || !*plugin_so) {
        printf("[SKIP] MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO not set\n");
        return 77;
    }

    tmpdir = mkdtemp(tmp_template);
    TASSERT(tmpdir != NULL);

    snprintf(dst_plugin, sizeof(dst_plugin), "%s/sample_multi_plane_runtime_manager_plugin.so", tmpdir);
    TASSERT(copy_file(plugin_so, dst_plugin) == 0);
    chmod(dst_plugin, 0755);

    memset(&cfg, 0, sizeof(cfg));
    memset(&host, 0, sizeof(host));
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    host.log_fn = host_log;
    host.get_config_fn = host_cfg;

    cfg.enabled = 1;
    cfg.plugin_dir = tmpdir;
    cfg.poll_interval_sec = 1;
    cfg.conflict_policy = 1; /* plugin-priority */
    cfg.verify_mode = "off";

    TASSERT(plugin_manager_init(&pm, &cfg, &host) == 0);
    TASSERT(plugin_manager_start(pm) == 0);
    TASSERT(plugin_manager_scan(pm) == 0);

    req.source = "unit-test";
    req.transaction_uuid = "uuid-live";
    req.tool = "plugin_version";

    rc = plugin_manager_invoke(pm, "plugin_version", &req, &resp);
    TASSERT(rc == PLUGIN_INVOKE_OK);
    TASSERT(resp.exit_code == 0);
    TASSERT(resp.out != NULL);

    req.tool = "plugin_echo";
    rc = plugin_manager_invoke(pm, "plugin_echo", &req, &resp);
    TASSERT(rc == PLUGIN_INVOKE_OK);
    TASSERT(resp.exit_code == 0);

    unlink(dst_plugin);
    TASSERT(plugin_manager_scan(pm) == 0);

    rc = plugin_manager_invoke(pm, "plugin_echo", &req, &resp);
    TASSERT(rc == PLUGIN_INVOKE_NOT_FOUND);

    TASSERT(plugin_manager_destroy(pm) == 0);
    rmdir(tmpdir);

    printf("dynamic_plugins_live: PASS\n");
    return 0;
}
