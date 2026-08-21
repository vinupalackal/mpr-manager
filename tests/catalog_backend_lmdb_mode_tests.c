#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lmdb.h>
#include <cJSON.h>

#include "catalog_backend.h"

#define TASSERT(c) do { if (!(c)) { \
    fprintf(stderr, "ASSERT FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    return 1; } } while (0)

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (fwrite(text, 1, strlen(text), f) != strlen(text)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int bootstrap_empty_lmdb(const char *dir)
{
    MDB_env *env = NULL;
    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    int rc;

    rc = mdb_env_create(&env);
    if (rc != MDB_SUCCESS) return 0;
    rc = mdb_env_set_maxdbs(env, 4);
    if (rc != MDB_SUCCESS) { mdb_env_close(env); return 0; }
    rc = mdb_env_set_mapsize(env, 4U * 1024U * 1024U);
    if (rc != MDB_SUCCESS) { mdb_env_close(env); return 0; }
    rc = mdb_env_open(env, dir, 0, 0644);
    if (rc != MDB_SUCCESS) { mdb_env_close(env); return 0; }

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != MDB_SUCCESS) { mdb_env_close(env); return 0; }
    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc != MDB_SUCCESS) { mdb_txn_abort(txn); mdb_env_close(env); return 0; }
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) { mdb_env_close(env); return 0; }

    mdb_env_sync(env, 1);
    mdb_env_close(env);
    return 1;
}

static int test_lmdb_import_lookup_and_cache(void)
{
    char cat_tpl[] = "/tmp/mprm-db-catalog-XXXXXX";
    char db_tpl[]  = "/tmp/mprm-db-lmdb-XXXXXX";
    char *cat_dir = mkdtemp(cat_tpl);
    char *db_dir = mkdtemp(db_tpl);
    char path_triage[512];
    char path_mgmt[512];
    char path_control[512];
    char path_cfg[512];
    char used_path[512];
    unsigned long imported = 0;
    mprm_lmdb_cache_stats_t st;
    cJSON *entry;
    cJSON *cmd;

    TASSERT(cat_dir != NULL);
    TASSERT(db_dir != NULL);

    snprintf(path_triage, sizeof(path_triage), "%s/multi-plane-runtime-manager-triage-catalog.json", cat_dir);
    snprintf(path_mgmt, sizeof(path_mgmt), "%s/multi-plane-runtime-manager-management-catalog.json", cat_dir);
    snprintf(path_control, sizeof(path_control), "%s/multi-plane-runtime-manager-control-catalog.json", cat_dir);
    snprintf(path_cfg, sizeof(path_cfg), "%s/multi-plane-runtime-manager-config-apply-catalog.json", cat_dir);

    TASSERT(write_text(path_triage,
        "{\"tools\":{"
        "\"device_uptime\":{\"command\":\"cat /proc/uptime\",\"timeout\":5,\"type\":\"static\",\"plane\":\"triage\"},"
        "\"memory_usage\":{\"command\":\"cat /proc/meminfo\",\"timeout\":5,\"type\":\"static\",\"plane\":\"triage\"}"
        "}}"));
    TASSERT(write_text(path_mgmt,
        "{\"tools\":{"
        "\"service_status\":{\"command\":\"/bin/ps\",\"timeout\":5,\"type\":\"static\",\"plane\":\"management\"}"
        "}}"));
    TASSERT(write_text(path_control, "{\"tools\":{}}"));
    TASSERT(write_text(path_cfg, "{\"tools\":{}}"));

    TASSERT(bootstrap_empty_lmdb(db_dir));

    setenv("MULTI_PLANE_RUNTIME_MANAGER_LMDB_PATH", db_dir, 1);
    setenv("MULTI_PLANE_RUNTIME_MANAGER_LRU_MAX_ENTRIES", "32", 1);
    setenv("MULTI_PLANE_RUNTIME_MANAGER_CATALOG_RELOAD_POLL_SEC", "2", 1);

    TASSERT(mprm_catalog_backend_lmdb_init(used_path, sizeof(used_path)) == 1);
    TASSERT(strstr(used_path, db_dir) != NULL);

    TASSERT(mprm_catalog_backend_lmdb_import_from_catalog_dir(cat_dir, &imported) == 1);
    TASSERT(imported == 3);

    mprm_catalog_backend_lmdb_cache_stats(&st);
    TASSERT(st.misses == 0);
    TASSERT(st.hits == 0);

    entry = mprm_catalog_backend_lmdb_lookup_entry_json("triage", "device_uptime", NULL, 0);
    TASSERT(entry != NULL);
    cmd = cJSON_GetObjectItem(entry, "command");
    TASSERT(cmd != NULL);
    TASSERT(strcmp(cJSON_GetStringValue(cmd), "cat /proc/uptime") == 0);
    cJSON_Delete(entry);

    mprm_catalog_backend_lmdb_cache_stats(&st);
    TASSERT(st.misses >= 1);

    entry = mprm_catalog_backend_lmdb_lookup_entry_json("triage", "device_uptime", NULL, 0);
    TASSERT(entry != NULL);
    cJSON_Delete(entry);

    mprm_catalog_backend_lmdb_cache_stats(&st);
    TASSERT(st.hits >= 1);
    TASSERT(st.max_entries == 32);

    mprm_catalog_backend_lmdb_shutdown();

    {
        char cmd_rm[1024];
        snprintf(cmd_rm, sizeof(cmd_rm), "rm -rf '%s' '%s'", cat_dir, db_dir);
        (void)system(cmd_rm);
    }
    return 0;
}

int main(void)
{
    if (test_lmdb_import_lookup_and_cache() != 0)
        return 1;
    printf("catalog_backend_lmdb_mode_tests: PASS\n");
    return 0;
}
