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
 * Unit tests of the parts of the preview that need no viewer and no screen
 * (src/pvcore.c and the page estimate of src/latex.c): the reader of the
 * progress of latex ("[1] [2 <a.png>] [3]"), the corrected estimate, the
 * temporary directory and its removal, the search for a program, and the
 * child process that is read through a pipe.  The argument is the
 * directory of the test documents (tests/data).
 */
#include "../src/util.h"
#include "../src/latex.h"
#include "../src/marker.h"
#include "../src/msdoc.h"
#include "../src/pvcore.h"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
static int checks;


static void
check_int(const char *what, long got, long want)
{
    checks++;

    if (got != want) {
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        failures++;
    }
}


static void
check_true(const char *what, int cond)
{
    checks++;

    if (cond == 0) {
        printf("FAIL %s\n", what);
        failures++;
    }
}


/* The pages that the reader counts in a text fed in one piece. */
static int
pages_of(const char *text)
{
    PvProg p;

    pv_prog_init(&p, 10);
    pv_prog_feed(&p, text, strlen(text));

    return p.pages;
}


static void
test_parser(void)
{
    PvProg p;
    const char *text = "[1] [2 <a.png>] [3]\n";

    check_int("three pages", pages_of(text), 3);
    pv_prog_init(&p, 10);

    for (size_t i = 0; text[i] != '\0'; i++) {
        pv_prog_feed(&p, text + i, 1);
    }

    check_int("three pages, a byte at a time", p.pages, 3);
    check_int("a file name in the page", pages_of("[7 <./pic1.pdf, id=5>]"), 1);
    check_int("the pdftex map", pages_of("[1{/usr/share/pdftex.map}]"), 1);
    check_int("page number 0 and negative", pages_of("[0] [-1] [20]"), 3);
    check_int("empty brackets", pages_of("[] [ ] [x] [U+2320]"), 0);
    check_int("a date in brackets", pages_of("[2020/01/01 v1.0 Package]"), 0);
    check_int("a version number", pages_of("[1.2.3]"), 0);
    check_int("pages after a file", pages_of("(./doc.aux) [1] [2]\n"), 2);
    check_int("a page cut by the end of a line", pages_of("[1\n]\n[2\n"), 2);
    check_int("a page followed by a mark line",
              pages_of("[1\n@@strprint pos 5\n]\n"), 1);
    check_int("ten thousand pages", pages_of("[10000]"), 1);

    /* The text of a warning is not looked at, until a blank line. */
    check_int("a bracket in an Overfull context",
              pages_of("Overfull \\hbox (3pt too wide)\n"
                       "\\T1/lmr/m/n/10 See [1] and [2]\n\n[3]\n"), 1);
    check_int("the page that follows a context directly",
              pages_of("Overfull \\hbox (3pt too wide)\n[] [] \n[5\n"), 1);
    check_int("the next page on the line of the context",
              pages_of("[1] Overfull \\hbox (3pt too wide)\n"
                       "[]|\\T1/lmr/m/n/10 1.3.7[][]| [2] [3]\n"), 3);
    check_int("a citation in a context is not a page",
              pages_of("[1] [2]\nOverfull \\hbox (3pt too wide)\n"
                       "\\T1/lmr/m/n/10 see [7] and [8] [3]\n"), 3);
    check_int("a bracket in an error",
              pages_of("! Undefined control sequence.\n"
                       "l.12 see [4] and \\foo\n\n[2]\n"), 1);
    check_int("a warning does not stop the next pages",
              pages_of("LaTeX Warning: x [3].\n\n[1] [2]\n"), 2);
}


