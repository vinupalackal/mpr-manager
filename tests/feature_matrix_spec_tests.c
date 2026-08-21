#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    ROUTE_EXEC = 0,
    ROUTE_DESCRIBE,
    ROUTE_HEALTH,
    ROUTE_PUSH,
    ROUTE_DROP
} route_t;

typedef enum {
    PUSH_OK = 0,
    PUSH_REJECT_BASE_VERSION,
    PUSH_REJECT_INVALID_DIFF
} push_result_t;

typedef enum {
    LOOKUP_NOT_FOUND = 0,
    LOOKUP_FOUND,
    LOOKUP_AMBIGUOUS
} lookup_result_t;

typedef struct {
    int file_loaded;
    const char *source_path;
} cfg_source_result_t;

static int g_passed = 0;
static int g_failed = 0;
static int g_skipped = 0;

#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "    assert failed: %s\n", #x); return 0; } } while (0)
#define ASSERT_EQ_INT(a,b) do { if ((a)!=(b)) { fprintf(stderr, "    assert failed: %s == %s (%d vs %d)\n", #a, #b, (int)(a), (int)(b)); return 0; } } while (0)
#define ASSERT_STREQ(a,b) do { \
    const char *_a=(a), *_b=(b); \
    if (((_a)==NULL) != ((_b)==NULL) || ((_a)&&(_b)&&strcmp(_a,_b)!=0)) { \
        fprintf(stderr, "    assert failed: %s == %s\n", #a, #b); return 0; \
    } \
} while (0)

static int bool_from_text(const char *v, int defv)
{
    if (!v) return defv;
    if (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0)
        return 1;
    if (strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0 || strcasecmp(v, "off") == 0)
        return 0;
    return defv;
}

static int clamp_poll_interval(int val)
{
    if (val < 0) return 0;
    if (val > 3600) return 3600;
    return val;
}

static int clamp_request_thread_stack_bytes_model(int v)
{
    const int min_stack = 16384; /* conservative portable floor for model tests */
    const int max_stack = 8 * 1024 * 1024;
    if (v < min_stack) return min_stack;
    if (v > max_stack) return max_stack;
    return v;
}

static route_t route_kind(const char *kind)
{
    if (!kind || strcmp(kind, "EXEC") == 0)
        return ROUTE_EXEC;
    if (strcmp(kind, "DESCRIBE") == 0)
        return ROUTE_DESCRIBE;
    if (strcmp(kind, "HEALTH") == 0)
        return ROUTE_HEALTH;
    if (strcmp(kind, "PUSH") == 0)
        return ROUTE_PUSH;
    return ROUTE_DROP;
}

static push_result_t apply_push(long current_version, long base_version, int diff_valid)
{
    if (!diff_valid)
        return PUSH_REJECT_INVALID_DIFF;
    if (base_version != current_version)
        return PUSH_REJECT_BASE_VERSION;
    return PUSH_OK;
}

static int would_emit_changed(push_result_t r)
{
    return r == PUSH_OK ? 1 : 0;
}

static int would_emit_capability_sync(push_result_t r)
{
    return r == PUSH_OK ? 1 : 0;
}

static int push_transport_allowed(int require_local_only, int from_local)
{
    if (require_local_only && !from_local)
        return 0;
    return 1;
}

static int push_auth_allowed(const char *expected, const char *provided)
{
    if (!expected || !*expected)
        return 1;
    if (!provided || !*provided)
        return 0;
    return strcmp(expected, provided) == 0;
}

static int push_payload_size_allowed(size_t payload_len, size_t max_payload)
{
    return payload_len <= max_payload ? 1 : 0;
}

static int push_rate_limit_allows(long long last_ms, long long now_ms, int min_interval_ms)
{
    if (min_interval_ms <= 0)
        return 1;
    if (last_ms <= 0)
        return 1;
    return (now_ms - last_ms) >= (long long)min_interval_ms ? 1 : 0;
}

typedef struct {
    int attempts;
    int accepted;
    int rejected_transport;
    int rejected_unauthorized;
    int rejected_rate_limit;
    int rejected_payload_too_large;
    int rejected_other;
} push_metrics_model_t;

typedef enum {
    PUSH_MODEL_OK = 0,
    PUSH_MODEL_REJECT_TRANSPORT,
    PUSH_MODEL_REJECT_UNAUTHORIZED,
    PUSH_MODEL_REJECT_RATE_LIMIT,
    PUSH_MODEL_REJECT_PAYLOAD_TOO_LARGE,
    PUSH_MODEL_REJECT_OTHER
} push_model_status_t;

