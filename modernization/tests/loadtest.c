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
 * Damaged and hostile document files for the loader (src/msdoc.c).  A file
 * may be cut off by a full disk, damaged by a bad medium or made by someone
 * who wants the program to fail, so the loader must refuse such a file with
 * a message and must never crash, hang or allocate a lot of memory for it.
 * Every case runs in a child process of its own, so that a crash or a hang
 * is a failure of one check and the peak memory of the child can be
 * measured.  The test makes all of its files in a private directory below
 * TMPDIR and removes them again.  The first argument is the directory of the
 * test data (tests/data), the optional second one the directory of the
 * sample documents, which must load and save again byte for byte.
 */
#include "../src/util.h"
#include "../src/build_type.h"
#include "../src/msdoc.h"
#include "../src/layout.h"
#include "../src/readfile.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef DOC_MAX_BYTES
#define DOC_MAX_BYTES (32u * 1024u * 1024u)
#define DOC_MAX_ITEMS 500000
#define DOC_MAX_MARKERS 1000000
#define DOC_MAX_HEADER 20000
#define DOC_MAX_PROPS 2000
#endif

#define PEAK_LIMIT_KB (100 * 1024)
#define CASE_SECONDS 60

enum {
    R_LOADED = 10,
    R_REFUSED = 11,
    R_NO_MESSAGE = 12
};

typedef struct {
    int exit_code;
    int signal;
    long peak_kb;
    char message[256];
} Outcome;

typedef struct {
    uint8_t *d;
    size_t n;
    size_t cap;
} Buf;

static int failures;
static int checks;
static char dir[PATH_MAX];
static unsigned long made;


static void
fail_now(const char *what)
{
    fprintf(stderr, "loadtest: %s\n", what);
    exit(2);
}


static void
check(bool ok, const char *what)
{
    checks++;

    if (!ok) {
        failures++;
        printf("FAIL %s\n", what);
    } else {
        printf("ok   %s\n", what);
    }
}


static void
buf_put(Buf *b, const void *p, size_t n)
{
    if (n == 0) {
        return;
    }

    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->d = realloc(b->d, b->cap);

        if (b->d == NULL) {
            fail_now("out of memory");
        }
    }

    memcpy(b->d + b->n, p, n);
    b->n += n;
}


static void
buf_str(Buf *b, const char *s)
{
    buf_put(b, s, strlen(s));
}


static void
buf_rep(Buf *b, const char *s, size_t count)
{
    size_t len = strlen(s);

    for (size_t i = 0; i < count; i++) {
        buf_put(b, s, len);
    }
}


/* Make a file name of a case in the private directory. */
static const char *
case_path(const char *name)
{
    static char path[PATH_MAX];

    path_join(path, sizeof path, dir, name);

    return path;
}


static void
write_file(const char *path, const Buf *b)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        fail_now("cannot write a test file");
    }

    if (b->n != 0 && fwrite(b->d, 1, b->n, f) != b->n) {
        fail_now("short write of a test file");
    }

    fclose(f);
}


static const char *
head_text(void)
{
    return "*R2\r\nMDHostile\0\r\nMR1001\r\nPP\\1\\\0\r\n"
           "DP0,8500,11000\r\nYCL99\r\n";
}


/* The header of a good document, with the body mark that follows it. */
static void
put_head(Buf *b, int structured)
{
    char rec[16];

    buf_put(b, "*R2\r\nMD", 7);
    buf_put(b, "Hostile\0\r\n", 10);
    buf_str(b, "MR1001\r\nPP\\1\\");
    buf_put(b, "\0\r\n", 3);
    buf_str(b, "DP0,8500,11000\r\nYCL99\r\n");
    snprintf(rec, sizeof rec, "*B%d\r\n", structured);
    buf_str(b, rec);
}


static void
put_text(Buf *b, const char *s)
{
    buf_put(b, "T", 1);
    buf_str(b, s);
    buf_put(b, "\0\r\n", 3);
}


/* A small good document with markers, a column record and a section. */
static void
good_doc(Buf *b)
{
    put_head(b, 1);
    buf_str(b, "S\r\n");
    put_text(b, "Hello \023" "6(S\r\n\023)\035");
    buf_str(b, "CW40\r\n");
    put_text(b, "one\035");
    put_text(b, "two\035");
    buf_str(b, "+\r\n");
    put_text(b, "inner\035");
    buf_str(b, "-\r\n*E\r\n");
}


