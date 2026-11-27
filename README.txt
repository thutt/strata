Strata is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

Strata is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along
with Strata.  If not, see <https://www.gnu.org/licenses/>.

Copyright (c) 2026 Logic Magicians Software


STRATA -- A TECHNICAL DOCUMENT EDITOR
=====================================

Strata edits long structured documents: reports, proposals and manuals made of
sections, blocks and columns, with outline collapse/expand, tables, markers and
on-line help. It has two front ends, a terminal and an X11 window, and it reads
and writes the Strata document format.

The programs:

  - the Editor (stred): edits a document
  - the Document Manager (str): the master menu that starts the Editor and
    the other tools
  - the Printer (strprint): prints a document through LaTeX (pdflatex, or
    latex for a DVI) to a PDF; a program of its own, not a part of the Editor
  - the Viewer (dviview): shows the DVI file that F12, Print, Preview makes
    ("strata-build dviview" builds it next to the Editor)

License: GNU General Public License version 3 or later (COPYING).

Installing
----------

Build with scripts/strata-build (see "Building" in the manual), then install
what was built under a prefix with scripts/strata-install.  The default prefix
is /usr, which needs administrator privileges:

    source scripts/setup /tmp/strata           # once per shell
    scripts/strata-build                        # build everything
    sudo scripts/strata-install --build-dir "$STRATA_BUILD_DIR"

Without administrator privileges the script installs into $HOME/usr:

    scripts/strata-install --build-dir "$STRATA_BUILD_DIR"

'scripts/strata-build install' installs nothing: it makes the install
tarball in the build directory.

An ordinary user's install is made whether or not the programs that Strata can
use at run time are on the system: they are optional.  A warning at the end of
the output, after everything else, lists each missing package with what is lost
without it and the apt-get command that an administrator runs; the exit status
is 0.  The prefix may be another directory that you own (--prefix DIR); the
same rules apply to it.  The script puts every file in place with
/usr/bin/install and writes a manifest.  As an administrator it also looks at
the programs that Strata needs at run time and, after a question, installs the
ones that are missing (see below).  Options: --prefix DIR, --destdir DIR (for a
package: the files go under DIR/PREFIX), --check (only the check), --no-prereqs
(install Strata alone), --yes, --list-prereqs, --dry-run, --quiet and
--uninstall.

The layout under the prefix:

    bin/str stred strprint dviview     the terminal version and the Printer
    bin/str-x11 stred-x11              the X11 version (when it was built)
    bin/strata-run COMMAND...          runs COMMAND with the installed
                                       programs first in the PATH
    share/strata/strata.hlp            the help file
    share/strata/doc/                  the documents of Strata
    share/strata/pdf/                  the manuals as PDF files (when built)
    share/strata/samples/              the sample documents
    share/strata/samples/envelopes/    the sample envelopes, one for each
                                       style of envelope
    share/strata/tutorial/             the documents of the Tutorial and their
                                       pictures, in a directory next to them
    share/strata/templates/            the templates of New, in folders:
                                       envelopes, letters and labels/avery
    share/strata/manifest              every file and directory installed
    share/doc/strata/                  this file and the licence

At the end the script lists where each group of files went, with the number of
files and their names.

Every program finds its files relative to where it is installed: stred and
str find the help file in share/strata above their own directory, str starts
the stred beside it (str-x11 starts stred-x11), and the Editor finds the
Printer and the Viewer in the same directory.  Nothing refers to the build
directory or to /usr/share, so the installed tree can be moved, and it runs
from a prefix under a home directory as it does from /usr.  The
settings Help File and Editor program of Setup, and the variables STRATA_HELP
and STRED, still override the places.  The version is the name of the branch
that the programs were built from (docs/VERSIONING.txt); "stred --version"
prints it.

    scripts/strata-build uninstall --prefix $HOME/usr

removes exactly what the install put there (the files of the manifest, and the
directories that the install made, when they are empty).  Packages that apt
installed stay.

The programs that Strata needs to run
-------------------------------------

The programs of the Printer (LaTeX, dvipng, ghostscript), the spell check and
the thesaurus are not part of Strata.  The install script looks for them, and
you do not install them by hand.  First it prints what is present and what is
missing:

    What Strata needs to run:
      present  LaTeX: Print to PDF, Preview and the equations on the screen
      missing  dvipng: equations, keycaps and pictures in the X11 version
      ...

