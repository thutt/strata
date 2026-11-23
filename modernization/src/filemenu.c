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

/* filemenu.c - the panels of the File menu. */
#include "util.h"
#include "config.h"
#include "diag.h"
#include "filemenu.h"
#include "globalmenu.h"
#include "template.h"
#include "fileops.h"
#include "marker.h"
#include "preview.h"
#include "printcli.h"
#include "printjob.h"
#include "readfile.h"
#include "recoverui.h"
#include "term.h"
#include "widgets.h"
#include "log.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* The working directory of the panels, which they share. */
static char workdir[PATH_MAX];


static void
init_workdir(const Editor *e)
{
    if (workdir[0] != '\0') {
        return;
    }

    if (e->doc->path != NULL && strchr(e->doc->path, '/') != NULL) {
        str_copy(workdir, sizeof workdir, e->doc->path);
        *strrchr(workdir, '/') = '\0';
    }

    if (workdir[0] == '\0' && getcwd(workdir, sizeof workdir) == NULL) {
        str_copy(workdir, sizeof workdir, ".");
    }
}


const char *
filemenu_workdir(const Editor *e)
{
    init_workdir(e);

    return workdir;
}


void
filemenu_set_workdir(const char *dir)
{
    str_copy(workdir, sizeof workdir, dir);
}


static const char *
base_of(const char *path)
{
    const char *sl = strrchr(path, '/');

    return sl != NULL ? sl + 1 : path;
}


/* The path of a name typed on a panel, in the working directory. */
static void
resolve(const char *name, char *out, size_t cap)
{
    if (name[0] == '/') {
        str_copy(out, cap, name);
    } else {
        path_join(out, cap, workdir, name);
    }
}


/*
 * Add an extension to a name that has none, as the format does: the last
 * dot of the name starts an extension unless it is the first character
 * (name_has_extension).  A name that has no room for the extension is left
 * as it is, so that it is refused as it was typed.
 */
static void
add_extension(char *name, size_t cap, const char *ext)
{
    size_t n = strlen(name);

    if (name_has_extension(name) == false && n + strlen(ext) < cap) {
        str_copy(name + n, cap - n, ext);
    }
}


static bool
file_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}


/* A text field of a panel. */
static void
text_field(WField *f, const char *label, const char *val, int pick,
           const char *suffixes)
{
    f->label = label;
    f->kind = WF_TEXT;
    str_copy(f->val, sizeof f->val, val);
    f->pick = pick;
    f->suffixes = suffixes;
}


static void
choice_field(WField *f, const char *label, const char *const *choices)
{
    f->label = label;
    f->kind = WF_CHOICE;
    f->choices = choices;
    f->sel = 0;
}


static void
revision_str(int rev, char *buf, size_t cap)
{
    marker_fmt_num(rev, 3, 0, buf, cap);
}


/* The character sets of the Import and Export panels. */
static const char *const charset_choices[] = {
    "IBM Extended ASCII (CP 437)", "96 Character ASCII (7 Bit)",
    "IBM Multilingual (CP 850)",   "Portuguese (CP 860)",
    "Norwegian/Danish (CP 865)",   "UTF-8",
    "Latin-1 (ISO 8859-1)",        NULL};

static const Charset charset_of[] = {CS_437, CS_ASCII7, CS_850,  CS_860,
                                     CS_865, CS_UTF8,   CS_LATIN1};


int
filemenu_ignore_changes(Editor *e)
{
    if (e->doc->modified == 0) {
        return 1;
    }

    return popup_yn("The file has changes that were not saved.  Ignore them?");
}


/* ----------------------------------------------------------------- save */
static const char *const save_choices[] = {"Save Entire Document",
                                           "Save Global Settings Only",
                                           "Extract Current Section", NULL};

static const char *const save_choices_flat[] = {"Save Entire Document",
                                                "Save Global Settings Only",
                                                "Extract Current Block", NULL};

static const char *const save_name_suffixes = ".doc .set .bak";