static void
test_lines(void)
{
    PvProg p;
    int about;
    char text[] = "\n@@strprint stage writing\n\n\n@@strprint pass 1\n\n"
                  "[1\n@@strprint pos 100\n] [2\n@@strprint pos 250\n]\n";

    pv_prog_init(&p, 40);
    check_int("writing at first", p.stage, PV_WRITING);
    check_int("lines change something", pv_prog_feed(&p, text,
                                                     strlen(text)), 1);
    check_int("latex stage", p.stage, PV_LATEX);
    check_int("pass 1", p.pass, 1);
    check_int("two pages", p.pages, 2);
    check_int("last position", p.pos, 250);
    check_int("pages at the position", p.pos_pages, 2);
    check_int("no pass is known", p.known, 0);
    check_int("total is about", pv_prog_total(&p, &about) > 0 && about, 1);

    {
        char more[] = "\n@@strprint pass 2\n\n[1] [2] [3]\n";

        pv_prog_feed(&p, more, strlen(more));
        check_int("pass 2", p.pass, 2);
        check_int("the pages of pass 1 are known", p.known, 2);
        check_int("pass 2 pages", p.pages, 3);
        check_int("the total is the known one, at least the pages",
                  pv_prog_total(&p, &about), 3);
        check_int("the known total is not about", about, 0);
    }

    {
        char again[] = "\n@@strprint pass 1\n\n[1]\n";

        pv_prog_feed(&p, again, strlen(again));
        check_int("a new first pass forgets", p.known, 0);
        check_int("and counts again", p.pages, 1);
    }

    {
        char err[] = "\n@@strprint error ! Undefined control sequence.\n\n"
                     "\n@@strprint error2 l.5 \\foo\n\n"
                     "strprint: something went wrong\n";

        pv_prog_feed(&p, err, strlen(err));
        check_true("first error", strcmp(p.err1, "! Undefined control "
                                                 "sequence.") == 0);
        check_true("second line", strcmp(p.err2, "l.5 \\foo") == 0);
        check_true("note", strcmp(p.note, "something went wrong") == 0);
    }
}


static void
test_refine(void)
{
    check_int("no mark: the prior", pv_refine(100, 0, -1), 100);
    check_int("a mark at the end: the pages", pv_refine(100, 50, 1000), 50);
    check_int("the middle, the prior is right", pv_refine(100, 50, 500), 100);
    check_int("the middle, the prior is low", pv_refine(60, 50, 500), 80);
    check_int("never less than the pages", pv_refine(10, 50, 900), 51);
    check_int("a low prior is raised to the pages", pv_refine(1, 50, 1000),
              50);
    check_int("a bad position is ignored", pv_refine(10, 5, 2000), 10);
}


/* A document of n paragraphs of len characters each. */
static Doc *
make_doc(int n, int len)
{
    Doc *d = doc_new(0);

    for (int i = 0; i < n; i++) {
        Block *b = block_new(1);

        for (int k = 0; k < len; k++) {
            text_insert(&b->cols[0].text, k, tok_char(u8('a' + k % 26)));
        }

        doc_insert_item(d, d->nitems, IT_BLOCK, b);
    }

    return d;
}


static void
test_estimate(const char *data)
{
    Doc *small = make_doc(1, 20);
    Doc *a = make_doc(100, 100);
    Doc *b = make_doc(200, 100);
    char path[1024];
    Doc *d;
    char err[128];

    check_int("one short line is one page", latex_estimate_pages(small), 1);
    check_true("100 paragraphs of 100 characters: about 5 pages",
               latex_estimate_pages(a) >= 4 && latex_estimate_pages(a) <= 8);
    check_true("twice the text, about twice the pages",
               latex_estimate_pages(b) >= 2 * latex_estimate_pages(a) - 1 &&
               latex_estimate_pages(b) <= 2 * latex_estimate_pages(a) + 1);
    check_true("more characters per inch, fewer pages",
               latex_estimate_with(a, 1600, 531) <
               latex_estimate_with(a, 1000, 531));
    doc_free(small);
    doc_free(a);
    doc_free(b);
    path_join(path, sizeof path, data, "plain_text.doc");
    d = doc_load(path, err, sizeof err);
    check_true("a test document is loaded", d != NULL);

    if (d != NULL) {
        check_true("the estimate of a test document",
                   latex_estimate_pages(d) >= 1);
        doc_free(d);
    }
}


