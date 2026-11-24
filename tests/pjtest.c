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
 * pjtest - unit tests of printjob.c: the arguments of lp, the refusal of
 * bad values, the default name of the PDF and the list of queues.
 *
 * usage: pjtest FAKELPSTAT
 * FAKELPSTAT is tests/fakelpstat.sh, which the list of queues runs.
 */
#include "../src/util.h"
#include "../src/printjob.h"
#include "../src/build_type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int checks;


static void
check(int ok, const char *what)
{
    checks++;

    if (ok == 0) {
        failures++;
        printf("FAIL %s\n", what);
    } else {
        printf("ok   %s\n", what);
    }
}


/* The arguments joined with "|", or the message of a refusal. */
static void
joined(const PjOpts *o, const char *pdf, int pages, char *out, size_t cap)
{
    PjArgv a;
    char err[256];

    out[0] = '\0';

    if (pj_lp_argv(o, "/usr/bin/lp", pdf, pages, &a, err, sizeof err) != 0) {
        snprintf(out, cap, "REFUSED: %s", err);

        return;
    }

    for (int i = 0; a.v[i] != NULL; i++) {
        if (i > 0) {
            strncat(out, "|", cap - strlen(out) - 1);
        }

        strncat(out, a.v[i], cap - strlen(out) - 1);
    }
}


static void
expect(const PjOpts *o, int pages, const char *want, const char *what)
{
    char got[1024];

    joined(o, "/tmp/x.pdf", pages, got, sizeof got);

    if (strcmp(got, want) != 0) {
        printf("  want: %s\n  got:  %s\n", want, got);
    }

    check(strcmp(got, want) == 0, what);
}


static void
refused(const PjOpts *o, int pages, const char *pdf, const char *part,
        const char *what)
{
    char got[1024];

    joined(o, pdf, pages, got, sizeof got);

    if (strncmp(got, "REFUSED: ", 9) != 0 || strstr(got, part) == NULL) {
        printf("  got: %s\n", got);
    }

    check(strncmp(got, "REFUSED: ", 9) == 0 && strstr(got, part) != NULL,
          what);
}


static void
test_args(void)
{
    PjOpts o;

    pj_opts_init(&o);
    str_copy(o.printer, sizeof o.printer, "laser");
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|1|/tmp/x.pdf", "args: a queue");

    o.copies = 7;
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|7|/tmp/x.pdf", "args: copies");

    o.sides = PJ_TWO;
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|7|-o|sides=two-sided-long-edge|"
                  "/tmp/x.pdf", "args: double sided");

    o.sides = PJ_ONE;
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|7|-o|sides=one-sided|"
                  "/tmp/x.pdf", "args: one sided");

    o.sides = PJ_DEFAULT;
    o.sort = PJ_SORT_YES;
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|7|-o|collate=true|/tmp/x.pdf",
           "args: sorted");

    o.sort = PJ_SORT_NO;
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|7|-o|collate=false|/tmp/x.pdf",
           "args: not sorted");

    /* A page of the size of an envelope is not scaled by the system. */
    o.sort = PJ_SORT_DEFAULT;
    o.envelope = 1;
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|7|-o|print-scaling=none|"
                  "/tmp/x.pdf", "args: an envelope is not scaled");

    o.sides = PJ_ONE;
    expect(&o, 3, "/usr/bin/lp|-d|laser|-n|7|-o|sides=one-sided|-o|"
                  "print-scaling=none|/tmp/x.pdf",
           "args: the scaling option goes after the sides");

    o.envelope = 0;
    o.sides = PJ_DEFAULT;
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|7|/tmp/x.pdf",
           "args: a letter or A4 keeps the system default");
    o.sort = PJ_SORT_NO;

    o.sort = PJ_SORT_DEFAULT;
    o.copies = 1;
    o.page_from = 2;
    o.page_to = 5;
    expect(&o, 9, "/usr/bin/lp|-d|laser|-n|1|-P|2-5|/tmp/x.pdf",
           "args: a page range");
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|1|-P|2-5|/tmp/x.pdf",
           "args: a page range when the pages are not known");

    o.page_to = 0;
    expect(&o, 9, "/usr/bin/lp|-d|laser|-n|1|-P|2-9|/tmp/x.pdf",
           "args: no last page means the last page of the PDF");
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|1|-P|2-|/tmp/x.pdf",
           "args: no last page and the pages not known");

    o.page_from = 0;
    o.page_to = 3;
    expect(&o, 9, "/usr/bin/lp|-d|laser|-n|1|-P|1-3|/tmp/x.pdf",
           "args: no first page means page 1");

    o.page_to = 40;
    expect(&o, 9, "/usr/bin/lp|-d|laser|-n|1|-P|1-9|/tmp/x.pdf",
           "args: a last page after the end stops at the end");

    o.page_from = 0;
    o.page_to = 0;
    str_copy(o.title, sizeof o.title, "my doc.doc");
    expect(&o, 0, "/usr/bin/lp|-d|laser|-n|1|-t|my doc.doc|/tmp/x.pdf",
           "args: the title is one argument, spaces and all");

    o.copies = 99;
    o.sides = PJ_TWO;
    o.sort = PJ_SORT_YES;
    o.page_from = 1;
    o.page_to = 2;
    expect(&o, 2, "/usr/bin/lp|-d|laser|-n|99|-o|sides=two-sided-long-edge|"
                  "-o|collate=true|-P|1-2|-t|my doc.doc|/tmp/x.pdf",
           "args: everything at once, in a fixed order");
}


