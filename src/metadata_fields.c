#include "metadata_fields.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS
#define MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS 1
#endif

static int str_eq(const char *a, const char *b)
{
    return (a && b && strcmp(a, b) == 0);
}

static int in_set(const char *value, const char *const *set, size_t n)
{
    size_t i;
    if (!value) return 0;
    for (i = 0; i < n; i++) {
        if (strcmp(value, set[i]) == 0) return 1;
    }
    return 0;
}

static void set_reason(char *reason, size_t reason_len, const char *msg)
{
    if (!reason || reason_len == 0) return;
    if (!msg) {
        reason[0] = '\0';
        return;
    }
    snprintf(reason, reason_len, "%s", msg);
}

void meta_init(req_metadata_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->static_type_valid = 1;
    m->dynamic_type_valid = 1;
    m->metadata_type_valid = 1;
}

void meta_free(req_metadata_t *m)
{
    if (!m) return;
    free(m->plane);
    free(m->plane_type);
    free(m->request_type);
    free(m->request_sub_type);
    meta_init(m);
}

meta_cfg_t meta_cfg_from_env(void)
{
    meta_cfg_t cfg;
    const char *enable_env = getenv("MULTI_PLANE_RUNTIME_MANAGER_METADATA_ENABLE");
    const char *mode_env = getenv("MULTI_PLANE_RUNTIME_MANAGER_METADATA_MODE");

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS
    cfg.enabled = 1;
#else
    cfg.enabled = 0;
#endif

    if (enable_env) {
        if (strcmp(enable_env, "0") == 0 || str_eq(enable_env, "false") || str_eq(enable_env, "off"))
            cfg.enabled = 0;
#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS
        else if (strcmp(enable_env, "1") == 0 || str_eq(enable_env, "true") || str_eq(enable_env, "on"))
            cfg.enabled = 1;
#endif
    }

    cfg.strict_mode = (mode_env && strcmp(mode_env, "strict") == 0) ? 1 : 0;
    return cfg;
}

meta_status_t meta_validate(const req_metadata_t *m,
                            const meta_cfg_t *cfg,
                            char *reason,
                            size_t reason_len)
{
    static const char *const planes[] = {"data", "control", "ops", "diagnostic"};
    static const char *const plane_types[] = {"read", "write", "exec", "observe"};

    set_reason(reason, reason_len, NULL);

    if (!m || !cfg || !cfg->enabled)
        return META_OK;

    if (!m->static_type_valid || !m->dynamic_type_valid || !m->metadata_type_valid) {
        set_reason(reason, reason_len, "metadata field type mismatch");
        return META_ERR_TYPE;
    }

    if (m->has_static && m->has_dynamic && m->static_flag && m->dynamic_flag) {
        set_reason(reason, reason_len, "static and dynamic both true");
        return META_ERR_CONFLICT;
    }

    if (cfg->strict_mode) {
        if (m->plane && !in_set(m->plane, planes, sizeof(planes) / sizeof(planes[0]))) {
            set_reason(reason, reason_len, "invalid plane");
            return META_ERR_POLICY;
        }
        if (m->plane_type && !in_set(m->plane_type, plane_types, sizeof(plane_types) / sizeof(plane_types[0]))) {
            set_reason(reason, reason_len, "invalid plane_type");
            return META_ERR_POLICY;
        }
    }

    return META_OK;
}

meta_status_t meta_apply_policy(const req_metadata_t *m,
                                const char *incoming_command,
                                int *allow_override,
                                meta_decision_t *decision,
                                char *reason,
                                size_t reason_len)
{
    int has_override = (incoming_command && *incoming_command) ? 1 : 0;

    if (allow_override) *allow_override = has_override;
    if (decision) *decision = META_DECISION_BASELINE;
    set_reason(reason, reason_len, NULL);

    if (!m)
        return META_OK;

    /* static=true forces catalog path. */
    if (m->has_static && m->static_flag) {
        if (allow_override) *allow_override = 0;
        if (decision) *decision = META_DECISION_FORCE_CATALOG;
        return META_OK;
    }

    /* dynamic=false rejects caller override. */
    if (m->has_dynamic && !m->dynamic_flag && has_override) {
        if (allow_override) *allow_override = 0;
        if (decision) *decision = META_DECISION_REJECT_OVERRIDE;
        set_reason(reason, reason_len, "dynamic=false rejects command override");
        return META_ERR_POLICY;
    }

    /* dynamic=true permits caller override (subject to existing safety gates). */
    if (m->has_dynamic && m->dynamic_flag) {
        if (allow_override) *allow_override = has_override;
        if (decision) *decision = META_DECISION_ALLOW_OVERRIDE;
        return META_OK;
    }

    return META_OK;
}

const char *meta_error_token(meta_status_t st)
{
    switch (st) {
    case META_ERR_TYPE:
        return "ERR_METADATA_TYPE";
    case META_ERR_CONFLICT:
        return "ERR_METADATA_CONFLICT";
    case META_ERR_POLICY:
        return "ERR_METADATA_POLICY";
    case META_OK:
    default:
        return "";
    }
}

int meta_error_exit_code(meta_status_t st)
{
    switch (st) {
    case META_ERR_TYPE:
        return 2;
    case META_ERR_CONFLICT:
        return 3;
    case META_ERR_POLICY:
        return 4;
    case META_OK:
    default:
        return 0;
    }
}

const char *meta_decision_name(meta_decision_t d)
{
    switch (d) {
    case META_DECISION_FORCE_CATALOG:
        return "force_catalog";
    case META_DECISION_ALLOW_OVERRIDE:
        return "allow_override";
    case META_DECISION_REJECT_OVERRIDE:
        return "reject_override";
    case META_DECISION_BASELINE:
    default:
        return "baseline";
    }
}
