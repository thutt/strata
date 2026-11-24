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

/* pvcore.c - the parts of the LaTeX preview that need no screen. */
#include "util.h"
#include "pvcore.h"
#include "sysprog.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define NSIGNALS 3

#define LINE_TAG "@@strprint "
#define NAME_TAG "strpv."


void
pv_prog_init(PvProg *p, int prior)
{
    memset(p, 0, sizeof *p);
    p->stage = PV_WRITING;
    p->prior = prior > 0 ? prior : 1;
    p->pos = -1;
    p->at_start = 1;
    p->last_val = -1;
}


/* The start of a line of TeX that is text of a warning or an error. */
static int
is_warning_start(const char *s, int n)
{
    static const char *const tags[] = {
        "! ", "Overfull ", "Underfull ", "LaTeX Warning", "LaTeX Error",
        "Package ", "Class ", "Runaway ", "Missing ", "pdfTeX warning",
        "l.", "Over-full", "Under-full"};

    for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        int len = slen(tags[i]);

        if (n >= len && strncmp(s, tags[i], usz(len)) == 0) {
            return 1;
        }
    }

    return 0;
}


/* A complete line that strprint wrote for the editor. */
static int
handle_line(PvProg *p)
{
    const char *s = p->line;
    int changed = 0;

    if (strncmp(s, LINE_TAG, sizeof LINE_TAG - 1) == 0) {
        s += sizeof LINE_TAG - 1;

        if (strcmp(s, "stage writing") == 0) {
            p->stage = PV_WRITING;
            changed = 1;
        } else if (strncmp(s, "pass ", 5) == 0) {
            int pass = atoi(s + 5);

            /* A pass after a finished one knows the pages of that one. */
            p->known = pass > 1 && p->pass == pass - 1 ? p->pages : 0;
            p->pass = pass;
            p->pages = 0;
            p->pos = -1;
            p->pos_pages = 0;
            p->stage = PV_LATEX;
            changed = 1;
        } else if (strncmp(s, "pos ", 4) == 0) {
            p->pos = atoi(s + 4);
            p->pos_pages = p->pages;
            changed = 1;
        } else if (strncmp(s, "error ", 6) == 0) {
            str_copy(p->err1, sizeof p->err1, s + 6);
        } else if (strncmp(s, "error2 ", 7) == 0) {
            str_copy(p->err2, sizeof p->err2, s + 7);
        } else if (strncmp(s, "diag ", 5) == 0) {
            Diag g;

            if (diag_decode(&g, s + 5) == 0) {
                if (p->ndiag < DIAG_MAX) {
                    g.seq = p->ndiag;
                    p->diag[p->ndiag++] = g;
                } else {
                    p->ndiag_dropped++;
                }

                changed = 1;
            }
        } else if (strncmp(s, "diagdropped ", 12) == 0) {
            p->ndiag_dropped += atoi(s + 12);
        }
    } else if (strncmp(s, "strprint: ", 10) == 0) {
        str_copy(p->note, sizeof p->note, s + 10);
    }

    return changed;
}


/* The end of a page mark "[N": the page is counted. */
static int
end_mark(PvProg *p)
{
    /*
     * In the text of a warning only the mark that begins a line, or the
     * number that follows the last page, is taken for a page: a citation
     * such as [12] in the text of an Overfull box is not.  TeX ends the
     * context of a box without a newline, so the next page can follow on
     * the same line.
     */
    int ok = p->skip == 0 || p->mark_at_start != 0 ||
             p->mark_val == p->last_val + 1;

    p->mark_state = 0;

    if (ok == 0) {
        return 0;
    }

    p->skip = 0;
    p->last_val = p->mark_val;
    p->pages++;

    return 1;
}