static void
test_refusals(void)
{
    PjOpts o;
    int n = 0;
    char err[256];
    const char *bad_queues[] = {"-evil", "a b", "a/b", "#x", "a\"b", "a'b",
                                "a,b", "a\\b", "-o", NULL};

    pj_opts_init(&o);
    str_copy(o.printer, sizeof o.printer, "laser");

    o.copies = 0;
    refused(&o, 0, "/tmp/x.pdf", "Copies must be", "refuse: 0 copies");
    o.copies = 100;
    refused(&o, 0, "/tmp/x.pdf", "Copies must be", "refuse: 100 copies");
    o.copies = -3;
    refused(&o, 0, "/tmp/x.pdf", "Copies must be", "refuse: -3 copies");
    o.copies = 1;

    for (int i = 0; bad_queues[i] != NULL; i++) {
        str_copy(o.printer, sizeof o.printer, bad_queues[i]);

        if (pj_check(&o, err, sizeof err) != -1) {
            printf("  not refused: %s\n", bad_queues[i]);
        } else {
            n++;
        }
    }

    check(n == 9, "refuse: every queue name that is an option, has a space, "
                  "a slash or a quote");
    str_copy(o.printer, sizeof o.printer, "Laser_Jet-4.office@host");
    check(pj_check(&o, err, sizeof err) == 0,
          "accept: a queue name with letters, digits and _ - . @");

    str_copy(o.printer, sizeof o.printer, "laser");
    o.page_from = 5;
    o.page_to = 2;
    refused(&o, 9, "/tmp/x.pdf", "first page is after", "refuse: 5 to 2");
    o.page_from = 12;
    o.page_to = 0;
    refused(&o, 9, "/tmp/x.pdf", "only 9 pages", "refuse: from beyond end");
    o.page_from = 0;
    o.page_to = 0;
    refused(&o, 0, "x.pdf", "full path", "refuse: a relative PDF path");

    o.printer[0] = '\0';
    refused(&o, 0, "/tmp/x.pdf", "No printer", "refuse: lp without a queue");
    check(pj_check(&o, err, sizeof err) == 0, "accept: no printer, no extras");

    str_copy(o.printer, sizeof o.printer, "File only");
    check(pj_printer_wanted(&o) == 0, "File only is no printer");
    str_copy(o.printer, sizeof o.printer, "file ONLY");
    check(pj_printer_wanted(&o) == 0, "File only in any case is no printer");

    o.page_from = 2;
    check(pj_check(&o, err, sizeof err) == -1 && strstr(err, "needs a "
          "printer") != NULL, "refuse: a page range without a printer");
    o.page_from = 0;
    o.copies = 2;
    check(pj_check(&o, err, sizeof err) == -1, "refuse: copies without a "
                                              "printer");
    o.copies = 1;
    o.sides = PJ_TWO;
    check(pj_check(&o, err, sizeof err) == -1, "refuse: sides without a "
                                              "printer");
    o.sides = PJ_DEFAULT;
    o.sort = PJ_SORT_YES;
    check(pj_check(&o, err, sizeof err) == -1, "refuse: sort without a "
                                              "printer");
    o.sort = PJ_SORT_DEFAULT;
    o.sides = 9;
    check(pj_check(&o, err, sizeof err) == -1, "refuse: an unknown sides");
}


static void
test_parse(void)
{
    int v = -1;
    char err[128];

    check(pj_parse_copies("1", &v, err, sizeof err) == 0 && v == 1,
          "parse: copies 1");
    check(pj_parse_copies("99", &v, err, sizeof err) == 0 && v == 99,
          "parse: copies 99");
    check(pj_parse_copies("0", &v, err, sizeof err) != 0, "parse: copies 0");
    check(pj_parse_copies("100", &v, err, sizeof err) != 0,
          "parse: copies 100");
    check(pj_parse_copies("", &v, err, sizeof err) != 0,
          "parse: empty copies");
    check(pj_parse_copies("2x", &v, err, sizeof err) != 0,
          "parse: copies 2x");
    check(pj_parse_copies(" 2", &v, err, sizeof err) != 0,
          "parse: copies with a space");
    check(pj_parse_copies("-1", &v, err, sizeof err) != 0,
          "parse: copies -1");
    check(pj_parse_copies("99999999999999999999", &v, err, sizeof err) != 0,
          "parse: a very long number");
    check(pj_parse_page("", &v, err, sizeof err) == 0 && v == 0,
          "parse: an empty page is none");
    check(pj_parse_page("7", &v, err, sizeof err) == 0 && v == 7,
          "parse: page 7");
    check(pj_parse_page("0", &v, err, sizeof err) != 0, "parse: page 0");
    check(pj_parse_page("100000", &v, err, sizeof err) != 0,
          "parse: page 100000");
    check(pj_parse_page("3-4", &v, err, sizeof err) != 0, "parse: page 3-4");
}