/*
 * Run one load in a child process.  The child loads the file, saves the
 * document again to find damage in the model and exits with the result; the
 * parent reads the message and the peak memory.
 */
static Outcome
run_case(const char *path)
{
    Outcome o;
    int pipefd[2];
    pid_t pid;
    int status = 0;
    struct rusage ru;

    memset(&o, 0, sizeof o);

    if (pipe(pipefd) != 0) {
        fail_now("pipe");
    }

    fflush(stdout);
    pid = fork();

    if (pid < 0) {
        fail_now("fork");
    }

    if (pid == 0) {
        char err[256] = "";
        Doc *d;

        close(pipefd[0]);
        alarm(CASE_SECONDS);
        d = doc_load(path, err, sizeof err);

        if (d == NULL) {
            if (write(pipefd[1], err, strlen(err)) < 0) {
                exit(99);
            }

            exit(err[0] != '\0' ? R_REFUSED : R_NO_MESSAGE);
        }

        size_t len = 0;
        uint8_t *out = doc_serialize(d, &len);

        free(out);
        doc_free(d);
        exit(R_LOADED);
    }

    close(pipefd[1]);

    ssize_t got = read(pipefd[0], o.message, sizeof o.message - 1);

    if (got > 0) {
        o.message[got] = '\0';
    }

    close(pipefd[0]);

    if (wait4(pid, &status, 0, &ru) != pid) {
        fail_now("wait4");
    }

    o.peak_kb = ru.ru_maxrss;

    if (WIFSIGNALED(status)) {
        o.signal = WTERMSIG(status);
    } else {
        o.exit_code = WEXITSTATUS(status);
    }

    return o;
}


static void
describe(char *buf, size_t cap, const char *what, const Outcome *o)
{
    if (o->signal != 0) {
        snprintf(buf, cap, "%s (killed by signal %d)", what, o->signal);
    } else if (strata_asan == 0 && o->peak_kb > PEAK_LIMIT_KB) {
        snprintf(buf, cap, "%s (peak %ld MB)", what, o->peak_kb / 1024);
    } else {
        snprintf(buf, cap, "%s", what);
    }
}


/* Say what happened, so that a log of the test is a table of the cases. */
static void
report_outcome(const Outcome *o)
{
    if (o->signal != 0) {
        printf("     killed by signal %d, peak %ld MB\n", o->signal,
               o->peak_kb / 1024);
    } else if (o->exit_code == R_LOADED) {
        printf("     loaded, peak %ld MB\n", o->peak_kb / 1024);
    } else {
        printf("     refused: %s (peak %ld MB)\n", o->message,
               o->peak_kb / 1024);
    }
}


/* The case must not crash, hang or use a lot of memory, whatever it does. */
static bool
survived(const Outcome *o)
{
    return o->signal == 0 &&
           (strata_asan != 0 || o->peak_kb <= PEAK_LIMIT_KB) &&
           (o->exit_code == R_LOADED || o->exit_code == R_REFUSED);
}


/* The case must be refused with a message that tells why. */
static void
expect_refused(const char *name, const Buf *b, const char *what)
{
    Outcome o;
    char text[300];

    write_file(case_path(name), b);
    o = run_case(case_path(name));
    describe(text, sizeof text, what, &o);
    check(survived(&o) && o.exit_code == R_REFUSED && o.message[0] != '\0',
          text);
    report_outcome(&o);
    unlink(case_path(name));
    made++;
}


static void
expect_loaded(const char *name, const Buf *b, const char *what)
{
    Outcome o;
    char text[300];

    write_file(case_path(name), b);
    o = run_case(case_path(name));
    describe(text, sizeof text, what, &o);
    check(survived(&o) && o.exit_code == R_LOADED, text);
    report_outcome(&o);
    unlink(case_path(name));
    made++;
}


/* Either answer is good; only a crash, a hang or a big allocation is not. */
static void
expect_survived(const char *name, const Buf *b, const char *what)
{
    Outcome o;
    char text[300];

    write_file(case_path(name), b);
    o = run_case(case_path(name));
    describe(text, sizeof text, what, &o);
    check(survived(&o), text);
    report_outcome(&o);
    unlink(case_path(name));
    made++;
}


