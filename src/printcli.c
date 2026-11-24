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

/* printcli.c - stred --print; see printcli.h. */
#include "util.h"
#include "printcli.h"
#include "fileops.h"
#include "printjob.h"
#include "printmenu.h"
#include "config.h"
#include "errlist.h"
#include "log.h"
#include "vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    EXIT_OK = 0,
    EXIT_FAILED = 1,
    EXIT_USAGE = 2,
    EXIT_NOTSENT = 3,
    EXIT_STOPPED = 4,
    EXIT_DIAG = 5
};


int
print_part(Doc *d, int cur_item, int current_only, int title,
           const char *from, const char *to, Doc *out, char *err,
           size_t errlen)
{
    Doc whole;

    err[0] = '\0';

    if (current_only != 0) {
        if (doc_part_current(d, cur_item, out) != 0) {
            snprintf(err, errlen, "There is no current section");

            return -1;
        }

        return 0;
    }

    if (from[0] != '\0' || to[0] != '\0') {
        if (d->structured == 0) {
            snprintf(err, errlen, "Only a structured document has sections");

            return -1;
        }

        return doc_part_sections(d, from, to, out, err, errlen);
    }

    if (title != 0) {
        *out = *d;
        doc_clear_view_pos(out);
        out->items = malloc(usz(d->nitems + 1) * sizeof *out->items);

        if (out->items == NULL) {
            snprintf(err, errlen, "Out of memory");

            return -1;
        }

        memcpy(out->items, d->items, usz(d->nitems) * sizeof *out->items);

        return 0;
    }

    if (doc_part_untitled(d, &whole) != 0) {
        snprintf(err, errlen, "There is nothing to print");

        return -1;
    }

    *out = whole;

    return 0;
}


static void
usage(void)
{
    fputs("usage: stred --print FILE.doc [--printer QUEUE] [--copies N]\n"
          "       [--sides one|two] [--sort yes|no] [-o OUT.pdf]\n"
          "       [--from SECTION] [--to SECTION] [--from-page N] "
          "[--to-page N]\n"
          "       [--no-title] [--double-spaced] [--black-and-white]\n"
          "       [--recover]\n"
          "       [--index] [--list-figures] [--list-pictures] "
          "[--list-tables] [--no-index] [--no-list-figures] "
          "[--no-list-pictures] [--no-list-tables] "
          "[--contents LEVEL] "
          "[--draft] [--no-error-log] [--no-banner] [--errors FILE] [-v]\n",
          stderr);
}


/* The directory of a path, "." for none. */
static void
docdir_of(const char *path, char *out, size_t cap)
{
    const char *sl = strrchr(path, '/');

    if (sl == NULL) {
        str_copy(out, cap, ".");
    } else if (sl == path) {
        str_copy(out, cap, "/");
    } else {
        size_t n = lsz(sl - path);

        n = n < cap - 1 ? n : cap - 1;
        memcpy(out, path, n);
        out[n] = '\0';
    }
}


/* What the tick of the command line remembers. */
typedef struct {
    int verbose; /* 1: tell the progress on the standard error. */
    int phase;
    int pages;
} Cli;


/* The tick of the command line: say the progress when asked, and wait. */
static int
cli_tick(void *ctx, int phase, PvProg *prog, int changed)
{
    Cli *c = ctx;

    if (c->verbose != 0 && changed != 0) {
        if (phase == PJ_SEND && c->phase != PJ_SEND) {
            fprintf(stderr, "Sending to the printer\n");
        } else if (phase == PJ_TYPESET && prog->pages != c->pages) {
            int about;
            int total = pv_prog_total(prog, &about);

            fprintf(stderr, about != 0 ? "Page %d of about %d\n" :
                                         "Page %d of %d\n", prog->pages,
                    total);
        }

        c->phase = phase;
        c->pages = prog->pages;
    }

    usleep(50000);

    return 0;
}


