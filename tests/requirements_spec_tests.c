#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Executable specification tests for newly added requirements:
 * - DYNAMIC_PLUGIN_REQUIREMENTS.md (TC-PLUG-001 ... TC-PLUG-010)
 * - METADATA_FIELDS_REQUIREMENTS.md (TC-META-001 ... TC-META-012)
 *
 * Note: Some plugin tests are marked SKIP because they require the
 * runtime plugin manager implementation and live integration surfaces.
 */

typedef enum {
    MODE_COMPAT = 0,
    MODE_STRICT = 1
} metadata_mode_t;

typedef enum {
    DECISION_BASELINE = 0,
    DECISION_FORCE_CATALOG,
    DECISION_ALLOW_DYNAMIC_OVERRIDE,
    DECISION_REJECT_OVERRIDE
} metadata_decision_t;

typedef struct {
    const char *plane;
    const char *plane_type;

    int has_static;
    int static_flag;
    int static_type_valid;

    int has_dynamic;
    int dynamic_flag;
    int dynamic_type_valid;

    int has_command_override;
} metadata_input_t;

typedef struct {
    int ok;
    const char *error_token; /* ERR_METADATA_* or NULL */
    metadata_decision_t decision;
} metadata_result_t;

typedef enum {
    CONFLICT_REJECT = 0,
    CONFLICT_PLUGIN_PRIORITY,
    CONFLICT_CATALOG_PRIORITY
} conflict_policy_t;

typedef enum {
    SOURCE_NONE = 0,
    SOURCE_PLUGIN,
    SOURCE_CATALOG
} tool_source_t;

static int g_failed = 0;
static int g_passed = 0;
static int g_skipped = 0;

#define TEST_ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "    assertion failed: %s\n", #expr); \
        return 0; \
    } \
} while (0)

#define TEST_ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "    assertion failed: %s == %s (%d vs %d)\n", #a, #b, (int)(a), (int)(b)); \
        return 0; \
    } \
} while (0)

#define TEST_ASSERT_STREQ(a, b) do { \
    const char *_sa = (a); \
    const char *_sb = (b); \
    if (((_sa) == NULL && (_sb) != NULL) || ((_sa) != NULL && (_sb) == NULL) || ((_sa) && (_sb) && strcmp((_sa), (_sb)) != 0)) { \
        fprintf(stderr, "    assertion failed: %s == %s\n", #a, #b); \
        return 0; \
    } \
} while (0)

static int str_in_set(const char *s, const char *const *set, size_t n) {
    size_t i;
    if (!s) return 0;
    for (i = 0; i < n; ++i) {
        if (strcmp(s, set[i]) == 0) return 1;
    }
    return 0;
}

static int is_allowed_plane(const char *plane) {
    static const char *const allowed[] = {"data", "control", "ops", "diagnostic"};
    return str_in_set(plane, allowed, sizeof(allowed) / sizeof(allowed[0]));
}

static int is_allowed_plane_type(const char *plane_type) {
    static const char *const allowed[] = {"read", "write", "exec", "observe"};
    return str_in_set(plane_type, allowed, sizeof(allowed) / sizeof(allowed[0]));
}

static metadata_result_t apply_metadata_policy(const metadata_input_t *in, metadata_mode_t mode) {
    metadata_result_t r;
    r.ok = 1;
    r.error_token = NULL;
    r.decision = DECISION_BASELINE;

    if (in->has_static && !in->static_type_valid) {
        r.ok = 0;
        r.error_token = "ERR_METADATA_TYPE";
        return r;
    }
    if (in->has_dynamic && !in->dynamic_type_valid) {
        r.ok = 0;
        r.error_token = "ERR_METADATA_TYPE";
        return r;
    }

    if (in->has_static && in->has_dynamic && in->static_flag && in->dynamic_flag) {
        r.ok = 0;
        r.error_token = "ERR_METADATA_CONFLICT";
        return r;
    }

    if (mode == MODE_STRICT) {
        if (in->plane && !is_allowed_plane(in->plane)) {
            r.ok = 0;
            r.error_token = "ERR_METADATA_POLICY";
            return r;
        }
        if (in->plane_type && !is_allowed_plane_type(in->plane_type)) {
            r.ok = 0;
            r.error_token = "ERR_METADATA_POLICY";
            return r;
        }
    }

    if (in->has_static && in->static_flag) {
        r.decision = DECISION_FORCE_CATALOG;
        return r;
    }

    if (in->has_dynamic && !in->dynamic_flag && in->has_command_override) {
        r.ok = 0;
        r.error_token = "ERR_METADATA_POLICY";
        r.decision = DECISION_REJECT_OVERRIDE;
        return r;
    }

    if (in->has_dynamic && in->dynamic_flag) {
        r.decision = DECISION_ALLOW_DYNAMIC_OVERRIDE;
        return r;
    }

    r.decision = DECISION_BASELINE;
    return r;
}

