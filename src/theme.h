#ifndef THEME_H
#define THEME_H

/*
 * Цветовая схема. Значения читаются из ~/.config/fmanager/fmanager.conf
 * при старте. Если файла нет — создаётся автоматически.
 *
 * Ключи конфига:
 *   dir_fg / dir_bg            — папки
 *   link_fg / link_bg          — симлинки
 *   exec_fg / exec_bg          — исполняемые файлы
 *   status_fg / status_bg      — строка статуса
 *   header_fg / header_bg      — заголовок панели
 *   key_fg / key_bg            — первая буква команды в подсказках
 *   syn_key_fg  — ключевые слова в подсветке синтаксиса
 *   syn_str_fg  — строковые литералы
 *   syn_com_fg  — комментарии
 *
 * Цвета: black, red, green, yellow, blue, magenta, cyan, white, default.
 */

enum {
    TH_DIR = 1,
    TH_LINK,
    TH_EXEC,
    TH_STATUS,
    TH_HEADER,
    TH_KEY,
    TH_SYN_KEY,   /* ключевые слова подсветки синтаксиса */
    TH_SYN_STR,   /* "строки" */
    TH_SYN_COM,   /* # комментарии */
};

void theme_init(void);

#endif
