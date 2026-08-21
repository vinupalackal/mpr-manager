#include "catalog_backend.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <cJSON.h>

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
#include <lmdb.h>
#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#endif

#define LMDB_LRU_MAX_DEFAULT 256U
#define LMDB_LRU_MAX_MIN     16U
#define LMDB_LRU_MAX_MAX     4096U

#ifndef MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
#define MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB 0
#endif

static char *trim_ws(char *s)
{
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static int equals_ci(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;
    if (!a || !b) return 0;
    while (*a && *b) {
        ca = (unsigned char)tolower((unsigned char)*a++);
        cb = (unsigned char)tolower((unsigned char)*b++);
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static mprm_catalog_backend_t parse_backend(const char *v)
{
    if (v && (equals_ci(v, "lmdb") || equals_ci(v, "mdb")))
        return MPRM_CATALOG_BACKEND_LMDB;
    return MPRM_CATALOG_BACKEND_JSON;
}

static const char *find_cfg_file(void)
{
    const char *env_path = getenv("MULTI_PLANE_RUNTIME_MANAGER_CONFIG_FILE");
    static const char *candidates[] = {
        NULL,
        "/etc/multi-plane-runtime-manager/multi-plane-runtime-manager.conf",
        "./multi-plane-runtime-manager.conf"
    };
    FILE *f = NULL;

    candidates[0] = (env_path && *env_path) ? env_path : NULL;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!candidates[i] || !*candidates[i])
            continue;
        f = fopen(candidates[i], "r");
        if (f) {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

static int read_backend_from_cfg(const char *cfg_path, mprm_catalog_backend_t *out_backend)
{
    FILE *f;
    char line[512];

    if (!cfg_path || !*cfg_path || !out_backend)
        return 0;

    f = fopen(cfg_path, "r");
    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = trim_ws(line);
        char *eq;
        char *key;
        char *val;

        if (*p == '\0' || *p == '#')
            continue;

        eq = strchr(p, '=');
        if (!eq)
            continue;

        *eq = '\0';
        key = trim_ws(p);
        val = trim_ws(eq + 1);

        if (strcmp(key, "MULTI_PLANE_RUNTIME_MANAGER_CATALOG_BACKEND") == 0) {
            *out_backend = parse_backend(val);
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

static int read_cfg_value(const char *cfg_path,
                          const char *want_key,
                          char *out,
                          size_t out_sz)
{
    FILE *f;
    char line[512];

    if (!cfg_path || !*cfg_path || !want_key || !*want_key || !out || out_sz == 0)
        return 0;

    f = fopen(cfg_path, "r");
    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = trim_ws(line);
        char *eq;
        char *key;
        char *val;

        if (*p == '\0' || *p == '#')
            continue;

        eq = strchr(p, '=');
        if (!eq)
            continue;

        *eq = '\0';
        key = trim_ws(p);
        val = trim_ws(eq + 1);

        if (strcmp(key, want_key) == 0) {
            snprintf(out, out_sz, "%s", val);
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

static unsigned long parse_ulong_or_default(const char *s, unsigned long def)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !*s)
        return def;
    v = strtoul(s, &end, 10);
    if (!end || *end != '\0')
        return def;
    return v;
}

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

void mprm_catalog_backend_select(mprm_catalog_backend_choice_t *out)
{
    const char *env_backend;
    const char *cfg_path;

    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->requested = MPRM_CATALOG_BACKEND_JSON;
    out->effective = MPRM_CATALOG_BACKEND_JSON;
    out->lmdb_supported = MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB ? 1 : 0;
    snprintf(out->source, sizeof(out->source), "%s", "default");

    cfg_path = find_cfg_file();
    if (cfg_path && read_backend_from_cfg(cfg_path, &out->requested))
        snprintf(out->source, sizeof(out->source), "config:%s", cfg_path);

    env_backend = getenv("MULTI_PLANE_RUNTIME_MANAGER_CATALOG_BACKEND");
    if (env_backend && *env_backend) {
        out->requested = parse_backend(env_backend);
        snprintf(out->source, sizeof(out->source), "%s", "env:MULTI_PLANE_RUNTIME_MANAGER_CATALOG_BACKEND");
    }

    out->effective = out->requested;
    if (out->requested == MPRM_CATALOG_BACKEND_LMDB && !out->lmdb_supported) {
        out->effective = MPRM_CATALOG_BACKEND_JSON;
        out->fallback_to_json = 1;
    }
}

const char *mprm_catalog_backend_name(mprm_catalog_backend_t backend)
{
    switch (backend) {
        case MPRM_CATALOG_BACKEND_LMDB:
            return "lmdb";
        case MPRM_CATALOG_BACKEND_JSON:
        default:
            return "json";
    }
}

int mprm_catalog_backend_make_plane_tool_key(const char *plane,
                                             const char *tool,
                                             char *out,
                                             size_t out_sz)
{
    int n;

    if (!plane || !*plane || !tool || !*tool || !out || out_sz == 0)
        return 0;

    n = snprintf(out, out_sz, "%s:%s", plane, tool);
    if (n < 0 || (size_t)n >= out_sz)
        return 0;

    return 1;
}

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
static MDB_env *g_lmdb_env = NULL;
static MDB_dbi g_lmdb_dbi = 0;
static int g_lmdb_ready = 0;
static char g_lmdb_path[PATH_MAX];

typedef struct lmdb_cache_entry_s {
    char *key;
    char *json;
    struct lmdb_cache_entry_s *prev;
    struct lmdb_cache_entry_s *next;
} lmdb_cache_entry_t;

static lmdb_cache_entry_t *g_cache_head = NULL;
static lmdb_cache_entry_t *g_cache_tail = NULL;
static unsigned long g_cache_count = 0;
static unsigned long g_cache_max = LMDB_LRU_MAX_DEFAULT;
static unsigned long g_cache_hits = 0;
static unsigned long g_cache_misses = 0;
static unsigned long g_cache_evictions = 0;
static unsigned long g_reload_events = 0;
static unsigned long g_generation = 1;
static unsigned long g_reload_poll_sec = 10;
static long long g_last_reload_check_ms = 0;
static time_t g_data_mtime_sec = 0;
static long g_data_mtime_nsec = 0;
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_reload_watch_thread;
static int g_reload_watch_running = 0;
static unsigned long g_reload_event_notifications = 0;
#if defined(__linux__)
static int g_reload_watch_fd = -1;
static int g_reload_watch_wd = -1;
#elif defined(__APPLE__)
static int g_reload_watch_kq = -1;
static int g_reload_watch_dirfd = -1;
#endif

typedef struct {
    const char *plane;
    const char *filename;
} lmdb_import_plane_file_t;

static const lmdb_import_plane_file_t g_import_plane_files[] = {
    { "triage",       "multi-plane-runtime-manager-triage-catalog.json" },
    { "management",   "multi-plane-runtime-manager-management-catalog.json" },
    { "control",      "multi-plane-runtime-manager-control-catalog.json" },
    { "config-apply", "multi-plane-runtime-manager-config-apply-catalog.json" },
};

static int build_data_mdb_path(char *out, size_t out_sz)
{
    static const char suffix[] = "/data.mdb";
    size_t base_len;

    if (!out || out_sz == 0)
        return 0;

    base_len = strnlen(g_lmdb_path, sizeof(g_lmdb_path));
    if (base_len == 0 || base_len >= sizeof(g_lmdb_path))
        return 0;

    if (base_len + sizeof(suffix) > out_sz)
        return 0;

    memcpy(out, g_lmdb_path, base_len);
    memcpy(out + base_len, suffix, sizeof(suffix));
    return 1;
}

static void cache_unlink(lmdb_cache_entry_t *e)
{
    if (!e)
        return;
    if (e->prev) e->prev->next = e->next;
    if (e->next) e->next->prev = e->prev;
    if (g_cache_head == e) g_cache_head = e->next;
    if (g_cache_tail == e) g_cache_tail = e->prev;
    e->prev = NULL;
    e->next = NULL;
}

static void cache_push_front(lmdb_cache_entry_t *e)
{
    if (!e)
        return;
    e->prev = NULL;
    e->next = g_cache_head;
    if (g_cache_head) g_cache_head->prev = e;
    g_cache_head = e;
    if (!g_cache_tail) g_cache_tail = e;
}

static void cache_free_entry(lmdb_cache_entry_t *e)
{
    if (!e)
        return;
    free(e->key);
    free(e->json);
    free(e);
}

static void cache_trim_to_max(void)
{
    while (g_cache_count > g_cache_max && g_cache_tail) {
        lmdb_cache_entry_t *victim = g_cache_tail;
        cache_unlink(victim);
        cache_free_entry(victim);
        g_cache_count--;
        g_cache_evictions++;
    }
}

static char *cache_get_json_copy(const char *key)
{
    lmdb_cache_entry_t *e;
    char *out = NULL;
    if (!key)
        return NULL;
    pthread_mutex_lock(&g_cache_lock);
    for (e = g_cache_head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            if (e != g_cache_head) {
                cache_unlink(e);
                cache_push_front(e);
            }
            out = strdup(e->json ? e->json : "");
            g_cache_hits++;
            pthread_mutex_unlock(&g_cache_lock);
            return out;
        }
    }
    g_cache_misses++;
    pthread_mutex_unlock(&g_cache_lock);
    return NULL;
}

static void cache_put_json(const char *key, const char *json)
{
    lmdb_cache_entry_t *e;
    if (!key || !*key || !json)
        return;

    pthread_mutex_lock(&g_cache_lock);
    for (e = g_cache_head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            char *new_json = strdup(json);
            if (!new_json) {
                pthread_mutex_unlock(&g_cache_lock);
                return;
            }
            free(e->json);
            e->json = new_json;
            if (e != g_cache_head) {
                cache_unlink(e);
                cache_push_front(e);
            }
            pthread_mutex_unlock(&g_cache_lock);
            return;
        }
    }

    e = (lmdb_cache_entry_t *)calloc(1, sizeof(*e));
    if (!e) {
        pthread_mutex_unlock(&g_cache_lock);
        return;
    }
    e->key = strdup(key);
    e->json = strdup(json);
    if (!e->key || !e->json) {
        cache_free_entry(e);
        pthread_mutex_unlock(&g_cache_lock);
        return;
    }

    cache_push_front(e);
    g_cache_count++;
    cache_trim_to_max();
    pthread_mutex_unlock(&g_cache_lock);
}

static void cache_clear_locked(void)
{
    lmdb_cache_entry_t *it = g_cache_head;
    while (it) {
        lmdb_cache_entry_t *n = it->next;
        cache_free_entry(it);
        it = n;
    }
    g_cache_head = NULL;
    g_cache_tail = NULL;
    g_cache_count = 0;
}

static int lmdb_import_one_catalog(MDB_txn *txn,
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
            return 1;
        return 0;
    }

    root = cJSON_Parse(json_text);
    free(json_text);
    json_text = NULL;
    if (!root)
        return 0;

    tools = cJSON_GetObjectItem(root, "tools");
    if (!tools || !cJSON_IsObject(tools)) {
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

        if (snprintf(keybuf, sizeof(keybuf), "%s:%s", plane, entry->string) >= (int)sizeof(keybuf))
            continue;

        value_json = cJSON_PrintUnformatted(entry);
        if (!value_json)
            continue;

        k.mv_data = keybuf;
        k.mv_size = strlen(keybuf);
        v.mv_data = value_json;
        v.mv_size = strlen(value_json);

        rc = mdb_put(txn, dbi, &k, &v, 0);
        free(value_json);
        if (rc != MDB_SUCCESS) {
            cJSON_Delete(root);
            return 0;
        }

        (*entry_count)++;
    }

    cJSON_Delete(root);
    return 1;
}

static void lmdb_invalidate_cache_locked(void)
{
    cache_clear_locked();
    g_reload_events++;
    g_generation++;
    g_data_mtime_sec = 0;
    g_data_mtime_nsec = 0;
}

#if defined(__linux__)
static int lmdb_watch_event_is_relevant(const struct inotify_event *ev)
{
    if (!ev)
        return 0;
    if (!(ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_ATTRIB | IN_DELETE | IN_CREATE | IN_DELETE_SELF | IN_MOVE_SELF)))
        return 0;
    if (ev->len == 0)
        return 1;
    if (strcmp(ev->name, "data.mdb") == 0 || strcmp(ev->name, "lock.mdb") == 0)
        return 1;
    return 0;
}

static void *lmdb_reload_watch_thread_main(void *arg)
{
    (void)arg;
    while (g_reload_watch_running) {
        char buf[4096];
        ssize_t n = read(g_reload_watch_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (!g_reload_watch_running)
                break;
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100 * 1000);
                continue;
            }
            break;
        }

        for (char *p = buf; p < buf + n; ) {
            struct inotify_event *ev = (struct inotify_event *)p;
            if (lmdb_watch_event_is_relevant(ev)) {
                pthread_mutex_lock(&g_cache_lock);
                lmdb_invalidate_cache_locked();
                g_reload_event_notifications++;
                pthread_mutex_unlock(&g_cache_lock);
                break;
            }
            p += sizeof(struct inotify_event) + ev->len;
        }
    }
    return NULL;
}