int
pv_prog_feed(PvProg *p, const char *buf, size_t n)
{
    int changed = 0;

    for (size_t k = 0; k < n; k++) {
        char c = buf[k];

        /* The page mark: "[", digits, and then a space, "]", "{", "<". */
        if (p->mark_state == 2 && is_digit(c) == 0 && c != '-') {
            if (c == ' ' || c == ']' || c == '{' || c == '<' ||
                c == '\n' || c == '\r') {
                changed |= end_mark(p);
            } else {
                p->mark_state = 0;
            }
        } else if (p->mark_state == 1) {
            p->mark_state = is_digit(c) || c == '-' ? 2 : 0;
            p->mark_val = 0;
        }

        if (p->mark_state == 2 && is_digit(c) != 0 && p->mark_val < 1000000) {
            p->mark_val = p->mark_val * 10 + (c - '0');
        }

        if (c == '[' && p->mark_state == 0) {
            p->mark_state = 1;
            p->mark_at_start = p->at_start;
        }

        if (c == '\n') {
            p->line[p->linelen] = '\0';

            if (p->linelen == 0) {
                p->skip = 0;
            }

            changed |= handle_line(p);
            p->linelen = 0;
            p->at_start = 1;

            continue;
        }

        if (p->linelen < PV_LINE - 1) {
            p->line[p->linelen++] = c;
            p->line[p->linelen] = '\0';

            if (p->linelen <= 14 && p->skip == 0 &&
                is_warning_start(p->line, p->linelen) != 0) {
                p->skip = 1;
            }
        }

        p->at_start = 0;
    }

    return changed;
}


int
pv_refine(int prior, int pages, int pos)
{
    long total = prior > 0 ? prior : 1;

    if (pos > 0 && pos <= 1000 && pages > 0) {
        long ex = (1L * pages * 1000 + pos / 2) / pos;

        total = (ex * pos + 1L * prior * (1000 - pos) + 500) / 1000;
    }

    return total < pages ? pages : isz(lsz(total));
}


int
pv_prog_total(const PvProg *p, int *about)
{
    int total;

    if (p->known > 0) {
        *about = 0;

        return p->known < p->pages ? p->pages : p->known;
    }

    *about = 1;
    total = pv_refine(p->prior, p->pos_pages, p->pos);

    return total < p->pages ? p->pages : total;
}


int
pv_prog_percent(const PvProg *p)
{
    int about;
    int total = pv_prog_total(p, &about);
    int pc = isz(lsz(100L * p->pages / (total > 0 ? total : 1)));

    if (p->stage == PV_WRITING) {
        return 0;
    }

    if (p->stage == PV_VIEWER) {
        return 100;
    }

    return pc > 99 ? 99 : pc;
}


int
pv_mkdtemp(char *path, size_t cap)
{
    const char *tmp = getenv("TMPDIR");
    char tmpl[1024];

    snprintf(tmpl, sizeof tmpl, "%s/" NAME_TAG "XXXXXX",
             tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");

    if (mkdtemp(tmpl) == NULL) {
        return -1;
    }

    /* mkdtemp makes mode 0700; say it again in case of an odd libc. */
    if (chmod(tmpl, 0700) != 0) {
        rmdir(tmpl);

        return -1;
    }

    str_copy(path, cap, tmpl);

    return 0;
}


/* Delete what is in a directory, then the directory; depth is limited. */
static int
rm_dir(const char *path, int depth)
{
    DIR *dp = opendir(path);
    struct dirent *e;

    if (dp == NULL) {
        return -1;
    }

    while ((e = readdir(dp)) != NULL) {
        char sub[1100];
        struct stat st;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }

        path_join(sub, sizeof sub, path, e->d_name);

        if (lstat(sub, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode) != 0 && depth < 8) {
            rm_dir(sub, depth + 1);
        } else {
            unlink(sub);
        }
    }

    closedir(dp);

    return rmdir(path);
}


int
pv_rmtree(const char *path)
{
    const char *base = strrchr(path, '/');

    base = base != NULL ? base + 1 : path;

    if (strncmp(base, NAME_TAG, sizeof NAME_TAG - 1) != 0) {
        return -1;
    }

    if (access(path, F_OK) != 0) {
        return 0;
    }

    return rm_dir(path, 0);
}


void
pv_self_dir(char *out, size_t cap)
{
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    char *sl;

    out[0] = '\0';

    if (n <= 0) {
        return;
    }

    buf[n] = '\0';
    sl = strrchr(buf, '/');

    if (sl != NULL) {
        *sl = '\0';
        str_copy(out, cap, buf);
    }
}


/* Whether path is a regular file that can be run. */
static int
is_program(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode) != 0 &&
           access(path, X_OK) == 0;
}