static int
check_save(const WField *f, int n UNUSED, char *err, int errcap)
{
    int v;

    if (f[1].val[0] == '\0') {
        snprintf(err, usz(errcap), "No file name specified");

        return 0;
    }

    if (doc_desc_valid(f[3].val) == 0) {
        snprintf(err, usz(errcap),
                 "The description has at most %d characters, with no control "
                 "character", DOC_MAX_DESC);

        return 0;
    }

    if (marker_parse_num(f[5].val, 3, &v) != 0) {
        snprintf(err, usz(errcap), "Not a valid revision number");

        return 0;
    }

    return 1;
}


/*
 * The revision a file gets when it is saved: the number typed on the panel
 * if it was changed, otherwise one more than now when the document has been
 * modified, and the same when it has not.
 */
static int
revision_to_save(const Doc *d, const char *typed, int modified)
{
    int cur = doc_revision(d);
    int v;

    if (marker_parse_num(typed, 3, &v) != 0 || v == cur) {
        return modified != 0 ? cur + 1 : cur;
    }

    return v;
}


void
filemenu_save(Editor *e)
{
    Doc *d = e->doc;
    WField f[6];
    char desc[DOC_MAX_DESC + 1];
    char auth[DOC_MAX_DESC + 1];
    char rev[32];
    const char *v;

    init_workdir(e);
    memset(f, 0, sizeof f);
    v = doc_header_get(d, "MD");
    str_copy(desc, sizeof desc, v != NULL ? v : "");
    v = doc_header_get(d, "MA");
    str_copy(auth, sizeof auth, v != NULL ? v : "");
    revision_str(doc_revision(d), rev, sizeof rev);

    text_field(&f[0], "Working Directory", workdir, PICK_DIR, NULL);
    text_field(&f[1], "Document Name", d->path != NULL ? base_of(d->path) : "",
               PICK_FILE, save_name_suffixes);
    choice_field(&f[2], "Options",
                 d->structured != 0 ? save_choices : save_choices_flat);
    text_field(&f[3], "Description", desc, PICK_NONE, NULL);
    text_field(&f[4], "Author", auth, PICK_NONE, NULL);
    text_field(&f[5], "Revision", rev, PICK_NONE, NULL);

    if (popup_form("Save File", f, 6, check_save) == 0) {
        return;
    }

    SaveMode mode = f[2].sel == 1 ? SAVE_GLOBAL :
                    f[2].sel == 2 ? SAVE_EXTRACT :
                                    SAVE_ENTIRE;
    char name[PATH_MAX];
    char path[PATH_MAX];

    str_copy(workdir, sizeof workdir, f[0].val);
    str_copy(name, sizeof name, f[1].val);
    add_extension(name, sizeof name, mode == SAVE_GLOBAL ? ".set" : ".doc");
    resolve(name, path, sizeof path);

    if (file_exists(path) != false &&
        popup_yn("Overwrite old version on disk?") == 0) {
        return;
    }

    bool changed = strcmp(desc, f[3].val) != 0 || strcmp(auth, f[4].val) != 0;
    int modified = d->modified != 0 || changed == true;
    int old_rev = doc_revision(d);
    int saved_flag = d->modified;
    char err[128];

    if (strcmp(desc, f[3].val) != 0) {
        doc_header_set(d, "MD", f[3].val);
    }

    if (strcmp(auth, f[4].val) != 0) {
        doc_header_set(d, "MA", f[4].val);
    }

    doc_set_revision(d, revision_to_save(d, f[5].val, modified));

    if (mode == SAVE_ENTIRE &&
        (modified != 0 || d->path == NULL || strcmp(d->path, path) != 0)) {
        /* A saved change, or a new file, keeps the place of the cursor. */
        ed_remember_position(e);
    }

    int unchanged = 0;
    /* A file of an older version of the file format that the save replaces
       is kept as it was first (nothing is done if the load made it). */
    int rc = docver_special_backup(path, NULL, err, sizeof err) != 0 ? -1 :
             mode == SAVE_ENTIRE ?
                 doc_save_depth(d, path, e->opt_backups, &unchanged, err,
                                sizeof err) :
                 doc_save_mode(d, path, mode, e->cur.item, err, sizeof err);

    if (rc != 0) {
        doc_set_revision(d, old_rev);
        d->modified = saved_flag;
        popup_msg("Save failed", err);

        return;
    }

    if (mode != SAVE_ENTIRE) {
        /* The document itself was not written. */
        d->modified = modified;
    }

    if (unchanged == 2) {
        ed_msg(e, "Saved %s (no backup made: %s)", path, err);
    } else {
        ed_msg(e, unchanged != 0 ? "Saved %s (no change, no backup made)" :
                                   "Saved %s",
               path);
    }
}


