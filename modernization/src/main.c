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

/* main.c - stred: the Strata document editor (reads and writes the .DOC
   files of the format of docs/DOC-FORMAT.txt). */
#include "util.h"
#include "ui.h"
#include "spellui.h"
#include "printcli.h"
#include "log.h"
#include "color.h"
#include "vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static void
usage(void)
{
    puts("usage: stred [options] [file.doc]\n"
         "  -H, --help-file PATH   the help file (default: $STRATA_HELP, "
         "then the install dirs)\n"
         "  --text FILE.doc OUT    render a document to formatted plain text "
         "and exit\n"
         "  --preview FILE.doc     show the pages with LaTeX and the viewer\n"
         "  --no-error-log         with --preview: no Error Log in the pages\n"
         "  --no-banner            with --preview: no PRINT FAILED banner\n"
         "  --spell FILE.doc       check the spelling of the document and "
         "offer to save it\n"
         "  --auto yes|no          with --spell: Auto Correct (default: "
         "yes)\n"
         "  -u, --unstructured     a new file that does not exist is created "
         "unstructured\n"
         "  --autosave yes|no      autosave of a changed document (default: "
         "yes)\n"
         "  --autosave-seconds N   the autosave interval (default: 120)\n"
         "  --var NAME VALUE       a variable of the If markers (repeat)\n"
         "  --special NAME VALUE   a special variable, .STRATA_NAME (str "
         "gives it)\n"
         "  --default-colors       use the colors that the program is "
         "delivered with,\n"
         "                         not those of the settings (also the "
         "variable\n"
         "                         STRATA_DEFAULT_COLORS)\n"
         "  --version              print the version and exit\n"
         "  -h, --usage            this message");
}


int
main(int argc, char **argv)
{
    App app;

    log_open("stred");
    log_note("stred: starts, build type %s, %d arguments",
             strata_build_name, argc - 1);
    memset(&app, 0, sizeof app);
    app.error_log = 1;
    app.banner = 1;
    const char *file = NULL;
    const char *hp = NULL;
    int preview = 0;
    int spell = 0;
    int autocorrect = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--help-file") == 0) {
            if (i + 1 < argc) {
                hp = argv[++i];
            }
        } else if (strcmp(argv[i], "--default-colors") == 0) {
            color_force_defaults();
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("stred (Strata) %s\n", strata_version);
            printf("file format %d\n", STRATA_FILE_FORMAT);

            return 0;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--usage") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--fkey-table") == 0) {
            ui_print_fkeys();
            return 0;
        } else if (strcmp(argv[i], "--dump-menus") == 0) {
            ui_dump_menus(stdout);
            return 0;
        } else if (strcmp(argv[i], "--text") == 0 && i + 2 < argc) {
            char err[128];
            Doc *d = doc_load(argv[i + 1], err, sizeof err);

            if (d == NULL) {
                fprintf(stderr, "%s: %s\n", argv[i + 1], err);
                return 1;
            }

            int rc = export_text(d, argv[i + 2], 78, err, sizeof err);

            if (rc != 0) {
                fprintf(stderr, "%s\n", err);
            }

            doc_free(d);
            return rc != 0 ? 1 : 0;
        } else if (strcmp(argv[i], "-u") == 0 ||
                   strcmp(argv[i], "--unstructured") == 0) {
            app.new_flat = 1;
        } else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
            str_copy(app.template_path, sizeof app.template_path, argv[++i]);
        } else if (strcmp(argv[i], "--description") == 0 && i + 1 < argc) {
            str_copy(app.description, sizeof app.description, argv[++i]);
        } else if (strcmp(argv[i], "--author") == 0 && i + 1 < argc) {
            str_copy(app.author, sizeof app.author, argv[++i]);
        } else if (strcmp(argv[i], "--autosave") == 0 && i + 1 < argc) {
            app.autosave_off = strcmp(argv[++i], "no") == 0;
        } else if (strcmp(argv[i], "--autosave-seconds") == 0 &&
                   i + 1 < argc) {
            app.autosave_seconds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--print") == 0) {
            return print_cli(argc, argv);
        } else if (strcmp(argv[i], "--preview") == 0) {
            preview = 1;
        } else if (strcmp(argv[i], "--no-error-log") == 0) {
            app.error_log = 0;
        } else if (strcmp(argv[i], "--no-banner") == 0) {
            app.banner = 0;
        } else if (strcmp(argv[i], "--spell") == 0) {
            spell = 1;
        } else if (strcmp(argv[i], "--auto") == 0 && i + 1 < argc) {
            autocorrect = strcmp(argv[++i], "no") != 0;
        } else if (strcmp(argv[i], "--var") == 0 ||
                   strcmp(argv[i], "--special") == 0) {
            char verr[240];

            if (vars_option(argc, argv, &i, verr, sizeof verr) != 1) {
                log_error("stred: the option %s was refused and the program "
                          "ends: %s", argv[i], verr);
                fprintf(stderr, "stred: %s\n", verr);

                return 2;
            }
        } else if (argv[i][0] != '-') {
            file = argv[i];
        }
    }

    help_find_file(hp, app.helppath, sizeof app.helppath);

    if (preview != 0) {
        return preview_file(&app, file != NULL ? file : "");
    }

    if (spell != 0) {
        if (file == NULL) {
            fprintf(stderr, "stred --spell needs a document\n");

            return 2;
        }

        return spellui_file(file, autocorrect);
    }

    return app_run(&app, file);
}
