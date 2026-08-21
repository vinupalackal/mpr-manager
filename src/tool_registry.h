#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include <stddef.h>

typedef enum {
    TOOL_PROVIDER_CATALOG = 0,
    TOOL_PROVIDER_PLUGIN = 1
} tool_provider_t;

typedef struct {
    char *tool_name;
    tool_provider_t provider;
    void *plugin; /* plugin_record_t* owned by plugin_manager */
} tool_binding_t;

typedef struct {
    char *tool_name;
    tool_provider_t provider;
    void *plugin; /* plugin_record_t* owned by plugin_manager */
} tool_registry_entry_t;

int tool_registry_init(void);
int tool_registry_bind_plugin_tool(const char *tool_name, void *plugin, int conflict_policy);
int tool_registry_unbind_plugin_tools(void *plugin);
int tool_registry_lookup(const char *tool_name, tool_binding_t *out);
int tool_registry_count(void);
int tool_registry_snapshot(tool_registry_entry_t **out_entries, size_t *out_count);
void tool_registry_snapshot_free(tool_registry_entry_t *entries, size_t count);
void tool_registry_destroy(void);

#endif
