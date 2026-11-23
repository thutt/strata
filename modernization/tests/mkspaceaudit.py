#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Makes docs/SPACE-CHOICE-AUDIT.txt: the fields of the panels, their kind,
and whether the space bar changes them.

    python3 -I -B tests/mkspaceaudit.py > docs/SPACE-CHOICE-AUDIT.txt
    python3 -I -B tests/mkspaceaudit.py --check docs/SPACE-CHOICE-AUDIT.txt

The marker panels and the Global Format panels are tables in the source
(src/marker.c, src/globalmenu.c) and are counted from them; the other panels
are listed here by hand, from the source of each panel.
"""
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(os.path.dirname(here), 'src')

# The kinds of a field of the Global Format panels (FK_*): a field with a fixed
# list of values is a choice, the others are typed.
FK_CHOICE = {'ATTR', 'JUST', 'INDENT', 'FONT', 'YN', 'FIRST', 'PRINT',
             'RULER', 'LNMODE', 'LNSIDE', 'LNSIZE', 'LNFONT', 'LNFMT',
             'LIST'}
FK_TYPED = {'TABS': 'text', 'SPACE': 'number', 'NUM': 'number',
            'TAG': 'text', 'CLEVEL': 'number', 'COUNT': 'info'}
PK_CHOICE = {'PAPER'}
PK_TYPED = {'LEN': 'number', 'PCT': 'number', 'TAG': 'text', 'HF': 'text',
            'INFO': 'info'}

# Panels listed by hand: (panel, field, kind).  kind is choice (a fixed list:
# Space steps it), list (a list built when the panel opens: Space steps it),
# number, text, name (a file, a directory, a person), or info (only shown).
HAND = [
    ('Print, Final / Draft', 'Printer', 'list'),
    ('Print, Final / Draft', 'Print Double Sided?', 'choice'),
    ('Print, Final / Draft', 'Sort Output Pages?', 'choice'),
    ('Print, Final / Draft', 'Print Current Section Only?', 'choice'),
    ('Print, Final / Draft', 'Print Title Page?', 'choice'),
    ('Print, Final / Draft', 'Print Double Spaced?', 'choice'),
    ('Print, Final / Draft', 'Error Log?', 'choice'),
    ('Print, Final / Draft', 'Table of Contents?', 'choice'),
    ('Print, Final / Draft', 'Index?', 'choice'),
    ('Print, Final / Draft', 'List of Figures?', 'choice'),
    ('Print, Final / Draft', 'List of Pictures?', 'choice'),
    ('Print, Final / Draft', 'List of Tables?', 'choice'),
    ('Print, Final / Draft', 'Print in Black and White?', 'choice'),
    ('Print, Final / Draft', 'Output File', 'name'),
    ('Print, Final / Draft', 'Copies', 'number'),
    ('Print, Final / Draft', 'Contents Levels', 'number'),
    ('Print, Final / Draft', 'Print from Section', 'text'),
    ('Print, Final / Draft', 'to Section', 'text'),
    ('Print, Final / Draft', 'Print from Page', 'number'),
    ('Print, Final / Draft', 'to Page', 'number'),
    ('Print, Final / Draft', 'Print Status', 'info'),
    ('Print, Preview', 'Preview Title Page?', 'choice'),
    ('Print, Preview', 'Error Log?', 'choice'),
    ('Print, Preview', 'Preview from Section', 'text'),
    ('Print, Preview', 'to Section', 'text'),
    ('Search', 'Find', 'choice'),
    ('Search', 'Direction', 'choice'),
    ('Search', 'Match Case', 'choice'),
    ('Search', 'Whole Word', 'choice'),
    ('Search', 'Attribute', 'choice'),
    ('Search', 'Search for', 'text'),
    ('Replace', 'Find', 'choice'),
    ('Replace', 'Match Case', 'choice'),
    ('Replace', 'Whole Word', 'choice'),
    ('Replace', 'Attribute', 'choice'),
    ('Replace', 'Replacement Case', 'choice'),
    ('Replace', 'Ask at Each', 'choice'),
    ('Replace', 'Search for', 'text'),
    ('Replace', 'Replace with', 'text'),
    ('Border', 'Top, Bottom, Left, Right, Inside (5 fields)', 'choice'),
    ('Border', 'Connect Borders?', 'choice'),
    ('Border', 'Top Gutter, Bottom Gutter', 'number'),
    ('Border', 'Shading (%)', 'number'),
    ('Save / Save As', 'Options', 'choice'),
    ('Save / Save As', 'Working Directory, Document Name', 'name'),
    ('Save / Save As', 'Description, Author, Revision', 'text'),
    ('New / Get / Import / Export', 'Options, Character Set, Scope, Check '
     'Validity (4 fields, 2 more in the Import panel)', 'choice'),
    ('New / Get / Import / Export', 'Working Directory, File Name, '
     'Template', 'name'),
    ('New / Get / Import / Export', 'Description, Author, Revision', 'text'),
    ('Macros', 'Include in Macro Set?, Options (retrieve)', 'choice'),
    ('Macros', 'Macro Key: Alt-, Description, Keystrokes, Author, '
     'Revision', 'text'),
    ('Macros', 'Working Directory, File Name', 'name'),
    ('Library', 'Include in Library?, Options (retrieve)', 'choice'),
    ('Library', 'Description, Author, Revision', 'text'),
    ('Library', 'Name of an entry (a list of the entries, a new name may be '
     'typed)', 'name'),
    ('Spelling', 'Auto Correct?', 'choice'),
    ('Spelling', 'Document Name, Custom Dictionary, Dictionary Language',
     'info'),
    ('Compare / Dictionaries / Restore', 'Working Directory, the document '
     'and file names', 'name'),
    ('Global, Fonts', 'Font 1 to Font 20 (a name of a typeface)', 'name'),
    ('Global, Options', 'Block Indicators and the five other rows', 'info'),
    ('Global, Options', 'Date Format, Keycap Foreground, Background, Scale, '
     'C1 to C9 Description', 'text'),
    ('Global, Columns', 'Number of Columns, Gutter Size', 'number'),
    ('Document Manager, Setup', 'Autosave', 'choice'),
    ('Document Manager, Setup', 'Print To, Printer Queue, Paper for New '
     'Documents, Terminal Characters', 'list'),
    ('Document Manager, Setup', 'Backups Kept, Autosave Interval, Undo '
     'Levels, Window Rows', 'number'),
    ('Document Manager, Setup', 'Dictionary Language (a code such as '
     'en_US)', 'text'),
    ('Document Manager, Setup', 'Working Directory, PDF Directory, PDF '
     'Viewer, Macro Set, Library, Editor program, Help File', 'name'),
    ('Document Manager, TeX Font Association', 'Family', 'choice'),
    ('Document Manager, TeX Font Association', 'Install Now', 'choice'),
    ('Document Manager, TeX Font Association', 'Name, LaTeX Package, Package '
     'Options, CTAN Archive, Map File', 'text'),
    ('Document Manager, TeX Font Association', 'Font Number, Installed',
     'info'),
    ('Document Manager, New / Template', 'Document Name, Working Directory, '
     'Template, Description, Author, Variables', 'name'),
]

# Fields that do not take Space as the next value, with the reason.
EXCLUDED = [
    ('text, number and name fields', 'Space is a character that can be '
     'typed.'),
    ('Font 1 to Font 20 (Global, Fonts)', 'A name of a typeface: the list '
     'of known ones is a convenience and a user may type another name.'),
    ('Library entry name', 'A new entry may be named: the list is of the '
     'entries that exist.'),
    ('Dictionary Language (Setup)', 'A code such as en_US; the dictionaries '
     'that are installed are not listed by the program.'),
    ('Date Format, Keycap values', 'A pattern or a number, not a list.'),
    ('info fields', 'Only shown: no cursor.'),
]


def marker_fields():
    """(count by kind, number of marker panels) from src/marker.c."""
    text = open(os.path.join(src, 'marker.c')).read()
    kinds = re.findall(r'^    \{"[^"]*", "[^"]*", (MF_[A-Z]+),', text, re.M)
    panels = len(re.findall(r'^static const MField f_[a-z_0-9]+\[\]', text,
                            re.M))
    out = {}
    for k in kinds:
        out[k] = out.get(k, 0) + 1
    return out, panels


def global_fields():
    text = open(os.path.join(src, 'globalmenu.c')).read()
    fk = re.findall(r'^    \{"[^"]*", "[^"]*", FK_([A-Z]+),', text, re.M)
    pk = re.findall(r'^    \{"[^"]*", "[^"]*", PK_([A-Z]+),', text, re.M)
    c = {'choice': 0, 'number': 0, 'text': 0, 'info': 0}
    for k in fk:
        if k in FK_CHOICE:
            c['choice'] += 1
        else:
            c[FK_TYPED[k]] += 1
    for k in pk:
        if k in PK_CHOICE:
            c['choice'] += 1
        else:
            c[PK_TYPED[k]] += 1
    return c, len(fk), len(pk)


def table():
    out = []
    mk, mpanels = marker_fields()
    gc, nfk, npk = global_fields()
    out.append('SPACE-CHOICE-AUDIT: THE SPACE BAR IN EVERY FIELD')
    out.append('=' * 48)
    out.append('')
    out.append('Rule: a field that has a bounded and known set of values (a')
    out.append('choice: Yes or No, a list, the Printer, the family of a font)')
    out.append('is changed by the space bar: Space takes the next value and')
    out.append('goes round from the last to the first.  Left, Right, F2 and')
    out.append('the plus keep working.  A text, number or name field takes the')
    out.append('space as a character.  A list that is built when the panel')
    out.append('opens (the print queues) is stepped through as far as it is')
    out.append('there; the entries that come later are stepped through when')
    out.append('they have come.')
    out.append('')
    out.append('How it is done: all choice fields of the Editor are one kind')
    out.append('(WF_CHOICE in src/widgets.c) and take Space in the widget; the')
    out.append('panels of the Document Manager (src/str.c) take it in their')
    out.append('choice_key.  A field that is given a list gets Space with no')
    out.append('code of its own.  Before this audit two fields of the')
    out.append('Document Manager had a fixed set of values and no list, so they')
    out.append('were typed: Install Now and Family of TeX Font Association.')
    out.append('They are choices now.  No other field was found.')
    out.append('')
    out.append('COUNTS (made from the tables of the source by this program)')
    out.append('-' * 59)
    out.append('Marker panels (src/marker.c), %d panels:' % mpanels)
    out.append('  choice %d (Space steps them), number %d, text %d' % (
        mk.get('MF_CHOICE', 0), mk.get('MF_NUM', 0), mk.get('MF_TEXT', 0)))
    out.append('Global Format panels (src/globalmenu.c), one definition of')
    out.append('each field (the panel of a level repeats the definitions):')
    out.append('  %d format fields and %d page-layout fields: choice %d, '
               'number %d,' % (nfk, npk, gc['choice'], gc['number']))
    out.append('  text %d, info %d' % (gc['text'], gc['info']))
    hand = {}
    for p, f, k in HAND:
        hand[k] = hand.get(k, 0) + 1
    out.append('Other panels (the table below, one row may name several')
    out.append('fields): %d rows: choice %d, list %d, number %d, text %d, '
               'name %d, info %d' % (
                   len(HAND), hand.get('choice', 0), hand.get('list', 0),
                   hand.get('number', 0), hand.get('text', 0),
                   hand.get('name', 0), hand.get('info', 0)))
    out.append('')
    out.append('Space already changed every field of kind choice or list')
    out.append('above except the two of TeX Font Association, which are the')
    out.append('only fields that changed with this audit.')
    out.append('')
    out.append('THE TABLE OF THE OTHER PANELS')
    out.append('-' * 59)
    out.append('%-26s %-8s %s' % ('Panel', 'Kind', 'Field(s)'))
    for p, f, k in HAND:
        chg = ' (new)' if f in ('Family', 'Install Now') else ''
        line = '%-26s %-8s %s%s' % (p[:26], k, f, chg)
        if len(line) <= 79:
            out.append(line)
        else:
            out.append('%-26s %-8s' % (p[:26], k))
            words = (f + chg).split()
            cur = '    '
            for w in words:
                if len(cur) + len(w) + 1 > 79:
                    out.append(cur.rstrip())
                    cur = '    '
                cur += w + ' '
            out.append(cur.rstrip())
    out.append('')
    out.append('Does Space change it?  Yes for every row of kind choice and')
    out.append('list; no for number, text, name (the space is typed) and info')
    out.append('(no cursor).')
    out.append('')
    out.append('FIELDS THAT DO NOT TAKE SPACE AS THE NEXT VALUE, AND WHY')
    out.append('-' * 59)
    for what, why in EXCLUDED:
        line = '%s: %s' % (what, why)
        words = line.split()
        cur = ''
        for w in words:
            if len(cur) + len(w) + 1 > 78:
                out.append(cur.rstrip())
                cur = '    '
            cur += w + ' '
        out.append(cur.rstrip())
    out.append('')
    out.append('TESTS')
    out.append('-' * 59)
    out.append('tests/spacechoice.py drives the Document Manager and the Editor')
    out.append('(Family and Install Now, Autosave, Print Double Sided?, Error')
    out.append('Log?, the Printer, a typed space in Copies and in a text field)')
    out.append('and fails when the handling of Space is off or does not go')
    out.append('round.  tests/xspacechoice.py does the same in the X11 Editor.')
    return '\n'.join(out) + '\n'


def main(argv):
    t = table()
    if len(argv) == 3 and argv[1] == '--check':
        return 0 if open(argv[2]).read() == t else 1
    sys.stdout.write(t)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