static void
test_names(void)
{
    char out[PATH_MAX];

    pj_default_out("/home/a/doc.doc", 0, out, sizeof out);
    check(strcmp(out, "/home/a/doc.pdf") == 0, "name: suffix replaced");
    pj_default_out("/home/a/doc.doc", 1, out, sizeof out);
    check(strcmp(out, "/home/a/doc-draft.pdf") == 0, "name: a draft");
    pj_default_out("/home/a.b/doc", 0, out, sizeof out);
    check(strcmp(out, "/home/a.b/doc.pdf") == 0,
          "name: a dot in the directory only");
    pj_default_out("doc.v2.doc", 0, out, sizeof out);
    check(strcmp(out, "doc.v2.pdf") == 0, "name: the last suffix only");
    pj_default_out("", 0, out, sizeof out);
    check(out[0] == '\0', "name: a document with no name");
    pj_default_out("/a/b/c.doc", 0, out, 8);
    check(strlen(out) < 8, "name: a small buffer is not overrun");
}



/* The answers of lpstat are kept for the run: each case starts afresh. */
static int
fresh_queues(PjQueues *q)
{
    pj_printers_reset();

    return pj_queues(q);
}


static int
fresh_default(char *out, size_t cap)
{
    pj_printers_reset();

    return pj_default_queue(out, cap);
}


static void
test_queues(const char *fake)
{
    PjQueues q;
    int n;

    setenv("STRATA_LPSTAT", fake, 1);
    setenv("FAKELPSTAT_QUEUES", "laser office -bad a/b", 1);
    n = fresh_queues(&q);
    check(n == 2 && strcmp(q.name[0], "laser") == 0 &&
          strcmp(q.name[1], "office") == 0,
          "queues: names are read, bad names are skipped");
    check(strcmp(q.list[0], PJ_FILE_ONLY) == 0 &&
          strcmp(q.list[1], PJ_PDF_VIEWER) == 0 &&
          strcmp(q.list[2], "laser") == 0 && strcmp(q.list[3], "office") ==
          0 && strcmp(q.list[4], PJ_SYSTEM_DEFAULT) == 0 &&
          strcmp(q.list[5], PJ_SEARCH) == 0 && q.list[6] == NULL &&
          q.nlist == 6,
          "queues: File only, PDF viewer, the queues, the system default");
    setenv("FAKELPSTAT_QUEUES", " ", 1);
    n = fresh_queues(&q);
    check(n == 0 && strcmp(q.list[2], PJ_SEARCH) == 0 && q.list[3] == NULL &&
          q.nlist == 3, "queues: none is none");
    setenv("STRATA_LPSTAT", "/no/such/lpstat", 1);
    n = fresh_queues(&q);
    check(n == -1 && strcmp(q.list[0], PJ_FILE_ONLY) == 0 &&
          strcmp(q.list[1], PJ_PDF_VIEWER) == 0 &&
          strcmp(q.list[2], PJ_SEARCH) == 0 && q.list[3] == NULL &&
          q.nlist == 3, "queues: no lpstat gives -1, File only and Open to "
          "print");
}


/* The default queue of the system, and the queues that do not accept jobs. */
static void
test_queue_states(const char *fake)
{
    PjQueues q;
    char def[PJ_QNAME];

    setenv("STRATA_LPSTAT", fake, 1);
    unsetenv("FAKELPSTAT_QUEUES");
    unsetenv("FAKELPSTAT_DEFAULT");
    unsetenv("FAKELPSTAT_OFF");
    check(fresh_default(def, sizeof def) == 1 && strcmp(def, "laser") == 0,
          "default queue: the one that lpstat -d tells");
    setenv("FAKELPSTAT_DEFAULT", "office", 1);
    check(fresh_default(def, sizeof def) == 1 &&
          strcmp(def, "office") == 0, "default queue: another one");
    setenv("FAKELPSTAT_DEFAULT", "", 1);
    check(fresh_default(def, sizeof def) == 0 && def[0] == '\0',
          "default queue: the system has none");
    setenv("STRATA_LPSTAT", "/no/such/lpstat", 1);
    check(fresh_default(def, sizeof def) == 0 && def[0] == '\0',
          "default queue: no lpstat, none");
    setenv("STRATA_LPSTAT", fake, 1);
    setenv("FAKELPSTAT_OFF", "office", 1);
    check(fresh_queues(&q) == 2 && q.off[0] == 0 && q.off[1] != 0,
          "queues: a queue that does not accept jobs is marked");
    setenv("FAKELPSTAT_QUEUES", "", 1);
    check(fresh_queues(&q) == 0 && q.nlist == 3 &&
          strcmp(q.list[2], PJ_SEARCH) == 0 && q.list[3] == NULL,
          "queues: none at all: no system default entry either");
    unsetenv("FAKELPSTAT_QUEUES");
    unsetenv("FAKELPSTAT_DEFAULT");
    unsetenv("FAKELPSTAT_OFF");
}


/* A file of a given mode in a directory. */
static void
put_file(const char *dir, const char *name, mode_t mode)
{
    char path[PATH_MAX];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "w");

    if (f != NULL) {
        fputs("#!/bin/sh\nexit 0\n", f);
        fclose(f);
    }

    chmod(path, mode);
}

