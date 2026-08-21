#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/plugin_manager.h"

#define TASSERT(x) do { if (!(x)) { fprintf(stderr, "assert failed: %s\n", #x); return 1; } } while (0)

#define INVOKE_LATENCY_MAX_US 500000.0

typedef struct {
    plugin_manager_t *pm;
    int rc;
} scan_thread_ctx_t;

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
    if (!out) {
        fclose(in);
        return -1;
    }
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

static uint64_t elapsed_ns(const struct timespec *start, const struct timespec *end)
{
    uint64_t s = (uint64_t)start->tv_sec * 1000000000ULL + (uint64_t)start->tv_nsec;
    uint64_t e = (uint64_t)end->tv_sec * 1000000000ULL + (uint64_t)end->tv_nsec;
    return e - s;
}

static void *scan_thread_main(void *arg)
{
    scan_thread_ctx_t *ctx = (scan_thread_ctx_t *)arg;
    ctx->rc = plugin_manager_scan(ctx->pm);
    return NULL;
}

int main(void)
{
    const char *plugin_so = getenv("MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO");
    char tmp_template[] = "/tmp/mprm-lock-scope-XXXXXX";
    char *tmpdir;
    char plugin_a[512];
    char plugin_b[512];

    plugin_manager_t *pm = NULL;
    plugin_cfg_t cfg;
    diag_host_api_t host;
    diag_invoke_req_t req;
    diag_invoke_resp_t resp;
    plugin_invoke_result_t rc;

    pthread_t tid;
    scan_thread_ctx_t tctx;
    struct timespec t0, t1;
    double invoke_us;

    if (!plugin_so || !*plugin_so) {
        printf("[SKIP] MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO not set\n");
        return 77;
    }

    tmpdir = mkdtemp(tmp_template);
    TASSERT(tmpdir != NULL);

    snprintf(plugin_a, sizeof(plugin_a), "%s/sample_multi_plane_runtime_manager_plugin.so", tmpdir);
    snprintf(plugin_b, sizeof(plugin_b), "%s/sample_multi_plane_runtime_manager_plugin_alt.so", tmpdir);

    TASSERT(copy_file(plugin_so, plugin_a) == 0);
    chmod(plugin_a, 0755);

    memset(&cfg, 0, sizeof(cfg));
    memset(&host, 0, sizeof(host));
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    memset(&tctx, 0, sizeof(tctx));

    host.log_fn = host_log;
    host.get_config_fn = host_cfg;

    cfg.enabled = 1;
    cfg.plugin_dir = tmpdir;
    cfg.poll_interval_sec = 60;
    cfg.discovery_mode = "poll";
    cfg.debounce_ms = 1500; /* intentionally high to validate lock-scope change */
    cfg.conflict_policy = 1;
    cfg.verify_mode = "off";

    TASSERT(plugin_manager_init(&pm, &cfg, &host) == 0);
    TASSERT(plugin_manager_start(pm) == 0);
    TASSERT(plugin_manager_scan(pm) == 0);

    req.source = "phase2-lock-scope";
    req.transaction_uuid = "phase2-lock-scope-uuid";
    req.tool = "plugin_echo";

    rc = plugin_manager_invoke(pm, "plugin_echo", &req, &resp);
    TASSERT(rc == PLUGIN_INVOKE_OK);
    TASSERT(resp.exit_code == 0);

    /* Introduce a new .so so the next scan does real work. */
    TASSERT(copy_file(plugin_so, plugin_b) == 0);
    chmod(plugin_b, 0755);

    tctx.pm = pm;
    tctx.rc = -1;
    TASSERT(pthread_create(&tid, NULL, scan_thread_main, &tctx) == 0);

    /* Let scan enter its debounce window. */
    usleep(100000);

    memset(&resp, 0, sizeof(resp));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rc = plugin_manager_invoke(pm, "plugin_echo", &req, &resp);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    TASSERT(rc == PLUGIN_INVOKE_OK);
    TASSERT(resp.exit_code == 0);

    invoke_us = (double)elapsed_ns(&t0, &t1) / 1000.0;
    TASSERT(invoke_us <= INVOKE_LATENCY_MAX_US);

    TASSERT(pthread_join(tid, NULL) == 0);
    TASSERT(tctx.rc == 0);

    TASSERT(plugin_manager_destroy(pm) == 0);

    unlink(plugin_b);
    unlink(plugin_a);
    rmdir(tmpdir);

    printf("plugin_lock_scope_live: PASS (invoke_us=%.2f)\n", invoke_us);
    return 0;
}
