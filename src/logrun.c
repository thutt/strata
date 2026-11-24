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
 * logrun.c - see logrun.h.  The collector is a child process, not a
 * thread: it has no state in common with the terminal handling and the
 * signals of str, it cannot be stopped by them, and when str ends in any
 * way (a crash included) the pipe that str holds is closed, which tells
 * the collector to write what is left and to end.
 */
#include "logrun.h"

#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The longest line that the collector writes. */
#define LINE_MAX_LEN 2048

static pid_t collector = -1;
static pid_t owner = -1;
static int endfd = -1;
static char logfile[PATH_MAX];
static char sockfile[PATH_MAX];


/*
 * Takes the socket file away.  A name that begins with @ is in the abstract
 * namespace of Linux: there is no file to take away.
 */
static void
socket_remove(void)
{
    if (sockfile[0] != '@') {
        unlink(sockfile);
    }
}


/* Makes the directory and the directories above it that are missing. */
static int
make_dirs(const char *path)
{
    char tmp[PATH_MAX];
    size_t n = strlen(path);

    if (n == 0 || n >= sizeof tmp) {
        return -1;
    }

    memcpy(tmp, path, n + 1);

    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';

            if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
                return -1;
            }

            tmp[i] = '/';
        }
    }

    if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}


/* True when name starts with prefix and ends with suffix. */
static int
has_affixes(const char *name, const char *prefix, const char *suffix)
{
    size_t n = strlen(name);
    size_t p = strlen(prefix);
    size_t s = strlen(suffix);

    if (n < p + s) {
        return 0;
    }

    return strncmp(name, prefix, p) == 0 &&
           strcmp(name + n - s, suffix) == 0;
}


/* The process number in a name like run-DATE-TIME-PID.sock, or -1. */
static long
socket_pid(const char *name)
{
    const char *dash = strrchr(name, '-');
    char *end = NULL;
    long v;

    if (dash == NULL) {
        return -1;
    }

    v = strtol(dash + 1, &end, 10);

    if (end == dash + 1 || strcmp(end, ".sock") != 0) {
        return -1;
    }

    return v;
}


static int
compare_names(const void *a, const void *b)
{
    const char *const *x = a;
    const char *const *y = b;

    return strcmp(*x, *y);
}


/*
 * Deletes the sockets of runs whose process is gone, and the logs of the
 * oldest runs so that at most keep - 1 remain: the run that starts now
 * makes the last one.
 */
static void
prune(const char *dir, size_t keep)
{
    DIR *d = opendir(dir);
    char **logs = NULL;
    size_t nlogs = 0;
    size_t cap = 0;
    const struct dirent *e;

    if (d == NULL) {
        return;
    }

    while ((e = readdir(d)) != NULL) {
        char path[PATH_MAX];

        if (has_affixes(e->d_name, "run-", ".sock")) {
            long pid = socket_pid(e->d_name);

            if (pid > 0 && kill((pid_t) pid, 0) != 0 && errno == ESRCH) {
                snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
                unlink(path);
            }
        } else if (has_affixes(e->d_name, "run-", ".log")) {
            if (nlogs == cap) {
                size_t ncap = cap == 0 ? 16 : cap * 2;
                char **grown = realloc(logs, ncap * sizeof *logs);

                if (grown == NULL) {
                    break;
                }

                logs = grown;
                cap = ncap;
            }

            logs[nlogs] = strdup(e->d_name);

            if (logs[nlogs] != NULL) {
                nlogs++;
            }
        }
    }

    closedir(d);

    if (nlogs > 1) {
        qsort(logs, nlogs, sizeof *logs, compare_names);
    }

    size_t keep_now = keep > 1 ? keep - 1 : 0;

    for (size_t i = 0; i < nlogs; i++) {
        if (nlogs - i > keep_now) {
            char path[PATH_MAX];

            snprintf(path, sizeof path, "%s/%s", dir, logs[i]);
            unlink(path);
        }

        free(logs[i]);
    }

    free(logs);
}


/* The names of the level in the lines of the log. */
static const char *
level_name(int level)
{
    if (level <= LOG_LEVEL_FATAL) {
        return "FATAL";
    }

    if (level == LOG_LEVEL_ERROR) {
        return "ERROR";
    }

    if (level == LOG_LEVEL_WARN) {
        return "WARN ";
    }

    if (level >= LOG_LEVEL_DEBUG) {
        return "DEBUG";
    }

    return "NOTE ";
}