/* The answer is asked for again when it is older than the refresh age. */
static void
test_queue_refresh(const char *fake)
{
    PjQueues q;

    setenv("STRATA_LPSTAT", fake, 1);
    setenv("FAKELPSTAT_QUEUES", "", 1);
    unsetenv("FAKELPSTAT_DEFAULT");
    unsetenv("FAKELPSTAT_OFF");
    setenv("STRATA_LPSTAT_REFRESH_MS", "600000", 1);
    check(fresh_queues(&q) == 0, "refresh: first answer, no queue");
    setenv("FAKELPSTAT_QUEUES", "laser", 1);
    check(pj_queues(&q) == 0,
          "refresh: a young answer is kept, a new queue is not seen yet");
    setenv("STRATA_LPSTAT_REFRESH_MS", "0", 1);

    /* Only a debug build reads the age from the variable; a release build
       keeps 30 seconds, so the answer is still the old one. */
    if (strata_release == 0) {
        check(pj_queues(&q) == 1 && strcmp(q.name[0], "laser") == 0,
              "refresh: an old answer is asked for again, the new queue "
              "shows with no restart");
    } else {
        check(pj_queues(&q) == 0,
              "refresh: a release build ignores the age variable and keeps "
              "the young answer");
    }

    check(strcmp(q.list[q.nlist - 1], PJ_SEARCH) == 0,
          "refresh: the search entry is the last one of the list");
    unsetenv("STRATA_LPSTAT_REFRESH_MS");
    unsetenv("FAKELPSTAT_QUEUES");
}


/* The devices of "lpinfo -v". */
static void
test_devices(void)
{
    PjDevice d[PJ_MAXDEV];
    const char *text =
        "network socket://192.168.1.201:9100\n"
        "network dnssd://HP%20LaserJet%20P2015dn%20%40%20host._ipp._tcp.local/"
        "?uuid=1\n"
        "direct usb://HP/LaserJet%20P2015dn?serial=XYZ\n"
        "serial serial://dev/ttyS0?baud=115200\n"
        "network ipp\n"
        "file cups-brf:/\n"
        "network ipp://printer.local:631/ipp/print\n";
    int n = pj_parse_devices(text, d, PJ_MAXDEV);

    check(n == 4, "devices: network and direct devices with an address are "
                  "taken, the other classes and the backends without one are "
                  "not");
    check(n == 4 && strcmp(d[0].cls, "network") == 0 &&
          strcmp(d[0].uri, "socket://192.168.1.201:9100") == 0 &&
          strcmp(d[0].name, "printer_192_168_1_201") == 0 &&
          d[0].model[0] == '\0' && d[0].driverless == 0,
          "devices: a socket device has the address as its name and needs a "
          "driver");
    check(n == 4 && strcmp(d[1].model, "HP LaserJet P2015dn @ host") == 0 &&
          strcmp(d[1].name, "HP_LaserJet_P2015dn") == 0 &&
          d[1].driverless == 1,
          "devices: a dnssd device has its decoded name, no driver is needed");
    check(n == 4 && strcmp(d[2].cls, "direct") == 0 &&
          strcmp(d[2].model, "HP LaserJet P2015dn") == 0 &&
          d[2].driverless == 0, "devices: a usb device has make and model");
    check(n == 4 && d[3].driverless == 1 &&
          strcmp(d[3].name, "printer_printer_local") == 0,
          "devices: an ipp device needs no driver");
    check(pj_parse_devices("", d, PJ_MAXDEV) == 0 &&
          pj_parse_devices("lpinfo: Forbidden\n", d, PJ_MAXDEV) == 0,
          "devices: nothing, or an error text, gives no device");
}



/*
 * The policy of a PDF viewer: the setting is a full path of a program that is
 * safe to run; a name is never searched in PATH.
 */
