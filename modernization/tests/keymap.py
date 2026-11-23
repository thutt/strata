#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# The complete key map of the programs, and the F12 menu routes.
#
# The map is made from the SOURCE, not from the manual: the table of function
# keys (fkeys[] of src/ui.c), the switches of handle_key_inner that read the
# control keys, the Alt keys and the cursor keys, and the menu tree that the
# Editor prints with --dump-menus.  What the source does not say (the words
# for a key, the bytes a terminal sends) is in the tables of this file, and
# the check below fails when the source and the tables differ: a key that the
# source reads and that the tables do not name, or the reverse, so that the
# document cannot go stale.
#
#   python3 -I -B tests/keymap.py --text      print the document
#   python3 -I -B tests/keymap.py --write F   write the document to F
#   python3 -I -B tests/keymap.py --check     compare source and tables
#
# STRED names the Editor program (the menu tree comes from it); without it
# the parts that need the program are left out and --check says so.
import os
import re
import subprocess
import sys
import textwrap

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
# KEYMAP_SRC names another folder of sources: the test of this file uses it
# to show that a changed source is found.
SRC = os.environ.get('KEYMAP_SRC',
                     os.path.join(os.path.dirname(HERE), 'src'))


def read_source(name):
    """The text of a source file of src/."""
    with open(os.path.join(SRC, name), encoding='utf-8') as f:
        return f.read()


# ------------------------------------------------------------------ source
def parse_fkeys(ui):
    """The rows of fkeys[]: (command name, modifier 0/1/2, number)."""
    a = ui.index('static const FKey fkeys[] = {')
    b = ui.index('};', a)
    return [(m.group(1), int(m.group(2)), int(m.group(3)))
            for m in re.finditer(r'\{"([^"]+)",\s*(\d),\s*(\d+),\s*\w+\}',
                                 ui[a:b + 2])]


def parse_ctrl(ui):
    """The control key codes that handle_key_inner reads."""
    a = ui.index('/* control keys (plain ASCII 1..26) */')
    b = ui.index('if (k >= 32 && k <= 255)', a)
    return sorted(int(m.group(1))
                  for m in re.finditer(r'case (\d+):', ui[a:b]))


def parse_alt(ui):
    """The characters of the Alt switch, and whether Alt-BkSp is there."""
    a = ui.index('/* alt keys */')
    b = ui.index('/* function keys */', a)
    chars = sorted(set(m.group(1)
                       for m in re.finditer(r"case '(.)':", ui[a:b])))
    digits = ("base >= '1' && base <= '9'" in ui[a:b] and
              "base == '0'" in ui[a:b])

    return chars, digits, 'case K_BKSP:' in ui[a:b]


def parse_moves(ui):
    """The cursor keys of the movement switch: key -> its MV_ names."""
    a = ui.index('/* movement */')
    b = ui.index('if (mv >= 0)', a)
    out = {}
    key = None

    for line in ui[a:b].splitlines():
        m = re.match(r'\s*case (K_[A-Z]+):', line)

        if m:
            key = m.group(1)
            out[key] = []

        if key is not None:
            out[key] += re.findall(r'MV_[A-Z_]+', line)

    return out


def find_stred():
    """The Editor program, or None."""
    s = os.environ.get('STRED')

    if s and os.path.exists(s):
        return s

    b = os.environ.get('STRATA_BUILD_DIR')

    if b and os.path.exists(os.path.join(b, 'term', 'stred')):
        return os.path.join(b, 'term', 'stred')

    return None


def menu_tree(stred):
    """The menu levels the Editor prints: a dict path -> list of names."""
    out = subprocess.run([stred, '--dump-menus'], capture_output=True,
                         text=True, timeout=30).stdout
    levels = {}

    for line in out.splitlines():
        p = line.split('\t')

        if len(p) == 3 and p[0] == 'menu':
            levels.setdefault(p[1], [])

            for n in p[2].split('|'):
                if n not in levels[p[1]]:
                    levels[p[1]].append(n)

    return levels


def menu_leaves(levels):
    """Every entry that opens nothing: its path with colons."""
    leaves = []

    def walk(path):
        for n in levels.get(path, []):
            sub = n if path == '' else path + ':' + n

            if sub in levels:
                walk(sub)
            else:
                leaves.append(sub)

    walk('')

    return leaves


# ------------------------------------------------------------------ tables
# The words for the commands of fkeys[]: at most 33 characters.
COMMANDS = {
    'Help': 'Help (the screen or the field)',
    'Split': 'Split column, block or section',
    'Copy': 'Copy text/column/block/section',
    'Cut': 'Cut text/column/block/section',
    'Paste': 'Paste',
    'Format': 'Block or column format',
    'Move': 'Move text/column/block/section',
    'Table': 'Add or split a column',
    'Search': 'Search panel',
    'Menu': 'The F12 menu bar',
    'Learn': 'Record a macro (ends it too)',
    'Math': 'Calculate in a column or table',
    'Lib Copy': 'Copy to the library',
    'Lib Cut': 'Cut to the library',
    'Lib Paste': 'Paste from the library',
    'Case': 'Change case of highlighted text',
    'Prev Error': 'Previous print error',
    'Next Error': 'Next print error',
    'Search Again': 'Search again',
    'Undo': 'Undo',
    'Redo': 'Redo',
    'Marker': 'Marker menu / edit marker here',
    'Append': 'Append column/block/section',
    'Join': 'Join columns/blocks/sections',
    'Delete': 'Delete highlighted area',
    'Overtype': 'Overtype on and off',
    'Set Attribute': 'Attribute menu',
    'Go To': 'Go to a section or a page',
    'Border': 'Border panel',
    'Replace': 'Replace panel',
}

# The group of each command, for the view by command group.
GROUP_OF = {
    'Help': 'Menus, help and files', 'Menu': 'Menus, help and files',
    'Split': 'Blocks, columns and sections',
    'Table': 'Blocks, columns and sections',
    'Append': 'Blocks, columns and sections',
    'Join': 'Blocks, columns and sections',
    'Go To': 'Movement',
    'Copy': 'Highlight, copy, cut and paste',
    'Cut': 'Highlight, copy, cut and paste',
    'Paste': 'Highlight, copy, cut and paste',
    'Move': 'Highlight, copy, cut and paste',
    'Delete': 'Highlight, copy, cut and paste',
    'Lib Copy': 'Highlight, copy, cut and paste',
    'Lib Cut': 'Highlight, copy, cut and paste',
    'Lib Paste': 'Highlight, copy, cut and paste',
    'Format': 'Format and attributes',
    'Set Attribute': 'Format and attributes',
    'Case': 'Format and attributes',
    'Border': 'Format and attributes',
    'Overtype': 'Editing',
    'Undo': 'Editing', 'Redo': 'Editing', 'Math': 'Editing',
    'Search': 'Search and replace',
    'Search Again': 'Search and replace',
    'Replace': 'Search and replace',
    'Prev Error': 'Search and replace',
    'Next Error': 'Search and replace',
    'Marker': 'Markers',
    'Learn': 'Tools',
}