static void
test_truncation(void)
{
    Buf g = {0};
    int crashes = 0;
    int refused_wrong = 0;
    int cuts = 0;

    good_doc(&g);
    expect_loaded("good.doc", &g, "a good small document loads");

    /* Every cut of the file, at every byte, inside records or between. */
    for (size_t n = 0; n < g.n; n++) {
        Buf t = {0};
        Outcome o;

        buf_put(&t, g.d, n);
        write_file(case_path("cut.doc"), &t);
        o = run_case(case_path("cut.doc"));
        cuts++;

        if (!survived(&o)) {
            crashes++;
        }

        /*
         * A cut before the end record leaves a document that is not whole.
         * Only the line end after "*E" may be missing.
         */
        if (n < g.n - 2 && o.exit_code != R_REFUSED) {
            refused_wrong++;
        }

        free(t.d);
    }

    unlink(case_path("cut.doc"));
    check(crashes == 0, "no cut of a document crashes, hangs or is large");
    check(refused_wrong == 0,
          "every cut of a document, at every byte, is refused");
    printf("     %d cuts\n", cuts);
    free(g.d);
}


static void
test_shapes(void)
{
    Buf b = {0};
    uint32_t seed = 12345;

    expect_refused("empty.doc", &b, "a file of zero bytes is refused");

    buf_str(&b, "just some text\r\nin a plain file\r\n");
    expect_refused("text.doc", &b, "a plain text file is refused");
    b.n = 0;

    buf_str(&b, "*R2\r\n");
    expect_refused("rhead.doc", &b, "a file with only *R2 is refused");
    b.n = 0;

    put_head(&b, 1);
    expect_refused("headonly.doc", &b,
                   "a header with no end record is refused");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "*E\r\n");
    expect_loaded("emptybody.doc", &b, "a header and an end record load");
    b.n = 0;

    buf_str(&b, head_text());
    buf_str(&b, "S\r\n");
    expect_refused("nob.doc", &b, "a file with no *B record is refused");
    b.n = 0;

    for (int i = 0; i < 65536; i++) {
        seed = seed * 1103515245u + 12345u;
        uint8_t c = u8(iun((seed >> 16) & 255u));

        buf_put(&b, &c, 1);
    }

    expect_refused("random.doc", &b, "random bytes are refused");
    b.n = 0;

    buf_str(&b, "*R2\r\n");

    for (int i = 0; i < 65536; i++) {
        seed = seed * 1103515245u + 12345u;
        uint8_t c = u8(iun((seed >> 16) & 255u));

        buf_put(&b, &c, 1);
    }

    expect_survived("rrandom.doc", &b, "*R2 and random bytes survive");
    b.n = 0;

    buf_str(&b, "*R2\r\n");
    buf_rep(&b, "\0", 100000);
    expect_survived("nuls.doc", &b, "*R2 and many NUL bytes survive");
    b.n = 0;

    put_head(&b, 1);
    buf_rep(&b, "\r", 50000);
    buf_rep(&b, "\n", 50000);
    buf_str(&b, "*E\r\n");
    expect_survived("crlf.doc", &b, "lone CR and LF bytes survive");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "S\r\n*E\r\nTRAILING DATA AFTER THE END\r\n");
    expect_loaded("trailing.doc", &b, "data after the end record is kept");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "T");
    buf_rep(&b, "abc", 1000);
    buf_str(&b, "\r\n*E\r\n");
    expect_refused("unterm.doc", &b, "a text record with no NUL is refused");
    b.n = 0;

    free(b.d);
}


