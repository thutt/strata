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
 * memshim.c - a counting malloc for the memory survey (docs/MEMORY.txt).
 *
 * A study tool, not part of the build.  Make and use it like this:
 *
 *     cc -O2 -shared -fPIC -o memshim.so tests/memshim.c -ldl
 *     python3 -I -B tests/memdrive.py memshim.so STRED DOC DUMP
 *     python3 -I -B tests/memsites.py DUMP STRED [PHASE [LINES]]
 *
 * LD_PRELOAD the object and send SIGUSR1 to the program: it appends to the
 * file named by MEMSHIM_OUT the bytes that are alive now, per allocation
 * site (the return addresses of six frames), the total alive and the peak.
 * Each block has a 16 byte header (size and site).  memdrive.py runs the
 * Editor through the steps of tests/measure.py (open, Ctrl-End, a search, a
 * save) and signals it after each; memsites.py names the sites with nm.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NSITE 65536
#define NFRAME 6

typedef struct {
    void *f[NFRAME];
    size_t live;
    size_t count;
    size_t total;
    int used;
} Site;

static Site sites[NSITE];
static size_t live_total, peak_total;
static int in_hook;
static void *(*real_malloc)(size_t);
static void (*real_free)(void *);
static void *(*real_realloc)(void *, size_t);
static unsigned char early[1 << 16];
static size_t early_n;

static unsigned
hash_frames(void **f, int n)
{
    unsigned h = 2166136261u;

    for (int i = 0; i < n; i++) {
        h = (h ^ (unsigned)((uintptr_t)f[i] >> 2)) * 16777619u;
    }

    return h;
}

static int
site_of(void)
{
    void *f[NFRAME + 2];
    int n = backtrace(f, NFRAME + 2);
    void **p = f + 2;

    n -= 2;

    if (n < 1) {
        return 0;
    }

    unsigned h = hash_frames(p, n);

    for (unsigned k = 0; k < NSITE; k++) {
        Site *s = &sites[(h + k) % NSITE];

        if (s->used == 0) {
            s->used = 1;
            memset(s->f, 0, sizeof s->f);
            memcpy(s->f, p, (size_t)n * sizeof(void *));
            return (int)((h + k) % NSITE);
        }

        if (memcmp(s->f, p, (size_t)n * sizeof(void *)) == 0 &&
            (n == NFRAME || s->f[n] == NULL)) {
            return (int)((h + k) % NSITE);
        }
    }

    return 0;
}

static void
init(void)
{
    if (real_malloc == NULL) {
        real_malloc = dlsym(RTLD_NEXT, "malloc");
        real_free = dlsym(RTLD_NEXT, "free");
        real_realloc = dlsym(RTLD_NEXT, "realloc");
    }
}

typedef struct {
    size_t size;
    size_t site;
} Hdr;

static void *
get(size_t n)
{
    init();

    if (real_malloc == NULL || in_hook != 0) {
        if (real_malloc == NULL) {
            void *p = early + early_n;

            early_n += (n + 47) & ~(size_t)15;

            return p;
        }

        Hdr *h = real_malloc(n + sizeof(Hdr));

        if (h == NULL) {
            return NULL;
        }

        h->size = n;
        h->site = (size_t)-1;

        return h + 1;
    }

    in_hook = 1;
    int s = site_of();
    Hdr *h = real_malloc(n + sizeof(Hdr));

    if (h == NULL) {
        in_hook = 0;
        return NULL;
    }

    h->size = n;
    h->site = (size_t)s;
    sites[s].live += n;
    sites[s].count++;
    sites[s].total += n;
    live_total += n;

    if (live_total > peak_total) {
        peak_total = live_total;
    }

    in_hook = 0;

    return h + 1;
}

void *
malloc(size_t n)
{
    return get(n);
}

void *
calloc(size_t a, size_t b)
{
    void *p = get(a * b);

    if (p != NULL) {
        memset(p, 0, a * b);
    }

    return p;
}

static void
drop(void *p)
{
    Hdr *h = (Hdr *)p - 1;

    if (h->site != (size_t)-1 && h->site < NSITE) {
        sites[h->site].live -= h->size;
        live_total -= h->size;
    }
}

void
free(void *p)
{
    init();

    if (p == NULL || ((unsigned char *)p >= early &&
                      (unsigned char *)p < early + sizeof early)) {
        return;
    }

    drop(p);
    real_free((Hdr *)p - 1);
}

void *
realloc(void *p, size_t n)
{
    init();

    if (p == NULL) {
        return get(n);
    }

    if ((unsigned char *)p >= early && (unsigned char *)p < early + sizeof early) {
        void *q = get(n);

        memcpy(q, p, n);

        return q;
    }

    Hdr *old = (Hdr *)p - 1;
    size_t oldn = old->size;
    size_t site = old->site;

    if (site == (size_t)-1 || in_hook != 0) {
        Hdr *h = real_realloc(old, n + sizeof(Hdr));

        if (h == NULL) {
            return NULL;
        }

        h->size = n;

        return h + 1;
    }

    in_hook = 1;
    Hdr *h = real_realloc(old, n + sizeof(Hdr));

    if (h == NULL) {
        in_hook = 0;
        return NULL;
    }

    h->size = n;
    sites[site].live += n;
    sites[site].live -= oldn;
    sites[site].total += n > oldn ? n - oldn : 0;
    live_total += n;
    live_total -= oldn;

    if (live_total > peak_total) {
        peak_total = live_total;
    }

    in_hook = 0;

    return h + 1;
}

static void
dump(int sig)
{
    (void)sig;
    const char *out = getenv("MEMSHIM_OUT");
    FILE *f = fopen(out != NULL ? out : "/dev/null", "a");

    if (f == NULL) {
        return;
    }

    fprintf(f, "PHASE live %zu peak %zu\n", live_total, peak_total);

    for (int i = 0; i < NSITE; i++) {
        if (sites[i].used != 0 && sites[i].live > 0) {
            fprintf(f, "S %zu %zu %zu", sites[i].live, sites[i].count,
                    sites[i].total);

            for (int k = 0; k < NFRAME && sites[i].f[k] != NULL; k++) {
                fprintf(f, " %p", sites[i].f[k]);
            }

            fprintf(f, "\n");
        }
    }

    fclose(f);
}

__attribute__((constructor)) static void
start(void)
{
    init();
    signal(SIGUSR1, dump);
    FILE *m = fopen("/proc/self/maps", "r");
    char line[256];

    if (m != NULL && getenv("MEMSHIM_OUT") != NULL) {
        FILE *f = fopen(getenv("MEMSHIM_OUT"), "a");

        while (f != NULL && fgets(line, sizeof line, m) != NULL) {
            if (strstr(line, "stred") != NULL && strstr(line, "r-xp") != NULL) {
                fprintf(f, "MAPS %s", line);
            }
        }

        if (f != NULL) {
            fclose(f);
        }
    }

    if (m != NULL) {
        fclose(m);
    }
}
