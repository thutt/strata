# Strata

Strata is a technical document editor for long structured documents: reports,
proposals and manuals. It runs in a terminal and in an X11 window. Print and
Preview typeset the document through LaTeX: the Printer writes a LaTeX file
and runs LaTeX, and the Viewer shows the result.

License: GNU General Public License, version 3 or later
([COPYING](COPYING), <https://www.gnu.org/licenses/>).

Status: the first release is in preparation. No release has been made yet. A
release is made from a tag named `vX.Y.0` and is delivered as a tarball.

## What is in it

| Program | What it does |
|---------|--------------|
| `stred` | the Editor: edits a document |
| `str` | the Document Manager: the master menu that starts the Editor |
| `strprint` | the Printer: works through LaTeX, to print a document to a PDF |
| `dviview` | the Viewer: shows the pages that Preview makes |

Each program is built in two versions, one for a terminal and one for an X11
window (`strata-build term` and `strata-build x11`).

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

For a person who has the release tarball `strata-X.Y.0.tar.xz` and its file
`strata-X.Y.0.tar.xz.sha256`. No build tools are needed.

    sha256sum -c strata-X.Y.0.tar.xz.sha256
    tar -xJf strata-X.Y.0.tar.xz
    cd strata-X.Y.0
    bash strata-install

Run the script with `bash`, not with `sh`. An ordinary user installs in the
directory `usr` of the home directory. An administrator installs in `/usr`
and is asked before any missing package is installed. The option `--dry-run`
shows what would be done, and the option `--uninstall` removes Strata again.
Then `~/usr/bin/str` starts the Document Manager and
`~/usr/bin/stred` the Editor.

### Requirements

A terminal or an X server. Everything else is optional, and the editing needs
nothing besides the Editor.

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
  `texlive-latex-extra` (`adjustbox.sty`), `texlive-pictures` (`tikz.sty`)
  and `lmodern` (the default LaTeX fonts).
- `dvipng`: equations, LaTeX markers and keycaps as images in the window
  version.
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
tool and the release procedure.

## Documentation

The release delivers these manuals, as documents that the Editor opens and as
PDF files:

- `GettingStarted`: the guide for a new user.
- `strata-reference-manual`: the Reference Manual.
- `cheat`, `cheat-short` and `mathcheat`: the cards.
- `errata`: the known anomalies of the terminal programs.
- `terminal-keys`: the keys that a terminal keeps for itself.

Their sources are in [manual/src](manual/src), and the design documents are in
[docs](docs).

## Known issues

The [Errata](manual/src/errata.src) lists the known anomalies of the terminal
programs, and [docs/KNOWN-ISSUES.txt](docs/KNOWN-ISSUES.txt) the other known
problems. A terminal draws single VT100 lines by default; the field Terminal
Characters in Setup selects Automatic, ASCII, VT100 or UTF-8.

## Repository

| Directory | Contents |
|-----------|----------|
| `src` | the programs: the Editor, the Document Manager and the Printer |
| `tools` | the Viewer and helper programs |
| `tests` | the tests |
| `scripts` | the build, install and release scripts |
| `manual` | the sources of the manuals and the manual build |
| `docs` | design notes and studies |
| `help` | the text of the on-line help |
| `templates` | templates for letters, envelopes and labels |
| `samples` | sample documents |

The branch `development-1` holds the work for the first release. The license
is in [COPYING](COPYING).