int
print_cli(int argc, char **argv)
{
    PjOpts o;
    const char *file = NULL;
    const char *from = "";
    const char *to = "";
    const char *outname = NULL;
    const char *errors_path = NULL;
    int title = 1;
    char err[256];
    char docdir[PATH_MAX];
    char fromsec[SECT_NUMLEN];
    char tosec[SECT_NUMLEN];
    Doc *d;
    int recover = 0;
    Doc part;
    PjResult res;
    Cli cli;
    Config cfg;
    int printer_given = 0;

    pj_opts_init(&o);
    memset(&cli, 0, sizeof cli);
    config_load(&cfg);
    str_copy(o.viewer, sizeof o.viewer, cfg.pdf_viewer);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;

        if (strcmp(a, "--print") == 0) {
            continue;
        } else if (strcmp(a, "--printer") == 0 && more) {
            str_copy(o.printer, sizeof o.printer, argv[++i]);
            printer_given = 1;
        } else if (strcmp(a, "--copies") == 0 && more) {
            if (pj_parse_copies(argv[++i], &o.copies, err, sizeof err) != 0) {
                fprintf(stderr, "stred: %s\n", err);

                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--sides") == 0 && more) {
            const char *v = argv[++i];

            if (strcmp(v, "one") == 0) {
                o.sides = PJ_ONE;
            } else if (strcmp(v, "two") == 0) {
                o.sides = PJ_TWO;
            } else {
                fprintf(stderr, "stred: --sides is one or two\n");

                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--sort") == 0 && more) {
            const char *v = argv[++i];

            if (strcmp(v, "yes") == 0) {
                o.sort = PJ_SORT_YES;
            } else if (strcmp(v, "no") == 0) {
                o.sort = PJ_SORT_NO;
            } else {
                fprintf(stderr, "stred: --sort is yes or no\n");

                return EXIT_USAGE;
            }
        } else if (strcmp(a, "-o") == 0 && more) {
            outname = argv[++i];
        } else if (strcmp(a, "--from") == 0 && more) {
            from = argv[++i];
        } else if (strcmp(a, "--to") == 0 && more) {
            to = argv[++i];
        } else if (strcmp(a, "--from-page") == 0 && more) {
            if (pj_parse_page(argv[++i], &o.page_from, err, sizeof err) !=
                0) {
                fprintf(stderr, "stred: %s\n", err);

                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--to-page") == 0 && more) {
            if (pj_parse_page(argv[++i], &o.page_to, err, sizeof err) != 0) {
                fprintf(stderr, "stred: %s\n", err);

                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--no-title") == 0) {
            title = 0;
        } else if (strcmp(a, "--double-spaced") == 0) {
            o.double_spaced = 1;
        } else if (strcmp(a, "--black-and-white") == 0) {
            o.black_white = 1;
        } else if (strcmp(a, "--recover") == 0) {
            recover = 1;
        } else if (strcmp(a, "--index") == 0) {
            o.index = 1;
        } else if (strcmp(a, "--list-figures") == 0) {
            o.list_figures = 1;
        } else if (strcmp(a, "--list-pictures") == 0) {
            o.list_pictures = 1;
        } else if (strcmp(a, "--list-tables") == 0) {
            o.list_tables = 1;
        } else if (strcmp(a, "--no-index") == 0) {
            o.index = 0;
        } else if (strcmp(a, "--no-list-figures") == 0) {
            o.list_figures = 0;
        } else if (strcmp(a, "--no-list-pictures") == 0) {
            o.list_pictures = 0;
        } else if (strcmp(a, "--no-list-tables") == 0) {
            o.list_tables = 0;
        } else if (strcmp(a, "--contents") == 0 && more) {
            o.contents = atoi(argv[++i]);

            if (o.contents < 0 || o.contents > 99) {
                fprintf(stderr,
                        "stred: --contents is a level from 0 to 99\n");

                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--draft") == 0) {
            o.draft = 1;
        } else if (strcmp(a, "--no-error-log") == 0) {
            o.error_log = 0;
        } else if (strcmp(a, "--errors") == 0 && more) {
            errors_path = argv[++i];
        } else if (strcmp(a, "--error-log") == 0) {
            o.error_log = 1;
        } else if (strcmp(a, "--no-banner") == 0) {
            o.banner = 0;
        } else if (strcmp(a, "--banner") == 0) {
            o.banner = 1;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            cli.verbose = 1;
        } else if (strcmp(a, "--var") == 0 || strcmp(a, "--special") == 0) {
            if (vars_option(argc, argv, &i, err, sizeof err) != 1) {
                log_error("stred: the option %s was refused and the print "
                          "does not start: %s", a, err);
                fprintf(stderr, "stred: %s\n", err);

                return EXIT_USAGE;
            }
        } else if (a[0] != '-' && file == NULL) {
            file = a;
        } else {
            usage();

            return EXIT_USAGE;
        }
    }

    if (file == NULL) {
        usage();

        return EXIT_USAGE;
    }

    if (outname != NULL) {
        str_copy(o.out, sizeof o.out, outname);
    } else {
        char note[256];

        pj_default_out_in(file, o.draft, cfg.pdf_dir, o.out, sizeof o.out,
                          note, sizeof note);

        if (note[0] != '\0') {
            fprintf(stderr, "stred: %s\n", note);
        }
    }

    /* Without --printer the print goes where the setting Print To of Setup names. */
    if (printer_given == 0) {
        char note[256];

        printmenu_default_printer(&cfg, NULL, o.printer, sizeof o.printer,
                                  note, sizeof note);

        if (note[0] != '\0') {
            fprintf(stderr, "stred: %s\n", note);
        }
    }

    {
        const char *base = strrchr(file, '/');

        str_copy(o.title, sizeof o.title, base != NULL ? base + 1 : file);
    }

    if (recover != 0) {
        DocRecovery rc;

        d = doc_recover(file, &rc, err, sizeof err);

        if (d != NULL && rc.damaged != 0) {
            fprintf(stderr, "%s: %s; printing the part that can be read (%d "
                    "blocks and %d sections, %lu of %lu bytes); the file is "
                    "not changed\n", file, rc.why, rc.blocks, rc.sections,
                    (unsigned long)rc.bytes_read,
                    (unsigned long)rc.file_size);
        }
    } else {
        d = doc_load(file, err, sizeof err);
    }

    if (d == NULL) {
        fprintf(stderr, "%s: %s\n", file, err);

        return EXIT_FAILED;
    }

    if (pj_check(&o, err, sizeof err) != 0 ||
        pj_check_out(o.out, err, sizeof err) != 0) {
        fprintf(stderr, "stred: %s\n", err);
        doc_free(d);

        return EXIT_USAGE;
    }

    str_copy(fromsec, sizeof fromsec, from);
    str_copy(tosec, sizeof tosec, to);

    if (print_part(d, 0, 0, title, fromsec, tosec, &part, err,
                   sizeof err) != 0) {
        fprintf(stderr, "stred: %s\n", err);
        doc_free(d);

        return EXIT_USAGE;
    }

    docdir_of(file, docdir, sizeof docdir);
    str_copy(o.source_path, sizeof o.source_path, file);
    pj_run(&o, &part, docdir, 1, cli_tick, &cli, &res);

    /* The items of the diagnostics are those of the part: make them the
       items of the document, so that the places are told for it. */
    diag_remap_array(res.diag, res.ndiag, &part, d);
    doc_part_free(&part);

    /* The error file is written with the items of the document. */
    if (errors_path != NULL) {
        DiagList fl;
        int fst = res.status == PJ_DONE ? (res.ndiag > 0 ? 4 : 0) : 1;

        diag_list_init(&fl);
        diag_list_set(&fl, res.diag, res.ndiag, res.ndiag_dropped);

        if (diag_file_write(errors_path, &fl, fst) != 0) {
            fprintf(stderr, "stred: cannot write %s\n", errors_path);
        }

        diag_list_free(&fl);
    }

    if (cli.verbose != 0 && cli.pages == 0 && res.pages > 0) {
        /* A short run ended before the first tick saw a page. */
        fprintf(stderr, "Page %d of %d\n", res.pages, res.pages);
    }

    if (res.status == PJ_DONE) {
        printf("%s\n", res.msg);
    } else {
        fprintf(stderr, "%s\n", res.msg);
    }

    /* The errors and warnings of the document, as text. */
    if (res.ndiag > 0) {
        char *text = errlist_text(res.diag, res.ndiag, res.ndiag_dropped);

        int failed = 0;

        for (int i = 0; i < res.ndiag; i++) {
            if (res.diag[i].kind == DIAG_ERROR) {
                failed = 1;
            }
        }

        fprintf(stderr, "%s: the document has errors or warnings:\n", file);

        if (failed != 0) {
            fprintf(stderr, "PRINT FAILED: the output was made, but it is "
                            "inaccurate.\n");
        }

        if (text != NULL) {
            fputs(text, stderr);
            free(text);
        }
    }

    doc_free(d);

    if (res.status == PJ_DONE) {
        return res.ndiag > 0 ? EXIT_DIAG : EXIT_OK;
    }

    if (res.status == PJ_NOTSENT) {
        return EXIT_NOTSENT;
    }

    return res.status == PJ_CANCELLED ? EXIT_STOPPED : EXIT_FAILED;
}
