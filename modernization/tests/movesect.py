#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""Move (F7) moves the highlighted blocks, columns and sections in the place.

The Editor is driven in a pseudo-terminal.  A document with sections is made,
Move is used with the cursor keys, the document is saved, and the outline of
the saved file is read back: the levels and the headlines of the sections, and
the blocks of each section.  A check fails when the keys do not move what the
text of the manual says (the section with its subsections, one level out or
in, the first or the last peer, the next peer of its level; a block within its
section; a column within its block), when Esc does not take the move back, or
when the headline of a section can be moved.

usage: STRED=path/to/stred python3 -I -B tests/movesect.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tui  # noqa: E402

BUILD = os.environ.get('STRATA_BUILD_DIR', '')
STRED = os.environ.get('STRED', os.path.join(BUILD, 'term', 'stred'))
HELP = os.environ.get('STRATA_HELP', os.path.join(BUILD, 'strata.hlp'))

FAILS = []


def check(name, ok, detail=None):
    print(('ok   ' if ok else 'FAIL ') + name)

    if not ok:
        FAILS.append(name)

        if detail is not None:
            print('     ' + str(detail))


def text_rec(s):
    return b'T' + s.encode() + b'\x1d\x00\r\n'


def make(tree):
    """The bytes of a document: tree is a list of (title, [blocks], [subs])."""
    out = [b'*R3\r\n', b'MDMove test\x00\r\n', b'MR1000\r\n',
           b'PP\\1\\\x00\r\n', b'DP0,8500,11000\r\n', b'*B1\r\n',
           b'S\r\n+\r\n']

    def sections(lst):
        for title, blocks, subs in lst:
            out.append(b'S\r\n' + text_rec(title))

            for b in blocks:
                out.append(b'B\r\n' + text_rec(b))

            if subs:
                out.append(b'+\r\n')
                sections(subs)
                out.append(b'-\r\n')

    sections(tree)
    out.append(b'-\r\n*E\r\n')

    return b''.join(out)


def outline(path):
    """The sections of a saved file as (level, title, [blocks]) in order."""
    with open(path, 'rb') as f:
        recs = f.read().split(b'\r\n')

    level = 0
    rows = []
    want_title = False
    after_body = False

    for r in recs:
        if r == b'*B1':
            after_body = True
        elif not after_body:
            continue
        elif r == b'+':
            level += 1
        elif r == b'-':
            level -= 1
        elif r == b'S':
            rows.append([level, None, []])
            want_title = True
        elif r.startswith(b'T'):
            txt = r[1:].rstrip(b'\x1d\x00').decode()

            if want_title and rows:
                rows[-1][1] = txt
                want_title = False
            elif rows:
                rows[-1][2].append(txt)

    return [(lv, t, b) for lv, t, b in rows[1:]]


def titles(rows):
    return [(lv, t) for lv, t, _b in rows]


TREE = [('One', ['one a', 'one b'], []),
        ('Two', ['two a'], [('Two A', ['two a a'], []),
                            ('Two B', ['two b a'], [])]),
        ('Three', ['three a'], []),
        ('Four', ['four a'], [])]


def run(tmp, name, tree, steps, rows=30):
    path = os.path.join(tmp, name)

    with open(path, 'wb') as f:
        f.write(make(tree))

    tui.session([STRED, '-H', HELP, path], steps + ['wait'], rows=rows)

    return path


# The steps: F7 offers Column, Block and Section; the highlight of the section
# of the cursor is accepted with Enter; the keys move it; Enter keeps it.
MOVE_SECTION = ['F7', 'DOWN', 'DOWN', 'ENTER', 'ENTER']
SAVE = ['C-s', 'wait']


def section_move(tmp, name, tree, goto, keys, accept=True):
    steps = goto + ['F7', 'DOWN', 'DOWN', 'ENTER', 'ENTER'] + keys
    steps += ['ENTER'] if accept else ['ESC']

    return outline(run(tmp, name, tree, steps + SAVE))


