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

"""The corners, tees and crossings of the borders on the screen.

Where lines of the borders of columns and blocks meet, the screen takes the
character from the lines that come together there: a corner where two lines
end, a tee where a line ends against another, a cross where two lines cross,
in single, double and the mixed forms of code page 437 (single line with a
double one: the nearest of the characters that the set has).  The Editor is
run in a pty and its screen read; the screen image export writes the same
characters in UTF-8, in code page 437 and as plain ASCII; a terminal that
cannot show them gets '+', '-', '=' and '|'.

    STRED=... STRATA_HELP=... python3 -I -B tests/joins.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
stred = os.environ.get('STRED', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='joins')
CRL = b'\r\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def session(*args, **kw):
    os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
    return tui.session(*args, **kw)


def trow(text):
    return b'T' + text + b'\x1d\x00' + CRL


def cb(m):
    return b'CB%d,1' % m + CRL + b'CL1' + CRL + b'CR1' + CRL


def mk(name, body):
    path = os.path.join(tmp, name)
    open(path, 'wb').write(b'*R3' + CRL + b'MR1001' + CRL + b'PP\\1\\\x00' +
        CRL + b'FHA1' + CRL + b'DP0,8500,11000' + CRL + body + b'*E' + CRL)
    return path


def table(m, rows=3, masks=None):
    """Three columns (14, 14 and the rest) in `rows` blocks, one above the
    other, each column with the border mask m."""
    out = b'*B0' + CRL
    for r in range(rows):
        mm = masks[r] if masks else m
        out += (b'S' if r == 0 else b'B') + CRL
        out += (b'CW14' + CRL + cb(mm) + trow(b'a%d' % r) + b'CW14' + CRL +
                cb(mm) + trow(b'b%d' % r) + cb(mm) + trow(b'c%d' % r))
    return out


def box(m, n=1):
    out = b'*B0' + CRL
    for r in range(n):
        out += (b'S' if r == 0 else b'B') + CRL + cb(m) + trow(b'block %d' % r)
    return out


def rows_of(path, n=12, env=None):
    d = session([stred, '-H', helpf, path], ['show'])
    return [l.replace('◄', ' ').rstrip() for l in d[0].split('\n')[2:2 + n]]


def line(c, l, r, mid, gap=' '):
    """A table line: three columns, c the character of the junctions."""
    return ''


# the characters: (horizontal, vertical, corners ...) for a kind pair
SS = dict(h='─', v='│', tl='┌', tr='┐', bl='└',
    br='┘', tl_='├', tr_='┤', td='┬', tu='┴',
    x='┼')
DD = dict(h='═', v='║', tl='╔', tr='╗', bl='╚',
    br='╝', tl_='╠', tr_='╣', td='╦', tu='╩',
    x='╬')
SD = dict(h='─', v='║', tl='╓', tr='╖', bl='╙',
    br='╜', tl_='╟', tr_='╢', td='╥', tu='╨',
    x='╫')  # single horizontal, double vertical
DS = dict(h='═', v='│', tl='╒', tr='╕', bl='╘',
    br='╛', tl_='╞', tr_='╡', td='╤', tu='╧',
    x='╪')  # double horizontal, single vertical


def col_line(ch, left, right, fill):
    """One line of the table of three columns: each column from its left
    border cell to its right border cell, a blank between the columns."""
    cols = [(0, 15), (17, 32), (34, 79)]
    out = [' '] * 80
    for a, b in cols:
        for x in range(a, b + 1):
            out[x] = fill
        out[a] = left
        out[b] = right
    return ''.join(out).rstrip()


def tee_line(c):
    return col_line(None, c['tl_'], c['tr_'], c['h'])


def top_line(c):
    return col_line(None, c['tl'], c['tr'], c['h'])


def bot_line(c):
    return col_line(None, c['bl'], c['br'], c['h'])


# ---- a table of three columns and three rows --------------------------------
for m, c, name in ((85, SS, 'single'), (170, DD, 'double'),
        (165, SD, 'single lines across, double sides'),
        (90, DS, 'double lines across, single sides'),
        (255, DD, 'wide (drawn as double)')):
    r = rows_of(mk('t%d.doc' % m, table(m)), 8)
    check('table, %s: the top corners are %s %s' % (name, c['tl'], c['tr']),
        r[0] == top_line(c), (r[0], top_line(c)))
    check('table, %s: between the rows each column has the tees %s %s' %
        (name, c['tl_'], c['tr_']), r[2] == tee_line(c) and
        r[4] == tee_line(c), (r[2], r[4], tee_line(c)))
    check('table, %s: the bottom corners are %s %s' % (name, c['bl'],
        c['br']), r[6] == bot_line(c), (r[6], bot_line(c)))
    check('table, %s: the sides go through the text rows' % name,
        r[1].startswith(c['v'] + 'a0') and r[1].rstrip().endswith(c['v']) and
        r[1].count(c['v']) == 6, r[1])

# ---- a box and two boxes -----------------------------------------------------
for m, c, name in ((85, SS, 'single'), (170, DD, 'double')):
    r = rows_of(mk('b%d.doc' % m, box(m)), 4)
    check('box, %s: four corners and the sides' % name,
        r[0] == c['tl'] + c['h'] * 78 + c['tr'] and
        r[2] == c['bl'] + c['h'] * 78 + c['br'] and
        r[1].startswith(c['v'] + 'block 0') and r[1].endswith(c['v']), r[:3])
    r = rows_of(mk('c%d.doc' % m, box(m, 2)), 6)
    check('two boxes, %s: the line between has a tee at each side' % name,
        r[2] == c['tl_'] + c['h'] * 78 + c['tr_'], r[:5])

# ---- a crossing: a column rule that meets a row rule inside a block ------
# The crossing needs vertical lines above and below and a line on both sides:
# two columns that touch (no gap) with borders on each.  The layout leaves a
# gap, so the cross is drawn where a wide box above and below meets: a box
# whose bottom line meets the sides of the table below (a tee that goes up
# and down) is the junction of three kinds below.
mixed = (b'*B0' + CRL + b'S' + CRL + cb(170) + trow(b'top box') + b'B' + CRL +
    b'CW14' + CRL + cb(85) + trow(b'a') + b'CW14' + CRL + cb(85) + trow(b'b') +
    cb(85) + trow(b'c'))
r = rows_of(mk('m1.doc', mixed), 5)
check('a double box above a single table: the tees of the sides that meet the '
    'double line (single vertical, double horizontal)',
    r[2][0] == '╠' and r[2][15] == '╤' and r[2][17] == '╤' and
    r[2][79] == '╣', r[:4])
mixed2 = (b'*B0' + CRL + b'S' + CRL + cb(85) + trow(b'top box') + b'B' + CRL +
    b'CW14' + CRL + cb(170) + trow(b'a') + b'CW14' + CRL + cb(170) +
    trow(b'b') + cb(170) + trow(b'c'))
r = rows_of(mk('m2.doc', mixed2), 5)
check('a single box above a double table: the nearest characters (double '
    'sides, double line)', r[2][0] == '╠' and r[2][15] == '╦' and
    r[2][79] == '╣', r[:4])
# A column with a border and its neighbour without one.
half = (b'*B0' + CRL + b'S' + CRL + b'CW14' + CRL + cb(85) + trow(b'a') +
    b'CW14' + CRL + trow(b'b') + trow(b'c') + b'B' + CRL + b'CW14' + CRL +
    cb(85) + trow(b'a2') + b'CW14' + CRL + trow(b'b2') + trow(b'c2'))
r = rows_of(mk('h1.doc', half), 5)
check('a column that has a border and one that has none: the rule goes on to '
    'the right of the bordered one, so its right side makes a CROSS (a right '
    'tee was the defect), and the corner above the first block stays a '
    'corner',
    r[2].startswith('\u251c' + '\u2500' * 14 + '\u253c') and
    r[2][16:].strip('\u2500') == '' and r[0] == '\u250c' + '\u2500' * 14 +
    '\u2510', r[:4])
# The seam between a bordered column and one with no border, for every kind
# of line: a cross where the rule goes on to the right of the bordered
# column, and the corner where there is no rule (above the first block).
for m, name, c in ((85, 'single', SS), (170, 'double', DD), (255, 'wide', DD)):
    seam = (b'*B0' + CRL + b'S' + CRL + b'CW14' + CRL + cb(m) + trow(b'a') +
        b'CW14' + CRL + trow(b'b') + trow(b'c') + b'B' + CRL + b'CW14' + CRL +
        cb(m) + trow(b'a2') + b'CW14' + CRL + trow(b'b2') + trow(b'c2'))
    r = rows_of(mk('sm%d.doc' % m, seam), 5)
    check('seam, %s frame next to a column with no border: a cross at the '
        'right side and the rule goes on' % name,
        len(r[2]) > 40 and r[2][0] == c['tl_'] and r[2][15] in
        (c['x'], '\u256c', '\u253c', '\u256a', '\u256b') and
        r[2][16:].strip('\u2500\u2550') == '', r[:4])
    check('seam, %s frame: the right corner above the first block and the '
        'right tee are not used at the seam' % name,
        r[0][15] == c['tr'] and r[2][15] not in (c['tr_'], c['tr']), r[:4])
# A neighbour with borders of its own: its lines are its lines, and the cell
# between the two frames stays clear.
both = (b'*B0' + CRL + b'S' + CRL + b'CW14' + CRL + cb(85) + trow(b'a') +
    b'CW14' + CRL + cb(85) + trow(b'b') + cb(85) + trow(b'c') + b'B' + CRL +
    b'CW14' + CRL + cb(85) + trow(b'a2') + b'CW14' + CRL + cb(85) +
    trow(b'b2') + cb(85) + trow(b'c2'))
r = rows_of(mk('sm8.doc', both), 5)
check('two framed columns side by side: the cell between the frames stays '
    'clear and no cross is made there', r[2][15:17].strip() == '' or
    '\u253c' not in r[2][14:18], r[:4])
# Sides only (no top and no bottom): the rule between two blocks is the
# horizontal line, and the sides that meet it make the tees at its ends.
r = rows_of(mk('s1.doc', box(80, 2)), 6)
check('sides only: the rule between two blocks ends in a left and a right '
    'tee against the sides',
    r[1] == '\u251c' + '\u2500' * 78 + '\u2524' and
    '\u250c' not in r[0] + r[1] and r[0].startswith('\u2502block 0') and
    r[2].startswith('\u2502block 1'), r[:4])
# No border: no box character.
plain = b'*B0' + CRL + b'S' + CRL + trow(b'one') + b'B' + CRL + trow(b'two')
r = rows_of(mk('n1.doc', plain), 4)
check('no border: the rule between blocks has no tee or corner',
    set(r[1]) <= {'─'} and not any(ch in r[0] + r[2] for ch in
    '┌┐└┘├┤│'), r[:4])

# ---- the rule between blocks of a table with sides only ---------------------
# The table of the showcase: columns that have only a left side, in blocks
# one below the other, with the rule between the blocks above, below and
# between the rows.  The rule is the horizontal line: the sides that meet it
# make tees and crosses, and the sides that end at it make a top tee above
# the first row and a bottom tee below the last.


def cbl(m, gap=False):
    return b'CB%d,1' % m + CRL + (b'CL1' + CRL if gap else b'')


def sides(m, rows=3, tail=b'', head=b'intro'):
    out = b'*B0' + CRL + b'S' + CRL + trow(head)
    for r in range(rows):
        out += (b'B' + CRL + b'CW12' + CRL + cbl(m, True) + trow(b'a%d' % r) +
                b'CW12' + CRL + cbl(m) + trow(b'b%d' % r) + cbl(m) +
                trow(b'c%d' % r))
    return out + tail


def xs(row, ch):
    return [i for i, c in enumerate(row) if c == ch]


AFTER = b'B' + CRL + trow(b'after')
for m, c, name in ((16, SS, 'single'), (32, SD, 'double sides')):
    r = rows_of(mk('w%d.doc' % m, sides(m, 3, AFTER)), 9)
    vs = xs(r[2], c['v'])
    check('sides only, %s: the rule above the first row has a corner at '
        'the first side and a top tee at the others' % name,
        len(vs) == 3 and r[1][vs[0]] == c['tl'] and r[1][vs[1]] == c['td'] and
        r[1][vs[2]] == c['td'] and set(r[1]) <= {c['h'], c['td'], c['tl']},
        r[:4])
    check('sides only, %s: the rule between two rows is a left tee and '
        'then a cross at each side' % name,
        r[3][vs[0]] == c['tl_'] and r[3][vs[1]] == c['x'] and
        r[3][vs[2]] == c['x'] and r[5][vs[1]] == c['x'], r[:6])
    check('sides only, %s: the rule below the last row is a corner at '
        'the first side and a bottom tee at the others' % name,
        r[7][vs[0]] == c['bl'] and r[7][vs[1]] == c['tu'] and
        r[7][vs[2]] == c['tu'], r[5:9])
    check('sides only, %s: the sides go through the text rows and the '
        'rule is a plain line elsewhere' % name,
        r[4].count(c['v']) == 3 and r[3].count(c['h']) > 60, r[3:5])

# A double rule (above a headline) under the table: the single sides end in
# a double line, the tee of a single vertical with a double horizontal.
head = (b'*B1' + CRL + b'S' + CRL + b'+' + CRL + b'S' + CRL +
        trow(b'Heading'))
r = rows_of(mk('w3.doc', sides(16, 2, head)), 7)
vs = xs(r[2], '\u2502')
check('sides only: a double rule under the table has the tees of a single '
    'vertical with a double line',
    len(vs) == 3 and r[5][vs[0]] == '\u2558' and
    all(r[5][x] == '\u2567' for x in vs[1:]) and
    set(r[5]) <= {'\u2550', '\u2567', '\u2558'}, r[:6])
# Where the rule is drawn alone nothing changes: no tee, no cross.
check('no table: the rule above and below a plain block has no junction',
    set(rows_of(mk('w4.doc', plain), 4)[1]) <= {'\u2500'} and
    not any(ch in ''.join(rows_of(mk('w5.doc', plain), 4)) for ch in
    '\u253c\u252c\u2534\u251c\u2524'), rows_of(mk('w5.doc', plain), 4))

# ---- the export of the screen image ----------------------------------------
FILE = ['F12', 'ENTER']


def export_bytes(path, setrights):
    out = path + '.txt'
    if os.path.exists(out):
        os.unlink(out)
    session([stred, '-H', helpf, path], FILE + ['RIGHT'] * 5 + ['ENTER',
        'wait', 'DOWN', 'text:' + out, 'DOWN', 'DOWN'] +
        ['RIGHT'] * setrights + ['INS', 'wait'])
    return open(out, 'rb').read() if os.path.exists(out) else b''


tp = mk('e1.doc', table(85))
cp = export_bytes(tp, 0).split(b'\r\n')
check('export, code page 437: the same characters as bytes: corners DA BF, '
    'tees C3 B4, corners C0 D9',
    cp[0].startswith(b'\xda' + b'\xc4' * 14 + b'\xbf') and
    cp[2].startswith(b'\xc3' + b'\xc4' * 14 + b'\xb4') and
    cp[4].startswith(b'\xc3') and cp[6].startswith(b'\xc0' + b'\xc4' * 14 +
    b'\xd9'), cp[:8])
u8 = export_bytes(tp, 5).decode('utf-8', 'replace').split('\r\n')
check('export, UTF-8: the same characters', u8[2].startswith('├' +
    '─' * 14 + '┤') and u8[0].startswith('┌'), u8[:4])
dp = mk('e2.doc', table(170))
u7 = export_bytes(dp, 1).split(b'\r\n')
check('export, 7 bit ASCII: + at the corners and the tees, = and | for the '
    'double lines (as a terminal without the characters shows them)',
    u7[0].startswith(b'+' + b'=' * 14 + b'+') and
    u7[2].startswith(b'+' + b'=' * 14 + b'+') and u7[1].startswith(b'|a0'),
    u7[:4])
l1 = export_bytes(tp, 6).split(b'\r\n')
check('export, Latin-1: + - | and not question marks', l1[2].startswith(
    b'+' + b'-' * 14 + b'+') and b'?' not in b''.join(l1[:8]), l1[:4])

# ---- a terminal that cannot show the characters ----------------------------
d = tui.session([stred, '-H', helpf, mk('a1.doc', table(170))], ['show'],
    env={'STRATA_ASCII': '1'})
ar = [l.replace('<', ' ').rstrip() for l in d[0].split('\n')[2:8]]
check('ASCII screen: corners and tees are +, double lines = and |',
    ar[0].startswith('+' + '=' * 14 + '+') and
    ar[2].startswith('+' + '=' * 14 + '+') and ar[1].startswith('|a0'), ar)

shutil.rmtree(tmp, ignore_errors=True)
print('joins: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
