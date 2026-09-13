#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "viewer.h"
#include "theme.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <locale.h>
#include <ncurses.h>


typedef enum { LANG_NONE, LANG_C, LANG_PY, LANG_SH } Lang;

static const char *kw_c[] = {
    "int","char","long","short","void","return","if","else","for","while",
    "do","break","continue","struct","enum","union","typedef","static",
    "const","extern","switch","case","default","sizeof","goto","unsigned",
    "signed","float","double","bool","true","false","NULL","wchar_t","size_t",
    NULL
};
static const char *kw_py[] = {
    "def","return","if","else","elif","for","while","in","not","and","or",
    "import","from","as","class","try","except","finally","with","lambda",
    "pass","break","continue","None","True","False","self","print","yield",
    NULL
};
static const char *kw_sh[] = {
    "if","then","else","fi","for","do","done","while","case","esac",
    "function","return","local","export","read","echo","cd","source",
    NULL
};

static Lang detect_lang(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return LANG_NONE;
    if (!strcmp(dot, ".c")   || !strcmp(dot, ".h")    ||
        !strcmp(dot, ".cpp") || !strcmp(dot, ".cc")   ||
        !strcmp(dot, ".hpp") || !strcmp(dot, ".rs")   ||
        !strcmp(dot, ".go")  || !strcmp(dot, ".js")   ||
        !strcmp(dot, ".ts")  || !strcmp(dot, ".java") ||
        !strcmp(dot, ".cs")) return LANG_C;
    if (!strcmp(dot, ".py")) return LANG_PY;
    if (!strcmp(dot, ".sh") || !strcmp(dot, ".bash")) return LANG_SH;
    return LANG_NONE;
}

static const char **lang_keywords(Lang l)
{
    switch (l) {
        case LANG_C:  return kw_c;
        case LANG_PY: return kw_py;
        case LANG_SH: return kw_sh;
        default:      return NULL;
    }
}

static int is_keyword(const char **kws, const char *word, size_t wlen)
{
    if (!kws) return 0;
    for (int i = 0; kws[i]; i++) {
        if (strlen(kws[i]) == wlen && strncmp(kws[i], word, wlen) == 0)
            return 1;
    }
    return 0;
}

static size_t utf8_char_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_char_width(const char *s, size_t clen)
{
    wchar_t wc = 0;
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    size_t rc = mbrtowc(&wc, s, clen, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0) return 1;
    int w = wcwidth(wc);
    return (w < 0) ? 1 : w;
}