# The control keys of handle_key_inner: code -> (key, words, mode, group).
CTRL = {
    1: ('Ctrl-A', 'Append block, same local format', 'B',
        'Blocks, columns and sections'),
    2: ('Ctrl-B', 'Bold on and off', 'B', 'Format and attributes'),
    3: ('Ctrl-C', 'Spell check the current word', 'B', 'Tools'),
    4: ('Ctrl-D', 'Delete the next word', 'B', 'Editing'),
    5: ('Ctrl-E', 'Block or column format', 'B', 'Format and attributes'),
    6: ('Ctrl-F', 'The Ctrl-F list of commands', 'B',
        'Menus, help and files'),
    7: ('Ctrl-G', 'Text indent', 'B', 'Format and attributes'),
    8: ('Ctrl-H', 'Hanging indent', 'B', 'Format and attributes'),
    9: ('Ctrl-I, Tab', 'Insert a tab', 'B', 'Editing'),
    10: ('Ctrl-J', 'End of line (as Enter)', 'B', 'Editing'),
    11: ('Ctrl-K', 'Cut block', 'B', 'Highlight, copy, cut and paste'),
    12: ('Ctrl-L', 'Cut section', 'S', 'Highlight, copy, cut and paste'),
    13: ('Enter, Ctrl-M', 'End of line; accept a highlight', 'B', 'Editing'),
    14: ('Ctrl-N', 'Normal text (attributes off)', 'B',
         'Format and attributes'),
    15: ('Ctrl-O', 'Append block, global format', 'B',
         'Blocks, columns and sections'),
    16: ('Ctrl-P', 'Print, Final', 'B', 'Menus, help and files'),
    17: ('Ctrl-Q', 'Quit the Editor', 'B', 'Menus, help and files'),
    18: ('Ctrl-R', 'Retrieve', 'B', 'Menus, help and files'),
    19: ('Ctrl-S', 'Save', 'B', 'Menus, help and files'),
    20: ('Ctrl-T', 'Thesaurus, current word', 'B', 'Tools'),
    21: ('Ctrl-U', 'Underscore on and off', 'B', 'Format and attributes'),
    22: ('Ctrl-V', 'Split the block', 'B', 'Blocks, columns and sections'),
    23: ('Ctrl-W', 'Equation view (EQUATION EDIT)', 'B', 'Tools'),
    24: ('Ctrl-X', 'Cut text (highlight first)', 'B',
         'Highlight, copy, cut and paste'),
    25: ('Ctrl-Y', 'Split the section', 'S', 'Blocks, columns and sections'),
    26: ('Ctrl-Z', 'Undo (an extra key)', 'B', 'Editing'),
    30: ('Ctrl-^', 'Edit Section / Edit Document', 'S', 'Tools'),
    31: ('Ctrl-/, Ctrl-_', 'Undo (also Ctrl-Hyphen)', 'B', 'Editing'),
}

# The Alt keys of the Alt switch: character -> (key, words, mode, group).
ALT = {
    '/': ('Alt-/', 'Redo', 'B', 'Editing'),
    '_': ('Alt-_', 'Soft hyphen', 'B', 'Editing'),
    '-': ('Alt--', 'Collapse the section', 'S', 'Movement'),
    '+': ('Alt-+, Alt-=', 'Expand the section', 'S', 'Movement'),
    '=': None,
    '*': ('Alt-*', "Outline the section's text", 'S', 'Movement'),
    '[': ('Alt-[, Alt-<, Alt-,', 'Promote the section', 'S',
          'Blocks, columns and sections'),
    '<': None,
    ',': None,
    ']': ('Alt-], Alt->, Alt-.', 'Demote the section', 'S',
          'Blocks, columns and sections'),
    '>': None,
    '.': None,
    'i': ('Alt-I', 'Italics on and off', 'B', 'Format and attributes'),
    'I': None,
    'o': ('Alt-O', 'Overtype on and off', 'B', 'Editing'),
    'O': None,
}

# Alt-digit and Alt-Backspace, which the parser finds as conditions.
ALT_DIGITS = ('Alt-1 .. Alt-9', 'Show headlines to that level', 'S',
              'Movement')
ALT_ZERO = ('Alt-0', 'Outline the whole document', 'S', 'Movement')
ALT_BKSP = ('Alt-Backspace', 'Delete the previous word', 'B', 'Editing')

# The cursor keys: the MV name of the source -> (key, words, mode).
MOVES = {
    'MV_LEFT': ('Left', 'Character left', 'B'),
    'MV_WORD_LEFT': ('Ctrl-Left', 'Word left', 'B'),
    'MV_COL_LEFT': ('Shift-Left', 'Column left', 'B'),
    'MV_RIGHT': ('Right', 'Character right', 'B'),
    'MV_WORD_RIGHT': ('Ctrl-Right', 'Word right', 'B'),
    'MV_COL_RIGHT': ('Shift-Right', 'Column right', 'B'),
    'MV_UP': ('Up', 'Line up', 'B'),
    'MV_SECT_HEAD': ('Shift-Up', "Section's headline", 'S'),
    'MV_DOWN': ('Down', 'Line down', 'B'),
    'MV_SECT_NEXT_HEAD': ('Shift-Down', "Next section's headline", 'S'),
    'MV_HOME': ('Home', 'Start of the line', 'B'),
    'MV_DOC_START': ('Shift-Home', 'Start of the document', 'B'),
    'MV_BLOCK_START': ('Ctrl-Home', 'Start of the block or column', 'B'),
    'MV_END': ('End', 'End of the line', 'B'),
    'MV_DOC_END': ('Shift-End', 'End of the document', 'B'),
    'MV_BLOCK_END': ('Ctrl-End', 'End of the block or column', 'B'),
    'MV_PGUP': ('PgUp', 'About half a screen up', 'B'),
    'MV_SECT_PREV_SIB': ('Shift-PgUp', 'Previous section, same level', 'S'),
    'MV_PGDN': ('PgDn', 'About half a screen down', 'B'),
    'MV_SECT_NEXT_SIB': ('Shift-PgDn', 'Next section, same level', 'S'),
}