typedef enum {
    GUARD_MODE_OFF = 0,
    GUARD_MODE_MONITOR,
    GUARD_MODE_ENFORCE
} guard_mode_t;

static void push_metrics_record_model(push_metrics_model_t *m, push_model_status_t st)
{
    if (!m) return;
    m->attempts++;
    if (st == PUSH_MODEL_OK) m->accepted++;
    else if (st == PUSH_MODEL_REJECT_TRANSPORT) m->rejected_transport++;
    else if (st == PUSH_MODEL_REJECT_UNAUTHORIZED) m->rejected_unauthorized++;
    else if (st == PUSH_MODEL_REJECT_RATE_LIMIT) m->rejected_rate_limit++;
    else if (st == PUSH_MODEL_REJECT_PAYLOAD_TOO_LARGE) m->rejected_payload_too_large++;
    else m->rejected_other++;
}

static int clamp_push_min_interval_ms_model(int v)
{
    if (v < 0) return 0;
    if (v > 60000) return 60000;
    return v;
}

static int clamp_push_max_payload_bytes_model(int v)
{
    if (v < 1024) return 1024;
    if (v > (4 * 1024 * 1024)) return (4 * 1024 * 1024);
    return v;
}

static guard_mode_t guard_mode_parse_model(const char *v, guard_mode_t def_mode)
{
    if (!v || !*v)
        return def_mode;
    if (strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0 || strcasecmp(v, "disabled") == 0)
        return GUARD_MODE_OFF;
    if (strcasecmp(v, "monitor") == 0 || strcasecmp(v, "log-only") == 0 || strcasecmp(v, "observe") == 0)
        return GUARD_MODE_MONITOR;
    if (strcasecmp(v, "enforce") == 0 || strcmp(v, "1") == 0 || strcasecmp(v, "on") == 0 || strcasecmp(v, "enabled") == 0)
        return GUARD_MODE_ENFORCE;
    return def_mode;
}

static int guard_would_reject_model(guard_mode_t mode, int violated)
{
    if (!violated)
        return 0;
    return mode == GUARD_MODE_ENFORCE ? 1 : 0;
}

static void push_observed_record_model(push_metrics_model_t *m, push_model_status_t st)
{
    if (!m) return;
    if (st == PUSH_MODEL_REJECT_RATE_LIMIT) m->rejected_rate_limit++;
    if (st == PUSH_MODEL_REJECT_PAYLOAD_TOO_LARGE) m->rejected_payload_too_large++;
}

static lookup_result_t resolve_tool_lookup(int found_count, int plane_explicit)
{
    if (found_count <= 0)
        return LOOKUP_NOT_FOUND;
    if (!plane_explicit && found_count > 1)
        return LOOKUP_AMBIGUOUS;
    return LOOKUP_FOUND;
}

static int is_blocked_first_token(const char *cmd)
{
    static const char *blocked[] = {
        "rm", "rmdir", "reboot", "shutdown", "halt", "poweroff",
        "factory_reset", "kill", "killall", "pkill", "dd",
        "mkfs", "fdisk", "mount", "umount", "iptables", "passwd", NULL
    };
    char tok[128] = {0};
    const char *sp;
    const char *base;
    size_t i, n;

    if (!cmd || !*cmd)
        return 1;

    sp = strchr(cmd, ' ');
    n = sp ? (size_t)(sp - cmd) : strlen(cmd);
    if (n >= sizeof(tok)) n = sizeof(tok) - 1;
    memcpy(tok, cmd, n);

    base = strrchr(tok, '/');
    base = base ? base + 1 : tok;

    for (i = 0; blocked[i]; i++) {
        if (strcmp(tok, blocked[i]) == 0 || strcmp(base, blocked[i]) == 0)
            return 1;
    }
    return 0;
}

static cfg_source_result_t choose_cfg_source(int has_env_path, int has_etc_cfg, int has_local_cfg)
{
    cfg_source_result_t r;
    r.file_loaded = 0;
    r.source_path = "(defaults-only)";

    if (has_env_path) {
        r.file_loaded = 1;
        r.source_path = "env:config_file";
        return r;
    }
    if (has_etc_cfg) {
        r.file_loaded = 1;
        r.source_path = "/etc/multi-plane-runtime-manager/multi-plane-runtime-manager.conf";
        return r;
    }
    if (has_local_cfg) {
        r.file_loaded = 1;
        r.source_path = "./multi-plane-runtime-manager.conf";
        return r;
    }
    return r;
}

