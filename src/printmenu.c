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

/* printmenu.c - the Print menu; see printmenu.h. */
#include "util.h"
#include "printmenu.h"
#include "marker.h"
#include "fileops.h"
#include "filemenu.h"
#include "widgets.h"
#include "ui.h"
#include "layout.h"
#include "preview.h"
#include "printjob.h"
#include "printcli.h"
#include "latex.h"
#include "perr.h"
#include "config.h"
#include "term.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const yes_no[] = {"No", "Yes", NULL};
/* The choices of the sides and of the order of the pages. */
static const char *const dflt_yes_no[] = {"Printer default", "Yes", "No",
                                          NULL};
static Doc *panel_doc; /* the document the panel in use is for */
/* The queues of the Printer field live as long as the panel does. */
static PjQueues queues;
/* The entries of the Printer field: the list of the queues and, when the
   panel starts with a value that is not in it, that value. */
static const char *printer_list[PJ_MAXQ + 6];
static char printer_origin[512];
/* 1 when lp was found for the panel in use: a queue then gets no file. */
static int panel_lp;

/*
 * The choices of the panel about the printer, kept for the life of the
 * Editor (and, in the settings, across runs): see remember_choices.  valid
 * is 0 until a print was made or the settings gave them.
 */
static struct {
    int valid;
    char printer[128];
    int sides; /* index of the choice: Printer default, Yes, No */
    int sort;
    int bw;
} last;


static void
set_field(WField *f, const char *label, int kind)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = kind;

    if (kind == WF_CHOICE) {
        f->choices = yes_no;
    }
}


/* The text of a field: the entry that is chosen of a choice field. */
static const char *
field_text(const WField *f)
{
    return f->kind == WF_CHOICE ? f->choices[f->sel] : f->val;
}


/* The index of an entry of the list of the Printer field, or 0. */
static int
printer_index(const char *text)
{
    for (int i = 0; printer_list[i] != NULL; i++) {
        if (strcmp(printer_list[i], text) == 0) {
            return i;
        }
    }

    return 0;
}


/*
 * Make the entries of the Printer field from the queues: File only, PDF
 * viewer, each queue, the system default when there are queues, a value that
 * the settings gave and the list does not have, and last the search.  cur
 * and prev are the entries that are chosen and the one to come back to.
 */
static void
printer_build(WField *f, const char *cur, const char *prev)
{
    int n = 0;
    int known = printer_origin[0] == '\0';

    for (int i = 0; i < queues.nlist; i++) {
        known = known || strcmp(queues.list[i], printer_origin) == 0;
    }

    for (int i = 0; i < queues.nlist; i++) {
        if (strcmp(queues.list[i], PJ_SEARCH) == 0 && known == 0) {
            printer_list[n++] = printer_origin;
            known = 1;
        }

        printer_list[n++] = queues.list[i];
    }

    printer_list[n] = NULL;
    f->choices = printer_list;
    f->sel = printer_index(cur);
    f->prev_sel = printer_index(prev);
}


/*
 * Before the field is drawn and after each key: look at the answer of lpstat
 * again, without waiting, so that the queues that come later are entries as
 * soon as they are known.  The entry that is chosen stays chosen.
 */
static void
printer_refresh(WField *f)
{
    char cur[512];
    char prev[512];

    str_copy(cur, sizeof cur, f->choices[f->sel]);
    str_copy(prev, sizeof prev, f->choices[f->prev_sel]);
    pj_queues_wait(&queues, 0);
    printer_build(f, cur, prev);
}


static const WField *
find_field(const WField *f, int n, const char *label);


/* The settings words of the choices of the sides and of the order. */
static const char *const sides_word[] = {"default", "two", "one"};
static const char *const sort_word[] = {"default", "yes", "no"};


static int
word_index(const char *const *w, const char *s, int dflt)
{
    for (int i = 0; i < 3; i++) {
        if (strcmp(w[i], s) == 0) {
            return i;
        }
    }

    return dflt;
}


/* Takes what the settings kept, when nothing was printed in this run. */
static void
last_from_config(const Config *cfg)
{
    if (last.valid != 0 || cfg->print_last_printer[0] == '\0') {
        return;
    }

    last.valid = 1;
    str_copy(last.printer, sizeof last.printer, cfg->print_last_printer);
    last.sides = word_index(sides_word, cfg->print_last_sides, 0);
    last.sort = word_index(sort_word, cfg->print_last_sort, 0);
    last.bw = strcmp(cfg->print_last_bw, "yes") == 0;
}


