#ifndef METADATA_FIELDS_H
#define METADATA_FIELDS_H

#include <stddef.h>

typedef struct {
    char *plane;
    char *plane_type;
    char *request_type;
    char *request_sub_type;

    int has_static;
    int static_flag;
    int static_type_valid;

    int has_dynamic;
    int dynamic_flag;
    int dynamic_type_valid;

    int metadata_type_valid;
    int has_metadata_obj;
} req_metadata_t;

typedef enum {
    META_OK = 0,
    META_ERR_TYPE,
    META_ERR_CONFLICT,
    META_ERR_POLICY
} meta_status_t;

typedef struct {
    int enabled;
    int strict_mode;
} meta_cfg_t;

typedef enum {
    META_DECISION_BASELINE = 0,
    META_DECISION_FORCE_CATALOG,
    META_DECISION_ALLOW_OVERRIDE,
    META_DECISION_REJECT_OVERRIDE
} meta_decision_t;

void meta_init(req_metadata_t *m);
void meta_free(req_metadata_t *m);

meta_cfg_t meta_cfg_from_env(void);

meta_status_t meta_validate(const req_metadata_t *m,
                            const meta_cfg_t *cfg,
                            char *reason,
                            size_t reason_len);

meta_status_t meta_apply_policy(const req_metadata_t *m,
                                const char *incoming_command,
                                int *allow_override,
                                meta_decision_t *decision,
                                char *reason,
                                size_t reason_len);

const char *meta_error_token(meta_status_t st);
const char *meta_decision_name(meta_decision_t d);
int meta_error_exit_code(meta_status_t st);

#endif