static void
test_specials(void)
{
    Outcome o;
    char text[300];
    char *fifo = strdup(case_path("fifo.doc"));
    char *sub = strdup(case_path("subdir.doc"));
    char *sparse = strdup(case_path("sparse.doc"));
    int fd;

    o = run_case(case_path("missing.doc"));
    check(survived(&o) && o.exit_code == R_REFUSED,
          "a file that does not exist is refused");

    if (mkdir(sub, 0700) != 0) {
        fail_now("mkdir");
    }

    o = run_case(sub);
    describe(text, sizeof text, "a directory is refused", &o);
    check(survived(&o) && o.exit_code == R_REFUSED, text);
    rmdir(sub);

    if (mkfifo(fifo, 0600) != 0) {
        fail_now("mkfifo");
    }

    o = run_case(fifo);
    describe(text, sizeof text, "a named pipe is refused and does not hang",
             &o);
    check(survived(&o) && o.exit_code == R_REFUSED, text);
    unlink(fifo);

    /* A sparse file that is just over the limit: no memory is made for it. */
    fd = open(sparse, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd < 0 || write(fd, "*R2\r\n", 5) != 5 ||
        ftruncate(fd, (off_t)DOC_MAX_BYTES + 1) != 0) {
        fail_now("cannot make a sparse file");
    }

    close(fd);
    o = run_case(sparse);
    describe(text, sizeof text, "a file over the size limit is refused", &o);
    check(survived(&o) && o.exit_code == R_REFUSED &&
              strstr(o.message, "large") != NULL, text);
    unlink(sparse);

    /* A claimed size of several gigabytes. */
    fd = open(sparse, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd < 0 || write(fd, "*R2\r\n", 5) != 5 ||
        ftruncate(fd, (off_t)5 * 1024 * 1024 * 1024) != 0) {
        fail_now("cannot make a very sparse file");
    }

    close(fd);
    o = run_case(sparse);
    describe(text, sizeof text, "a file of five gigabytes is refused", &o);
    check(survived(&o) && o.exit_code == R_REFUSED, text);
    unlink(sparse);
    free(fifo);
    free(sub);
    free(sparse);
}


static void
test_counts(void)
{
    Buf b = {0};

    put_head(&b, 1);
    buf_rep(&b, "S\r\n", DOC_MAX_ITEMS + 10);
    buf_str(&b, "*E\r\n");
    expect_refused("items.doc", &b, "too many sections are refused");
    b.n = 0;

    put_head(&b, 1);
    buf_rep(&b, "T\0\r\n", 0);

    for (int i = 0; i < DOC_MAX_ITEMS + 10; i++) {
        buf_put(&b, "T\0\r\n", 4);
    }

    buf_str(&b, "*E\r\n");
    expect_refused("blocks.doc", &b, "too many blocks are refused");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "T");

    for (int i = 0; i < DOC_MAX_MARKERS + 10; i++) {
        buf_str(&b, "\023" "1(\023)");
    }

    buf_put(&b, "\0\r\n*E\r\n", 7);
    expect_refused("markers.doc", &b, "too many markers are refused");
    b.n = 0;

    buf_str(&b, "*R2\r\n");
    buf_rep(&b, "XY1\r\n", DOC_MAX_HEADER + 10);
    buf_str(&b, "*B1\r\n*E\r\n");
    expect_refused("header.doc", &b, "too many header records are refused");
    b.n = 0;

    put_head(&b, 1);
    buf_rep(&b, "XY1\r\n", DOC_MAX_PROPS + 10);
    put_text(&b, "x\035");
    buf_str(&b, "*E\r\n");
    expect_refused("props.doc", &b,
                   "too many property records of a block are refused");
    b.n = 0;

    put_head(&b, 1);
    buf_rep(&b, "CW5\r\nTa\0\r\n", 0);

    for (int i = 0; i < 70000; i++) {
        buf_str(&b, "CW5\r\n");
        put_text(&b, "a");
    }

    buf_str(&b, "*E\r\n");
    expect_survived("columns.doc", &b, "a block of 70000 columns survives");
    b.n = 0;

    free(b.d);
}


static void
test_numbers(void)
{
    Buf b = {0};

    buf_str(&b, "*R2\r\nPML99999999999999999999999\r\n*B1\r\nS\r\n*E\r\n");
    expect_refused("hugehdr.doc", &b,
                   "a number of 23 digits in the header is refused");
    b.n = 0;

    buf_str(&b, "*R2\r\nDP0,8500,1100000000\r\n*B1\r\nS\r\n*E\r\n");
    expect_refused("hugedp.doc", &b,
                   "a number of ten digits in the header is refused");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "BM99999999999\r\n");
    put_text(&b, "x\035");
    buf_str(&b, "*E\r\n");
    expect_refused("hugeprop.doc", &b,
                   "a number of 11 digits in a block record is refused");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "CW99999999999999999999\r\n");
    put_text(&b, "x\035");
    buf_str(&b, "*E\r\n");
    expect_refused("hugecw.doc", &b,
                   "a column width of 20 digits is refused");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "CW-5\r\n");
    put_text(&b, "x\035");
    buf_str(&b, "*E\r\n");
    expect_survived("negcw.doc", &b, "a negative column width survives");
    b.n = 0;

    buf_str(&b, "*R2\r\nMR1001\r\n*B99999999999999999999\r\nS\r\n*E\r\n");
    expect_survived("hugeb.doc", &b, "a huge body mark number survives");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "T\023");
    buf_rep(&b, "9", 5000);
    buf_str(&b, "(abc\023)");
    buf_put(&b, "\0\r\n*E\r\n", 7);
    expect_survived("hugemtype.doc", &b,
                    "a marker type of 5000 digits survives");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "S");
    buf_str(&b, "E99999999999\r\n");
    buf_str(&b, "*E\r\n");
    expect_survived("hugese.doc", &b, "a long fold number survives");
    b.n = 0;

    free(b.d);
}