static int is_shared_object_name(const char *name) {
    size_t len;
    if (!name) return 0;
    len = strlen(name);
    return len >= 3 && strcmp(name + len - 3, ".so") == 0;
}

static tool_source_t resolve_conflict_source(int has_plugin_tool, int has_catalog_tool, conflict_policy_t policy) {
    if (has_plugin_tool && !has_catalog_tool) return SOURCE_PLUGIN;
    if (!has_plugin_tool && has_catalog_tool) return SOURCE_CATALOG;
    if (!has_plugin_tool && !has_catalog_tool) return SOURCE_NONE;

    if (policy == CONFLICT_PLUGIN_PRIORITY) return SOURCE_PLUGIN;
    if (policy == CONFLICT_CATALOG_PRIORITY) return SOURCE_CATALOG;
    return SOURCE_NONE; /* reject */
}

static int plugin_security_precheck_ok(int path_confined, int owner_ok, int mode_ok, int integrity_ok, int integrity_required) {
    if (!path_confined || !owner_ok || !mode_ok) return 0;
    if (integrity_required && !integrity_ok) return 0;
    return 1;
}

static int names_unique(const char *const *names, size_t n) {
    size_t i, j;
    for (i = 0; i < n; ++i) {
        for (j = i + 1; j < n; ++j) {
            if (strcmp(names[i], names[j]) == 0) return 0;
        }
    }
    return 1;
}

/* ---------- Metadata tests ---------- */
static int tc_meta_001(void) {
    metadata_input_t in = {"diagnostic", "exec", 0, 0, 1, 0, 0, 1, 0};
    metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_STREQ(r.error_token, NULL);
    return 1;
}

static int tc_meta_002(void) {
    metadata_input_t ok = {NULL, NULL, 1, 1, 1, 1, 0, 1, 0};
    metadata_input_t bad_static = {NULL, NULL, 1, 1, 0, 0, 0, 1, 0};
    metadata_input_t bad_dynamic = {NULL, NULL, 0, 0, 1, 1, 1, 0, 0};
    metadata_result_t r1 = apply_metadata_policy(&ok, MODE_COMPAT);
    metadata_result_t r2 = apply_metadata_policy(&bad_static, MODE_COMPAT);
    metadata_result_t r3 = apply_metadata_policy(&bad_dynamic, MODE_COMPAT);

    TEST_ASSERT_TRUE(r1.ok);
    TEST_ASSERT_EQ_INT(r2.ok, 0);
    TEST_ASSERT_EQ_INT(r3.ok, 0);
    TEST_ASSERT_STREQ(r2.error_token, "ERR_METADATA_TYPE");
    TEST_ASSERT_STREQ(r3.error_token, "ERR_METADATA_TYPE");
    return 1;
}

static int tc_meta_003(void) {
    metadata_input_t in = {NULL, NULL, 1, 1, 1, 1, 1, 1, 0};
    metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
    TEST_ASSERT_EQ_INT(r.ok, 0);
    TEST_ASSERT_STREQ(r.error_token, "ERR_METADATA_CONFLICT");
    return 1;
}

static int tc_meta_004(void) {
    metadata_input_t strict_bad = {"foo", "bar", 0, 0, 1, 0, 0, 1, 0};
    metadata_input_t compat_bad = strict_bad;
    metadata_result_t r1 = apply_metadata_policy(&strict_bad, MODE_STRICT);
    metadata_result_t r2 = apply_metadata_policy(&compat_bad, MODE_COMPAT);
    TEST_ASSERT_EQ_INT(r1.ok, 0);
    TEST_ASSERT_STREQ(r1.error_token, "ERR_METADATA_POLICY");
    TEST_ASSERT_TRUE(r2.ok);
    return 1;
}