MOVE_BYTES = {
    'Left': 'D', 'Right': 'C', 'Up': 'A', 'Down': 'B', 'Home': 'H',
    'End': 'F', 'PgUp': '5~', 'PgDn': '6~',
}


def move_bytes(base, mod):
    """The xterm bytes of a cursor key, with the modifier digit."""
    last = MOVE_BYTES[base]

    if last.endswith('~'):
        return 'ESC[%s%s~' % (last[:-1], ';' + mod if mod else '')

    return 'ESC[%s%s' % ('1;' + mod if mod else '', last)

# Keys that are in the source in places other than the three switches:
# (key, words, mode, group, bytes, a text that must be in src/ui.c).
OTHER = [
    ('Ins', 'Append a section, same level', 'B',
     'Blocks, columns and sections', 'ESC[2~', 'ed_append_section(ED, 0)'),
    ('Shift-Ins', 'Append a subsection', 'B',
     'Blocks, columns and sections', 'ESC[2;2~',
     'ed_append_section(ED, 1)'),
    ('Del', 'Delete the character', 'B', 'Editing', 'ESC[3~',
     'base == K_DEL'),
    ('Backspace', 'Delete the character before', 'B', 'Editing',
     '7F (some send 08)', 'base == K_BKSP'),
    ('Esc', 'Cancel a highlight', 'B', 'Highlight, copy, cut and paste',
     '1B alone', 'K_ESC'),
    ('Ctrl-Shift-Space', 'No-break space', 'B', 'Editing',
     'ESC[32;6u or 00', 'K_NBSP'),
    ('Keypad + - *', 'As Alt-+, Alt--, Alt-* (X11)', 'S', 'Movement',
     'X11 only', 'ed_fold'),
]

# The words for the entries of the menu tree that have a key.
LEAF_KEYS = {
    'File:Save': 'Ctrl-S', 'File:Retrieve': 'Ctrl-R',
    'File:New': None, 'File:Delete': None, 'File:Import': None,
    'File:Export': None,
    'Global:Format': None, 'Global:Page': None, 'Global:Typeface': None,
    'Global:Marker': None, 'Global:Options': None,
    'Global:Named:Edit': None, 'Global:Named:Delete': None,
    'Edit:Title': None, 'Edit:Structured': None, 'Edit:Unstructured': None,
    'Edit:Section': 'Ctrl-^', 'Edit:Document': 'Ctrl-^',
    'Edit:Window': None,
    'Print:Draft': None, 'Print:Final': 'Ctrl-P',
    'Print:Preview:Document': None, 'Print:Preview:Block': None,
    'Print:Preview:Section': None,
    'Print:Next Error': 'Ctrl-F8', 'Print:Previous Error': 'Ctrl-F7',
    'Print:Remove Error': None, 'Print:Remove Print Errors': None,
    'Tools:Macros:Edit': None, 'Tools:Macros:Delete': None,
    'Tools:Macros:Learn': 'Ctrl-F1', 'Tools:Macros:Save': None,
    'Tools:Macros:Retrieve': None,
    'Tools:Library:Edit': None, 'Tools:Library:Delete': None,
    'Tools:Library:Save': None, 'Tools:Library:Retrieve': None,
    'Tools:Spell:Document': None, 'Tools:Spell:Block': None,
    'Tools:Spell:Section': None, 'Tools:Spell:Word': 'Ctrl-C',
    'Tools:Thesaurus': 'Ctrl-T',
    'Tools:Autosort:Section': None, 'Tools:Autosort:Table': None,
    'Tools:Count': None, 'Tools:Undo': 'Ctrl-F10', 'Tools:Redo': 'Ctrl-F11',
    'Quit': 'Ctrl-Q',
}

# The commands of fkeys[] that already have a menu entry.
MENU_OF_COMMAND = {
    'Learn': 'Tools:Macros:Learn',
    'Prev Error': 'Print:Previous Error',
    'Next Error': 'Print:Next Error',
    'Undo': 'Tools:Undo',
    'Redo': 'Tools:Redo',
    'Menu': None,
}

# The proposals for the commands that have no menu entry: (menu, position,
# label, the reason).  The owner chooses.
PROPOSALS = {
    'Border': ('Tools', 'before Autosort', 'Border',
               'chapter 3 of the manual puts Border, Case and Math side by '
               'side, ahead of Tools: Macros and Library'),
    'Case': ('Tools', 'before Autosort', 'Case',
             'as Border: a command on highlighted text'),
    'Math': ('Tools', 'before Autosort', 'Math',
             'next to Autosort, which also works on a column or table'),
    'Lib Copy': ('Tools, Library', 'first entry', 'Copy',
                 'the library chapter keeps the three commands together; '
                 'Library now has Edit, Delete, Save and Retrieve'),
    'Lib Cut': ('Tools, Library', 'second entry', 'Cut', 'as Lib Copy'),
    'Lib Paste': ('Tools, Library', 'third entry', 'Paste', 'as Lib Copy'),
    'Search': ('Tools', 'before Border', 'Search',
               'there is no Search menu; the Edit menu is for the kind of '
               'document, so Tools holds the three search commands'),
    'Replace': ('Tools', 'after Search', 'Replace', 'as Search'),
    'Search Again': ('Tools', 'after Replace', 'Search Again', 'as Search'),
    'Help': ('the bar', 'between Tools and Quit', 'Help',
             'F1 is taken by xfce4-terminal; a bar entry is where a user '
             'looks for Help (letter H is free in the bar)'),
    'Marker': ('Edit', 'last entry', 'Marker',
               'Global, Marker is the marker SETTINGS, so inserting a '
               'marker needs a different label'),
    'Split': ('Edit', 'new entries', 'Split', 'the editing commands'),
    'Copy': ('Edit', 'new entries', 'Copy', 'the editing commands'),
    'Cut': ('Edit', 'new entries', 'Cut', 'the editing commands'),
    'Paste': ('Edit', 'new entries', 'Paste', 'the editing commands'),
    'Move': ('Edit', 'new entries', 'Move', 'the editing commands'),
    'Format': ('Edit', 'new entries', 'Format', 'the editing commands'),
    'Table': ('Edit', 'new entries', 'Table', 'the editing commands'),
    'Append': ('Edit', 'new entries', 'Append', 'the editing commands'),
    'Join': ('Edit', 'new entries', 'Join', 'the editing commands'),
    'Delete': ('Edit', 'new entries', 'Delete', 'the editing commands'),
    'Overtype': ('Edit', 'new entries', 'Overtype', 'the editing commands'),
    'Set Attribute': ('Edit', 'new entries', 'Set Attribute',
                      'the editing commands'),
    'Go To': ('Edit', 'new entries', 'Go To', 'the editing commands'),
}


