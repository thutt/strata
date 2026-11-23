THE MANUAL OF STRATA

RULES FOR THE PERSON WHO WRITES THE MANUAL

  The manual is for people who use Strata to write.  It says what they see
  and what they do: what the keys and the fields do, the values, the
  messages, the settings, and the programs that must be installed.  It does
  not say how the programs are built.  If a sentence is about how the
  program does something (the order of its steps inside, the files that it
  makes while it works, the codes of characters and keys, the commands that
  it gives to LaTeX, the way one program talks to another, the checks that it
  makes on a directory), delete the sentence.  A detail that a developer
  needs may stay between ".if .STRATA_BUILD_TYPE # release" and ".endif": the
  release edition leaves that text out.  build.sh check-impl (checkimpl.py)
  finds the words and the kinds of sentence that belong to the code, in the
  text that the release edition prints; it fails on a hit that is not
  allowed in checkimpl.allow.

The product of this directory is ONE document: the whole Reference Manual,
strata-reference-manual.doc, and its PDF, strata-reference-manual.pdf (made by
the Printer from that document).  The build writes them, and every other
product, outside the source tree (see WHERE THE PRODUCTS ARE); the copy of the
document that the package ships is doc/strata-reference-manual.doc in the
tree, which "build.sh publish" brings up to date.  It holds, in
this order, the title page, the contents, chapters 1 to 11, appendices A to E,
the glossary (F), the index and the list of figures.  It is a structured
document: every chapter is a section of the first level, so Go To section, the
section tags, the outline and the section commands of the Document Manager
work on it; the cross-references, and the page numbers of the contents and
the index, are cross-references inside the one document; the pictures are in
the document; and the Editor's Search finds a word anywhere in it.

Four documents stay separate, because they are cards and guides and not the
reference: the Key Reference leaflet (cheat and cheat-short), the Math
Symbols card (mathcheat) and Getting Started (the system requirements, the
install, the introduction and the Tutorial, in one document made of the
parts gs-*.src).  A fifth document, Strata File Formats
(doc/strata-file-format.doc), is for people who write programs that make or
read the files of Strata.  It is made here for them, it is not delivered
with the product, and no delivered document refers to it.

THE MATH SYMBOLS CARD

  The card (mathcheat) is a tri-fold sheet: print both sides of one sheet of
  letter paper, landscape, flipped on the short edge, and fold it in three
  (the last panel of the first page is the cover).  The symbols are in
  mathsyms.txt: add a symbol there, run
      python3 -I manual/mkmathcheat.py --measure
  and then build.sh cards and build.sh check-math (every symbol is typeset
  by the Printer, and the check reports which symbols of the packages are not
  on the card).

WHERE THE PRODUCTS ARE

  A build never writes a product into the source tree.  Everything that
  build.sh makes goes into one folder, called the products folder:

      $MANUAL_OUT                          if that variable is set,
      $STRATA_BUILD_DIR/manual             else (scripts/setup sets it),
      the folder "manual" next to $XBIN    else.

  Below it: doc/ holds the documents (the single document, the cards, the
  chapter documents of the target docs), their lists of headlines
  (.doc.meta), the lists of conditional sections (.doc.cond, .doc.hidden)
  and the lists of labels; samples/ holds the sample documents; pdf/ holds
  the PDF files (developer/ the developer edition), or the folder that PDFDIR
  names.  The Printer reads the pictures from doc/pictures of the tree.

  Kept in git, as sources: the documents that the package ships (doc/ and
  samples/) and the pictures (doc/pictures).  Two targets write there, on
  purpose: "build.sh publish" makes the documents again and copies them, and
  "build.sh pics" takes the pictures.  "build.sh check-tree" builds every
  other target and fails when a file was written into the tree; the quick
  form of that test is part of strata-build test (tests/srctree.py).  The
  target clean deletes the products folder.

BUILDING IT

  bash manual/build.sh          Make the single document, its PDF, and the
                                separate cards and guides with their PDF
                                files (the same as the target "all").
  bash manual/build.sh single   Make only the single document and its PDF.
  bash manual/build.sh developer  Print the document for developers.
  bash manual/build.sh check-edition  Check the two PDF files.
  bash manual/build.sh help     List every target.

The programs are found in the directory that XBIN names (the x11 build
directory, such as /tmp/strata/release/x11).  The build needs python3, and
pdflatex for the PDF.  The text is taken from manual/src as it is on the day
the build runs, in the order of manual/src/master.txt.

HOW TO CHANGE THE MANUAL

  1. Edit a chapter source in manual/src and run "build.sh single".  The
     document, the contents, the index, every cross-reference and the page
     numbers are made again from the sources.  This is the way that keeps
     the sources and the document the same.  "build.sh publish" copies the
     result into doc/ of the tree, where the package takes it from.
  2. For a small correction, open doc/strata-reference-manual.doc in the
     Editor and change it there; the contents and the index follow, because
     their page numbers are cross-references that the Printer evaluates.  The
     next "build.sh single" or "publish" makes the document from the
     sources again, so
     make the same correction in the chapter source too.

