#include <errno.h>
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

#define STEADY_SAMPLES 2000
#define CHURN_SAMPLES 2000

#define STEADY_P95_MAX_US_DEFAULT 20000.0
#define STEADY_P99_MAX_US_DEFAULT 50000.0
#define CHURN_P95_MAX_US_DEFAULT 50000.0
#define CHURN_P99_MAX_US_DEFAULT 150000.0

typedef struct {
    double p50_us;
    double p95_us;
    double p99_us;
    double max_us;
} latency_stats_t;

typedef struct {
    plugin_manager_t *pm;
    volatile int *stop_flag;
    int *scan_errors;
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

static int cmp_u64(const void *a, const void *b)
{
    const uint64_t va = *(const uint64_t *)a;
    const uint64_t vb = *(const uint64_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static size_t percentile_index(size_t n, double p)
{
    size_t idx;
    if (n == 0) return 0;
    if (p <= 0.0) return 0;
    if (p >= 1.0) return n - 1;
    idx = (size_t)((double)(n - 1) * p + 0.5);
    if (idx >= n) idx = n - 1;
    return idx;
}

static int measure_invokes(plugin_manager_t *pm,
                           const char *tool,
                           const diag_invoke_req_t *req,
                           size_t n,
                           latency_stats_t *out_stats)
{
    uint64_t *samples = NULL;
    size_t i;
    diag_invoke_resp_t resp;

    samples = (uint64_t *)calloc(n, sizeof(uint64_t));
    if (!samples) return -1;

    for (i = 0; i < n; ++i) {
        struct timespec t0;
        struct timespec t1;
        plugin_invoke_result_t rc;

        memset(&resp, 0, sizeof(resp));
        clock_gettime(CLOCK_MONOTONIC, &t0);
        rc = plugin_manager_invoke(pm, tool, req, &resp);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        if (rc != PLUGIN_INVOKE_OK || resp.exit_code != 0) {
            free(samples);
            return -2;
        }

        samples[i] = elapsed_ns(&t0, &t1);
    }

    qsort(samples, n, sizeof(samples[0]), cmp_u64);

    out_stats->p50_us = (double)samples[percentile_index(n, 0.50)] / 1000.0;
    out_stats->p95_us = (double)samples[percentile_index(n, 0.95)] / 1000.0;
    out_stats->p99_us = (double)samples[percentile_index(n, 0.99)] / 1000.0;
    out_stats->max_us = (double)samples[n - 1] / 1000.0;

    free(samples);
    return 0;
}

static void *scan_thread_main(void *arg)
{
    scan_thread_ctx_t *ctx = (scan_thread_ctx_t *)arg;
    while (!*(ctx->stop_flag)) {
        if (plugin_manager_scan(ctx->pm) != 0)
            (*(ctx->scan_errors))++;
        usleep(10000);
    }
    return NULL;
}

static double read_env_double_or_default(const char *name, double dflt)
{
    char *end = NULL;
    const char *v = getenv(name);
    double out;
    if (!v || !*v) return dflt;
    errno = 0;
    out = strtod(v, &end);
    if (errno != 0 || end == v || out <= 0.0) return dflt;
    return out;
}

int main(void)
{
    const char *plugin_so = getenv("MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO");
    char tmp_template[] = "/tmp/mprm-phase1-XXXXXX";
    char *tmpdir;
    char dst_plugin[512];

    plugin_manager_t *pm = NULL;
    plugin_cfg_t cfg;
    diag_host_api_t host;
    diag_invoke_req_t req;
    latency_stats_t steady;
    latency_stats_t churn;

    volatile int stop_flag = 0;
    int scan_errors = 0;
    pthread_t scan_thread;
    scan_thread_ctx_t thread_ctx;

    double steady_p95_max_us;
    double steady_p99_max_us;
    double churn_p95_max_us;
    double churn_p99_max_us;

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
    memset(&steady, 0, sizeof(steady));
    memset(&churn, 0, sizeof(churn));

    host.log_fn = host_log;
    host.get_config_fn = host_cfg;

    cfg.enabled = 1;
    cfg.plugin_dir = tmpdir;
    cfg.poll_interval_sec = 60;
    cfg.discovery_mode = "hybrid";
    cfg.debounce_ms = 100;
    cfg.conflict_policy = 1;
    cfg.verify_mode = "off";

    TASSERT(plugin_manager_init(&pm, &cfg, &host) == 0);
    TASSERT(plugin_manager_start(pm) == 0);
    TASSERT(plugin_manager_scan(pm) == 0);

    req.source = "phase1-baseline";
    req.transaction_uuid = "phase1-baseline-uuid";
    req.tool = "plugin_echo";

    /* Warmup */
    TASSERT(measure_invokes(pm, "plugin_echo", &req, 200, &steady) == 0);

    TASSERT(measure_invokes(pm, "plugin_echo", &req, STEADY_SAMPLES, &steady) == 0);

    thread_ctx.pm = pm;
    thread_ctx.stop_flag = &stop_flag;
    thread_ctx.scan_errors = &scan_errors;
    TASSERT(pthread_create(&scan_thread, NULL, scan_thread_main, &thread_ctx) == 0);

    TASSERT(measure_invokes(pm, "plugin_echo", &req, CHURN_SAMPLES, &churn) == 0);

    stop_flag = 1;
    TASSERT(pthread_join(scan_thread, NULL) == 0);
    TASSERT(scan_errors == 0);

    steady_p95_max_us = read_env_double_or_default("MPRM_PHASE1_STEADY_P95_MAX_US", STEADY_P95_MAX_US_DEFAULT);
    steady_p99_max_us = read_env_double_or_default("MPRM_PHASE1_STEADY_P99_MAX_US", STEADY_P99_MAX_US_DEFAULT);
    churn_p95_max_us = read_env_double_or_default("MPRM_PHASE1_CHURN_P95_MAX_US", CHURN_P95_MAX_US_DEFAULT);
    churn_p99_max_us = read_env_double_or_default("MPRM_PHASE1_CHURN_P99_MAX_US", CHURN_P99_MAX_US_DEFAULT);

    printf("phase1_baseline_guardrails:\n");
    printf("  steady_us: p50=%.2f p95=%.2f p99=%.2f max=%.2f\n", steady.p50_us, steady.p95_us, steady.p99_us, steady.max_us);
    printf("  churn_us : p50=%.2f p95=%.2f p99=%.2f max=%.2f\n", churn.p50_us, churn.p95_us, churn.p99_us, churn.max_us);
    printf("  guardrails_us:\n");
    printf("    steady_p95<=%.2f steady_p99<=%.2f\n", steady_p95_max_us, steady_p99_max_us);
    printf("    churn_p95<=%.2f churn_p99<=%.2f\n", churn_p95_max_us, churn_p99_max_us);

    TASSERT(steady.p95_us <= steady_p95_max_us);
    TASSERT(steady.p99_us <= steady_p99_max_us);
    TASSERT(churn.p95_us <= churn_p95_max_us);
    TASSERT(churn.p99_us <= churn_p99_max_us);

    TASSERT(plugin_manager_destroy(pm) == 0);
    unlink(dst_plugin);
    rmdir(tmpdir);

    printf("phase1_baseline_guardrails: PASS\n");
    return 0;
}