#define _POSIX_C_SOURCE 200809L

#include "ui.h"
#include "fs.h"
#include "scan.h"
#include "cache.h"
#include "dialogs.h"
#include "viewer.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ncurses.h>

#define UI_PATH_MAX 4096

typedef struct {
    char path[UI_PATH_MAX];
    Entry *entries;
    size_t count;
    int cursor;
    int scroll;
    unsigned char *marked;
    WINDOW *win;
} Panel;

static Panel panel;
static ScanStats stats;
static int running = 1;
static int scan_in_progress = 0;
static char cache_file_path[1024] = "";
static char state_file_path[1024] = "";
typedef enum { SORT_NAME, SORT_SIZE, SORT_TIME, SORT_EXT, SORT_COUNT } SortMode;
static SortMode sort_mode = SORT_NAME;
static const char *sort_mode_name(void);

typedef struct {
    char **items;
    int count;
    int cap;
} FoundList;

static void found_init(FoundList *fl) { fl->items = NULL; fl->count = 0; fl->cap = 0; }

static void found_free(FoundList *fl)
{
    for (int i = 0; i < fl->count; i++) free(fl->items[i]);
    free(fl->items);
    fl->items = NULL;
    fl->count = 0;
    fl->cap = 0;
}

static void found_add(FoundList *fl, const char *path)
{
    if (fl->count >= fl->cap) {
        fl->cap = fl->cap ? fl->cap * 2 : 64;
        char **tmp = realloc(fl->items, (size_t)fl->cap * sizeof(char *));
        if (!tmp) return;
        fl->items = tmp;
    }
    fl->items[fl->count++] = strdup(path);
}

static int is_skip_path(const char *path)
{
    static const char *skip[] = { "/proc", "/sys", "/dev", "/run", NULL };
    for (int i = 0; skip[i]; i++) {
        size_t l = strlen(skip[i]);
        if (strncmp(path, skip[i], l) == 0 &&
            (path[l] == '\0' || path[l] == '/'))
            return 1;
    }
    return 0;
}

static char type_char(EntryType t)
{
    switch (t) {
        case ENTRY_DIR:  return '/';
        case ENTRY_LINK: return '@';
        case ENTRY_EXEC: return '*';
        default:         return ' ';
    }
}

static void human_size(long long bytes, char *buf, size_t bufsz)
{
    if (bytes < 1024)                        snprintf(buf, bufsz, "%lld B",  bytes);
    else if (bytes < 1024LL * 1024)          snprintf(buf, bufsz, "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024LL * 1024 * 1024)   snprintf(buf, bufsz, "%.1f MB", bytes / (1024.0 * 1024.0));
    else                                     snprintf(buf, bufsz, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

static void full_path(const Panel *p, const char *name, char *out, size_t outsz)
{
    size_t len = strlen(p->path);
    int n;
    if (len > 0 && p->path[len - 1] == '/')
        n = snprintf(out, outsz, "%s%s", p->path, name);
    else
        n = snprintf(out, outsz, "%s/%s", p->path, name);
    if (n >= (int)outsz) out[outsz - 1] = '\0';
}

static void join_path(const char *dir, const char *name, char *out, size_t outsz)
{
    size_t dlen = strlen(dir);
    if (dlen > 0 && dir[dlen - 1] == '/')
        snprintf(out, outsz, "%s%s", dir, name);
    else
        snprintf(out, outsz, "%s/%s", dir, name);
}

static void user_path_to_abs(const char *input, char *out, size_t outsz)
{
    if (!input || !*input) {
        snprintf(out, outsz, "%s", panel.path);
        return;
    }

    if (input[0] == '/') {
        snprintf(out, outsz, "%s", input);
    } else if (input[0] == '~' && (input[1] == '\0' || input[1] == '/')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            if (input[1] == '/')
                snprintf(out, outsz, "%s%s", home, input + 1);
            else
                snprintf(out, outsz, "%s", home);
        } else {
            snprintf(out, outsz, "%s", input);
        }
    } else {
        join_path(panel.path, input, out, outsz);
    }

    normalize_path(out);
}

static int contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl) return 1;
    }
    return 0;
}

