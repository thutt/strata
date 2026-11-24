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
 * recovertest - the recovery of a damaged document (doc_recover_mem,
 * doc_recover, doc_recovered_path in src/msdoc.c) without a terminal.
 *
 * The damaged files are made here from the documents of tests/data: cut at
 * every record boundary and in the middle of every record, with the end
 * record taken off, and with a bad record in the middle and a good end.
 * Every check is about a value that the recovery must make: the blocks that
 * come back are those of the original before the damage, by their text; a
 * whole document is not "damaged"; the file is not changed; the name is never
 * the name of the original; a file over a limit and a file that is no
 * document are still refused; the peak memory of a recovery is that of a
 * load.  The first argument is the directory of the test data.
 */
#include "../src/util.h"
#include "../src/msdoc.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
static int checks;

static void
check(const char *name, int cond)
{
    checks++;
    printf("%s %s\n", cond ? "ok  " : "FAIL", name);
    fails += !cond;
}


static uint8_t *
slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;

    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);

    if (b == NULL || fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(b);

        return NULL;
    }

    fclose(f);
    *len = (size_t)n;

    return b;
}


/* The text of block number k (0 is the first block) of a document. */
static int
block_text(const Doc *d, int k, char *out, size_t cap)
{
    int seen = 0;

    out[0] = '\0';

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        if (seen++ == k) {
            const Block *b = d->items[i].blk;
            size_t at = 0;

            for (int c = 0; c < b->ncols; c++) {
                char t[2048];

                text_plain(&b->cols[c].text, t, sizeof t);
                at += (size_t)snprintf(out + at, cap - at, "%s|", t);
            }

            return 1;
        }
    }

    return 0;
}


static int
count_blocks(const Doc *d)
{
    int n = 0;

    for (int i = 0; i < d->nitems; i++) {
        n += d->items[i].type == IT_BLOCK;
    }

    return n;
}


/* Offsets after every CR LF of a document (the record boundaries). */
static size_t *
boundaries(const uint8_t *b, size_t len, int *n)
{
    size_t *v = malloc(sizeof *v * (len / 2 + 2));

    *n = 0;

    for (size_t i = 0; i + 1 < len; i++) {
        if (b[i] == '\r' && b[i + 1] == '\n') {
            v[(*n)++] = i + 2;
        }
    }

    return v;
}


static void
test_cuts(const char *name, const uint8_t *orig, size_t len, const Doc *whole)
{
    int nb;
    size_t *bound = boundaries(orig, len, &nb);
    int bad = 0;
    int wrong_text = 0;
    int tried = 0;
    char msg[160];

    /* Every cut at a record boundary and one byte into the next record. */
    for (int k = 0; k < nb; k++) {
        for (int extra = 0; extra < 2; extra++) {
            size_t cut = bound[k] + (size_t)extra;
            DocRecovery r;
            char err[160];
            Doc *d;

            if (cut >= len - 3) {
                continue;
            }

            tried++;
            d = doc_recover_mem(orig, cut, &r, err, sizeof err);

            if (cut < 3) {
                bad += d != NULL;
                continue;
            }

            if (d == NULL || r.damaged == 0 || r.why[0] == '\0' ||
                r.bytes_read > cut || count_blocks(d) != r.blocks ||
                r.blocks > count_blocks(whole)) {
                bad++;
            }

            if (d != NULL) {
                /* The blocks that come back are the first blocks of the
                   original, by their text. */
                for (int j = 0; j < r.blocks; j++) {
                    char a[8192];
                    char b[8192];

                    block_text(d, j, a, sizeof a);
                    block_text(whole, j, b, sizeof b);

                    /* The last block may have lost the columns that were cut. */
                    if (j < r.blocks - 1 ? strcmp(a, b) != 0 :
                                           strncmp(b, a, strlen(a)) != 0) {
                        wrong_text++;
                        break;
                    }
                }

                doc_free(d);
            }
        }
    }

    free(bound);
    snprintf(msg, sizeof msg, "%s: %d cuts all recovered (%d tried)", name,
             tried - bad, tried);
    check(msg, bad == 0 && tried > 0);
    snprintf(msg, sizeof msg, "%s: the recovered blocks are the first blocks "
             "of the original, by text", name);
    check(msg, wrong_text == 0);
}


static void
test_whole(const char *name, const uint8_t *orig, size_t len)
{
    DocRecovery r;
    char err[160];
    Doc *d = doc_recover_mem(orig, len, &r, err, sizeof err);
    char msg[160];

    snprintf(msg, sizeof msg, "%s: a whole document is not damaged", name);
    check(msg, d != NULL && r.damaged == 0 && r.why[0] == '\0');
    doc_free(d);
}


