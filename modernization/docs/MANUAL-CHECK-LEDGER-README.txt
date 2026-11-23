THE LEDGERS OF THE MANUAL CHECK

This document explains the ledgers that the check of the Reference Manual
wrote.  A ledger is a plain text file that records, for every section of a
part of the manual, whether the section was checked and how.  The check read
the whole Reference Manual against the programs, section by section, in six
parts, and each part has one ledger.

WHAT A LEDGER LINE IS

A ledger line is the entry of one section in a ledger file.  It has these
items, in this order:

  1. The number of the section, as the printed manual shows it (for example
     5.20 or C.3).  Some ledgers also give the source file and the label of
     the section.
  2. The heading of the section.
  3. The state of the section: read.  Every section was read, in full, in its
     source file.  A ledger may add a letter that tells how far the
     comparison with the program went (see the next part).
  4. How the facts of the section were checked: the program file or the table
     that the statements were compared with, the program that was run and
     the test that was made, or the reason why a check was not possible.
  5. What was wrong in the section and what was changed.  The words no change
     needed mean that every statement agreed with the program.

A section that has no line in its ledger was not checked.  The number of lines
of a ledger equals the number of sections of its part: the first page of each
ledger names that number.

THE NOTATION OF THE STATE

Three notations were used, one in each family of ledgers.  They say the same
thing in different words; the first page of each ledger explains its own.

  The letters V, P and T (the ledger of chapters 6 to 8):
    V   The statements of the section were compared with the program: with its
        source, or by running it.  A V section can still hold a statement that
        was not compared; its line names that statement.
    P   Most statements were compared; the line names the ones that were not.
    T   The section was read and the tools that run on every section agree
        with it (the fact checks, the style rules and the spelling check), but
        its statements were not compared one by one with the program.

  The words checked by and run: (the ledger of chapters 4 and 5 and the other
  ledgers that use them):
    checked by:   names the file of the program or the document that the
                  statements were compared with.
    run:          names what was done with the running program or a printed
                  document, and what it showed.
    not run       says that a statement was read for sense and compared with
                  the source of the program, and that its behaviour was not
                  tried; the reason follows.

  The words not testable here:
    A line says not testable here when the behaviour cannot be tried in the
    environment of the project.  The reason is always given and is collected
    in docs/KNOWN-ISSUES.txt, section KI-3: the Linux console, the keys that
    a user's own terminal, window manager or desktop keep for themselves, a
    terminal that reports modified keys, and the close button of the window of
    the X11 version.

THE LEDGER FILES

  docs/MANUAL-CHECK-LEDGER-ch01-02.txt   Chapters 1 and 2: A View of Strata
                                         and File.
  docs/MANUAL-CHECK-LEDGER-ch03.txt      Chapter 3: Edit.
  docs/MANUAL-CHECK-LEDGER-ch04-05.txt   Chapters 4 and 5: Format and Markers.
  docs/MANUAL-CHECK-LEDGER-ch06-08.txt   Chapters 6, 7 and 8: Print and
                                         Preview, The Document Manager, Spell
                                         Check and Thesaurus.
  docs/MANUAL-CHECK-LEDGER-ch09-11.txt   Chapters 9, 10 and 11: Equations,
                                         Conversion and Messages.
  docs/MANUAL-CHECK-LEDGER-apx.txt       Appendices A to E, the Glossary, the
                                         Key Reference and the Math Symbols.
  docs/MANUAL-CHECK-LEDGER.txt           The first ledger, of the second
                                         editorial pass, which checked the
                                         sections that the tools could reach
                                         and read only a few sections in full.
                                         The six ledgers above replace it.

HOW THE LEDGERS ARE USED

The ledgers are the evidence of the check.  To see how a section was checked,
find its number in the ledger of its part.  To see what the check did not
cover, look for the lines that say not run, not testable here or a letter other
than V.  The faults that the check found and left in the programs are listed in
claude-interchange/MANUAL-CHECK-STATUS.txt; those that the project has decided
to keep are in docs/KNOWN-ISSUES.txt.

The checks that keep the manual true after the ledgers were written are the
programs in manual/: checkeditorial.py (the rules E1 to E27 of style and
layout), checkfacts.py (the messages, menu entries, options, markers, field
labels and samples against the programs), checkspell.py (the spelling), and
checkpdf.py and checkedition.py (the printed documents).  build.sh check-style
and build.sh check-edition run them.
