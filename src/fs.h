#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <sys/types.h>

/*
 * Файловые операции (POSIX).
 */

typedef enum {
    ENTRY_FILE,
    ENTRY_DIR,
    ENTRY_LINK,
    ENTRY_EXEC,
    ENTRY_OTHER
} EntryType;

typedef struct {
    char *name;
    long long size;
    EntryType type;
    mode_t mode;         /* права доступа (биты st_mode) */
    char *link_target;   /* цель симлинка или NULL */
    long long mtime;
} Entry;

int fs_list_dir(const char *path, Entry **out, size_t *count);
void fs_free_entries(Entry *entries, size_t count);

int fs_mkdir(const char *path);
int fs_create_file(const char *path);
int fs_delete(const char *path);
int fs_copy(const char *src, const char *dst);
int fs_move(const char *src, const char *dst);
int fs_rename(const char *old_path, const char *new_path);

void normalize_path(char *path);

/* Человекочитаемые права: "-rwxr-xr-x" и т.п.
 * buf должен быть минимум 11 байт. */
void fs_format_mode(mode_t mode, char *buf, size_t bufsz);

#endif