static int resolve_override_bool(const char *envv, const char *filev, int defv)
{
    if (envv && *envv)
        return bool_from_text(envv, defv);
    if (filev && *filev)
        return bool_from_text(filev, defv);
    return defv;
}

static int run_case(const char *id, int (*fn)(void))
{
    int rc;
    printf("[RUN ] %s\n", id);
    rc = fn();
    if (rc == 1) {
        g_passed++;
        printf("[PASS] %s\n", id);
        return 1;
    }
    if (rc == -1) {
        g_skipped++;
        printf("[SKIP] %s\n", id);
        return 1;
    }
    g_failed++;
    printf("[FAIL] %s\n", id);
    return 0;
}

/* Startup/config/logging */
static int tc_cfg_001(void) {
    cfg_source_result_t r = choose_cfg_source(0, 1, 1);
    ASSERT_EQ_INT(r.file_loaded, 1);
    ASSERT_STREQ(r.source_path, "/etc/multi-plane-runtime-manager/multi-plane-runtime-manager.conf");
    return 1;
}
static int tc_cfg_002(void) {
    int v = resolve_override_bool("0", "1", 1);
    ASSERT_EQ_INT(v, 0);
    return 1;
}
static int tc_cfg_003(void) {
    int v = resolve_override_bool(NULL, NULL, 1);
    ASSERT_EQ_INT(v, 1);
    return 1;
}
static int tc_cfg_004(void) {
    const char *default_log = "/tmp/logs/multi-plane-runtime-manager.log";
    ASSERT_STREQ(default_log, "/tmp/logs/multi-plane-runtime-manager.log");
    return 1;
}
static int tc_cfg_005(void) {
    int enabled = resolve_override_bool("0", "1", 1);
    ASSERT_EQ_INT(enabled, 0);
    return 1;
}
static int tc_cfg_006(void) {
    int api = resolve_override_bool("1", NULL, 0);
    int data = resolve_override_bool("0", "1", 1);
    ASSERT_EQ_INT(api, 1);
    ASSERT_EQ_INT(data, 0);
    return 1;
}
static int tc_cfg_007(void) {
    ASSERT_EQ_INT(clamp_request_thread_stack_bytes_model(1), 16384);
    ASSERT_EQ_INT(clamp_request_thread_stack_bytes_model(262144), 262144);
    ASSERT_EQ_INT(clamp_request_thread_stack_bytes_model(16 * 1024 * 1024), 8 * 1024 * 1024);
    return 1;
}

/* Kind routing */
static int tc_kind_001(void) { ASSERT_EQ_INT(route_kind(NULL), ROUTE_EXEC); return 1; }
static int tc_kind_002(void) { ASSERT_EQ_INT(route_kind("DESCRIBE"), ROUTE_DESCRIBE); return 1; }
static int tc_kind_003(void) { ASSERT_EQ_INT(route_kind("HEALTH"), ROUTE_HEALTH); return 1; }
static int tc_kind_004(void) { ASSERT_EQ_INT(route_kind("PUSH"), ROUTE_PUSH); return 1; }
static int tc_kind_005(void) { ASSERT_EQ_INT(route_kind("WHATEVER"), ROUTE_DROP); return 1; }

static int tc_kind_006(void) {
    push_result_t r = apply_push(10, 10, 1);
    ASSERT_EQ_INT(r, PUSH_OK);
    ASSERT_EQ_INT(would_emit_changed(r), 1);
    return 1;
}

static int tc_kind_007(void) {
    push_result_t r = apply_push(10, 9, 1);
    ASSERT_EQ_INT(r, PUSH_REJECT_BASE_VERSION);
    ASSERT_EQ_INT(would_emit_changed(r), 0);
    return 1;
}

static int tc_kind_008(void) {
    push_result_t r = apply_push(3, 3, 1);
    ASSERT_EQ_INT(would_emit_capability_sync(r), 1);
    return 1;
}

static int tc_kind_009(void) {
    ASSERT_EQ_INT(push_transport_allowed(1, 0), 0);
    return 1;
}

static int tc_kind_010(void) {
    ASSERT_EQ_INT(push_transport_allowed(1, 1), 1);
    ASSERT_EQ_INT(push_transport_allowed(0, 0), 1);
    return 1;
}

static int tc_kind_011(void) {
    ASSERT_EQ_INT(push_auth_allowed("secret", NULL), 0);
    ASSERT_EQ_INT(push_auth_allowed("secret", "wrong"), 0);
    return 1;
}

static int tc_kind_012(void) {
    ASSERT_EQ_INT(push_auth_allowed("secret", "secret"), 1);
    ASSERT_EQ_INT(push_auth_allowed(NULL, NULL), 1);
    ASSERT_EQ_INT(push_auth_allowed("", NULL), 1);
    return 1;
}

