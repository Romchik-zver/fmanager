#define _POSIX_C_SOURCE 200809L

#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ncurses.h>

#define CFG_DIRNAME  ".config/fmanager"
#define CFG_FILENAME "fmanager.conf"

static int color_from_name(const char *name)
{
    if (!name) return -1;
    if (strcmp(name, "black")   == 0) return COLOR_BLACK;
    if (strcmp(name, "red")     == 0) return COLOR_RED;
    if (strcmp(name, "green")   == 0) return COLOR_GREEN;
    if (strcmp(name, "yellow")  == 0) return COLOR_YELLOW;
    if (strcmp(name, "blue")    == 0) return COLOR_BLUE;
    if (strcmp(name, "magenta") == 0) return COLOR_MAGENTA;
    if (strcmp(name, "cyan")    == 0) return COLOR_CYAN;
    if (strcmp(name, "white")   == 0) return COLOR_WHITE;
    return -1;
}

static void strip(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static void build_cfg_path(char *out, size_t outsz)
{
    out[0] = '\0';
    const char *home = getenv("HOME");
    if (!home || !*home) return;
    snprintf(out, outsz, "%s/%s/%s", home, CFG_DIRNAME, CFG_FILENAME);
}

static void ensure_dir(const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';

    if (mkdir(dir, 0755) == 0) return;
    if (errno != ENOENT) return;

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void write_default_config(const char *path)
{
    ensure_dir(path);

    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f,
        "# fmanager — конфигурация\n"
        "# Цвета: black red green yellow blue magenta cyan white default\n"
        "# Файл пересоздаётся автоматически, если его удалить.\n"
        "\n"
        "# Папки (directories)\n"
        "dir_fg=blue\n"
        "dir_bg=default\n"
        "\n"
        "# Символические ссылки (symlinks)\n"
        "link_fg=cyan\n"
        "link_bg=default\n"
        "\n"
        "# Исполняемые файлы (executables)\n"
        "exec_fg=green\n"
        "exec_bg=default\n"
        "\n"
        "# Строка статуса\n"
        "status_fg=black\n"
        "status_bg=white\n"
        "\n"
        "# Заголовок панели (текущий путь)\n"
        "header_fg=black\n"
        "header_bg=cyan\n"
        "\n"
        "# Подсветка клавиш в help и подсказках (первая буква команды)\n"
        "key_fg=red\n"
        "key_bg=default\n"
        "\n"
        "# Подсветка синтаксиса в просмотрщике (v)\n"
        "syn_key_fg=yellow\n"
        "syn_str_fg=green\n"
        "syn_com_fg=cyan\n"
    );
    fclose(f);
}

void theme_init(void)
{
    if (!has_colors()) return;

    start_color();
    use_default_colors();

    int dir_fg = COLOR_BLUE,  dir_bg = -1;
    int lnk_fg = COLOR_CYAN,  lnk_bg = -1;
    int exe_fg = COLOR_GREEN, exe_bg = -1;
    int st_fg  = COLOR_BLACK, st_bg  = COLOR_WHITE;
    int hd_fg  = COLOR_BLACK, hd_bg  = COLOR_CYAN;
    int key_fg = COLOR_RED,   key_bg = -1;
    int syk_fg = COLOR_YELLOW;
    int sys_fg = COLOR_GREEN;
    int syc_fg = COLOR_CYAN;

    char cfg_path[1024];
    build_cfg_path(cfg_path, sizeof(cfg_path));

    if (cfg_path[0]) {
        FILE *probe = fopen(cfg_path, "r");
        if (!probe) write_default_config(cfg_path);
        else fclose(probe);

        FILE *f = fopen(cfg_path, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char *p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '#' || *p == '\n' || *p == '\0') continue;

                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq = '\0';
                char *key = line;
                char *val = eq + 1;
                strip(key);
                strip(val);

                int c;
                if      (strcmp(key, "dir_fg")     == 0) { c = color_from_name(val); if (c >= 0) dir_fg = c; }
                else if (strcmp(key, "dir_bg")     == 0) { c = color_from_name(val); if (c >= 0) dir_bg = c; }
                else if (strcmp(key, "link_fg")    == 0) { c = color_from_name(val); if (c >= 0) lnk_fg = c; }
                else if (strcmp(key, "link_bg")    == 0) { c = color_from_name(val); if (c >= 0) lnk_bg = c; }
                else if (strcmp(key, "exec_fg")    == 0) { c = color_from_name(val); if (c >= 0) exe_fg = c; }
                else if (strcmp(key, "exec_bg")    == 0) { c = color_from_name(val); if (c >= 0) exe_bg = c; }
                else if (strcmp(key, "status_fg")  == 0) { c = color_from_name(val); if (c >= 0) st_fg  = c; }
                else if (strcmp(key, "status_bg")  == 0) { c = color_from_name(val); if (c >= 0) st_bg  = c; }
                else if (strcmp(key, "header_fg")  == 0) { c = color_from_name(val); if (c >= 0) hd_fg  = c; }
                else if (strcmp(key, "header_bg")  == 0) { c = color_from_name(val); if (c >= 0) hd_bg  = c; }
                else if (strcmp(key, "key_fg")     == 0) { c = color_from_name(val); if (c >= 0) key_fg = c; }
                else if (strcmp(key, "key_bg")     == 0) { c = color_from_name(val); if (c >= 0) key_bg = c; }
                else if (strcmp(key, "syn_key_fg") == 0) { c = color_from_name(val); if (c >= 0) syk_fg = c; }
                else if (strcmp(key, "syn_str_fg") == 0) { c = color_from_name(val); if (c >= 0) sys_fg = c; }
                else if (strcmp(key, "syn_com_fg") == 0) { c = color_from_name(val); if (c >= 0) syc_fg = c; }
            }
            fclose(f);
        }
    }

    init_pair(TH_DIR,     dir_fg, dir_bg);
    init_pair(TH_LINK,    lnk_fg, lnk_bg);
    init_pair(TH_EXEC,    exe_fg, exe_bg);
    init_pair(TH_STATUS,  st_fg,  st_bg);
    init_pair(TH_HEADER,  hd_fg,  hd_bg);
    init_pair(TH_KEY,     key_fg, key_bg);
    init_pair(TH_SYN_KEY, syk_fg, -1);
    init_pair(TH_SYN_STR, sys_fg, -1);
    init_pair(TH_SYN_COM, syc_fg, -1);
}