/*
 * After a print that was not cancelled: keep the Printer, the sides, the
 * order of the pages and black and white in memory and in the settings (the
 * file is read again first, so that what another Editor or Setup wrote since
 * is not lost).  Not the Output File, the copies, the pages or the parts:
 * they belong to this document and this print.
 */
static void
remember_choices(const WField *f, int n)
{
    const WField *x;
    Config c;
    char err[200];

    x = find_field(f, n, "Printer");

    if (x == NULL) {
        return;
    }

    last.valid = 1;
    str_copy(last.printer, sizeof last.printer, field_text(x));
    x = find_field(f, n, "Print Double Sided?");
    last.sides = x != NULL ? x->sel : 0;
    x = find_field(f, n, "Sort Output Pages?");
    last.sort = x != NULL ? x->sel : 0;
    x = find_field(f, n, "Print in Black and White?");
    last.bw = x != NULL && x->sel != 0;
    config_load(&c);
    str_copy(c.print_last_printer, sizeof c.print_last_printer, last.printer);
    str_copy(c.print_last_sides, sizeof c.print_last_sides,
             sides_word[last.sides]);
    str_copy(c.print_last_sort, sizeof c.print_last_sort,
             sort_word[last.sort]);
    str_copy(c.print_last_bw, sizeof c.print_last_bw,
             last.bw != 0 ? "yes" : "no");
    config_save(&c, err, sizeof err);
}


/* The sections a panel's from and to fields name must exist. */
static int
check_sections(const WField *f, int n, char *err, int errcap)
{
    const char *from = "";
    const char *to = "";

    for (int i = 0; i < n; i++) {
        if (strcmp(f[i].label, "Print from Section") == 0 ||
            strcmp(f[i].label, "Preview from Section") == 0) {
            from = f[i].val;
        } else if (strcmp(f[i].label, "to Section") == 0) {
            to = f[i].val;
        }
    }

    if (from[0] != '\0' || to[0] != '\0') {
        Doc part;
        char msg[128];

        if (panel_doc->structured == 0) {
            snprintf(err, usz(errcap), "Only a structured document has "
                                       "sections");

            return 0;
        }

        if (doc_part_sections(panel_doc, from, to, &part, msg,
                              sizeof msg) != 0) {
            snprintf(err, usz(errcap), "%s", msg);

            return 0;
        }

        doc_part_free(&part);
    }

    return 1;
}


/* The field of a panel with a label, or NULL. */
static const WField *
find_field(const WField *f, int n, const char *label)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(f[i].label, label) == 0) {
            return &f[i];
        }
    }

    return NULL;
}


/* Sides and sort: the index of the choice and the value of the option. */
static int
sides_of(const WField *f)
{
    return f == NULL || f->sel == 0 ? PJ_DEFAULT : f->sel == 1 ? PJ_TWO :
                                                                  PJ_ONE;
}


static int
sort_of(const WField *f)
{
    return f == NULL || f->sel == 0 ? PJ_SORT_DEFAULT :
           f->sel == 1              ? PJ_SORT_YES :
                                      PJ_SORT_NO;
}


/* The levels of the table of contents: a whole number from 1 to 99. */
static int
parse_levels(const char *s, int *out, char *err, size_t cap)
{
    int v = 0;
    int i = 0;

    for (; s[i] == ' '; i++) {
    }

    if (is_digit(s[i]) == 0) {
        snprintf(err, cap, "Contents Levels is a number from 1 to 99");

        return -1;
    }

    v = s[i] - '0';
    i++;

    if (is_digit(s[i]) != 0) {
        v = v * 10 + (s[i] - '0');
        i++;
    }

    for (; s[i] == ' '; i++) {
    }

    if (s[i] != '\0' || v < 1) {
        snprintf(err, cap, "Contents Levels is a number from 1 to 99");

        return -1;
    }

    *out = v;

    return 0;
}


/*
 * The option of a list from its field: PJ_AUTO when the field says what the
 * document says (the option is then taken from the document), else 1 or 0.
 */
static int
part_option(const Doc *d, const WField *f, int n, const char *label, int kind)
{
    const WField *x = find_field(f, n, label);
    int want = x != NULL && x->sel != 0;
    int have = d != NULL && doc_list_on(d, kind) != 0;

    return want == have ? PJ_AUTO : want;
}