/*
 * Save the document under its name with depth numbered backups.  An
 * Autosave makes none (depth 0): it would push the backups of the versions
 * that the user saved out of the series.
 */
static int
save_under_name(Editor *e, int depth)
{
    Doc *d = e->doc;

    if (d->path == NULL) {
        filemenu_save(e);

        return d->path != NULL ? 0 : -1;
    }

    char err[128];
    int old_rev = doc_revision(d);
    int saved_flag = d->modified;

    if (d->modified != 0) {
        doc_set_revision(d, old_rev + 1);
    }

    int unchanged = 0;

    if (d->modified != 0) {
        /* The place of the cursor is kept when a change is saved. */
        ed_remember_position(e);
    }

    if (docver_special_backup(d->path, NULL, err, sizeof err) != 0 ||
        doc_save_depth(d, d->path, depth, &unchanged, err, sizeof err) != 0) {
        doc_set_revision(d, old_rev);
        d->modified = saved_flag;
        popup_msg("Save failed", err);

        return -1;
    }

    if (unchanged == 2) {
        ed_msg(e, "Saved %s (no backup made: %s)", d->path, err);
    } else {
        ed_msg(e, unchanged != 0 ? "Saved %s (no change, no backup made)" :
                                   "Saved %s",
               d->path);
    }

    return 0;
}


int
filemenu_quick_save(Editor *e)
{
    return save_under_name(e, e->opt_backups);
}


int
filemenu_autosave(Editor *e)
{
    return save_under_name(e, 0);
}


/* ------------------------------------------------------------- retrieve */
static const char *const retrieve_choices[] = {
    "Retrieve Entire Document",
    "Retrieve Body Only",
    "Retrieve Global Settings",
    "Retrieve Typeface Settings Only",
    "Retrieve Named Block Settings Only",
    "Retrieve & Insert after Current Section",
    NULL};

static const char *const retrieve_choices_flat[] = {
    "Retrieve Entire Document",
    "Retrieve Body Only",
    "Retrieve Global Settings",
    "Retrieve Typeface Settings Only",
    "Retrieve Named Block Settings Only",
    "Retrieve & Insert after Current Block",
    NULL};

static const RetrieveMode retrieve_modes[] = {
    RET_ENTIRE, RET_BODY, RET_GLOBAL, RET_TYPEFACE, RET_NAMED, RET_INSERT};


static int
check_retrieve(const WField *f, int n UNUSED, char *err, int errcap)
{
    if (f[1].val[0] == '\0') {
        snprintf(err, usz(errcap), "No file name specified");

        return 0;
    }

    return 1;
}


