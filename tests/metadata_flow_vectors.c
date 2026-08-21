#include <stdio.h>
#include <string.h>

#include "../src/metadata_fields.h"

typedef struct {
    const char *id;
    int strict_mode;

    const char *plane;
    const char *plane_type;

    int has_static;
    int static_flag;
    int static_type_valid;

    int has_dynamic;
    int dynamic_flag;
    int dynamic_type_valid;

    int metadata_type_valid;
    const char *incoming_command;

    meta_status_t expected_status;
    meta_decision_t expected_decision;
    int expected_allow_override;
    const char *expected_token;
} flow_vector_t;

static int run_vector(const flow_vector_t *v)
{
    req_metadata_t m;
    meta_cfg_t cfg;
    meta_status_t st;
    meta_decision_t decision = META_DECISION_BASELINE;
    int allow_override = 0;
    char reason[128];

    meta_init(&m);

    cfg.enabled = 1;
    cfg.strict_mode = v->strict_mode;

    if (v->plane) m.plane = strdup(v->plane);
    if (v->plane_type) m.plane_type = strdup(v->plane_type);

    m.has_static = v->has_static;
    m.static_flag = v->static_flag;
    m.static_type_valid = v->static_type_valid;

    m.has_dynamic = v->has_dynamic;
    m.dynamic_flag = v->dynamic_flag;
    m.dynamic_type_valid = v->dynamic_type_valid;

    m.metadata_type_valid = v->metadata_type_valid;

    st = meta_validate(&m, &cfg, reason, sizeof(reason));
    if (st == META_OK) {
        st = meta_apply_policy(&m, v->incoming_command, &allow_override,
                               &decision, reason, sizeof(reason));
    }

    if (st != v->expected_status) {
        fprintf(stderr, "[%s] status mismatch: got=%d expected=%d\n", v->id, st, v->expected_status);
        meta_free(&m);
        return 1;
    }
    if (decision != v->expected_decision) {
        fprintf(stderr, "[%s] decision mismatch: got=%d expected=%d\n", v->id, decision, v->expected_decision);
        meta_free(&m);
        return 1;
    }
    if (allow_override != v->expected_allow_override) {
        fprintf(stderr, "[%s] allow_override mismatch: got=%d expected=%d\n", v->id, allow_override, v->expected_allow_override);
        meta_free(&m);
        return 1;
    }

    if (strcmp(meta_error_token(st), v->expected_token) != 0) {
        fprintf(stderr, "[%s] token mismatch: got=%s expected=%s\n", v->id,
                meta_error_token(st), v->expected_token);
        meta_free(&m);
        return 1;
    }

    meta_free(&m);
    return 0;
}