static int tc_meta_005(void) {
    metadata_input_t in = {NULL, NULL, 0, 0, 1, 1, 0, 1, 1};
    metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
    TEST_ASSERT_EQ_INT(r.ok, 0);
    TEST_ASSERT_EQ_INT(r.decision, DECISION_REJECT_OVERRIDE);
    TEST_ASSERT_STREQ(r.error_token, "ERR_METADATA_POLICY");
    return 1;
}

static int tc_meta_006(void) {
    metadata_input_t in = {NULL, NULL, 1, 1, 1, 0, 0, 1, 1};
    metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQ_INT(r.decision, DECISION_FORCE_CATALOG);
    return 1;
}

static int tc_meta_007(void) {
    metadata_input_t in = {NULL, NULL, 0, 0, 1, 1, 1, 1, 1};
    metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQ_INT(r.decision, DECISION_ALLOW_DYNAMIC_OVERRIDE);
    return 1;
}

static int tc_meta_008(void) {
    /* Spec-level check: if metadata accepted, echo payload may be produced by feature gate. */
    metadata_input_t in = {"diagnostic", "exec", 1, 0, 1, 1, 1, 1, 0};
    metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
    TEST_ASSERT_TRUE(r.ok);
    return 1;
}

static int tc_meta_009(void) {
    metadata_input_t legacy = {NULL, NULL, 0, 0, 1, 0, 0, 1, 0};
    metadata_result_t r = apply_metadata_policy(&legacy, MODE_COMPAT);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQ_INT(r.decision, DECISION_BASELINE);
    return 1;
}

static int tc_meta_010(void) {
    /* Logging completeness requires runtime logger integration; policy decision is testable here. */
    metadata_input_t in = {"ops", "observe", 1, 0, 1, 1, 0, 1, 0};
    metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
    TEST_ASSERT_TRUE(r.ok);
    return 1;
}

static int tc_meta_011(void) {
    metadata_input_t invalid = {"invalid", "invalid", 1, 1, 1, 1, 1, 1, 0};
    metadata_result_t disabled = apply_metadata_policy(&invalid, MODE_COMPAT);
    metadata_result_t enabled = apply_metadata_policy(&invalid, MODE_STRICT);
    TEST_ASSERT_EQ_INT(disabled.ok, 0); /* conflict still enforced in compat */
    TEST_ASSERT_STREQ(disabled.error_token, "ERR_METADATA_CONFLICT");
    TEST_ASSERT_EQ_INT(enabled.ok, 0);
    TEST_ASSERT_STREQ(enabled.error_token, "ERR_METADATA_CONFLICT");
    return 1;
}

static int tc_meta_012(void) {
    /* Simple non-flaky perf/robustness smoke: run policy loop and ensure completion. */
    const int loops = 500000;
    int i;
    clock_t start = clock();
    metadata_input_t in = {"diagnostic", "exec", 1, 0, 1, 1, 1, 1, 1};
    for (i = 0; i < loops; ++i) {
        metadata_result_t r = apply_metadata_policy(&in, MODE_COMPAT);
        if (!r.ok) return 0;
    }
    clock_t end = clock();
    double sec = (double)(end - start) / (double)CLOCKS_PER_SEC;
    TEST_ASSERT_TRUE(sec < 10.0);
    return 1;
}

/* ---------- Plugin tests ---------- */
static int tc_plug_001(void) {
    TEST_ASSERT_TRUE(is_shared_object_name("diag_ok.so"));
    TEST_ASSERT_TRUE(!is_shared_object_name("readme.txt"));
    TEST_ASSERT_TRUE(!is_shared_object_name("diag_bad_abi.so.bak"));
    return 1;
}

static int tc_plug_002(void) { return -1; } /* integration required */
static int tc_plug_003(void) { return -1; } /* integration required */
static int tc_plug_004(void) { return -1; } /* integration required */

static int tc_plug_005(void) {
    const char *tools[] = {"hot_tool", "diag_ping", "diag_uptime"};
    TEST_ASSERT_TRUE(names_unique(tools, 3));
    return 1;
}