static void
test_viewer_policy(void)
{
    char dir[300];
    char safe[320];
    char loose[320];
    char sticky[320];
    char wide[320];
    char poison[320];
    char path[700];
    char found[PATH_MAX];
    char why[256];
    char old[2048];
    const char *dirs[2];

    tmp_path(dir, sizeof dir, "pjview.XXXXXX");

    if (mkdtemp(dir) == NULL) {
        printf("FAIL cannot make a directory\n");
        failures++;

        return;
    }

    snprintf(safe, sizeof safe, "%s/safe", dir);
    snprintf(loose, sizeof loose, "%s/loose", dir);
    snprintf(sticky, sizeof sticky, "%s/sticky", dir);
    snprintf(wide, sizeof wide, "%s/wide", dir);
    snprintf(poison, sizeof poison, "%s/poison", dir);
    mkdir(safe, 0755);
    mkdir(loose, 0755);
    mkdir(sticky, 0755);
    chmod(sticky, 01777);
    mkdir(wide, 0755);
    chmod(wide, 0777);
    mkdir(poison, 0755);
    put_file(safe, "tool", 0755);
    put_file(safe, "evince", 0755);
    put_file(safe, "plain", 0644);
    put_file(safe, "gw", 0775);
    put_file(safe, "ow", 0757);
    put_file(safe, "soffice", 0755);
    put_file(loose, "tool", 0775);
    put_file(sticky, "stk", 0755);
    put_file(wide, "wd", 0755);
    put_file(poison, "okular", 0755);
    str_copy(old, sizeof old, getenv("PATH") != NULL ? getenv("PATH") : "");

    snprintf(path, sizeof path, "%s/tool", safe);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_OK && strcmp(found, path) == 0,
          "viewer: a full path of a safe program is accepted");

    {
        static const char *const bad[] = {"", "tool", "evince", "./tool",
            "../tool", "a b", "a;b", "a'b", "a\"b", "$(x)", "-tool",
            "rel/tool", "/a/../b", "/a/..", "~/tool", "tool\n", "/to ol",
            "a|b", "/a;b", "/a$b", "/a\nb", NULL};
        int all = 1;

        for (int i = 0; bad[i] != NULL; i++) {
            if (pj_viewer_check(bad[i], found, sizeof found, why,
                                sizeof why) != PJ_VIEWER_BAD_FORM ||
                found[0] != '\0' ||
                strstr(why, "full path, such as /usr/bin/evince") == NULL) {
                all = 0;
                printf("  accepted: [%s]\n", bad[i]);
            }
        }

        check(all != 0, "viewer: a name, a relative path and a path with a "
                        "space or a shell character are refused, and the "
                        "reason says to give the full path");
    }

    /* A name is not searched in PATH, whatever PATH holds. */
    setenv("PATH", safe, 1);
    check(pj_viewer_check("tool", found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_BAD_FORM,
          "viewer: a name that is in PATH is refused all the same");
    setenv("PATH", old, 1);

    snprintf(path, sizeof path, "%s/soffice", safe);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_PROCESSOR && strstr(why, "word processor") != NULL,
          "viewer: a word processor is refused, and the reason says so");
    snprintf(path, sizeof path, "%s/nothere", safe);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_NOT_FOUND && strstr(why, "not an executable") != NULL,
          "viewer: a path of no file is refused");
    snprintf(path, sizeof path, "%s/plain", safe);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_NOT_FOUND,
          "viewer: a file that cannot be run is refused");
    check(pj_viewer_check(safe, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_NOT_FOUND,
          "viewer: a directory is refused");
    snprintf(path, sizeof path, "%s/gw", safe);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_UNSAFE && found[0] == '\0',
          "viewer: a program that its group can write is not safe");
    snprintf(path, sizeof path, "%s/ow", safe);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_UNSAFE,
          "viewer: a program that others can write is not safe");
    snprintf(path, sizeof path, "%s/tool", loose);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_UNSAFE,
          "viewer: the same name, group writable: not safe");
    snprintf(path, sizeof path, "%s/stk", sticky);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_UNSAFE,
          "viewer: a directory that anyone can write is not safe, the sticky "
          "bit or not");
    snprintf(path, sizeof path, "%s/wd", wide);
    check(pj_viewer_check(path, found, sizeof found, why, sizeof why) ==
              PJ_VIEWER_UNSAFE,
          "viewer: a directory that anyone can write, with no sticky bit, is "
          "not safe");

    /* The default is looked for in the fixed directories and not in PATH. */
    dirs[0] = safe;
    dirs[1] = NULL;
    pj_viewer_dirs_set(dirs);
    setenv("PATH", poison, 1);
    snprintf(path, sizeof path, "%s/evince", safe);
    check(pj_viewer_default(found, sizeof found) == 1 &&
              strcmp(found, path) == 0,
          "viewer: the default is the viewer of the fixed directory");
    dirs[0] = loose;
    pj_viewer_dirs_set(dirs);
    check(pj_viewer_default(found, sizeof found) == 0 && found[0] == '\0',
          "viewer: a viewer that is only in PATH is not the default");
    dirs[0] = loose;
    pj_viewer_dirs_set(dirs);
    setenv("PATH", safe, 1);
    check(pj_viewer_default(found, sizeof found) == 0,
          "viewer: and PATH is not looked at when the fixed directory has "
          "none");
    pj_viewer_dirs_set(NULL);
    setenv("PATH", old, 1);

    {
        static const char *const gone[][2] = {
            {"safe", "tool"}, {"safe", "evince"}, {"safe", "plain"},
            {"safe", "gw"}, {"safe", "ow"}, {"safe", "soffice"},
            {"loose", "tool"}, {"sticky", "stk"}, {"wide", "wd"},
            {"poison", "okular"}, {NULL, NULL}};

        for (int i = 0; gone[i][0] != NULL; i++) {
            snprintf(path, sizeof path, "%s/%s/%s", dir, gone[i][0],
                     gone[i][1]);
            unlink(path);
        }
    }

    rmdir(safe);
    rmdir(loose);
    rmdir(sticky);
    rmdir(wide);
    rmdir(poison);
    rmdir(dir);
}