static void
test_nesting(void)
{
    Buf b = {0};
    int depth = 200000;

    put_head(&b, 1);
    buf_str(&b, "T");
    buf_rep(&b, "\023" "1(", (size_t)depth);
    buf_str(&b, "core");
    buf_rep(&b, "\023)", (size_t)depth);
    buf_put(&b, "\0\r\n*E\r\n", 7);
    expect_survived("deep.doc", &b, "200000 markers nested survive");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "T");
    buf_rep(&b, "\023" "1(", (size_t)depth);
    buf_put(&b, "\0\r\n*E\r\n", 7);
    expect_survived("deepopen.doc", &b,
                    "200000 markers opened and never closed survive");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "T");
    buf_rep(&b, "\023)", (size_t)depth);
    buf_put(&b, "\0\r\n*E\r\n", 7);
    expect_survived("closes.doc", &b, "200000 stray marker ends survive");
    b.n = 0;

    put_head(&b, 1);
    buf_str(&b, "T");
    buf_rep(&b, "\001", 1);
    buf_rep(&b, "\020", 1);
    buf_rep(&b, "\022", 1);
    buf_put(&b, "\0\r\n*E\r\n", 7);
    expect_survived("escapes.doc", &b,
                    "escape bytes at the end of a record survive");
    b.n = 0;

    free(b.d);
}


/* The reader that macro sets, help, imports and logs share. */
static void
test_readfile(void)
{
    char err[128];
    size_t len = 99;
    uint8_t *p;
    Buf b = {0};
    char *fifo = strdup(case_path("rf.fifo"));
    char path[PATH_MAX];

    path_join(path, sizeof path, dir, "rf.dat");
    buf_str(&b, "ab");
    buf_put(&b, "\0", 1);
    buf_str(&b, "cd");
    write_file(path, &b);
    p = readfile_all(path, 5, &len, err, sizeof err);
    check(p != NULL && len == 5 && memcmp(p, "ab\0cd", 5) == 0 && p[5] == 0,
          "a file of exactly the limit is read whole, with its NUL byte");
    free(p);

    p = readfile_all(path, 4, &len, err, sizeof err);
    check(p == NULL && len == 0 && strstr(err, "too large") != NULL,
          "a file of one byte over the limit is refused");

    b.n = 0;
    write_file(path, &b);
    p = readfile_all(path, 5, &len, err, sizeof err);
    check(p != NULL && len == 0 && p[0] == 0, "an empty file is read");
    free(p);

    p = readfile_all(dir, 100, &len, err, sizeof err);
    check(p == NULL && strstr(err, "directory") != NULL,
          "a directory is refused with its name");

    if (mkfifo(fifo, 0600) != 0) {
        fail_now("mkfifo");
    }

    alarm(CASE_SECONDS);
    p = readfile_all(fifo, 100, &len, err, sizeof err);
    alarm(0);
    check(p == NULL && strstr(err, "regular") != NULL,
          "a named pipe is refused and the open does not wait");
    unlink(fifo);

    p = readfile_all(case_path("rf.none"), 100, &len, err, sizeof err);
    check(p == NULL && strstr(err, "cannot open") != NULL,
          "a file that does not exist is refused");

    p = readfile_all(path, 100, &len, NULL, 0);
    check(p != NULL, "no message buffer is needed");
    free(p);
    unlink(path);
    free(fifo);
    free(b.d);
}



/* Is the file one of the container: the first record *R2 or *R3? */
static bool
is_container(const uint8_t *d)
{
    return d[0] == '*' && d[1] == 'R' && (d[2] == '2' || d[2] == '3');
}