/* A document of n paragraphs of 100 characters, each with one marker. */
static Doc *
make_doc_markers(int n, int mtype)
{
    Doc *d = doc_new(0);

    for (int i = 0; i < n; i++) {
        Block *b = block_new(1);

        for (int k = 0; k < 100; k++) {
            text_insert(&b->cols[0].text, k, tok_char(u8('a' + k % 26)));
        }

        text_insert(&b->cols[0].text, 50, tok_marker(mtype, NULL, 0));
        doc_insert_item(d, d->nitems, IT_BLOCK, b);
    }

    return d;
}


/*
 * A document of n paragraphs of 100 characters, each with per named Index
 * markers (an Index marker with no main heading is listed nowhere, so the
 * estimate counts only the named ones).
 */
static Doc *
make_doc_named(int n, int per)
{
    static const uint8_t none[1] = {0};
    Doc *d = doc_new(0);
    const MInfo *mi = marker_info(3);

    for (int i = 0; i < n; i++) {
        Block *b = block_new(1);

        for (int k = 0; k < 100; k++) {
            text_insert(&b->cols[0].text, k, tok_char(u8('a' + k % 26)));
        }

        for (int m = 0; m < per; m++) {
            MPayload p;
            char name[32];
            size_t len;
            uint8_t *data;

            snprintf(name, sizeof name, "term%d-%d", i, m);
            memset(&p, 0, sizeof p);
            mp_parse(&p, mi, none, 0);
            mp_set(&p, "2", name, 1);
            data = mp_build(&p, &len);
            mp_free(&p);
            text_insert(&b->cols[0].text, 50, tok_marker(3, data, len));
            free(data);
        }

        doc_insert_item(d, d->nitems, IT_BLOCK, b);
    }

    return d;
}


/*
 * The estimate counts what the print makes besides the text: the index and
 * the lists as the stored settings say, with the entries that have a name,
 * and the height of a Picture marker.
 */
static void
test_generated(void)
{
    Doc *plain = make_doc(100, 100);
    Doc *named = make_doc_named(100, 10);
    Doc *unnamed = make_doc_markers(100, 3);
    Doc *pics = make_doc_markers(30, 25);
    Doc *noprn = make_doc_markers(30, 12);
    int base;
    int off;

    doc_list_set(named, LK_INDEX, 0);
    off = latex_estimate_pages(named);
    doc_list_set(named, LK_INDEX, -1);
    check_true("an index of 1000 named entries adds its pages",
               latex_estimate_pages(named) >= off + 15);
    doc_list_set(named, LK_INDEX, 0);
    check_int("an index that is off adds no page", latex_estimate_pages(named),
              off);
    doc_list_set(named, LK_INDEX, 1);
    check_true("an index that is on adds its pages",
               latex_estimate_pages(named) >= off + 15);
    base = latex_estimate_pages(plain);
    doc_list_set(plain, LK_INDEX, -1);
    check_int("an automatic index with no marker adds no page",
              latex_estimate_pages(plain), base);
    doc_list_set(unnamed, LK_INDEX, 0);
    off = latex_estimate_pages(unnamed);
    doc_list_set(unnamed, LK_INDEX, 1);
    check_int("an index that is on with 100 Index markers that have no "
              "main heading adds no page", latex_estimate_pages(unnamed), off);
    check_true("30 Picture markers take room: more pages than other markers",
               latex_estimate_pages(pics) >= latex_estimate_pages(noprn) + 4);
    doc_free(plain);
    doc_free(named);
    doc_free(unnamed);
    doc_free(pics);
    doc_free(noprn);
}


