#define _POSIX_C_SOURCE 200809L

#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CacheArray cache = {NULL, 0, 0};

static int cmp_filesys(const void *a, const void *b)
{
    const FileSys *x = (const FileSys *)a;
    const FileSys *y = (const FileSys *)b;
    return strcmp(x->path, y->path);
}

long long cache_get(const char *path)
{
    if (cache.count == 0) return -1;

    FileSys key;
    key.path = (char *)path;
    key.weight = 0;

    FileSys *found = bsearch(&key, cache.data, (size_t)cache.count,
                             sizeof(FileSys), cmp_filesys);
    return found ? found->weight : -1;
}

void cache_put(const char *path, long long weight)
{
    if (cache.count >= cache.capacity) {
        int new_cap = (cache.capacity == 0) ? 10000 : cache.capacity * 2;
        FileSys *new_data = realloc(cache.data, (size_t)new_cap * sizeof(FileSys));
        if (!new_data) return;
        cache.data = new_data;
        cache.capacity = new_cap;
    }

    size_t len = strlen(path) + 1;
    cache.data[cache.count].path = malloc(len);
    if (!cache.data[cache.count].path) return;
    memcpy(cache.data[cache.count].path, path, len);
    cache.data[cache.count].weight = weight;
    cache.count++;
}

void cache_sort(void)
{
    if (cache.count > 1) {
        qsort(cache.data, (size_t)cache.count, sizeof(FileSys), cmp_filesys);
    }
}

void free_cache(void)
{
    for (int i = 0; i < cache.count; i++) free(cache.data[i].path);
    free(cache.data);
    cache.data = NULL;
    cache.count = 0;
    cache.capacity = 0;
}

int cache_save(const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (!f) return -1;

    for (int i = 0; i < cache.count; i++) {
        /* Путь может содержать пробелы, но не табы — используем \t как разделитель. */
        fprintf(f, "%s\t%lld\n", cache.data[i].path, cache.data[i].weight);
    }
    fclose(f);
    return 0;
}

int cache_load(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    free_cache();

    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        /* Разделитель — последний \t в строке */
        char *tab = strrchr(line, '\t');
        if (!tab) continue;

        *tab = '\0';
        char *path = line;
        long long weight = atoll(tab + 1);

        /* Убираем завершающий \n */
        size_t plen = strlen(path);
        if (plen == 0) continue;

        if (plen > 0 && (path[plen - 1] == '\n' || path[plen - 1] == '\r'))
            path[plen - 1] = '\0';

        cache_put(path, weight);
    }

    fclose(f);
    cache_sort();
    return 0;
}