def main():
    if not os.path.exists(STRED):
        print('SKIP: build the programs first')

        return 0

    tmp = tempfile.mkdtemp(prefix='mvs')
    # The cursor starts in the first headline.  Down goes to the next block:
    # the headline of One, its two blocks, then the headline of Two.
    to_two = ['DOWN', 'DOWN', 'DOWN']
    to_three = ['DOWN'] * 9
    to_four = ['DOWN'] * 11
    to_two_b = ['DOWN'] * 7

    base = titles(outline(run(tmp, 'base.doc', TREE, SAVE)))
    check('the document that the test starts from has the 6 sections',
          base == [(1, 'One'), (1, 'Two'), (2, 'Two A'), (2, 'Two B'),
                   (1, 'Three'), (1, 'Four')], base)

    got = titles(section_move(tmp, 'up.doc', TREE, to_three, ['UP']))
    check('Up moves Three above Two with its subsections: Two stays whole',
          got == [(1, 'One'), (1, 'Three'), (1, 'Two'), (2, 'Two A'),
                  (2, 'Two B'), (1, 'Four')], got)

    got = titles(section_move(tmp, 'down.doc', TREE, to_two, ['DOWN']))
    check('Down moves Two with Two A and Two B below Three',
          got == [(1, 'One'), (1, 'Three'), (1, 'Two'), (2, 'Two A'),
                  (2, 'Two B'), (1, 'Four')], got)

    got = titles(section_move(tmp, 'home.doc', TREE, to_four, ['HOME']))
    check('Home makes Four the first section of its level',
          got == [(1, 'Four'), (1, 'One'), (1, 'Two'), (2, 'Two A'),
                  (2, 'Two B'), (1, 'Three')], got)

    got = titles(section_move(tmp, 'end.doc', TREE, to_two, ['END']))
    check('End makes Two with its subsections the last section of its level',
          got == [(1, 'One'), (1, 'Three'), (1, 'Four'), (1, 'Two'),
                  (2, 'Two A'), (2, 'Two B')], got)

    got = titles(section_move(tmp, 'right.doc', TREE, to_three, ['RIGHT']))
    check('Right makes Three a subsection (the last) of Two: one level in',
          got == [(1, 'One'), (1, 'Two'), (2, 'Two A'), (2, 'Two B'),
                  (2, 'Three'), (1, 'Four')], got)

    got = titles(section_move(tmp, 'left.doc', TREE, to_two_b, ['LEFT']))
    check('Left makes Two B a section of the level of Two, after it: one '
          'level out',
          got == [(1, 'One'), (1, 'Two'), (2, 'Two A'), (1, 'Two B'),
                  (1, 'Three'), (1, 'Four')], got)

    got = titles(section_move(tmp, 'pgup.doc', TREE, to_four, ['PGUP']))
    check('PgUp moves Four above the previous section of its level, Three',
          got == [(1, 'One'), (1, 'Two'), (2, 'Two A'), (2, 'Two B'),
                  (1, 'Four'), (1, 'Three')], got)

    got = titles(section_move(tmp, 'pgdn.doc', TREE, to_two, ['PGDN']))
    check('PgDn moves Two with its subsections below the next peer',
          got == [(1, 'One'), (1, 'Three'), (1, 'Two'), (2, 'Two A'),
                  (2, 'Two B'), (1, 'Four')], got)

    # A section that is the only one of its level in its parent: the parent
    # keeps no empty level when it moves out.
    tree = [('Top', ['top a'], [('Only', ['only a'], [])]),
            ('Next', ['next a'], [])]
    rows = section_move(tmp, 'only.doc', tree, ['DOWN', 'DOWN', 'DOWN'],
                        ['LEFT'])
    check('a section that moves out of its parent leaves the parent with no '
          'empty level',
          titles(rows) == [(1, 'Top'), (1, 'Only'), (1, 'Next')], titles(rows))

    # Keys of a move that cannot go farther change nothing.
    got = titles(section_move(tmp, 'first.doc', TREE, [], ['UP', 'HOME']))
    check('the first section does not move up or to the home place',
          got == base, got)

    # Esc takes the whole move back.
    got = titles(section_move(tmp, 'esc.doc', TREE, to_three,
                              ['UP', 'UP'], accept=False))
    check('Esc takes the moves of the section back', got == base, got)

    # The blocks of a section go with it.
    rows = section_move(tmp, 'blocks.doc', TREE, to_three, ['UP'])
    check('the blocks of a moved section stay in it',
          [b for _l, t, b in rows if t == 'Three'] == [['three a']] and
          [b for _l, t, b in rows if t == 'Two'] == [['two a']], rows)

    # Blocks: one block moves within its section; the headline cannot move.
    steps = ['DOWN', 'F7', 'DOWN', 'ENTER', 'ENTER', 'DOWN']
    rows = outline(run(tmp, 'blk.doc', TREE, steps + ['ENTER'] + SAVE))
    check('Move Block Down: the first block of One moves below the second',
          [b for _l, t, b in rows if t == 'One'] == [['one b', 'one a']], rows)

    steps = ['DOWN', 'DOWN', 'F7', 'DOWN', 'ENTER', 'ENTER', 'UP']
    rows = outline(run(tmp, 'blk2.doc', TREE, steps + ['ENTER'] + SAVE))
    check('Move Block Up: the second block of One moves above the first',
          [b for _l, t, b in rows if t == 'One'] == [['one b', 'one a']], rows)

    steps = ['F7', 'DOWN', 'ENTER', 'ENTER', 'DOWN']
    rows = outline(run(tmp, 'blk3.doc', TREE, steps + ['ENTER'] + SAVE))
    check('the headline of a section does not move',
          titles(rows)[0] == (1, 'One') and
          [b for _l, t, b in rows if t == 'One'] == [['one a', 'one b']], rows)

    steps = ['DOWN', 'F7', 'DOWN', 'ENTER', 'ENTER', 'END']
    rows = outline(run(tmp, 'blk4.doc', TREE, steps + ['ENTER'] + SAVE))
    check('Move Block End: the first block moves to the last place',
          [b for _l, t, b in rows if t == 'One'] == [['one b', 'one a']], rows)

    steps = ['DOWN', 'DOWN', 'F7', 'DOWN', 'ENTER', 'ENTER', 'HOME']
    rows = outline(run(tmp, 'blk5.doc', TREE, steps + ['ENTER'] + SAVE))
    check('Move Block Home: the last block moves to the first place after the '
          'headline',
          [b for _l, t, b in rows if t == 'One'] == [['one b', 'one a']], rows)

    # A Cross-reference marker follows its section: the number that it holds
    # is the number that the section has after the move.
    xref = b'\x132(XN3\x00\r\nXT1\r\n\x13)'
    data = make(TREE).replace(b'Tone a\x1d\x00',
                              b'Tsee ' + xref + b' now\x1d\x00', 1)

    def saved_number(name, keys):
        path = os.path.join(tmp, name)

        with open(path, 'wb') as f:
            f.write(data)

        steps = to_three + ['F7', 'DOWN', 'DOWN', 'ENTER', 'ENTER'] + keys
        tui.session([STRED, '-H', HELP, path], steps + ['ENTER'] + SAVE,
                    rows=30)
        with open(path, 'rb') as f:
            body = f.read()

        i = body.find(b'XN')

        return body[i + 2:i + 3] if i >= 0 else None

    check('a Cross-reference marker holds the number of the section after '
          'it moved up: 3 becomes 2',
          saved_number('xr-up.doc', ['UP']) == b'2')
    check('a Cross-reference marker keeps its number when the section moves '
          'and keeps its place', saved_number('xr-none.doc', []) == b'3')
    check('a Cross-reference marker holds the number of the section after '
          'it moved to the first place: 3 becomes 1',
          saved_number('xr-home.doc', ['HOME']) == b'1')

    print('movesect: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