As an administrator on a Debian or Ubuntu machine, if something is missing, the
script queries apt for the packages that are needed and prints a table with
every package that would be installed (name, version, estimated download size,
estimated
installed size and what it is needed for) and the totals, and then prompts
"Install these packages? [y/N]".  Only y or Y goes on; any other answer, or
just Enter, stops the script with exit status 3 before anything is installed
and ends the output with the same warning that lists the missing packages
(--no-prereqs installs Strata alone).  Without a terminal nothing is installed
and the script exits with status 4 unless --yes was given (--yes means: do not
prompt).  --list-prereqs prints what is present and, for an administrator, the
table, and stops.

The packages are installed with apt, into the system.  If that fails with the
symptom of an old package index ("Failed to fetch", "404 Not Found", "Unable to
fetch some archives": the mirrors no longer have versions that the index names,
as on a fresh system) the script prompts "Run apt-get update and try again?
[y/N]" and, on y (or with --yes, which puts no question), runs apt-get update
and makes a second attempt of the install with --fix-missing; on n, or if the
second attempt fails, the script stops with exit status 1 and prints the
commands to run by hand (sudo
apt-get update, then the install again).  A failure of another kind (no disk
space, a dpkg error) is reported as it is and does not offer the update.  The
install of the packages is up to about 700 MB on a machine that has no TeX
Live.

Anybody else gets no installed programs: Strata is installed, and the warning
at the end names the missing packages and the command for an administrator
(sudo apt-get install --no-install-recommends PACKAGES).  The exit status is
0, and nothing about the missing programs is printed before the warning.
Strata uses the programs as soon as they are installed.  --no-prereqs installs
Strata alone, without the check and without the warning.

Not installed by the script: the programs for the printer queue (cups-client
needs a running CUPS server), a PDF viewer, and the C compiler.  A machine
that has no apt (not Debian or Ubuntu) gets a message, and the rest of the
install is done; the Prerequisites below name what to
install with its own package manager.

A developer who wants to know what building, testing and running need runs

    scripts/strata-build check-tools

which lists the programs, headers and libraries that are present or missing,
the Debian package of each missing one, and one apt command that installs them
all.  Nothing is installed, and no build directory is needed.

Errors of a document
--------------------

When the Printer typesets a document (Print or Preview) it also checks it.
What it finds wrong is reported: an equation that is not valid TeX math, a
picture that is missing or cannot be read, a character that no font has, a
Keep that is never closed, an error that LaTeX reports.  The printed
document ends with an Error Log page (turn it off with "Error Log? No" in
the Print and Preview panels, or --no-error-log).  A print with errors has
FAILED: every page of its PDF or DVI file carries the banner "PRINT FAILED -
N ERRORS - THIS PRINT IS INACCURATE" in red, a red E3 (or orange W3) label
with the number of the Error Log entry marks each place, and the Viewer shows
a red status area (--no-banner leaves the banner out).  The Editor also puts a
PRINT ERROR marker in the text at each place, so the errors can be fixed
there and then: the Print menu has Next Error (Ctrl-F8), Previous Error
(Ctrl-F7), Remove Error and Remove Print Errors; the Search panel (F9) can
look for the next Print Error and Ctrl-F9 repeats the search.  You cannot
make a Print Error yourself, and it is never printed.  The Document
Manager's Print and Preview write the errors as text at the end.

X11 frontend
------------

    # needs libx11-dev; builds $STRATA_BUILD_DIR/x11/stred and .../str
    make x11
    # or .../x11/stred file.doc  -- opens a window instead of using the terminal
    $STRATA_BUILD_DIR/x11/str

Same programs, same keys, drawn in an X window of 80x50 character cells. With
Xft installed it uses anti-aliased DejaVu Sans Mono with real bold and italic
faces; otherwise it falls back to a monospaced Unicode core font. The keypad
+ - * keys do the outline commands (collapse/expand/outline), Alt-F2...F9,
Alt-F12 and Shift/Ctrl cursor combinations arrive natively (no Ctrl-F
workaround needed), and closing the window acts as Ctrl-Q. In the Document
Manager, Edit opens the Editor in a second window and the Document Manager
returns when it closes.

Optional environment: STRATA_X11_XFT (fontconfig family, e.g. Liberation Mono; 0
forces the core fonts), STRATA_X11_SIZE (Xft pixel size, default 16; core fonts
13/15/18/20, default 15), STRATA_X11_GEOM (COLSxROWS, default 80x50),
STRATA_X11_FONT (a full core-font XLFD).