/* A bad record in the middle and a good end: recover what comes before. */
static void
test_middle(const uint8_t *orig, size_t len, const Doc *whole)
{
    static const char bad[] = "\r\nBM123456789\r\n";
    size_t at = len / 4 * 3;
    uint8_t *m;
    DocRecovery r;
    char err[160];
    Doc *d;
    int ok = 1;

    /* Put the bad record at the first record boundary after the middle. */
    while (at + 1 < len && !(orig[at] == '\r' && orig[at + 1] == '\n')) {
        at++;
    }

    m = malloc(len + sizeof bad);
    memcpy(m, orig, at);
    memcpy(m + at, bad, sizeof bad - 1);
    memcpy(m + at + sizeof bad - 1, orig + at + 2, len - at - 2);
    d = doc_recover_mem(m, len + sizeof bad - 3, &r, err, sizeof err);
    check("a bad record in the middle: the part before it is recovered",
          d != NULL && r.damaged == 1 && r.blocks > 0 &&
          r.blocks < count_blocks(whole) &&
          strstr(r.why, "too many digits") != NULL);
    check("a bad record in the middle: it stops at the bad record and does "
          "not go on", d != NULL && r.bytes_read == at + 2 &&
          count_blocks(d) == r.blocks);

    if (d != NULL) {
        for (int j = 0; j < r.blocks; j++) {
            char a[8192];
            char b[8192];

            block_text(d, j, a, sizeof a);
            block_text(whole, j, b, sizeof b);
            ok &= strcmp(a, b) == 0;
        }

        doc_free(d);
    }

    check("a bad record in the middle: the blocks are those of the original",
          ok);
    free(m);
}


static void
test_refusals(void)
{
    DocRecovery r;
    char err[160];
    static const char notdoc[] = "just some text\r\nand more\r\n";
    Doc *d = doc_recover_mem((const uint8_t *)notdoc, sizeof notdoc - 1, &r,
                             err, sizeof err);

    check("a file that is not a document is refused", d == NULL);
    d = doc_recover_mem((const uint8_t *)"*R", 2, &r, err, sizeof err);
    check("a file of two bytes is refused", d == NULL);
    d = doc_recover_mem((const uint8_t *)"", 0, &r, err, sizeof err);
    check("an empty file is refused", d == NULL);
}


static void
test_limits(void)
{
    /* More blocks than the limit, then cut: the limit still holds. */
    size_t cap = 6 * 1000 * 1000;
    uint8_t *b = malloc(cap);
    size_t at = 0;
    DocRecovery r;
    char err[160];
    Doc *d;

    at += (size_t)snprintf((char *)b + at, cap - at,
                           "*R2\r\nMR1001\r\n*B0\r\nS\r\n");

    for (int i = 0; i < 510000 && at + 16 < cap; i++) {
        at += (size_t)snprintf((char *)b + at, cap - at, "B\r\nT\x1d%c\r\n",
                               0);
    }

    d = doc_recover_mem(b, at - 3, &r, err, sizeof err);
    check("more blocks than the limit are refused in recovery too",
          d == NULL && strstr(err, "too large") != NULL);
    doc_free(d);
    free(b);
}


