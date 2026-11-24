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

/* spell.c - the spell check: words, personal list, checker child, walk. */
#include "spell.h"
#include "config.h"
#include "layout.h"
#include "marker.h"
#include "util.h"
#include "sysprog.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ================================================================ words */
/* A letter of CP437: the ASCII letters and the accented letters. */
static int
sp_letter(int c)
{
    if (is_alpha(c)) {
        return 1;
    }

    if (c >= 0x80) {
        unsigned u = cp437_uni[c & 0xFF];

        return u >= 0xC0 && u <= 0xFF && u != 0xD7 && u != 0xF7;
    }

    return 0;
}


int
spell_is_letter(int c)
{
    return sp_letter(c);
}


static int
sp_alnum(int c)
{
    return sp_letter(c) != 0 || is_digit(c);
}


/*
 * Does the token belong to a run of word characters?  A soft hyphen does: it
 * is no part of the word's letters (spell_find_word leaves it out), but it
 * does not end the word.
 */
static int
sp_wordtok(const Tok *k)
{
    if (k->kind == TK_CTRL && k->a == SHY_CTRL) {
        return 1;
    }

    return k->kind == TK_CHAR && k->a > ' ' && k->a != 0x7F;
}


/* Is the token a soft hyphen? */
static int
sp_shy(const Tok *k)
{
    return k->kind == TK_CTRL && k->a == SHY_CTRL;
}


int
spell_find_word(const Text *t, int from, int to, SpellWord *w)
{
    int end = to < t->n ? to : t->n;
    int i = from < 0 ? 0 : from;

    while (i < end) {
        if (sp_wordtok(&t->v[i]) == 0) {
            i++;
            continue;
        }

        int a = i;

        while (i < end && sp_wordtok(&t->v[i]) != 0) {
            i++;
        }

        int b = i;

        while (a < b && (sp_shy(&t->v[a]) != 0 || sp_alnum(t->v[a].a) == 0)) {
            a++;
        }

        while (b > a &&
               (sp_shy(&t->v[b - 1]) != 0 || sp_alnum(t->v[b - 1].a) == 0)) {
            b--;
        }

        /* Nothing is left, or the word begins with a digit. */
        if (a >= b || is_digit(t->v[a].a)) {
            continue;
        }

        w->start = a;
        w->end = b;
        w->len = 0;

        int n = 0;

        for (int k = a; k < b; k++) {
            if (sp_shy(&t->v[k]) != 0) {
                continue;
            }

            if (n < SPELL_MAXWORD) {
                w->text[n++] = chr(t->v[k].a);
            }

            w->len++;
        }

        w->too_long = w->len > SPELL_MAXWORD;
        w->text[n] = '\0';

        return 1;
    }

    return 0;
}


int
spell_word_at(const Text *t, int tok, SpellWord *w)
{
    int p = tok;

    if (p >= t->n || p < 0 || sp_wordtok(&t->v[p]) == 0) {
        p--;
    }

    if (p < 0 || p >= t->n || sp_wordtok(&t->v[p]) == 0) {
        return 0;
    }

    int s = p;
    int e = p + 1;

    while (s > 0 && sp_wordtok(&t->v[s - 1]) != 0) {
        s--;
    }

    while (e < t->n && sp_wordtok(&t->v[e]) != 0) {
        e++;
    }

    return spell_find_word(t, s, e, w);
}


int
spell_replace_word(Text *t, const SpellWord *w, const char *s)
{
    int old = w->end - w->start;
    int n = 0;

    text_delete(t, w->start, old);

    for (int i = 0; s[i] != '\0'; i++) {
        if (u8(s[i]) >= ' ') {
            text_insert(t, w->start + n, tok_char(u8(s[i])));
            n++;
        }
    }

    return n - old;
}


int
spell_has_digit(const char *s)
{
    for (int i = 0; s[i] != '\0'; i++) {
        if (is_digit(s[i])) {
            return 1;
        }
    }

    return 0;
}


/* ============================================================== UTF-8 */
void
spell_to_utf8(const char *cp437, char *out, size_t cap)
{
    size_t o = 0;

    for (size_t i = 0; cp437[i] != '\0'; i++) {
        char u[4];
        int n = utf8_encode(cp437_uni[u8(cp437[i])], u);

        if (o + usz(n) + 1 > cap) {
            break;
        }

        memcpy(out + o, u, usz(n));
        o += usz(n);
    }

    if (cap > 0) {
        out[o] = '\0';
    }
}


/* The code point of the UTF-8 character at s, and its length in *n. */
static unsigned
utf8_decode(const char *s, size_t *n)
{
    unsigned c = u8(s[0]);

    if (c < 0x80) {
        *n = 1;

        return c;
    }

    if ((c & 0xE0) == 0xC0 && (u8(s[1]) & 0xC0) == 0x80) {
        *n = 2;

        return ((c & 0x1F) << 6) | (u8(s[1]) & 0x3Fu);
    }

    if ((c & 0xF0) == 0xE0 && (u8(s[1]) & 0xC0) == 0x80 &&
        (u8(s[2]) & 0xC0) == 0x80) {
        *n = 3;

        return ((c & 0x0F) << 12) | ((u8(s[1]) & 0x3Fu) << 6) |
               (u8(s[2]) & 0x3Fu);
    }

    /* A byte that is not UTF-8 (or a longer form) is taken alone. */
    *n = 1;

    return 0xFFFD;
}


void
spell_from_utf8(const char *utf8, char *out, size_t cap)
{
    size_t o = 0;
    size_t i = 0;

    while (utf8[i] != '\0' && o + 1 < cap) {
        size_t n;
        unsigned cp = utf8_decode(utf8 + i, &n);
        int b = '?';

        if (cp < 0x80) {
            b = isz(cp);
        } else {
            for (int k = 0x80; k < 256; k++) {
                if (cp437_uni[k] == cp) {
                    b = k;
                    break;
                }
            }
        }

        out[o++] = chr(b);
        i += n;
    }

    if (cap > 0) {
        out[o] = '\0';
    }
}


/* ====================================================== personal list */
void
spell_dict_path(char *out, size_t cap)
{
    char cfg[PATH_MAX];

    config_path(cfg, isz(sizeof cfg));

    char *sl = strrchr(cfg, '/');

    if (sl == NULL) {
        str_copy(out, cap, "dictionary");

        return;
    }

    sl[1] = '\0';
    snprintf(out, cap, "%sdictionary", cfg);
}


static void
dict_push(SpellDict *d, const char *w)
{
    if (d->n >= d->cap) {
        d->cap = d->cap == 0 ? 32 : d->cap * 2;
        d->w = realloc(d->w, usz(d->cap) * sizeof *d->w);

        if (d->w == NULL) {
            abort();
        }
    }

    size_t n = strlen(w);

    d->w[d->n] = malloc(n + 1);

    if (d->w[d->n] == NULL) {
        abort();
    }

    memcpy(d->w[d->n], w, n + 1);
    d->n++;
}