# --------------------------------------------------------------- the rows
def fkey_name(mod, n):
    return ('Ctrl-' if mod == 1 else 'Alt-' if mod == 2 else '') + 'F%d' % n


def fkey_bytes(mod, n):
    """The bytes of the xterm form of a function key."""
    m = {0: '', 1: '5', 2: '3'}[mod]
    code = {5: 15, 6: 17, 7: 18, 8: 19, 9: 20, 10: 21, 11: 23, 12: 24}

    if n <= 4:
        if mod == 0:
            return 'ESC O ' + 'PQRS'[n - 1]

        return 'ESC[1;%s%s' % (m, 'PQRS'[n - 1])

    if mod == 0:
        return 'ESC[%d~' % code[n]

    return 'ESC[%d;%s~' % (code[n], m)


def editor_rows(fk, ctrl_codes, alt_chars, alt_digits, alt_bksp, moves):
    """Every key of the Editor: (key, words, mode, group, bytes)."""
    rows = []

    for name, mod, n in fk:
        rows.append((fkey_name(mod, n), COMMANDS.get(name, '?'), 'B',
                     GROUP_OF.get(name, '?'), fkey_bytes(mod, n)))

    for code in ctrl_codes:
        if code in CTRL:
            key, words, mode, group = CTRL[code]
            rows.append((key, words, mode, group, '%02X' % code))

    for c in alt_chars:
        e = ALT.get(c)

        if e is not None:
            rows.append((e[0], e[1], e[2], e[3],
                         'ESC ' + ('?' if c == '?' else c)))

    if alt_digits:
        rows.append(ALT_DIGITS + ('ESC 1 .. ESC 9',))
        rows.append(ALT_ZERO + ('ESC 0',))

    if alt_bksp:
        rows.append(ALT_BKSP + ('ESC 7F',))

    seen = set()

    for key, mvs in moves.items():
        for mv in mvs:
            if mv in MOVES and mv not in seen:
                seen.add(mv)
                k, words, mode = MOVES[mv]
                base = k.split('-')[-1]
                mod = ('2' if k.startswith('Shift') else
                       '5' if k.startswith('Ctrl') else '')
                b = move_bytes(base, mod)

                rows.append((k, words, mode, 'Movement', b))

    for key, words, mode, group, b, probe in OTHER:
        rows.append((key, words, mode, group, b))

    return rows


def fmt(key, words, mode, b):
    """One line of a table: 78 columns."""
    return '  %-20s %-33s %-3s %s' % (key, words, mode, b)


# ---------------------------------------------------------------- the text
def wrap(text, indent=2):
    return textwrap.fill(text, 78, initial_indent=' ' * indent,
                         subsequent_indent=' ' * indent)


def kv(k, t, kw=22):
    """A key and its words, the words wrapped under themselves."""
    lines = textwrap.wrap(t, 78 - kw - 2)
    out = ['  %-*s %s' % (kw, k, lines[0] if lines else '')]

    for ln in lines[1:]:
        out.append(' ' * (kw + 3) + ln)

    return '\n'.join(out)


def used_alt_printable(alt_chars, alt_digits):
    used = set(alt_chars)

    if alt_digits:
        used |= set('0123456789')

    return used


