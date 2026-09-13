#define _POSIX_C_SOURCE 200809L

#include "scan.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdatomic.h>

#define SCAN_PATH_MAX 4096

static const char *skip_dirs[] = {
    "/proc",
    "/sys",
    "/dev",
    "/run",
    NULL
};

static int is_skipped(const char *path)
{
    for (int i = 0; skip_dirs[i]; i++) {
        size_t len = strlen(skip_dirs[i]);
        if (strncmp(path, skip_dirs[i], len) == 0 &&
            (path[len] == '\0' || path[len] == '/')) {
            return 1;
        }
    }
    return 0;
}

void scan_init_stats(ScanStats *s)
{
    if (!s) return;
    s->files = 0;
    s->errors = 0;
    s->no_access = 0;
}

long long scan_dir_weight(const char *path, ScanStats *stats)
{
    if (is_skipped(path)) return 0;

    DIR *d = opendir(path);
    if (!d) {
        if (stats) stats->no_access++;
        return 0;
    }

    struct dirent *entry;
    struct stat st;
    long long total = 0;
    char fullpath[SCAN_PATH_MAX];

    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;

        size_t len = strlen(path);
        if (len > 0 && path[len - 1] == '/') {
            snprintf(fullpath, sizeof(fullpath), "%s%s", path, entry->d_name);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        }

        if (lstat(fullpath, &st) == -1) {
            if (stats) stats->errors++;
            continue;
        }

        if (S_ISREG(st.st_mode)) {
            if (stats) stats->files++;

            long long sz = (long long)st.st_blocks * 512;
            if (sz == 0) sz = (long long)st.st_size;
            total += sz;
        } else if (S_ISDIR(st.st_mode)) {
            total += scan_dir_weight(fullpath, stats);
        }
    }

    closedir(d);
    cache_put(path, total);
    return total;
}

static pthread_t      scan_thread;
static atomic_bool    scan_running = false;
static atomic_bool    scan_has_worker = false;
static char           scan_root[SCAN_PATH_MAX];
static long long      scan_result_bytes = 0;
static ScanStats      scan_result_stats;

static void *scan_worker(void *arg)
{
    (void)arg;

    free_cache();

    ScanStats local;
    scan_init_stats(&local);

    long long bytes = scan_dir_weight(scan_root, &local);

    cache_sort();

    scan_result_bytes = bytes;
    scan_result_stats = local;

    atomic_store(&scan_running, false);
    return NULL;
}

int scan_start_background(const char *path)
{
    if (atomic_load(&scan_running)) return 0;

    strncpy(scan_root, path, sizeof(scan_root) - 1);
    scan_root[sizeof(scan_root) - 1] = '\0';

    atomic_store(&scan_running, true);
    atomic_store(&scan_has_worker, true);

    if (pthread_create(&scan_thread, NULL, scan_worker, NULL) != 0) {
        atomic_store(&scan_running, false);
        atomic_store(&scan_has_worker, false);
        return 0;
    }
    return 1;
}

int scan_is_running(void)
{
    return atomic_load(&scan_running) ? 1 : 0;
}

long long scan_finish(ScanStats *out_stats)
{
    if (!atomic_load(&scan_has_worker)) return 0;

    pthread_join(scan_thread, NULL);

    atomic_store(&scan_has_worker, false);

    if (out_stats) *out_stats = scan_result_stats;
    return scan_result_bytes;
}
