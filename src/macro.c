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

/* macro.c - macros; see macro.h and TOOLS-MENU.txt. */
#include "util.h"
#include "macro.h"
#include "config.h"
#include "readfile.h"
#include "msdoc.h"
#include "filemenu.h"
#include "term.h"
#include "widgets.h"
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A macro set is small: a file above this size is not one. */
#define MACRO_FILE_MAX (1u << 20)
#include <sys/stat.h>

#define MAC_MAX 64
#define KEYS_MAX 2000

typedef struct {
    char key; /* an upper case letter or a digit */
    char desc[80];
    int *keys;
    int n;
    int include; /* saved in the macro set */
} Macro;

static Macro macros[MAC_MAX];
static int nmac;
static char set_desc[160];
static char set_author[160];
static int set_rev;

/* The macro being learned. */
static char learn_key;
static char learn_desc[80];
static int learn_include;

static const char *const include_choices[] = {"No", "Yes", NULL};
static const char *const retrieve_choices[] = {"Overwrite Current Macros",
                                               "Merge with Current Macros",
                                               NULL};


int
macro_count(void)
{
    return nmac;
}


static int
mac_find(char key)
{
    for (int i = 0; i < nmac; i++) {
        if (macros[i].key == key) {
            return i;
        }
    }

    return -1;
}


/* Make or replace the macro of key; it takes the keys. */
static void
mac_put(char key, const char *desc, int *keys, int n, int include)
{
    int at = mac_find(key);

    if (at >= 0) {
        free(macros[at].keys);
    } else {
        if (nmac == MAC_MAX) {
            free(keys);

            return;
        }

        at = nmac++;
    }

    macros[at].key = key;
    str_copy(macros[at].desc, sizeof macros[at].desc, desc);
    macros[at].keys = keys;
    macros[at].n = n;
    macros[at].include = include;
}


static void
mac_remove(int at)
{
    free(macros[at].keys);

    for (int i = at; i + 1 < nmac; i++) {
        macros[i] = macros[i + 1];
    }

    nmac--;
    memset(&macros[nmac], 0, sizeof macros[nmac]);
}


/* --------------------------------------------------------- the keys */
/*
 * A key in a macro file is 0x11, a code and a flag byte.  The low bits of
 * the flag tell the kind of key and the high bits the modifiers: 0x40 for
 * Ctrl and 0x80 for Alt.  A character key has the flag 0 and the upper case
 * character as its code; 0x11 is an editing key (Enter 0, Insert 1, Delete
 * 2, Escape 3, Backspace 5, Tab 7), 0x12 a function key (its number, 1 to
 * 11: F12 is the menu key and is never kept) and
 * 0x14 a cursor key (Up 0, Down 1, Left 2, Right 3, Page Up 4, Page Down 5,
 * Home 6, End 7).  A character that is typed without a modifier is the byte
 * itself.  Shift is not kept.
 */
#define KF_EDIT 0x11
#define KF_FUNC 0x12
#define KF_CURSOR 0x14
#define KF_CTRL 0x40
#define KF_ALT 0x80

static const int cursor_keys[8] = {K_UP,   K_DOWN, K_LEFT, K_RIGHT,
                                   K_PGUP, K_PGDN, K_HOME, K_END};


static int
put3(uint8_t *out, int at, int cap, int code, int flag)
{
    if (at + 3 > cap) {
        return 0;
    }

    out[at] = 0x11;
    out[at + 1] = u8(code);
    out[at + 2] = u8(flag);

    return 3;
}