The Document Manager
--------------------

$STRATA_BUILD_DIR/term/str is the master menu. Ins (or Enter) selects, Esc
cancels, F1 is help, first-letter jumps.

  - Edit / Preview / Print Document: Document Name / Working Directory /
    Variables panel, + lists files, a missing file prompts for
    Structured/Unstructured, then runs the Editor; Print and Preview are on
    the menu of the Editor (a PDF with the Printer, sent to a CUPS queue
    with lp when a printer is named; the pages in the Viewer)
  - Setup: working directory, editor program, help file and the default
    Printer (a CUPS queue) are saved to ~/.strata/config
  - Screen colors: Setup, Screen Colors (F2) sets the foreground and
    background color of 16 kinds of text, separately for the terminal and
    X11 (keys x11_color_KIND_fg and so on in ~/.strata/config).  The default
    is yellow on blue.  Start with --default-colors, or set
    STRATA_DEFAULT_COLORS=1, to ignore the settings.  STRATA_COLORS (none, 16,
    256, truecolor) gives the number of colors that the terminal has.
  - The spell check is in the Editor (Tools, Spell): it needs aspell or
    hunspell, see Prerequisites

The Editor also takes --preview FILE and -u (create new files unstructured).

Prerequisites
-------------

Package names are Debian/Ubuntu names. Nothing outside this list is needed.

  - gcc (or any C11 compiler), make: building everything; yes
  - libx11-dev: the X11 frontend (make x11); only for X11
  - libxft-dev: anti-aliased fonts in the X11 window; optional (falls back to
    core fonts)
  - aspell and a dictionary (aspell-en), or hunspell and a dictionary
    (hunspell-en-us): the spell check of F12, Tools, Spell and Ctrl-C.
    OPTIONAL.  Strata uses
    the first program that it finds in
    PATH (aspell, then hunspell; the variable STRATA_SPELL names another
    one), with the language of the Dictionary Language of Setup in the
    Document Manager (en_US).  Without them the menu entries stay and a
    message shows what to install, and the Editor works as before.  The
    user's own words are in ~/.strata/dictionary.
  - mythes-en-us: the thesaurus for Tools, Thesaurus (Ctrl-T), from
    /usr/share/mythes.  Without the package Tools, Thesaurus and Ctrl-T say
    that the package is not installed and the Editor works as before.
    STRATA_THESAURUS names another directory or .dat file (an empty value:
    no thesaurus).  The package needs dictionaries-common (installed with
    it); LibreOffice is not needed, it is only suggested.
  - texlive-fonts-recommended and texlive-latex-base: the standard LaTeX fonts
    of the list of the TeX Font Association of the Document Manager, numbers
    1 to 18
  - curl and unzip: only for Install Now of the TeX Font Association, which
    downloads a font from CTAN into ~/texmf; optional
  - Lato, the LaTeX package lato (number 19 of the font list): optional.  It
    is not in a basic TeX Live; the TeX Font Association downloads it from
    CTAN on request (the archive is 24 MB), or install texlive-fonts-extra.
    Without it entry 19 shows "not installed" and prints with the Text font
  - cups-client (lp, lpstat): OPTIONAL, only to send the PDF of F12, Print to a
    printer (Debian and Ubuntu: apt install cups-client; a running CUPS with
    a queue is needed to print).  Without it the PDF is made all the same and
    the Printer field shows "lp not found".  STRATA_LP and STRATA_LPSTAT name
    other programs.  The PDF needs pdflatex and the Printer, not CUPS.
  - evince, or another program that shows PDF files: OPTIONAL, only for Open
    to print in the Print panels, which opens the PDF in it so that it is
    printed from its dialog (sudo apt install evince).  Evince is the default
    of the field PDF Viewer of Setup.  The Editor looks for evince, okular,
    atril, zathura, qpdfview, mupdf, xpdf and epdfview in PATH, in this order
    (never a browser, and never a program that edits documents: xdg-open,
    gio and open are not used because on some machines they start a word
    processor).  The setting pdf_viewer of ~/.strata/config, which Setup
    shows as PDF Viewer, names another program; a word processor (soffice,
    libreoffice, oowriter, oodraw, lowriter, lodraw, abiword, wps, calligra,
    winword) is refused.  Without a
    viewer, or without a display, the PDF is made and saved, and the message
    line reports this.
  - latex, dvipdfmx, pdflatex (texlive-latex-base, texlive-latex-extra,
    texlive-latex-recommended, texlive-extra-utils, lmodern) and ghostscript:
    the Printer, F12, Print, Draft and Final (a PDF through the Printer and
    pdflatex), and F12, Print, Preview, which runs the Printer with latex and
    then the Viewer.  All optional: without them Print and Preview say what is
    missing.  The Viewer needs libx11-dev, libfreetype-dev and libpng-dev to
    build ("strata-build dviview" stops and names the packages that are
    missing) and an X display to run.