void
spell_dict_append(SpellDict *d, const char *word)
{
    dict_push(d, word);
}


#define DICT_FILE_WORDS_MAX 1000000


void
spell_dict_load(SpellDict *d)
{
    char ln[512];
    FILE *f = config_read_file("dictionary");

    memset(d, 0, sizeof *d);

    if (f == NULL) {
        return;
    }

    /* A word list with more words than this is not a person's own list. */
    while (d->n < DICT_FILE_WORDS_MAX && fgets(ln, sizeof ln, f) != NULL) {
        char w[SPELL_MAXWORD + 1];

        ln[strcspn(ln, "\r\n")] = '\0';

        if (ln[0] == '\0') {
            continue;
        }

        spell_from_utf8(ln, w, sizeof w);
        dict_push(d, w);
    }

    fclose(f);
}


void
spell_dict_free(SpellDict *d)
{
    for (int i = 0; i < d->n; i++) {
        free(d->w[i]);
    }

    free(d->w);
    memset(d, 0, sizeof *d);
}


/* Is the word the entry with a capital letter in front? */
static int
initial_cap_of(const char *word, const char *entry)
{
    if (entry[0] == '\0' || word[0] != chr(to_upper(entry[0]))) {
        return 0;
    }

    return strcmp(word + 1, entry + 1) == 0;
}


static int
all_lower(const char *s)
{
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            return 0;
        }
    }

    return 1;
}


int
spell_dict_has(const SpellDict *d, const char *word)
{
    if (d == NULL) {
        return 0;
    }

    for (int i = 0; i < d->n; i++) {
        if (strcmp(d->w[i], word) == 0) {
            return 1;
        }

        if (all_lower(d->w[i]) != 0 && initial_cap_of(word, d->w[i]) != 0) {
            return 1;
        }
    }

    return 0;
}


int
spell_dict_add(SpellDict *d, const char *word, char *err, size_t errcap)
{
    size_t total = 1;
    char *buf;
    size_t o = 0;
    int rc;

    if (spell_dict_has(d, word) != 0 && all_lower(word) == 0) {
        return 0;
    }

    for (int i = 0; i < d->n; i++) {
        if (strcmp(d->w[i], word) == 0) {
            return 0;
        }
    }

    dict_push(d, word);

    for (int i = 0; i < d->n; i++) {
        total += strlen(d->w[i]) * 3 + 1;
    }

    buf = malloc(total);

    if (buf == NULL) {
        abort();
    }

    for (int i = 0; i < d->n; i++) {
        spell_to_utf8(d->w[i], buf + o, total - o);
        o += strlen(buf + o);
        buf[o++] = '\n';
    }

    buf[o] = '\0';
    rc = config_write_file("dictionary", buf, err, isz(errcap));
    free(buf);

    return rc;
}


/* ============================================================ the child */
struct SpellProc {
    pid_t pid;
    int in; /* we write to the child here */
    int out; /* we read from the child here */
    char *obuf; /* what is waiting to be written */
    size_t olen;
    size_t opos;
    size_t ocap;
    char *ibuf; /* what was read and is not a whole line yet */
    size_t ilen;
    size_t icap;
    char *cur; /* the lines of the answer being read */
    size_t clen;
    size_t ccap;
    char **ans; /* the answers that are complete, oldest first */
    int nans;
    int capans;
    int ahead; /* the first answer in ans that is not taken */
    int banner; /* the first line of the checker was seen */
    int dead;
};


/* A growing byte buffer. */
static void
grow(char **buf, size_t *cap, size_t need)
{
    if (need <= *cap) {
        return;
    }

    size_t n = *cap == 0 ? 1024 : *cap;

    while (n < need) {
        n *= 2;
    }

    *buf = realloc(*buf, n);

    if (*buf == NULL) {
        abort();
    }

    *cap = n;
}


/* Copy at most n bytes of src, and end dst with a NUL. */
static void
copy_n(char *dst, size_t cap, const char *src, size_t n)
{
    if (cap == 0) {
        return;
    }

    if (n >= cap) {
        n = cap - 1;
    }

    memcpy(dst, src, n);
    dst[n] = '\0';
}


static int
is_exec(const char *p)
{
    return access(p, X_OK) == 0;
}


/*
 * Find a program: a name with a slash is the full path that the user named
 * (the variable STRATA_SPELL), a plain name is looked for in the fixed
 * folders of the system and never in PATH.  0 and the path when it is found.
 */
static int
in_path(const char *name, char *out, size_t cap)
{
    if (strchr(name, '/') != NULL) {
        if (is_exec(name) != 0) {
            str_copy(out, cap, name);

            return 0;
        }

        return -1;
    }

    return sp_find(name, out, cap, NULL);
}


int
spell_find_program(char *path, size_t cap)
{
    const char *env = getenv("STRATA_SPELL");

    if (env != NULL && env[0] != '\0') {
        return in_path(env, path, cap);
    }

    if (in_path("aspell", path, cap) == 0) {
        return 0;
    }

    return in_path("hunspell", path, cap);
}


void
spell_language(char *out, size_t cap)
{
    Config c;

    config_load(&c);
    str_copy(out, cap, c.spell_lang[0] != '\0' ? c.spell_lang : "en_US");
}


static int
set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);

    if (fl < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}


static long
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}


/* A time in milliseconds as an int, for poll. */
static int
ms_int(long ms)
{
    if (ms < 0) {
        return 0;
    }

    return ms > INT_MAX ? INT_MAX : isz(lsz(ms));
}


/* One whole line was read: part of an answer, or its end (a blank line). */
static void
got_line(SpellProc *p, const char *ln, size_t n)
{
    if (p->banner == 0) {
        p->banner = 1;

        return;
    }

    if (n == 0) {
        if (p->nans >= p->capans) {
            p->capans = p->capans == 0 ? 16 : p->capans * 2;
            p->ans = realloc(p->ans, usz(p->capans) * sizeof *p->ans);

            if (p->ans == NULL) {
                abort();
            }
        }

        grow(&p->cur, &p->ccap, p->clen + 1);
        p->cur[p->clen] = '\0';
        p->ans[p->nans++] = p->cur;
        p->cur = NULL;
        p->ccap = 0;
        p->clen = 0;

        return;
    }

    grow(&p->cur, &p->ccap, p->clen + n + 2);
    memcpy(p->cur + p->clen, ln, n);
    p->clen += n;
    p->cur[p->clen++] = '\n';
}


static void
parse_input(SpellProc *p)
{
    size_t s = 0;

    for (size_t i = 0; i < p->ilen; i++) {
        if (p->ibuf[i] == '\n') {
            size_t n = i - s;

            if (n > 0 && p->ibuf[s + n - 1] == '\r') {
                n--;
            }

            got_line(p, p->ibuf + s, n);
            s = i + 1;
        }
    }

    if (s > 0) {
        memmove(p->ibuf, p->ibuf + s, p->ilen - s);
        p->ilen -= s;
    }
}


