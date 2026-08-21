#include "plugin_manager.h"
#include "tool_registry.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <poll.h>
#if defined(__linux__)
#include <sys/inotify.h>
#endif

#define MAX_PLUGIN_DIRS 8

typedef enum {
    WATCH_MODE_NONE = 0,
    WATCH_MODE_POLL,
    WATCH_MODE_NOTIFY,
    WATCH_MODE_HYBRID
} watcher_mode_t;

typedef enum {
    PLUGIN_DISCOVERED = 0,
    PLUGIN_LOADING,
    PLUGIN_ACTIVE,
    PLUGIN_DRAINING,
    PLUGIN_UNLOADED,
    PLUGIN_FAILED
} plugin_state_t;

typedef struct plugin_record {
    char *path;
    char *id;
    void *dl_handle;
    plugin_state_t state;
    uint32_t api_version;

    diag_plugin_ctx_t *ctx;

    int (*get_api_version)(void);
    int (*init)(const diag_host_api_t *, diag_plugin_ctx_t **);
    size_t (*get_tool_count)(diag_plugin_ctx_t *);
    int (*get_tool)(diag_plugin_ctx_t *, size_t, diag_tool_def_t *);
    int (*invoke)(diag_plugin_ctx_t *, const diag_invoke_req_t *, diag_invoke_resp_t *);
    void (*deinit)(diag_plugin_ctx_t *);

    atomic_uint in_flight;
    unsigned long long mtime_ns;
    off_t file_size;

    char **tool_names;
    size_t tool_count;

    int seen;
    struct plugin_record *next;
} plugin_record_t;

struct plugin_manager {
    plugin_cfg_t cfg;
    diag_host_api_t host;

    pthread_mutex_t lock;
    pthread_mutex_t scan_lock;
    pthread_t watcher_tid;
    int watcher_running;
    watcher_mode_t watcher_mode;

#if defined(__linux__)
    int notify_fd;
    int notify_watch[MAX_PLUGIN_DIRS];
    size_t notify_watch_count;
#endif

    char plugin_dirs[MAX_PLUGIN_DIRS][PATH_MAX];
    char plugin_dirs_real[MAX_PLUGIN_DIRS][PATH_MAX];
    size_t plugin_dir_count;

    plugin_record_t *plugins;
    plugin_metrics_t metrics;
};

typedef struct {
    char *path;
    unsigned long long mtime_ns;
    off_t file_size;
} scan_entry_t;