The sources are written in the plain text form that the header of
mkmanual.py describes.

THE BORDERS OF THE TABLES

Every table of every document has the same borders, and the generator sets
them (mkmanual.py, table() and table_mask(); a source never says anything
about borders).  Tables have a heading row with a single frame (a line at the
top, the left, the right and between the columns) and a double line under the
heading, and its text is bold; the other rows have single lines at the left,
the right, between the columns and at the bottom, and no line at the top,
because the line above closes it.  A table made with .rows has no heading
row: its first row has a single line at its top and is not bold.  A line
between two columns is stored once, as the right side of the left column, as
the Border panel stores it, so the records of the columns are CB89,1 and
CB73,1 for the heading row, CB84,1 and CB68,1 for the other rows and CB85,1
and CB69,1 for the first row of a table made with .rows (the first column
has CL1; every column has CR1).

  build.sh check-tables

(checktables.py, part of "build.sh check") fails when a table of a document
does not follow the rule, and its self-test shows that it can fail: a body row
with only a left line, a heading cell that is not bold and so on.

The same check fails on a row that was split in two by a continuation line
that does not begin with a space.  In a table of the sources a line that
begins with a space continues the row before it; a line that does not is a
new row of its own, so the second half of a wrapped cell becomes a row.  The
check finds the first half: a body row whose first cell ends in an article or
a conjunction ("... of the"), or in a preposition when the first cell of the
next row begins with a small letter.

PARTS: A DOCUMENT THAT ANOTHER DOCUMENT INCLUDES

A part is a document of its own that the Reference Manual includes with the
Include marker of the product, so that the Printer puts its text at the
place of the marker when the manual is printed or previewed.  The first part
is the table of the keys that terminals and desktops keep for themselves
(src/terminal-keys.src, included in appendix D).  A part is written with the
directive .part before any text: it has no headlines, and it is made as a
document with no structure (the Printer puts such a part at the place of the
marker).  The including source says

  .includedoc terminal-keys.doc

which writes the Include marker, alone in a block; put it in the last block
of its section.  The Printer looks for the part in the directory of the
document that holds the marker and then in the working directory, so the part
is kept next to the document: build.sh makes the parts (the names are the
variable partnames of build.sh) with the single document, prints the manual
from the folder that holds them, and publish copies them to doc/ of the
tree, where the package takes them from and where every installed document
of the manual is installed with them.  A part is made with the same rules
as the other documents, so a table in it follows the border rule above.
tests/keypart.py shows that the marker is in the manual, that the part is
beside it, that the Printer puts the table in the print, and that a master
with no part beside it is an error of the Printer.

CHECKING IT

  python3 -I manual/checkpdf.py PDF/strata-reference-manual.pdf \
      PRODUCTS/doc/strata-reference-manual.doc.meta

reads the PDF with pdftotext and checks that the contents and the index give
the page on which each headline is printed, that no cross-reference is left
unresolved, that no page is blank and that no program is named.  The Editor,
the Printer and the Viewer on a synthetic document of more than a thousand
pages are tested by "make test" (the Editor) and "make big" (everything); see
docs/BIG-DOCUMENTS.txt.

CHECKING THE PICTURES

  build.sh pics-check