static int match_mask(const char *name, const char *mask)
{
    if (*mask == '\0') return *name == '\0';
    if (*mask == '*') {
        while (*mask == '*') mask++;
        if (*mask == '\0') return 1;
        while (*name) {
            if (match_mask(name, mask)) return 1;
            name++;
        }
        return match_mask(name, mask);
    }
    if (*name == '\0') return 0;
    if (*mask == '?') return match_mask(name + 1, mask + 1);
    if (tolower((unsigned char)*name) == tolower((unsigned char)*mask))
        return match_mask(name + 1, mask + 1);
    return 0;
}

static int has_mask(const char *s)
{
    for (; *s; s++) if (*s == '*' || *s == '?') return 1;
    return 0;
}

static void find_walk(const char *dir, const char *query, int use_mask,
                      FoundList *out, int max, int depth)
{
    if (depth > 32 || out->count >= max) return;
    if (is_skip_path(dir)) return;

    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) continue;

        char full[UI_PATH_MAX];
        join_path(dir, de->d_name, full, sizeof(full));

        int hit = use_mask ? match_mask(de->d_name, query)
                           : contains_ci(de->d_name, query);
        if (hit) found_add(out, full);
        if (out->count >= max) break;

        struct stat st;
        if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode))
            find_walk(full, query, use_mask, out, max, depth + 1);
    }
    closedir(d);
}

static int pick_from_list(const char *title, FoundList *fl, const char *base)
{
    int h = LINES - 4;
    if (h < 6) h = 6;
    int w = COLS - 4;
    if (w < 40) w = 40;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_border(win);

    int cursor = 0;
    int scroll = 0;
    int list_h = h - 2;
    size_t base_len = base ? strlen(base) : 0;

    while (1) {
        werase(win);
        draw_border(win);

        char hdr[256];
        snprintf(hdr, sizeof(hdr), " %s (%d) ", title, fl->count);
        mvwprintw(win, 0, 2, "%s", hdr);

        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + list_h) scroll = cursor - list_h + 1;

        for (int i = 0; i < list_h; i++) {
            int idx = scroll + i;
            if (idx >= fl->count) break;
            const char *full = fl->items[idx];

            const char *shown = full;
            if (base_len > 0 && strncmp(full, base, base_len) == 0) {
                const char *p = full + base_len;
                while (*p == '/') p++;
                if (*p) shown = p;
            }

            int sel = (idx == cursor);
            if (sel) wattron(win, A_REVERSE);
            mvwprintw(win, i + 1, 1, " %-.*s", w - 3, shown);
            if (sel) wattroff(win, A_REVERSE);
        }

        mvwprintw(win, h - 1, 2, "[Enter] go  [Esc] cancel");
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 27) { cursor = -1; break; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) break;
        if (ch == KEY_UP && cursor > 0) cursor--;
        else if (ch == KEY_DOWN && cursor < fl->count - 1) cursor++;
        else if (ch == KEY_PPAGE) { cursor -= list_h; if (cursor < 0) cursor = 0; }
        else if (ch == KEY_NPAGE) { cursor += list_h; if (cursor >= fl->count) cursor = fl->count - 1; }
        else if (ch == KEY_HOME) cursor = 0;
        else if (ch == KEY_END) cursor = fl->count - 1;
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return cursor;
}

static void marked_alloc(Panel *p)
{
    free(p->marked);
    p->marked = NULL;
    if (p->count > 0) {
        p->marked = calloc(p->count, 1);
    }
}

static int marked_count(const Panel *p)
{
    int n = 0;
    for (size_t i = 0; i < p->count; i++) if (p->marked && p->marked[i]) n++;
    return n;
}

static void marked_clear(Panel *p)
{
    if (p->marked) memset(p->marked, 0, p->count);
}

