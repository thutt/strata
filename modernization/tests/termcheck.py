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

"""The start check of the terminal version and its ASCII fallback.

The Editor writes U+2502 at the start and asks the terminal where the cursor
is; one column more means a terminal that reads UTF-8, three mean one that
does not.  Without an answer (800 ms) the locale decides; in tmux and screen
the locale decides at once.  STRATA_ASCII=1 and 0 and the setting
terminal_charset (auto, utf8, ascii) settle it.  When the program chose ASCII
by itself, the first message line shows so.  Every cell is then drawn as an
ASCII look-alike.  The answers of a terminal are never typed as keys.

The pty of tests/tui.py answers like a terminal ('utf8': a character moves the
cursor by one column; 'latin1': a byte does, and the byte 0x9A is answered
with the reply to who are you, as an xterm in 8-bit mode does) or never.

    STRED=... STR=... STRATA_HELP=... python3 -I -B tests/termcheck.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, sys, tempfile, time, unicodedata
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')
if not os.path.exists(stred) or not os.path.exists(sample):
    print('SKIP: build the programs first')
    sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='termcheck')
doc = os.path.join(tmp, 'chars.doc')
shutil.copy(sample, doc)
PROBE = b'\x1b[2;1H\xe2\x94\x82\x1b[6n'
NOTICE = 'No UTF-8 terminal: ASCII is drawn.'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def home(config=''):
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    if config != '':
        open(os.path.join(h, 'config'), 'w').write(config)
        os.chmod(os.path.join(h, 'config'), 0o600)
    return h


def start(lang, answer, force=None, config='', mux=False, steps=(),
          prog=None, pre=()):
    """Run the Editor on the sample; returns (raw bytes, the screens)."""
    env = {'STRATA_ASCII': force, 'LANG': lang, 'LC_ALL': None,
           'LC_CTYPE': None, 'STRATA_HOME': home(config),
           'TMUX': 'x' if mux else None, 'STY': None}
    out = tui.session(prog or [stred, doc, '-H', helpf],
                      list(pre) + ['waitfor:Character set', 'rawbytes',
                      'show'] + list(steps), env=env, answer=answer)
    return eval(out[0]), out[1:]


def rest(raw):
    """The bytes after the question of the start check."""
    return raw.replace(PROBE, b'', 1)


def high(raw):
    return sum(1 for b in raw if b > 0x7f)


BOXH = '\u2500'.encode()      # the horizontal line of the section bar
TRI = '\u25c4'.encode()       # the triangle at the end of a block

# ---- the decision ---------------------------------------------------------
raw, s = start('en_US.UTF-8', 'utf8')
check('utf8 terminal, UTF-8 locale: the question was asked once',
      raw.count(PROBE) == 1, raw[:80])
check('utf8 terminal, UTF-8 locale: the screen has the UTF-8 shapes',
      BOXH in raw and TRI in raw and high(raw) > 100, high(raw))
check('utf8 terminal, UTF-8 locale: no notice', NOTICE not in s[0], s[0])

raw, s = start('C', 'latin1')
check('8-bit terminal, locale C: no byte above 0x7F after the question',
      high(rest(raw)) == 0 and raw.count(PROBE) == 1, high(rest(raw)))
check('8-bit terminal, locale C: the first line shows ASCII is drawn',
      NOTICE in s[0] and 'font is not checked' in s[0], s[0])
check('8-bit terminal, locale C: look-alikes: "1 Character set<"',
      '1 Character set<' in s[0] and '\u25c4' not in s[0], s[0])
raw, s = start('C', 'latin1', steps=['DOWN', 'show'])
check('the notice is gone after the first key', NOTICE not in s[1], s[1])

raw, s = start('en_US.UTF-8', 'latin1')
check('8-bit terminal with a UTF-8 locale: the terminal wins, ASCII',
      high(rest(raw)) == 0 and NOTICE in s[0], (high(rest(raw)), s[0]))

raw, s = start('C', 'utf8')
check('UTF-8 terminal with locale C: the terminal wins, UTF-8 shapes',
      BOXH in raw and TRI in raw and NOTICE not in s[0], s[0])

# ---- no answer: the locale decides, and the start does not hang ----------
for lang, ascii_ in (('C', True), ('en_US.UTF-8', False), ('POSIX', True),
                     ('de_DE.utf8', False)):
    t0 = time.time()
    raw, s = start(lang, None)
    took = time.time() - t0
    check('no answer, locale %s: %s' % (lang, 'ASCII' if ascii_ else 'UTF-8'),
          (high(rest(raw)) == 0) == ascii_ and
          (NOTICE in s[0]) == ascii_, (high(rest(raw)), s[0]))
    check('no answer, locale %s: the start takes less than 2 seconds (%.1f)'
          % (lang, took), took < 2.0, took)

# ---- keys typed while the program waits for the answer are not lost -------
raw, s = start('en_US.UTF-8', None, pre=['bytes:5a'], steps=['wait',
               'show'])
check('a key typed while the check waits for the answer is read after it',
      '1 ZCharacter set' in s[1], s[1])

# ---- multiplexers: the locale decides at once -----------------------------
raw, s = start('C', 'utf8', mux=True)
check('in tmux, locale C: ASCII, and no question was asked',
      high(raw) == 0 and b'\x1b[6n' not in raw and NOTICE in s[0],
      (high(raw), s[0]))
raw, s = start('en_US.UTF-8', 'latin1', mux=True)
check('in tmux, UTF-8 locale: UTF-8, whatever the pty answers',
      BOXH in raw and b'\x1b[6n' not in raw, high(raw))

# ---- STRATA_ASCII and the setting -----------------------------------------
raw, s = start('en_US.UTF-8', 'utf8', force='1')
check('STRATA_ASCII=1 forces ASCII, with no question and no notice',
      high(raw) == 0 and b'\x1b[6n' not in raw and NOTICE not in s[0] and
      '1 Character set<' in s[0], (high(raw), s[0]))
raw, s = start('C', 'latin1', force='0')
check('STRATA_ASCII=0 forces UTF-8 shapes, with no question',
      BOXH in raw and b'\x1b[6n' not in raw and NOTICE not in s[0], high(raw))
raw, s = start('en_US.UTF-8', 'utf8', config='terminal_charset=ascii\n')
check('the setting terminal_charset=ascii forces ASCII',
      high(raw) == 0 and b'\x1b[6n' not in raw, high(raw))
raw, s = start('C', 'latin1', config='terminal_charset=utf8\n')
check('the setting terminal_charset=utf8 forces UTF-8',
      BOXH in raw and b'\x1b[6n' not in raw, high(raw))
raw, s = start('C', 'latin1', force='0', config='terminal_charset=ascii\n')
check('STRATA_ASCII wins over the setting', BOXH in raw, high(raw))
raw, s = start('en_US.UTF-8', 'utf8', config='terminal_charset=auto\n')
check('the setting auto is the automatic choice', BOXH in raw, high(raw))

# ---- the answers of the terminal are not keys -----------------------------
# Forced UTF-8 on a terminal in 8-bit mode: some shapes contain the byte 0x9A,
# and the terminal answers who it is.  The reply must not be typed.
raw, s = start('C', 'latin1', force='0', steps=['wait', 'show'])
check('8-bit terminal, UTF-8 forced: the reply is not typed into the text',
      '64;1;2;6' not in s[1] and '28c' not in s[1] and '*chars.doc' not in
      s[1] and '1 Character set\u25c4' in s[1], s[1])
raw, s = start('en_US.UTF-8', 'utf8', force='0', steps=[
    'bytes:1b5b3f36343b313b323b363b393b31353b31373b31383b32313b32323b323863',
    'bytes:1b5b353b3952', 'show'])
check('a device report and a cursor report that come as keys are dropped',
      '64;1' not in s[1] and '5;9' not in s[1] and '*chars.doc' not in s[1]
      and '1 Character set' in s[1], s[1])
raw, s = start('en_US.UTF-8', 'utf8', force='0', steps=['F3', 'wait',
               'show'])
check('(control) the key F3 opens the Copy menu', 'Column' in s[1], s[1])
raw, s = start('en_US.UTF-8', 'utf8', force='0', steps=[
    'bytes:1b5b353b3952', 'wait', 'show'])
check('a cursor report (row 5) that comes as keys is not the key F3',
      'Column' not in s[1] and '1 Character set' in s[1], s[1])
raw, s = start('en_US.UTF-8', 'utf8', force='0', steps=['F1', 'wait', 'show'])
check('the F1 key still works (Help opens)', 'HELP: Edit Screen' in s[1], s[1])

# ---- the table: every shape has a look-alike -------------------------------
raw, s = start('C', 'latin1')
rows = {}
for line in s[0].split('\n'):
    m = re.match(r'^(?:[*\u2666]|\[FONT\])?([0-9A-F]0) {2}(.*?)(<|\u25c4)?\s*$',
                 line)
    if m:
        # The Font marker of the sample is a short label now, no diamond.
        rows[m.group(1)] = m.group(2).replace(' ', '').replace('[FONT]', '')
cells = {}
for hi in range(0x00, 0x100, 0x10):
    cells[hi] = rows.get('%02X' % hi, '')
check('the table: 16 rows of 16 characters on the screen',
      all(15 <= len(cells[h]) <= 17 for h in cells), cells)
# The oracle: the Unicode names, not the table of the program.
import importlib.util
cp437 = bytes(range(256)).decode('cp437')
bad = []
for hi in range(0x80, 0x100, 0x10):
    row = rows.get('%02X' % hi, '')
    for i in range(16):
        u = cp437[hi + i]
        got = row[i] if i < len(row) else ''
        name = unicodedata.name(u, '')
        base = unicodedata.normalize('NFD', u)[0]
        if 'BLOCK' in name:
            want = '#'
        elif 'SHADE' in name:
            want = ':#'
        elif 'BOX DRAWINGS' in name:
            want = ('=' if 'DOUBLE HORIZONTAL' in name and 'AND' not in name
                    else '|' if name.endswith(('VERTICAL', ' VERTICAL'))
                    and 'AND' not in name else '-' if
                    name.endswith('HORIZONTAL') and 'AND' not in name
                    and 'DOUBLE' not in name else '+|=-')
        elif u.isalpha() and ord(base) < 128:
            want = base
        else:
            want = None
        ok = (got != '' and ord(got) < 128 and
              (got in want if want else got != '?' or u == '\u00bf'))
        if not ok:
            bad.append((hex(hi + i), u, got, want))
check('the table: bars, blocks, shades and letters are the right look-alike '
      '(%d wrong)' % len(bad), bad == [], bad[:6])
check('the table: no cell of the sample is a question mark but the '
      'inverted one', all(c.count('?') <= 1 for c in cells.values()), cells)

# ---- the Document Manager and its Setup ------------------------------------
if os.path.exists(strprog):
    h = home()
    out = tui.session([strprog, '-H', helpf],
                      ['rawbytes'] + tui.OPEN_SETUP + ['wait', 'rawbytes'],
                      env={'STRATA_ASCII': None,
                      'LANG': 'C', 'LC_ALL': None, 'STRATA_HOME': h,
                      'TMUX': None}, answer='latin1')
    check('Document Manager in ASCII: no byte above 0x7F, also in Setup',
          high(rest(eval(out[0]))) == 0 and high(eval(out[1])) == 0 and
          len(eval(out[1])) > 200, (high(eval(out[1])), out[1][:100]))
    # Terminal Characters is on the page of the advanced settings of Setup:
    # PgDn from the first page reaches it, and the field is the third there.
    sh = tui.OPEN_SETUP + ['PGDN', 'DOWN', 'DOWN']
    env = {'STRATA_ASCII': '0', 'LANG': 'en_US.UTF-8', 'LC_ALL': None,
           'STRATA_HOME': h, 'TMUX': None}
    out = tui.session([strprog, '-H', helpf], sh + ['show'], env=env)
    check('Setup has the field Terminal Characters, Automatic at the start',
          any('Terminal Characters:' in l and 'Automatic' in l
              for l in out[0].split('\n')), out[0])
    tui.session([strprog, '-H', helpf], sh + ['RIGHT', 'RIGHT', 'INS',
                'wait'], env=env)
    conf = open(os.path.join(h, 'config')).read() \
        if os.path.exists(os.path.join(h, 'config')) else ''
    check('Setup saves Terminal Characters ASCII as terminal_charset=ascii',
          '\nterminal_charset=ascii\n' in '\n' + conf, conf)
    out = tui.session([strprog, '-H', helpf], sh + ['show'], env=env)
    check('Setup shows ASCII again at the next start',
          any('Terminal Characters:' in l and 'ASCII' in l
              for l in out[0].split('\n')), out[0])
    tui.session([strprog, '-H', helpf], sh + ['LEFT', 'LEFT', 'INS',
                'wait'], env=env)
    conf = open(os.path.join(h, 'config')).read()
    check('Setup: Automatic leaves no line terminal_charset',
          'terminal_charset' not in conf, conf)
    h2 = home('terminal_charset=ascii\n')
    env2 = dict(env, STRATA_ASCII=None, STRATA_HOME=h2, LANG='en_US.UTF-8')
    out = tui.session([strprog, '-H', helpf], ['show'], env=env2,
                      answer='utf8')
    check('the Document Manager follows the setting: ASCII borders',
          '\u2500' not in out[0] and '\u2502' not in out[0] and '+=' in
          out[0], out[0])
    out = tui.session([strprog, '-H', helpf], ['show'],
                      env=dict(env2, STRATA_HOME=home(), LANG='C'),
                      answer='latin1')
    check('the Document Manager shows that ASCII was chosen',
          NOTICE in out[0], out[0])

shutil.rmtree(tmp, ignore_errors=True)
print('termcheck: %d failures' % fails)
sys.exit(1 if fails else 0)