static void render_line(int row, const char *line, int len, Lang lang, int max_cols)
{
    if (row < 0 || max_cols <= 1) return;

    int col = 0;
    size_t i = 0;

    if (lang == LANG_PY || lang == LANG_SH) {
        size_t j = 0;
        while (j < (size_t)len && (line[j] == ' ' || line[j] == '\t')) j++;
        if (j < (size_t)len && line[j] == '#') {
            attron(COLOR_PAIR(TH_SYN_COM));
            for (size_t k = 0; k < (size_t)len && col < max_cols; k++)
                mvaddch(row, col++, (unsigned char)line[k]);
            attroff(COLOR_PAIR(TH_SYN_COM));
            return;
        }
    }

    const char **kws = lang_keywords(lang);

    while (i < (size_t)len && col < max_cols) {
        unsigned char c = (unsigned char)line[i];

        if (lang == LANG_C && c == '/' && i + 1 < (size_t)len && line[i+1] == '/') {
            attron(COLOR_PAIR(TH_SYN_COM));
            while (i < (size_t)len && col < max_cols)
                mvaddch(row, col++, (unsigned char)line[i++]);
            attroff(COLOR_PAIR(TH_SYN_COM));
            break;
        }

        if (c == '"' || c == '\'') {
            char quote = (char)c;
            attron(COLOR_PAIR(TH_SYN_STR));
            if (col < max_cols) mvaddch(row, col++, c);
            i++;
            while (i < (size_t)len && col < max_cols) {
                mvaddch(row, col++, (unsigned char)line[i]);
                if (line[i] == quote) { i++; break; }
                if (line[i] == '\\' && i + 1 < (size_t)len) {
                    i++;
                    if (col < max_cols) mvaddch(row, col++, (unsigned char)line[i]);
                }
                i++;
            }
            attroff(COLOR_PAIR(TH_SYN_STR));
            continue;
        }

        if (isalpha(c) || c == '_') {
            size_t start = i;
            while (i < (size_t)len &&
                   (isalnum((unsigned char)line[i]) || line[i] == '_')) i++;
            size_t wlen = i - start;

            if (is_keyword(kws, line + start, wlen)) {
                attron(A_BOLD | COLOR_PAIR(TH_SYN_KEY));
                for (size_t k = start; k < i && col < max_cols; k++)
                    mvaddch(row, col++, (unsigned char)line[k]);
                attroff(A_BOLD | COLOR_PAIR(TH_SYN_KEY));
            } else {
                for (size_t k = start; k < i && col < max_cols; k++)
                    mvaddch(row, col++, (unsigned char)line[k]);
            }
            continue;
        }

        size_t clen = utf8_char_len(c);
        if (i + clen > (size_t)len) clen = 1;
        int cw = utf8_char_width(line + i, clen);

        if (col + cw <= max_cols) {
            char tmp[8];
            size_t copy = (clen > 7) ? 7 : clen;
            memcpy(tmp, line + i, copy);
            tmp[copy] = '\0';
            mvwaddstr(stdscr, row, col, tmp);
        }
        col += cw;
        i += clen;
    }
}