int
spell_proc_pump(SpellProc *p, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    for (;;) {
        struct pollfd pf[2];
        int np = 0;
        long left = deadline - now_ms();

        if (p->nans > p->ahead) {
            return p->nans - p->ahead;
        }

        if (p->dead != 0) {
            return -1;
        }

        pf[np].fd = p->out;
        pf[np].events = POLLIN;
        pf[np].revents = 0;
        np++;

        if (p->opos < p->olen) {
            pf[np].fd = p->in;
            pf[np].events = POLLOUT;
            pf[np].revents = 0;
            np++;
        }

        if (left < 0) {
            left = 0;
        }

        int rc = poll(pf, usz(np), ms_int(left));

        if (rc < 0 && errno != EINTR) {
            p->dead = 1;

            return -1;
        }

        if (rc > 0 && np > 1 && (pf[1].revents & (POLLOUT | POLLERR)) != 0) {
            ssize_t n = write(p->in, p->obuf + p->opos, p->olen - p->opos);

            if (n > 0) {
                p->opos += lsz(n);
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                p->dead = 1;

                return -1;
            }

            if (p->opos >= p->olen) {
                p->opos = 0;
                p->olen = 0;
            }
        }

        if (rc > 0 && (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            char tmp[4096];
            ssize_t n = read(p->out, tmp, sizeof tmp);

            if (n > 0) {
                grow(&p->ibuf, &p->icap, p->ilen + lsz(n) + 1);
                memcpy(p->ibuf + p->ilen, tmp, lsz(n));
                p->ilen += lsz(n);
                parse_input(p);
            } else if (n == 0) {
                p->dead = 1;
            } else if (errno != EAGAIN && errno != EINTR) {
                p->dead = 1;
            }
        }

        if (now_ms() >= deadline) {
            return p->nans - p->ahead;
        }
    }
}


/* Put bytes in the queue of the output. */
static void
queue_out(SpellProc *p, const char *s, size_t n)
{
    grow(&p->obuf, &p->ocap, p->olen + n + 1);
    memcpy(p->obuf + p->olen, s, n);
    p->olen += n;
}


int
spell_proc_ask(SpellProc *p, const char *line)
{
    if (p->dead != 0) {
        return -1;
    }

    queue_out(p, "^", 1);
    queue_out(p, line, strlen(line));
    queue_out(p, "\n", 1);

    return 0;
}


void
spell_proc_accept(SpellProc *p, const char *word)
{
    char u[SPELL_MAXWORD * 3 + 2];

    if (p->dead != 0 || word[0] == '\0') {
        return;
    }

    spell_to_utf8(word, u, sizeof u);
    queue_out(p, "@", 1);
    queue_out(p, u, strlen(u));
    queue_out(p, "\n", 1);
}


/* Parse one line of an answer: & or ? word count offset: s1, s2 ... or #. */
static int
parse_miss(const char *ln, size_t n, SpellMiss *m)
{
    char buf[1024];
    char *s;
    char *colon;

    if (n == 0 || n >= sizeof buf || (ln[0] != '&' && ln[0] != '?' &&
                                       ln[0] != '#')) {
        return -1;
    }

    memcpy(buf, ln, n);
    buf[n] = '\0';
    memset(m, 0, sizeof *m);
    s = buf + 1;

    while (*s == ' ') {
        s++;
    }

    char *sp = strchr(s, ' ');

    if (sp == NULL) {
        return -1;
    }

    *sp = '\0';
    str_copy(m->orig, sizeof m->orig, s);
    s = sp + 1;

    if (ln[0] != '#') {
        /* The count of the suggestions comes first. */
        strtol(s, &s, 10);
    }

    m->offset = atoi(s) - 1;

    if (m->offset < 0) {
        m->offset = 0;
    }

    colon = strchr(s, ':');

    if (colon != NULL) {
        s = colon + 1;

        while (*s != '\0' && m->nsug < SPELL_MAXSUG) {
            while (*s == ' ' || *s == ',') {
                s++;
            }

            size_t k = strcspn(s, ",");
            size_t e = k;

            while (e > 0 && s[e - 1] == ' ') {
                e--;
            }

            if (e > 0) {
                copy_n(m->sug[m->nsug], sizeof m->sug[m->nsug], s, e);
                m->nsug++;
            }

            s += k;
        }
    }

    return 0;
}


int
spell_proc_answer(SpellProc *p, SpellMiss *m, int max)
{
    if (p->ahead >= p->nans) {
        return -1;
    }

    const char *a = p->ans[p->ahead];
    int n = 0;

    while (*a != '\0') {
        size_t len = strcspn(a, "\n");

        if (n < max && parse_miss(a, len, &m[n]) == 0) {
            n++;
        }

        a += len;

        if (*a == '\n') {
            a++;
        }
    }

    free(p->ans[p->ahead]);
    p->ans[p->ahead] = NULL;
    p->ahead++;

    if (p->ahead >= p->nans) {
        p->ahead = 0;
        p->nans = 0;
    }

    return n;
}


int
spell_proc_check_word(SpellProc *p, const char *word, SpellMiss *m)
{
    char u[SPELL_MAXWORD * 3 + 2];
    int rc;
    long end = now_ms() + 10000;

    spell_to_utf8(word, u, sizeof u);

    if (spell_proc_ask(p, u) != 0) {
        return -1;
    }

    do {
        rc = spell_proc_pump(p, 200);
    } while (rc == 0 && now_ms() < end);

    if (rc <= 0) {
        return -1;
    }

    int n = spell_proc_answer(p, m, 1);

    return n > 0 ? 0 : 1;
}


void
spell_proc_close(SpellProc *p)
{
    if (p == NULL) {
        return;
    }

    close(p->in);
    close(p->out);

    /* The checker ends when its input ends; the signal is for one that
       does not. */
    for (int i = 0; i < 20; i++) {
        if (waitpid(p->pid, NULL, WNOHANG) != 0) {
            p->pid = 0;
            break;
        }

        usleep(10000);
    }

    if (p->pid > 0) {
        kill(p->pid, SIGKILL);
        waitpid(p->pid, NULL, 0);
    }

    for (int i = p->ahead; i < p->nans; i++) {
        free(p->ans[i]);
    }

    free(p->ans);
    free(p->cur);
    free(p->obuf);
    free(p->ibuf);
    free(p);
}


/* The last part of a path. */
static const char *
base_name(const char *path)
{
    const char *sl = strrchr(path, '/');

    return sl != NULL ? sl + 1 : path;
}


/* Turns an upper case letter into a lower case one; other characters stay. */
static char
lower_letter(char c)
{
    static const char lower[] = "abcdefghijklmnopqrstuvwxyz";

    if (c >= 'A' && c <= 'Z') {
        return lower[c - 'A'];
    }

    return c;
}


/* True for a lower case letter. */
static int
is_lower(char c)
{
    return c >= 'a' && c <= 'z';
}


int
spell_dictionary_package(const char *prog, const char *lang, char *out,
                         size_t cap)
{
    const char *b = base_name(prog);
    const char *rest;
    char language[4];
    char region[3];
    size_t n = 0;
    size_t r = 0;
    int is_aspell = strcmp(b, "aspell") == 0;
    int is_hunspell = strcmp(b, "hunspell") == 0;
    int with_region = 0;

    out[0] = '\0';

    if (is_aspell == 0 && is_hunspell == 0) {
        return 0;
    }

    while (n < 3 && is_lower(lang[n])) {
        language[n] = lang[n];
        n++;
    }

    language[n] = '\0';

    if (n < 2) {
        return 0;
    }

    region[0] = '\0';
    rest = lang + n;

    if (*rest == '_' || *rest == '-') {
        rest++;

        while (r < 2 && lower_letter(rest[r]) >= 'a' &&
               lower_letter(rest[r]) <= 'z') {
            region[r] = lower_letter(rest[r]);
            r++;
        }

        region[r] = '\0';

        if (r != 2 || rest[r] != '\0') {
            return 0;
        }
    } else if (*rest != '\0') {
        return 0;
    }

    /* Debian names the dictionaries of these languages by the region. */
    if (is_hunspell != 0) {
        with_region = strcmp(language, "en") == 0 ||
                      strcmp(language, "de") == 0 ||
                      strcmp(language, "pt") == 0;
    } else {
        with_region = strcmp(language, "pt") == 0;
    }

    if (with_region != 0 && region[0] == '\0') {
        const char *dflt = "us";

        if (strcmp(language, "de") == 0) {
            dflt = "de";
        } else if (strcmp(language, "pt") == 0) {
            dflt = "br";
        }

        snprintf(region, sizeof region, "%s", dflt);
    }

    if (with_region != 0) {
        snprintf(out, cap, "%s-%s-%s", is_aspell != 0 ? "aspell" : "hunspell",
                 language, region);
    } else {
        snprintf(out, cap, "%s-%s", is_aspell != 0 ? "aspell" : "hunspell",
                 language);
    }

    return 1;
}


SpellProc *
spell_proc_open(const char *lang, char *err, size_t errcap)
{
    char prog[PATH_MAX];
    int toc[2];
    int fromc[2];
    pid_t pid;

    if (spell_find_program(prog, sizeof prog) != 0) {
        snprintf(err, errcap,
                 "No spelling checker was found.  Install the packages "
                 "hunspell and hunspell-en-us (or aspell and aspell-en); "
                 "the install script of Strata installs them.  Another "
                 "program can be named in the variable STRATA_SPELL.");

        return NULL;
    }

    signal(SIGPIPE, SIG_IGN);

    if (pipe(toc) != 0) {
        snprintf(err, errcap, "Cannot make a pipe: %s", strerror(errno));

        return NULL;
    }

    if (pipe(fromc) != 0) {
        close(toc[0]);
        close(toc[1]);
        snprintf(err, errcap, "Cannot make a pipe: %s", strerror(errno));

        return NULL;
    }

    pid = fork();

    if (pid < 0) {
        close(toc[0]);
        close(toc[1]);
        close(fromc[0]);
        close(fromc[1]);
        snprintf(err, errcap, "Cannot start %s: %s", prog, strerror(errno));

        return NULL;
    }

    if (pid == 0) {
        const char *b = base_name(prog);
        char *argv[8];
        int n = 0;
        char opt_a[] = "-a";
        char opt_d[] = "-d";
        char opt_enc[] = "--encoding=utf-8";
        char opt_i[] = "-i";
        char opt_u8[] = "utf-8";
        char langbuf[64];
        int dn;

        str_copy(langbuf, sizeof langbuf, lang);
        dup2(toc[0], 0);
        dup2(fromc[1], 1);
        dn = open("/dev/null", O_WRONLY);

        if (dn >= 0) {
            dup2(dn, 2);
        }

        for (int fd = 3; fd < 64; fd++) {
            close(fd);
        }

        argv[n++] = prog;
        argv[n++] = opt_a;

        if (strcmp(b, "hunspell") == 0) {
            argv[n++] = opt_i;
            argv[n++] = opt_u8;
        }

        argv[n++] = opt_d;
        argv[n++] = langbuf;

        if (strcmp(b, "aspell") == 0) {
            argv[n++] = opt_enc;
        }

        argv[n] = NULL;
        sp_child_env(prog);
        execv(prog, argv);
        _exit(127);
    }

    close(toc[0]);
    close(fromc[1]);

    SpellProc *p = calloc(1, sizeof *p);

    if (p == NULL) {
        abort();
    }

    p->pid = pid;
    p->in = toc[1];
    p->out = fromc[0];
    set_nonblock(p->in);
    set_nonblock(p->out);

    /* The terse mode: only the words that are wrong are answered. */
    queue_out(p, "!\n", 2);

    /* The first line is the greeting; without it the dictionary is not
       there or the program could not start. */
    long end = now_ms() + 8000;

    while (p->banner == 0 && p->dead == 0 && now_ms() < end) {
        spell_proc_pump(p, 100);
    }

    if (p->banner == 0) {
        char pkg[64];

        if (spell_dictionary_package(prog, lang, pkg, sizeof pkg) != 0) {
            snprintf(err, errcap,
                     "The spelling checker %s did not start for the "
                     "language %s: its dictionary is not installed.  "
                     "Install the package %s, or change the Dictionary "
                     "Language in Setup of the Document Manager.",
                     base_name(prog), lang, pkg);
        } else {
            snprintf(err, errcap,
                     "The spelling checker %s did not start for the "
                     "language %s: its dictionary is not installed.  "
                     "Install the dictionary of that language for %s, or "
                     "change the Dictionary Language in Setup of the "
                     "Document Manager.",
                     base_name(prog), lang, base_name(prog));
        }

        spell_proc_close(p);

        return NULL;
    }

    return p;
}


/* ================================================================== walk */
#define SP_DEPTH 4

/* One level of the walk: the document, or the text of a note. */
typedef struct {
    Doc *doc;
    int owned; /* the document was made for a note and is freed with it */
    int item;
    int col;
    int tok; /* where the next piece of text begins */
    int hi; /* the last item to check */
    int p_tok; /* the marker in the level below that holds this text */
} Frame;

/* A word of the piece of text that is being checked. */
typedef struct {
    SpellWord w;
    int cat; /* 0 asked of the checker, 1 flagged, 2 passed */
    int line; /* the line of the request, or -1 */
    int off; /* byte offset in that line */
    int miss; /* the number of its miss in the pool, or -1 */
} CWord;

typedef struct {
    int first; /* the first word of the line */
} LineInfo;

enum {
    PH_NEXT, /* the next piece of text is to be taken */
    PH_WAIT, /* the lines are asked and the answers are awaited */
    PH_POP /* the words of the piece are given one by one */
};

struct SpellRun {
    Frame fr[SP_DEPTH];
    int depth;
    SpellProc *proc;
    SpellDict *dict;
    int autocorrect;
    int ac_only; /* only the auto corrections are made */
    char **acc; /* the words that were accepted */
    int nacc;
    int capacc;
    char **acf; /* the auto corrections: from, to */
    char **act;
    int nac;
    int capac;
    int replaced;
    int phase;
    int ck_level; /* the piece of text: the level, item and column */
    int ck_item;
    int ck_col;
    int ck_end; /* the token where the piece ends */
    int ck_nested; /* the marker with a note that ends it, or -1 */
    CWord *cw;
    int ncw;
    int capcw;
    LineInfo *li;
    int nli;
    int capli;
    int nli_got;
    SpellMiss *pool;
    int npool;
    int cappool;
    SpellMiss *tmp; /* the answers being read */
    int qpos; /* the next word of cw to give */
    int hit_w; /* the index in cw of the last hit, or -1 */
    int *tk_item;
    int *tk_col;
    int *tk_tok;
};

#define TMP_MISSES 64


static int
nested_marker(const Tok *k)
{
    const MInfo *mi;

    if (k->kind != TK_MARKER) {
        return 0;
    }

    mi = marker_info(tok_mtype(k));

    return mi != NULL && mi->nested != 0 && mi->type != 9;
}


static void
push_str(char ***v, int *n, int *cap, const char *s)
{
    if (*n >= *cap) {
        *cap = *cap == 0 ? 16 : *cap * 2;
        *v = realloc(*v, usz(*cap) * sizeof **v);

        if (*v == NULL) {
            abort();
        }
    }

    size_t len = strlen(s);

    (*v)[*n] = malloc(len + 1);

    if ((*v)[*n] == NULL) {
        abort();
    }

    memcpy((*v)[*n], s, len + 1);
    (*n)++;
}


static int
find_str(char **v, int n, const char *s)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(v[i], s) == 0) {
            return i;
        }
    }

    return -1;
}