void
filemenu_retrieve(Editor *e)
{
    Doc *d = e->doc;
    WField f[3];

    init_workdir(e);
    memset(f, 0, sizeof f);
    text_field(&f[0], "Working Directory", workdir, PICK_DIR, NULL);
    text_field(&f[1], "Document Name", d->path != NULL ? base_of(d->path) : "",
               PICK_FILE, ".doc .set .bak");
    choice_field(&f[2], "Options",
                 d->structured != 0 ? retrieve_choices : retrieve_choices_flat);

    if (popup_form("Retrieve File", f, 3, check_retrieve) == 0) {
        return;
    }

    RetrieveMode mode = retrieve_modes[f[2].sel];
    char name[PATH_MAX];
    char path[PATH_MAX];
    char err[160];

    str_copy(workdir, sizeof workdir, f[0].val);
    str_copy(name, sizeof name, f[1].val);
    resolve(name, path, sizeof path);

    /* A name that is not there is tried with .doc, whatever dots it has: a
       name such as "notes v1.2" is a name without an extension. */
    if (file_exists(path) == false && strlen(name) + 5 <= sizeof name) {
        char with[PATH_MAX];

        str_copy(with, sizeof with, name);
        str_copy(with + strlen(with), sizeof with - strlen(with), ".doc");
        resolve(with, path, sizeof path);

        if (file_exists(path) == false) {
            resolve(name, path, sizeof path);
            add_extension(name, sizeof name, ".doc");
            resolve(name, path, sizeof path);
        }
    }

    int rec = -1;
    Doc *nd = recoverui_load(path, err, sizeof err, "Retrieve failed", &rec);

    if (nd == NULL) {
        popup_msg("Retrieve failed", err);

        return;
    }

    if (mode != RET_INSERT && filemenu_ignore_changes(e) == 0) {
        doc_free(nd);

        return;
    }

    if (mode == RET_ENTIRE) {
        Doc *old = e->doc;

        ed_set_doc(e, nd);
        ed_restore_position(e);
        doc_free(old);

        if (rec >= 0) {
            ed_msg(e, "Recovered %d blocks of %s; the original is not "
                      "changed", rec, path);
        } else {
            ed_msg(e, "Retrieved %s", path);
        }

        return;
    }

    if (doc_retrieve_into(d, nd, mode, e->cur.item, err, sizeof err) != 0) {
        popup_msg("Retrieve failed", err);
    } else {
        ed_invalidate(e);

        if (mode == RET_BODY) {
            e->cur.item = 0;
            e->cur.col = 0;
            e->cur.tok = 0;
            ed_move(e, MV_DOC_START, 24);
        }

        if (rec >= 0) {
            ed_msg(e, "Recovered %d blocks of %s; the original is not "
                      "changed", rec, path);
        } else {
            ed_msg(e, "Retrieved %s", path);
        }
    }

    doc_free(nd);
}


/* ------------------------------------------------------------------ new */
static int
check_new(const WField *f, int n UNUSED, char *err, int errcap)
{
    int v;

    if (f[1].val[0] == '\0') {
        snprintf(err, usz(errcap), "No file name specified");

        return 0;
    }

    if (doc_desc_valid(f[3].val) == 0) {
        snprintf(err, usz(errcap),
                 "The description has at most %d characters, with no control "
                 "character", DOC_MAX_DESC);

        return 0;
    }

    if (marker_parse_num(f[5].val, 3, &v) != 0) {
        snprintf(err, usz(errcap), "Not a valid revision number");

        return 0;
    }

    return 1;
}