static int lmdb_reload_watch_start(void)
{
    int mask;
    if (g_reload_watch_running)
        return 1;

    g_reload_watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (g_reload_watch_fd < 0)
        return 0;

    mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_ATTRIB | IN_DELETE | IN_CREATE | IN_DELETE_SELF | IN_MOVE_SELF;
    g_reload_watch_wd = inotify_add_watch(g_reload_watch_fd, g_lmdb_path, mask);
    if (g_reload_watch_wd < 0) {
        close(g_reload_watch_fd);
        g_reload_watch_fd = -1;
        return 0;
    }

    g_reload_watch_running = 1;
    if (pthread_create(&g_reload_watch_thread, NULL, lmdb_reload_watch_thread_main, NULL) != 0) {
        g_reload_watch_running = 0;
        inotify_rm_watch(g_reload_watch_fd, g_reload_watch_wd);
        close(g_reload_watch_fd);
        g_reload_watch_fd = -1;
        g_reload_watch_wd = -1;
        return 0;
    }
    return 1;
}

static void lmdb_reload_watch_stop(void)
{
    if (!g_reload_watch_running)
        return;
    g_reload_watch_running = 0;
    if (g_reload_watch_fd >= 0) {
        close(g_reload_watch_fd);
        g_reload_watch_fd = -1;
    }
    pthread_join(g_reload_watch_thread, NULL);
    g_reload_watch_wd = -1;
}
#elif defined(__APPLE__)
static void *lmdb_reload_watch_thread_main(void *arg)
{
    (void)arg;
    while (g_reload_watch_running) {
        struct kevent ev;
        struct timespec ts;
        int n;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        n = kevent(g_reload_watch_kq, NULL, 0, &ev, 1, &ts);
        if (!g_reload_watch_running)
            break;
        if (n <= 0)
            continue;

        if (ev.filter == EVFILT_VNODE &&
            (ev.fflags & (NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME | NOTE_LINK | NOTE_REVOKE))) {
            pthread_mutex_lock(&g_cache_lock);
            lmdb_invalidate_cache_locked();
            g_reload_event_notifications++;
            pthread_mutex_unlock(&g_cache_lock);
        }
    }
    return NULL;
}