def build(stred):
    ui = read_source('ui.c')
    fk = parse_fkeys(ui)
    ctrl_codes = parse_ctrl(ui)
    alt_chars, alt_digits, alt_bksp = parse_alt(ui)
    moves = parse_moves(ui)
    rows = editor_rows(fk, ctrl_codes, alt_chars, alt_digits, alt_bksp, moves)
    out = []

    def p(t=''):
        out.append(t)

    def rule(t, ch='='):
        p()
        p(t)
        p(ch * len(t))

    p('KEY MAP OF THE PROGRAMS, AND THE F12 MENU ROUTES')
    p('=' * 47)
    p()
    p(wrap('Made by tests/keymap.py from the source (src/ui.c, src/term.c, '
           'src/term_x11.c, src/menu.c, src/widgets.c, src/str.c, '
           'src/macro.c, tools/dviview) and from the menu tree that the '
           'Editor prints with --dump-menus.  It is a study: nothing in '
           'the programs was changed.  Where a key is lost in a terminal or '
           'a desktop, the study of docs/KEYS-ENVIRONMENTS.txt and '
           'docs/TERMINALS.txt is the source.', 0))
    p()
    p(wrap('Mode: B both kinds of document, S structured only.  Bytes: '
           'what a terminal sends (ESC is the byte 1B).  A key written '
           'with a comma has the same bytes as the one before it, or is '
           'one more way to type the same command.', 0))
    p()
    p('Contents')
    p('  PART 1  THE EDITOR')
    p('    1.1 Function keys    1.2 Cursor keys    1.3 Control keys')
    p('    1.4 Alt keys    1.5 Other keys    1.6 The keys by command group')
    p('    1.7 Free keys    1.8 Keys with problems')
    p('    1.9 The soft hyphen and the collapse of a section')
    p('  PART 2  THE DOCUMENT MANAGER')
    p('  PART 3  PANELS, FORMS, LISTS, THE PREVIEW AND THE VIEWER')
    p('  PART 4  THE F12 MENU AND THE MENU ROUTES')

    rule('PART 1.  THE EDITOR')
    p()
    p('1.1  Function keys (the table fkeys[] of src/ui.c)')
    p('-' * 52)
    p(wrap('F10 and F11 have no command; a macro can record them.  F12 is '
           'the menu and is reserved.  Shift with a function key does '
           'nothing alone (the key strip shows empty labels); with Ctrl or '
           'Alt the Ctrl or Alt command is taken.  Alt-F1, Alt-F10 and '
           'Alt-F11 say "not available".  The reader also takes ESC[P to '
           'ESC[S, ESC[1;mP to S, ESC O m P to S (Konsole), ESC[[A to E '
           '(the Linux console) and the rxvt codes ESC[11~ to ESC[24~.'))
    p()
    p(fmt('Key', 'Command', 'M', 'Bytes (xterm form)'))
    p(fmt('-' * 20, '-' * 33, '-', '-' * 19))

    for r in rows[:len(fk)]:
        p(fmt(r[0], r[1], r[2], r[4]))

    p()
    p('1.2  Cursor keys (the movement switch of src/ui.c)')
    p('-' * 52)
    p()
    p(fmt('Key', 'Movement', 'M', 'Bytes (xterm form)'))
    p(fmt('-' * 20, '-' * 33, '-', '-' * 19))

    order = ['Left', 'Right', 'Up', 'Down', 'Home', 'End', 'PgUp', 'PgDn']
    rank = {'': 0, 'Shift': 1, 'Ctrl': 2}
    mrows = [r for r in rows
             if r[3] == 'Movement' and r[0].split('-')[-1] in MOVE_BYTES]
    mrows.sort(key=lambda r: (order.index(r[0].split('-')[-1]),
                              rank[r[0].split('-')[0]
                                   if '-' in r[0] else '']))

    for r in mrows:
        p(fmt(r[0], r[1], r[2], r[4]))

    p()
    p('1.3  Control keys (the switch of control codes)')
    p('-' * 52)
    p()
    p(fmt('Key', 'Command', 'M', 'Byte'))
    p(fmt('-' * 20, '-' * 33, '-', '-' * 19))

    for code in ctrl_codes:
        if code in CTRL:
            key, words, mode, group = CTRL[code]
            p(fmt(key, words, mode, '%02X' % code))

    p()
    p('1.4  Alt keys (the Alt switch, and the macros)')
    p('-' * 52)
    p()
    p(wrap('An Alt-letter that a macro is recorded on runs the macro and '
           'comes first: a macro on Alt-I or Alt-O hides the command.  '
           'Alt-1 to Alt-9 and Alt-0 work in a structured document.'))
    p()
    p(fmt('Key', 'Command', 'M', 'Bytes'))
    p(fmt('-' * 20, '-' * 33, '-', '-' * 19))

    for r in rows:
        if r[4].startswith('ESC ') and not r[4].startswith('ESC O '):
            p(fmt(r[0], r[1], r[2], r[4]))

    p(fmt('Alt-letter', 'Run the macro of that key', 'B', 'ESC letter'))
    p()
    p('1.5  Other keys')
    p('-' * 52)
    p()
    p(fmt('Key', 'Command', 'M', 'Bytes'))
    p(fmt('-' * 20, '-' * 33, '-', '-' * 19))

    for key, words, mode, group, b, probe in OTHER:
        p(fmt(key, words, mode, b))

    p()
    p(wrap('While a highlight is on: Enter accepts it, Esc cancels it, the '
           'cursor keys move the end of it, F1 is Help, and nothing else '
           'acts.  In Autosort Home and End go to the edges of the section '
           'and + and - choose the direction.  Ctrl-F1 ends a recording '
           '(Learn).  In X11 the keypad + - * are Alt-+, Alt-- and Alt-*; '
           'in a terminal they arrive as the plain characters.'))
    p()
    p('1.6  The keys by command group')
    p('-' * 52)
    groups = []

    for r in rows:
        if r[3] not in groups and r[3] != '?':
            groups.append(r[3])

    for g in sorted(groups):
        p()
        p('  ' + g)

        for r in rows:
            if r[3] == g:
                p(fmt('  ' + r[0], r[1], r[2], r[4]))

    # The free keys.
    used = used_alt_printable(alt_chars, alt_digits)
    printable = [chr(c) for c in range(33, 127)]
    free_alt = [c for c in printable if c not in used and c not in 'IO']
    macro_only = [c for c in free_alt if c.isalpha()]
    free_alt_punct = [c for c in free_alt if not c.isalpha()]
    free_ctrl = [c for c in range(1, 32) if c not in ctrl_codes and c != 27]

    p()
    p('1.7  Free keys')
    p('-' * 52)
    p()
    p(wrap('Keys that no command uses and that a terminal sends unchanged '
           '(a desktop may still keep some of them).'))
    p()
    p(wrap('Control: no letter is free; Ctrl-A to Ctrl-Z are all used, and '
           'Ctrl-I, Ctrl-J, Ctrl-M and Ctrl-H are the bytes of Tab, Enter '
           'and Backspace.  Free bytes: ' +
           ', '.join('%02X (Ctrl-%s)' % (c, chr(c + 64)) for c in free_ctrl)
           + '.  In a terminal Ctrl-\\ (1C) and Ctrl-] (1D) arrive '
           'unchanged; with a German or French layout Ctrl-] needs the '
           'AltGr key.'))
    p()
    p(wrap('Alt, letters: Alt-I and Alt-O are commands; every other letter, '
           'A to Z (%d keys), is free and is the key of a macro (Macro '
           'Key: Alt-).  A terminal sends Alt-Shift-letter as ESC and the '
           'capital, which the macro list takes as the same key.'
           % len(set(c.upper() for c in macro_only))))
    p()
    p(wrap('Alt, punctuation that no command uses: ' +
           ' '.join(free_alt_punct) + '.  Alt-] and Alt-\\ arrive plain.  '
           'Alt-[ (a command) and Alt-Shift-O cannot work in a terminal '
           '(section 1.8).'))
    p()
    p(wrap('Function keys: F10, F11 (no command; a macro records them), '
           'Alt-F1, Alt-F10, Alt-F11 (say "not available"); Shift-F1 to '
           'Shift-F11 (nothing); Ctrl-Alt with anything (read as '
           'Ctrl-Alt and ignored).'))

    p()
    p('1.8  Keys with problems')
    p('-' * 52)
    p()
    problems = [
        ('Inside the product', [
            'Ctrl-/, Ctrl-_ and Ctrl-Hyphen are all the byte 1F: Undo.  '
            'The soft hyphen was on Ctrl-Hyphen and moved to Alt-_.',
            'Ctrl-I is Tab, Ctrl-J and Ctrl-M are Enter, Ctrl-[ is Esc '
            '(byte 1B): the same bytes as those keys.',
            'Ctrl-H is Hanging indent, and Backspace is the byte 7F.  A '
            'terminal that sends 08 for Backspace (an xterm with '
            'backarrowKey off, a Windows console) makes Backspace run '
            'Hanging indent.',
            'Undo has three keys (Ctrl-/, Ctrl-Z, Ctrl-F10) and Redo two '
            '(Alt-/, Ctrl-F11).',
            'Ctrl-Space is the byte 00, which is read as the no-break '
            'space: a terminal cannot tell Ctrl-Shift-Space from it.',
            'Ctrl-Shift with a letter is the byte of Ctrl with the letter: '
            'a terminal cannot send it, and the Editor has no command on '
            'it.  Ctrl-Alt-letter arrives as a Ctrl-Alt key and is '
            'ignored.',
            'Alt-[ (promote the section) is ESC [, the start of every '
            'control sequence: the reader waits for the rest, times out '
            'and returns Esc.  Alt-< and Alt-, do the same command and '
            'work.  Alt-Shift-O is ESC O, which the reader takes for the '
            'start of F1 to F4 and drops.',
            'An Alt-letter is ESC and the letter, within 40 ms: over a '
            'slow ssh link it may arrive as Esc and a letter.',
        ]),
        ('Terminals and desktops (docs/KEYS-ENVIRONMENTS.txt)', [
            'xterm sends Alt with a character as a character with the '
            'eighth bit set unless XTerm*metaSendsEscape is true: every '
            'Alt key then types a character.',
            'Shift-Ins (Append a subsection) is the terminals\' paste '
            '(xterm, xfce4-terminal); Shift-PgUp and Shift-PgDn are '
            'scrollback; xfce4-terminal also keeps Shift-Up, Shift-Down, '
            'Shift-Home and Shift-End.',
            'F1 (Help) is the help of xfce4-terminal; F10 and F11 (menu, '
            'full screen) are taken by xfce4-terminal, GNOME Terminal and '
            'Konsole in part, and the two keys have no command.',
            'Alt-F4 (Delete) closes the window in almost every window '
            'manager; GNOME takes Alt-F1, F2, F4, F5, F6, F7, F8 and F10; '
            'Xfce takes Alt-F4, F6 to F11 and all of Ctrl-F1 to Ctrl-F12; '
            'KDE takes Alt-F2, F3, F4 and Ctrl-F1 to Ctrl-F4.  So Border '
            '(Alt-F8), Go To (Alt-F7), Math (Ctrl-F2), Case (Ctrl-F6) and '
            'the library keys are lost in some desktops; the Ctrl-F list '
            'reaches them.',
            'tmux keeps Ctrl-B (Bold) as its prefix.',
            'Konsole sends Shift-, Ctrl- and Alt-F1 to F4 as ESC O m P; '
            'the reader reads them since the last change.',
            'Consoles without Ctrl-F and Alt-F keys (linux, screen, '
            'rxvt, vt100): those commands need the Ctrl-F list.',
        ]),
    ]

    for title, items in problems:
        p('  ' + title)

        for t in items:
            p(wrap(t, 4).replace('    ', '  - ', 1))

        p()

    p('1.9  The soft hyphen and the collapse of a section')
    p('-' * 52)
    p()
    p(wrap('Today: the soft hyphen is Alt-_ (ESC _; Alt-Shift-minus on a US '
           'layout).  A section collapses with Alt-- (ESC -; the keypad '
           'minus in X11), expands with Alt-+ or Alt-= or the keypad +, '
           'and the outline of its text is Alt-*.  Alt-minus was the '
           'soft hyphen key for a short time and was given up because it '
           'collapses a section.'))
    p()
    p(wrap('Four ways to choose.  None is chosen; the owner chooses after '
           'reading the whole map.'))
    cand = [
        ('A', 'No change.  Soft hyphen Alt-_, collapse Alt--.',
         'Nothing moves.  The soft hyphen needs Shift on every layout, '
         'and on a layout where _ is not Shift-minus (French) the key '
         'is not Alt-Shift-minus.'),
        ('B', 'Swap them.  Soft hyphen Alt--, collapse Alt-_.',
         'The soft hyphen is the plain hyphen key with Alt, as the old '
         'program had it on Ctrl-Hyphen.  Collapse takes '
         'the shifted key; the keypad minus must be mapped to Alt-_ in '
         'X11 or it would insert a soft hyphen; the manual and the '
         'cheat sheet change in four places.'),
        ('C', 'Soft hyphen on Ctrl-] (the byte 1D, free).',
         'No Shift and no Alt: it arrives unchanged in every terminal '
         'and in X11.  With a German or French layout Ctrl-] needs '
         'AltGr.  Alt-_ and Alt-- stay as they are.  A new byte for the '
         'macro recorder.'),
        ('D', 'Soft hyphen only in the Ctrl-F list, and a key later.',
         'Nothing collides; a soft hyphen takes three keystrokes '
         '(Ctrl-F, a choice, Enter) and a macro can take any Alt-letter '
         'for it.  The key can be chosen after the whole map is read.'),
    ]

    for letter, head, why in cand:
        p()
        p(wrap(letter + '.  ' + head, 2))
        p(wrap(why, 5))

    # PART 2
    rule('PART 2.  THE DOCUMENT MANAGER')
    p()
    p(wrap('The Document Manager (src/str.c) shows menus and forms; it has '
           'no document keys.  The keys of its menu and form widgets are '
           'those of PART 3, with these differences:'))
    p()
    dm = [
        ('Up, Down, Home, End', 'move the highlight of the menu; the '
         'first letter of an entry jumps to it'),
        ('Enter, Ins', 'choose the entry of the menu'),
        ('Esc', 'leave a menu that allows it'),
        ('F1', 'Help for the entry or the field'),
        ('Tab, Down, Enter', 'next field; Enter on the last field '
         'accepts the form'),
        ('Shift-Tab, Up', 'previous field (the value is checked first)'),
        ('PgUp, PgDn', 'next, previous page of a form with pages (Setup)'),
        ('Ins', 'accept the form'),
        ('Esc', 'cancel the form'),
        ('Left, Right', 'in a choice: the previous, next choice (no wrap); '
         'in a text field: the cursor'),
        ('Space', 'in a choice: the next choice (wraps)'),
        ('F2, +, Alt-+', 'in a choice or a name field: the list'),
        ('Home, End', 'in a text field: the start, the end'),
        ('Backspace, Del', 'in a text field: delete; Del in a Setup field '
         'puts the default back'),
        ('Tab (two part field)', 'to the second part of the field'),
    ]

    for k, t in dm:
        p(kv(k, t))

    p()
    p(wrap('In the Setup form (the Screen Colors page) Space, Left, Right '
           'and F2 choose a colour, Tab switches foreground and '
           'background, Del and R put a colour back.'))

    # PART 3
    rule('PART 3.  PANELS, FORMS, LISTS, THE PREVIEW AND THE VIEWER')
    p()
    p(wrap('The keys of the widgets (src/widgets.c, src/menu.c) are the '
           'same wherever a panel is used, in the Editor and in the '
           'Document Manager.'))
    p()
    widgets = [
        ('The F12 menu bar (src/menu.c)', [
            ('F12, Esc (top level)', 'close the menu'),
            ('Esc', 'one level back'),
            ('Left, Right', 'the previous, next entry (wraps)'),
            ('Home, End', 'the first, the last entry'),
            ('Enter, Ins, Down', 'open the entry or run the command'),
            ('a letter', 'the entry that starts with it, and run it'),
            ('F1', 'Help on the highlighted entry'),
        ]),
        ('A popup menu or a list', [
            ('Up, Down, Home, End', 'move the highlight'),
            ('PgUp, PgDn', 'one screenful'),
            ('Enter', 'choose'),
            ('Esc', 'cancel'),
            ('a letter', 'the next entry that starts with it'),
            ('F1', 'Help'),
        ]),
        ('A name line (popup input)', [
            ('Enter', 'accept'), ('Esc', 'cancel'),
            ('Left, Right, Home, End', 'move the cursor'),
            ('Backspace, Del', 'delete (the first key replaces all)'),
            ('F1', 'Help'),
        ]),
        ('A form of the Editor (popup form)', [
            ('Ins', 'accept (the check of the panel runs first)'),
            ('Esc', 'cancel'),
            ('Up, Shift-Tab', 'previous field'),
            ('Down, Tab, Enter', 'next field (no wrap)'),
            ('PgUp, PgDn', 'next, previous panel of a group'),
            ('Left, Right', 'choice: previous, next; text: the cursor'),
            ('Space', 'choice: next, wraps'),
            ('F2, +, Alt-+', 'choice: the list; name: the picker'),
            ('Home, End', 'start, end of a text field'),
            ('Backspace, Del', 'delete in a text field'),
            ('F1', 'Help for the field'),
        ]),
        ('A grid of names (popup grid)', [
            ('Enter, Ins', 'choose'), ('Esc', 'cancel'),
            ('Up, Down', 'move, wrapping'),
            ('Left, Right', 'column left, right'),
            ('Home, End', 'first, last'),
            ('a letter', 'the next entry that starts with it'),
        ]),
        ('A question', [
            ('Y', 'yes'), ('N, Esc', 'no'),
        ]),
        ('A text to read (pager)', [
            ('Esc, Enter, q, Ctrl-Q', 'close'),
            ('Up, Down', 'one line'),
            ('PgUp, PgDn, Space', 'one screenful'),
            ('Home, End', 'start, end'),
        ]),
        ('The help screens', [
            ('Esc, F1, q', 'close'),
            ('Up, Down, PgUp, PgDn, Space', 'scroll'),
            ('Home, End', 'top, bottom'),
            ('Tab, Right', 'the next link'),
            ('Shift-Tab, Left', 'the previous link'),
            ('Enter', 'follow the link'),
            ('Backspace, Ctrl-H', 'back (closes at the first topic)'),
            ('Z', 'zoom'), ('I', 'the index'),
        ]),
        ('The spell panel and the thesaurus (Ctrl-C, Ctrl-T)', [
            ('Left, Right', 'previous, next suggestion or word'),
            ('Home, End', 'first, last'),
            ('Enter, Ins', 'accept the suggestion'),
            ('Y, N, Esc', 'answer the question, cancel'),
            ('Up, Down', 'thesaurus: the row above, below'),
            ('PgUp, PgDn', 'thesaurus: a page of rows'),
            ('Ctrl-Home, Ctrl-End', 'thesaurus: the heading word, the last'),
            ('Ctrl-T', 'thesaurus: look up the highlighted word'),
            ('Ctrl-PgUp', 'thesaurus: back to the last word'),
        ]),
        ('The Preview and Print wait panels', [
            ('Esc', 'stop LaTeX, delete the files, leave'),
        ]),
        ('The Viewer (X11 only, tools/dviview)', [
            ('Q, Esc', 'quit'),
            ('PgDn, Space, N', 'next page'),
            ('PgUp, P', 'previous page'),
            ('Home, End', 'first, last page'),
            ('G, digits, Enter', 'go to a page'),
            ('Up, Down, Left, Right', 'scroll'),
            ('+, =, keypad +', 'zoom in'),
            ('-, keypad -', 'zoom out'),
            ('0, keypad 0, keypad *', 'zoom 100 percent'),
            ('F1, ?', 'show the legend'),
        ]),
    ]

    for title, items in widgets:
        p(title)

        for k, t in items:
            p('  %-30s %s' % (k, t))

        p()

    # PART 4
    rule('PART 4.  THE F12 MENU AND THE MENU ROUTES')
    p()

    if stred is None:
        p(wrap('The menu tree is printed by the Editor (--dump-menus); this '
               'text was made without the Editor, so PART 4 is left out.'))

        return '\n'.join(out) + '\n'

    levels = menu_tree(stred)
    p('The menu as the program has it (the bar; Edit varies with the '
      'document):')
    p()

    def show(path, depth):
        for n in levels.get(path, []):
            sub = n if path == '' else path + ':' + n
            key = LEAF_KEYS.get(sub)
            tail = '   [%s]' % key if key else ''
            p('  ' * (depth + 1) + n + ('' if sub in levels else tail))

            if sub in levels:
                show(sub, depth + 1)

    show('', 0)
    leaves = menu_leaves(levels)
    p()
    p('Commands that have a key and an entry:')
    p()

    for name, path in sorted(MENU_OF_COMMAND.items()):
        if path is not None:
            key = [fkey_name(m, n) for c, m, n in fk if c == name][0]
            p('  %-14s %-10s %s' % (name, key, path.replace(':', ', ')))

    p()
    p(wrap('Menu entries that have no key of their own: ' +
           ', '.join(l.replace(':', ' ') for l in leaves
                     if LEAF_KEYS.get(l) is None) + '.'))
    p()
    p(wrap('The original: the repository documents say that its menus hold '
           'only these entries.  docs/EDIT-MENU.txt: the Edit menu has '
           'Title, Structured or Unstructured, Section or Document, and '
           'Window, and the commands of the function keys are "not on it '
           'in the original" (the collapse, expand, promote and demote '
           'commands stay on their keys).  docs/TOOLS-MENU.txt: Tools has '
           'Macros, Library, Spell, Thesaurus, Autosort and Count.  '
           'docs/KEYSTROKES.txt marks Undo and Redo (Tools) as not in '
           'the original.  So none of the commands below has a menu '
           'entry in the original: its function key is its only way.'))
    p()
    p('Commands with a function key and NO menu entry (' +
      str(len(missing_commands(fk))) + '):')
    p()
    p('  %-16s %-10s %s' % ('Command', 'Key', 'Proposed menu route'))
    p('  %-16s %-10s %s' % ('-' * 16, '-' * 10, '-' * 30))

    for name, key in missing_commands(fk):
        pr = PROPOSALS.get(name)
        route = '?' if pr is None else '%s, %s' % (pr[0], pr[2])
        p('  %-16s %-10s %s' % (name, key, route))

    p()
    p('The proposals, with the reason:')

    for name, key in missing_commands(fk):
        pr = PROPOSALS.get(name, ('?', '?', '?', '?'))

        if pr[3] == 'the editing commands':
            continue

        p()
        p(wrap('%s (%s): %s, %s, %s.  %s.' % (name, key, pr[0], pr[1],
                                              pr[2], pr[3][0].upper() +
                                              pr[3][1:]), 2))

    p()
    p(wrap('The editing commands (Split, Copy, Cut, Paste, Move, Format, '
           'Table, Append, Join, Delete, Overtype, Set Attribute, Go To '
           'and Marker): fourteen entries would double the Edit menu, '
           'which the original keeps to three.  Two ways, and the owner '
           'chooses.  Way 1: new entries on the Edit menu with those '
           'names, each running the command as its key does.  Way 2: ONE '
           'new entry, "Commands", on the Tools menu (or on the bar), '
           'that opens the same list as Ctrl-F with every command that '
           'has a function key and its key (W29.1 made that list).  Way '
           '2 also covers Border, Case, Math, the library commands, the '
           'search commands and Help, so the proposals above are the '
           'alternative to it for those commands.', 2))
    p()
    p(wrap('Letters of the bar entries: with Case, Math and Count, '
           'Macros and Math, Spell and Search all in Tools, a letter '
           'jumps to the first entry that starts with it, so a new '
           'entry changes which entry a letter reaches; the manual and '
           'the cheat sheet name the letters.', 2))

    return '\n'.join(out) + '\n'


