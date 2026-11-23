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

These are small hand-made .DOC files used as regression tests.  The
"make test" target checks that each one loads and is written back
unchanged.

column_record.doc
    A block whose columns are separated by an "SE2" record.  An older
    version of the loader moved that record to the next block, so the
    file was not written back unchanged.  The pattern comes from a
    larger real document.

table_rows.doc
    A table of three rows with two columns each.  Every row begins with
    a "BM15" block property record.  An older version of the loader
    joined all the rows into one block of six columns.  The original
    program draws them as separate rows.

many_columns.doc
    One block with 50 columns, each one cell wide.  The original program
    loads and displays such a table, clipping it at the right edge of
    the screen.  The editor used to stop at 12 columns.

symbols.doc
    Symbol markers (type 32).  The first block holds a copyright sign
    (code 440) between "Copyright" and "1994".  The second holds a
    literal hyphen between "low" and "level" and an unknown symbol code
    (999), which stays a bracketed marker.

markers.doc
    Written by the original program (Manuscript 2.1 in DOSBox-X).  It holds
    one of every marker command of the Marker Commands menu that works in a
    body of an unstructured document: 43 markers of 36 types, each created
    with distinctive values.  Cross-reference needs a structured document,
    and Page Number and Section only work in a header or a footer, so these
    three are not in it.  The file is also in sample.docs/.

two_columns.doc
    Two blocks of two columns.  The first column of each holds one short
    line and the second holds a paragraph that wraps onto several rows.  In
    the original the Down key in the first column goes to the first column
    of the next block.  An older version of the editor stopped, because it
    tried to move onto the empty rows of the first column.

sections_se.doc
    Sections written the newer way: each starts with an SE<n> record, which
    is a section start that also holds the fold state (0 expanded, 2
    contracted), instead of a plain S record.  Here "Prerequisites" is
    contracted when the file is opened.  An older version of the loader kept
    the SE record as a block property, so the sections were not numbered,
    the status line showed "Sect: -" and the outline keys did nothing.

note_complex.doc
    A note whose text is two blocks, the second with its own justification
    (BFJ2).  The note editor must show both blocks, and editing one must
    keep the other and its formatting.

blank.set
    A template file: the settings and an empty document.  It is the file
    the template lookup of the New command finds, ignoring the case of the
    name, as DOS would.

sort_sections.doc, sort_ties.doc, sort_table.doc
    Documents for Tools, Autosort.  sort_sections.doc has a section "Parent"
    with seven unsorted subsections (one with subsections of its own, which
    stay as they are).  The original sorted them as 10 x, 9 x, apple, Apple,
    Apple pie, banana, cherry.  sort_ties.doc has subsections that differ
    only in case or in punctuation; the original puts them in one order
    whatever order they are in: A-b, a_b, "a b", ab, bob, bOb, Bob, BOB.
    sort_table.doc has four blocks of two columns; Autosort, Table sorts
    them by the first column: Apple, banana, Cherry, pear.

title_page.doc, plain_text.doc
    title_page.doc is structured and has a title page: the record "=1",
    then a section with the blocks "Front page" and "By the author", then
    the structure.  The original keeps the title page out of the text and
    edits it from Edit, Title.  plain_text.doc is unstructured with two
    blocks; Edit, Structured turns it into one section.

equation.doc
    An unstructured document whose only block is an Equation marker with
    the text "y = 10 + e^x", written as TeX.  It is alone in its block, so
    it is a displayed equation: the X11 version typesets it to a picture.

equation_inline.doc
    Equations in the middle of a line (x^2+y^2, a fraction, e^{i pi}+1=0 and
    an integral), one that is alone in its block, and a line that ends with a
    sum that does not fit in the rest of the line: the picture starts the next
    line and the text goes on after it.  The X11 version draws all of them as
    pictures, with the baseline of an inline picture on the baseline of the
    text.

equation_lines.doc
    An equation of two lines ("a = 1 \\" and "b = 2", the lines ended by the
    end-of-line byte).  Its label must show both on one line.  An older
    version of the label code gave up on any text with an end of line and
    drew "[EQN ]", so the text of the equation was not shown when pictures
    were turned off.

print_orient.doc, print_whitespace.doc, print_keycap.doc, print_chars.doc
    Made by tests/mkprintdata.py for strprint (the pdflatex print).
    print_orient.doc has portrait and landscape pages (Orientation
    markers, one of them in the middle of a block, and a landscape page
    that is open at the end); print_whitespace.doc a Whitespace of every
    unit (field U); print_keycap.doc a Keycap in the fonts 1 to 9 and 20
    (field 5) with some DF names; print_chars.doc the characters of CP437
    including some that no font has (they are printed as [U+XXXX]).
    tests/lxtest.c checks the text that is written for each of them.