static int lmdb_reload_watch_start(void)
{
    struct kevent ev;
    if (g_reload_watch_running)
        return 1;

    g_reload_watch_dirfd = open(g_lmdb_path, O_EVTONLY);
    if (g_reload_watch_dirfd < 0)
        return 0;

    g_reload_watch_kq = kqueue();
    if (g_reload_watch_kq < 0) {
        close(g_reload_watch_dirfd);
        g_reload_watch_dirfd = -1;
        return 0;
    }

    EV_SET(&ev,
           g_reload_watch_dirfd,
           EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME | NOTE_LINK | NOTE_REVOKE,
           0,
           NULL);
    if (kevent(g_reload_watch_kq, &ev, 1, NULL, 0, NULL) < 0) {
        close(g_reload_watch_kq);
        close(g_reload_watch_dirfd);
        g_reload_watch_kq = -1;
        g_reload_watch_dirfd = -1;
        return 0;
    }

    g_reload_watch_running = 1;
    if (pthread_create(&g_reload_watch_thread, NULL, lmdb_reload_watch_thread_main, NULL) != 0) {
        g_reload_watch_running = 0;
        close(g_reload_watch_kq);
        close(g_reload_watch_dirfd);
        g_reload_watch_kq = -1;
        g_reload_watch_dirfd = -1;
        return 0;
    }
    return 1;
}