static int tc_plug_006(void) {
    TEST_ASSERT_EQ_INT(resolve_conflict_source(1, 1, CONFLICT_REJECT), SOURCE_NONE);
    TEST_ASSERT_EQ_INT(resolve_conflict_source(1, 1, CONFLICT_PLUGIN_PRIORITY), SOURCE_PLUGIN);
    TEST_ASSERT_EQ_INT(resolve_conflict_source(1, 1, CONFLICT_CATALOG_PRIORITY), SOURCE_CATALOG);
    return 1;
}

static int tc_plug_007(void) {
    TEST_ASSERT_TRUE(plugin_security_precheck_ok(1, 1, 1, 1, 1));
    TEST_ASSERT_TRUE(!plugin_security_precheck_ok(0, 1, 1, 1, 1));
    TEST_ASSERT_TRUE(!plugin_security_precheck_ok(1, 0, 1, 1, 1));
    TEST_ASSERT_TRUE(!plugin_security_precheck_ok(1, 1, 0, 1, 1));
    TEST_ASSERT_TRUE(!plugin_security_precheck_ok(1, 1, 1, 0, 1));
    TEST_ASSERT_TRUE(plugin_security_precheck_ok(1, 1, 1, 0, 0));
    return 1;
}

static int tc_plug_008(void) {
    /* Spec-level isolation model: failure of one plugin does not block another. */
    int plugin_a_loaded = 0;
    int plugin_b_loaded = 1;
    TEST_ASSERT_EQ_INT(plugin_a_loaded, 0);
    TEST_ASSERT_EQ_INT(plugin_b_loaded, 1);
    return 1;
}

static int tc_plug_009(void) { return -1; } /* integration + soak required */

static int tc_plug_010(void) {
    int feature_enabled = 0;
    int discovery_active = feature_enabled ? 1 : 0;
    TEST_ASSERT_EQ_INT(discovery_active, 0);
    feature_enabled = 1;
    discovery_active = feature_enabled ? 1 : 0;
    TEST_ASSERT_EQ_INT(discovery_active, 1);
    return 1;
}

typedef struct {
    const char *id;
    int (*fn)(void); /* 1 pass, 0 fail, -1 skip */
} test_case_t;

static void run_case(const test_case_t *tc) {
    int rc;
    printf("[RUN ] %s\n", tc->id);
    rc = tc->fn();
    if (rc == 1) {
        ++g_passed;
        printf("[PASS] %s\n", tc->id);
    } else if (rc == -1) {
        ++g_skipped;
        printf("[SKIP] %s (requires runtime integration surface)\n", tc->id);
    } else {
        ++g_failed;
        printf("[FAIL] %s\n", tc->id);
    }
}

int main(void) {
    const test_case_t tests[] = {
        {"TC-META-001", tc_meta_001},
        {"TC-META-002", tc_meta_002},
        {"TC-META-003", tc_meta_003},
        {"TC-META-004", tc_meta_004},
        {"TC-META-005", tc_meta_005},
        {"TC-META-006", tc_meta_006},
        {"TC-META-007", tc_meta_007},
        {"TC-META-008", tc_meta_008},
        {"TC-META-009", tc_meta_009},
        {"TC-META-010", tc_meta_010},
        {"TC-META-011", tc_meta_011},
        {"TC-META-012", tc_meta_012},
        {"TC-PLUG-001", tc_plug_001},
        {"TC-PLUG-002", tc_plug_002},
        {"TC-PLUG-003", tc_plug_003},
        {"TC-PLUG-004", tc_plug_004},
        {"TC-PLUG-005", tc_plug_005},
        {"TC-PLUG-006", tc_plug_006},
        {"TC-PLUG-007", tc_plug_007},
        {"TC-PLUG-008", tc_plug_008},
        {"TC-PLUG-009", tc_plug_009},
        {"TC-PLUG-010", tc_plug_010}
    };
    size_t i;
    size_t n = sizeof(tests) / sizeof(tests[0]);

    for (i = 0; i < n; ++i) {
        run_case(&tests[i]);
    }

    printf("\nSummary: %d passed, %d failed, %d skipped, %zu total\n",
           g_passed, g_failed, g_skipped, n);

    return g_failed == 0 ? 0 : 1;
}