/* The text of the document that holds the words just checked. */
static Text *
frame_text(const SpellRun *r, int level, int item, int col)
{
    return &r->fr[level].doc->items[item].blk->cols[col].text;
}


SpellRun *
spellrun_new(Doc *d, int lo, int hi, SpellProc *proc, SpellDict *dict,
             int autocorrect)
{
    SpellRun *r = calloc(1, sizeof *r);

    if (r == NULL) {
        abort();
    }

    r->fr[0].doc = d;
    r->fr[0].item = lo < 0 ? 0 : lo;
    r->fr[0].hi = hi;
    r->depth = 1;
    r->proc = proc;
    r->dict = dict;
    r->autocorrect = autocorrect;
    r->hit_w = -1;
    r->tmp = malloc(TMP_MISSES * sizeof *r->tmp);

    if (r->tmp == NULL) {
        abort();
    }

    if (proc != NULL && dict != NULL) {
        for (int i = 0; i < dict->n; i++) {
            spell_proc_accept(proc, dict->w[i]);
        }
    }

    return r;
}


/* Give a note's text back to the marker that holds it. */
static void
write_back(SpellRun *r)
{
    Frame *c = &r->fr[r->depth - 1];
    Frame *p = &r->fr[r->depth - 2];
    Doc *nd = c->doc;
    static const char blank[] = "S\r\nT\x1d\x00\r\n";
    size_t len;
    uint8_t *all = doc_serialize(nd, &len);
    size_t start = 0;
    Text *pt = frame_text(r, r->depth - 2, p->item, p->col);
    Tok *mk = &pt->v[c->p_tok];
    MPayload mp;
    size_t olen;
    uint8_t *out;

    for (int i = 0; i < nd->header.n; i++) {
        start += usz(nd->header.v[i].n) + 2;
    }

    size_t end = len - nd->tail_len;
    size_t n = end > start ? end - start : 0;

    /* A note that was empty stays empty. */
    if (n == sizeof blank - 1 && memcmp(all + start, blank, n) == 0) {
        n = 0;
    }

    mp_parse(&mp, marker_info(tok_mtype(mk)), tok_mdata(mk), tok_mlen(mk));
    free(mp.nested);
    mp.nested = malloc(n + 1);

    if (mp.nested == NULL) {
        abort();
    }

    memcpy(mp.nested, all + start, n);
    mp.nested_len = n;
    out = mp_build(&mp, &olen);
    tok_marker_set(mk, out, olen);
    free(out);
    mp_free(&mp);
    free(all);

    if (p->doc->items[p->item].blk != NULL) {
        p->doc->items[p->item].blk->dirty = 1;
    }

    doc_touch(p->doc);
}


