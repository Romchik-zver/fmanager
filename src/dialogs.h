#ifndef DIALOGS_H
#define DIALOGS_H

#include <stddef.h>
#include <ncurses.h>

/* Рисует рамку UTF-8 символами (обход ACS ncurses). */
void draw_border(WINDOW *win);

/* Ввод одной строки. Возвращает 0 при успехе, -1 если пусто/отменено. */
int prompt_input(const char *title, char *out, size_t outsz);

int prompt_path_input(const char *title, const char *base_dir, char *out, size_t outsz);

/* Диалог подтверждения y/n. Возвращает 1 на "да", 0 иначе. */
int confirm_dialog(const char *question);

/* Простое сообщение с ожиданием любой клавиши. */
void message_dialog(const char *msg);

/* Окно справки со списком строк, ждёт клавишу. */
void show_help(const char *const *lines, int n);

#endif