static int state_load(char *out_path, size_t outsz, int *out_cursor)
{
    if (!state_file_path[0]) return 0;
    FILE *f = fopen(state_file_path, "r");
    if (!f) return 0;

    char path[UI_PATH_MAX] = "";
    int cursor = 0;

    if (fgets(path, sizeof(path), f)) {
        size_t n = strlen(path);
        while (n > 0 && (path[n-1] == '\n' || path[n-1] == '\r')) path[--n] = '\0';
        if (n > 0 && fscanf(f, "%d", &cursor) == 1) {
            fclose(f);
            snprintf(out_path, outsz, "%s", path);
            *out_cursor = cursor;
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void state_save(const char *path, int cursor)
{
    if (!state_file_path[0]) return;
    FILE *f = fopen(state_file_path, "w");
    if (!f) return;
    fprintf(f, "%s\n%d\n", path, cursor);
    fclose(f);
}

static void free_panel(Panel *p)
{
    fs_free_entries(p->entries, p->count);
    p->entries = NULL;
    p->count = 0;
    p->cursor = 0;
    p->scroll = 0;
    free(p->marked);
    p->marked = NULL;
}

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = (const Entry *)a;
    const Entry *y = (const Entry *)b;

    int xd = (x->type == ENTRY_DIR);
    int yd = (y->type == ENTRY_DIR);
    if (xd != yd) return yd - xd;

    switch (sort_mode) {
        case SORT_SIZE:
            if (x->size != y->size) return (x->size < y->size) ? 1 : -1;
            break;

        case SORT_TIME:
            if (x->mtime != y->mtime) return (x->mtime < y->mtime) ? 1 : -1;
            break;

        case SORT_EXT: {
            const char *xe = strrchr(x->name, '.');
            const char *ye = strrchr(y->name, '.');
            xe = xe ? xe + 1 : "";
            ye = ye ? ye + 1 : "";
            int c = strcmp(xe, ye);
            if (c) return c;
            break;
        }

        case SORT_NAME:
        default:
            break;
    }
    return strcmp(x->name, y->name);
}

static void sort_entries(Entry *e, size_t n)
{
    if (n > 1) qsort(e, n, sizeof(Entry), entry_cmp);
}

static void load_panel(Panel *p)
{
    free_panel(p);
    if (fs_list_dir(p->path, &p->entries, &p->count) != 0) {
        p->entries = NULL;
        p->count = 0;
        return;
    }

    for (size_t i = 0; i < p->count; i++) {
        if (p->entries[i].type == ENTRY_DIR) {
            char full[UI_PATH_MAX];
            full_path(p, p->entries[i].name, full, sizeof(full));
            long long cached = cache_get(full);
            p->entries[i].size = (cached >= 0) ? cached : 0;
        }
    }

    sort_entries(p->entries, p->count);
    p->cursor = 0;
    p->scroll = 0;
    marked_alloc(p);
}

static void reload_panel_keep_cursor(Panel *p)
{
    int old_cursor = p->cursor;
    load_panel(p);
    if (p->count == 0) { p->cursor = 0; return; }
    if (old_cursor >= (int)p->count) old_cursor = (int)p->count - 1;
    if (old_cursor < 0) old_cursor = 0;
    p->cursor = old_cursor;
}

static void enter_dir(Panel *p, const char *name)
{
    size_t len = strlen(p->path);
    if (len > 0 && p->path[len - 1] == '/')
        snprintf(p->path + len, sizeof(p->path) - len, "%s", name);
    else
        snprintf(p->path + len, sizeof(p->path) - len, "/%s", name);
    normalize_path(p->path);
    load_panel(p);
}

static void go_up(Panel *p)
{
    char *last = strrchr(p->path, '/');
    if (!last) return;
    if (last == p->path) {
        p->path[1] = '\0';
    } else {
        *last = '\0';
    }
    load_panel(p);
}

static Entry *current_entry(void);
static void draw_panel(Panel *p)
{
    werase(p->win);
    int h, w;
    getmaxyx(p->win, h, w);

    int max_rows = h - 2;
    if (max_rows < 1) max_rows = 1;

    if (p->cursor < p->scroll) p->scroll = p->cursor;
    if (p->cursor >= p->scroll + max_rows) p->scroll = p->cursor - max_rows + 1;

    for (int i = 0; i < max_rows; i++) {
        int idx = p->scroll + i;
        if (idx >= (int)p->count) break;

        Entry *e = &p->entries[idx];
        int selected = (idx == p->cursor);
        int is_marked = (p->marked && p->marked[idx]);

        if (selected) wattron(p->win, A_REVERSE);

        int color = 0;
        if      (e->type == ENTRY_DIR)  color = TH_DIR;
        else if (e->type == ENTRY_LINK) color = TH_LINK;
        else if (e->type == ENTRY_EXEC) color = TH_EXEC;
        if (color) wattron(p->win, COLOR_PAIR(color));

        char szbuf[32];
        if (e->type == ENTRY_DIR && e->size == 0) {
            snprintf(szbuf, sizeof(szbuf), "<dir>");
        } else {
            human_size(e->size, szbuf, sizeof(szbuf));
        }

        int name_w = w - 16;
        if (name_w < 5) name_w = 5;

        char prefix_ch = is_marked ? '+' : ' ';
        mvwprintw(p->win, i + 1, 1, "%c %-*.*s %10s %c",
                  prefix_ch, name_w, name_w, e->name, szbuf, type_char(e->type));

        if (color)    wattroff(p->win, COLOR_PAIR(color));
        if (selected) wattroff(p->win, A_REVERSE);
    }

    draw_border(p->win);

    wattron(p->win, A_BOLD | COLOR_PAIR(TH_HEADER));
    char title[UI_PATH_MAX + 32];
    int mc = marked_count(p);
    if (mc > 0)
        snprintf(title, sizeof(title), " %s  [%d selected] ", p->path, mc);
    else
        snprintf(title, sizeof(title), " %s ", p->path);
    int title_max = w - 2;
    if ((int)strlen(title) > title_max) title[title_max] = '\0';
    mvwprintw(p->win, 0, 1, "%s", title);
    wattroff(p->win, A_BOLD | COLOR_PAIR(TH_HEADER));

    wnoutrefresh(p->win);
}

static void draw_status(void)
{
    char buf[1024];

    if (scan_in_progress) {
        snprintf(buf, sizeof(buf),
                 " Scanning / ... please wait (UI is responsive, q to abort) ");
    } else {
        int pos = snprintf(buf, sizeof(buf),
                 " Files: %lld | Cache: %d | Errors: %lld | No access: %lld | Sort: %s ",
                 stats.files, cache.count, stats.errors, stats.no_access,
                 sort_mode_name());

        Entry *e = current_entry();
        if (e && pos > 0 && pos < (int)sizeof(buf) - 2) {
            char modebuf[12];
            fs_format_mode(e->mode, modebuf, sizeof(modebuf));

            if (e->type == ENTRY_LINK && e->link_target) {
                snprintf(buf + pos, sizeof(buf) - pos,
                         "| %s  %s -> %s", modebuf, e->name, e->link_target);
            } else {
                snprintf(buf + pos, sizeof(buf) - pos,
                         "| %s  %s", modebuf, e->name);
            }
        }
    }

    attron(COLOR_PAIR(TH_STATUS));
    mvhline(LINES - 2, 0, ' ', COLS);
    mvprintw(LINES - 2, 0, "%-.*s", COLS, buf);
    attroff(COLOR_PAIR(TH_STATUS));
}

static void draw_help(void)
{
    static const char *items[] = {
        "help", "search", "rename", "view", "edit",
        "copy", "move",   "newdir", "file", "delete",
        "scan", "perm", "quit", ":shell"
    };
    int n = (int)(sizeof(items) / sizeof(items[0]));

    mvhline(LINES - 1, 0, ' ', COLS);

    int col = 1;
    for (int i = 0; i < n; i++) {
        const char *p = items[i];
        if (*p && col < COLS) {
            attron(A_BOLD | COLOR_PAIR(TH_KEY));
            mvaddch(LINES - 1, col++, (unsigned char)*p);
            attroff(A_BOLD | COLOR_PAIR(TH_KEY));
            p++;
        }
        while (*p && col < COLS) {
            mvaddch(LINES - 1, col++, (unsigned char)*p);
            p++;
        }
        if (i < n - 1) {
            if (col < COLS) mvaddch(LINES - 1, col++, ' ');
            if (col < COLS) mvaddch(LINES - 1, col++, '|');
            if (col < COLS) mvaddch(LINES - 1, col++, ' ');
        }
    }
}

static void draw_all(void)
{
    draw_panel(&panel);
    draw_status();
    draw_help();
    doupdate();
}


static Entry *current_entry(void)
{
    if (panel.count == 0) return NULL;
    if (panel.cursor < 0 || panel.cursor >= (int)panel.count) return NULL;
    return &panel.entries[panel.cursor];
}

static int collect_targets(int *idxs, int max)
{
    int n = 0;
    for (size_t i = 0; i < panel.count && n < max; i++) {
        if (panel.marked && panel.marked[i]) idxs[n++] = (int)i;
    }
    if (n == 0) {
        if (panel.cursor >= 0 && panel.cursor < (int)panel.count) {
            idxs[n++] = panel.cursor;
        }
    }
    return n;
}

static void action_help(void)
{
    static const char *lines[] = {
        "help (this window)",
        "Space - mark/unmark entry (multi-select)",
        "u     - clear all marks",
        "p     - chmod (change permissions, octal input)",
        "/     - Search. Plain text = substring, or mask: *.c   ?",
        "r     - Rename selected entry",
        "v     - View file (built-in; q or ESC to exit)",
        "e     - Edit file (built-in; ESC saves and exits)",
        "c     - Copy to directory (marked or current)",
        "m     - Move to directory (marked or current)",
        "n     - New directory (mkdir)",
        "f     - New empty file (touch)",
        "d     - Delete (marked or current, with confirmation)",
        "s     - Scan / and fill cache (runs in background)",
        "S     - sort cycle: name -> size -> time -> ext",
        "q     - Quit",
        "Icons: / dir  @ symlink  * executable  + marked",
    };
    show_help(lines, (int)(sizeof(lines) / sizeof(lines[0])));
}

static void action_search(void)
{
    if (panel.count == 0 && !panel.path[0]) return;

    char query[256] = "";
    if (prompt_input("Search (recursive):", query, sizeof(query)) != 0) return;

    FoundList fl;
    found_init(&fl);
    int use_mask = has_mask(query);
    find_walk(panel.path, query, use_mask, &fl, 5000, 0);

    if (fl.count == 0) {
        message_dialog("Not found");
        found_free(&fl);
        return;
    }

    int pick = pick_from_list("Search results", &fl, panel.path);
    if (pick >= 0 && pick < fl.count) {
        const char *hit = fl.items[pick];
        const char *slash = strrchr(hit, '/');

        if (slash) {
            char dir[UI_PATH_MAX];
            size_t dlen = (size_t)(slash - hit);
            if (dlen == 0) {
                snprintf(dir, sizeof(dir), "/");
                slash++;
            } else {
                if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
                memcpy(dir, hit, dlen);
                dir[dlen] = '\0';
            }
            const char *name = slash;

            snprintf(panel.path, sizeof(panel.path), "%s", dir);
            load_panel(&panel);
            for (size_t i = 0; i < panel.count; i++) {
                if (strcmp(panel.entries[i].name, name) == 0) {
                    panel.cursor = (int)i;
                    break;
                }
            }
        }
    }
    found_free(&fl);
}

static void action_rename(void)
{
    Entry *e = current_entry();
    if (!e) return;

    char newname[UI_PATH_MAX] = "";
    if (prompt_input("Rename to (new name only):", newname, sizeof(newname)) != 0) return;

    char src[UI_PATH_MAX], dst[UI_PATH_MAX];
    full_path(&panel, e->name, src, sizeof(src));
    full_path(&panel, newname,  dst, sizeof(dst));

    if (fs_rename(src, dst) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "rename failed: %s", strerror(errno));
        message_dialog(msg);
    }
    reload_panel_keep_cursor(&panel);
}

static void action_view(void)
{
    Entry *e = current_entry();
    if (!e) return;
    if (e->type == ENTRY_DIR) { enter_dir(&panel, e->name); return; }

    char path[UI_PATH_MAX];
    full_path(&panel, e->name, path, sizeof(path));

    if (viewer_open(path, 0) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cannot open: %s", strerror(errno));
        message_dialog(msg);
    }
    clear();
    touchwin(stdscr);
    refresh();
}

static void action_edit(void)
{
    Entry *e = current_entry();
    if (!e || e->type == ENTRY_DIR) return;

    char path[UI_PATH_MAX];
    full_path(&panel, e->name, path, sizeof(path));

    viewer_open(path, 1);

    reload_panel_keep_cursor(&panel);
    clear();
    touchwin(stdscr);
    refresh();
}

static int overwrite_dialog(const char *name)
{
    int h = 7;
    int w = 60;
    if (w > COLS - 4) w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_border(win);
    mvwprintw(win, 0, 2, " Overwrite ");
    mvwprintw(win, 2, 2, "'%.*s' already exists", w - 6, name);
    mvwprintw(win, 4, 2, "[y] overwrite  [n] skip  [a] all  [c] cancel");
    wrefresh(win);

    int ans = 0;
    int ch;
    while ((ch = wgetch(win)) != ERR) {
        if (ch == 'y' || ch == 'Y') { ans =  1; break; }
        if (ch == 'n' || ch == 'N') { ans =  0; break; }
        if (ch == 'a' || ch == 'A') { ans =  2; break; }
        if (ch == 'c' || ch == 'C' || ch == 27) { ans = -1; break; }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return ans;
}

static void action_copy(void)
{
    int idxs[4096];
    int n = collect_targets(idxs, 4096);
    if (n == 0) return;

    char input[UI_PATH_MAX] = "";
    if (prompt_path_input("Copy to directory:", panel.path,
                          input, sizeof(input)) != 0) return;
    char destdir[UI_PATH_MAX];
    user_path_to_abs(input, destdir, sizeof(destdir));

    int fail = 0;
    int overwrite_all = 0;
    int cancelled = 0;

    for (int k = 0; k < n; k++) {
        Entry *e = &panel.entries[idxs[k]];
        char src[UI_PATH_MAX], dst[UI_PATH_MAX];
        full_path(&panel, e->name, src, sizeof(src));
        join_path(destdir, e->name, dst, sizeof(dst));

        struct stat st;
        if (stat(dst, &st) == 0) {
            int ans = overwrite_all ? 2 : overwrite_dialog(e->name);
            if (ans == -1) { cancelled = 1; break; }
            if (ans ==  0) continue;
            if (ans ==  2) overwrite_all = 1;
            fs_delete(dst);
        }

        if (fs_copy(src, dst) != 0) fail++;
    }

    if (fail || cancelled) {
        char msg[256];
        if (cancelled)
            snprintf(msg, sizeof(msg), "cancelled (%d done, %d failed)", n - fail, fail);
        else
            snprintf(msg, sizeof(msg), "copy failed for %d item(s): %s", fail, strerror(errno));
        message_dialog(msg);
    }
    reload_panel_keep_cursor(&panel);
}

static void action_move(void)
{
    int idxs[4096];
    int n = collect_targets(idxs, 4096);
    if (n == 0) return;

    char input[UI_PATH_MAX] = "";
    if (prompt_path_input("Move to directory:", panel.path,
                          input, sizeof(input)) != 0) return;
    char destdir[UI_PATH_MAX];
    user_path_to_abs(input, destdir, sizeof(destdir));

    int fail = 0;
    int overwrite_all = 0;
    int cancelled = 0;

    for (int k = 0; k < n; k++) {
        Entry *e = &panel.entries[idxs[k]];
        char src[UI_PATH_MAX], dst[UI_PATH_MAX];
        full_path(&panel, e->name, src, sizeof(src));
        join_path(destdir, e->name, dst, sizeof(dst));

        struct stat st;
        if (stat(dst, &st) == 0) {
            int ans = overwrite_all ? 2 : overwrite_dialog(e->name);
            if (ans == -1) { cancelled = 1; break; }
            if (ans ==  0) continue;
            if (ans ==  2) overwrite_all = 1;
            fs_delete(dst);
        }

        if (fs_move(src, dst) != 0) fail++;
    }

    if (fail || cancelled) {
        char msg[256];
        if (cancelled)
            snprintf(msg, sizeof(msg), "cancelled (%d done, %d failed)", n - fail, fail);
        else
            snprintf(msg, sizeof(msg), "move failed for %d item(s): %s", fail, strerror(errno));
        message_dialog(msg);
    }
    reload_panel_keep_cursor(&panel);
}

static void action_mkdir(void)
{
    char name[UI_PATH_MAX] = "";
    if (prompt_path_input("New directory name (or path):", panel.path,
                          name, sizeof(name)) != 0) return;

    char full[UI_PATH_MAX];
    user_path_to_abs(name, full, sizeof(full));

    if (fs_mkdir(full) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "mkdir failed: %s", strerror(errno));
        message_dialog(msg);
    }
    reload_panel_keep_cursor(&panel);
}

static void action_newfile(void)
{
    char name[UI_PATH_MAX] = "";
    if (prompt_path_input("New file name (or path):", panel.path,
                          name, sizeof(name)) != 0) return;

    char full[UI_PATH_MAX];
    user_path_to_abs(name, full, sizeof(full));

    if (fs_create_file(full) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "create file failed: %s", strerror(errno));
        message_dialog(msg);
    }
    reload_panel_keep_cursor(&panel);
}