/*
 * The entry "Search for printers..." of the Printer field: run the search of
 * the devices that CUPS can reach and show what it found and the command
 * that adds each.  The field keeps its value; no queue is added.  Esc stops
 * the search.
 */
static void
search_printers(char *val UNUSED, size_t cap UNUSED, int arg UNUSED)
{
    PjSearch *s = NULL;
    char err[256];
    char text[8192];
    int stopped = 0;

    if (pj_search_start(&s, err, sizeof err) != 0) {
        popup_msg("Search for printers", err);

        return;
    }

    while (pj_search_poll(s) == 0) {
        popup_notice("Search for printers", "Searching for printers on the "
                     "network, for up to 10 seconds...", "Esc stops");

        if (term_getkey(50) == K_ESC) {
            stopped = 1;
            break;
        }
    }

    if (stopped == 0) {
        pj_search_report(s, text, sizeof text);
        popup_long("Printers found", text);
    }

    pj_search_end(s);
}


/* Read the options from the fields of a print panel.  Returns 0 or -1. */
static int
read_opts(const WField *f, int n, PjOpts *o, char *err, size_t cap)
{
    const WField *x;

    pj_opts_init(o);
    x = find_field(f, n, "Output File");
    str_copy(o->out, sizeof o->out, x != NULL ? x->val : "");
    x = find_field(f, n, "Printer");
    str_copy(o->printer, sizeof o->printer, x != NULL ? field_text(x) : "");
    x = find_field(f, n, "Copies");

    if (x != NULL && pj_parse_copies(x->val, &o->copies, err, cap) != 0) {
        return -1;
    }

    o->sides = sides_of(find_field(f, n, "Print Double Sided?"));
    o->sort = sort_of(find_field(f, n, "Sort Output Pages?"));
    x = find_field(f, n, "Print from Page");

    if (x != NULL && pj_parse_page(x->val, &o->page_from, err, cap) != 0) {
        return -1;
    }

    x = find_field(f, n, "to Page");

    if (x != NULL && pj_parse_page(x->val, &o->page_to, err, cap) != 0) {
        return -1;
    }

    x = find_field(f, n, "Print Double Spaced?");
    o->double_spaced = x != NULL && x->sel != 0;
    x = find_field(f, n, "Error Log?");
    o->error_log = x == NULL || x->sel != 0;
    /* The fields start with what the document says; a part is given to the
       Printer only when the field was changed (the options win). */
    o->index = part_option(panel_doc, f, n, "Index?", LK_INDEX);
    o->list_figures = part_option(panel_doc, f, n, "List of Figures?",
                                  LK_FIGURE);
    o->list_pictures = part_option(panel_doc, f, n, "List of Pictures?",
                                   LK_PICTURE);
    o->list_tables = part_option(panel_doc, f, n, "List of Tables?",
                                 LK_TABLE);
    x = find_field(f, n, "Print in Black and White?");
    o->black_white = x != NULL && x->sel != 0;
    x = find_field(f, n, "Table of Contents?");

    {
        int want = 0;
        int have = panel_doc != NULL && panel_doc->structured != 0 ?
                       doc_contents_level(panel_doc) : 0;

        if (x != NULL && x->sel != 0) {
            const WField *lv = find_field(f, n, "Contents Levels");

            if (parse_levels(lv != NULL ? lv->val : "3", &want, err, cap) !=
                0) {
                return -1;
            }
        }

        o->contents = want == have ? PJ_AUTO : want;
    }

    return 0;
}


/* 1 when this print goes to a queue with lp and leaves no PDF file. */
static int
queue_only(const PjOpts *o)
{
    return pj_printer_wanted(o) != 0 && panel_lp != 0;
}


static int
check_print(const WField *f, int n, char *err, int errcap)
{
    PjOpts o;

    if (read_opts(f, n, &o, err, usz(errcap)) == 0 &&
        strcmp(o.printer, PJ_SEARCH) == 0) {
        snprintf(err, usz(errcap), "Search for printers only looks for "
                                   "printers: choose File only, PDF viewer "
                                   "or a printer to print");

        return 0;
    }

    if (read_opts(f, n, &o, err, usz(errcap)) != 0 ||
        pj_check(&o, err, usz(errcap)) != 0) {
        return 0;
    }

    /* A print to a queue makes no file: the Output File is not looked at. */
    if (queue_only(&o) != 0) {
        return check_sections(f, n, err, errcap);
    }

    if (pj_check_out(o.out, err, usz(errcap)) != 0) {
        return 0;
    }

    if (panel_doc->path != NULL && strcmp(o.out, panel_doc->path) == 0) {
        snprintf(err, usz(errcap), "The PDF cannot replace the document");

        return 0;
    }

    return check_sections(f, n, err, errcap);
}


