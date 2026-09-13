#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <locale.h>
#include <ncurses.h>

#include "ui.h"
#include "cache.h"

static void sigint_handler(int sig)
{
    (void)sig;
    endwin();
    free_cache();
    exit(0);
}

int main(void)
{
    setlocale(LC_ALL, "");

    signal(SIGINT, sigint_handler);

    ui_init();
    int rc = ui_run();
    ui_cleanup();
    free_cache();

    return rc;
}
