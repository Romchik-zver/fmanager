#define _POSIX_C_SOURCE 200809L

#include "dialogs.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ncurses.h>

void draw_border(WINDOW *win)
{
    int h, w;
    getmaxyx(win, h, w);
    if (h < 2 || w < 2) return;

    mvwaddstr(win, 0, 0, "┌");
    for (int i = 1; i < w - 1; i++) waddstr(win, "─");
    waddstr(win, "┐");

    mvwaddstr(win, h - 1, 0, "└");
    for (int i = 1; i < w - 1; i++) waddstr(win, "─");
    waddstr(win, "┘");

    for (int i = 1; i < h - 1; i++) {
        mvwaddstr(win, i, 0, "│");
        mvwaddstr(win, i, w - 1, "│");
    }
}

int prompt_input(const char *title, char *out, size_t outsz)
{
    int h = 5;
    int w = COLS - 10;
    if (w < 40) w = COLS - 2;
    if (w < 20) w = 20;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_border(win);
    mvwprintw(win, 0, 2, " %s ", title);
    mvwprintw(win, 2, 2, "> ");
    wrefresh(win);

    curs_set(1);
    wmove(win, 2, 4);

    int i = 0;
    out[0] = '\0';

    while (i < (int)outsz - 1) {
        int c = wgetch(win);

        if (c == 27 || c == '\n' || c == '\r' || c == KEY_ENTER) break;

        if (c == KEY_BACKSPACE || c == 127 || c == 8) {
            if (i > 0) {
                i--;
                mvwaddch(win, 2, 4 + i, ' ');
                wmove(win, 2, 4 + i);
                wrefresh(win);
            }
            continue;
        }

        if (c >= 32 && c < 127) {
            out[i++] = (char)c;
            waddch(win, c);
            wrefresh(win);
        }
    }
    out[i] = '\0';

    curs_set(0);

    delwin(win);
    touchwin(stdscr);
    refresh();

    return (strlen(out) > 0) ? 0 : -1;
}

static void expand_tab(char *buf, size_t bufsz, const char *base_dir)
{
    char *last_slash = strrchr(buf, '/');

    char dir_part[4096] = "";
    const char *base_part;

    if (last_slash) {
        size_t dlen = (size_t)(last_slash - buf) + 1;
        if (dlen >= sizeof(dir_part)) return;
        memcpy(dir_part, buf, dlen);
        dir_part[dlen] = '\0';
        base_part = last_slash + 1;
    } else {
        base_part = buf;
    }

    char absdir[4096];
    if (dir_part[0] == '\0') {
        snprintf(absdir, sizeof(absdir), "%s", base_dir ? base_dir : ".");
    } else if (dir_part[0] == '/') {
        snprintf(absdir, sizeof(absdir), "%s", dir_part);
    } else if (dir_part[0] == '~' && (dir_part[1] == '/' || dir_part[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            if (dir_part[1] == '/')
                snprintf(absdir, sizeof(absdir), "%s%s", home, dir_part + 1);
            else
                snprintf(absdir, sizeof(absdir), "%s", home);
        } else {
            snprintf(absdir, sizeof(absdir), "%s", dir_part);
        }
    } else {
        const char *bd = base_dir ? base_dir : ".";
        size_t bdlen = strlen(bd);
        if (bdlen > 0 && bd[bdlen - 1] == '/')
            snprintf(absdir, sizeof(absdir), "%s%s", bd, dir_part);
        else
            snprintf(absdir, sizeof(absdir), "%s/%s", bd, dir_part);
    }

    DIR *d = opendir(absdir);
    if (!d) return;

    size_t blen = strlen(base_part);
    char common[1024] = "";
    int matches = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) continue;
        if (strncmp(de->d_name, base_part, blen) != 0) continue;

        if (matches == 0) {
            snprintf(common, sizeof(common), "%s", de->d_name);
        } else {
            size_t i = 0;
            while (common[i] && de->d_name[i] && common[i] == de->d_name[i]) i++;
            common[i] = '\0';
        }
        matches++;
    }
    closedir(d);

    if (matches == 0) return;

    if (matches == 1) {
        char fullpath[4096];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", absdir, common);
        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            size_t cl = strlen(common);
            if (cl + 1 < sizeof(common)) {
                common[cl] = '/';
                common[cl + 1] = '\0';
            }
        }
    }

    snprintf(buf, bufsz, "%s%s", dir_part, common);
}