static void action_delete(void)
{
    int idxs[4096];
    int n = collect_targets(idxs, 4096);
    if (n == 0) return;

    char q[256];
    if (n == 1)
        snprintf(q, sizeof(q), "Delete '%s'?", panel.entries[idxs[0]].name);
    else
        snprintf(q, sizeof(q), "Delete %d items?", n);
    if (!confirm_dialog(q)) return;

    int fail = 0;
    for (int k = 0; k < n; k++) {
        Entry *e = &panel.entries[idxs[k]];
        char full[UI_PATH_MAX];
        full_path(&panel, e->name, full, sizeof(full));
        if (fs_delete(full) != 0) fail++;
    }

    if (fail) {
        char msg[256];
        snprintf(msg, sizeof(msg), "delete failed for %d item(s): %s", fail, strerror(errno));
        message_dialog(msg);
    }
    reload_panel_keep_cursor(&panel);
}

static const char *sort_mode_name(void)
{
    switch (sort_mode) {
        case SORT_SIZE: return "size";
        case SORT_TIME: return "time";
        case SORT_EXT:  return "ext";
        default:        return "name";
    }
}

static void action_sort(void)
{
    char keep[256] = "";
    if (panel.cursor >= 0 && panel.cursor < (int)panel.count) {
        snprintf(keep, sizeof(keep), "%s", panel.entries[panel.cursor].name);
    }

    sort_mode = (SortMode)((sort_mode + 1) % SORT_COUNT);
    sort_entries(panel.entries, panel.count);

    if (keep[0]) {
        for (size_t i = 0; i < panel.count; i++) {
            if (strcmp(panel.entries[i].name, keep) == 0) {
                panel.cursor = (int)i;
                break;
            }
        }
    }
}