/*
 * Makes one line of the log from one message, in the form
 *
 *     2026-11-14 21:05:33.123 ERROR stred[1234]: src/x.c:12: the text
 *
 * The control characters of the text become blanks, so that a message is
 * always one line.  Returns the length of the line.
 */
static size_t
make_line(char *out, size_t cap, const char *msg, size_t len)
{
    struct timespec ts;
    struct tm tm;
    int level = LOG_LEVEL_NOTE;
    size_t n;
    size_t i = 0;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm);
    n += (size_t) snprintf(out + n, cap - n, ".%03ld ",
                           ts.tv_nsec / 1000000);

    if (len > 3 && msg[0] == '<') {
        int v = 0;

        i = 1;

        while (i < len && msg[i] >= '0' && msg[i] <= '9' && v < 1000) {
            v = v * 10 + (msg[i] - '0');
            i++;
        }

        if (i < len && msg[i] == '>') {
            level = v & 7;
            i++;
        } else {
            i = 0;
        }
    }

    n += (size_t) snprintf(out + n, cap - n, "%s ", level_name(level));

    while (i < len && n + 2 < cap) {
        char c = msg[i++];

        /* A byte above 127 is negative where char is signed. */
        out[n++] = ((c >= 0 && c < 0x20) || c == 0x7f) ? ' ' : c;
    }

    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }

    out[n++] = '\n';

    return n;
}


/* Reads every message that is waiting in the socket and writes its line. */
static void
drain(int sock, int out)
{
    char msg[LINE_MAX_LEN];
    char line[LINE_MAX_LEN + 64];

    for (;;) {
        ssize_t len = recv(sock, msg, sizeof msg, MSG_DONTWAIT);

        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }

            return;
        }

        if (len > 0) {
            size_t n = make_line(line, sizeof line, msg, (size_t) len);
            ssize_t w = write(out, line, n);

            if (w < 0) {
                return;
            }
        }
    }
}


/* The body of the collector: write the messages until str is gone. */
static void
collect(int sock, int rd, int out)
{
    struct pollfd p[2];

    for (;;) {
        int r;
        int done = 0;

        p[0].fd = sock;
        p[0].events = POLLIN;
        p[0].revents = 0;
        p[1].fd = rd;
        p[1].events = POLLIN;
        p[1].revents = 0;
        r = poll(p, 2, -1);

        if (r < 0 && errno != EINTR) {
            break;
        }

        if (r > 0 && (p[0].revents & POLLIN) != 0) {
            drain(sock, out);
        }

        if (r > 0 && p[1].revents != 0) {
            char c;

            if (read(rd, &c, 1) <= 0) {
                done = 1;
            }
        }

        if (done != 0) {
            break;
        }
    }

    drain(sock, out);
}


/* Points the link current.log at the log file of this run. */
static void
point_link(const char *dir, const char *name)
{
    char link[PATH_MAX];
    char tmp[PATH_MAX];
    long pid = getpid();

    snprintf(link, sizeof link, "%s/current.log", dir);
    snprintf(tmp, sizeof tmp, "%s/current.log.%ld", dir, pid);
    unlink(tmp);

    if (symlink(name, tmp) != 0) {
        return;
    }

    if (rename(tmp, link) != 0) {
        unlink(tmp);
    }
}


/* Opens a new log file, run-DATE-TIME.log, and returns its descriptor. */
static int
open_log(const char *dir, char *stamp, size_t cap)
{
    time_t now = time(NULL);
    struct tm tm;
    char base[64];

    localtime_r(&now, &tm);
    strftime(base, sizeof base, "%Y%m%d-%H%M%S", &tm);

    for (int k = 1; k < 100; k++) {
        int fd;

        if (k == 1) {
            snprintf(stamp, cap, "%s", base);
        } else {
            snprintf(stamp, cap, "%s-%d", base, k);
        }

        snprintf(logfile, sizeof logfile, "%s/run-%s.log", dir, stamp);
        fd = open(logfile, O_WRONLY | O_APPEND | O_CREAT | O_EXCL | O_CLOEXEC,
                  0600);

        if (fd >= 0) {
            return fd;
        }

        if (errno != EEXIST) {
            return -1;
        }
    }

    return -1;
}