static void
test_type_bytes(void)
{
    Buf b = {0};

    put_head(&b, 1);

    for (int c = 0; c < 256; c++) {
        char rec[8];

        if (c == '\r' || c == '\n' || c == 'T' || c == '*') {
            continue;
        }

        rec[0] = (char)c;
        rec[1] = '1';
        rec[2] = '\0';
        buf_str(&b, rec);
        buf_str(&b, "\r\n");
    }

    put_text(&b, "x\035");
    buf_str(&b, "*E\r\n");
    expect_survived("types.doc", &b, "every record type byte survives");
    free(b.d);
}


static bool
read_whole(const char *path, Buf *b)
{
    FILE *f = fopen(path, "rb");
    uint8_t chunk[65536];
    size_t got;

    if (f == NULL) {
        return false;
    }

    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
        buf_put(b, chunk, got);
    }

    fclose(f);

    return true;
}


/* A pseudo random number; the test must make the same files every time. */
static uint32_t
next_random(uint32_t *seed)
{
    *seed = *seed * 1103515245u + 12345u;

    return (*seed >> 8) & 0xFFFFFFu;
}


/* Make a damaged copy of a good document: bytes changed, numbers made long. */
static void
mutate(const Buf *in, Buf *out, uint32_t *seed)
{
    static const char *const bits[] = {"9999999", "99999999999999999999",
                                       "-1", "\023", "\023)", "\023" "7(",
                                       "\r\n", "\001", "\020", "\022", "*E",
                                       "*B", "CW", "T", "S", "0"};
    size_t kinds = next_random(seed) % 4 + 1;

    out->n = 0;
    buf_put(out, in->d, in->n);

    for (size_t k = 0; k < kinds && out->n > 8; k++) {
        size_t at = next_random(seed) % out->n;
        size_t how = next_random(seed) % 4;

        if (how == 0) {
            out->d[at] = u8(iun(next_random(seed) & 255u));
        } else if (how == 1) {
            const char *bit = bits[next_random(seed) % 16];
            size_t bl = strlen(bit);
            Buf t = {0};

            buf_put(&t, out->d, at);
            buf_put(&t, bit, bl);
            buf_put(&t, out->d + at, out->n - at);
            free(out->d);
            *out = t;
        } else if (how == 2) {
            size_t cut = next_random(seed) % 64 + 1;

            if (at + cut < out->n) {
                memmove(out->d + at, out->d + at + cut, out->n - at - cut);
                out->n -= cut;
            }
        } else {
            size_t cut = next_random(seed) % 64 + 1;
            Buf t = {0};

            if (at + cut < out->n) {
                buf_put(&t, out->d, at + cut);
                buf_put(&t, out->d + at, out->n - at);
                free(out->d);
                *out = t;
            }
        }
    }
}


/* Lay out and save a document that loaded, as the programs do with it. */
static void
use_document(Doc *d)
{
    size_t len = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        Block *b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            Fmt f;
            ColLayout l;
            int w = b->cols[c].width != 0 ? b->cols[c].width : 78;

            fmt_resolve(d, i, c, &f);
            layout_column(&f, &b->cols[c].text, w, &l, 1);
            layout_free(&l);
        }
    }

    free(doc_serialize(d, &len));
}


/*
 * Damaged copies of every good document of a directory are loaded and used
 * in a child process, so that a crash is a failure with the name of the
 * file and the number of the copy.
 */