static void
test_marks(const char *data)
{
    char path[1024];
    char err[128];
    Doc *d;
    LatexOpts o;
    char *mem = NULL;
    size_t len = 0;
    FILE *m;
    int count = 0;
    int last = -1;
    int sorted = 1;
    const char *p;

    path_join(path, sizeof path, data, "plain_text.doc");
    d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        check_true("document for the marks", 0);

        return;
    }

    memset(&o, 0, sizeof o);
    o.docdir = data;
    m = open_memstream(&mem, &len);
    latex_write(d, m, &o);
    fclose(m);
    check_true("no marks without the option", strstr(mem, "@@strprint") ==
                                              NULL);
    free(mem);
    mem = NULL;
    memset(&o, 0, sizeof o);
    o.docdir = data;
    o.marks = 1;
    m = open_memstream(&mem, &len);
    latex_write(d, m, &o);
    fclose(m);

    for (p = mem; (p = strstr(p, "@@strprint pos ")) != NULL; p++) {
        int v = atoi(p + 15);

        count++;
        sorted = sorted != 0 && v >= last && v <= 1000;
        last = v;
    }

    check_true("marks are written with the option", count > 0);
    check_true("the marks go up and stay within 1000", sorted);
    check_true("the first mark is 0", strstr(mem, "@@strprint pos 0}") !=
                                      NULL);
    free(mem);
    doc_free(d);
}


/* The names in a directory, without . and .., as a count. */
static int
count_entries(const char *dir)
{
    DIR *dp = opendir(dir);
    struct dirent *e;
    int n = 0;

    if (dp == NULL) {
        return -1;
    }

    while ((e = readdir(dp)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            n++;
        }
    }

    closedir(dp);

    return n;
}


static void
touch(const char *dir, const char *name)
{
    char path[1100];
    FILE *f;

    path_join(path, sizeof path, dir, name);
    f = fopen(path, "w");

    if (f != NULL) {
        fputs("x\n", f);
        fclose(f);
    }
}


static void
test_tmpdir(void)
{
    char area[PATH_MAX];
    char outside[PATH_MAX];
    int had_tmpdir = getenv("TMPDIR") != NULL;
    char saved[PATH_MAX] = "";
    char dir[1024];
    char sub[1100];
    char link[1100];
    char target[1100];
    struct stat st;

    if (had_tmpdir != 0) {
        str_copy(saved, sizeof saved, getenv("TMPDIR"));
    }

    tmp_path(area, sizeof area, "pvtest.XXXXXX");
    tmp_path(outside, sizeof outside, "pvtest-out.XXXXXX");
    check_true("private area", mkdtemp(area) != NULL);
    check_true("private outside", mkdtemp(outside) != NULL);
    setenv("TMPDIR", area, 1);
    check_int("the directory is made", pv_mkdtemp(dir, sizeof dir), 0);
    check_true("it is under TMPDIR", strncmp(dir, area, strlen(area)) == 0);
    check_true("with the name strpv.", strstr(dir, "/strpv.") != NULL);
    check_int("it exists", stat(dir, &st), 0);
    check_int("mode 0700", st.st_mode & 0777, 0700);
    touch(dir, "doc.tex");
    touch(dir, "doc.log");
    path_join(sub, sizeof sub, dir, "strprint.abc");
    mkdir(sub, 0700);
    touch(sub, "doc.dvi");
    touch(outside, "keep.txt");
    path_join(target, sizeof target, outside, "keep.txt");
    path_join(link, sizeof link, sub, "pic1.eps");
    check_int("a link", symlink(target, link), 0);
    path_join(link, sizeof link, sub, "dirlink");
    check_int("a link to a directory", symlink(outside, link), 0);
    check_int("the tree is removed", pv_rmtree(dir), 0);
    check_int("the directory is gone", stat(dir, &st), -1);
    check_int("the area is empty", count_entries(area), 0);
    check_int("the file behind a link is kept", stat(target, &st), 0);
    check_int("removing it again is fine", pv_rmtree(dir), 0);
    check_int("a path of another name is refused", pv_rmtree(outside), -1);
    check_int("the outside directory is still there", stat(outside, &st), 0);
    check_int("the root is refused", pv_rmtree("/"), -1);
    unlink(target);
    rmdir(outside);
    rmdir(area);

    /* Put the variable back as the caller had it. */
    if (had_tmpdir != 0) {
        setenv("TMPDIR", saved, 1);
    } else {
        unsetenv("TMPDIR");
    }
}