/*
 * The tick of the editor: draw the panel and look at the keys.  ctx is the
 * title of the panel.
 */
static int
panel_tick(void *ctx, int phase, PvProg *prog, int changed)
{
    const char *title = ctx;
    const char *hint = phase == PJ_SEND ? "Esc stops lp" : NULL;
    int k;

    if (changed != 0) {
        preview_panel(title, "Sending to the printer", prog, hint);
    }

    k = term_getkey(50);

    if (k == K_ESC) {
        return 1;
    }

    if (k == K_RESIZE) {
        preview_panel(title, "Sending to the printer", prog, hint);
    }

    return 0;
}


/* 1 when the faults of the run include an error: the print failed. */
static int
print_failed(const PjResult *res)
{
    for (int i = 0; i < res->ndiag; i++) {
        if (res->diag[i].kind == DIAG_ERROR) {
            return 1;
        }
    }

    return 0;
}


void
printmenu_default_printer(const Config *cfg, const PjQueues *q, char *out,
                          size_t cap, char *note, size_t ncap)
{
    PjQueues own;
    char queue[PJ_QNAME];

    note[0] = '\0';
    config_print_queue(cfg, queue, sizeof queue);

    switch (config_print_to(cfg)) {
    case CONFIG_PRINT_OPEN:
        str_copy(out, cap, PJ_PDF_VIEWER);
        break;

    case CONFIG_PRINT_PRINTER:
        if (q == NULL) {
            pj_queues(&own);
            q = &own;
        }

        if (q->n == 0) {
            str_copy(out, cap, PJ_FILE_ONLY);
            snprintf(note, ncap, q->pending != 0 ?
                         "The printer list is not available yet, so the PDF "
                         "is only saved (Setup, Print To)" :
                         "No printer is available, so the PDF is "
                         "only saved (Setup, Print To)");
        } else {
            str_copy(out, cap, queue[0] != '\0' ? queue : PJ_SYSTEM_DEFAULT);
        }

        break;

    default:
        str_copy(out, cap, PJ_FILE_ONLY);
        break;
    }
}