int main(void)
{
    const flow_vector_t vectors[] = {
        {
            .id = "VEC-LEGACY-COMPAT",
            .strict_mode = 0,
            .plane = NULL,
            .plane_type = NULL,
            .has_static = 0,
            .static_flag = 0,
            .static_type_valid = 1,
            .has_dynamic = 0,
            .dynamic_flag = 0,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = "cat /proc/uptime",
            .expected_status = META_OK,
            .expected_decision = META_DECISION_BASELINE,
            .expected_allow_override = 1,
            .expected_token = ""
        },
        {
            .id = "VEC-STATIC-FORCES-CATALOG",
            .strict_mode = 0,
            .plane = "diagnostic",
            .plane_type = "exec",
            .has_static = 1,
            .static_flag = 1,
            .static_type_valid = 1,
            .has_dynamic = 0,
            .dynamic_flag = 0,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = "cat /etc/passwd",
            .expected_status = META_OK,
            .expected_decision = META_DECISION_FORCE_CATALOG,
            .expected_allow_override = 0,
            .expected_token = ""
        },
        {
            .id = "VEC-DYNAMIC-FALSE-REJECTS-OVERRIDE",
            .strict_mode = 0,
            .plane = "diagnostic",
            .plane_type = "exec",
            .has_static = 0,
            .static_flag = 0,
            .static_type_valid = 1,
            .has_dynamic = 1,
            .dynamic_flag = 0,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = "cat /proc/version",
            .expected_status = META_ERR_POLICY,
            .expected_decision = META_DECISION_REJECT_OVERRIDE,
            .expected_allow_override = 0,
            .expected_token = "ERR_METADATA_POLICY"
        },
        {
            .id = "VEC-DYNAMIC-TRUE-ALLOWS-OVERRIDE",
            .strict_mode = 0,
            .plane = "diagnostic",
            .plane_type = "exec",
            .has_static = 0,
            .static_flag = 0,
            .static_type_valid = 1,
            .has_dynamic = 1,
            .dynamic_flag = 1,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = "ls -la /tmp",
            .expected_status = META_OK,
            .expected_decision = META_DECISION_ALLOW_OVERRIDE,
            .expected_allow_override = 1,
            .expected_token = ""
        },
        {
            .id = "VEC-CONFLICT-STATIC-DYNAMIC",
            .strict_mode = 0,
            .plane = "diagnostic",
            .plane_type = "exec",
            .has_static = 1,
            .static_flag = 1,
            .static_type_valid = 1,
            .has_dynamic = 1,
            .dynamic_flag = 1,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = NULL,
            .expected_status = META_ERR_CONFLICT,
            .expected_decision = META_DECISION_BASELINE,
            .expected_allow_override = 0,
            .expected_token = "ERR_METADATA_CONFLICT"
        },
        {
            .id = "VEC-TYPE-ERROR-STATIC",
            .strict_mode = 0,
            .plane = "diagnostic",
            .plane_type = "exec",
            .has_static = 1,
            .static_flag = 1,
            .static_type_valid = 0,
            .has_dynamic = 0,
            .dynamic_flag = 0,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = NULL,
            .expected_status = META_ERR_TYPE,
            .expected_decision = META_DECISION_BASELINE,
            .expected_allow_override = 0,
            .expected_token = "ERR_METADATA_TYPE"
        },
        {
            .id = "VEC-STRICT-INVALID-PLANE",
            .strict_mode = 1,
            .plane = "foo",
            .plane_type = "exec",
            .has_static = 0,
            .static_flag = 0,
            .static_type_valid = 1,
            .has_dynamic = 0,
            .dynamic_flag = 0,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = NULL,
            .expected_status = META_ERR_POLICY,
            .expected_decision = META_DECISION_BASELINE,
            .expected_allow_override = 0,
            .expected_token = "ERR_METADATA_POLICY"
        },
        {
            .id = "VEC-STRICT-VALID-ENUMS",
            .strict_mode = 1,
            .plane = "triage",
            .plane_type = "observe",
            .has_static = 0,
            .static_flag = 0,
            .static_type_valid = 1,
            .has_dynamic = 0,
            .dynamic_flag = 0,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = NULL,
            .expected_status = META_OK,
            .expected_decision = META_DECISION_BASELINE,
            .expected_allow_override = 0,
            .expected_token = ""
        },
        {
            .id = "VEC-STRICT-LEGACY-ALIAS-OK",
            .strict_mode = 1,
            .plane = "ops",
            .plane_type = "observe",
            .has_static = 0,
            .static_flag = 0,
            .static_type_valid = 1,
            .has_dynamic = 0,
            .dynamic_flag = 0,
            .dynamic_type_valid = 1,
            .metadata_type_valid = 1,
            .incoming_command = NULL,
            .expected_status = META_OK,
            .expected_decision = META_DECISION_BASELINE,
            .expected_allow_override = 0,
            .expected_token = ""
        }
    };

    size_t i;
    int failures = 0;

    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        int rc = run_vector(&vectors[i]);
        if (rc == 0) {
            printf("[PASS] %s\n", vectors[i].id);
        } else {
            printf("[FAIL] %s\n", vectors[i].id);
            failures++;
        }
    }

    printf("\nmetadata_flow_vectors: %zu total, %d failed\n",
           sizeof(vectors) / sizeof(vectors[0]), failures);

    return failures == 0 ? 0 : 1;
}