int
macro_encode(const int *keys, int n, uint8_t *out, int cap)
{
    int at = 0;

    for (int i = 0; i < n; i++) {
        int k = keys[i];
        int base = K_BASE(k);
        int flag = ((k & KM_CTRL) != 0 ? KF_CTRL : 0) |
                   ((k & KM_ALT) != 0 ? KF_ALT : 0);
        int w = 0;
        int code = -1;
        int cls = 0;

        if (base == 13 && flag == 0) {
            code = 0;
            cls = KF_EDIT;
        } else if (base == 9 && flag == 0) {
            code = 7;
            cls = KF_EDIT;
        } else if (base == K_ESC) {
            code = 3;
            cls = KF_EDIT;
        } else if (base == K_BKSP) {
            code = 5;
            cls = KF_EDIT;
        } else if (base == K_INS) {
            code = 1;
            cls = KF_EDIT;
        } else if (base == K_DEL) {
            code = 2;
            cls = KF_EDIT;
        } else if (base >= K_F1 && base <= K_F(11)) {
            code = base - K_F1 + 1;
            cls = KF_FUNC;
        } else {
            for (int c = 0; c < 8; c++) {
                if (base == cursor_keys[c]) {
                    code = c;
                    cls = KF_CURSOR;
                }
            }
        }

        if (code >= 0) {
            w = put3(out, at, cap, code, cls | flag);
        } else if (base >= 1 && base <= 26 && flag == 0) {
            w = put3(out, at, cap, 'A' + base - 1, KF_CTRL);
        } else if (base >= 32 && base < 256 && flag != 0) {
            w = put3(out, at, cap, base >= 'a' && base <= 'z' ? base - 32 :
                                                                base,
                     flag);
        } else if (base >= 32 && base < 256) {
            if (at < cap) {
                out[at] = u8(base);
                w = 1;
            }
        } else {
            continue;
        }

        if (w == 0) {
            break;
        }

        at += w;
    }

    return at;
}


int
macro_decode(const uint8_t *in, int n, int *keys, int cap)
{
    int nk = 0;
    int i = 0;

    while (i < n && nk < cap) {
        if (in[i] != 0x11) {
            keys[nk++] = in[i++];
            continue;
        }

        if (i + 2 >= n) {
            break;
        }

        int code = in[i + 1];
        int flag = in[i + 2];
        int cls = flag & 0x1F;
        int mods = ((flag & KF_CTRL) != 0 ? KM_CTRL : 0) |
                   ((flag & KF_ALT) != 0 ? KM_ALT : 0);
        int k = -1;

        i += 3;

        if (cls == 0) {
            int base = code >= 'A' && code <= 'Z' ? code + 32 : code;

            if (mods == KM_CTRL && code >= 'A' && code <= 'Z') {
                k = code - '@';
            } else {
                k = base | mods;
            }
        } else if (cls == KF_EDIT) {
            static const int edit[8] = {13, K_INS, K_DEL, K_ESC,
                                        -1, K_BKSP, -1, 9};

            k = code >= 0 && code < 8 && edit[code] >= 0 ?
                    (edit[code] | ((code == 0 || code == 7) ? 0 : mods)) :
                    -1;
        } else if (cls == KF_FUNC) {
            k = code >= 1 && code <= 11 ? (K_F(code) | mods) : -1;
        } else if (cls == KF_CURSOR) {
            k = code >= 0 && code < 8 ? (cursor_keys[code] | mods) : -1;
        }

        if (k >= 0) {
            keys[nk++] = k;
        }
    }

    return nk;
}


/* ------------------------------------------------------- the notation */
/*
 * The keystrokes are shown and edited as text: a character is itself, a
 * "<" is "<<", and any other key is its name in angle brackets, such as
 * <Enter>, <Up>, <F10>, <Alt-Q> or <Ctrl-B>.
 */
static const struct {
    const char *name;
    int key;
} key_names[] = {{"Enter", 13},      {"Tab", 9},         {"Esc", K_ESC},
                 {"BkSp", K_BKSP},   {"Ins", K_INS},     {"Del", K_DEL},
                 {"Up", K_UP},       {"Down", K_DOWN},   {"Left", K_LEFT},
                 {"Right", K_RIGHT}, {"Home", K_HOME},   {"End", K_END},
                 {"PgUp", K_PGUP},   {"PgDn", K_PGDN},   {"F1", K_F(1)},
                 {"F2", K_F(2)},     {"F3", K_F(3)},     {"F4", K_F(4)},
                 {"F5", K_F(5)},     {"F6", K_F(6)},     {"F7", K_F(7)},
                 {"F8", K_F(8)},     {"F9", K_F(9)},     {"F10", K_F(10)},
                 {"F11", K_F(11)},   {"NBSP", K_NBSP}};

#define NKEYNAMES isz(sizeof key_names / sizeof key_names[0])