static void lmdb_reload_watch_stop(void)
{
    if (!g_reload_watch_running)
        return;
    g_reload_watch_running = 0;
    if (g_reload_watch_kq >= 0) {
        close(g_reload_watch_kq);
        g_reload_watch_kq = -1;
    }
    if (g_reload_watch_dirfd >= 0) {
        close(g_reload_watch_dirfd);
        g_reload_watch_dirfd = -1;
    }
    pthread_join(g_reload_watch_thread, NULL);
}
#else
static int lmdb_reload_watch_start(void)
{
    return 0;
}

static void lmdb_reload_watch_stop(void)
{
}
#endif
#endif

int mprm_catalog_backend_lmdb_init(char *path_used, size_t path_used_sz)
{
    const char *env_path;
    const char *cfg_path;
    char cfg_val[PATH_MAX];
    const char *chosen = "/var/lib/multi-plane-runtime-manager/catalog.lmdb";
#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    const char *env_lru;
    const char *env_poll;
    char cfg_lru[64];
    char cfg_poll[64];
#endif

    if (path_used && path_used_sz)
        snprintf(path_used, path_used_sz, "%s", "");

    env_path = getenv("MULTI_PLANE_RUNTIME_MANAGER_LMDB_PATH");
    if (env_path && *env_path)
        chosen = env_path;
    else {
        cfg_path = find_cfg_file();
        if (cfg_path && read_cfg_value(cfg_path,
                                       "MULTI_PLANE_RUNTIME_MANAGER_LMDB_PATH",
                                       cfg_val,
                                       sizeof(cfg_val))
            && cfg_val[0] != '\0') {
            chosen = cfg_val;
        }
    }

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    cfg_lru[0] = '\0';
    env_lru = getenv("MULTI_PLANE_RUNTIME_MANAGER_LRU_MAX_ENTRIES");
    if (env_lru && *env_lru) {
        g_cache_max = parse_ulong_or_default(env_lru, LMDB_LRU_MAX_DEFAULT);
    } else {
        cfg_path = find_cfg_file();
        if (cfg_path && read_cfg_value(cfg_path,
                                       "MULTI_PLANE_RUNTIME_MANAGER_LRU_MAX_ENTRIES",
                                       cfg_lru,
                                       sizeof(cfg_lru))
            && cfg_lru[0] != '\0') {
            g_cache_max = parse_ulong_or_default(cfg_lru, LMDB_LRU_MAX_DEFAULT);
        } else {
            g_cache_max = LMDB_LRU_MAX_DEFAULT;
        }
    }
    if (g_cache_max < LMDB_LRU_MAX_MIN) g_cache_max = LMDB_LRU_MAX_MIN;
    if (g_cache_max > LMDB_LRU_MAX_MAX) g_cache_max = LMDB_LRU_MAX_MAX;

    cfg_poll[0] = '\0';
    env_poll = getenv("MULTI_PLANE_RUNTIME_MANAGER_CATALOG_RELOAD_POLL_SEC");
    if (env_poll && *env_poll) {
        g_reload_poll_sec = parse_ulong_or_default(env_poll, 10);
    } else {
        cfg_path = find_cfg_file();
        if (cfg_path && read_cfg_value(cfg_path,
                                       "MULTI_PLANE_RUNTIME_MANAGER_CATALOG_RELOAD_POLL_SEC",
                                       cfg_poll,
                                       sizeof(cfg_poll))
            && cfg_poll[0] != '\0') {
            g_reload_poll_sec = parse_ulong_or_default(cfg_poll, 10);
        } else {
            g_reload_poll_sec = 10;
        }
    }
    if (g_reload_poll_sec > 3600) g_reload_poll_sec = 3600;
#endif

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    if (!g_lmdb_ready) {
        MDB_txn *txn = NULL;
        int rc;

        rc = mdb_env_create(&g_lmdb_env);
        if (rc != MDB_SUCCESS)
            return 0;

        rc = mdb_env_set_maxdbs(g_lmdb_env, 4);
        if (rc != MDB_SUCCESS) {
            mdb_env_close(g_lmdb_env);
            g_lmdb_env = NULL;
            return 0;
        }

        rc = mdb_env_open(g_lmdb_env, chosen, MDB_RDONLY, 0444);
        if (rc != MDB_SUCCESS) {
            mdb_env_close(g_lmdb_env);
            g_lmdb_env = NULL;
            return 0;
        }

        rc = mdb_txn_begin(g_lmdb_env, NULL, MDB_RDONLY, &txn);
        if (rc != MDB_SUCCESS) {
            mdb_env_close(g_lmdb_env);
            g_lmdb_env = NULL;
            return 0;
        }

        rc = mdb_dbi_open(txn, NULL, 0, &g_lmdb_dbi);
        if (rc != MDB_SUCCESS) {
            mdb_txn_abort(txn);
            mdb_env_close(g_lmdb_env);
            g_lmdb_env = NULL;
            return 0;
        }

        mdb_txn_commit(txn);
        g_lmdb_ready = 1;
        snprintf(g_lmdb_path, sizeof(g_lmdb_path), "%s", chosen);

        pthread_mutex_lock(&g_cache_lock);
        cache_clear_locked();
        g_cache_hits = 0;
        g_cache_misses = 0;
        g_cache_evictions = 0;
        g_reload_events = 0;
        g_generation = 1;
        g_last_reload_check_ms = 0;
        g_data_mtime_sec = 0;
        g_data_mtime_nsec = 0;
        pthread_mutex_unlock(&g_cache_lock);

        {
            struct stat st;
            char data_mdb_path[PATH_MAX];
            if (build_data_mdb_path(data_mdb_path, sizeof(data_mdb_path)) &&
                stat(data_mdb_path, &st) == 0) {
                pthread_mutex_lock(&g_cache_lock);
                g_data_mtime_sec = st.st_mtime;
#if defined(__APPLE__)
                g_data_mtime_nsec = st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
                g_data_mtime_nsec = st.st_mtim.tv_nsec;
#else
                g_data_mtime_nsec = 0;
#endif
                pthread_mutex_unlock(&g_cache_lock);
            }
        }

        (void)lmdb_reload_watch_start();
    }

    if (path_used && path_used_sz)
        snprintf(path_used, path_used_sz, "%s", g_lmdb_path);
    return g_lmdb_ready;
#else
    (void)chosen;
    return 0;
#endif
}