print_features.doc
    Made by hand for strprint (the print program, docs/PRINT-LATEX.txt).
    A structured document with a title page and a picture marker that
    points at picture.png in this directory, and with a footnote, a
    levelnote and an endnote, counters, keycaps, a good equation, an
    equation that is not valid TeX and one that is not balanced, tab
    stops with a dot fill, a text indent, symbols (one of them unknown),
    a line, whitespace and a page break, markers that are printed in the
    long form (Include, Keep, Index, Orientation), a table with borders
    and three cross-references (tag, headline and page).  "strata-build
    printtest" prints it with the other documents when it is given as
    the directory of documents: PRINTDOCS=tests/data.

spell.doc
    Made by hand (the header is that of note_complex.doc) for the tests of
    the spell check.  Four blocks: a block of two lines with the misspelt
    words quikc, jumpd, teh, dont-stop and wrold; a block with a word in
    which an attribute changes (x, then qz in bold, then t), a tab, and the
    words Strata3, 3rd and x2y; a block with a note (footnote) that holds
    the text "A foootnote here." between "Befor" and "Aftr zzq."; and a
    block of words with punctuation (qqa.qqb, qqc-qqd, 'qqe', qqf's, qqg.,
    qqh,qqi, (qql), qqs-, -qqt) followed by teh and zzq again.  The fake
    checker of the tests (tests/fakespell.sh) knows teh, quikc, wrold, zzq,
    dont and foootnote as wrong words.

th_test.idx and th_test.dat
    A small thesaurus in the MyThes format (the format of the files
    th_en_US_v2.idx and .dat of the package mythes-en-us), made up for the
    tests of the thesaurus; none of the real data is in it.  They are made by
    tests/mkthesdata.py (python3 -I tests/mkthesdata.py tests/data); the
    offsets of the index are computed.  The 30 headwords are: abbey, abbot,
    abdicate, abduct, abet, abide (the words next to "abcd" for the list of
    similar words), area, center, core, fast, happy (an antonym, "sad"), 
    headline (the stem of "headlines"), heart, next day and passage and
    piece of writing (headwords of several words), quick (a noun with a
    related term, an adjective with synonyms, similar terms, an antonym and a
    generic term, an adverb, and a sense that has nothing to show), rapid,
    run (the stem of "running"), section (three senses, a synonym of several
    words; the stem of "sections"), sector, set, slow, speedy, swift, zeal,
    zebra, zone, zoom and zygote (the end of the alphabet).  The words
    "ran" and "zzzqx" are not in it.  The tests (tests/edtest.c and
    tests/smoke.py) name the directory with th_use and STRATA_THESAURUS.

keep_fixed.doc, keep_text.doc, keep_float.doc, keep_floatend.doc,
keep_unmatched.doc, keep_stray.doc, keep_nested.doc, keep_long.doc,
keep_table.doc, keep_sections.doc and keep_notes.doc
    Made by tests/mkkeepdata.py (python3 -I tests/mkkeepdata.py tests/data)
    for the tests of the Keep marker in the printing path (tests/keep.py,
    tests/lxtest.c and tests/edtest.c).  Every line of text is a label such
    as KA03, so that a test can say on which page a line was typeset.  Each
    document has a filler of 34 lines (L01 to L34) near the end of a page,
    so that the region after it does not fit on page 1: keep_fixed (a pair of
    markers in blocks of their own), keep_text (the markers in the text of
    blocks), keep_float (Start (Floating): the text after the End moves up),
    keep_floatend (a floating region at the end of the document),
    keep_unmatched (a Start without an End), keep_stray (an End without a
    Start), keep_nested (two Starts, two Ends), keep_long (a region of 80
    lines), keep_table (the Start and the End in columns of different rows
    of a table), keep_sections (a pair across a section boundary) and
    keep_notes (a footnote in a region).  The control of a test is the same
    document with the markers taken out.

secnum_deep.doc, secnum_fln.doc, secnum_fla0.doc, secnum_forms.doc,
secnum_jump.doc, secnum_contract.doc, secnum_start2.doc, secnum_title.doc,
secnum_heads.doc, secnum_chars.doc, secnum_xref.doc
    Structured documents for the numbers of the sections (tests/mksecdata.py
    writes them).  The deep one has 26 levels; the FLN one numbers only to
    level 3; the FLA0 one has no tags; the forms one has the tag formats of
    levels 1 to 5 (Part I., A., (a), i. and a bullet) and the plain numbers
    below them; the jump one has a level 3 under a level 1 section; the
    contract one has contracted sections; the start2 one starts at level 2;
    the title one has a title page; the heads one has headlines with bold
    text, no text, two lines, a Keycap, a note and an equation; the chars
    one has tags with characters that TeX treats in a special way and with
    characters of the code page; the xref one has cross-references that
    print the tags.  Every headline is one word, such as Hd007x, that occurs
    nowhere else, so that tests/secnum.py finds it in the text of the PDF.

custom.hyc
    A custom hyphenation dictionary of the original, made by the original
    from a document of six words with soft hyphens (ab-cd, a long word,
    num-ber1-23, pre-fix-ed, x-y and zor-ba-flux; docs/DICTIONARIES.txt,
    section 3).  tests/hyctest.py reads it with tools/hyc.py.