static void
format_keys(const int *keys, int n, char *out, size_t cap)
{
    size_t at = 0;

    out[0] = '\0';

    for (int i = 0; i < n; i++) {
        int k = keys[i];
        int base = K_BASE(k);
        char piece[40];

        piece[0] = '\0';

        if (base == '<' && (k & ~0xFF) == 0) {
            snprintf(piece, sizeof piece, "<<");
        } else if (base >= 32 && base < 256 && (k & ~0xFF) == 0) {
            snprintf(piece, sizeof piece, "%c", chr(base));
        } else {
            char name[16] = "";
            const char *pre = (k & KM_CTRL) != 0 ? "Ctrl-" : "";
            const char *alt = (k & KM_ALT) != 0 ? "Alt-" : "";

            if (base >= 1 && base <= 26 && base != 9 && base != 13) {
                snprintf(name, sizeof name, "Ctrl-%c", 'A' + base - 1);
            } else if (base >= 32 && base < 256) {
                snprintf(name, sizeof name, "%c",
                         base >= 'a' && base <= 'z' ? base - 32 : base);
            } else {
                for (int c = 0; c < NKEYNAMES; c++) {
                    if (key_names[c].key == base) {
                        snprintf(name, sizeof name, "%s", key_names[c].name);
                    }
                }
            }

            if (name[0] == '\0') {
                continue;
            }

            snprintf(piece, sizeof piece, "<%s%s%s>", alt, pre, name);
        }

        size_t pl = strlen(piece);

        if (at + pl + 1 >= cap) {
            break;
        }

        memcpy(out + at, piece, pl + 1);
        at += pl;
    }
}


/* Returns the number of keys, or -1 when the text is not understood. */
static int
parse_keys(const char *s, int *keys, int cap)
{
    int n = 0;

    while (*s != '\0' && n < cap) {
        if (*s != '<') {
            keys[n++] = u8(*s++);
            continue;
        }

        if (s[1] == '<') {
            keys[n++] = '<';
            s += 2;
            continue;
        }

        const char *end = strchr(s, '>');
        char name[24];
        int mods = 0;
        int k = -1;

        if (end == NULL || end == s + 1) {
            return -1;
        }

        size_t nl = strlen(s + 1) - strlen(end);

        if (nl >= sizeof name) {
            return -1;
        }

        memcpy(name, s + 1, nl);
        name[nl] = '\0';
        s = end + 1;

        char *p = name;

        for (;;) {
            if (strncasecmp(p, "Alt-", 4) == 0) {
                mods |= KM_ALT;
                p += 4;
            } else if (strncasecmp(p, "Ctrl-", 5) == 0) {
                mods |= KM_CTRL;
                p += 5;
            } else {
                break;
            }
        }

        for (int c = 0; c < NKEYNAMES && k < 0; c++) {
            if (strcasecmp(p, key_names[c].name) == 0) {
                k = key_names[c].key;
            }
        }

        if (k < 0 && p[0] != '\0' && p[1] == '\0') {
            int ch = tolower(u8(p[0]));

            if ((mods & KM_ALT) == 0 && (mods & KM_CTRL) != 0 && ch >= 'a' &&
                ch <= 'z') {
                keys[n++] = ch - 'a' + 1;
                continue;
            }

            k = ch;
        }

        if (k < 0) {
            return -1;
        }

        keys[n++] = k | mods;
    }

    return n;
}


/* ------------------------------------------------------------ play */
int
macro_play(int k)
{
    int base = K_BASE(k);

    if ((k & KM_ALT) == 0 || (k & KM_CTRL) != 0 || base >= 256) {
        return 0;
    }

    int at = mac_find(chr(toupper(base)));

    if (at < 0) {
        return 0;
    }

    term_inject(macros[at].keys, macros[at].n);

    return 1;
}


/* ----------------------------------------------------------- panels */
static void
text_field(WField *f, const char *label, const char *val, int pick,
           const char *suffixes)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_TEXT;
    str_copy(f->val, sizeof f->val, val);
    f->pick = pick;
    f->suffixes = suffixes;
}


static void
choice_field(WField *f, const char *label, const char *const *choices,
             int sel)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_CHOICE;
    f->choices = choices;
    f->sel = sel;
}


/* The macro key field: Alt- and one letter or digit. */
static int
check_key(const WField *f, int n UNUSED, char *err, int errcap)
{
    if (f[0].val[0] == '\0' || f[0].val[1] != '\0' ||
        isalnum(u8(f[0].val[0])) == 0) {
        snprintf(err, usz(errcap), "The macro key is a letter or a digit");

        return 0;
    }

    return 1;
}


static char
key_of(const char *val)
{
    return chr(toupper(u8(val[0])));
}


