#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Integration runner for plugin runtime test cases that require a live
 * environment and plugin-manager implementation.
 *
 * Covered IDs:
 *   TC-PLUG-002, TC-PLUG-003, TC-PLUG-004, TC-PLUG-009
 *
 * Behavior:
 * - Returns 77 (skip) if integration mode is not enabled.
 * - Runs per-case shell hooks when configured via environment variables.
 * - Fails if a configured hook returns non-zero.
 * - Returns 77 if enabled but no case hook is configured.
 */

typedef struct {
    const char *id;
    const char *env_name;
} it_case_t;

static int run_hook(const char *id, const char *cmd) {
    int rc;
    printf("[RUN ] %s\n", id);
    printf("       hook: %s\n", cmd);
    rc = system(cmd);
    if (rc == 0) {
        printf("[PASS] %s\n", id);
        return 1;
    }
    printf("[FAIL] %s (hook exit=%d)\n", id, rc);
    return 0;
}

int main(void) {
    const char *enabled = getenv("MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_IT_ENABLE");
    const char *plugin_dir = getenv("MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_DIR");
    const it_case_t cases[] = {
        {"TC-PLUG-002", "MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_002_CMD"},
        {"TC-PLUG-003", "MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_003_CMD"},
        {"TC-PLUG-004", "MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_004_CMD"},
        {"TC-PLUG-009", "MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_009_CMD"}
    };

    size_t i;
    int executed = 0;
    int failed = 0;

    if (!enabled || strcmp(enabled, "1") != 0) {
        printf("[SKIP] plugin integration disabled (set MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_IT_ENABLE=1)\n");
        return 77;
    }

    if (!plugin_dir || plugin_dir[0] == '\0') {
        printf("[INFO] MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_DIR is not set. Continuing with hook-only execution.\n");
    } else {
        printf("[INFO] plugin dir: %s\n", plugin_dir);
    }

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *cmd = getenv(cases[i].env_name);
        if (!cmd || cmd[0] == '\0') {
            printf("[SKIP] %s (set %s to run this case)\n", cases[i].id, cases[i].env_name);
            continue;
        }
        ++executed;
        if (!run_hook(cases[i].id, cmd)) {
            failed = 1;
        }
    }

    if (executed == 0) {
        printf("[SKIP] no integration hooks configured\n");
        return 77;
    }

    if (failed) {
        printf("\nIntegration summary: FAILED\n");
        return 1;
    }

    printf("\nIntegration summary: PASSED (%d case hooks executed)\n", executed);
    return 0;
}