static char *trim_ws_inplace(char *s)
{
    char *e;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    if (*s == '\0') return s;
    e = s + strlen(s) - 1;
    while (e > s && (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r')) {
        *e = '\0';
        e--;
    }
    return s;
}

static void split_plugin_dirs(plugin_manager_t *pm)
{
    char buf[PATH_MAX * 2];
    char *save = NULL;
    char *tok;

    pm->plugin_dir_count = 0;

    if (!pm->cfg.plugin_dir || !*pm->cfg.plugin_dir)
        pm->cfg.plugin_dir = "/usr/lib/multi-plane-runtime-manager/plugins";

    snprintf(buf, sizeof(buf), "%s", pm->cfg.plugin_dir);
    tok = strtok_r(buf, ",", &save);
    while (tok && pm->plugin_dir_count < MAX_PLUGIN_DIRS) {
        char *t = trim_ws_inplace(tok);
        if (*t) {
            snprintf(pm->plugin_dirs[pm->plugin_dir_count], PATH_MAX, "%s", t);
            if (!realpath(t, pm->plugin_dirs_real[pm->plugin_dir_count])) {
                snprintf(pm->plugin_dirs_real[pm->plugin_dir_count], PATH_MAX, "%s", t);
            }
            pm->plugin_dir_count++;
        }
        tok = strtok_r(NULL, ",", &save);
    }

    if (pm->plugin_dir_count == 0) {
        snprintf(pm->plugin_dirs[0], PATH_MAX, "%s", "/usr/lib/multi-plane-runtime-manager/plugins");
        if (!realpath(pm->plugin_dirs[0], pm->plugin_dirs_real[0])) {
            snprintf(pm->plugin_dirs_real[0], PATH_MAX, "%s", pm->plugin_dirs[0]);
        }
        pm->plugin_dir_count = 1;
    }
}

static int is_so_file(const char *name)
{
    size_t n;
    if (!name) return 0;
    n = strlen(name);
    return (n > 3 && strcmp(name + n - 3, ".so") == 0);
}

static const char *watch_mode_name(watcher_mode_t mode)
{
    switch (mode) {
    case WATCH_MODE_POLL: return "poll";
    case WATCH_MODE_NOTIFY: return "notify";
    case WATCH_MODE_HYBRID: return "hybrid";
    case WATCH_MODE_NONE:
    default:
        return "none";
    }
}

static int watcher_cfg_poll_sec(const plugin_manager_t *pm)
{
    int sec = pm ? pm->cfg.poll_interval_sec : 0;
    return sec > 0 ? sec : 60;
}

static void debounce_wait_ms(const plugin_manager_t *pm)
{
    int ms = pm ? pm->cfg.debounce_ms : 0;
    if (ms <= 0)
        return;
    if (ms > 5000)
        ms = 5000;
    usleep((useconds_t)ms * 1000U);
}

static void pm_log(plugin_manager_t *pm, int level, const char *fmt, ...)
{
    va_list ap;
    if (!pm || !pm->host.log_fn) return;
    va_start(ap, fmt);
    {
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        pm->host.log_fn(level, "%s", buf);
    }
    va_end(ap);
}

static plugin_record_t *find_plugin_by_path(plugin_manager_t *pm, const char *path)
{
    plugin_record_t *p = pm->plugins;
    while (p) {
        if (p->path && strcmp(p->path, path) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

static int path_confined(plugin_manager_t *pm, const char *path, char *resolved, size_t resolved_len)
{
    size_t i;
    if (!realpath(path, resolved))
        return 0;

    for (i = 0; i < pm->plugin_dir_count; i++) {
        const char *base = pm->plugin_dirs_real[i];
        size_t base_len = strlen(base);
        if (strncmp(resolved, base, base_len) != 0)
            continue;
        if (resolved[base_len] != '/' && resolved[base_len] != '\0')
            continue;
        (void)resolved_len;
        return 1;
    }

    (void)resolved_len;
    return 0;
}

static int ownership_mode_ok(const char *path)
{
    struct stat st;
    uid_t me = geteuid();
    if (stat(path, &st) != 0)
        return 0;
    if (!S_ISREG(st.st_mode))
        return 0;
    if (st.st_uid != 0 && st.st_uid != me)
        return 0;
    if ((st.st_mode & S_IWGRP) || (st.st_mode & S_IWOTH))
        return 0;
    return 1;
}

static unsigned long long stat_mtime_ns(const struct stat *st)
{
#if defined(__APPLE__)
    return (unsigned long long)st->st_mtimespec.tv_sec * 1000000000ULL
         + (unsigned long long)st->st_mtimespec.tv_nsec;
#else
    return (unsigned long long)st->st_mtim.tv_sec * 1000000000ULL
         + (unsigned long long)st->st_mtim.tv_nsec;
#endif
}

static void unload_plugin_locked(plugin_manager_t *pm, plugin_record_t *pr)
{
    unsigned int tries = 0;
    if (!pr) return;

    pr->state = PLUGIN_DRAINING;
    tool_registry_unbind_plugin_tools(pr);

    while (atomic_load(&pr->in_flight) > 0 && tries < 500) {
        /*
         * Phase-2.1: avoid holding pm->lock while waiting for in-flight
         * invocations to drain. This keeps unrelated invoke lookups responsive
         * during unload/reload churn.
         */
        pthread_mutex_unlock(&pm->lock);
        usleep(10000);
        pthread_mutex_lock(&pm->lock);
        tries++;
    }

    if (pr->deinit && pr->ctx)
        pr->deinit(pr->ctx);
    pr->ctx = NULL;

    if (pr->dl_handle)
        dlclose(pr->dl_handle);
    pr->dl_handle = NULL;

    pr->state = PLUGIN_UNLOADED;
}

static void remove_plugin_node_locked(plugin_manager_t *pm, plugin_record_t *pr)
{
    plugin_record_t *prev = NULL, *cur = pm->plugins;
    size_t i;
    while (cur) {
        if (cur == pr) {
            if (prev) prev->next = cur->next;
            else pm->plugins = cur->next;
            for (i = 0; i < cur->tool_count; i++)
                free(cur->tool_names[i]);
            free(cur->tool_names);
            free(cur->path);
            free(cur->id);
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static int resolve_symbols(plugin_record_t *pr)
{
    pr->get_api_version = (int (*)(void))dlsym(pr->dl_handle, "diag_plugin_get_api_version");
    pr->init = (int (*)(const diag_host_api_t *, diag_plugin_ctx_t **))dlsym(pr->dl_handle, "diag_plugin_init");
    pr->get_tool_count = (size_t (*)(diag_plugin_ctx_t *))dlsym(pr->dl_handle, "diag_plugin_get_tool_count");
    pr->get_tool = (int (*)(diag_plugin_ctx_t *, size_t, diag_tool_def_t *))dlsym(pr->dl_handle, "diag_plugin_get_tool");
    pr->invoke = (int (*)(diag_plugin_ctx_t *, const diag_invoke_req_t *, diag_invoke_resp_t *))dlsym(pr->dl_handle, "diag_plugin_invoke");
    pr->deinit = (void (*)(diag_plugin_ctx_t *))dlsym(pr->dl_handle, "diag_plugin_deinit");

    if (!pr->get_api_version || !pr->init || !pr->get_tool_count ||
        !pr->get_tool || !pr->invoke || !pr->deinit)
        return -1;
    return 0;
}

static int load_plugin_locked(plugin_manager_t *pm, const char *path)
{
    char resolved[PATH_MAX];
    struct stat st;
    plugin_record_t *pr = NULL;
    size_t i;
    int rc = -1;

    if (!path_confined(pm, path, resolved, sizeof(resolved))) {
        pm_log(pm, 4, "plugin_failed path_confine path=%s", path);
        pm->metrics.plugins_failed_total++;
        return -1;
    }

    if (!ownership_mode_ok(resolved)) {
        pm_log(pm, 4, "plugin_failed owner_mode path=%s", resolved);
        pm->metrics.plugins_failed_total++;
        return -1;
    }

    if (pm->cfg.verify_mode && strcmp(pm->cfg.verify_mode, "off") != 0) {
        pm_log(pm, 4, "plugin_failed verify_mode=%s unsupported in v1 path=%s",
               pm->cfg.verify_mode, resolved);
        pm->metrics.plugins_failed_total++;
        return -1;
    }

    if (stat(resolved, &st) != 0) {
        pm->metrics.plugins_failed_total++;
        return -1;
    }

    pr = (plugin_record_t *)calloc(1, sizeof(*pr));
    if (!pr) {
        pm->metrics.plugins_failed_total++;
        return -1;
    }

    pr->path = strdup(resolved);
    pr->id = strdup(resolved);
    pr->state = PLUGIN_LOADING;
    atomic_init(&pr->in_flight, 0);
    pr->mtime_ns = stat_mtime_ns(&st);
    pr->file_size = st.st_size;

    pr->dl_handle = dlopen(resolved, RTLD_NOW | RTLD_LOCAL);
    if (!pr->dl_handle) {
        pm_log(pm, 4, "plugin_failed dlopen path=%s err=%s", resolved, dlerror());
        goto fail;
    }

    if (resolve_symbols(pr) != 0) {
        pm_log(pm, 4, "plugin_failed symbols path=%s", resolved);
        goto fail;
    }

    pr->api_version = (uint32_t)pr->get_api_version();
    if (pr->api_version != DIAG_PLUGIN_API_VERSION) {
        pm_log(pm, 4, "plugin_failed api_version path=%s got=%u need=%u",
               resolved, pr->api_version, DIAG_PLUGIN_API_VERSION);
        goto fail;
    }

    if (pr->init(&pm->host, &pr->ctx) != 0 || !pr->ctx) {
        pm_log(pm, 4, "plugin_failed init path=%s", resolved);
        goto fail;
    }

    pr->tool_count = pr->get_tool_count(pr->ctx);
    if (pr->tool_count == 0) {
        pm_log(pm, 4, "plugin_failed no_tools path=%s", resolved);
        goto fail;
    }

    pr->tool_names = (char **)calloc(pr->tool_count, sizeof(char *));
    if (!pr->tool_names) goto fail;

    for (i = 0; i < pr->tool_count; i++) {
        diag_tool_def_t td;
        memset(&td, 0, sizeof(td));
        if (pr->get_tool(pr->ctx, i, &td) != 0 || !td.tool_name || !*td.tool_name) {
            pm_log(pm, 4, "plugin_failed get_tool path=%s idx=%zu", resolved, i);
            goto fail;
        }

        if (pm->cfg.catalog_tool_exists_cb && pm->cfg.conflict_policy == 0 &&
            pm->cfg.catalog_tool_exists_cb(td.tool_name, pm->cfg.catalog_ctx)) {
            pm_log(pm, 4, "plugin_failed conflict_catalog tool=%s path=%s", td.tool_name, resolved);
            goto fail;
        }

        if (tool_registry_bind_plugin_tool(td.tool_name, pr, pm->cfg.conflict_policy) != 0) {
            pm_log(pm, 4, "plugin_failed conflict_registry tool=%s path=%s", td.tool_name, resolved);
            goto fail;
        }

        pr->tool_names[i] = strdup(td.tool_name);
        if (!pr->tool_names[i]) goto fail;
        pm->metrics.tools_registered_total++;
    }

    pr->state = PLUGIN_ACTIVE;
    pr->seen = 1;
    pr->next = pm->plugins;
    pm->plugins = pr;

    pm->metrics.plugins_loaded_total++;
    pm_log(pm, 6, "plugin_loaded path=%s tools=%zu", resolved, pr->tool_count);
    return 0;

fail:
    if (pr) {
        tool_registry_unbind_plugin_tools(pr);
        if (pr->deinit && pr->ctx) pr->deinit(pr->ctx);
        if (pr->dl_handle) dlclose(pr->dl_handle);
        if (pr->tool_names) {
            for (i = 0; i < pr->tool_count; i++)
                free(pr->tool_names[i]);
            free(pr->tool_names);
        }
        free(pr->path);
        free(pr->id);
        free(pr);
    }
    pm->metrics.plugins_failed_total++;
    rc = -1;
    return rc;
}

static int reload_plugin_locked(plugin_manager_t *pm, plugin_record_t *pr)
{
    char *path_dup;
    if (!pr || !pr->path)
        return -1;

    path_dup = strdup(pr->path);
    if (!path_dup)
        return -1;

    unload_plugin_locked(pm, pr);
    remove_plugin_node_locked(pm, pr);

    if (load_plugin_locked(pm, path_dup) == 0) {
        pm->metrics.plugin_reload_total++;
        pm_log(pm, 6, "plugin_reloaded path=%s", path_dup);
        free(path_dup);
        return 0;
    }

    pm_log(pm, 4, "plugin_reload_failed path=%s", path_dup);
    free(path_dup);
    return -1;
}

static int scan_entry_append(scan_entry_t **entries,
                             size_t *count,
                             size_t *cap,
                             const char *path,
                             const struct stat *st)
{
    scan_entry_t *tmp;
    char *dup;

    if (!entries || !count || !cap || !path || !st)
        return -1;

    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 16 : (*cap * 2);
        tmp = (scan_entry_t *)realloc(*entries, new_cap * sizeof(**entries));
        if (!tmp)
            return -1;
        *entries = tmp;
        *cap = new_cap;
    }

    dup = strdup(path);
    if (!dup)
        return -1;

    (*entries)[*count].path = dup;
    (*entries)[*count].mtime_ns = stat_mtime_ns(st);
    (*entries)[*count].file_size = st->st_size;
    (*count)++;
    return 0;
}

static void scan_entries_free(scan_entry_t *entries, size_t count)
{
    size_t i;
    if (!entries)
        return;
    for (i = 0; i < count; i++)
        free(entries[i].path);
    free(entries);
}

static void collect_scan_entries(plugin_manager_t *pm,
                                 scan_entry_t **entries,
                                 size_t *entry_count)
{
    size_t d_idx;
    size_t cap = 0;

    *entries = NULL;
    *entry_count = 0;

    for (d_idx = 0; d_idx < pm->plugin_dir_count; d_idx++) {
        DIR *d = opendir(pm->plugin_dirs[d_idx]);
        struct dirent *ent;
        if (!d) {
            pm_log(pm, 4, "plugin_scan_open_failed dir=%s err=%s",
                   pm->plugin_dirs[d_idx], strerror(errno));
            continue;
        }

        while ((ent = readdir(d)) != NULL) {
            char full[PATH_MAX];
            char normalized[PATH_MAX];
            struct stat st;
            int n;

            if (ent->d_name[0] == '.')
                continue;
            if (!is_so_file(ent->d_name))
                continue;

            n = snprintf(full, sizeof(full), "%s/%s", pm->plugin_dirs[d_idx], ent->d_name);
            if (n < 0 || (size_t)n >= sizeof(full))
                continue;

            if (!realpath(full, normalized))
                continue;

            if (stat(normalized, &st) != 0)
                continue;

            if (scan_entry_append(entries, entry_count, &cap, normalized, &st) != 0) {
                pm_log(pm, 4, "plugin_scan_collect_oom path=%s", normalized);
                continue;
            }
        }

        closedir(d);
    }
}

static void *watcher_main(void *arg)
{
    plugin_manager_t *pm = (plugin_manager_t *)arg;

    if (!pm)
        return NULL;

    if (pm->watcher_mode == WATCH_MODE_POLL) {
        while (pm->watcher_running) {
            int sec = watcher_cfg_poll_sec(pm);
            for (int i = 0; i < sec && pm->watcher_running; i++)
                sleep(1);
            if (!pm->watcher_running)
                break;
            plugin_manager_scan(pm);
        }
        return NULL;
    }

#if defined(__linux__)
    if (pm->watcher_mode == WATCH_MODE_NOTIFY) {
        char evbuf[4096];
        while (pm->watcher_running) {
            ssize_t n = read(pm->notify_fd, evbuf, sizeof(evbuf));
            if (n <= 0) {
                if (!pm->watcher_running)
                    break;
                if (errno == EINTR)
                    continue;
                if (errno == EBADF || errno == EINVAL)
                    break;
                usleep(100000);
                continue;
            }

            plugin_manager_scan(pm);
        }
        return NULL;
    }

    if (pm->watcher_mode == WATCH_MODE_HYBRID) {
        char evbuf[4096];
        while (pm->watcher_running) {
            struct pollfd pfd;
            int rc;
            int timeout_ms = watcher_cfg_poll_sec(pm) * 1000;

            pfd.fd = pm->notify_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;

            rc = poll(&pfd, 1, timeout_ms);
            if (rc == 0) {
                plugin_manager_scan(pm); /* periodic reconcile */
                continue;
            }

            if (rc < 0) {
                if (errno == EINTR)
                    continue;
                pm_log(pm, 4, "plugin_watcher_poll_err mode=hybrid err=%s", strerror(errno));
                plugin_manager_scan(pm);
                usleep(100000);
                continue;
            }

            if (pfd.revents & POLLIN) {
                ssize_t n = read(pm->notify_fd, evbuf, sizeof(evbuf));
                if (n < 0 && errno != EINTR) {
                    pm_log(pm, 4, "plugin_watcher_notify_read_err mode=hybrid err=%s", strerror(errno));
                }
                plugin_manager_scan(pm);
            } else if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                pm_log(pm, 4, "plugin_watcher_notify_fd_state mode=hybrid revents=0x%x", pfd.revents);
                plugin_manager_scan(pm);
                usleep(100000);
            }
        }
        return NULL;
    }
#endif

    return NULL;
}

int plugin_manager_init(plugin_manager_t **out_pm,
                        const plugin_cfg_t *cfg,
                        const diag_host_api_t *host)
{
    plugin_manager_t *pm;

    if (!out_pm || !cfg)
        return -1;

    pm = (plugin_manager_t *)calloc(1, sizeof(*pm));
    if (!pm)
        return -1;

    pm->cfg = *cfg;
    if (host)
        pm->host = *host;

    if (!pm->cfg.plugin_dir)
        pm->cfg.plugin_dir = "/usr/lib/multi-plane-runtime-manager/plugins";
    if (pm->cfg.poll_interval_sec < 0)
        pm->cfg.poll_interval_sec = 0;
    if (!pm->cfg.verify_mode)
        pm->cfg.verify_mode = "off";
    if (!pm->cfg.discovery_mode || !*pm->cfg.discovery_mode)
        pm->cfg.discovery_mode = "hybrid";
    if (pm->cfg.debounce_ms < 0)
        pm->cfg.debounce_ms = 0;

    pm->watcher_mode = WATCH_MODE_NONE;
#if defined(__linux__)
    pm->notify_fd = -1;
    pm->notify_watch_count = 0;
    for (size_t i = 0; i < MAX_PLUGIN_DIRS; i++)
        pm->notify_watch[i] = -1;
#endif

    pthread_mutex_init(&pm->lock, NULL);
    pthread_mutex_init(&pm->scan_lock, NULL);
    tool_registry_init();

    split_plugin_dirs(pm);

    *out_pm = pm;
    return 0;
}

int plugin_manager_start(plugin_manager_t *pm)
{
    const char *mode_s;
    int want_poll = 0;
    int want_notify = 0;

    if (!pm)
        return -1;
    if (!pm->cfg.enabled)
        return 0;

    plugin_manager_scan(pm);

    mode_s = pm->cfg.discovery_mode ? pm->cfg.discovery_mode : "hybrid";
    if (strcmp(mode_s, "poll") == 0) {
        want_poll = 1;
    } else if (strcmp(mode_s, "notify") == 0) {
        want_notify = 1;
        want_poll = 1; /* v1 policy: periodic reconcile is mandatory */
    } else { /* hybrid or unknown */
        want_poll = 1;
        want_notify = 1;
    }

    if (want_poll && pm->cfg.poll_interval_sec <= 0)
        pm->cfg.poll_interval_sec = 60;

    if (strcmp(mode_s, "notify") == 0) {
        pm_log(pm, 6, "plugin_watcher_v1_policy_enforced requested_mode=notify effective_mode=hybrid poll_interval=%d",
               pm->cfg.poll_interval_sec);
    }

    if (want_poll && !want_notify) {
        pm->watcher_mode = WATCH_MODE_POLL;
        pm->watcher_running = 1;
        if (pthread_create(&pm->watcher_tid, NULL, watcher_main, pm) != 0) {
            pm->watcher_running = 0;
            pm->watcher_mode = WATCH_MODE_NONE;
            return -1;
        }

        pm_log(pm, 6, "plugin_watcher_started mode=%s dir=%s poll_interval=%d debounce_ms=%d",
               watch_mode_name(pm->watcher_mode),
             pm->cfg.plugin_dir, pm->cfg.poll_interval_sec, pm->cfg.debounce_ms);
        return 0;
    }

#if defined(__linux__)
    if (want_notify) {
        pm->notify_fd = inotify_init1(IN_CLOEXEC);
    }

    if (want_notify && pm->notify_fd >= 0) {
        size_t i;
        for (i = 0; i < pm->plugin_dir_count && i < MAX_PLUGIN_DIRS; i++) {
            int wd = inotify_add_watch(pm->notify_fd,
                                       pm->plugin_dirs[i],
                                       IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
                                       IN_CLOSE_WRITE | IN_ATTRIB);
            if (wd >= 0) {
                pm->notify_watch[pm->notify_watch_count++] = wd;
            } else {
                pm_log(pm, 4, "plugin_notify_watch_failed dir=%s err=%s",
                       pm->plugin_dirs[i], strerror(errno));
            }
        }
    }

    if (want_notify && pm->notify_fd >= 0 && pm->notify_watch_count > 0) {
        pm->watcher_mode = want_poll ? WATCH_MODE_HYBRID : WATCH_MODE_NOTIFY;
        pm->watcher_running = 1;
        if (pthread_create(&pm->watcher_tid, NULL, watcher_main, pm) != 0) {
            size_t i;
            pm->watcher_running = 0;
            pm->watcher_mode = WATCH_MODE_NONE;
            for (i = 0; i < pm->notify_watch_count; i++) {
                if (pm->notify_watch[i] >= 0)
                    inotify_rm_watch(pm->notify_fd, pm->notify_watch[i]);
            }
            close(pm->notify_fd);
            pm->notify_watch_count = 0;
            for (i = 0; i < MAX_PLUGIN_DIRS; i++)
                pm->notify_watch[i] = -1;
            pm->notify_fd = -1;
            return -1;
        }

        pm_log(pm, 6, "plugin_watcher_started mode=%s dir=%s poll_interval=%d debounce_ms=%d",
               watch_mode_name(pm->watcher_mode),
               pm->cfg.plugin_dir,
               pm->cfg.poll_interval_sec,
               pm->cfg.debounce_ms);
        return 0;
    }

    if (want_notify && pm->notify_fd >= 0) {
        size_t i;
        for (i = 0; i < pm->notify_watch_count; i++) {
            if (pm->notify_watch[i] >= 0)
                inotify_rm_watch(pm->notify_fd, pm->notify_watch[i]);
        }
        close(pm->notify_fd);
    }
    pm->notify_watch_count = 0;
    for (size_t i = 0; i < MAX_PLUGIN_DIRS; i++)
        pm->notify_watch[i] = -1;
    pm->notify_fd = -1;
    if (want_poll) {
        pm->watcher_mode = WATCH_MODE_POLL;
        pm->watcher_running = 1;
        if (pthread_create(&pm->watcher_tid, NULL, watcher_main, pm) != 0) {
            pm->watcher_running = 0;
            pm->watcher_mode = WATCH_MODE_NONE;
            return -1;
        }
        pm_log(pm, 4, "plugin_watcher_notify_unavailable_fallback mode=poll dirs=%s poll_interval=%d debounce_ms=%d",
               pm->cfg.plugin_dir,
               pm->cfg.poll_interval_sec,
               pm->cfg.debounce_ms);
        return 0;
    }

    pm_log(pm, 4, "plugin_watcher_disabled mode=notify_unavailable dirs=%s", pm->cfg.plugin_dir);
#else
    if (want_poll) {
        pm->watcher_mode = WATCH_MODE_POLL;
        pm->watcher_running = 1;
        if (pthread_create(&pm->watcher_tid, NULL, watcher_main, pm) != 0) {
            pm->watcher_running = 0;
            pm->watcher_mode = WATCH_MODE_NONE;
            return -1;
        }
        pm_log(pm, 6, "plugin_watcher_started mode=%s dirs=%s poll_interval=%d debounce_ms=%d",
               watch_mode_name(pm->watcher_mode),
               pm->cfg.plugin_dir,
               pm->cfg.poll_interval_sec,
               pm->cfg.debounce_ms);
        return 0;
    }

    pm_log(pm, 6, "plugin_watcher_disabled mode=off dirs=%s poll_interval=0", pm->cfg.plugin_dir);
#endif

    return 0;
}

int plugin_manager_stop(plugin_manager_t *pm)
{
    plugin_record_t *p, *next;
    if (!pm)
        return -1;

    if (pm->watcher_running) {
        pm->watcher_running = 0;
#if defined(__linux__)
        if (pm->watcher_mode == WATCH_MODE_NOTIFY && pm->notify_fd >= 0) {
            size_t i;
            for (i = 0; i < pm->notify_watch_count; i++) {
                if (pm->notify_watch[i] >= 0)
                    inotify_rm_watch(pm->notify_fd, pm->notify_watch[i]);
                pm->notify_watch[i] = -1;
            }
            pm->notify_watch_count = 0;
            close(pm->notify_fd);
            pm->notify_fd = -1;
        }
#endif
        pthread_join(pm->watcher_tid, NULL);
    }

    pm->watcher_mode = WATCH_MODE_NONE;

    pthread_mutex_lock(&pm->scan_lock);
    pthread_mutex_lock(&pm->lock);
    p = pm->plugins;
    while (p) {
        next = p->next;
        unload_plugin_locked(pm, p);
        p = next;
    }
    while (pm->plugins)
        remove_plugin_node_locked(pm, pm->plugins);
    pthread_mutex_unlock(&pm->lock);
    pthread_mutex_unlock(&pm->scan_lock);

    return 0;
}

int plugin_manager_scan(plugin_manager_t *pm)
{
    scan_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t i;
    int registry_changed = 0;

    if (!pm || !pm->cfg.enabled)
        return 0;

    pthread_mutex_lock(&pm->scan_lock);

    /* Phase-2 lock-scope refinement:
     * keep debounce and filesystem traversal out of pm->lock to reduce
     * invoke-path stalls during scan cycles.
     */
    debounce_wait_ms(pm);
    collect_scan_entries(pm, &entries, &entry_count);

    pthread_mutex_lock(&pm->lock);

    for (plugin_record_t *p = pm->plugins; p; p = p->next)
        p->seen = 0;

    for (i = 0; i < entry_count; i++) {
        plugin_record_t *pr = find_plugin_by_path(pm, entries[i].path);
        if (!pr) {
            if (load_plugin_locked(pm, entries[i].path) == 0)
                registry_changed = 1;
            continue;
        }

        pr->seen = 1;
        if (pr->mtime_ns != entries[i].mtime_ns || pr->file_size != entries[i].file_size) {
            if (reload_plugin_locked(pm, pr) == 0)
                registry_changed = 1;
        }
    }

    {
        plugin_record_t *p = pm->plugins;
        plugin_record_t *next;
        while (p) {
            next = p->next;
            if (!p->seen) {
                pm_log(pm, 6, "plugin_removed path=%s", p->path ? p->path : "?");
                unload_plugin_locked(pm, p);
                remove_plugin_node_locked(pm, p);
                registry_changed = 1;
            }
            p = next;
        }
    }

    pthread_mutex_unlock(&pm->lock);

    scan_entries_free(entries, entry_count);

    if (registry_changed && pm->cfg.on_registry_changed_cb)
        pm->cfg.on_registry_changed_cb(pm->cfg.registry_changed_ctx);

    pthread_mutex_unlock(&pm->scan_lock);

    return 0;
}

int plugin_manager_destroy(plugin_manager_t *pm)
{
    if (!pm)
        return 0;

    plugin_manager_stop(pm);
    tool_registry_destroy();
    pthread_mutex_destroy(&pm->scan_lock);
    pthread_mutex_destroy(&pm->lock);
    free(pm);
    return 0;
}

plugin_invoke_result_t plugin_manager_invoke(plugin_manager_t *pm,
                                             const char *tool,
                                             const diag_invoke_req_t *req,
                                             diag_invoke_resp_t *resp)
{
    tool_binding_t b;
    plugin_record_t *pr;
    int rc;

    if (!pm || !pm->cfg.enabled || !tool)
        return PLUGIN_INVOKE_NOT_FOUND;

    pthread_mutex_lock(&pm->lock);
    if (tool_registry_lookup(tool, &b) != 0 || b.provider != TOOL_PROVIDER_PLUGIN || !b.plugin) {
        pthread_mutex_unlock(&pm->lock);
        return PLUGIN_INVOKE_NOT_FOUND;
    }

    pr = (plugin_record_t *)b.plugin;
    if (!pr || pr->state != PLUGIN_ACTIVE || !pr->invoke || !pr->ctx) {
        pthread_mutex_unlock(&pm->lock);
        return PLUGIN_INVOKE_ERR_UNAVAILABLE;
    }

    atomic_fetch_add(&pr->in_flight, 1);
    pthread_mutex_unlock(&pm->lock);

    memset(resp, 0, sizeof(*resp));
    rc = pr->invoke(pr->ctx, req, resp);

    atomic_fetch_sub(&pr->in_flight, 1);

    pm->metrics.plugin_invocations_total++;

    if (rc != 0) {
        pm->metrics.plugin_invocation_errors_total++;
        return PLUGIN_INVOKE_ERR_INVOKE;
    }
    return PLUGIN_INVOKE_OK;
}

void plugin_manager_get_metrics(plugin_manager_t *pm, plugin_metrics_t *out)
{
    if (!pm || !out) return;
    *out = pm->metrics;
}

int plugin_manager_get_tool_publish_meta(plugin_manager_t *pm,
                                         const char *tool_name,
                                         char *plugin_path_out,
                                         size_t plugin_path_out_len,
                                         int *timeout_out)
{
    plugin_record_t *pr;
    size_t i;

    if (!pm || !tool_name || !*tool_name)
        return -1;

    pthread_mutex_lock(&pm->lock);
    pr = pm->plugins;
    while (pr) {
        if (pr->state == PLUGIN_ACTIVE && pr->tool_names) {
            for (i = 0; i < pr->tool_count; i++) {
                if (pr->tool_names[i] && strcmp(pr->tool_names[i], tool_name) == 0) {
                    if (plugin_path_out && plugin_path_out_len > 0) {
                        snprintf(plugin_path_out, plugin_path_out_len, "%s", pr->path ? pr->path : "");
                    }
                    if (timeout_out)
                        *timeout_out = 0;
                    pthread_mutex_unlock(&pm->lock);
                    return 0;
                }
            }
        }
        pr = pr->next;
    }
    pthread_mutex_unlock(&pm->lock);
    return 1;
}