To install the full set of the programs (everything above, and the LaTeX
fonts that the list of the TeX Font Association requires):

    sudo apt-get install build-essential make libx11-dev libx11-6 libxft-dev \
        libxft2 libfreetype-dev libfontconfig-dev libpng-dev \
        mythes-en-us dictionaries-common aspell aspell-en hunspell \
        hunspell-en-us curl unzip cups-client evince ghostscript \
        texlive-latex-base texlive-latex-extra texlive-latex-recommended \
        texlive-binaries texlive-pictures texlive-plain-generic lmodern \
        texlive-fonts-recommended texlive-fonts-extra texlive-extra-utils \
        tex-gyre

The fonts of the TeX Font Association (the list of the Document Manager) are
the LaTeX packages lmodern, mathptmx, helvet, courier, mathpazo, bookman,
newcent, avant, chancery, charter, utopia, tgtermes, tgheros, tgpagella and
tgcursor, which texlive-latex-base, texlive-latex-recommended,
texlive-fonts-recommended and tex-gyre provide, and lato (number 19), which
texlive-fonts-extra provides.  A machine without texlive-fonts-extra shows
the entry of Lato as not installed.

Prerequisites of the tests and of the pictures of the manual
------------------------------------------------------------

The tests (strata-build test, smoke, xtest) and the manual (manual/build.sh)
need, in addition to the full set above:

  - python3: every test and every tool of the manual.
  - xvfb, xdotool, x11-apps (xwd) and x11-utils (xwininfo): the X11 tests, the
    Viewer tests, the install test and the pictures of the manual run the X11
    programs on a virtual display.
  - bubblewrap (bwrap): the install and temporary file tests run in a private
    view of the file system.
  - curl, unzip and cups-client (lp): the tests of Install Now of the TeX Font
    Association and of the choice of the printer fail without them, although
    Strata itself runs without them.
  - poppler-utils (pdftotext, pdftoppm, pdfinfo, pdffonts): the print tests
    and the checks of the PDF files read the PDF files with them.
  - dvipng: the equations on the screen of the X11 version and the tests of
    pictures, keycaps and equations that typeset them.
  - latex, pdflatex, dvipdfmx and kpsewhich of the packages above.
  - tmux is not needed.  A window manager is not needed (the tests run
    without one, and so say what they cannot check).

To install the programs of the tests as well:

    sudo apt-get install python3 xvfb xdotool x11-apps x11-utils bubblewrap \
        poppler-utils dvipng curl unzip cups-client

A test that is skipped reports why and which program is missing; a test that
fails for a missing program is a fault of the list above and is to be
reported.

Building
--------

Build products never go into the source tree. Everything is built through a
sourced setup script, scripts/setup in the project directory (the root of the
repository, where the Makefile is), that names the build directory.  (The
sources were in the directory modernization until 2026-11-24; they are at the
root of the tree now.)

    # the root of the repository
    cd <project>
    source scripts/setup /tmp/strata             # the debug build
    source scripts/setup -b release /tmp/strata  # or release, or asan
    make

Or, from any directory once setup has been sourced, strata-build (in scripts/,
on PATH) does the build: strata-build builds everything, strata-build x11 adds
the X11 frontend, strata-build test and strata-build clean pass those targets
to make.  It does not run if the build directory is unset or inside the source
tree.