/* The PDF Directory: its text, and the default of the Output File. */
static void
test_pdf_dir(void)
{
    char dir[300];
    char sub[320];
    char ro[320];
    char got[PATH_MAX];
    char out[PATH_MAX];
    char note[256];
    char why[256];
    char doc[PATH_MAX];
    char home[320];

    tmp_path(dir, sizeof dir, "pjpdfd.XXXXXX");

    if (mkdtemp(dir) == NULL) {
        printf("FAIL cannot make a directory\n");
        failures++;

        return;
    }

    snprintf(sub, sizeof sub, "%s/pdfs", dir);
    snprintf(ro, sizeof ro, "%s/readonly", dir);
    mkdir(sub, 0755);
    mkdir(ro, 0555);
    snprintf(home, sizeof home, "%s", dir);
    setenv("HOME", home, 1);
    check(pj_pdf_dir_check("", got, sizeof got, why, sizeof why) == 0 &&
          got[0] == '\0', "pdf dir: empty is the directory of the document");
    check(pj_pdf_dir_check("pdfs", got, sizeof got, why, sizeof why) == 1 &&
          strstr(why, "relative") != NULL,
          "pdf dir: a relative path is refused, and the reason says so");
    check(pj_pdf_dir_check(sub, got, sizeof got, why, sizeof why) == 0 &&
          strcmp(got, sub) == 0, "pdf dir: a full path that can be used");
    check(pj_pdf_dir_check("~/pdfs", got, sizeof got, why, sizeof why) == 0 &&
          strcmp(got, sub) == 0, "pdf dir: ~/ is the home directory");
    check(pj_pdf_dir_check("~/nothere", got, sizeof got, why, sizeof why) ==
          2 && strstr(why, "does not exist") != NULL,
          "pdf dir: a directory that does not exist");
    check(pj_pdf_dir_check(ro, got, sizeof got, why, sizeof why) == 3 &&
          strstr(why, "cannot be written") != NULL,
          "pdf dir: a directory that cannot be written");

    snprintf(doc, sizeof doc, "/home/a/doc.doc");
    pj_default_out_in(doc, 0, "", out, sizeof out, note, sizeof note);
    check(strcmp(out, "/home/a/doc.pdf") == 0 && note[0] == '\0',
          "default out: the directory of the document when none is set");
    pj_default_out_in(doc, 1, sub, out, sizeof out, note, sizeof note);
    snprintf(got, sizeof got, "%s/doc-draft.pdf", sub);
    check(strcmp(out, got) == 0 && note[0] == '\0',
          "default out: NAME-draft.pdf in the PDF Directory");
    pj_default_out_in(doc, 0, "~/pdfs", out, sizeof out, note, sizeof note);
    snprintf(got, sizeof got, "%s/doc.pdf", sub);
    check(strcmp(out, got) == 0, "default out: the PDF Directory with ~/");
    pj_default_out_in(doc, 0, "~/nothere", out, sizeof out, note,
                      sizeof note);
    check(strcmp(out, "/home/a/doc.pdf") == 0 &&
          strstr(note, "does not exist") != NULL &&
          strstr(note, "document's directory") != NULL,
          "default out: an unusable directory: the document's, and a note");
    pj_default_out_in("", 0, sub, out, sizeof out, note, sizeof note);
    check(out[0] == '\0', "default out: no document, no name");

    chmod(ro, 0755);
    rmdir(ro);
    rmdir(sub);
    rmdir(dir);
}


/* Write a small program that appends its name and arguments to a log. */
static void
make_fake(const char *dir, const char *name, const char *log)
{
    char path[PATH_MAX];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "w");

    if (f == NULL) {
        printf("FAIL cannot make %s\n", path);
        failures++;

        return;
    }

    fprintf(f, "#!/bin/sh\necho \"%s $*\" >> %s\n", name, log);
    fprintf(f, "echo \"argv0=$0 preload=[${LD_PRELOAD:-}] "
               "audit=[${LD_AUDIT:-}]\" >> %s.env\n", log);
    fprintf(f, "exit 0\n");
    fclose(f);
    chmod(path, 0755);
}


/* The text of the log after a moment: the opener is not waited for. */
static void
log_text(const char *log, char *out, size_t cap)
{
    out[0] = '\0';

    for (int i = 0; i < 40; i++) {
        FILE *f = fopen(log, "r");

        if (f != NULL) {
            size_t n = fread(out, 1, cap - 1, f);

            out[n] = '\0';
            fclose(f);

            if (n > 0) {
                return;
            }
        }

        usleep(50000);
    }
}


/* Whether the log stays without a line for a moment: nothing was started. */
static int
log_stays_empty(const char *log)
{
    usleep(300000);

    return access(log, F_OK) != 0;
}


