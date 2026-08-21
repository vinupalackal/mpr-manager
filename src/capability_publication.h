#ifndef CAPABILITY_PUBLICATION_H
#define CAPABILITY_PUBLICATION_H

#include <stddef.h>

typedef enum {
    CAP_PROVIDER_CATALOG = 0,
    CAP_PROVIDER_PLUGIN = 1
} cap_provider_t;

typedef struct {
    char name[128];
    char type[16];   /* static|dynamic */
    char plane[32];
    int timeout;
    cap_provider_t provider;
} capability_entry_t;

typedef struct {
    capability_entry_t *items;
    size_t count;
} capability_snapshot_t;

int capability_snapshot_build(const capability_entry_t *catalog_entries,
                              size_t catalog_count,
                              const capability_entry_t *dynamic_entries,
                              size_t dynamic_count,
                              int conflict_policy,
                              capability_snapshot_t *out_snapshot);

void capability_snapshot_free(capability_snapshot_t *snapshot);

#endif
