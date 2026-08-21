#ifndef CATALOG_BACKEND_H
#define CATALOG_BACKEND_H

#include <stddef.h>
#include <limits.h>

typedef enum {
    MPRM_CATALOG_BACKEND_JSON = 0,
    MPRM_CATALOG_BACKEND_LMDB = 1
} mprm_catalog_backend_t;

typedef struct {
    mprm_catalog_backend_t requested;
    mprm_catalog_backend_t effective;
    int lmdb_supported;
    int fallback_to_json;
    char source[PATH_MAX];
} mprm_catalog_backend_choice_t;

typedef struct cJSON cJSON;

typedef struct {
    unsigned long hits;
    unsigned long misses;
    unsigned long evictions;
    unsigned long entries;
    unsigned long max_entries;
    unsigned long reload_events;
    unsigned long generation;
    unsigned long reload_poll_sec;
} mprm_lmdb_cache_stats_t;

void mprm_catalog_backend_select(mprm_catalog_backend_choice_t *out);
const char *mprm_catalog_backend_name(mprm_catalog_backend_t backend);
int mprm_catalog_backend_make_plane_tool_key(const char *plane,
                                             const char *tool,
                                             char *out,
                                             size_t out_sz);
int mprm_catalog_backend_lmdb_init(char *path_used, size_t path_used_sz);
void mprm_catalog_backend_lmdb_shutdown(void);
int mprm_catalog_backend_lmdb_is_ready(void);
cJSON *mprm_catalog_backend_lmdb_lookup_entry_json(const char *plane,
                                                   const char *tool,
                                                   char *key_used,
                                                   size_t key_used_sz);
int mprm_catalog_backend_lmdb_import_from_catalog_dir(const char *catalog_dir,
                                                      unsigned long *imported_out);
int mprm_catalog_backend_lmdb_replace_plane_catalog(const char *plane,
                                                    const cJSON *catalog,
                                                    long target_version,
                                                    char *errbuf,
                                                    size_t errbuf_sz);
void mprm_catalog_backend_lmdb_reload_poll(long long now_ms,
                                           int *cache_invalidated_out);
void mprm_catalog_backend_lmdb_cache_clear(void);
void mprm_catalog_backend_lmdb_cache_stats(mprm_lmdb_cache_stats_t *out);

#endif