void mprm_catalog_backend_lmdb_shutdown(void)
{
#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    lmdb_reload_watch_stop();

    pthread_mutex_lock(&g_cache_lock);
    cache_clear_locked();
    pthread_mutex_unlock(&g_cache_lock);

    if (g_lmdb_env) {
        mdb_dbi_close(g_lmdb_env, g_lmdb_dbi);
        mdb_env_close(g_lmdb_env);
        g_lmdb_env = NULL;
    }
    g_lmdb_dbi = 0;
    g_lmdb_ready = 0;
    g_lmdb_path[0] = '\0';
    g_last_reload_check_ms = 0;
    g_data_mtime_sec = 0;
    g_data_mtime_nsec = 0;
#endif
}

int mprm_catalog_backend_lmdb_is_ready(void)
{
#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    return g_lmdb_ready;
#else
    return 0;
#endif
}

cJSON *mprm_catalog_backend_lmdb_lookup_entry_json(const char *plane,
                                                   const char *tool,
                                                   char *key_used,
                                                   size_t key_used_sz)
{
    char keybuf[256];

    if (!mprm_catalog_backend_make_plane_tool_key(plane, tool, keybuf, sizeof(keybuf)))
        return NULL;
    if (key_used && key_used_sz)
        snprintf(key_used, key_used_sz, "%s", keybuf);

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    {
        char *cached;
        cached = cache_get_json_copy(keybuf);
        if (cached) {
            cJSON *entry = cJSON_Parse(cached);
            free(cached);
            return entry;
        }
    }
#endif

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    MDB_txn *txn = NULL;
    MDB_val k, v;
    int rc;
    char *json_buf;
    cJSON *entry;

    if (!g_lmdb_ready || !g_lmdb_env)
        return NULL;

    k.mv_data = keybuf;
    k.mv_size = strlen(keybuf);

    rc = mdb_txn_begin(g_lmdb_env, NULL, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS)
        return NULL;

    rc = mdb_get(txn, g_lmdb_dbi, &k, &v);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return NULL;
    }

    json_buf = (char *)malloc(v.mv_size + 1);
    if (!json_buf) {
        mdb_txn_abort(txn);
        return NULL;
    }
    memcpy(json_buf, v.mv_data, v.mv_size);
    json_buf[v.mv_size] = '\0';

    entry = cJSON_Parse(json_buf);
    if (entry)
        cache_put_json(keybuf, json_buf);
    free(json_buf);
    mdb_txn_abort(txn);
    return entry;
