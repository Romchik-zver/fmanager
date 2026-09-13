#define _POSIX_C_SOURCE 200809L

#include "opener.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int has_program(const char *name)
{
    const char *path = getenv("PATH");
    if (!path || !*path) return 0;

    char buf[4096];
    const char *p = path;
    while (1) {
        const char *end = strchr(p, ':');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len > 0 && len + strlen(name) + 2 < sizeof(buf)) {
            snprintf(buf, sizeof(buf), "%.*s/%s", (int)len, p, name);
            if (access(buf, X_OK) == 0) return 1;
        }

        if (!end) break;
        p = end + 1;
    }
    return 0;
}

int opener_open(const char *path)
{
    if (!path || !*path) return -1;

    const char *prog = NULL;
    const char *programs[] = {"xdg-open", "gio", "kde-open", "exo-open", NULL};
    for (int i = 0; programs[i]; i++) {
        if (has_program(programs[i])) {
            prog = programs[i];
            break;
        }
    }

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        setsid();

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }

        if (strcmp(prog, "gio") == 0)
            execlp(prog, prog, "open", path, (char *)NULL);
        else
            execlp(prog, prog, path, (char *)NULL);

        _exit(127);
    }

    return 0;
}