void
filemenu_new(Editor *e)
{
    WField f[6];

    Template *tl = calloc(TEMPLATE_MAX, sizeof *tl);
    int ntl = tl != NULL ? template_list(tl, TEMPLATE_MAX) : 0;
    char tname[PATH_MAX] = "DEFAULT.SET";

    /* With templates the first question is which one the document copies;
       Esc takes the built-in settings, as the field Template does when it
       is left alone. */
    if (ntl > 0) {
        int c = template_ask(tl, ntl, 1);

        if (c >= 2) {
            str_copy(tname, sizeof tname, tl[c - 2].path);
        } else if (c == 1) {
            str_copy(tname, sizeof tname, "Unstructured");
        } else if (c == 0) {
            str_copy(tname, sizeof tname, "Structured");
        }
    }

    free(tl);
    init_workdir(e);
    memset(f, 0, sizeof f);
    text_field(&f[0], "Working Directory", workdir, PICK_DIR, NULL);
    text_field(&f[1], "Document Name", "", PICK_FILE, ".doc");
    text_field(&f[2], "Template", tname, PICK_CALL, NULL);
    f[2].call = template_pick;
    f[2].finish_on_ins = 1;
    text_field(&f[3], "Description", "", PICK_NONE, NULL);
    text_field(&f[4], "Author", "", PICK_NONE, NULL);
    text_field(&f[5], "Revision", "1.000", PICK_NONE, NULL);

    if (popup_form("New File", f, 6, check_new) == 0) {
        return;
    }

    str_copy(workdir, sizeof workdir, f[0].val);

    char name[PATH_MAX];
    char path[PATH_MAX];
    char tpath[PATH_MAX];
    char err[160];

    str_copy(name, sizeof name, f[1].val);
    add_extension(name, sizeof name, ".doc");
    resolve(name, path, sizeof path);

    if (file_exists(path) != false &&
        popup_yn("The file exists.  Replace it when it is saved?") == 0) {
        return;
    }

    if (filemenu_ignore_changes(e) == 0) {
        return;
    }

    Doc *nd = NULL;

    if (strcasecmp(f[2].val, "Structured") == 0) {
        nd = doc_new(1);
        doc_default_paper(nd);
    } else if (strcasecmp(f[2].val, "Unstructured") == 0 ||
               strcasecmp(f[2].val, "UNSTRUCTURED.BUILTIN") == 0) {
        nd = doc_new(0);
        doc_default_paper(nd);
    } else if (template_find(f[2].val, workdir, tpath, sizeof tpath) != 0) {
        nd = doc_load(tpath, err, sizeof err);

        if (nd == NULL) {
            popup_msg("Template", err);

            return;
        }

        /* A document made from a template starts at the top. */
        doc_clear_view_pos(nd);
    } else if (strcasecmp(f[2].val, "DEFAULT.SET") == 0) {
        /* There is no default template file: use the built-in one. */
        const char *it[] = {"Structured", "Unstructured"};
        int c = popup_menu("New document type", it, 2, 0);

        if (c < 0) {
            return;
        }

        nd = doc_new(c == 0);
        doc_default_paper(nd);
    } else {
        popup_msg("Template", "Cannot find the template file");

        return;
    }

    free(nd->path);
    nd->path = strdup(path);

    /* A blank description or author keeps the template's. */
    if (f[3].val[0] != '\0') {
        doc_header_set(nd, "MD", f[3].val);
    }

    if (f[4].val[0] != '\0') {
        doc_header_set(nd, "MA", f[4].val);
    }

    int rev;

    if (marker_parse_num(f[5].val, 3, &rev) == 0) {
        doc_set_revision(nd, rev);
    }

    Doc *old = e->doc;

    ed_set_doc(e, nd);
    doc_free(old);
    ed_msg(e, "New document");
}


/* --------------------------------------------------------------- delete */
static int
check_name_only(const WField *f, int n UNUSED, char *err, int errcap)
{
    if (f[1].val[0] == '\0') {
        snprintf(err, usz(errcap), "No file name specified");

        return 0;
    }

    return 1;
}


void
filemenu_delete(Editor *e)
{
    WField f[2];

    init_workdir(e);
    memset(f, 0, sizeof f);
    text_field(&f[0], "Working Directory", workdir, PICK_DIR, NULL);
    text_field(&f[1], "File Name", "", PICK_FILE, "");

    if (popup_form("Delete File", f, 2, check_name_only) == 0) {
        return;
    }

    char path[PATH_MAX];
    char msg[PATH_MAX + 32];

    str_copy(workdir, sizeof workdir, f[0].val);
    resolve(f[1].val, path, sizeof path);

    if (file_exists(path) == false) {
        popup_msg("Delete", "Cannot find the file");

        return;
    }

    snprintf(msg, sizeof msg, "Delete %s?", base_of(path));

    if (popup_yn(msg) == 0) {
        return;
    }

    if (unlink(path) != 0) {
        popup_msg("Delete", "Cannot delete the file");

        return;
    }

    ed_msg(e, "Deleted %s", path);
}


/* --------------------------------------------------------------- import */
static const char *const import_choices[] = {"Ignore Line Endings",
                                             "Preserve Line Endings", NULL};