#else
    return NULL;
#endif
}

int mprm_catalog_backend_lmdb_import_from_catalog_dir(const char *catalog_dir,
                                                      unsigned long *imported_out)
{
    if (imported_out)
        *imported_out = 0;

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    MDB_env *wenv = NULL;
    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    int rc;
    unsigned long imported = 0;

    if (!catalog_dir || !*catalog_dir)
        return 0;
    if (!g_lmdb_ready || !g_lmdb_path[0])
        return 0;

    rc = mdb_env_create(&wenv);
    if (rc != MDB_SUCCESS)
        return 0;
    rc = mdb_env_set_maxdbs(wenv, 4);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(wenv);
        return 0;
    }
    rc = mdb_env_open(wenv, g_lmdb_path, 0, 0644);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(wenv);
        return 0;
    }

    rc = mdb_txn_begin(wenv, NULL, 0, &txn);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(wenv);
        return 0;
    }
    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        mdb_env_close(wenv);
        return 0;
    }
    rc = mdb_drop(txn, dbi, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        mdb_env_close(wenv);
        return 0;
    }

    for (size_t i = 0; i < sizeof(g_import_plane_files) / sizeof(g_import_plane_files[0]); i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", catalog_dir, g_import_plane_files[i].filename);
        if (!lmdb_import_one_catalog(txn, dbi, g_import_plane_files[i].plane, path, &imported)) {
            mdb_txn_abort(txn);
            mdb_env_close(wenv);
            return 0;
        }
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(wenv);
        return 0;
    }
    mdb_env_sync(wenv, 1);
    mdb_env_close(wenv);

    pthread_mutex_lock(&g_cache_lock);
    lmdb_invalidate_cache_locked();
    pthread_mutex_unlock(&g_cache_lock);

    if (imported_out)
        *imported_out = imported;
    return 1;