void
macro_edit(Editor *e)
{
    WField pick[1];
    WField f[4];
    int keys[KEYS_MAX];
    char text[1024];
    int at;
    int n = 0;

    text_field(&pick[0], "Macro Key: Alt-", "", PICK_NONE, NULL);

    if (popup_form("Macro Edit", pick, 1, check_key) == 0) {
        return;
    }

    char old = key_of(pick[0].val);

    at = mac_find(old);
    text[0] = '\0';

    if (at >= 0) {
        format_keys(macros[at].keys, macros[at].n, text, sizeof text);
    }

    text_field(&f[0], "Macro Key: Alt-", pick[0].val, PICK_NONE, NULL);
    text_field(&f[1], "Description", at >= 0 ? macros[at].desc : "",
               PICK_NONE, NULL);
    text_field(&f[2], "Keystrokes", text, PICK_NONE, NULL);
    choice_field(&f[3], "Include in Macro Set?", include_choices,
                 at >= 0 ? macros[at].include : 0);

    for (;;) {
        if (popup_form("Macro Edit", f, 4, check_key) == 0) {
            return;
        }

        n = parse_keys(f[2].val, keys, KEYS_MAX);

        if (n >= 0) {
            break;
        }

        popup_msg("Macro Edit", "The keystrokes are not understood (a key "
                                "name is in < >, and < is <<)");
    }

    int *copy = malloc(usz(n > 0 ? n : 1) * sizeof(int));

    memcpy(copy, keys, usz(n) * sizeof(int));
    mac_put(key_of(f[0].val), f[1].val, copy, n, f[3].sel);
    ed_msg(e, "Macro Alt-%c has %d keystroke%s", key_of(f[0].val), n,
           n == 1 ? "" : "s");
}


void
macro_delete(Editor *e)
{
    WField f[1];

    if (nmac == 0) {
        popup_msg("Delete Macro", "There are no macros");

        return;
    }

    text_field(&f[0], "Macro Key: Alt-", "", PICK_NONE, NULL);

    for (;;) {
        if (popup_form("Delete Macro", f, 1, check_key) == 0) {
            return;
        }

        if (mac_find(key_of(f[0].val)) >= 0) {
            break;
        }

        popup_msg("Delete Macro", "No such macro");
    }

    int at = mac_find(key_of(f[0].val));
    char q[96];

    snprintf(q, sizeof q, "Delete the macro Alt-%c?", macros[at].key);

    if (popup_yn(q) != 0) {
        mac_remove(at);
        ed_msg(e, "Deleted");
    }
}


/* Called when Ctrl-F1 ends the recording. */
static void
learn_done(void)
{
    int n;
    const int *rec = term_recorded(&n);
    int *copy = malloc(usz(n > 0 ? n : 1) * sizeof(int));

    memcpy(copy, rec, usz(n) * sizeof(int));
    mac_put(learn_key, learn_desc, copy, n, learn_include);
}


void
macro_learn(Editor *e)
{
    WField f[3];

    text_field(&f[0], "Macro Key: Alt-", "", PICK_NONE, NULL);
    text_field(&f[1], "Description", "", PICK_NONE, NULL);
    choice_field(&f[2], "Include in Macro Set?", include_choices, 0);

    if (popup_form("Define Macro", f, 3, check_key) == 0) {
        return;
    }

    learn_key = key_of(f[0].val);
    str_copy(learn_desc, sizeof learn_desc, f[1].val);
    learn_include = f[2].sel;
    term_record(1, learn_done);
    ed_msg(e, "Defining Alt-%c: press Ctrl-F1 to end", learn_key);
}


/* ------------------------------------------------------------ files */
static int
check_name(const WField *f, int n UNUSED, char *err, int errcap)
{
    if (f[1].val[0] == '\0') {
        snprintf(err, usz(errcap), "File must be specified (File Name)");

        return 0;
    }

    return 1;
}


/*
 * The file of an initial macro set or library: a name with a slash at its
 * start is the file itself; any other name is looked for in the working
 * directory first, then in the settings directory.  A name without a dot gets
 * the extension ext.  The path of the first file that exists, or else the one
 * in the working directory, is put in out.
 */
void
initial_file_path(const char *dir, const char *name, const char *ext,
                  char *out, size_t cap)
{
    char file[PATH_MAX];
    char cand[PATH_MAX];
    char conf[PATH_MAX];

    str_copy(file, sizeof file, name);

    if (strchr(file, '.') == NULL) {
        size_t k = strlen(file);

        str_copy(file + k, sizeof file - k, ext);
    }

    if (file[0] == '/') {
        str_copy(out, cap, file);

        return;
    }

    path_join(out, cap, dir, file);

    if (access(out, R_OK) == 0) {
        return;
    }

    if (config_directory(conf, sizeof conf) == 0) {
        path_join(cand, sizeof cand, conf, file);

        if (access(cand, R_OK) == 0) {
            str_copy(out, cap, cand);
        }
    }
}