void
printmenu_print(Editor *e, int draft)
{
    WField f[24];
    int n = 0;
    char dflt[PATH_MAX];
    char dprinter[PJ_QNAME];
    char note[256];
    char note2[160];
    char lp[PATH_MAX];
    char err[256];
    Config cfg;
    int isec;
    int ititle = -1;
    int ifrom = -1;
    int ito = -1;
    PjOpts o;
    Doc part;
    PjResult res;
    const char *base;

    config_load(&cfg);
    pj_default_out_in(e->doc->path, draft, cfg.pdf_dir, dflt, sizeof dflt,
                      note, sizeof note);

    if (dflt[0] == '\0') {
        str_copy(dflt, sizeof dflt, draft != 0 ? "Untitled-draft.pdf" :
                                                 "Untitled.pdf");
    }

    pj_queues(&queues);
    printmenu_default_printer(&cfg, &queues, dprinter, sizeof dprinter, note2,
                              sizeof note2);

    /* The panel starts with what was chosen at the last print; Setup is
       the default of a first use. */
    last_from_config(&cfg);

    if (last.valid != 0) {
        int known = strcmp(last.printer, PJ_FILE_ONLY) == 0 ||
                    strcmp(last.printer, PJ_PDF_VIEWER) == 0 ||
                    strcmp(last.printer, PJ_PDF_VIEWER_OLD) == 0 ||
                    queues.pending != 0;

        for (int i = 0; i < queues.nlist; i++) {
            known = known || strcmp(queues.list[i], last.printer) == 0;
        }

        if (known != 0) {
            str_copy(dprinter, sizeof dprinter,
                     strcmp(last.printer, PJ_PDF_VIEWER_OLD) == 0 ?
                         PJ_PDF_VIEWER : last.printer);
        } else {
            str_copy(dprinter, sizeof dprinter, PJ_FILE_ONLY);
            snprintf(note2, sizeof note2, "The printer %.60s is not in the "
                     "list of the system any more: File only is shown",
                     last.printer);
        }
    }

    if (note[0] != '\0' || note2[0] != '\0') {
        ed_msg(e, "%s%s%s", note, note[0] != '\0' && note2[0] != '\0' ?
                                  "; " : "", note2);
    }

    set_field(&f[n], "Output File", WF_TEXT);
    f[n].pick = PICK_FILE;
    f[n].suffixes = ".pdf";
    str_copy(f[n].val, sizeof f[n].val, dflt);
    n++;
    set_field(&f[n], "Printer", WF_CHOICE);
    f[n].refresh = printer_refresh;
    f[n].call = search_printers;
    f[n].call_item = PJ_SEARCH;
    printer_origin[0] = '\0';
    printer_build(&f[n], PJ_FILE_ONLY, PJ_FILE_ONLY);

    if (strcmp(printer_list[printer_index(dprinter)], dprinter) != 0) {
        str_copy(printer_origin, sizeof printer_origin, dprinter);
    }

    printer_build(&f[n], dprinter, dprinter);
    n++;
    set_field(&f[n], "Copies", WF_TEXT);
    str_copy(f[n].val, sizeof f[n].val, "1");
    n++;
    set_field(&f[n], "Print Double Sided?", WF_CHOICE);
    f[n].choices = dflt_yes_no;
    f[n++].sel = last.valid != 0 ? last.sides : 0;
    set_field(&f[n], "Sort Output Pages?", WF_CHOICE);
    f[n].choices = dflt_yes_no;
    f[n++].sel = last.valid != 0 ? last.sort : 0;
    isec = n;
    set_field(&f[n++], "Print Current Section Only?", WF_CHOICE);

    if (draft == 0) {
        ititle = n;
        set_field(&f[n], "Print Title Page?", WF_CHOICE);
        f[n].sel = 1;
        n++;
    }

    set_field(&f[n++], "Print Double Spaced?", WF_CHOICE);

    if (draft == 0) {
        ifrom = n;
        set_field(&f[n++], "Print from Section", WF_TEXT);
        ito = n;
        set_field(&f[n++], "to Section", WF_TEXT);
    }

    set_field(&f[n++], "Print from Page", WF_TEXT);
    set_field(&f[n++], "to Page", WF_TEXT);

    /* The last of the fields of the panel, so that they keep
       their places. */
    set_field(&f[n], "Error Log?", WF_CHOICE);
    f[n].sel = 1;
    n++;
    /* The document's own choices are the first values of these fields. */
    {
        int level = e->doc->structured != 0 ? doc_contents_level(e->doc) : 0;
        char lv[16];

        set_field(&f[n], "Table of Contents?", WF_CHOICE);
        f[n++].sel = level > 0;
        set_field(&f[n], "Contents Levels", WF_TEXT);
        snprintf(lv, sizeof lv, "%d", level > 0 ? level : 3);
        str_copy(f[n].val, sizeof f[n].val, lv);
        n++;
        set_field(&f[n], "Index?", WF_CHOICE);
        f[n++].sel = doc_list_on(e->doc, LK_INDEX) != 0;
        set_field(&f[n], "List of Figures?", WF_CHOICE);
        f[n++].sel = doc_list_on(e->doc, LK_FIGURE) != 0;
        set_field(&f[n], "List of Pictures?", WF_CHOICE);
        f[n++].sel = doc_list_on(e->doc, LK_PICTURE) != 0;
        set_field(&f[n], "List of Tables?", WF_CHOICE);
        f[n++].sel = doc_list_on(e->doc, LK_TABLE) != 0;
    }

    set_field(&f[n], "Print in Black and White?", WF_CHOICE);
    f[n++].sel = last.valid != 0 && last.bw != 0;
    panel_lp = pj_find_lp(lp, sizeof lp) == 0;

    /* The Output File is only active when the print leaves a PDF: with a
       queue (the entries after File only and PDF viewer) it stays in its
       place and is inactive, with the text that it had. */
    if (panel_lp != 0) {
        f[0].dep = 2;
        f[0].dep_sel = 1;
        f[0].dep_le = 1;
    }

    if (panel_lp == 0) {
        set_field(&f[n], "Print Status", WF_INFO);
        str_copy(f[n].val, sizeof f[n].val,
                 "lp not found: only the PDF is made");
        n++;
    }

    panel_doc = e->doc;

    for (;;) {
        if (popup_form(draft != 0 ? "Draft Print" : "Print Document", f, n,
                       check_print) == 0) {
            return;
        }

        read_opts(f, n, &o, err, sizeof err);

        /* No file is written for a queue, so nothing is replaced. */
        if (queue_only(&o) != 0 || access(o.out, F_OK) != 0) {
            break;
        }

        base = strrchr(o.out, '/');
        snprintf(err, sizeof err, "Replace %.50s?",
                 base != NULL ? base + 1 : o.out);

        if (popup_yn(err) != 0) {
            break;
        }
    }

    o.draft = draft;
    o.keep_pdf = 0;
    str_copy(o.viewer, sizeof o.viewer, cfg.pdf_viewer);
    base = e->doc->path != NULL ? strrchr(e->doc->path, '/') : NULL;

    if (e->doc->path != NULL) {
        str_copy(o.title, sizeof o.title,
                 base != NULL ? base + 1 : e->doc->path);
        str_copy(o.source_path, sizeof o.source_path, e->doc->path);
    }

    if (print_part(e->doc, e->cur.item, f[isec].sel,
                   ititle < 0 ? 0 : f[ititle].sel,
                   ifrom < 0 ? "" : f[ifrom].val, ito < 0 ? "" : f[ito].val,
                   &part, err, sizeof err) != 0) {
        popup_msg("Print", err);

        return;
    }

    {
        char docdir[PATH_MAX];
        char ctx[] = "Print";

        preview_docdir(e, docdir, sizeof docdir);
        pj_set_workdir(filemenu_workdir(e));
        pj_run(&o, &part, docdir, latex_estimate_pages(&part), panel_tick,
               ctx, &res);
    }

    if (res.status != PJ_CANCELLED) {
        remember_choices(f, n);
    }

    /* The faults of a run that reached the document become markers. */
    if (res.status != PJ_CANCELLED && res.faults_known != 0) {
        perr_apply(e, PERR_RUN_PRINT, res.diag, res.ndiag, &part);
    }

    doc_part_free(&part);

    if (res.status == PJ_DONE && (res.sent == 0 || res.kept == 0)) {
        /* What was made, then what was marked: the message line is short. */
        const char *tail = "";

        if (res.faults_known != 0) {
            tail = res.ndiag > 0 ? "" : "  No print errors.";
        }

        ed_msg(e, "%s%s", res.msg, tail);
    } else if (res.status == PJ_CANCELLED &&
               strcmp(res.msg, PJ_CANCEL_MSG) == 0) {
        ed_msg(e, "Print cancelled");
    } else {
        popup_text(res.title[0] != '\0' ? res.title : "Print", res.msg);

        /* The message line shows how many errors were marked. */
        if (res.faults_known != 0 && res.status != PJ_CANCELLED) {
            if (res.ndiag > 0) {
                ed_msg(e, "Print: %s%d print error%s (Ctrl-F8 finds the "
                       "next)", print_failed(&res) != 0 ? "PRINT FAILED, " :
                                                          "", res.ndiag,
                       res.ndiag == 1 ? "" : "s");
            } else {
                ed_msg(e, "Print: no print errors");
            }
        }
    }
}