int viewer_open(const char *path, int editable)
{
    size_t cap = 1 << 20;
    char *buf = malloc(cap);
    if (!buf) return -1;
    buf[0] = '\0';
    size_t len = 0;

    FILE *f = fopen(path, "r");
    if (!f) { free(buf); return -1; }
    len = fread(buf, 1, cap - 1, f);
    buf[len] = '\0';
    fclose(f);

    Lang lang = detect_lang(path);

    int scroll_row = 0;
    int editing = 1;

    int total_rows = 1;
    for (size_t i = 0; i < len; i++) if (buf[i] == '\n') total_rows++;

    timeout(-1);

    while (editing) {
        int text_h = LINES - 3;
        if (text_h < 1) text_h = 1;

        if (editable) {
            total_rows = 1;
            for (size_t i = 0; i < len; i++) if (buf[i] == '\n') total_rows++;
        }

        /* Курсор (в editable) — всегда в конце буфера */
        int cursor_row = 0, cursor_col = 0;
        if (editable) {
            size_t j = 0;
            while (j < len) {
                if (buf[j] == '\n') { cursor_row++; cursor_col = 0; j++; continue; }
                if (buf[j] == '\t') {
                    int sp = 4 - (cursor_col % 4);
                    cursor_col += sp;
                    j++;
                    if (cursor_col >= COLS) { cursor_row++; cursor_col = 0; }
                    continue;
                }
                size_t clen = utf8_char_len((unsigned char)buf[j]);
                if (j + clen > len) clen = 1;
                int w = utf8_char_width(buf + j, clen);
                cursor_col += w;
                if (cursor_col >= COLS) { cursor_row++; cursor_col = 0; }
                j += clen;
            }
            if (cursor_row < scroll_row) scroll_row = cursor_row;
            if (cursor_row >= scroll_row + text_h) scroll_row = cursor_row - text_h + 1;
        }

        clear();

        {
            size_t line_start = 0;
            int cur_row = 0;
            int screen_row = 0;

            while (line_start <= len && screen_row < text_h) {
                size_t line_end = line_start;
                while (line_end < len && buf[line_end] != '\n') line_end++;

                if (cur_row >= scroll_row) {
                    render_line(screen_row,
                                buf + line_start,
                                (int)(line_end - line_start),
                                lang,
                                COLS);
                    screen_row++;
                }
                cur_row++;

                if (line_end >= len) break;
                line_start = line_end + 1;
            }
        }

        attron(A_REVERSE);
        mvhline(LINES - 2, 0, ' ', COLS);
        if (editable)
            mvprintw(LINES - 2, 0, " EDIT: %s | %zu bytes | ESC = save & exit ", path, len);
        else
            mvprintw(LINES - 2, 0, " VIEW: %s | %zu bytes | q or ESC = exit ", path, len);
        attroff(A_REVERSE);

        if (editable)
            mvprintw(LINES - 1, 0, " Type text | Enter = newline | Backspace = delete | ESC = save & quit");
        else
            mvprintw(LINES - 1, 0, " Arrows/PgUp/PgDn = scroll | q or ESC = return");

        if (editable) {
            int sy = cursor_row - scroll_row;
            if (sy < 0) sy = 0;
            if (sy >= text_h) sy = text_h - 1;
            move(sy, cursor_col);
        } else {
            move(LINES - 1, 0);
        }
        refresh();

        wint_t wch;
        int rc = get_wch(&wch);

        if (rc == ERR) continue;

        if (rc == KEY_CODE_YES) {
            int key = (int)wch;

            if (key == KEY_BACKSPACE) {
                if (editable && len > 0) {
                    len--;
                    while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80) len--;
                    buf[len] = '\0';
                }
                continue;
            }
            if (key == KEY_ENTER) {
                if (editable && len + 2 < cap) { buf[len++] = '\n'; buf[len] = '\0'; }
                continue;
            }
            if (!editable) {
                if (key == KEY_UP    && scroll_row > 0) scroll_row--;
                else if (key == KEY_DOWN) { if (scroll_row + text_h < total_rows) scroll_row++; }
                else if (key == KEY_PPAGE) { scroll_row -= text_h; if (scroll_row < 0) scroll_row = 0; }
                else if (key == KEY_NPAGE) {
                    scroll_row += text_h;
                    if (scroll_row > total_rows - text_h && total_rows > text_h)
                        scroll_row = total_rows - text_h;
                }
                else if (key == KEY_HOME) scroll_row = 0;
                else if (key == KEY_END)  {
                    scroll_row = total_rows - text_h;
                    if (scroll_row < 0) scroll_row = 0;
                }
            }
            continue;
        }

        if (wch == 27) { editing = 0; break; }

        if (!editable) {
            if (wch == 'q' || wch == 'Q') { editing = 0; break; }
            if (wch == KEY_UP)    { if (scroll_row > 0) scroll_row--; }
            else if (wch == KEY_DOWN) {
                if (scroll_row + text_h < total_rows) scroll_row++;
            }
            continue;
        }

        if (wch == '\n' || wch == '\r') {
            if (len + 2 < cap) { buf[len++] = '\n'; buf[len] = '\0'; }
            continue;
        }
        if (wch == KEY_BACKSPACE || wch == 127 || wch == 8) {
            if (len > 0) {
                len--;
                while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80) len--;
                buf[len] = '\0';
            }
            continue;
        }
        if (wch == '\t') {
            if (len + 2 < cap) { buf[len++] = '\t'; buf[len] = '\0'; }
            continue;
        }
        if (wch >= 32) {
            char mbs[MB_LEN_MAX];
            mbstate_t st;
            memset(&st, 0, sizeof(st));
            size_t n = wcrtomb(mbs, (wchar_t)wch, &st);
            if (n != (size_t)-1 && len + n + 1 < cap) {
                memcpy(buf + len, mbs, n);
                len += n;
                buf[len] = '\0';
            }
        }
    }

    timeout(100);

    if (editable) {
        FILE *out = fopen(path, "w");
        if (out) {
            fwrite(buf, 1, len, out);
            fclose(out);
        }
    }

    free(buf);
    return 0;
}
