# Strata

Strata is a technical document editor for long structured documents: reports,
proposals and manuals. It runs in a terminal and in an X11 window. Print and
Preview typeset the document through LaTeX.

License: GNU General Public License, version 3 or later
([COPYING](COPYING), <https://www.gnu.org/licenses/>).

## What is in it

The program is `str`. It starts with a menu, from which a document is edited,
previewed and printed. Print and Preview work through LaTeX: the document is
typeset by LaTeX, and Preview shows the pages that result.

`str` is built in two versions, one for a terminal and one for an X11 window
(`strata-build term` and `strata-build x11`).

## What it does

- Structured documents: sections with numbers that follow the outline, which
  can be folded and opened; unstructured documents of plain blocks.
- Columns and tables, with borders.
- Equations written in TeX math, and pictures from PNG, JPEG, PDF and EPS
  files.
- Cross references to the number, the headline or the page of a section.
- A table of contents, lists of figures and tables, and an index.
- Keep markers, which keep a table or a listing together on one page.
- Print and Preview through LaTeX, with an Error Log: what LaTeX found goes
  back into the document as Print Error markers.
- Line numbering in the margin of a print.
- Templates for letters, envelopes and labels.
- A spell check and a thesaurus, when their dictionaries are installed.

## Install

For a person who has the tarball `strata-X.Y.0.tar.xz` and its file
`strata-X.Y.0.tar.xz.sha256`. No build tools are needed.

    sha256sum -c strata-X.Y.0.tar.xz.sha256
    tar -xJf strata-X.Y.0.tar.xz
    cd strata-X.Y.0
    bash strata-install

Run the script with `bash`, not with `sh`. An ordinary user installs in the
directory `usr` of the home directory. An administrator installs in `/usr`
and is asked before any missing package is installed. The option `--dry-run`
shows what would be done, and the option `--uninstall` removes Strata again.
Then `~/usr/bin/str` starts Strata.

### Where the files are installed

The files go below a prefix: the directory `usr` of the home directory for an
ordinary user, and `/usr` for an administrator. PREFIX stands for it below.

    PREFIX/bin/str                     Strata in a terminal
    PREFIX/bin/str-x11                 Strata in an X11 window, when built
    PREFIX/bin/strata-run COMMAND      runs COMMAND with the environment
                                       that the installed Strata needs
    PREFIX/share/strata/strata.hlp     the help file
    PREFIX/share/strata/doc/           the manuals, as Strata documents
    PREFIX/share/strata/doc/challenge/ the challenge document and its
                                       pictures
    PREFIX/share/strata/pdf/           the manuals, as PDF files
    PREFIX/share/strata/samples/       sample documents: letters, envelopes,
                                       labels, fonts, comments, story
    PREFIX/share/strata/tutorial/      the documents of the Tutorial
    PREFIX/share/strata/templates/     the templates of New, in folders
    PREFIX/share/strata/manifest       every file and directory installed
    PREFIX/share/doc/strata/           the license, README.txt and
                                       KNOWN-ISSUES.txt

The programs that `str` starts are in `PREFIX/bin` as well.

### Starting Strata and opening Getting Started

Start the program in a terminal with `str`, or in an X11 window with
`str-x11`; for an ordinary user the full names are `~/usr/bin/str` and
`~/usr/bin/str-x11`, for an administrator `/usr/bin/str` and
`/usr/bin/str-x11`. The menu that appears lists what to do with documents.
Move with Up and Down, and press Ins or Enter to choose.

To read the guide for a new user, choose Edit / Preview / Print Document. In
the panel, give the whole name of the document as the Document Name and press
Ins:

    /home/NAME/usr/share/strata/doc/GettingStarted.doc    ordinary user
    /usr/share/strata/doc/GettingStarted.doc              administrator

NAME is the user name. A tilde is not expanded in a document name, so the
home directory is typed out. Esc goes back, and F1 shows the help.

### Requirements

A terminal or an X server. Everything else is optional, and the editing in a
terminal needs nothing besides Strata itself. The window version, `str-x11`,
needs the libraries `libxft2` and `libfontconfig1`. A missing library or
program never stops the install: the install is made, and its end names what
is missing and which program will not start until it is installed.

LaTeX is needed for Print and Preview. The install script checks for it and
lists what is missing at its end, after everything else; the installation is
made all the same. Without LaTeX:

- Works: typing, editing and saving documents.
- Does not work: printing to a PDF file or to a printer, and Preview. Print
  reports "Print needs LaTeX" and writes no PDF file.
- Equations, LaTeX markers and keycaps are shown in their short form instead
  of as images.

The install script names these packages for LaTeX and the programs around it:

- `texlive-latex-base` (`pdflatex` and `latex`, and the files `fancyhdr.sty`
  and others), `texlive-binaries` (`dvipdfmx` and `kpsewhich`),
  `texlive-latex-extra` (`adjustbox.sty` and others), `texlive-pictures`
  (`tikz.sty`), `texlive-plain-generic` (`ulem.sty`),
  `texlive-latex-recommended` (`xcolor.sty` and others),
  `texlive-fonts-recommended` (the fonts Times, Helvetica and Palatino),
  `tex-gyre` (the TeX Gyre fonts) and `lmodern` (the default LaTeX fonts).
- `dvipng`: equations as images in the window version.
- `poppler-utils` (`pdftoppm`): pictures, keycaps and LaTeX markers as images
  in the window version.
- `ghostscript`: pictures from PDF and EPS files.

For the other optional programs:

- `hunspell` with `hunspell-en-us`, and `mythes-en-us`: the spell check and
  the thesaurus.
- `cups-client` (`lp` and `lpstat`): printing on a printer.
- `curl` and `unzip`: installing a font from Setup.

## Build from the source

    scripts/check-tools                        # the packages that are needed
    source scripts/setup -b release /tmp/strata
    scripts/strata-build
    scripts/strata-build test

[README.txt](README.txt) has the rest: the build types, the tests, the install
tool.

## Documentation

Strata delivers these manuals, as documents that it opens (in
`PREFIX/share/strata/doc`) and as PDF files (in `PREFIX/share/strata/pdf`):

- `GettingStarted`: the guide for a new user.
- `strata-reference-manual`: the Reference Manual.
- `cheat`, `cheat-short` and `mathcheat`: the cards.
- `errata`: the known anomalies of the terminal programs.
- `terminal-keys`: the keys that a terminal keeps for itself.
- `challenge`: "How do your document preparation systems fare?", the
  measured numbers for a long document, as a Strata document and a PDF file.

Every manual has a PDF file with the name of the document and the ending
`.pdf`, such as `PREFIX/share/strata/pdf/GettingStarted.pdf` and
`PREFIX/share/strata/pdf/strata-reference-manual.pdf`, except `terminal-keys`,
which is a part of the Reference Manual and has none of its own. For an
ordinary user PREFIX is `~/usr`, for an administrator `/usr`.

Their sources are in [manual/src](manual/src), and the design documents are in
[docs](docs).

## Known issues

The [Errata](manual/src/errata.src) lists the known anomalies of the terminal
version, and [docs/KNOWN-ISSUES.txt](docs/KNOWN-ISSUES.txt) the other known
problems. A terminal draws single VT100 lines by default; the field Terminal
Characters in Setup selects Automatic, ASCII, VT100 or UTF-8.

## Repository

The top of the source tree:

| Directory | Contents |
|-----------|----------|
| `src` | the programs |
| `tools` | helper programs |
| `tests` | the tests |
| `scripts` | the build, install and version scripts |
| `manual` | the manuals and the manual build |
| `docs` | design notes and studies |
| `help` | the text of the on-line help |
| `templates` | the templates of New |
| `samples` | sample documents |
| `logo` | the definitions of the logos in the documents |
| `notes` | working notes |

Below `manual`:

| Directory | Contents |
|-----------|----------|
| `manual/src` | the sources of the manuals, one text file for each part |
| `manual/doc` | the manuals as Strata documents, and their pictures |
| `manual/challenge` | the challenge document and its pictures |
| `manual/samples` | the documents that the Tutorial teaches with |
| `manual/scenes` | the scripts that take the pictures of pages |

Below `templates` and `samples` are the folders `letters`, `envelopes` and
`labels`; `samples` has also `fonts`, `comments` and `story`.

The files at the top are `Makefile` (used by `scripts/strata-build`),
`README.txt` for developers and [COPYING](COPYING), the license.