static void
test_names(void)
{
    char dir[PATH_MAX];
    char orig[PATH_MAX + 32];
    char out[PATH_MAX + 64];
    char f[PATH_MAX + 64];
    const char *base = getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp";
    FILE *fp;

    snprintf(dir, sizeof dir, "%s/rcvXXXXXX", base);

    if (mkdtemp(dir) == NULL) {
        check("a folder for the names", 0);

        return;
    }

    snprintf(orig, sizeof orig, "%s/report.doc", dir);
    check("the name is NAME.recovered.doc beside the original",
          doc_recovered_path(orig, out, sizeof out) == 0 &&
          snprintf(f, sizeof f, "%s/report.recovered.doc", dir) > 0 &&
          strcmp(out, f) == 0);
    fp = fopen(f, "w");
    fclose(fp);
    check("NAME.recovered.doc taken: NAME.recovered2.doc",
          doc_recovered_path(orig, out, sizeof out) == 0 &&
          snprintf(f, sizeof f, "%s/report.recovered2.doc", dir) > 0 &&
          strcmp(out, f) == 0);
    fp = fopen(f, "w");
    fclose(fp);
    check("and then NAME.recovered3.doc",
          doc_recovered_path(orig, out, sizeof out) == 0 &&
          strstr(out, "report.recovered3.doc") != NULL);
    snprintf(orig, sizeof orig, "%s/report.recovered.doc", dir);
    check("the original that is named like a recovery gets another name",
          doc_recovered_path(orig, out, sizeof out) == 0 &&
          strcmp(out, orig) != 0);
    snprintf(orig, sizeof orig, "%s/REPORT.DOC", dir);
    check("the extension in capitals is taken off",
          doc_recovered_path(orig, out, sizeof out) == 0 &&
          strstr(out, "REPORT.recovered.doc") != NULL);
    snprintf(orig, sizeof orig, "%s/noext", dir);
    check("a name with no extension",
          doc_recovered_path(orig, out, sizeof out) == 0 &&
          strstr(out, "noext.recovered.doc") != NULL);

    /* The folder cannot be written: the working directory is used. */
    if (geteuid() != 0 && chmod(dir, 0500) == 0) {
        char cwd[PATH_MAX];

        snprintf(orig, sizeof orig, "%s/locked.doc", dir);
        check("a folder that cannot be written: the working directory",
              doc_recovered_path(orig, out, sizeof out) == 0 &&
              strncmp(out, dir, strlen(dir)) != 0 &&
              strstr(out, "locked.recovered.doc") != NULL);
        chmod(dir, 0700);
        (void)cwd;
    }

    /* the whole path through a file: the original is not changed */
    snprintf(orig, sizeof orig, "%s/cut.doc", dir);
    {
        static const char data[] =
            "*R2\r\nMR1001\r\n*B0\r\nS\r\nTone\x1d\x00\r\nB\r\nTtwo\x1d\x00\r\n"
            "B\r\nTthr";
        DocRecovery r;
        char err[160];
        struct stat before;
        struct stat after;
        size_t n1 = 0;
        size_t n2 = 0;
        uint8_t *a;
        uint8_t *b;
        Doc *d;

        fp = fopen(orig, "wb");
        fwrite(data, 1, sizeof data - 1, fp);
        fclose(fp);
        stat(orig, &before);
        a = slurp(orig, &n1);
        d = doc_recover(orig, &r, err, sizeof err);
        b = slurp(orig, &n2);
        stat(orig, &after);
        check("doc_recover of a file: the part that is whole, 2 blocks",
              d != NULL && r.damaged == 1 && r.blocks == 2 &&
              strstr(r.why, "no end") != NULL);
        check("the recovered document has a new name and is marked changed",
              d != NULL && d->path != NULL && strcmp(d->path, orig) != 0 &&
              strstr(d->path, "cut.recovered.doc") != NULL &&
              d->modified != 0);
        check("the original file is byte for byte as it was, with the same "
              "time",
              n1 == n2 && memcmp(a, b, n1) == 0 &&
              before.st_mtime == after.st_mtime &&
              before.st_size == after.st_size);
        {
            size_t sl = 0;
            uint8_t *o = d != NULL ? doc_serialize(d, &sl) : NULL;
            char e2[160];
            Doc *d2 = o != NULL ? doc_load_mem(o, sl, e2, sizeof e2) : NULL;

            check("the recovered document is whole: it saves and loads again",
                  d2 != NULL && count_blocks(d2) == 2);
            doc_free(d2);
            free(o);
        }

        doc_free(d);
        free(a);
        free(b);
    }
}


extern char **environ;


/*
 * The peak memory of a fresh process that runs this program with the
 * argument mode ("--child-load" or "--child-recover"), in kilobytes: the
 * child writes its own VmHWM (the high water mark of its own memory, read
 * from /proc/self/status) on its standard output.  ru_maxrss of the child
 * is not used: Linux folds the peak of the image that exec replaced into it,
 * so a child of this large process would show the size of this process.
 * Returns -1 when the child fails: a failed child measured nothing.
 */
static long
peak_of(const char *self, const char *mode)
{
    pid_t p;
    int status = 0;
    int fd[2];
    char a0[PATH_MAX];
    char a1[64];
    char *args[3];
    char text[64];
    ssize_t got;
    posix_spawn_file_actions_t fa;

    snprintf(a0, sizeof a0, "%s", self);
    snprintf(a1, sizeof a1, "%s", mode);
    args[0] = a0;
    args[1] = a1;
    args[2] = NULL;

    if (pipe(fd) != 0) {
        return -1;
    }

    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_addclose(&fa, fd[0]);

    if (posix_spawn(&p, "/proc/self/exe", &fa, NULL, args, environ) != 0) {
        posix_spawn_file_actions_destroy(&fa);
        close(fd[0]);
        close(fd[1]);

        return -1;
    }

    posix_spawn_file_actions_destroy(&fa);
    close(fd[1]);
    got = read(fd[0], text, sizeof text - 1);
    close(fd[0]);

    if (waitpid(p, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || got <= 0) {
        return -1;
    }

    text[got] = '\0';

    return atol(text);
}


/* The high water mark of this process in kilobytes, or -1. */
static long
own_peak(void)
{
    char line[256];
    long kb = -1;
    FILE *f = fopen("/proc/self/status", "r");

    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "VmHWM:", 6) == 0) {
            kb = atol(line + 6);
        }
    }

    if (f != NULL) {
        fclose(f);
    }

    return kb;
}