static void
test_open(void)
{
    static const char none[] = "no PDF viewer was found: set one in Setup";
    static const char *const viewers[] = {"evince", "okular", "atril",
                                          "zathura", "qpdfview", "mupdf",
                                          "xpdf", "epdfview", NULL};
    char dir[300];
    char bin[320];
    char poison[320];
    char marker[320];
    char log[320];
    char got[512];
    char why[256];
    char want[320];
    char envwant[400];
    char envlog[400];
    const char *dirs[2];
    PjOpts o;
    PjOpts q;

    tmp_path(dir, sizeof dir, "pjopen.XXXXXX");

    if (mkdtemp(dir) == NULL) {
        printf("FAIL cannot make a directory\n");
        failures++;

        return;
    }

    snprintf(bin, sizeof bin, "%s/bin", dir);
    mkdir(bin, 0755);
    snprintf(poison, sizeof poison, "%s/poison", dir);
    mkdir(poison, 0755);
    snprintf(marker, sizeof marker, "%s/marker", dir);
    snprintf(log, sizeof log, "%s/log", dir);
    pj_opts_init(&o);
    str_copy(o.printer, sizeof o.printer, PJ_PDF_VIEWER);
    check(pj_open_wanted(&o) != 0 && pj_printer_wanted(&o) == 0 &&
              strcmp(PJ_PDF_VIEWER, "PDF viewer") == 0,
          "open: PDF viewer is not a queue and is wanted");
    str_copy(o.printer, sizeof o.printer, "Open to print");
    check(pj_open_wanted(&o) != 0 && pj_printer_wanted(&o) == 0,
          "open: the old words Open to print are still wanted");
    str_copy(o.printer, sizeof o.printer, "pdf VIEWER");
    check(pj_open_wanted(&o) != 0, "open: the words need no capitals");
    str_copy(o.printer, sizeof o.printer, "File only");
    check(pj_open_wanted(&o) == 0 && pj_printer_wanted(&o) == 0,
          "open: File only is neither");
    str_copy(o.printer, sizeof o.printer, "laser");
    check(pj_open_wanted(&o) == 0 && pj_printer_wanted(&o) != 0,
          "open: a queue is neither");
    pj_opts_init(&q);
    str_copy(q.printer, sizeof q.printer, PJ_PDF_VIEWER);
    check(pj_check(&q, why, sizeof why) == 0,
          "open: the name PDF viewer is accepted");
    q.copies = 2;
    check(pj_check(&q, why, sizeof why) == -1 &&
          strstr(why, "More than one copy needs a printer") != NULL &&
          strstr(why, "print dialog") != NULL,
          "open: copies are refused and the reason says why");
    q.copies = 1;
    str_copy(q.printer, sizeof q.printer, "Open  to print");
    check(pj_check(&q, why, sizeof why) == -1 &&
          strstr(why, "not the name of a printer queue") != NULL,
          "open: other words with a space are refused");

    /* The viewers of the test are in the fixed directory that the test
       sets; PATH holds a directory with a poisoned program of the name
       evince, which must never run. */
    dirs[0] = bin;
    dirs[1] = NULL;
    pj_viewer_dirs_set(dirs);
    make_fake(poison, "evince", marker);
    setenv("PATH", poison, 1);
    setenv("DISPLAY", ":99", 1);
    unsetenv("WAYLAND_DISPLAY");
    pj_opts_init(&o);
    str_copy(o.printer, sizeof o.printer, PJ_PDF_VIEWER);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0 &&
              strcmp(why, none) == 0 && log_stays_empty(marker),
          "open: no viewer in the fixed directory: not started, and the "
          "evince of PATH did not run");
    check(pj_open_pdf(&o, "x.pdf", why, sizeof why) == 0 &&
          strstr(why, "full path") != NULL,
          "open: the PDF must be given with its full path");

    /* The general openers of the system are not looked for: they may start
       a word processor. */
    make_fake(bin, "xdg-open", log);
    make_fake(bin, "gio", log);
    make_fake(bin, "open", log);
    make_fake(bin, "firefox", log);
    make_fake(bin, "soffice", log);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0 &&
          strcmp(why, none) == 0 && log_stays_empty(log),
          "open: xdg-open, gio, open, a browser and soffice are not started");

    /* The viewers, from the last of the list to the first: each is used
       when it is the first of the list that is in PATH. */
    for (int i = 7; i >= 0; i--) {
        make_fake(bin, viewers[i], log);
        snprintf(want, sizeof want, "%s /tmp/x.pdf\n", viewers[i]);
        got[0] = '\0';
        check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 1,
              "open: a viewer of the list is started");
        log_text(log, got, sizeof got);
        printf("%s %s", strcmp(got, want) == 0 ? "ok  " : "FAIL",
               "open: the first viewer of the list is the one that runs\n");
        checks++;

        if (strcmp(got, want) != 0) {
            failures++;
            printf("  wanted %s  got %s", want, got);
        }

        unlink(log);
    }

    unsetenv("DISPLAY");
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0 &&
          strcmp(why, none) == 0 && log_stays_empty(log),
          "open: no display: no viewer is started, and the same message");
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 1,
          "open: a Wayland display is a display");
    log_text(log, got, sizeof got);
    check(strcmp(got, "evince /tmp/x.pdf\n") == 0,
          "open: with a Wayland display evince was run");
    unlink(log);
    unsetenv("WAYLAND_DISPLAY");

    /* The program is run by its full path, and the variables that load
       another library into it are taken out of its environment. */
    setenv("DISPLAY", ":99", 1);
    setenv("LD_PRELOAD", "/nonexistent/evil.so", 1);
    setenv("LD_AUDIT", "/nonexistent/audit.so", 1);
    snprintf(envlog, sizeof envlog, "%s.env", log);
    unlink(envlog);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 1,
          "open: a viewer is started with LD_PRELOAD set in the run");
    log_text(envlog, got, sizeof got);
    snprintf(envwant, sizeof envwant, "argv0=%s/evince preload=[] audit=[]",
             bin);
    check(strstr(got, envwant) != NULL,
          "open: argv[0] is the full path, and LD_PRELOAD and LD_AUDIT are "
          "not passed on");
    unsetenv("LD_PRELOAD");
    unsetenv("LD_AUDIT");
    unlink(envlog);
    unlink(log);
    unsetenv("DISPLAY");

    /* The poisoned evince of PATH was never run, in all the starts above. */
    check(log_stays_empty(marker),
          "open: the program of PATH named evince never ran");

    /* The setting wins, on any machine, with a display or without. */
    snprintf(o.viewer, sizeof o.viewer, "%s/mine", bin);
    make_fake(bin, "mine", log);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 1,
          "open: the setting names a program with its path");
    log_text(log, got, sizeof got);
    check(strcmp(got, "mine /tmp/x.pdf\n") == 0,
          "open: the program of the setting was run, not evince");
    unlink(log);
    /* A name or a relative path (an old settings file) is never run: it is
       treated as not given, and without a display nothing is started. */
    setenv("DISPLAY", ":99", 1);
    str_copy(o.viewer, sizeof o.viewer, "mine");
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 1,
          "open: a setting that is a name is ignored: the default viewer "
          "of the fixed directory is started instead");
    log_text(log, got, sizeof got);
    check(strcmp(got, "evince /tmp/x.pdf\n") == 0,
          "open: and it was the default viewer, not the name of the setting");
    unlink(log);
    unsetenv("DISPLAY");
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0 &&
              strcmp(why, none) == 0 && log_stays_empty(log),
          "open: a setting that is a name, without a display: nothing runs");
    setenv("DISPLAY", ":99", 1);
    str_copy(o.viewer, sizeof o.viewer, "./mine");
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 1,
          "open: a relative path is ignored too");
    log_text(log, got, sizeof got);
    check(strcmp(got, "evince /tmp/x.pdf\n") == 0,
          "open: a relative path never ran");
    unlink(log);
    snprintf(o.viewer, sizeof o.viewer, "%s/nothere", bin);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0 &&
              strstr(why, "setting pdf_viewer") != NULL,
          "open: a program of the setting that is not there: not started");
    snprintf(o.viewer, sizeof o.viewer, "%s/notrunnable", bin);
    {
        char path[PATH_MAX];
        FILE *f;

        snprintf(path, sizeof path, "%s/notrunnable", bin);
        f = fopen(path, "w");

        if (f != NULL) {
            fputs("not a program\n", f);
            fclose(f);
        }
    }
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0,
          "open: a file that cannot be run is not started");

    /* A word processor is refused, as a name and as a path. */
    {
        static const char *const bad[] = {"soffice", "libreoffice",
            "LibreOffice", "/usr/bin/libreoffice7.6", "/usr/lib/"
            "libreoffice/program/soffice.bin", "oowriter", "oodraw",
            "lowriter", "lodraw", "abiword", "/opt/kingsoft/wps-office/wps",
            "calligrawords", "winword.exe", "SOFFICE", NULL};
        static const char *const good[] = {"mine", "evince", "/usr/bin/"
            "zathura", "okular", "mupdf-x11", "my-viewer", NULL};
        int all = 1;
        int clean = 1;

        for (int i = 0; bad[i] != NULL; i++) {
            if (pj_viewer_refused(bad[i]) == 0) {
                all = 0;
                printf("  not refused: %s\n", bad[i]);
            }
        }

        for (int i = 0; good[i] != NULL; i++) {
            if (pj_viewer_refused(good[i]) != 0) {
                clean = 0;
                printf("  refused: %s\n", good[i]);
            }
        }

        check(all != 0, "open: every name of a word processor is refused");
        check(clean != 0, "open: a viewer or any other name is not refused");
    }

    snprintf(o.viewer, sizeof o.viewer, "%s/soffice", bin);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0 &&
          strcmp(why, none) == 0 && log_stays_empty(log),
          "open: soffice as the setting is refused, and not started");
    snprintf(o.viewer, sizeof o.viewer, "%s/libreoffice", bin);
    make_fake(bin, "libreoffice", log);
    check(pj_open_pdf(&o, "/tmp/x.pdf", why, sizeof why) == 0 &&
              strcmp(why, none) == 0 && log_stays_empty(log),
          "open: libreoffice as the setting is refused, and not started");
    pj_viewer_dirs_set(NULL);

    /* Remove the files of the test. */
    {
        static const char *const names[] = {"open", "gio", "xdg-open", "mine",
            "firefox", "soffice", "libreoffice", "notrunnable", "evince",
            "okular", "atril",
            "zathura", "qpdfview", "mupdf", "xpdf", "epdfview", NULL};
        char path[PATH_MAX];

        for (int i = 0; names[i] != NULL; i++) {
            snprintf(path, sizeof path, "%s/%s", bin, names[i]);
            unlink(path);
        }

        snprintf(path, sizeof path, "%s/evince", poison);
        unlink(path);
        unlink(marker);
        unlink(log);
        snprintf(path, sizeof path, "%s.env", log);
        unlink(path);
        rmdir(poison);
        rmdir(bin);
        rmdir(dir);
    }
}


int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: pjtest FAKELPSTAT\n");

        return 2;
    }

    char fake[PATH_MAX];

    test_args();
    test_refusals();
    test_parse();
    test_names();
    /* A program that a variable names is a full path. */
    if (realpath(argv[1], fake) == NULL) {
        fprintf(stderr, "pjtest: no file %s\n", argv[1]);

        return 2;
    }

    test_queues(fake);
    test_queue_refresh(fake);
    test_devices();
    test_queue_states(fake);
    test_viewer_policy();
    test_pdf_dir();
    test_open();
    printf("pjtest: %d checks, %d failures\n", checks, failures);

    return failures != 0 ? 1 : 0;
}
