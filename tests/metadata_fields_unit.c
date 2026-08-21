#include <stdio.h>
#include <string.h>
#include "../src/metadata_fields.h"

#define TASSERT(cond) do { if (!(cond)) { fprintf(stderr, "assert failed: %s\n", #cond); return 1; } } while (0)

int main(void)
{
    req_metadata_t m;
    meta_cfg_t cfg;
    meta_status_t st;
    int allow = 0;
    meta_decision_t dec = META_DECISION_BASELINE;
    char reason[128];

    meta_init(&m);

    cfg.enabled = 1;
    cfg.strict_mode = 0;

    /* Type error */
    m.has_static = 1;
    m.static_type_valid = 0;
    st = meta_validate(&m, &cfg, reason, sizeof(reason));
    TASSERT(st == META_ERR_TYPE);

    meta_free(&m);
    meta_init(&m);

    /* Conflict */
    m.has_static = 1;
    m.static_flag = 1;
    m.has_dynamic = 1;
    m.dynamic_flag = 1;
    st = meta_validate(&m, &cfg, reason, sizeof(reason));
    TASSERT(st == META_ERR_CONFLICT);

    meta_free(&m);
    meta_init(&m);

    /* Strict enum check */
    cfg.strict_mode = 1;
    m.plane = strdup("invalid");
    st = meta_validate(&m, &cfg, reason, sizeof(reason));
    TASSERT(st == META_ERR_POLICY);

    meta_free(&m);
    meta_init(&m);

    /* Strict canonical/alias taxonomy checks */
    cfg.strict_mode = 1;
    m.plane = strdup("triage");
    st = meta_validate(&m, &cfg, reason, sizeof(reason));
    TASSERT(st == META_OK);
    meta_free(&m);
    meta_init(&m);

    m.plane = strdup("diagnostic");
    st = meta_validate(&m, &cfg, reason, sizeof(reason));
    TASSERT(st == META_OK);
    {
        int normalized = 0;
        const char *canon = meta_plane_canonical("diagnostic", &normalized);
        TASSERT(canon != NULL);
        TASSERT(strcmp(canon, "triage") == 0);
        TASSERT(normalized == 1);
    }

    {
        int normalized = 0;
        const char *canon = meta_plane_canonical("management", &normalized);
        TASSERT(canon != NULL);
        TASSERT(strcmp(canon, "management") == 0);
        TASSERT(normalized == 0);
    }

    {
        int normalized = 0;
        const char *canon = meta_plane_canonical("unknown", &normalized);
        TASSERT(canon == NULL);
        TASSERT(normalized == 0);
    }

    meta_free(&m);
    meta_init(&m);

    /* dynamic=false + override => policy reject */
    cfg.strict_mode = 0;
    m.has_dynamic = 1;
    m.dynamic_flag = 0;
    st = meta_apply_policy(&m, "echo hi", &allow, &dec, reason, sizeof(reason));
    TASSERT(st == META_ERR_POLICY);
    TASSERT(allow == 0);
    TASSERT(dec == META_DECISION_REJECT_OVERRIDE);

    meta_free(&m);
    meta_init(&m);

    /* static=true forces catalog */
    m.has_static = 1;
    m.static_flag = 1;
    st = meta_apply_policy(&m, "echo hi", &allow, &dec, reason, sizeof(reason));
    TASSERT(st == META_OK);
    TASSERT(allow == 0);
    TASSERT(dec == META_DECISION_FORCE_CATALOG);

    meta_free(&m);
    meta_init(&m);

    /* dynamic=true allows override */
    m.has_dynamic = 1;
    m.dynamic_flag = 1;
    st = meta_apply_policy(&m, "echo hi", &allow, &dec, reason, sizeof(reason));
    TASSERT(st == META_OK);
    TASSERT(allow == 1);
    TASSERT(dec == META_DECISION_ALLOW_OVERRIDE);

    /* Error token mapping */
    TASSERT(strcmp(meta_error_token(META_ERR_TYPE), "ERR_METADATA_TYPE") == 0);
    TASSERT(meta_error_exit_code(META_ERR_TYPE) == 2);
    TASSERT(meta_error_exit_code(META_ERR_CONFLICT) == 3);
    TASSERT(meta_error_exit_code(META_ERR_POLICY) == 4);

    meta_free(&m);
    printf("metadata_fields_unit: PASS\n");
    return 0;
}
