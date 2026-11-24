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
 * Making the bitmap fonts that a page needs, the way TeX does.  Some fonts
 * of TeX Live (the EC fonts, among others) come as METAFONT sources and size
 * files only: there is no outline and no bitmap until something draws one.
 * TeX makes the bitmap (a PK file) on demand and keeps it in its own cache.
 * The Viewer uses the same machinery: the program kpsewhich, with the
 * making of PK files enabled, which runs mktexpk and METAFONT.  The work is
 * done in child processes, so the window stays alive; a font is tried once
 * in a run and for at most MK_LIMIT seconds.  No shell reads a line: the
 * programs are started by their full paths with an argument vector.
 */
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dviview.h"
#include "sysprog.h"

/* The seconds that one font may take, and how many are made at once. */
#define MK_LIMIT 10
#define MK_RUNNING 3
/* A resolution above this would make a bitmap of absurd size. */
#define MK_MAXDPI 4000

typedef struct {
    DFont *f;
    pid_t pid;
    int fd;
    long dpi;
    time_t t0;
    char out[1024];
    size_t n;
} Job;

static Job *jobs;
static int njobs;
static int tools = -1;
static char kpse[PATH_MAX];
static char shown[300];


/* The names of fonts have letters, digits and a few signs: nothing else. */
static int
name_ok(const char *s)
{
    if (s[0] == '\0' || s[0] == '-') {
        return 0;
    }

    for (; *s != '\0'; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_' || *s == '-' ||
              *s == '.' || *s == '+')) {
            return 0;
        }
    }

    return 1;
}


/*
 * Whether the programs that make a font are there: kpsewhich, mktexpk and
 * METAFONT (mf, or mf-nowin that mktexpk prefers).  Looked at once.
 */
static int
have_tools(void)
{
    char p[PATH_MAX];

    if (tools >= 0) {
        return tools;
    }

    tools = 0;

    if (sp_find("kpsewhich", kpse, sizeof kpse, NULL) == 0 &&
        sp_find("mktexpk", p, sizeof p, NULL) == 0 &&
        (sp_find("mf", p, sizeof p, NULL) == 0 ||
         sp_find("mf-nowin", p, sizeof p, NULL) == 0)) {
        tools = 1;
    }

    return tools;
}


int
mk_possible(void)
{
    return have_tools();
}


/* Starts the child of a job: kpsewhich with the making of PK files on. */
static void
job_start(Job *j)
{
    char dpi[64];
    char pk[320];
    char opt_mktex[] = "-mktex=pk";
    char opt_mode[] = "-mode=ljfour";
    char *argv[6];
    int fds[2];
    pid_t pid;

    snprintf(dpi, sizeof dpi, "-dpi=%ld", j->dpi);
    snprintf(pk, sizeof pk, "%s.pk", j->f->name);
    argv[0] = kpse;
    argv[1] = opt_mktex;
    argv[2] = opt_mode;
    argv[3] = dpi;
    argv[4] = pk;
    argv[5] = NULL;
    j->t0 = time(NULL);
    j->pid = -1;

    if (pipe(fds) != 0) {
        return;
    }

    pid = fork();

    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);

        return;
    }

    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);

        /* Its own group, so that a stop reaches METAFONT too. */
        setpgid(0, 0);
        dup2(fds[1], 1);

        if (nul >= 0) {
            dup2(nul, 0);
            dup2(nul, 2);
        }

        close(fds[0]);
        close(fds[1]);

        /* A failed making leaves missfont.log in the folder it runs in. */
        if (chdir(getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp") != 0) {
            _exit(127);
        }

        sp_child_env(kpse);
        execv(kpse, argv);
        _exit(127);
    }

    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    j->pid = pid;
    j->fd = fds[0];
}


