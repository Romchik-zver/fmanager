#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <locale.h>
#include <ncurses.h>

#ifndef FMANAGER_VERSION
#define FMANAGER_VERSION "unknown"
#endif

#include "ui.h"
#include "cache.h"

static void sigint_handler(int sig)
{
    (void)sig;
    endwin();
    free_cache();
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("fmanager %s\n", FMANAGER_VERSION);
        return 0;
    }
    setlocale(LC_ALL, "");

    signal(SIGINT, sigint_handler);

    ui_init();
    int rc = ui_run();
    ui_cleanup();
    free_cache();

    return rc;
}