static void
mutate_dir(const char *where, int copies)
{
    DIR *dp = opendir(where);
    struct dirent *de;
    int files = 0;
    int bad = 0;
    unsigned loaded = 0;
    unsigned refused = 0;

    if (dp == NULL) {
        return;
    }

    while ((de = readdir(dp)) != NULL) {
        char path[PATH_MAX];
        char name[NAME_MAX + 1];
        size_t nl;
        Buf in = {0};
        pid_t pid;
        int status = 0;
        int pfd[2];
        unsigned counts[2] = {0, 0};

        /* The child closes the directory, so it needs a copy of the name. */
        str_copy(name, sizeof name, de->d_name);
        nl = strlen(name);

        if (nl < 5 || strcasecmp(name + nl - 4, ".doc") != 0) {
            continue;
        }

        path_join(path, sizeof path, where, name);

        if (!read_whole(path, &in) || in.n < 3 || in.n > 400000 ||
            !is_container(in.d)) {
            free(in.d);
            continue;
        }

        files++;

        if (pipe(pfd) != 0) {
            fail_now("pipe");
        }

        fflush(stdout);
        pid = fork();

        if (pid == 0) {
            uint32_t seed = 2166136261u;
            Buf copy = {0};

            close(pfd[0]);
            closedir(dp);
            alarm(CASE_SECONDS * 4);

            for (size_t k = 0; k < nl; k++) {
                seed = (seed ^ u8(name[k])) * 16777619u;
            }

            for (int c = 0; c < copies; c++) {
                char err[256];
                Doc *d;

                mutate(&in, &copy, &seed);
                d = doc_load_mem(copy.d, copy.n, err, sizeof err);

                if (d == NULL) {
                    counts[1]++;
                } else {
                    use_document(d);
                    doc_free(d);
                    counts[0]++;
                }
            }

            if (write(pfd[1], counts, sizeof counts) < 0) {
                exit(99);
            }

            free(copy.d);
            free(in.d);
            exit(0);
        }

        close(pfd[1]);

        if (lsz(read(pfd[0], counts, sizeof counts)) != sizeof counts) {
            counts[0] = counts[1] = 0;
        }

        close(pfd[0]);
        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("     %s: the child ended with status %d\n", name,
                   status);
            bad++;
        }

        loaded += counts[0];
        refused += counts[1];
        free(in.d);
    }

    closedir(dp);

    {
        char what[PATH_MAX + 120];

        snprintf(what, sizeof what,
                 "%d damaged copies of each of %d documents of %s are "
                 "laid out and saved without a fault",
                 copies, files, where);
        check(files > 0 && bad == 0, what);
        printf("     %u copies loaded, %u refused\n", loaded, refused);
    }
}


/* A good file loads and is saved again byte for byte. */
static void
regress_dir(const char *where, const char *suffix_check)
{
    DIR *dp = opendir(where);
    struct dirent *de;
    int files = 0;
    int bad = 0;

    if (dp == NULL) {
        return;
    }

    while ((de = readdir(dp)) != NULL) {
        char path[PATH_MAX];
        size_t nl = strlen(de->d_name);
        Buf in = {0};
        char err[256];
        Doc *d;

        if (nl < 5 || (strcasecmp(de->d_name + nl - 4, ".doc") != 0 &&
                       strcasecmp(de->d_name + nl - 4, ".set") != 0 &&
                       strcasecmp(de->d_name + nl - 4, ".lib") != 0)) {
            continue;
        }

        snprintf(path, sizeof path, "%s/%s", where, de->d_name);

        if (!read_whole(path, &in) || in.n < 3 || !is_container(in.d)) {
            free(in.d);
            continue;
        }

        /* The file is saved with the current version of the file format. */
        in.d[2] = (uint8_t)('0' + STRATA_FILE_FORMAT);
        d = doc_load(path, err, sizeof err);
        files++;

        if (d == NULL) {
            printf("     %s is refused: %s\n", de->d_name, err);
            bad++;
        } else {
            size_t len = 0;
            uint8_t *out = doc_serialize(d, &len);

            if (len != in.n || memcmp(out, in.d, len) != 0) {
                printf("     %s is saved differently\n", de->d_name);
                bad++;
            }

            free(out);
            doc_free(d);
        }

        free(in.d);
    }

    closedir(dp);

    {
        char what[PATH_MAX + 80];

        snprintf(what, sizeof what,
                 "the %d files of %s load and save again byte for byte",
                 files, suffix_check);
        check(files > 0 && bad == 0, what);
    }
}


int
main(int argc, char **argv)
{
    const char *tmp = getenv("TMPDIR");
    char tpl[PATH_MAX];

    if (argc < 2) {
        fprintf(stderr, "usage: loadtest DATADIR [SAMPLEDIR]\n");

        return 2;
    }

    snprintf(tpl, sizeof tpl, "%s/loadtest.XXXXXX",
             tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");

    if (mkdtemp(tpl) == NULL) {
        fail_now("cannot make the test directory");
    }

    snprintf(dir, sizeof dir, "%s", tpl);
    test_truncation();
    test_shapes();
    test_specials();
    test_counts();
    test_numbers();
    test_nesting();
    test_type_bytes();
    test_readfile();
    regress_dir(argv[1], "the test data");
    mutate_dir(argv[1], 40);

    if (argc > 2) {
        regress_dir(argv[2], "the samples");
        mutate_dir(argv[2], 20);
    }

    rmdir(dir);
    printf("loadtest: %d checks, %d failures, %lu files made\n", checks,
           failures, made);

    return failures != 0 ? 1 : 0;
}