void
printmenu_preview(Editor *e, PreviewKind kind)
{
    Doc part;
    char perr[128];
    int r = 0;
    int error_log = 1;

    if (kind == PV_DOCUMENT) {
        WField f[4];

        set_field(&f[0], "Preview Title Page?", WF_CHOICE);
        f[0].sel = 1;
        set_field(&f[1], "Preview from Section", WF_TEXT);
        set_field(&f[2], "to Section", WF_TEXT);
        set_field(&f[3], "Error Log?", WF_CHOICE);
        f[3].sel = 1;
        panel_doc = e->doc;

        if (popup_form("Preview Document", f, 4, check_sections) == 0) {
            return;
        }

        error_log = f[3].sel;

        r = print_part(e->doc, 0, 0, f[0].sel, f[1].val, f[2].val, &part,
                       perr, sizeof perr);
    } else if (kind == PV_BLOCK) {
        r = doc_part_block(e->doc, e->cur.item, &part);
    } else {
        r = doc_part_current(e->doc, e->cur.item, &part);
    }

    if (r != 0) {
        popup_msg("Preview", kind == PV_BLOCK ? "The cursor is not in a "
                                                "block" :
                                                "There is no current "
                                                "section");

        return;
    }

    preview_run(e, &part, error_log, NULL);

    doc_part_free(&part);
}