The install tarball is a build artifact that is made only on request:
strata-build install-tarball (after the programs are built, and after the
manuals are built with strata-build manual) makes
strata-NAME.tar.xz and its checksum file strata-NAME.tar.xz.sha256 in the
build directory, where NAME is the version of the build (for example
strata-development-1-230cb783.tar.xz; a build type other than release is
added to the name, and a build with no supported version makes no tarball).
The default build, strata-build with no argument, does not make it.  The
tarball holds one directory with the install script, the payload and
INSTALL.txt, and two runs from the same commit give the same file.  A reader
checks the download with sha256sum -c, unpacks it with tar -xJf and runs
bash strata-install; INSTALL.txt says so.  It is a fatal error to make the
tarball without every manual and the PDF file of every manual: the message
names what is missing and the command, and no tarball is left.
'strata-build manual' makes
the manuals and their PDF files in the build directory (it needs LaTeX) and
writes nothing into the source tree.  No PDF file is committed (.gitignore and
tests/pdfguard.py).  A release has no
signature: the SHA-256 value is its integrity check, and a reader who wants
more builds from the source.  docs/INSTALL-POLICY.txt has the rules.

A developer makes a release with scripts/strata-release, which takes the
release number X.Y.0 as its argument: 'scripts/strata-release 1.0.0 --dry-run'
checks everything and changes nothing, and 'scripts/strata-release 1.0.0'
makes the annotated tag v1.0.0 on HEAD (or on the commit of --commit), builds
that tag in a scratch worktree, makes the manuals and their PDF files (it
needs LaTeX, which it checks before it makes the tag) and leaves
strata-1.0.0.tar.xz and its .sha256 file in the directory release of the build
root of the environment (STRATA_BUILD_ROOT, set by sourcing scripts/setup:
'source scripts/setup -b release /tmp/strata' first; the tool has no option to
put the files anywhere else).  An existing tag is refused and never changed, a
failure removes the tag that the tool made, and nothing is pushed or signed.
'scripts/strata-release 1.0.0 --rebuild' makes the tarball again from the tag
that exists (for example in a clone on another machine) and changes no tag; its
bytes differ from a release made elsewhere, so the checksum that counts is the
one of the published file.  The tool is never shipped.

Sourcing setup also sets STRATA_HELP (the help file that the build makes) and
STRATA_TEMPLATES (the templates folder of the source tree), so that a program
run in place from the build directory finds its help and its templates as an
installed program does; no other variable is needed. The build also makes a
link share/strata/templates in the build directory, so that a shell that never
sourced setup finds the templates too. An installed program needs no variable.

The directory given is the build root, and the build goes to a directory of
its own below it, named after the build type: /tmp/strata/debug, /tmp/strata/
release or /tmp/strata/asan (debug = -O0 -g, release = -O2, asan = -O1 -g with
AddressSanitizer and UBSan). The default type is debug; choose another with
-b TYPE. Builds of several types can be present at the same time. setup rejects
a build directory inside the source tree and a directory that contains
it. It exports STRATA_BUILD_ROOT, STRATA_BUILD_DIR, STRATA_BUILD_TYPE,
STRATA_DIR, STRATA_ROOT and STRATA_HELP, and defines strata-show-config and
strata-set-build-type TYPE (switch type in the same shell). The Makefile stops
with an error if STRATA_BUILD_DIR is not set. make test runs the test suites;
make clean removes the term/ and x11/ subdirectories of the build directory.

The manual and the Viewer follow the same rule. manual/build.sh makes the
documents (and their lists of headlines), the sample documents and the PDF
files in $STRATA_BUILD_DIR/manual (doc/, samples/ and pdf/ below it; set
MANUAL_OUT to put them elsewhere, or PDFDIR for the PDF files only), and
the Viewer of tools/dviview makes its objects, its program and the results
of its tests below $STRATA_BUILD_DIR as well. The documents that the package
ships (manual/doc and manual/samples) and the pictures of manual/doc/pictures
are sources kept in git: "manual/build.sh publish" copies the documents that
it made into the tree, and "manual/build.sh pics" takes the pictures there.
Those two targets are the only ones that write into the source tree.
"manual/build.sh check-tree" (and strata-build test, in a quick form) builds
and fails if any other file was written there (tests/srctree.py).

Build and run
-------------

    source ../scripts/setup /tmp/strata            # once per shell
    # builds $STRATA_BUILD_DIR/term/stred
    make
    $STRATA_BUILD_DIR/term/stred file.doc
    # the Document Manager (master menu); the Editor alone also offers a
    # simple one
    $STRATA_BUILD_DIR/term/str
    # render a document to formatted plain text
    $STRATA_BUILD_DIR/term/stred --text in.doc out.txt