int
pv_find_prog(const char *envname, const char *name, char *out, size_t cap)
{
    const char *env = envname != NULL ? getenv(envname) : NULL;
    char dir[1024];

    /* A variable names the program by its full path: the user chose it. */
    if (env != NULL && env[0] != '\0') {
        return sp_find_env(envname, name, out, cap);
    }

    /* A program of Strata is first looked for next to the running one. */
    pv_self_dir(dir, sizeof dir);

    if (dir[0] != '\0') {
        char cand[PATH_MAX];

        path_join(cand, sizeof cand, dir, name);

        if (is_program(cand) != 0) {
            str_copy(out, cap, cand);

            return 0;
        }
    }

    /* Then in the fixed folders of the system, never in PATH. */
    return sp_find(name, out, cap, NULL);
}


int
pv_job_start(PvJob *j, char *const argv[], const char *tmpdir)
{
    int fds[2];
    pid_t pid;

    j->pid = 0;
    j->fd = -1;
    j->status = 0;

    if (pipe(fds) != 0) {
        return -1;
    }

    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    pid = fork();

    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);

        return -1;
    }

    if (pid == 0) {
        int null = open("/dev/null", O_RDONLY);

        setpgid(0, 0);

        if (null >= 0) {
            dup2(null, 0);
        }

        dup2(fds[1], 1);
        dup2(fds[1], 2);
        setenv("TMPDIR", tmpdir, 1);
        sp_child_env(argv[0]);
        execv(argv[0], argv);
        _exit(127);
    }

    setpgid(pid, pid);
    close(fds[1]);
    j->pid = pid;
    j->fd = fds[0];

    return 0;
}


long
pv_job_read(PvJob *j, char *buf, size_t cap)
{
    ssize_t n;

    if (j->fd < 0) {
        return 0;
    }

    n = read(j->fd, buf, cap);

    if (n < 0) {
        return -1;
    }

    return n;
}


int
pv_job_done(PvJob *j)
{
    pid_t r;

    if (j->pid <= 0) {
        return 1;
    }

    r = waitpid(j->pid, &j->status, WNOHANG);

    if (r == 0) {
        return 0;
    }

    if (r < 0 && errno == EINTR) {
        return 0;
    }

    if (r < 0) {
        j->status = -1;
    }

    /* Whatever is left of the process group goes with the child. */
    killpg(j->pid, SIGKILL);
    j->pid = 0;

    return 1;
}


void
pv_job_kill(PvJob *j)
{
    int st;

    if (j->pid > 0) {
        killpg(j->pid, SIGKILL);

        while (waitpid(j->pid, &st, 0) < 0 && errno == EINTR) {
            continue;
        }

        j->status = st;
        j->pid = 0;
    }

    pv_job_close(j);
}


void
pv_job_close(PvJob *j)
{
    if (j->fd >= 0) {
        close(j->fd);
        j->fd = -1;
    }
}


/* The signals that stop the program while a job runs. */
static const int caught_signals[NSIGNALS] = {SIGINT, SIGTERM, SIGHUP};
static struct sigaction old_actions[NSIGNALS];
static int hooked;

/* Set by the handler of a signal that stops the program. */
static volatile sig_atomic_t sig_caught;
/* The process groups to stop at once when that happens. */
static volatile pid_t sig_job;
static volatile pid_t sig_viewer;


static void
on_signal(int s)
{
    sig_caught = s;

    if (sig_job > 0) {
        kill(-sig_job, SIGKILL);
    }

    if (sig_viewer > 0) {
        kill(-sig_viewer, SIGKILL);
    }
}


void
pv_sig_hook(void)
{
    struct sigaction sa;

    if (hooked != 0) {
        return;
    }

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sig_caught = 0;

    for (int i = 0; i < NSIGNALS; i++) {
        sigaction(caught_signals[i], &sa, &old_actions[i]);
    }

    hooked = 1;
}


void
pv_sig_set(pid_t job, pid_t viewer)
{
    sig_job = job;
    sig_viewer = viewer;
}


int
pv_sig_caught(void)
{
    return sig_caught;
}


void
pv_sig_release(void)
{
    int sig = sig_caught;

    sig_job = 0;
    sig_viewer = 0;

    if (hooked != 0) {
        for (int i = 0; i < NSIGNALS; i++) {
            sigaction(caught_signals[i], &old_actions[i], NULL);
        }

        hooked = 0;
    }

    if (sig != 0) {
        sig_caught = 0;
        raise(sig);
    }
}