static void action_chmod(void)
{
    Entry *e = current_entry();
    if (!e) return;

    char modebuf[12];
    fs_format_mode(e->mode, modebuf, sizeof(modebuf));
    char cur_octal[8];
    snprintf(cur_octal, sizeof(cur_octal), "%03o", e->mode & 07777);

    char title[192];
    snprintf(title, sizeof(title), "chmod %s (%s = %s). New mode (octal):",
             e->name, cur_octal, modebuf);

    char input[32] = "";
    if (prompt_input(title, input, sizeof(input)) != 0) return;

    errno = 0;
    char *end = NULL;
    long mode_val = strtol(input, &end, 8);
    if (errno != 0 || end == input || *end != '\0' ||
        mode_val < 0 || mode_val > 07777) {
        message_dialog("Invalid mode. Expected octal 0..7777, e.g. 755");
        return;
    }

    char path[UI_PATH_MAX];
    full_path(&panel, e->name, path, sizeof(path));

    if (chmod(path, (mode_t)mode_val) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "chmod failed: %s", strerror(errno));
        message_dialog(msg);
    }
    reload_panel_keep_cursor(&panel);
}

static void action_scan(void)
{
    if (scan_in_progress) return;

    if (!scan_start_background("/")) {
        message_dialog("Scan already running");
        return;
    }
    scan_in_progress = 1;
}

