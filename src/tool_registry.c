#include "tool_registry.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct binding_node {
    tool_binding_t b;
    struct binding_node *next;
} binding_node_t;

static pthread_rwlock_t g_lock = PTHREAD_RWLOCK_INITIALIZER;
static binding_node_t *g_head = NULL;

void tool_registry_snapshot_free(tool_registry_entry_t *entries, size_t count);

static binding_node_t *find_node_unlocked(const char *tool_name)
{
    binding_node_t *n = g_head;
    while (n) {
        if (n->b.tool_name && strcmp(n->b.tool_name, tool_name) == 0)
            return n;
        n = n->next;
    }
    return NULL;
}

int tool_registry_init(void)
{
    g_head = NULL;
    return 0;
}

int tool_registry_bind_plugin_tool(const char *tool_name, void *plugin, int conflict_policy)
{
    binding_node_t *n;

    if (!tool_name || !*tool_name || !plugin)
        return -1;

    pthread_rwlock_wrlock(&g_lock);

    n = find_node_unlocked(tool_name);
    if (n) {
        if (conflict_policy == 1) { /* plugin-priority */
            n->b.provider = TOOL_PROVIDER_PLUGIN;
            n->b.plugin = plugin;
            pthread_rwlock_unlock(&g_lock);
            return 0;
        }
        pthread_rwlock_unlock(&g_lock);
        return -2; /* conflict */
    }

    n = (binding_node_t *)calloc(1, sizeof(*n));
    if (!n) {
        pthread_rwlock_unlock(&g_lock);
        return -3;
    }
    n->b.tool_name = strdup(tool_name);
    if (!n->b.tool_name) {
        free(n);
        pthread_rwlock_unlock(&g_lock);
        return -3;
    }
    n->b.provider = TOOL_PROVIDER_PLUGIN;
    n->b.plugin = plugin;
    n->next = g_head;
    g_head = n;

    pthread_rwlock_unlock(&g_lock);
    return 0;
}

int tool_registry_unbind_plugin_tools(void *plugin)
{
    binding_node_t *n, *prev = NULL;

    pthread_rwlock_wrlock(&g_lock);

    n = g_head;
    while (n) {
        if (n->b.provider == TOOL_PROVIDER_PLUGIN && n->b.plugin == plugin) {
            binding_node_t *dead = n;
            if (prev) prev->next = n->next;
            else g_head = n->next;
            n = n->next;
            free(dead->b.tool_name);
            free(dead);
            continue;
        }
        prev = n;
        n = n->next;
    }

    pthread_rwlock_unlock(&g_lock);
    return 0;
}

int tool_registry_lookup(const char *tool_name, tool_binding_t *out)
{
    binding_node_t *n;
    if (!tool_name || !out)
        return -1;

    memset(out, 0, sizeof(*out));

    pthread_rwlock_rdlock(&g_lock);
    n = find_node_unlocked(tool_name);
    if (!n) {
        pthread_rwlock_unlock(&g_lock);
        return 1; /* not found */
    }
    out->tool_name = n->b.tool_name;
    out->provider = n->b.provider;
    out->plugin = n->b.plugin;
    pthread_rwlock_unlock(&g_lock);
    return 0;
}

int tool_registry_count(void)
{
    int count = 0;
    binding_node_t *n;
    pthread_rwlock_rdlock(&g_lock);
    n = g_head;
    while (n) {
        count++;
        n = n->next;
    }
    pthread_rwlock_unlock(&g_lock);
    return count;
}

int tool_registry_snapshot(tool_registry_entry_t **out_entries, size_t *out_count)
{
    binding_node_t *n;
    size_t count = 0;
    size_t i = 0;
    tool_registry_entry_t *entries = NULL;

    if (!out_entries || !out_count)
        return -1;

    *out_entries = NULL;
    *out_count = 0;

    pthread_rwlock_rdlock(&g_lock);
    n = g_head;
    while (n) {
        count++;
        n = n->next;
    }

    if (count == 0) {
        pthread_rwlock_unlock(&g_lock);
        return 0;
    }

    entries = (tool_registry_entry_t *)calloc(count, sizeof(*entries));
    if (!entries) {
        pthread_rwlock_unlock(&g_lock);
        return -2;
    }

    n = g_head;
    while (n && i < count) {
        entries[i].tool_name = n->b.tool_name ? strdup(n->b.tool_name) : NULL;
        if (n->b.tool_name && !entries[i].tool_name) {
            pthread_rwlock_unlock(&g_lock);
            tool_registry_snapshot_free(entries, i);
            return -2;
        }
        entries[i].provider = n->b.provider;
        entries[i].plugin = n->b.plugin;
        i++;
        n = n->next;
    }
    pthread_rwlock_unlock(&g_lock);

    *out_entries = entries;
    *out_count = i;
    return 0;
}

void tool_registry_snapshot_free(tool_registry_entry_t *entries, size_t count)
{
    size_t i;
    if (!entries)
        return;
    for (i = 0; i < count; i++)
        free(entries[i].tool_name);
    free(entries);
}

void tool_registry_destroy(void)
{
    binding_node_t *n, *next;
    pthread_rwlock_wrlock(&g_lock);
    n = g_head;
    while (n) {
        next = n->next;
        free(n->b.tool_name);
        free(n);
        n = next;
    }
    g_head = NULL;
    pthread_rwlock_unlock(&g_lock);
}