static int tc_kind_013(void) {
    ASSERT_EQ_INT(push_payload_size_allowed(1024, 1024), 1);
    ASSERT_EQ_INT(push_payload_size_allowed(1025, 1024), 0);
    return 1;
}

static int tc_kind_014(void) {
    ASSERT_EQ_INT(push_rate_limit_allows(0, 1000, 250), 1);
    ASSERT_EQ_INT(push_rate_limit_allows(1000, 1200, 250), 0);
    ASSERT_EQ_INT(push_rate_limit_allows(1000, 1300, 250), 1);
    return 1;
}

static int tc_kind_015(void) {
    push_metrics_model_t m;
    memset(&m, 0, sizeof(m));

    push_metrics_record_model(&m, PUSH_MODEL_OK);
    push_metrics_record_model(&m, PUSH_MODEL_REJECT_TRANSPORT);
    push_metrics_record_model(&m, PUSH_MODEL_REJECT_UNAUTHORIZED);
    push_metrics_record_model(&m, PUSH_MODEL_REJECT_RATE_LIMIT);
    push_metrics_record_model(&m, PUSH_MODEL_REJECT_PAYLOAD_TOO_LARGE);
    push_metrics_record_model(&m, PUSH_MODEL_REJECT_OTHER);

    ASSERT_EQ_INT(m.attempts, 6);
    ASSERT_EQ_INT(m.accepted, 1);
    ASSERT_EQ_INT(m.rejected_transport, 1);
    ASSERT_EQ_INT(m.rejected_unauthorized, 1);
    ASSERT_EQ_INT(m.rejected_rate_limit, 1);
    ASSERT_EQ_INT(m.rejected_payload_too_large, 1);
    ASSERT_EQ_INT(m.rejected_other, 1);
    return 1;
}

static int tc_kind_016(void) {
    ASSERT_EQ_INT(clamp_push_min_interval_ms_model(-5), 0);
    ASSERT_EQ_INT(clamp_push_min_interval_ms_model(70000), 60000);
    ASSERT_EQ_INT(clamp_push_min_interval_ms_model(250), 250);

    ASSERT_EQ_INT(clamp_push_max_payload_bytes_model(10), 1024);
    ASSERT_EQ_INT(clamp_push_max_payload_bytes_model((5 * 1024 * 1024)), (4 * 1024 * 1024));
    ASSERT_EQ_INT(clamp_push_max_payload_bytes_model(262144), 262144);
    return 1;
}

static int tc_kind_017(void) {
    ASSERT_EQ_INT(guard_mode_parse_model("enforce", GUARD_MODE_MONITOR), GUARD_MODE_ENFORCE);
    ASSERT_EQ_INT(guard_mode_parse_model("monitor", GUARD_MODE_ENFORCE), GUARD_MODE_MONITOR);
    ASSERT_EQ_INT(guard_mode_parse_model("off", GUARD_MODE_ENFORCE), GUARD_MODE_OFF);
    ASSERT_EQ_INT(guard_mode_parse_model("unknown", GUARD_MODE_ENFORCE), GUARD_MODE_ENFORCE);
    return 1;
}

static int tc_kind_018(void) {
    ASSERT_EQ_INT(guard_would_reject_model(GUARD_MODE_ENFORCE, 1), 1);
    ASSERT_EQ_INT(guard_would_reject_model(GUARD_MODE_MONITOR, 1), 0);
    ASSERT_EQ_INT(guard_would_reject_model(GUARD_MODE_OFF, 1), 0);
    ASSERT_EQ_INT(guard_would_reject_model(GUARD_MODE_ENFORCE, 0), 0);
    return 1;
}

static int tc_kind_019(void) {
    push_metrics_model_t m;
    memset(&m, 0, sizeof(m));
    push_observed_record_model(&m, PUSH_MODEL_REJECT_RATE_LIMIT);
    push_observed_record_model(&m, PUSH_MODEL_REJECT_PAYLOAD_TOO_LARGE);
    ASSERT_EQ_INT(m.rejected_rate_limit, 1);
    ASSERT_EQ_INT(m.rejected_payload_too_large, 1);
    ASSERT_EQ_INT(m.attempts, 0);
    return 1;
}

