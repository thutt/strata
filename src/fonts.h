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

/*
 * fonts.h - the list of fonts for printing.  A font of a document is a
 * number (1 Headline, 2 Text, 3 Header/Footer, 4 to 20 the other fonts); the
 * list says which LaTeX font each number is.  It is the Maintain Download
 * Fonts panel of str (docs/FONTS.txt).  The list is kept for the user in the
 * file ~/.strata/fonts; the entries that are not in it have the defaults, the
 * fonts that every LaTeX has.  An entry can name a font that is not
 * installed: then the archive to download from CTAN and the map file to enable
 * are given, and fonts_install does it.
 */
#ifndef FONTS_H
#define FONTS_H
#include <stddef.h>

#define FONT_COUNT 20

enum {
    FF_ROMAN, /* \rmfamily */
    FF_SANS, /* \sffamily */
    FF_MONO /* \ttfamily */
};

typedef struct {
    char name[48]; /* what the panels show, such as Palatino */
    char package[48]; /* the LaTeX package, or "" for the fonts of LaTeX */
    char options[64]; /* the options of the package, or "" */
    int family; /* FF_ROMAN, FF_SANS or FF_MONO */
    char ctan[48]; /* the archive on CTAN (install/fonts/NAME.tds.zip) */
    char map[48]; /* the map file to enable after installing, or "" */
} FontEntry;


/* The longest label that fonts_labels makes, with its end. */
#define FONT_LABEL 80


/*
 * The labels of the font numbers for a choice list: the role or the number
 * and the name of the entry in the list (as fonts_load gives it), such as
 * "Text (Latin Modern Roman)" or "Font 4 (Times)".
 */
void
fonts_labels(char labels[FONT_COUNT][FONT_LABEL]);


/* The list that a user who changed nothing has. */
void
fonts_defaults(FontEntry t[FONT_COUNT]);


/* The defaults, changed by the file of the user when it is there. */
void
fonts_load(FontEntry t[FONT_COUNT]);


/* Write the list; returns 0, or -1 with a message in err. */
int
fonts_save(const FontEntry t[FONT_COUNT], char *err, int errcap);


/* A value that is safe to write in a TeX file and in a command: letters,
   digits and . _ - + = , only.  Returns 1 when it is. */
int
fonts_word_ok(const char *s);


/* \rmfamily, \sffamily or \ttfamily. */
const char *
fonts_family_command(int family);


/* \usepackage[options]{package}, or "" when the entry has no package. */
void
fonts_package_line(const FontEntry *e, char *out, size_t cap);


/* Is the package of the entry found by TeX (kpsewhich)?  1 when it is, and
   for an entry without a package. */
int
fonts_installed(const FontEntry *e);


/*
 * Ask kpsewhich about the packages of a whole table at once (one program
 * for up to 14 packages) and keep the answers, so that fonts_installed
 * finds them.  A second call asks only for the packages not known yet.
 */
void
fonts_installed_prefetch(const FontEntry t[FONT_COUNT]);


/* Forget what is known of the package of an entry (it was just installed). */
void
fonts_installed_forget(const FontEntry *e);


/*
 * Download the font of the entry and install it for the user: the archive
 * is fetched (from https://mirrors.ctan.org/install/fonts/NAME.tds.zip, or
 * from the address in the environment variable STRATA_CTAN_URL followed by
 * /NAME.tds.zip), unpacked into the TeX directory of the user (TEXMFHOME,
 * ~/texmf), the file name database is made again and the map file is
 * enabled.  Returns 0, -1 with the reason in msg, or 1 when the user
 * stopped it (msg says so; nothing of the font is left).
 *
 * While a step runs, tick (when it is not NULL) is called about every 50
 * milliseconds with the line that says what is being done, such as
 * "Downloading Lato... 3.2 MB so far", and with can_stop: 1 while the
 * download runs, which Esc may stop, 0 for the quick steps that must not be
 * cut off.  A tick that returns a non-zero value stops the download.  The
 * tick is also the place where the caller waits for a key, so that the
 * loop does not spin.
 */
typedef int (*FontTick)(void *ctx, const char *text, int can_stop);


int
fonts_install(const FontEntry *e, char *msg, size_t cap, FontTick tick,
              void *ctx);
#endif
