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

#define FAST_INVOKE_MAX_US 500000.0

typedef struct {
    plugin_manager_t *pm;
    int rc;
} scan_ctx_t;

typedef struct {
    plugin_manager_t *pm;
    int rc;
} invoke_ctx_t;

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
    scan_ctx_t *ctx = (scan_ctx_t *)arg;
    ctx->rc = plugin_manager_scan(ctx->pm);
    return NULL;
}

static void *slow_invoke_thread_main(void *arg)
{
    invoke_ctx_t *ctx = (invoke_ctx_t *)arg;
    diag_invoke_req_t req;
    diag_invoke_resp_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.source = "phase2.1-unload-wait";
    req.transaction_uuid = "phase2.1-slow";
    req.tool = "plugin_sleep_slow";

    ctx->rc = (int)plugin_manager_invoke(ctx->pm, "plugin_sleep_slow", &req, &resp);
    if (ctx->rc == PLUGIN_INVOKE_OK && resp.exit_code == 0)
        ctx->rc = 0;
    else
        ctx->rc = -1;

    return NULL;
}

int main(void)
{
    const char *fast_plugin_so = getenv("MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO");
    const char *slow_plugin_so = getenv("MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_SLOW_PLUGIN_SO");

    char tmp_template[] = "/tmp/mprm-unload-lock-XXXXXX";
    char *tmpdir;
    char fast_dst[512];
    char slow_dst[512];

    plugin_manager_t *pm = NULL;
    plugin_cfg_t cfg;
    diag_host_api_t host;

    pthread_t slow_tid;
    pthread_t scan_tid;
    invoke_ctx_t invoke_ctx;
    scan_ctx_t scan_ctx;

    diag_invoke_req_t fast_req;
    diag_invoke_resp_t fast_resp;
    plugin_invoke_result_t fast_rc;
    struct timespec t0, t1;
    double fast_invoke_us;

    if (!fast_plugin_so || !*fast_plugin_so || !slow_plugin_so || !*slow_plugin_so) {
        printf("[SKIP] plugin shared object env not set\n");
        return 77;
    }

    tmpdir = mkdtemp(tmp_template);
    TASSERT(tmpdir != NULL);

    snprintf(fast_dst, sizeof(fast_dst), "%s/sample_multi_plane_runtime_manager_plugin.so", tmpdir);
    snprintf(slow_dst, sizeof(slow_dst), "%s/sample_slow_multi_plane_runtime_manager_plugin.so", tmpdir);

    TASSERT(copy_file(fast_plugin_so, fast_dst) == 0);
    TASSERT(copy_file(slow_plugin_so, slow_dst) == 0);
    chmod(fast_dst, 0755);
    chmod(slow_dst, 0755);

    memset(&cfg, 0, sizeof(cfg));
    memset(&host, 0, sizeof(host));
    host.log_fn = host_log;
    host.get_config_fn = host_cfg;

    cfg.enabled = 1;
    cfg.plugin_dir = tmpdir;
    cfg.poll_interval_sec = 60;
    cfg.discovery_mode = "poll";
    cfg.debounce_ms = 0;
    cfg.conflict_policy = 1;
    cfg.verify_mode = "off";

    TASSERT(plugin_manager_init(&pm, &cfg, &host) == 0);
    TASSERT(plugin_manager_start(pm) == 0);
    TASSERT(plugin_manager_scan(pm) == 0);

    memset(&invoke_ctx, 0, sizeof(invoke_ctx));
    invoke_ctx.pm = pm;
    TASSERT(pthread_create(&slow_tid, NULL, slow_invoke_thread_main, &invoke_ctx) == 0);

    usleep(100000);

    TASSERT(unlink(slow_dst) == 0);

    memset(&scan_ctx, 0, sizeof(scan_ctx));
    scan_ctx.pm = pm;
    scan_ctx.rc = -1;
    TASSERT(pthread_create(&scan_tid, NULL, scan_thread_main, &scan_ctx) == 0);

    usleep(100000);

    memset(&fast_req, 0, sizeof(fast_req));
    memset(&fast_resp, 0, sizeof(fast_resp));
    fast_req.source = "phase2.1-unload-wait";
    fast_req.transaction_uuid = "phase2.1-fast";
    fast_req.tool = "plugin_echo";

    clock_gettime(CLOCK_MONOTONIC, &t0);
    fast_rc = plugin_manager_invoke(pm, "plugin_echo", &fast_req, &fast_resp);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    TASSERT(fast_rc == PLUGIN_INVOKE_OK);
    TASSERT(fast_resp.exit_code == 0);

    fast_invoke_us = (double)elapsed_ns(&t0, &t1) / 1000.0;
    TASSERT(fast_invoke_us <= FAST_INVOKE_MAX_US);

    TASSERT(pthread_join(slow_tid, NULL) == 0);
    TASSERT(invoke_ctx.rc == 0);

    TASSERT(pthread_join(scan_tid, NULL) == 0);
    TASSERT(scan_ctx.rc == 0);

    TASSERT(plugin_manager_destroy(pm) == 0);

    unlink(fast_dst);
    unlink(slow_dst);
    rmdir(tmpdir);

    printf("plugin_unload_wait_lock_scope_live: PASS (fast_invoke_us=%.2f)\n", fast_invoke_us);
    return 0;
}