void
filemenu_import(Editor *e)
{
    WField f[4];

    init_workdir(e);
    memset(f, 0, sizeof f);
    text_field(&f[0], "Working Directory", workdir, PICK_DIR, NULL);
    text_field(&f[1], "ASCII File Name", "", PICK_FILE, "");
    choice_field(&f[2], "Options", import_choices);
    choice_field(&f[3], "Character Set", charset_choices);

    if (popup_form("Import ASCII Text File", f, 4, check_name_only) == 0) {
        return;
    }

    char path[PATH_MAX];

    str_copy(workdir, sizeof workdir, f[0].val);
    resolve(f[1].val, path, sizeof path);

    char reason[96];
    size_t n = 0;
    uint8_t *data = readfile_all(path, DOC_MAX_BYTES, &n, reason,
                                 sizeof reason);

    if (data == NULL) {
        popup_msg("Import failed", reason);

        return;
    }

    int lost = ed_import_ascii(e, data, n, f[2].sel == 0, charset_of[f[3].sel]);

    free(data);

    if (lost < 0) {
        ed_msg(e, "Imported only the first part: a document has at most "
               "500000 blocks");
    } else if (lost > 0) {
        ed_msg(e, "Imported; %d character%s not kept", lost,
               lost == 1 ? "" : "s");
    } else {
        ed_msg(e, "Imported %s", path);
    }
}


/* --------------------------------------------------------------- export */
static const char *const export_choices[] = {"Export a Screen Image",
                                             "Export Data Only",
                                             "Export a LaTeX File", NULL};

/* The number of the choice that writes the LaTeX file of Print. */
#define EXPORT_LATEX 2

/* The most diagnostics that a popup lists after an export. */
#define EXPORT_LISTED 6

static const char *const export_scope[] = {"Export Entire Document",
                                           "Export Current Section", NULL};

static const char *const export_check[] = {"No", "Yes", NULL};


/* The waiting box of the check of a LaTeX export. */
static void
export_check_box(const char *title)
{
    int C = term_cols();
    int R = term_rows();
    int w = C - 6 < 52 ? C - 6 : 52;
    int x = (C - w) / 2;
    int y = R / 2 - 3;

    w_base();
    box(x, y, w, 6, title);
    term_puts(x + 3, y + 2, "Checking the file with LaTeX", TA_BOLD);
    term_puts(x + 3, y + 4, "Esc stops the check", TA_DIM);
    term_cursor(0, 0, 0);
    term_present();
}


/*
 * The tick of the check of a LaTeX export: draw the waiting box and look at
 * the keys.  ctx is the title of the box.  Esc stops the check.  Before the
 * check starts the file is being written, which is quick.
 */
static int
export_tick(void *ctx, int phase, PvProg *prog, int changed)
{
    int k;

    if (prog != NULL && phase == PJ_TYPESET && changed != 0) {
        export_check_box(ctx);
    }

    k = term_getkey(50);

    if (k == K_ESC) {
        return 1;
    }

    if (k == K_RESIZE) {
        export_check_box(ctx);
    }

    return 0;
}


/*
 * Export the document, or its current section, as the LaTeX file that Print
 * and Preview typeset.  The Printer writes it (see pj_export_latex): the
 * pictures are copied next to it, and a document with errors is exported all
 * the same, with the errors told here.  Print Error markers are not made.
 * With check the engine of a print is run on the file as well, with a
 * waiting box that Esc stops, and the errors of TeX are told like the others.
 */
