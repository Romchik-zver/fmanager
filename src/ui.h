#ifndef UI_H
#define UI_H

/*
 * TUI на ncurses: две панели, навигация, файловые операции.
 * Использование:
 *   ui_init();
 *   int rc = ui_run();
 *   ui_cleanup();
 */

void ui_init(void);
void ui_cleanup(void);
int  ui_run(void);

#endif
