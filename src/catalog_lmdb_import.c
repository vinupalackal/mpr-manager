#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lmdb.h>
#include <cJSON.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    const char *plane;
    const char *filename;
} plane_file_t;

static const plane_file_t k_plane_files[] = {
    { "triage",       "multi-plane-runtime-manager-triage-catalog.json" },
    { "management",   "multi-plane-runtime-manager-management-catalog.json" },
    { "control",      "multi-plane-runtime-manager-control-catalog.json" },
    { "config-apply", "multi-plane-runtime-manager-config-apply-catalog.json" },
};

static int read_file_all(const char *path, char **out_buf)
{
    FILE *f;
    long sz;
    char *buf;

    if (!path || !out_buf)
        return 0;

    f = fopen(path, "rb");
    if (!f)
        return 0;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 0;
    }
    rewind(f);

    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[sz] = '\0';
    fclose(f);

    *out_buf = buf;
    return 1;
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (!path || !*path)
        return 0;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 1 : 0;
    if (mkdir(path, 0755) == 0)
        return 1;
    return errno == EEXIST;
}

static int import_one_catalog(MDB_txn *txn,
                              MDB_dbi dbi,
                              const char *plane,
                              const char *path,
                              unsigned long *entry_count)
{
    char *json_text = NULL;
    cJSON *root = NULL;
    cJSON *tools;

    if (!read_file_all(path, &json_text)) {
        if (errno == ENOENT)
            return 1; /* missing plane file is allowed */
        fprintf(stderr, "WARN: unable to read %s (%s)\n", path, strerror(errno));
        return 1;
    }

    root = cJSON_Parse(json_text);
    free(json_text);
    json_text = NULL;
    if (!root) {
        fprintf(stderr, "ERROR: invalid JSON in %s\n", path);
        return 0;
    }

    tools = cJSON_GetObjectItem(root, "tools");
    if (!tools || !cJSON_IsObject(tools)) {
        fprintf(stderr, "WARN: no tools object in %s\n", path);
        cJSON_Delete(root);
        return 1;
    }

    for (cJSON *entry = tools->child; entry; entry = entry->next) {
        char keybuf[512];
        char *value_json;
        MDB_val k, v;
        int rc;

        if (!entry->string || !*entry->string)
            continue;

        if (snprintf(keybuf, sizeof(keybuf), "%s:%s", plane, entry->string) >= (int)sizeof(keybuf)) {
            fprintf(stderr, "WARN: key too long, skip %s:%s\n", plane, entry->string);
            continue;
        }

        value_json = cJSON_PrintUnformatted(entry);
        if (!value_json) {
            fprintf(stderr, "WARN: failed to serialize %s:%s\n", plane, entry->string);
            continue;
        }

        k.mv_data = keybuf;
        k.mv_size = strlen(keybuf);
        v.mv_data = value_json;
        v.mv_size = strlen(value_json);

        rc = mdb_put(txn, dbi, &k, &v, 0);
        free(value_json);
        if (rc != MDB_SUCCESS) {
            fprintf(stderr, "ERROR: lmdb put failed for %s (%s)\n", keybuf, mdb_strerror(rc));
            cJSON_Delete(root);
            return 0;
        }

        (*entry_count)++;
    }

    cJSON_Delete(root);
    return 1;
}

int main(int argc, char **argv)
{
    const char *catalog_dir = "/etc/multi-plane-runtime-manager";
    const char *lmdb_path = "/var/lib/multi-plane-runtime-manager/catalog.lmdb";
    size_t map_size = 16U * 1024U * 1024U;
    MDB_env *env = NULL;
    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    unsigned long imported = 0;
    int rc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--catalog-dir") == 0 && i + 1 < argc) {
            catalog_dir = argv[++i];
        } else if (strcmp(argv[i], "--lmdb-path") == 0 && i + 1 < argc) {
            lmdb_path = argv[++i];
        } else if (strcmp(argv[i], "--map-size") == 0 && i + 1 < argc) {
            map_size = (size_t)strtoull(argv[++i], NULL, 10);
        } else {
            fprintf(stderr,
                    "Usage: %s [--catalog-dir DIR] [--lmdb-path DIR] [--map-size BYTES]\n",
                    argv[0]);
            return 2;
        }
    }

    if (!ensure_dir(lmdb_path)) {
        fprintf(stderr, "ERROR: unable to create/access LMDB directory: %s\n", lmdb_path);
        return 1;
    }

    rc = mdb_env_create(&env);
    if (rc != MDB_SUCCESS) {
        fprintf(stderr, "ERROR: mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_maxdbs(env, 4);
    mdb_env_set_mapsize(env, map_size);

    rc = mdb_env_open(env, lmdb_path, 0, 0644);
    if (rc != MDB_SUCCESS) {
        fprintf(stderr, "ERROR: mdb_env_open(%s) failed: %s\n", lmdb_path, mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != MDB_SUCCESS) {
        fprintf(stderr, "ERROR: mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc != MDB_SUCCESS) {
        fprintf(stderr, "ERROR: mdb_dbi_open failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return 1;
    }

    rc = mdb_drop(txn, dbi, 0);
    if (rc != MDB_SUCCESS) {
        fprintf(stderr, "ERROR: mdb_drop failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return 1;
    }

    for (size_t i = 0; i < sizeof(k_plane_files) / sizeof(k_plane_files[0]); i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", catalog_dir, k_plane_files[i].filename);
        if (!import_one_catalog(txn, dbi, k_plane_files[i].plane, path, &imported)) {
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return 1;
        }
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        fprintf(stderr, "ERROR: mdb_txn_commit failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    mdb_env_sync(env, 1);
    mdb_env_close(env);

    printf("Imported %lu catalog entries into LMDB at %s\n", imported, lmdb_path);
    return 0;
}
