#ifndef CACHE_H
#define CACHE_H

/*
 * Кэш размеров директорий + сохранение/загрузка на диск.
 * После cache_put-ов вызывай cache_sort(), тогда cache_get через bsearch.
 */

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

/* Сохраняет кэш в текстовый файл. Формат: <path>\t<weight>\n.
 * Возвращает 0 при успехе, -1 при ошибке. */
int cache_save(const char *filename);

/* Загружает кэш из файла (заменяет текущий). Возвращает 0 при успехе. */
int cache_load(const char *filename);

#endif