/*
 * Makes and binds the socket.  The name is in the directory of the logs; a
 * directory whose path is too long for a socket name gets a name in the
 * abstract namespace of Linux instead: it is not a file, so nothing is made
 * in /tmp or anywhere else.
 */
static int
open_socket(const char *dir, const char *stamp)
{
    union {
        struct sockaddr sa;
        struct sockaddr_un un;
    } addr;
    int bufsize = 1 << 20;
    long pid = getpid();
    long uid = getuid();
    int fd;

    snprintf(sockfile, sizeof sockfile, "%s/run-%s-%ld.sock", dir, stamp,
             pid);

    if (strlen(sockfile) >= sizeof addr.un.sun_path) {
        snprintf(sockfile, sizeof sockfile, "@strata-%ld-%ld", uid, pid);
    }

    if (strlen(sockfile) >= sizeof addr.un.sun_path) {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.un.sun_family = AF_UNIX;
    memcpy(addr.un.sun_path, sockfile, strlen(sockfile));

    socklen_t alen = sizeof addr;

    if (sockfile[0] == '@') {
        /* The address is as long as the name; the first byte is a zero. */
        addr.un.sun_path[0] = '\0';
        alen = sizeof addr.un.sun_family;

        for (const char *c = sockfile; *c != '\0'; c++) {
            alen++;
        }
    } else {
        unlink(sockfile);
    }

    if (bind(fd, &addr.sa, alen) != 0) {
        close(fd);

        return -1;
    }

    if (sockfile[0] != '@') {
        chmod(sockfile, 0600);
    }

    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof bufsize);

    return fd;
}


/* Makes a pipe whose two ends are closed by exec. */
static int
make_pipe(int fds[2])
{
    if (pipe(fds) != 0) {
        return -1;
    }

    for (int i = 0; i < 2; i++) {
        if (fcntl(fds[i], F_SETFD, FD_CLOEXEC) != 0) {
            close(fds[0]);
            close(fds[1]);

            return -1;
        }
    }

    return 0;
}


int
logrun_start(const char *dir, size_t keep)
{
    char stamp[80];
    int logfd;
    int sock;
    int fds[2];
    pid_t pid;

    if (collector > 0) {
        return 0;
    }

    if (make_dirs(dir) != 0) {
        return -1;
    }

    prune(dir, keep);
    logfd = open_log(dir, stamp, sizeof stamp);

    if (logfd < 0) {
        return -1;
    }

    sock = open_socket(dir, stamp);

    if (sock < 0) {
        close(logfd);
        unlink(logfile);

        return -1;
    }

    if (make_pipe(fds) != 0) {
        close(logfd);
        close(sock);
        unlink(logfile);
        socket_remove();

        return -1;
    }

    pid = fork();

    if (pid < 0) {
        close(logfd);
        close(sock);
        close(fds[0]);
        close(fds[1]);
        unlink(logfile);
        socket_remove();

        return -1;
    }

    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);

        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGHUP, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGWINCH, SIG_IGN);
        signal(SIGPIPE, SIG_IGN);

        if (nul >= 0) {
            dup2(nul, 0);
            dup2(nul, 1);
            dup2(nul, 2);

            if (nul > 2) {
                close(nul);
            }
        }

        close(fds[1]);

        for (int fd = 3; fd < 1024; fd++) {
            if (fd != sock && fd != fds[0] && fd != logfd) {
                close(fd);
            }
        }

        collect(sock, fds[0], logfd);
        socket_remove();
        _exit(0);
    }

    close(fds[0]);
    close(sock);
    close(logfd);
    endfd = fds[1];
    collector = pid;
    owner = getpid();
    setenv(LOG_SOCKET_ENV, sockfile, 1);

    {
        const char *slash = strrchr(logfile, '/');

        point_link(dir, slash != NULL ? slash + 1 : logfile);
    }

    return 0;
}


void
logrun_end(void)
{
    int st;

    if (collector <= 0 || owner != getpid()) {
        return;
    }

    close(endfd);
    endfd = -1;

    for (int i = 0; i < 200; i++) {
        if (waitpid(collector, &st, WNOHANG) != 0) {
            break;
        }

        usleep(10000);
    }

    collector = -1;
}


const char *
logrun_path(void)
{
    return collector > 0 ? logfile : NULL;
}