static void
pop_frame(SpellRun *r)
{
    Frame *c = &r->fr[r->depth - 1];

    if (c->owned != 0) {
        if (c->doc->modified != 0) {
            write_back(r);
        }

        doc_free(c->doc);
    }

    r->depth--;
}


/* The document of the note that marker mk holds, or NULL when empty. */
static Doc *
load_nested(const Tok *mk)
{
    static const char head[] = DOC_FIRST_RECORD "\r\nMR1006\r\n*B0\r\n";
    static const char foot[] = "*E\r\n";
    MPayload mp;
    char err[96];
    Doc *nd = NULL;

    mp_parse(&mp, marker_info(tok_mtype(mk)), tok_mdata(mk), tok_mlen(mk));

    if (mp.nested_len > 0) {
        size_t hl = sizeof head - 1;
        size_t fl = sizeof foot - 1;
        uint8_t *buf = malloc(hl + mp.nested_len + fl + 1);

        if (buf == NULL) {
            abort();
        }

        memcpy(buf, head, hl);
        memcpy(buf + hl, mp.nested, mp.nested_len);
        memcpy(buf + hl + mp.nested_len, foot, fl);
        nd = doc_load_mem(buf, hl + mp.nested_len + fl, err, sizeof err);
        free(buf);
    }

    mp_free(&mp);

    return nd;
}


static void
reset_chunk(SpellRun *r)
{
    r->ncw = 0;
    r->nli = 0;
    r->nli_got = 0;
    r->npool = 0;
    r->qpos = 0;
    r->phase = PH_NEXT;
    r->hit_w = -1;
}


/* The piece of text is done: go on after it, into its note if it has one. */
static void
end_chunk(SpellRun *r)
{
    Frame *f = &r->fr[r->ck_level];
    int m = r->ck_nested;

    reset_chunk(r);

    if (m < 0) {
        f->tok = r->ck_end;

        return;
    }

    f->tok = m + 1;

    if (r->depth < SP_DEPTH) {
        Text *t = frame_text(r, r->ck_level, r->ck_item, r->ck_col);
        Doc *nd = load_nested(&t->v[m]);

        if (nd != NULL) {
            Frame *c = &r->fr[r->depth];

            memset(c, 0, sizeof *c);
            c->doc = nd;
            c->owned = 1;
            c->hi = nd->nitems - 1;
            c->p_tok = m;
            r->depth++;
        }
    }
}