int prompt_path_input(const char *title, const char *base_dir,
                      char *out, size_t outsz)
{
    int h = 5;
    int w = COLS - 10;
    if (w < 40) w = COLS - 2;
    if (w < 20) w = 20;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_border(win);
    mvwprintw(win, 0, 2, " %s ", title);
    mvwprintw(win, 2, 2, "> ");
    mvwprintw(win, 4, 2, "Tab = autocomplete   Enter = confirm   ESC = cancel");
    wrefresh(win);

    curs_set(1);
    wmove(win, 2, 4);

    int i = 0;
    out[0] = '\0';

    while (i < (int)outsz - 1) {
        int c = wgetch(win);

        if (c == 27 || c == '\n' || c == '\r' || c == KEY_ENTER) break;

        if (c == KEY_BACKSPACE || c == 127 || c == 8) {
            if (i > 0) {
                i--;
                mvwaddch(win, 2, 4 + i, ' ');
                wmove(win, 2, 4 + i);
                wrefresh(win);
            }
            continue;
        }

        if (c == '\t') {
            out[i] = '\0';
            expand_tab(out, outsz, base_dir);
            i = (int)strlen(out);

            int max_cols = w - 5;
            wmove(win, 2, 4);
            for (int k = 0; k < max_cols; k++) waddch(win, ' ');
            wmove(win, 2, 4);
            for (int k = 0; k < i && k < max_cols; k++)
                waddch(win, (unsigned char)out[k]);
            wrefresh(win);
            continue;
        }

        if (c >= 32 && c < 127) {
            out[i++] = (char)c;
            waddch(win, c);
            wrefresh(win);
        }
    }
    out[i] = '\0';

    curs_set(0);

    delwin(win);
    touchwin(stdscr);
    refresh();

    return (strlen(out) > 0) ? 0 : -1;
}

int confirm_dialog(const char *question)
{
    int h = 5;
    int w = 60;
    if (w > COLS - 4) w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_border(win);
    mvwprintw(win, 0, 2, " Confirm ");
    mvwprintw(win, 2, 2, "%.*s (y/n)", w - 6, question);
    wrefresh(win);

    int ans = 0;
    int ch;
    while ((ch = wgetch(win)) != ERR) {
        if (ch == 'y' || ch == 'Y') { ans = 1; break; }
        if (ch == 'n' || ch == 'N' || ch == 27) { ans = 0; break; }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return ans;
}

void message_dialog(const char *msg)
{
    int h = 5;
    int w = 60;
    if (w > COLS - 4) w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_border(win);
    mvwprintw(win, 0, 2, " Info ");
    mvwprintw(win, 2, 2, "%.*s", w - 6, msg);
    mvwprintw(win, h - 2, 2, "press any key...");
    wrefresh(win);
    wgetch(win);

    delwin(win);
    touchwin(stdscr);
    refresh();
}

static void draw_help_line(WINDOW *win, int row, const char *line, int max_w)
{
    if (max_w <= 0) return;

    int col = 2;
    const char *p = line;

    if (*p && col < max_w) {
        wattron(win, A_BOLD | COLOR_PAIR(TH_KEY));
        mvwaddch(win, row, col, (unsigned char)*p);
        wattroff(win, A_BOLD | COLOR_PAIR(TH_KEY));
        col++;
        p++;
    }

    while (*p && col < max_w) {
        mvwaddch(win, row, col, (unsigned char)*p);
        col++;
        p++;
    }
}

void show_help(const char *const *lines, int n)
{
    int h = n + 4;
    int w = 72;
    if (w > COLS - 4) w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);
    draw_border(win);
    mvwprintw(win, 0, 2, " Help ");
    for (int i = 0; i < n; i++) {
        draw_help_line(win, i + 2, lines[i], w - 4);
    }
    mvwprintw(win, h - 2, 2, "press any key...");
    wrefresh(win);
    wgetch(win);

    delwin(win);
    touchwin(stdscr);
    refresh();
}