static void
test_find(void)
{
    char out[1024];

    setenv("PVTEST_PROG", "/bin/sh", 1);
    check_int("the variable names it", pv_find_prog("PVTEST_PROG", "nosuch",
                                                    out, sizeof out), 0);
    check_true("and is used", strcmp(out, "/bin/sh") == 0);
    setenv("PVTEST_PROG", "/no/such/file", 1);
    check_int("a wrong variable is an error", pv_find_prog("PVTEST_PROG",
                                                           "sh", out,
                                                           sizeof out), -1);
    unsetenv("PVTEST_PROG");
    check_int("PATH is searched", pv_find_prog("PVTEST_PROG", "sh", out,
                                               sizeof out), 0);
    check_true("an absolute path", out[0] == '/');
    check_int("not found", pv_find_prog(NULL, "no-such-program-xyz", out,
                                        sizeof out), -1);
}


static void
test_job(void)
{
    PvJob j;
    char buf[256];
    char sh[] = "/bin/sh";
    char opt[] = "-c";
    char fast[] = "echo '[1] [2]'; echo oops >&2; echo $TMPDIR; exit 3";
    char slow[] = "sleep 30 & echo $! > pvtest.pid; echo started; wait";
    char *argv[4];
    char area[PATH_MAX];
    char pidfile[1100];
    size_t got = 0;
    int tries;

    tmp_path(area, sizeof area, "pvtest.XXXXXX");
    check_true("area", mkdtemp(area) != NULL);
    argv[0] = sh;
    argv[1] = opt;
    argv[2] = fast;
    argv[3] = NULL;
    check_int("a job starts", pv_job_start(&j, argv, area), 0);
    memset(buf, 0, sizeof buf);

    for (tries = 0; tries < 200; tries++) {
        long n = pv_job_read(&j, buf + got, sizeof buf - got - 1);

        if (n > 0) {
            got += usz(isz(lsz(n)));
        } else if (n == 0) {
            break;
        } else {
            usleep(10000);
        }
    }

    check_true("its output and its errors come through the pipe",
               strstr(buf, "[1] [2]") != NULL && strstr(buf, "oops") != NULL);
    check_true("TMPDIR is set for it", strstr(buf, area) != NULL);

    for (tries = 0; tries < 200 && pv_job_done(&j) == 0; tries++) {
        usleep(10000);
    }

    check_int("it has ended", pv_job_done(&j), 1);
    check_int("with its status", WEXITSTATUS(j.status), 3);
    pv_job_close(&j);

    /* A job that is stopped takes what it started with it. */
    argv[2] = slow;
    path_join(pidfile, sizeof pidfile, area, "pvtest.pid");
    {
        char cwdsave[1024];

        check_true("cwd", getcwd(cwdsave, sizeof cwdsave) != NULL);
        check_int("chdir", chdir(area), 0);
        check_int("a slow job starts", pv_job_start(&j, argv, area), 0);

        for (tries = 0; tries < 300; tries++) {
            if (access(pidfile, F_OK) == 0 && count_entries(area) > 0) {
                FILE *f = fopen(pidfile, "r");
                int pid = 0;

                if (f != NULL) {
                    if (fscanf(f, "%d", &pid) != 1) {
                        pid = 0;
                    }

                    fclose(f);
                }

                if (pid > 0) {
                    pv_job_kill(&j);
                    usleep(100000);
                    check_int("the child of the job is gone", kill(pid, 0),
                              -1);
                    check_int("no process is left", pv_job_done(&j), 1);
                    break;
                }
            }

            usleep(10000);
        }

        check_true("the slow job wrote its pid", tries < 300);
        check_int("chdir back", chdir(cwdsave), 0);
    }

    unlink(pidfile);
    rmdir(area);
}


int
main(int argc, char **argv)
{
    test_parser();
    test_lines();
    test_refine();
    test_tmpdir();
    test_find();
    test_job();
    test_generated();

    if (argc > 1) {
        test_estimate(argv[1]);
        test_marks(argv[1]);
    }

    printf("pvtest: %d checks, %d failures\n", checks, failures);

    return failures != 0 ? 1 : 0;
}
