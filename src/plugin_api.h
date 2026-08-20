#ifndef DIAG_PLUGIN_API_H
#define DIAG_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#define DIAG_PLUGIN_API_VERSION 1

typedef struct diag_plugin_ctx diag_plugin_ctx_t;

typedef struct {
    void (*log_fn)(int level, const char *fmt, ...);
    const char *(*get_config_fn)(const char *key);
} diag_host_api_t;

typedef struct {
    const char *tool_name;
    const char *tool_desc;
    uint32_t flags;
} diag_tool_def_t;

typedef struct {
    const char *transaction_uuid;
    const char *source;
    const char *tool;
    const void *payload;
    size_t payload_len;
} diag_invoke_req_t;

typedef struct {
    int exit_code;
    const void *out;
    size_t out_len;
} diag_invoke_resp_t;

#endif