static int
child_load(const uint8_t *b, size_t n)
{
    char err[160];
    Doc *d = doc_load_mem(b, n, err, sizeof err);

    return d == NULL ? 1 : 0;
}


static int
child_recover(const uint8_t *b, size_t n)
{
    char err[160];
    DocRecovery r;
    Doc *d = doc_recover_mem(b, n, &r, err, sizeof err);

    return d == NULL ? 1 : 0;
}


/*
 * The big file of the memory test: a document of many short blocks, 4 MB
 * long.  Used by this process and by the fresh child that measures.
 */
static uint8_t *
make_big(size_t *len)
{
    size_t cap = 4 * 1000 * 1000;
    uint8_t *b = malloc(cap);
    size_t at = 0;

    at += (size_t)snprintf((char *)b + at, cap - at,
                           "*R2\r\nMR1001\r\n*B0\r\nS\r\n");

    while (at + 80 < cap - 10) {
        at += (size_t)snprintf((char *)b + at, cap - at,
                               "B\r\nT%c%c%c some words of a block of text "
                               "that is long enough\x1d%c\r\n", 'a', 'b', 'c',
                               0);
    }

    /* The end record: without it the file is a cut file and a load refuses it. */
    at += (size_t)snprintf((char *)b + at, cap - at, "*E\r\n");
    *len = at;

    return b;
}


/* The child of the measurement: load, or recover a file cut in a record. */
static int
run_child(const char *mode)
{
    size_t at = 0;
    uint8_t *b = make_big(&at);
    int rc = 2;

    if (strcmp(mode, "--child-load") == 0) {
        rc = child_load(b, at);
    } else if (strcmp(mode, "--child-recover") == 0) {
        /* The same file, cut in the middle of a record: no end. */
        rc = child_recover(b, at - 40);
    }

    printf("%ld\n", own_peak());
    fflush(stdout);

    return rc;
}


static void
test_memory(const char *self)
{
    long whole;
    long rec;

    whole = peak_of(self, "--child-load");
    rec = peak_of(self, "--child-recover");
    printf("peak memory: a load %ld KB, a recovery %ld KB\n", whole, rec);
    check("the load of the whole big file succeeds and a peak is measured",
          whole > 0);
    check("the recovery of the cut big file succeeds and a peak is measured",
          rec > 0);
#ifdef __SANITIZE_ADDRESS__
    /* The shadow memory of the sanitizer is most of the peak. */
    check("the peak memory of a recovery is under 600 MB (address sanitizer)",
          rec > 0 && rec < 600 * 1024);
#else
    check("the peak memory of a recovery is under 100 MB",
          rec > 0 && rec < 100 * 1024);
#endif
    check("and not much more than that of a load of the whole file",
          whole > 0 && rec > 0 && rec <= whole + whole / 2 + 8 * 1024);
}


int
main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/data";

    if (argc > 1 && strncmp(argv[1], "--child-", 8) == 0) {
        return run_child(argv[1]);
    }

    static const char *const names[] = {"plain_text.doc", "title_page.doc",
                                        "xref.doc", "print_panels.doc", NULL};

    for (int k = 0; names[k] != NULL; k++) {
        char path[PATH_MAX];
        size_t len = 0;
        uint8_t *b;
        char err[160];
        Doc *whole;

        snprintf(path, sizeof path, "%s/%s", dir, names[k]);
        b = slurp(path, &len);

        if (b == NULL) {
            printf("FAIL cannot read %s\n", path);
            fails++;
            continue;
        }

        whole = doc_load_mem(b, len, err, sizeof err);

        if (whole == NULL) {
            printf("FAIL %s does not load: %s\n", names[k], err);
            fails++;
            free(b);
            continue;
        }

        test_whole(names[k], b, len);
        test_cuts(names[k], b, len, whole);

        if (k == 3) {
            test_middle(b, len, whole);
        }

        doc_free(whole);
        free(b);
    }

    test_refusals();
    test_limits();
    test_names();
    test_memory(argv[0]);
    printf("recovertest: %d checks, %d failures\n", checks, fails);

    return fails == 0 ? 0 : 1;
}