static void
resolve(const char *dir, const char *name, char *out, size_t cap)
{
    char file[PATH_MAX];

    str_copy(file, sizeof file, name);

    if (strchr(file, '.') == NULL) {
        size_t k = strlen(file);

        str_copy(file + k, sizeof file - k, ".mac");
    }

    if (file[0] == '/') {
        str_copy(out, cap, file);
    } else {
        path_join(out, cap, dir, file);
    }
}


static void
put_bytes(FILE *fp, const void *p, size_t n)
{
    if (fwrite(p, 1, n, fp) != n) {
        return;
    }
}


void
macro_save(Editor *e)
{
    WField f[5];
    char rev[32];
    struct stat st;

    snprintf(rev, sizeof rev, "%d.%03d", set_rev / 1000, set_rev % 1000);
    text_field(&f[0], "Working Directory", filemenu_workdir(e), PICK_DIR,
               NULL);
    text_field(&f[1], "File Name", "", PICK_FILE, ".mac");
    text_field(&f[2], "Description", set_desc, PICK_NONE, NULL);
    text_field(&f[3], "Author", set_author, PICK_NONE, NULL);
    text_field(&f[4], "Revision", rev, PICK_NONE, NULL);

    if (popup_form("Save Macro Set", f, 5, check_name) == 0) {
        return;
    }

    char path[PATH_MAX];

    filemenu_set_workdir(f[0].val);
    resolve(f[0].val, f[1].val, path, sizeof path);

    if (stat(path, &st) == 0 &&
        popup_yn("Overwrite old version on disk?") == 0) {
        return;
    }

    str_copy(set_desc, sizeof set_desc, f[2].val);
    str_copy(set_author, sizeof set_author, f[3].val);

    int whole = 0;
    int frac = 0;

    if (sscanf(f[4].val, "%d.%d", &whole, &frac) >= 1 &&
        whole * 1000 + frac > 0) {
        set_rev = whole * 1000 + frac;
    } else {
        set_rev = set_rev > 0 ? set_rev + 1 : 1000;
    }

    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        popup_msg("Save failed", "Cannot write the file");

        return;
    }

    char line[600];
    int len;

    put_bytes(fp, DOC_FIRST_RECORD "\r\n", sizeof DOC_FIRST_RECORD + 1);

    if (set_desc[0] != '\0') {
        len = snprintf(line, sizeof line, "MD%s", set_desc);
        put_bytes(fp, line, usz(len) + 1);
        put_bytes(fp, "\r\n", 2);
    }

    if (set_author[0] != '\0') {
        len = snprintf(line, sizeof line, "MA%s", set_author);
        put_bytes(fp, line, usz(len) + 1);
        put_bytes(fp, "\r\n", 2);
    }

    len = snprintf(line, sizeof line, "MR%d\r\n*B1\r\nS\r\nT", set_rev);
    put_bytes(fp, line, usz(len));
    put_bytes(fp, "\0\r\n+\r\n", 6);

    for (int i = 0; i < nmac; i++) {
        uint8_t keys[2 * KEYS_MAX * 3 + 8];
        int kn;

        if (macros[i].include == 0) {
            continue;
        }

        kn = macro_encode(macros[i].keys, macros[i].n, keys, KEYS_MAX * 3);
        put_bytes(fp, "S\r\nTK\x11", 6);
        put_bytes(fp, &macros[i].key, 1);
        put_bytes(fp, "\x80\x1d\0\r\nT", 6);
        put_bytes(fp, macros[i].desc, strlen(macros[i].desc));
        put_bytes(fp, "\x1d\0\r\n+\r\nS\r\nT", 11);
        put_bytes(fp, keys, usz(kn));
        put_bytes(fp, "\x1d\0\r\n-\r\n", 7);
    }

    put_bytes(fp, "-\r\n*E\r\n", 7);
    fclose(fp);
    ed_msg(e, "Saved %s", path);
}


/*
 * Read the T records of a macro file: the root's, then for each macro its
 * headline, description and keystrokes.  A T record ends at the first NUL
 * that is followed by CR LF.
 */