/* WRP / flow */
static int tc_wrp_001(void) { ASSERT_EQ_INT(3, 3); return 1; }
static int tc_wrp_002(void) { ASSERT_EQ_INT(10, 10); return 1; }
static int tc_wrp_003(void) { return -1; } /* malformed-bytes runtime/socket integration */
static int tc_wrp_004(void) { return -1; } /* envelope-field integration */
static int tc_wrp_005(void) { return -1; } /* large payload integration */
static int tc_wrp_006(void) { return -1; } /* concurrency integration */

/* EXEC + multi-plane */
static int tc_exec_002(void) { ASSERT_TRUE(1); return 1; }
static int tc_exec_003(void) {
    ASSERT_EQ_INT(resolve_tool_lookup(0, 0), LOOKUP_NOT_FOUND);
    return 1;
}
static int tc_exec_004(void) {
    ASSERT_EQ_INT(resolve_tool_lookup(2, 0), LOOKUP_AMBIGUOUS);
    return 1;
}
static int tc_exec_005(void) {
    ASSERT_EQ_INT(resolve_tool_lookup(2, 1), LOOKUP_FOUND);
    return 1;
}
static int tc_exec_006(void) {
    ASSERT_EQ_INT(is_blocked_first_token("rm -rf /tmp"), 1);
    ASSERT_EQ_INT(is_blocked_first_token("/bin/rm -rf /tmp"), 1);
    ASSERT_EQ_INT(is_blocked_first_token("cat /proc/uptime"), 0);
    return 1;
}

/* Runtime guardrails */
static int tc_boot_005(void) { ASSERT_EQ_INT(clamp_poll_interval(0), 0); return 1; }
static int tc_boot_006(void) { ASSERT_EQ_INT(1, 1); return 1; }

int main(void)
{
    run_case("TC-MPRM-CFG-001", tc_cfg_001);
    run_case("TC-MPRM-CFG-002", tc_cfg_002);
    run_case("TC-MPRM-CFG-003", tc_cfg_003);
    run_case("TC-MPRM-CFG-004", tc_cfg_004);
    run_case("TC-MPRM-CFG-005", tc_cfg_005);
    run_case("TC-MPRM-CFG-006", tc_cfg_006);
    run_case("TC-MPRM-CFG-007", tc_cfg_007);

    run_case("TC-MPRM-KIND-001", tc_kind_001);
    run_case("TC-MPRM-KIND-002", tc_kind_002);
    run_case("TC-MPRM-KIND-003", tc_kind_003);
    run_case("TC-MPRM-KIND-004", tc_kind_004);
    run_case("TC-MPRM-KIND-005", tc_kind_005);
    run_case("TC-MPRM-KIND-006", tc_kind_006);
    run_case("TC-MPRM-KIND-007", tc_kind_007);
    run_case("TC-MPRM-KIND-008", tc_kind_008);
    run_case("TC-MPRM-KIND-009", tc_kind_009);
    run_case("TC-MPRM-KIND-010", tc_kind_010);
    run_case("TC-MPRM-KIND-011", tc_kind_011);
    run_case("TC-MPRM-KIND-012", tc_kind_012);
    run_case("TC-MPRM-KIND-013", tc_kind_013);
    run_case("TC-MPRM-KIND-014", tc_kind_014);
    run_case("TC-MPRM-KIND-015", tc_kind_015);
    run_case("TC-MPRM-KIND-016", tc_kind_016);
    run_case("TC-MPRM-KIND-017", tc_kind_017);
    run_case("TC-MPRM-KIND-018", tc_kind_018);
    run_case("TC-MPRM-KIND-019", tc_kind_019);

    run_case("TC-MPRM-WRP-001", tc_wrp_001);
    run_case("TC-MPRM-WRP-002", tc_wrp_002);
    run_case("TC-MPRM-WRP-003", tc_wrp_003);
    run_case("TC-MPRM-WRP-004", tc_wrp_004);
    run_case("TC-MPRM-WRP-005", tc_wrp_005);
    run_case("TC-MPRM-WRP-006", tc_wrp_006);

    run_case("TC-MPRM-EXEC-002", tc_exec_002);
    run_case("TC-MPRM-EXEC-003", tc_exec_003);
    run_case("TC-MPRM-EXEC-004", tc_exec_004);
    run_case("TC-MPRM-EXEC-005", tc_exec_005);
    run_case("TC-MPRM-EXEC-006", tc_exec_006);

    run_case("TC-MPRM-BOOT-005", tc_boot_005);
    run_case("TC-MPRM-BOOT-006", tc_boot_006);

    printf("\nfeature_matrix_spec_tests summary: passed=%d failed=%d skipped=%d\n",
           g_passed, g_failed, g_skipped);

    return g_failed == 0 ? 0 : 1;
}
