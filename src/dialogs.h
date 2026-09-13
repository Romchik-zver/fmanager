#ifndef DIALOGS_H
#define DIALOGS_H

#include <stddef.h>
#include <ncurses.h>

void draw_border(WINDOW *win);

int prompt_input(const char *title, char *out, size_t outsz);

int prompt_path_input(const char *title, const char *base_dir, char *out, size_t outsz);

int confirm_dialog(const char *question);

void message_dialog(const char *msg);

void show_help(const char *const *lines, int n);

#endif