static void
add_word(SpellRun *r, const SpellWord *w, int cat)
{
    if (r->ncw >= r->capcw) {
        r->capcw = r->capcw == 0 ? 64 : r->capcw * 2;
        r->cw = realloc(r->cw, usz(r->capcw) * sizeof *r->cw);

        if (r->cw == NULL) {
            abort();
        }
    }

    CWord *c = &r->cw[r->ncw++];

    c->w = *w;
    c->cat = cat;
    c->line = -1;
    c->off = 0;
    c->miss = -1;
}


static int
find_ac(const SpellRun *r, const char *word)
{
    return find_str(r->acf, r->nac, word);
}


/* How a word is treated: 0 ask the checker, 1 flag it, 2 pass it. */
static int
classify(const SpellRun *r, const SpellWord *w)
{
    if (r->ac_only != 0) {
        return find_ac(r, w->text) >= 0 ? 1 : 2;
    }

    if (find_ac(r, w->text) >= 0) {
        return 1;
    }

    if (spell_dict_has(r->dict, w->text) != 0 ||
        find_str(r->acc, r->nacc, w->text) >= 0) {
        return 2;
    }

    if (spell_has_digit(w->text) != 0) {
        return 1;
    }

    return r->proc != NULL ? 0 : 2;
}


/* Ask the checker about the words of the piece, in lines of moderate size. */
static void
ask_lines(SpellRun *r)
{
    char *line = NULL;
    size_t cap = 0;
    size_t len = 0;
    int cur = -1;

    for (int i = 0; i < r->ncw; i++) {
        CWord *c = &r->cw[i];
        char u[SPELL_MAXWORD * 3 + 2];

        if (c->cat != 0) {
            continue;
        }

        spell_to_utf8(c->w.text, u, sizeof u);

        if (cur >= 0 && len + strlen(u) + 1 > 1500) {
            spell_proc_ask(r->proc, line);
            len = 0;
            cur = -1;
        }

        if (cur < 0) {
            if (r->nli >= r->capli) {
                r->capli = r->capli == 0 ? 16 : r->capli * 2;
                r->li = realloc(r->li, usz(r->capli) * sizeof *r->li);

                if (r->li == NULL) {
                    abort();
                }
            }

            cur = r->nli++;
            r->li[cur].first = i;
        }

        grow(&line, &cap, len + strlen(u) + 2);

        if (len > 0) {
            line[len++] = ' ';
        }

        c->line = cur;
        c->off = isz(len);
        memcpy(line + len, u, strlen(u) + 1);
        len += strlen(u);
    }

    if (cur >= 0) {
        spell_proc_ask(r->proc, line);
    }

    free(line);
}


/* Take the next piece of text; 0 when there is none. */
static int
begin_chunk(SpellRun *r)
{
    for (;;) {
        Frame *f = &r->fr[r->depth - 1];

        if (f->item > f->hi || f->item >= f->doc->nitems) {
            if (r->depth == 1) {
                return 0;
            }

            pop_frame(r);
            continue;
        }

        Item *it = &f->doc->items[f->item];

        if (it->type != IT_BLOCK || it->blk == NULL ||
            f->col >= it->blk->ncols) {
            f->item++;
            f->col = 0;
            f->tok = 0;
            continue;
        }

        Text *t = &it->blk->cols[f->col].text;

        if (f->tok >= t->n) {
            f->col++;
            f->tok = 0;
            continue;
        }

        int m = -1;

        for (int i = f->tok; i < t->n; i++) {
            if (nested_marker(&t->v[i]) != 0) {
                m = i;
                break;
            }
        }

        int to = m >= 0 ? m : t->n;
        SpellWord w;
        int pos = f->tok;

        reset_chunk(r);
        r->ck_level = r->depth - 1;
        r->ck_item = f->item;
        r->ck_col = f->col;
        r->ck_end = to;
        r->ck_nested = m;

        while (spell_find_word(t, pos, to, &w) != 0) {
            pos = w.end;

            if (w.too_long == 0) {
                add_word(r, &w, classify(r, &w));
            }
        }

        if (r->proc != NULL && r->ac_only == 0) {
            ask_lines(r);
        }

        r->phase = r->nli > 0 ? PH_WAIT : PH_POP;

        return 1;
    }
}


/* Which word of the request line l does the offset point to? */
static int
word_of_offset(const SpellRun *r, int l, int off)
{
    int best = -1;

    for (int i = r->li[l].first; i < r->ncw; i++) {
        const CWord *c = &r->cw[i];

        if (c->line > l) {
            break;
        }

        if (c->line != l) {
            continue;
        }

        if (c->off == off) {
            return i;
        }

        if (c->off <= off) {
            best = i;
        }
    }

    return best;
}


/* Read the answers that are complete; 1 when all are in, -1 on error. */
static int
collect(SpellRun *r, int budget_ms)
{
    int rc = spell_proc_pump(r->proc, budget_ms);

    while (rc > 0) {
        int l = r->nli_got;
        int n = spell_proc_answer(r->proc, r->tmp, TMP_MISSES);

        if (n < 0) {
            break;
        }

        for (int k = 0; k < n; k++) {
            int wi = word_of_offset(r, l, r->tmp[k].offset);

            if (wi < 0 || r->cw[wi].miss >= 0) {
                continue;
            }

            if (r->npool >= r->cappool) {
                r->cappool = r->cappool == 0 ? 16 : r->cappool * 2;
                r->pool = realloc(r->pool, usz(r->cappool) * sizeof *r->pool);

                if (r->pool == NULL) {
                    abort();
                }
            }

            r->pool[r->npool] = r->tmp[k];
            r->cw[wi].miss = r->npool++;
        }

        r->nli_got++;
        rc = r->nli_got < r->nli ? spell_proc_pump(r->proc, 0) : 0;
    }

    if (rc < 0) {
        return -1;
    }

    return r->nli_got >= r->nli ? 1 : 0;
}


/* Replace the word wi of the piece of text; the later words move. */
static void
do_replace(SpellRun *r, int wi, const char *s)
{
    Text *t = frame_text(r, r->ck_level, r->ck_item, r->ck_col);
    Doc *d = r->fr[r->ck_level].doc;
    SpellWord old = r->cw[wi].w;
    int delta = spell_replace_word(t, &old, s);

    for (int i = wi + 1; i < r->ncw; i++) {
        r->cw[i].w.start += delta;
        r->cw[i].w.end += delta;
    }

    r->ck_end += delta;

    if (r->ck_nested >= 0) {
        r->ck_nested += delta;
    }

    if (r->ck_level == 0 && r->tk_item != NULL &&
        *r->tk_item == r->ck_item && *r->tk_col == r->ck_col) {
        if (*r->tk_tok >= old.end) {
            *r->tk_tok += delta;
        } else if (*r->tk_tok > old.start) {
            *r->tk_tok = old.start;
        }
    }

    if (d->items[r->ck_item].blk != NULL) {
        d->items[r->ck_item].blk->dirty = 1;
    }

    doc_touch(d);
    r->replaced++;
}