int
mk_request(DFont *f, long dpi)
{
    Job *j;

    if (f->mk_tried != 0 || !name_ok(f->name) || dpi < 1 ||
        dpi > MK_MAXDPI || !have_tools()) {
        return 0;
    }

    f->mk_tried = 1;
    jobs = xrealloc(jobs, to_size(njobs + 1) * sizeof *jobs);
    j = &jobs[njobs++];
    memset(j, 0, sizeof *j);
    j->f = f;
    j->dpi = dpi;
    j->pid = 0;
    j->fd = -1;

    return 1;
}


static int
running(void)
{
    int n = 0;

    for (int i = 0; i < njobs; i++) {
        if (jobs[i].pid > 0) {
            n++;
        }
    }

    return n;
}


int
mk_active(void)
{
    return njobs > 0;
}


/* Takes a job out of the list, whatever its end was. */
static void
job_drop(int i)
{
    if (jobs[i].fd >= 0) {
        close(jobs[i].fd);
    }

    memmove(&jobs[i], &jobs[i + 1], to_size(njobs - i - 1) * sizeof *jobs);
    njobs--;
}


/* Reads what the child wrote so far (the path of the file it made). */
static void
job_read(Job *j)
{
    while (j->fd >= 0 && j->n + 1 < sizeof j->out) {
        ssize_t r = read(j->fd, j->out + j->n, sizeof j->out - j->n - 1);

        if (r <= 0) {
            return;
        }

        j->n += (size_t)r;
        j->out[j->n] = '\0';
    }
}


/* The end of a job: the font has its bitmap or stays drawn as boxes. */
static int
job_done(Job *j, int ok, const char *why)
{
    DFont *f = j->f;
    char *nl;
    struct stat st;

    job_read(j);
    nl = strchr(j->out, '\n');

    if (nl != NULL) {
        *nl = '\0';
    }

    if (ok && j->out[0] == '/' && stat(j->out, &st) == 0) {
        f->pk = pk_load(j->out);
    }

    if (f->pk != NULL) {
        f->missing = 0;

        return 1;
    }

    viewer_tell("note",
                "font %s not found, drawing boxes where its letters are; "
                "the PDF is not affected.  The Viewer tried to make the "
                "font and %s",
                f->name, why);

    return 0;
}


int
mk_poll(void)
{
    int changed = 0;
    int i = 0;
    time_t now = time(NULL);

    while (i < njobs) {
        Job *j = &jobs[i];
        int st = 0;

        if (j->pid == 0) {
            if (running() < MK_RUNNING) {
                job_start(j);

                if (j->pid < 0) {
                    viewer_tell("note",
                                "font %s not found, drawing boxes where "
                                "its letters are; the PDF is not "
                                "affected.  The program that makes it "
                                "could not be started", j->f->name);
                    job_drop(i);

                    continue;
                }
            }

            i++;

            continue;
        }

        job_read(j);

        if (waitpid(j->pid, &st, WNOHANG) == j->pid) {
            changed += job_done(j, WIFEXITED(st) && WEXITSTATUS(st) == 0,
                                "it did not succeed");
            job_drop(i);

            continue;
        }

        if (now - j->t0 >= MK_LIMIT) {
            kill(-j->pid, SIGKILL);
            kill(j->pid, SIGKILL);
            waitpid(j->pid, &st, 0);
            job_done(j, 0, "it took more than 10 seconds");
            job_drop(i);

            continue;
        }

        i++;
    }

    return changed;
}


/* Ends the jobs that run: the Viewer is closed or stopped. */
void
mk_stop(void)
{
    for (int i = 0; i < njobs; i++) {
        if (jobs[i].pid > 0) {
            kill(-jobs[i].pid, SIGKILL);
            kill(jobs[i].pid, SIGKILL);
            waitpid(jobs[i].pid, NULL, 0);
            jobs[i].pid = 0;
        }
    }
}


const char *
mk_status(void)
{
    char more[40] = "";

    if (njobs == 0) {
        return NULL;
    }

    if (njobs > 1) {
        snprintf(more, sizeof more, " (and %d more)", njobs - 1);
    }

    snprintf(shown, sizeof shown, "Making the font %s ...%s", jobs[0].f->name,
             more);

    return shown;
}
