#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/plugin_api.h"

struct diag_plugin_ctx {
    const diag_host_api_t *host;
};

static const char *g_tools[] = {
    "plugin_sleep_slow"
};

int diag_plugin_get_api_version(void)
{
    return DIAG_PLUGIN_API_VERSION;
}

int diag_plugin_init(const diag_host_api_t *host, diag_plugin_ctx_t **ctx)
{
    diag_plugin_ctx_t *c = (diag_plugin_ctx_t *)calloc(1, sizeof(*c));
    if (!c) return -1;
    c->host = host;
    *ctx = c;
    return 0;
}

size_t diag_plugin_get_tool_count(diag_plugin_ctx_t *ctx)
{
    (void)ctx;
    return sizeof(g_tools) / sizeof(g_tools[0]);
}

int diag_plugin_get_tool(diag_plugin_ctx_t *ctx, size_t idx, diag_tool_def_t *out)
{
    (void)ctx;
    if (!out || idx >= (sizeof(g_tools) / sizeof(g_tools[0])))
        return -1;
    out->tool_name = g_tools[idx];
    out->tool_desc = "slow invoke test plugin tool";
    out->flags = 0;
    return 0;
}

int diag_plugin_invoke(diag_plugin_ctx_t *ctx, const diag_invoke_req_t *req, diag_invoke_resp_t *resp)
{
    static const char *ok = "slow-ok";
    (void)ctx;

    if (!req || !resp || !req->tool)
        return -1;

    if (strcmp(req->tool, "plugin_sleep_slow") != 0)
        return -1;

    usleep(2000000);
    resp->exit_code = 0;
    resp->out = ok;
    resp->out_len = strlen(ok);
    return 0;
}

void diag_plugin_deinit(diag_plugin_ctx_t *ctx)
{
    free(ctx);
}