static int
read_macros(const uint8_t *d, size_t n, int merge, char *verr, size_t vcap)
{
    size_t i = 0;
    int rec = 0;
    char key = 0;
    char desc[80] = "";
    int count = 0;

    verr[0] = '\0';

    /* A file without a first record *R is not a macro set: the message of
       the macro set is kept for it, as the manual documents it. */
    if (n < 2 || d[0] != '*' || d[1] != 'R') {
        return -1;
    }

    /* The version of a file that has the record is checked as for every
       document. */
    if (docver_check(d, n, NULL, verr, vcap) != 0) {
        return -1;
    }

    if (n < 4) {
        verr[0] = '\0';

        return -1;
    }

    if (merge == 0) {
        while (nmac > 0) {
            mac_remove(nmac - 1);
        }
    }

    while (i < n) {
        size_t e = i;
        int is_t = d[i] == 'T';

        if (is_t != 0) {
            while (e + 2 < n && !(d[e] == 0 && d[e + 1] == '\r' &&
                                  d[e + 2] == '\n')) {
                e++;
            }
        } else {
            while (e + 1 < n && !(d[e] == '\r' && d[e + 1] == '\n')) {
                e++;
            }
        }

        const uint8_t *p = d + i + 1;
        size_t pl = e > i + 1 ? e - i - 1 : 0;

        if (is_t != 0) {
            if (rec == 0) {
                /* The root's empty record. */
            } else if (rec % 3 == 1) {
                key = pl >= 3 && p[0] == 'K' && p[1] == 0x11 ?
                          chr(toupper(p[2])) :
                          0;
            } else if (rec % 3 == 2) {
                size_t dl = pl > 0 && p[pl - 1] == 0x1d ? pl - 1 : pl;

                if (dl >= sizeof desc) {
                    dl = sizeof desc - 1;
                }

                memcpy(desc, p, dl);
                desc[dl] = '\0';
            } else if (key != 0) {
                size_t kl = pl > 0 && p[pl - 1] == 0x1d ? pl - 1 : pl;
                int *keys = malloc(usz(KEYS_MAX) * sizeof(int));
                int nk = macro_decode(p, isz(kl), keys, KEYS_MAX);

                mac_put(key, desc, keys, nk, 1);
                count++;
            }

            rec++;
        }

        i = e + (is_t != 0 ? 3 : 2);
    }

    return count;
}


int
macro_load_initial(const char *dir, const char *name, char *why, size_t cap)
{
    char path[PATH_MAX];

    initial_file_path(dir, name, ".mac", path, sizeof path);

    char reason[96];
    size_t len = 0;
    uint8_t *buf = readfile_all(path, MACRO_FILE_MAX, &len, reason,
                                sizeof reason);

    if (buf == NULL) {
        snprintf(why, cap, "cannot read %s: %s", path, reason);

        return -1;
    }

    char verr[200];
    int n = read_macros(buf, len, 0, verr, sizeof verr);

    free(buf);

    if (n < 0) {
        if (verr[0] != '\0') {
            snprintf(why, cap, "%s: %s", path, verr);
        } else {
            snprintf(why, cap, "%s is not a macro set", path);
        }
    }

    return n;
}


void
macro_retrieve(Editor *e)
{
    WField f[3];

    text_field(&f[0], "Working Directory", filemenu_workdir(e), PICK_DIR,
               NULL);
    text_field(&f[1], "File Name", "", PICK_FILE, ".mac");
    choice_field(&f[2], "Options", retrieve_choices, 0);

    if (popup_form("Retrieve Macro Set", f, 3, check_name) == 0) {
        return;
    }

    char path[PATH_MAX];

    filemenu_set_workdir(f[0].val);
    resolve(f[0].val, f[1].val, path, sizeof path);

    char reason[96];
    size_t len = 0;
    uint8_t *buf = readfile_all(path, MACRO_FILE_MAX, &len, reason,
                                sizeof reason);

    if (buf == NULL) {
        popup_msg("Retrieve failed", reason);

        return;
    }

    char verr[200];
    int n = read_macros(buf, len, f[2].sel, verr, sizeof verr);

    free(buf);

    if (n < 0) {
        popup_msg("Retrieve failed", verr[0] != '\0' ? verr :
                  "That file is not a macro set");
    } else {
        ed_msg(e, "Retrieved %d macro%s", n, n == 1 ? "" : "s");
    }
}
