#ifndef CACHE_H
#define CACHE_H

typedef struct {
    char *path;
    long long weight;
} FileSys;

typedef struct {
    FileSys *data;
    int count;
    int capacity;
} CacheArray;

extern CacheArray cache;

long long cache_get(const char *path);
void cache_put(const char *path, long long weight);
void cache_sort(void);
void free_cache(void);

int cache_save(const char *filename);

int cache_load(const char *filename);

#endif
