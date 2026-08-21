#include "capability_publication.h"

#include <stdlib.h>
#include <string.h>

static int same_tool_scope(const capability_entry_t *a, const capability_entry_t *b)
{
    return strcmp(a->name, b->name) == 0 && strcmp(a->plane, b->plane) == 0;
}

static int entry_cmp(const void *lhs, const void *rhs)
{
    const capability_entry_t *a = (const capability_entry_t *)lhs;
    const capability_entry_t *b = (const capability_entry_t *)rhs;
    int p = strcmp(a->plane, b->plane);
    if (p != 0) return p;
    p = strcmp(a->name, b->name);
    if (p != 0) return p;
    return (int)a->provider - (int)b->provider;
}

int capability_snapshot_build(const capability_entry_t *catalog_entries,
                              size_t catalog_count,
                              const capability_entry_t *dynamic_entries,
                              size_t dynamic_count,
                              int conflict_policy,
                              capability_snapshot_t *out_snapshot)
{
    capability_entry_t *items;
    size_t count = 0;
    size_t cap = catalog_count + dynamic_count;

    if (!out_snapshot)
        return -1;

    out_snapshot->items = NULL;
    out_snapshot->count = 0;

    if (cap == 0)
        return 0;

    items = (capability_entry_t *)calloc(cap, sizeof(*items));
    if (!items)
        return -2;

    for (size_t i = 0; i < catalog_count; i++) {
        items[count++] = catalog_entries[i];
    }

    for (size_t i = 0; i < dynamic_count; i++) {
        const capability_entry_t *d = &dynamic_entries[i];
        int replaced = 0;

        for (size_t j = 0; j < count; j++) {
            if (!same_tool_scope(&items[j], d))
                continue;

            if (conflict_policy == 1) {
                items[j] = *d;
            }
            replaced = 1;
            break;
        }

        if (!replaced) {
            items[count++] = *d;
        }
    }

    qsort(items, count, sizeof(*items), entry_cmp);

    out_snapshot->items = items;
    out_snapshot->count = count;
    return 0;
}

void capability_snapshot_free(capability_snapshot_t *snapshot)
{
    if (!snapshot)
        return;
    free(snapshot->items);
    snapshot->items = NULL;
    snapshot->count = 0;
}
