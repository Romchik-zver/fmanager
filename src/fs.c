#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define FS_PATH_MAX   4096
#define COPY_BUF_SIZE 65536

void normalize_path(char *path)
{
    if (!path) return;

    char *src = path;
    char *dst = path;
    while (*src) {
        if (*src == '/') {
            if (dst > path && *(dst - 1) == '/') { src++; continue; }
        }
        *dst++ = *src++;
    }
    *dst = '\0';

    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';
}

static void build_path(char *buf, size_t bufsz, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/')
        snprintf(buf, bufsz, "%s%s", dir, name);
    else
        snprintf(buf, bufsz, "%s/%s", dir, name);
}

void fs_format_mode(mode_t mode, char *buf, size_t bufsz)
{
    if (bufsz < 11) {
        if (bufsz > 0) buf[0] = '\0';
        return;
    }

    char type = '?';
    if      (S_ISDIR(mode))  type = 'd';
    else if (S_ISLNK(mode))  type = 'l';
    else if (S_ISREG(mode))  type = '-';
    else if (S_ISCHR(mode))  type = 'c';
    else if (S_ISBLK(mode))  type = 'b';
    else if (S_ISFIFO(mode)) type = 'p';
    else if (S_ISSOCK(mode)) type = 's';

    buf[0]  = type;
    buf[1]  = (mode & S_IRUSR) ? 'r' : '-';
    buf[2]  = (mode & S_IWUSR) ? 'w' : '-';
    buf[3]  = (mode & S_IXUSR) ? 'x' : '-';
    buf[4]  = (mode & S_IRGRP) ? 'r' : '-';
    buf[5]  = (mode & S_IWGRP) ? 'w' : '-';
    buf[6]  = (mode & S_IXGRP) ? 'x' : '-';
    buf[7]  = (mode & S_IROTH) ? 'r' : '-';
    buf[8]  = (mode & S_IWOTH) ? 'w' : '-';
    buf[9]  = (mode & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';

    if (mode & S_ISUID) buf[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) buf[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) buf[9] = (mode & S_IXOTH) ? 't' : 'T';
}

int fs_list_dir(const char *path, Entry **out, size_t *count)
{
    *out = NULL;
    *count = 0;

    DIR *d = opendir(path);
    if (!d) return -1;

    size_t cap = 64;
    size_t n = 0;
    Entry *arr = malloc(cap * sizeof(Entry));
    if (!arr) { closedir(d); return -1; }

    struct dirent *de;
    struct stat st;
    char fullpath[FS_PATH_MAX];

    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) continue;

        build_path(fullpath, sizeof(fullpath), path, de->d_name);
        if (lstat(fullpath, &st) == -1) continue;

        if (n >= cap) {
            cap *= 2;
            Entry *tmp = realloc(arr, cap * sizeof(Entry));
            if (!tmp) break;
            arr = tmp;
        }

        arr[n].name = strdup(de->d_name);
        if (!arr[n].name) break;

        arr[n].mode = st.st_mode;
        arr[n].mtime = (long long)st.st_mtime;

        if (S_ISREG(st.st_mode)) {
            long long sz = (long long)st.st_blocks * 512;
            if (sz == 0) sz = (long long)st.st_size;
            arr[n].size = sz;
        } else {
            arr[n].size = (long long)st.st_size;
        }

        if (S_ISDIR(st.st_mode)) {
            arr[n].type = ENTRY_DIR;
        } else if (S_ISLNK(st.st_mode)) {
            arr[n].type = ENTRY_LINK;
        } else if (S_ISREG(st.st_mode)) {
            if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
                arr[n].type = ENTRY_EXEC;
            else
                arr[n].type = ENTRY_FILE;
        } else {
            arr[n].type = ENTRY_OTHER;
        }

        arr[n].link_target = NULL;
        if (S_ISLNK(st.st_mode)) {
            char target[FS_PATH_MAX];
            ssize_t r = readlink(fullpath, target, sizeof(target) - 1);
            if (r >= 0) {
                target[r] = '\0';
                arr[n].link_target = strdup(target);
            }
        }

        n++;
    }

    closedir(d);
    *out = arr;
    *count = n;
    return 0;
}

void fs_free_entries(Entry *entries, size_t count)
{
    if (!entries) return;
    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
        free(entries[i].link_target);
    }
    free(entries);
}

int fs_mkdir(const char *path)
{
    return (mkdir(path, 0755) == 0) ? 0 : -1;
}

int fs_create_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

int fs_delete(const char *path)
{
    if (strcmp(path, "/") == 0) return -1;

    struct stat st;
    if (lstat(path, &st) == -1) return -1;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;

        struct dirent *de;
        char fullpath[FS_PATH_MAX];
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 ||
                strcmp(de->d_name, "..") == 0) continue;
            build_path(fullpath, sizeof(fullpath), path, de->d_name);
            (void)fs_delete(fullpath);
        }
        closedir(d);
        return (rmdir(path) == 0) ? 0 : -1;
    }

    return (unlink(path) == 0) ? 0 : -1;
}

static int copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;

    struct stat st;
    if (fstat(in, &st) != 0) { close(in); return -1; }

    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (out < 0) { close(in); return -1; }

    char buf[COPY_BUF_SIZE];
    ssize_t r;
    int rc = 0;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w < 0) { rc = -1; goto done; }
            off += w;
        }
    }
    if (r < 0) rc = -1;

done:
    close(in);
    close(out);
    return rc;
}

static int copy_recursive(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) == -1) return -1;

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, st.st_mode & 0777) == -1 && errno != EEXIST) return -1;

        DIR *d = opendir(src);
        if (!d) return -1;

        struct dirent *de;
        char sfull[FS_PATH_MAX], dfull[FS_PATH_MAX];
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 ||
                strcmp(de->d_name, "..") == 0) continue;
            build_path(sfull, sizeof(sfull), src, de->d_name);
            build_path(dfull, sizeof(dfull), dst, de->d_name);
            (void)copy_recursive(sfull, dfull);
        }
        closedir(d);
        return 0;
    }

    return copy_file(src, dst);
}

int fs_copy(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(dst, &st) == 0) { errno = EEXIST; return -1; }
    return copy_recursive(src, dst);
}

int fs_rename(const char *old_path, const char *new_path)
{
    return (rename(old_path, new_path) == 0) ? 0 : -1;
}

int fs_move(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(dst, &st) == 0) { errno = EEXIST; return -1; }

    if (rename(src, dst) == 0) return 0;
    if (errno != EXDEV) return -1;

    if (copy_recursive(src, dst) != 0) return -1;
    return fs_delete(src);
}