static void action_shell(void)
{
    char cmd[1024] = "";
    if (prompt_input("Shell command:", cmd, sizeof(cmd)) != 0) return;

    def_prog_mode();
    endwin();

    char oldcwd[UI_PATH_MAX];
    int have_old = (getcwd(oldcwd, sizeof(oldcwd)) != NULL);

    if (chdir(panel.path) != 0) { }

    printf("\n$ %s\n", cmd);
    fflush(stdout);
    int rc = system(cmd);
    if (rc == -1) {
        perror("system");
    }

    printf("\n[press Enter to return]");
    fflush(stdout);

    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }

    if (have_old) {
        if (chdir(oldcwd) != 0) {  }
    }

    reset_prog_mode();
    clear();
    touchwin(stdscr);
    refresh();

    reload_panel_keep_cursor(&panel);
}

static void handle_key(int ch)
{
    switch (ch) {
        case 'q': case 'Q': running = 0; break;

        case 'h': case 'H': action_help();    break;
        case '/':           action_search();  break;
        case 'r': case 'R': action_rename();  break;
        case 'v': case 'V': action_view();    break;
        case 'e': case 'E': action_edit();    break;
        case 'c': case 'C': action_copy();    break;
        case 'm': case 'M': action_move();    break;
        case 'n': case 'N': action_mkdir();   break;
        case 'f': case 'F': action_newfile(); break;
        case 'd': case 'D': action_delete();  break;
        case 's':           action_scan();    break;
        case 'S':           action_sort();    break;
        case 'p': case 'P': action_chmod();   break;
        case 'u': case 'U': marked_clear(&panel); break;
        case ':':           action_shell();   break;

        case ' ':
            if (panel.count > 0 && panel.marked &&
                panel.cursor >= 0 && panel.cursor < (int)panel.count) {
                panel.marked[panel.cursor] ^= 1;
                if (panel.cursor < (int)panel.count - 1) panel.cursor++;
            }
            break;

        case KEY_UP:
            if (panel.cursor > 0) panel.cursor--;
            break;
        case KEY_DOWN:
            if (panel.cursor < (int)panel.count - 1) panel.cursor++;
            break;
        case KEY_LEFT:
            go_up(&panel);
            break;
        case KEY_RIGHT: {
            Entry *e = current_entry();
            if (e && e->type == ENTRY_DIR) enter_dir(&panel, e->name);
            break;
        }
        case KEY_HOME:
            panel.cursor = 0;
            break;
        case KEY_END:
            if (panel.count > 0) panel.cursor = (int)panel.count - 1;
            break;

        case '\n':
        case '\r':
        case KEY_ENTER: {
            Entry *e = current_entry();
            if (e) {
                if (e->type == ENTRY_DIR) enter_dir(&panel, e->name);
                else                      action_view();
            }
            break;
        }

        case KEY_BACKSPACE:
        case 127:
        case 8:
            go_up(&panel);
            break;

        case KEY_RESIZE: {
            if (panel.win) delwin(panel.win);
            int rows = LINES - 3;
            panel.win = newwin(rows, COLS, 0, 0);
            keypad(panel.win, TRUE);
            clear();
            break;
        }

        default:
            break;
    }
}

void ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    theme_init();

    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(cache_file_path, sizeof(cache_file_path),
                 "%s/.cache/fmanager.cache", home);
        cache_load(cache_file_path);

        snprintf(state_file_path, sizeof(state_file_path),
                 "%s/.cache/fmanager.state", home);
    }
}

void ui_cleanup(void)
{
    if (cache_file_path[0]) cache_save(cache_file_path);
    state_save(panel.path, panel.cursor);
    free_panel(&panel);
    if (panel.win) delwin(panel.win);
    endwin();
}

int ui_run(void)
{
    if (LINES < 8 || COLS < 40) {
        endwin();
        fprintf(stderr, "Terminal too small (need at least 40x8)\n");
        return 1;
    }

    int rows = LINES - 3;

    memset(&panel, 0, sizeof(panel));

    int saved_cursor = 0;
    if (!state_load(panel.path, sizeof(panel.path), &saved_cursor)) {
        if (!getcwd(panel.path, sizeof(panel.path))) {
            strncpy(panel.path, "/", sizeof(panel.path) - 1);
            panel.path[sizeof(panel.path) - 1] = '\0';
        }
    }

    panel.win = newwin(rows, COLS, 0, 0);
    keypad(panel.win, TRUE);

    load_panel(&panel);

    if (saved_cursor >= 0 && saved_cursor < (int)panel.count) {
        panel.cursor = saved_cursor;
    }

    scan_init_stats(&stats);

    timeout(100);

    while (running) {
        if (scan_in_progress && !scan_is_running()) {
            ScanStats result;
            long long bytes = scan_finish(&result);
            stats = result;
            scan_in_progress = 0;

            if (cache_file_path[0]) cache_save(cache_file_path);

            char hs[32];
            human_size(bytes, hs, sizeof(hs));

            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Scan complete. Total: %s, Files: %lld, Errors: %lld, No access: %lld",
                     hs, stats.files, stats.errors, stats.no_access);
            message_dialog(msg);

            reload_panel_keep_cursor(&panel);
        }

        draw_all();

        int ch = getch();
        if (ch != ERR) handle_key(ch);
    }

    if (scan_in_progress) scan_finish(&stats);
    return 0;
}