#else
    (void)catalog_dir;
    return 0;
#endif
}

int mprm_catalog_backend_lmdb_replace_plane_catalog(const char *plane,
                                                    const cJSON *catalog,
                                                    long target_version,
                                                    char *errbuf,
                                                    size_t errbuf_sz)
{
    if (errbuf && errbuf_sz)
        snprintf(errbuf, errbuf_sz, "%s", "");

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    MDB_txn *txn = NULL;
    MDB_cursor *cursor = NULL;
    MDB_val k, v;
    int rc;
    char prefix[128];
    size_t prefix_len;
    const cJSON *tools;

    (void)target_version;

    if (!plane || !*plane || !catalog) {
        if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "invalid arguments");
        return 0;
    }
    if (!g_lmdb_ready || !g_lmdb_env) {
        if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "LMDB backend not initialized");
        return 0;
    }

    tools = cJSON_GetObjectItem((cJSON *)catalog, "tools");
    if (!tools || !cJSON_IsObject(tools)) {
        if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "candidate catalog missing tools object");
        return 0;
    }

    if (snprintf(prefix, sizeof(prefix), "%s:", plane) >= (int)sizeof(prefix)) {
        if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "plane key prefix too long");
        return 0;
    }
    prefix_len = strlen(prefix);

    rc = mdb_txn_begin(g_lmdb_env, NULL, 0, &txn);
    if (rc != MDB_SUCCESS) {
        if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "mdb_txn_begin failed: %s", mdb_strerror(rc));
        return 0;
    }

    rc = mdb_cursor_open(txn, g_lmdb_dbi, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "mdb_cursor_open failed: %s", mdb_strerror(rc));
        return 0;
    }

    rc = mdb_cursor_get(cursor, &k, &v, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        int should_delete = 0;
        if (k.mv_size > prefix_len && memcmp(k.mv_data, prefix, prefix_len) == 0)
            should_delete = 1;

        if (should_delete) {
            rc = mdb_cursor_del(cursor, 0);
            if (rc != MDB_SUCCESS) {
                mdb_cursor_close(cursor);
                mdb_txn_abort(txn);
                if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "mdb_cursor_del failed: %s", mdb_strerror(rc));
                return 0;
            }
            rc = mdb_cursor_get(cursor, &k, &v, MDB_NEXT);
        } else {
            rc = mdb_cursor_get(cursor, &k, &v, MDB_NEXT);
        }
    }
    mdb_cursor_close(cursor);

    for (const cJSON *entry = tools->child; entry; entry = entry->next) {
        char keybuf[512];
        char *value_json;
        if (!entry->string || !*entry->string)
            continue;

        if (snprintf(keybuf, sizeof(keybuf), "%s:%s", plane, entry->string) >= (int)sizeof(keybuf)) {
            mdb_txn_abort(txn);
            if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "tool key too long for plane '%s'", plane);
            return 0;
        }

        value_json = cJSON_PrintUnformatted((cJSON *)entry);
        if (!value_json) {
            mdb_txn_abort(txn);
            if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "failed to serialize tool '%s'", entry->string);
            return 0;
        }

        k.mv_data = keybuf;
        k.mv_size = strlen(keybuf);
        v.mv_data = value_json;
        v.mv_size = strlen(value_json);
        rc = mdb_put(txn, g_lmdb_dbi, &k, &v, 0);
        free(value_json);

        if (rc != MDB_SUCCESS) {
            mdb_txn_abort(txn);
            if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "mdb_put failed for '%s': %s", keybuf, mdb_strerror(rc));
            return 0;
        }
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "mdb_txn_commit failed: %s", mdb_strerror(rc));
        return 0;
    }

    mdb_env_sync(g_lmdb_env, 1);

    pthread_mutex_lock(&g_cache_lock);
    lmdb_invalidate_cache_locked();
    pthread_mutex_unlock(&g_cache_lock);
    return 1;
