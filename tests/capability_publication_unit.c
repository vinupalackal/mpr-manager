#include <stdio.h>
#include <string.h>

#include "../src/capability_publication.h"

#define TASSERT(x) do { if (!(x)) { fprintf(stderr, "assert failed: %s\n", #x); return 1; } } while (0)

static int has_tool(const capability_snapshot_t *s, const char *plane, const char *name, const char *type)
{
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->items[i].plane, plane) == 0 &&
            strcmp(s->items[i].name, name) == 0 &&
            strcmp(s->items[i].type, type) == 0)
            return 1;
    }
    return 0;
}

int main(void)
{
    capability_entry_t catalog[3];
    capability_entry_t dynamic[2];
    capability_snapshot_t out;

    memset(catalog, 0, sizeof(catalog));
    memset(dynamic, 0, sizeof(dynamic));
    memset(&out, 0, sizeof(out));

    snprintf(catalog[0].name, sizeof(catalog[0].name), "%s", "uptime");
    snprintf(catalog[0].type, sizeof(catalog[0].type), "%s", "static");
    snprintf(catalog[0].plane, sizeof(catalog[0].plane), "%s", "triage");
    catalog[0].timeout = 5;
    catalog[0].provider = CAP_PROVIDER_CATALOG;

    snprintf(catalog[1].name, sizeof(catalog[1].name), "%s", "diag_ping");
    snprintf(catalog[1].type, sizeof(catalog[1].type), "%s", "static");
    snprintf(catalog[1].plane, sizeof(catalog[1].plane), "%s", "triage");
    catalog[1].timeout = 10;
    catalog[1].provider = CAP_PROVIDER_CATALOG;

    snprintf(catalog[2].name, sizeof(catalog[2].name), "%s", "manager_status");
    snprintf(catalog[2].type, sizeof(catalog[2].type), "%s", "static");
    snprintf(catalog[2].plane, sizeof(catalog[2].plane), "%s", "management");
    catalog[2].timeout = 15;
    catalog[2].provider = CAP_PROVIDER_CATALOG;

    snprintf(dynamic[0].name, sizeof(dynamic[0].name), "%s", "diag_ping");
    snprintf(dynamic[0].type, sizeof(dynamic[0].type), "%s", "dynamic");
    snprintf(dynamic[0].plane, sizeof(dynamic[0].plane), "%s", "triage");
    dynamic[0].timeout = 0;
    dynamic[0].provider = CAP_PROVIDER_PLUGIN;

    snprintf(dynamic[1].name, sizeof(dynamic[1].name), "%s", "plugin_echo");
    snprintf(dynamic[1].type, sizeof(dynamic[1].type), "%s", "dynamic");
    snprintf(dynamic[1].plane, sizeof(dynamic[1].plane), "%s", "triage");
    dynamic[1].timeout = 0;
    dynamic[1].provider = CAP_PROVIDER_PLUGIN;

    TASSERT(capability_snapshot_build(catalog, 3, dynamic, 2, 0, &out) == 0);
    TASSERT(out.count == 4);
    TASSERT(has_tool(&out, "triage", "diag_ping", "static"));
    TASSERT(has_tool(&out, "triage", "plugin_echo", "dynamic"));
    capability_snapshot_free(&out);

    TASSERT(capability_snapshot_build(catalog, 3, dynamic, 2, 1, &out) == 0);
    TASSERT(out.count == 4);
    TASSERT(has_tool(&out, "triage", "diag_ping", "dynamic"));
    TASSERT(has_tool(&out, "triage", "uptime", "static"));
    capability_snapshot_free(&out);

    TASSERT(capability_snapshot_build(NULL, 0, NULL, 0, 0, &out) == 0);
    TASSERT(out.count == 0);

    printf("capability_publication_unit: PASS\n");
    return 0;
}