int
spell_whole_words(const char *word, int word_off, const SpellMiss *m,
                  char out[][SPELL_MAXWORD + 1], int max)
{
    char wu[SPELL_MAXWORD * 3 + 2];
    int n = 0;

    spell_to_utf8(word, wu, sizeof wu);

    size_t wl = strlen(wu);
    size_t ol = strlen(m->orig);
    size_t rel = usz(m->offset - word_off);

    for (int i = 0; i < m->nsug && n < max; i++) {
        char full[SPELL_MAXWORD * 4 + 2 * SPELL_MAXWORD * 3 + 2];
        char cp[SPELL_MAXWORD * 4 + 1];

        if (rel == 0 && ol == wl) {
            str_copy(full, sizeof full, m->sug[i]);
        } else if (rel + ol <= wl) {
            char head[SPELL_MAXWORD * 3 + 2];

            copy_n(head, sizeof head, wu, rel);
            snprintf(full, sizeof full, "%s%s%s", head, m->sug[i],
                     wu + rel + ol);
        } else {
            continue;
        }

        spell_from_utf8(full, cp, sizeof cp);

        if (strcmp(cp, word) == 0 || strlen(cp) > SPELL_MAXWORD ||
            strchr(cp, '?') != NULL) {
            continue;
        }

        str_copy(out[n++], SPELL_MAXWORD + 1, cp);
    }

    return n;
}


/* The hit that a word of the piece of text makes. */
static void
fill_hit(const SpellRun *r, const CWord *c, SpellHit *h)
{
    memset(h, 0, sizeof *h);
    str_copy(h->word, sizeof h->word, c->w.text);
    h->in_note = r->ck_level > 0;
    h->no_guess = c->cat == 1 && c->miss < 0;

    if (c->miss >= 0) {
        h->nsug = spell_whole_words(c->w.text, c->off, &r->pool[c->miss],
                                    h->sug, SPELL_MAXSUG);
    }
}


int
spellrun_next(SpellRun *r, int budget_ms, SpellHit *hit)
{
    long deadline = now_ms() + budget_ms;

    r->hit_w = -1;

    for (;;) {
        if (r->phase == PH_NEXT) {
            if (begin_chunk(r) == 0) {
                return SR_DONE;
            }
        }

        if (r->phase == PH_WAIT) {
            long left = deadline - now_ms();
            int rc = collect(r, left > 50 ? 50 : ms_int(left));

            if (rc < 0) {
                return SR_ERROR;
            }

            if (rc == 0) {
                if (now_ms() >= deadline) {
                    return SR_BUSY;
                }

                continue;
            }

            r->phase = PH_POP;
        }

        while (r->qpos < r->ncw) {
            int wi = r->qpos++;
            CWord *c = &r->cw[wi];
            int k;

            if (c->cat == 2 || (c->cat == 0 && c->miss < 0)) {
                continue;
            }

            if (r->ac_only == 0 &&
                (spell_dict_has(r->dict, c->w.text) != 0 ||
                 find_str(r->acc, r->nacc, c->w.text) >= 0)) {
                continue;
            }

            k = find_ac(r, c->w.text);

            if (k >= 0) {
                do_replace(r, wi, r->act[k]);
                continue;
            }

            fill_hit(r, c, hit);
            r->hit_w = wi;

            return SR_HIT;
        }

        end_chunk(r);

        if (now_ms() >= deadline) {
            return SR_BUSY;
        }
    }
}


/* Remember that the word from is to be replaced by to when it comes again. */
static void
add_ac(SpellRun *r, const char *from, const char *to)
{
    if (r->nac >= r->capac) {
        r->capac = r->capac == 0 ? 16 : r->capac * 2;
        r->acf = realloc(r->acf, usz(r->capac) * sizeof *r->acf);
        r->act = realloc(r->act, usz(r->capac) * sizeof *r->act);

        if (r->acf == NULL || r->act == NULL) {
            abort();
        }
    }

    r->acf[r->nac] = malloc(strlen(from) + 1);
    r->act[r->nac] = malloc(strlen(to) + 1);

    if (r->acf[r->nac] == NULL || r->act[r->nac] == NULL) {
        abort();
    }

    memcpy(r->acf[r->nac], from, strlen(from) + 1);
    memcpy(r->act[r->nac], to, strlen(to) + 1);
    r->nac++;
}


void
spellrun_replace(SpellRun *r, const char *word)
{
    char from[SPELL_MAXWORD + 1];

    if (r->hit_w < 0) {
        return;
    }

    str_copy(from, sizeof from, r->cw[r->hit_w].w.text);
    do_replace(r, r->hit_w, word);

    if (r->autocorrect != 0 && strcmp(from, word) != 0 &&
        find_ac(r, from) < 0) {
        add_ac(r, from, word);
    }
}


void
spellrun_accept(SpellRun *r)
{
    if (r->hit_w >= 0) {
        push_str(&r->acc, &r->nacc, &r->capacc, r->cw[r->hit_w].w.text);
    }
}


int
spellrun_define(SpellRun *r, char *err, size_t errcap)
{
    if (r->hit_w < 0 || r->dict == NULL) {
        return 0;
    }

    return spell_dict_add(r->dict, r->cw[r->hit_w].w.text, err, errcap);
}


int
spellrun_pending_autocorrect(const SpellRun *r)
{
    return r->nac > 0;
}


void
spellrun_finish_autocorrect(SpellRun *r)
{
    SpellHit h;
    int rc;

    /* The text goes on after the word that the user has just seen. */
    if (r->hit_w >= 0) {
        Frame *f = &r->fr[r->ck_level];

        f->tok = r->cw[r->hit_w].w.end;
    }

    reset_chunk(r);
    r->ac_only = 1;

    do {
        rc = spellrun_next(r, 1000, &h);
    } while (rc == SR_BUSY || rc == SR_HIT);
}


int
spellrun_replaced(const SpellRun *r)
{
    return r->replaced;
}


void
spellrun_track(SpellRun *r, int *item, int *col, int *tok)
{
    r->tk_item = item;
    r->tk_col = col;
    r->tk_tok = tok;
}


void
spellrun_free(SpellRun *r)
{
    if (r == NULL) {
        return;
    }

    /* A note that is half done is given back to its marker. */
    while (r->depth > 1) {
        pop_frame(r);
    }

    for (int i = 0; i < r->nacc; i++) {
        free(r->acc[i]);
    }

    for (int i = 0; i < r->nac; i++) {
        free(r->acf[i]);
        free(r->act[i]);
    }

    free(r->acc);
    free(r->acf);
    free(r->act);
    free(r->cw);
    free(r->li);
    free(r->pool);
    free(r->tmp);
    free(r);
}


const char *
spellrun_section(const SpellRun *r)
{
    return r->ck_level > 0 ? "Body (Note)" : "Body";
}


