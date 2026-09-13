#ifndef SCAN_H
#define SCAN_H

/*
 * Рекурсивный подсчёт размера директории + фоновое сканирование в потоке.
 *
 * Синхронный API:     scan_dir_weight()
 * Асинхронный API:    scan_start_background() / scan_is_running() / scan_finish()
 *
 * Внутри используется lstat (не идёт по симлинкам — защита от зацикливания).
 * Псевдо-ФС /proc, /sys, /dev, /run пропускаются.
 * Все найденные директории кэшируются через cache_put.
 */

typedef struct {
    long long files;      /* успешно прочитанных файлов */
    long long errors;     /* ошибок lstat/доступа */
    long long no_access;  /* директорий, которые не открылись */
} ScanStats;

void scan_init_stats(ScanStats *s);

/* Синхронный обход: блокирует вызывающий поток. */
long long scan_dir_weight(const char *path, ScanStats *stats);

/*
 * Асинхронное сканирование в фоновом потоке.
 * scan_start_background — запускает скан, возвращает 1 при успехе,
 *                         0 если скан уже идёт.
 * scan_is_running       — 1, пока фоновый поток работает.
 * scan_finish           — дожидается завершения, копирует статистику
 *                         в *out_stats, возвращает общий размер в байтах.
 *                         После вызова поток освобождён (join).
 */
int  scan_start_background(const char *path);
int  scan_is_running(void);
long long scan_finish(ScanStats *out_stats);

#endif