F1 opens the help for the place that the cursor is in. The help is written in
help/*.txt; the build joins the topics into the file strata.hlp in the build
directory, next to term and x11, where the programs find it (or pass
-H path/to/file, or set STRATA_HELP, which scripts/setup sets). Without a help
file F1 shows a message that no help file was found.

Needs a terminal of at least about 40 columns by 20 rows (80x25 or larger
recommended).  The terminal version draws its screen in one of three ways: in
UTF-8 (double and single lines and the other shapes, in a UTF-8 locale with a
font that has them), with the line characters of the VT100 set (single lines;
ASCII bytes only; nearly every terminal speaks VT100, and no locale or font is
needed), or with plain ASCII look-alikes (+ - = | # : < >).  The default is the
VT100 screen; with the Setup field set to Automatic the program chooses at its
start: in a UTF-8 locale it requests the position of the cursor from
the terminal after one box character (one column: UTF-8); otherwise it uses
VT100 when the type of the terminal (TERM) has the line characters, or the
terminal answers that it is a VT100 or later, else ASCII.  A note is shown once
on the message line, and the choice is written to the log.  STRATA_ASCII=1
forces ASCII, STRATA_ASCII=0 forces UTF-8; the Setup field Terminal Characters
(key terminal_charset: auto, ascii, vt100, utf8; the default, with no setting,
is vt100, and auto is the choice that checks the terminal) does the same.
The font cannot be checked: a font without the shapes shows squares (choose
VT100, whose lines the terminal draws itself).  To test a terminal by hand,
type printf '\033(0lqqk\033(B\n' (a box top: the terminal speaks VT100) and
printf '\342\224\200\n' (a line: UTF-8 with the shape in the font).
docs/TERMINALS.txt has the study of which terminals work.  The Editor needs at
least 20 rows, and its X11 window is not made lower than 20 rows.  The height
of the X11 window is kept in ~/.strata/config (x11_rows) when you change it,
and Setup has a field for it and one for the most rows of the terminal version
(terminal_rows).  The program never changes the size of a terminal window.

Character sets.  Documents keep the IBM code page 437 (CP437) of the original
format; this has not changed.  The File menu's Import and Export read and
write plain text in UTF-8 and in Latin-1 as well as in the code pages: a
character that CP437 has no place for becomes a question mark on Import, and
the message line shows how many were not kept.  A four byte character typed in
the terminal version (an emoji) is one dropped key.  See docs/UTF8.txt for
the study and the stages that are not done.

Keys
----

Where a terminal cannot send a key, there is an alternate.

  - Edit: type, Enter (end-of-line), Tab, Backspace, Del (also deletes an empty
    block), Ctrl-D delete word, Alt-Backspace word back, Alt-O overtype
  - Attributes: Ctrl-B bold, Alt-I italics, Ctrl-U underscore, Ctrl-N normal
  - Structure: Ins new section, Shift-Ins subsection (some terminals reserve it
    for paste: use Ctrl-F -> Append -> Sub-section), Ctrl-A block (same format),
    Ctrl-O block (global), Ctrl-V split block, Ctrl-Y split section, Alt-< /
    Alt-> promote / demote section
  - Outline: Alt-1...9 levels, Alt-0 whole document, Alt-- collapse, Alt-+
    expand, Alt-* outline section
  - Areas: F3 Copy, F4 Cut, F7 Move: pick Text/Column/Block/Section, highlight
    with the cursor keys, Enter; F5 Paste; Ctrl-K cut block, Ctrl-L cut section,
    Ctrl-X cut text
  - Library: Ctrl-F3 Lib Copy, Ctrl-F4 Lib Cut, Ctrl-F5 Lib Paste (the entries
    are made and managed with F12, Tools, Library)
  - Macros: Alt and the macro key runs a macro; F12, Tools, Macros, Learn
    records one (Ctrl-F1 ends the recording)
  - Commands: F2 Split, F6/Ctrl-E Format block, Ctrl-H hanging indent, F8 Table,
    F9 Search, F12 Menu, F1 Help
  - Alt-F commands: Alt-F2...F9 and Alt-F12, or Ctrl-F for a menu of them
    (Append, Join, Delete, Overtype, Set Attr, Go To, Replace, Marker)
  - Files: Ctrl-S save, Ctrl-R retrieve, Ctrl-Q quit, Ctrl-P print (Final:
    a PDF and a printer)

The F12 menu is File Global Edit Print Tools Quit.

Help: Tab/Shift-Tab select a link, Enter follows it, Backspace goes back, I
opens an index of all topics, Esc closes.
