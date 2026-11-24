/*
 * Strata is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Strata is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Strata.  If not, see <https://www.gnu.org/licenses/>.
 */
/* Copyright (c) 2026 Logic Magicians Software */

/*
 * log.c - see log.h.  The messages go to the socket of the collector of
 * the run (see logrun.c) and nowhere else.
 */
#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

/* The longest message that is sent; a longer one is cut. */
#define LOG_MAX 1400

static char program[32] = "strata";
static int sock = -1;
static int tried;


void
log_open(const char *name)
{
    size_t n = strlen(name);

    if (n >= sizeof program) {
        n = sizeof program - 1;
    }

    memcpy(program, name, n);
    program[n] = '\0';
    log_close();
}


void
log_close(void)
{
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }

    tried = 0;
}


/* Connects to the socket that str made for the run, if there is one. */
static void
connect_socket(void)
{
    const char *path = getenv(LOG_SOCKET_ENV);
    union {
        struct sockaddr sa;
        struct sockaddr_un un;
    } addr;
    int fd;

    tried = 1;

    if (path == NULL || path[0] == '\0') {
        return;
    }

    if (strlen(path) >= sizeof addr.un.sun_path) {
        return;
    }

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (fd < 0) {
        return;
    }

    memset(&addr, 0, sizeof addr);
    addr.un.sun_family = AF_UNIX;
    memcpy(addr.un.sun_path, path, strlen(path));
    socklen_t len = sizeof addr;

    /*
     * A name that begins with @ is a name in the abstract namespace of Linux:
     * it is not a file, so it has no directory and the length of a path does
     * not matter.  The tests use it.  The address is as long as the name.
     */
    if (path[0] == '@') {
        addr.un.sun_path[0] = '\0';
        len = sizeof addr.un.sun_family;

        for (const char *c = path; *c != '\0'; c++) {
            len++;
        }
    }

    if (connect(fd, &addr.sa, len) != 0) {
        close(fd);

        return;
    }

    sock = fd;
}


int
log_run_active(void)
{
    if (sock < 0 && tried == 0) {
        connect_socket();
    }

    return sock >= 0 ? 1 : 0;
}


static void
send_message(int level, const char *file, int line, const char *fmt,
             va_list ap)
{
    char buf[LOG_MAX + 1];
    long pid = getpid();
    int n;
    int m;

    if (strata_release != 0 && level > LOG_LEVEL_WARN) {
        return;
    }

    if (sock < 0 && tried == 0) {
        connect_socket();
    }

    if (sock < 0) {
        return;
    }

    n = snprintf(buf, sizeof buf, "<%d>%s[%ld]: %s:%d: ", level, program,
                 pid, file, line);

    if (n < 0 || n >= LOG_MAX) {
        return;
    }

    m = vsnprintf(buf + n, sizeof buf - (size_t) n, fmt, ap);

    if (m < 0) {
        return;
    }

    if (m >= LOG_MAX - n) {
        m = LOG_MAX - n - 1;
    }

    while (send(sock, buf, (size_t) (n + m), MSG_NOSIGNAL) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
}


void
log_write(int level, const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    send_message(level, file, line, fmt, ap);
    va_end(ap);
}


void
log_die(int status, const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    send_message(LOG_LEVEL_FATAL, file, line, fmt, ap);
    va_end(ap);
    exit(status);
}