takes every picture of the manual again from the programs of the tree (the
scenes of scenes/*.py, with Xvfb and xdotool) into a scratch folder, and
compares each one with the picture of doc/pictures with checkpics.py.  The
pictures are compared as shapes and as colours.  Shapes: the light pixels
and the dark pixels are compared one after the other (a letter, a line, a
box or a menu entry that is in one picture and not in the other one counts,
whether it is light on a dark ground, such as the text of the screen, or dark
on a light ground, such as the key strip and the menu bars); the colours at
the edge of a letter (they depend on the font settings of the machine) do
not count.  Colours: the pixels that are in another class of colour (four
levels of each of red, green and blue) count, so that a letter that is yellow
in one picture and white in the other one is found.  The program lists the
pictures that differ by more than 30 pixels of shape (the variable
PICTHRESHOLD changes the margin) or by more than 300 pixels of colour
(PICCOLOURTHRESHOLD), the pictures that have another size, the pictures that
only one side has and the scenes that were skipped.  PICFOOT=40 leaves the
last 40 rows of each picture out of the comparison; the key strip is
compared by default.  PICNEW names the scratch folder, which is kept.  The
file checkpics.allow says what is known, one line each: "none NAME" (no
scene makes the picture and it is not missing; no picture has this line now,
because pics-check makes the pages that the Printer prints again with the
scripts of scenes/, from the list of checkpictable.py --scripts, and compares
them like the others), "known NAME PIXELS reason" and
"colour NAME PIXELS reason" (a difference of shape or of colour that was
looked at and is accepted: a difference that changes is flagged again), "size
NAME WIDTHxHEIGHT reason" (the new picture has this size, which depends on
the machine, and it is accepted without a comparison), "mask NAME X Y WIDTH
HEIGHT reason" (a rectangle that shows something that changes from run to
run, such as the time of day or the path of the program, is left out of the
comparison; the rest of the picture is compared; a picture that is flagged is
listed with the box around the pixels that differ, to write the line) and
"ignore NAME reason" (a whole picture that depends on something that changes,
not compared; say why, and prefer mask).  tests/checkpics.py tests the
program with small pictures of its own (python3 -I -B tests/checkpics.py;
"make test" runs it).
Nothing in doc/pictures is changed by the check: look at every picture that
differs (old and new), replace the ones that are wrong with "build.sh pics"
(or shots.py with the names of the scenes), and change the page text that
goes with them.  Taking all the pictures takes about 16 minutes; shots.py
gives each scene 60 seconds (SHOTS_TIMEOUT), reports a scene that fails and
goes on, and allows one run at a time.

THE TABLE OF THE PICTURES

The file pictures.txt has one line for every picture file of doc/pictures:
the name, what makes it (a scene of shots.py or of scenes/*.py, or a script of
scenes/ that prints a page with the Printer), the program that the scene
starts, the document that it opens, the date of the last retake and the
words that say how to make it again.  The check

  build.sh check-pictable

(checkpictable.py, part of "build.sh check") fails when a picture has no line,
when a line has no picture, when a scene named in the table does not exist or
starts another program, and when a scene has no picture.  After pictures are
retaken, "python3 -I manual/checkpictable.py --write" writes the table again
(the dates come from the history of git).  Seven pictures are pages that the
Printer prints and have no scene: appchar-print (scenes/appchar-pdf.sh),
print-errlog (scenes/errlog-pdf.sh), print-failed-page
(scenes/failed-pdf.sh), print-headfoot-page (scenes/hf-pdf.sh),
print-index-page (scenes/index-pdf.sh), print-lists-page (scenes/lists-pdf.sh)
and print-border-page (scenes/border-pdf.sh); pics-check does not take them
again.  The last four print example documents that scenes/mkpicdocs.py makes;
the scenes that open numbers.doc, border.doc, headfoot.doc or indexlist.doc
(scenes/gaps.py) get them from the same program.

The scenes of Setup start the Document Manager through scenes/neutral-str.sh,
which shows the sample folder as /home/writer/documents and the Editor and the
help file as /usr/local/strata/stred and strata.hlp (a private view of the
file system made by bwrap: nothing is changed), so that no folder of the
machine that takes the pictures is shown.

THE OLDER WAY

The targets "docs" and "pdf" make one document and one PDF for every chapter
and appendix (about twenty-five), as the manual was made before.  They are
kept as options and are not part of "all".

EDITIONS

The sources may hold conditional text between the directives .if EXPR,
.else and .endif (see the header of mkmanual.py).  The directives write the
If, Else and Endif markers of the document, so the document holds all of the
text. The PDF that is shipped (strata-reference-manual.pdf in the PDF folder,
made by "single") is printed with the special variable .STRATA_BUILD_TYPE set
to release, and the text for developers is left out of it; "developer" prints
the same document with debug into the folder developer of the PDF folder, and
"check-edition" (checkedition.py) proves that the text a release leaves out is
not in the release PDF and is in the developer PDF, and that no command line
of a program is in the text of the release PDF (checknames.py --pdf). The text
for developers is the command lines of the Editor, the Printer and the Viewer,
the environment variables that name another program, and the exit status of
the programs: Strata is started with the Document Manager, so the release
edition does not tell how to start the others. It is between ".if
.STRATA_BUILD_TYPE # release" and ".endif" (checknames.py fails when such a
word is in text that the release prints). A headline may stand inside
conditional text when no later headline of the same parent is printed after it
(the numbers of the headlines are part of their text), so the sections for
developers are the last of their chapter or appendix: the last part of
Appendix A, of chapter 6 and of chapter 11. The files .cond and .hidden next
to the documents are made by the generator for the checks of the cards and of
the PDF.

OTHER FILES

  mkmanual.py    The generator of the documents from the sources.
  checkpdf.py    The check of the PDF of the single document.
  checkedition.py  The check of the editions of the PDF files.
  shots.py, xk.py, scenes/   The pictures of the screens (build.sh pics).
  mkchars.py     The tables of the special characters.
  checkcheat.py, KEYS.txt    The check of the key names of the leaflet.
  checkkeys.py, checkkeys.allow   The check that every key is a keycap.
  checkstrata.py, checkstrata.allow   The check that the name is \strata.
  mathsyms.txt               The list of the symbols of the Math Symbols card
                             (src/mathcheat.src is the source of the card:
                             the generator makes the panels from the list).
  mkmathcheat.py             The panels of the card from the list; with
                             --measure it measures every symbol with LaTeX
                             and writes mathsyms.sizes (do that after a symbol
                             is added to the list).
  checkmathsyms.py           The check of the card (build.sh check-math):
                             every entry through the Printer, the symbols of
                             the packages against the list and
                             mathsyms-left-out.txt, and the layout of the
                             printed card (two pages, margins, no overflow).
  samples/       Sample documents made from samples/src.