static void
export_latex_file(Editor *e, const char *path, int current_only, int check)
{
    char title[] = "Export";
    PjExport x;
    PjResult res;
    Config cfg;
    Doc part;
    char err[256];
    char docdir[PATH_MAX];
    const char *base;

    if (print_part(e->doc, e->cur.item, current_only, 1, "", "", &part, err,
                   sizeof err) != 0) {
        log_error("stred: cannot choose the part of the document to export "
                  "as LaTeX to %s: %s", path, err);
        popup_msg("Export failed", err);

        return;
    }

    memset(&x, 0, sizeof x);
    str_copy(x.out, sizeof x.out, path);
    base = e->doc->path != NULL ? strrchr(e->doc->path, '/') : NULL;

    if (e->doc->path != NULL) {
        str_copy(x.source, sizeof x.source,
                 base != NULL ? base + 1 : e->doc->path);
        str_copy(x.source_path, sizeof x.source_path, e->doc->path);
    }

    config_load(&cfg);
    x.keep_pictures = cfg.latex_keep_pictures;
    x.positions = cfg.latex_positions;
    x.check = check;
    preview_docdir(e, docdir, sizeof docdir);
    pj_set_workdir(filemenu_workdir(e));
    pj_export_latex(&x, &part, docdir, check != 0 ? export_tick : NULL, title,
                    &res);
    doc_part_free(&part);

    if (res.status == PJ_CANCELLED) {
        ed_msg(e, "%s", res.msg);

        return;
    }

    if (res.status != PJ_DONE) {
        log_error("stred: the export as LaTeX to %s failed: %s", path,
                  res.msg);
        popup_text("Export failed", res.msg);

        return;
    }

    if (res.ndiag > 0) {
        char text[2048];
        char more[32];

        snprintf(text, sizeof text, "%.600s.", res.msg);

        for (int i = 0; i < res.ndiag && i < EXPORT_LISTED; i++) {
            char line[2 * DIAG_MSG + 400];
            size_t at = strlen(text);

            diag_describe(&res.diag[i], line, sizeof line);
            str_copy(text + at, sizeof text - at, "  ");
            at = strlen(text);
            str_copy(text + at, sizeof text - at, line);
        }

        if (res.ndiag > EXPORT_LISTED) {
            size_t at = strlen(text);

            snprintf(more, sizeof more, "  (%d more)",
                     res.ndiag - EXPORT_LISTED);
            str_copy(text + at, sizeof text - at, more);
        }

        popup_text("Export", text);
    } else if (strstr(res.msg, "not checked") != NULL) {
        /* The file is made but the check was not: say why, in full. */
        popup_text("Export", res.msg);
    }

    ed_msg(e, "%s", res.msg);
}


void
filemenu_export(Editor *e)
{
    WField f[6];
    int n = 4;
    int iscope = -1;
    int icheck;

    init_workdir(e);
    memset(f, 0, sizeof f);
    text_field(&f[0], "Working Directory", workdir, PICK_DIR, NULL);
    text_field(&f[1], "ASCII File Name", "", PICK_FILE, "");
    choice_field(&f[2], "Options", export_choices);
    choice_field(&f[3], "Character Set", charset_choices);

    if (e->doc->structured != 0) {
        iscope = n;
        choice_field(&f[n++], "Scope", export_scope);
    }

    /* The check is a choice of the LaTeX file only: it is shown then. */
    icheck = n;
    choice_field(&f[n], "Check Validity", export_check);
    f[n].dep = 3;
    f[n].dep_sel = EXPORT_LATEX;
    n++;

    if (popup_form("Export ASCII Text File", f, n, check_name_only) == 0) {
        return;
    }

    char path[PATH_MAX];
    char err[160];

    str_copy(workdir, sizeof workdir, f[0].val);
    resolve(f[1].val, path, sizeof path);

    /* A LaTeX file has the extension .tex when the name has none. */
    if (f[2].sel == EXPORT_LATEX && strchr(base_of(path), '.') == NULL &&
        strlen(path) + 5 < sizeof path) {
        str_copy(path + strlen(path), sizeof path - strlen(path), ".tex");
    }

    if (file_exists(path) != false &&
        popup_yn("Overwrite old version on disk?") == 0) {
        return;
    }

    if (f[2].sel == EXPORT_LATEX) {
        export_latex_file(e, path, iscope >= 0 && f[iscope].sel == 1,
                          f[icheck].sel == 1);

        return;
    }

    int item = iscope >= 0 && f[iscope].sel == 1 ? e->cur.item : -1;

    if (doc_export_ascii(e->doc, path, f[2].sel == 0, charset_of[f[3].sel],
                         item, err, sizeof err) != 0) {
        log_error("stred: the export of the text to %s failed: %s", path,
                  err);
        popup_msg("Export failed", err);

        return;
    }

    log_note("stred: exported the text of the document to %s", path);
    ed_msg(e, "Exported %s", path);
}