#else
    (void)plane;
    (void)catalog;
    (void)target_version;
    if (errbuf && errbuf_sz) snprintf(errbuf, errbuf_sz, "LMDB backend not enabled in this build");
    return 0;
#endif
}

void mprm_catalog_backend_lmdb_reload_poll(long long now_ms,
                                           int *cache_invalidated_out)
{
    if (cache_invalidated_out)
        *cache_invalidated_out = 0;

#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    struct stat st;
    char data_mdb_path[PATH_MAX];
    time_t cur_sec;
    long cur_nsec;
    int changed = 0;

    if (!g_lmdb_ready || !g_lmdb_env)
        return;

    if (g_reload_poll_sec == 0)
        return;

    pthread_mutex_lock(&g_cache_lock);
    if (g_last_reload_check_ms > 0 &&
        now_ms > 0 &&
        (unsigned long)(now_ms - g_last_reload_check_ms) < g_reload_poll_sec * 1000UL) {
        pthread_mutex_unlock(&g_cache_lock);
        return;
    }
    g_last_reload_check_ms = now_ms;
    pthread_mutex_unlock(&g_cache_lock);

    if (!build_data_mdb_path(data_mdb_path, sizeof(data_mdb_path)))
        return;

    if (stat(data_mdb_path, &st) != 0)
        return;

    cur_sec = st.st_mtime;
#if defined(__APPLE__)
    cur_nsec = st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
    cur_nsec = st.st_mtim.tv_nsec;
#else
    cur_nsec = 0;
#endif

    pthread_mutex_lock(&g_cache_lock);
    if (g_data_mtime_sec == 0 && g_data_mtime_nsec == 0) {
        g_data_mtime_sec = cur_sec;
        g_data_mtime_nsec = cur_nsec;
    } else if (g_data_mtime_sec != cur_sec || g_data_mtime_nsec != cur_nsec) {
        g_data_mtime_sec = cur_sec;
        g_data_mtime_nsec = cur_nsec;
        lmdb_invalidate_cache_locked();
        changed = 1;
    }
    pthread_mutex_unlock(&g_cache_lock);

    if (changed && cache_invalidated_out)
        *cache_invalidated_out = 1;
#else
    (void)now_ms;
#endif
}

void mprm_catalog_backend_lmdb_cache_clear(void)
{
#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    pthread_mutex_lock(&g_cache_lock);
    lmdb_invalidate_cache_locked();
    pthread_mutex_unlock(&g_cache_lock);
#endif
}

void mprm_catalog_backend_lmdb_cache_stats(mprm_lmdb_cache_stats_t *out)
{
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
#if MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    pthread_mutex_lock(&g_cache_lock);
    out->hits = g_cache_hits;
    out->misses = g_cache_misses;
    out->evictions = g_cache_evictions;
    out->entries = g_cache_count;
    out->max_entries = g_cache_max;
    out->reload_events = g_reload_events;
    out->generation = g_generation;
    out->reload_poll_sec = g_reload_poll_sec;
    pthread_mutex_unlock(&g_cache_lock);
#endif
}