def missing_commands(fk):
    """The commands of fkeys[] with no menu entry: (name, key)."""
    out = []

    for name, mod, n in fk:
        if name in ('Menu',):
            continue

        if MENU_OF_COMMAND.get(name) is None:
            key = fkey_name(mod, n)

            if name == 'Marker' and mod == 2:
                continue

            out.append((name, key))

    return out


# ------------------------------------------------------------------- check
def check(stred, drop=None):
    """The list of problems: what the source has and the tables lack."""
    ui = read_source('ui.c')
    fk = parse_fkeys(ui)
    bad = []
    names = set(n for n, m, k in fk)

    for n in sorted(names):
        if n not in COMMANDS or n == drop:
            bad.append('command %s of fkeys[] has no entry in the key list' %
                       n)

    for n in sorted(set(COMMANDS) - names):
        bad.append('the key list names %s, which fkeys[] does not have' % n)

    codes = parse_ctrl(ui)

    for c in codes:
        if c not in CTRL:
            bad.append('control key %d is read by the source, not named' % c)

    for c in sorted(CTRL):
        if c not in codes:
            bad.append('control key %d is named, not read by the source' % c)

    chars, digits, bksp = parse_alt(ui)

    for c in chars:
        if c not in ALT:
            bad.append('Alt-%s is read by the source, not named' % c)

    for c in sorted(ALT):
        if c not in chars:
            bad.append('Alt-%s is named, not read by the source' % c)

    if digits is False:
        bad.append('the Alt-digit rule is not in the source any more')

    if bksp is False:
        bad.append('Alt-Backspace is not in the source any more')

    mvs = set()

    for key, lst in parse_moves(ui).items():
        mvs |= set(lst)

    for m in sorted(mvs):
        if m not in MOVES:
            bad.append('movement %s is read by the source, not named' % m)

    for m in sorted(MOVES):
        if m not in mvs:
            bad.append('movement %s is named, not read by the source' % m)

    for key, words, mode, group, b, probe in OTHER:
        if probe not in ui and probe not in read_source('term.c') and \
           probe not in read_source('term_x11.c'):
            bad.append('the source has no "%s" for the key %s' % (probe, key))

    for name, route in PROPOSALS.items():
        if name not in names:
            bad.append('a proposal for %s, which has no function key' % name)

    for name, mod, n in fk:
        if (MENU_OF_COMMAND.get(name) is None and name != 'Menu' and
                name not in PROPOSALS):
            bad.append('command %s has no menu entry and no proposal' % name)

    if stred is not None:
        levels = menu_tree(stred)
        leaves = menu_leaves(levels)

        for lf in leaves:
            if lf not in LEAF_KEYS:
                bad.append('menu entry %s is not named in the tables' % lf)

        for lf in sorted(LEAF_KEYS):
            if lf not in leaves and lf != 'Quit':
                bad.append('menu entry %s is named, not in the menu' % lf)

        for name, path in MENU_OF_COMMAND.items():
            if path is not None and path not in leaves:
                bad.append('the menu entry %s of %s is not in the menu' %
                           (path, name))

        for name, mod, n in fk:
            for part in (COMMANDS.get(name, ''),):
                if len(part) > 33:
                    bad.append('the words of %s are longer than 33' % name)

    for lst in (COMMANDS.values(),):
        for w in lst:
            if len(w) > 33:
                bad.append('words too long: ' + w)

    return bad


def main(argv):
    stred = find_stred()

    # KEYMAP_DROP names a command whose entry is taken out of the tables:
    # the test of this file uses it to show that the check notices.
    drop = os.environ.get('KEYMAP_DROP')

    if drop:
        COMMANDS.pop(drop, None)

    if len(argv) >= 2 and argv[1] == '--check':
        bad = check(stred)

        if stred is None:
            print('note: STRED is not set: the menu tree was not compared')

        for b in bad:
            print('FAIL ' + b)

        print('keymap: %s' % ('all in order' if not bad else
                              '%d problem(s)' % len(bad)))

        return 1 if bad else 0

    text = build(stred)

    for line in text.splitlines():
        if len(line) > 80 or any(ord(c) > 126 for c in line):
            sys.stderr.write('keymap: bad line: %r\n' % line)

            return 2

    if len(argv) >= 3 and argv[1] == '--write':
        with open(argv[2], 'w') as f:
            f.write(text)

        return 0

    sys.stdout.write(text)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