/* ================================================================ context */
/* Put the text of tokens from to to of t in out as plain characters. */
static void
plain_put(const Text *t, int from, int to, char *out, size_t cap, size_t *o,
          int hs_tok, int he_tok, int *hs, int *he)
{
    for (int i = from; i < to && i < t->n; i++) {
        const Tok *k = &t->v[i];
        char ch = '\0';

        if (i == hs_tok) {
            *hs = isz(*o);
        }

        if (i == he_tok) {
            *he = isz(*o);
        }

        switch (k->kind) {
            case TK_CHAR:
                ch = k->a < ' ' ? ' ' : chr(k->a);
                break;

            case TK_TAB:
                ch = 0x10;
                break;

            case TK_MARKER:
                ch = 0x04;
                break;

            case TK_NL:
                /* The line end that closes a block is not drawn. */
                ch = i + 1 < t->n ? '\n' : '\0';
                break;

            case TK_CTRL:
                /* A no-break space is shown as on the screen of the text. */
                if (k->a == NBSP_CTRL) {
                    ch = chr(NBSP_SHOWN);
                }

                break;

            case TK_ATTR:
            default:
                break;
        }

        if (ch != '\0' && *o + 2 < cap) {
            out[(*o)++] = ch;
        }
    }

    if (to == he_tok) {
        *he = isz(*o);
    }
}


/* The start of the line that holds token tok, looking back 2000 tokens. */
static int
line_start(const Text *t, int tok)
{
    int lim = tok - 2000 < 0 ? 0 : tok - 2000;
    int i = tok;

    while (i > lim && t->v[i - 1].kind != TK_NL) {
        i--;
    }

    return i;
}


void
spellrun_context(const SpellRun *r, char *out, size_t cap, int *hs, int *he,
                 int maxblocks)
{
    size_t o = 0;

    out[0] = '\0';
    *hs = 0;
    *he = 0;

    if (r->hit_w < 0 || cap < 8) {
        return;
    }

    const CWord *c = &r->cw[r->hit_w];
    const Doc *d = r->fr[r->ck_level].doc;
    const Text *t = frame_text(r, r->ck_level, r->ck_item, r->ck_col);
    int ls = line_start(t, c->w.start);

    /* The line before the line of the word comes first. */
    if (ls > 0) {
        ls = line_start(t, ls - 1);
    }

    plain_put(t, ls, t->n, out, cap, &o, c->w.start, c->w.end, hs, he);

    int blocks = 1;

    for (int ci = r->ck_col + 1; ci < d->items[r->ck_item].blk->ncols; ci++) {
        if (o + 4 < cap) {
            out[o++] = ' ';
            out[o++] = ' ';
        }

        plain_put(&d->items[r->ck_item].blk->cols[ci].text, 0,
                  d->items[r->ck_item].blk->cols[ci].text.n, out, cap, &o, -1,
                  -1, hs, he);
    }

    for (int i = r->ck_item + 1; i < d->nitems && blocks < maxblocks &&
                                 o + 8 < cap;
         i++) {
        if (d->items[i].type != IT_BLOCK || d->items[i].blk == NULL) {
            continue;
        }

        out[o++] = '\n';
        blocks++;

        for (int ci = 0; ci < d->items[i].blk->ncols; ci++) {
            if (ci > 0 && o + 4 < cap) {
                out[o++] = ' ';
                out[o++] = ' ';
            }

            plain_put(&d->items[i].blk->cols[ci].text, 0,
                      d->items[i].blk->cols[ci].text.n, out, cap, &o, -1, -1,
                      hs, he);
        }
    }

    out[o] = '\0';
}


/* The tokens of the line of the last hit, as plain characters. */
static int
line_range(const SpellRun *r, int *from, int *to)
{
    if (r->hit_w < 0) {
        return -1;
    }

    const Text *t = frame_text(r, r->ck_level, r->ck_item, r->ck_col);
    int s = line_start(t, r->cw[r->hit_w].w.start);
    int e = r->cw[r->hit_w].w.end;

    while (e < t->n && t->v[e].kind != TK_NL) {
        e++;
    }

    *from = s;
    *to = e;

    return 0;
}


/* The plain line, and for every character the token it came from. */
static int
line_chars(const Text *t, int from, int to, char *out, int *map, int cap)
{
    int n = 0;

    for (int i = from; i < to && n + 1 < cap; i++) {
        const Tok *k = &t->v[i];

        if (k->kind == TK_CHAR) {
            out[n] = k->a < ' ' ? ' ' : chr(k->a);
        } else if (k->kind == TK_TAB) {
            out[n] = 0x10;
        } else if (k->kind == TK_MARKER) {
            out[n] = 0x04;
        } else {
            continue;
        }

        map[n++] = i;
    }

    out[n] = '\0';

    return n;
}


void
spellrun_line(const SpellRun *r, char *out, size_t cap)
{
    int from;
    int to;

    out[0] = '\0';

    if (line_range(r, &from, &to) != 0) {
        return;
    }

    const Text *t = frame_text(r, r->ck_level, r->ck_item, r->ck_col);
    int *map = malloc(sizeof *map * usz(to - from + 1));

    if (map == NULL) {
        abort();
    }

    line_chars(t, from, to, out, map, isz(cap));
    free(map);
}


int
spellrun_set_line(SpellRun *r, const char *line)
{
    int from;
    int to;

    if (line_range(r, &from, &to) != 0) {
        return -1;
    }

    Text *t = frame_text(r, r->ck_level, r->ck_item, r->ck_col);
    Doc *d = r->fr[r->ck_level].doc;
    int cap = to - from + 2;
    char *old = malloc(usz(cap));
    int *map = malloc(sizeof *map * usz(cap));

    if (old == NULL || map == NULL) {
        abort();
    }

    int ol = line_chars(t, from, to, old, map, cap);
    int nl = slen(line);
    int p = 0;
    int s = 0;

    while (p < ol && p < nl && old[p] == line[p]) {
        p++;
    }

    while (s < ol - p && s < nl - p && old[ol - 1 - s] == line[nl - 1 - s]) {
        s++;
    }

    for (int i = p; i < ol - s; i++) {
        if (old[i] == 0x10 || old[i] == 0x04) {
            free(old);
            free(map);

            return -1;
        }
    }

    for (int i = p; i < nl - s; i++) {
        if (line[i] == 0x10 || line[i] == 0x04) {
            free(old);
            free(map);

            return -1;
        }
    }

    if (p == ol - s && p == nl - s) {
        free(old);
        free(map);

        return 0;
    }

    int pos = p < ol ? map[p] : to;
    int tokend = to;

    for (int i = ol - s - 1; i >= p; i--) {
        text_delete(t, map[i], 1);
        tokend--;
    }

    for (int i = p; i < nl - s; i++) {
        text_insert(t, pos + (i - p), tok_char(u8(line[i])));
        tokend++;
    }

    d->items[r->ck_item].blk->dirty = 1;
    doc_touch(d);
    r->replaced++;

    /* The rest of the line is not checked again: go on after it. */
    reset_chunk(r);
    r->fr[r->ck_level].tok = tokend;
    free(old);
    free(map);

    return 0;
}
