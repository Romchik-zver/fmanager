#ifndef SCAN_H
#define SCAN_H

typedef struct {
    long long files;
    long long errors;
    long long no_access;
} ScanStats;

void scan_init_stats(ScanStats *s);

long long scan_dir_weight(const char *path, ScanStats *stats);

int  scan_start_background(const char *path);
int  scan_is_running(void);
long long scan_finish(ScanStats *out_stats);

#endif
