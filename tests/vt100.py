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

"""The VT100 character set of the terminal programs (docs/VT100.txt).

In a terminal that is not in UTF-8 the programs write ASCII bytes only: the
lines of the screen are the letters of the DEC Special Graphics set between
ESC ( 0 and ESC ( B, and the set is always switched off again at the end of
an update, before the cursor is moved, at the exit, at a signal and before a
child program runs.  The pty of tests/tui.py has a screen model that knows the
set, so that the checks see the shapes and the raw bytes show how they were
written.

    STRED=... STR=... STRATA_HELP=... python3 -I -B tests/vt100.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, stat, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')
if not (os.path.exists(stred) and os.path.exists(strprog) and
        os.path.exists(sample)):
    print('SKIP: build the programs first')
    sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='vt100')
doc = os.path.join(tmp, 'chars.doc')
shutil.copy(sample, doc)
# A release build records warnings only: a choice that the program made by
# itself and that is not UTF-8 is a warning, the others are notes.
NOTES = not (os.environ.get('STRATA_BUILD_TYPE') == 'release' or
             '/release/' in stred)
PROBE = b'\x1b[2;1H\xe2\x94\x82\x1b[6n'
DA_QUERY = b'\x1b[c'
ON = b'\x1b(0'
OFF = b'\x1b(B'
MENU = 'Strata Document Manager'
NOTICE_VT = 'No UTF-8 terminal: VT100 lines are drawn.'
NOTICE_ASCII = 'No UTF-8 or VT100 terminal: ASCII is drawn.'
DA_VT100 = b'\x1b[?1;2c'
DA_VT220 = b'\x1b[?62;1;2;6;9;15;18;21;22c'


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


def high(raw):
    return sum(1 for b in raw if b > 0x7f)


def violations(raw):
    """Where the terminal is in the graphics set when it must not be: at the
    end of an update (the cursor is shown), when the cursor is moved, when
    the alternate screen is left, and at the end of the bytes."""
    on = False
    bad = []
    i = 0
    while i < len(raw):
        if raw[i:i + 3] == ON:
            on = True
            i += 3
        elif raw[i:i + 3] == OFF:
            on = False
            i += 3
        else:
            m = re.match(rb'\x1b\[(\?25h|\?1049l|\d+;\d+H)', raw[i:i + 16])
            if m and on:
                bad.append((i, m.group(0)))
            i += m.end() if m else 1
    if on:
        bad.append((len(raw), b'end'))
    return bad


def run(prog, lang='C', term='xterm', answer=None, da=None, force=None,
        config='', mux=False, steps=(), extra_env=None, wait='Document',
        rows=25, status=False):
    """Run a program with the terminal and the locale of a case; returns the
    raw bytes of the whole session and the screens of the 'show' steps."""
    h = home(config)
    env = {'STRATA_ASCII': force, 'LANG': lang, 'LC_ALL': None,
           'LC_CTYPE': None, 'STRATA_HOME': h, 'TERM': term,
           'TMUX': 'x' if mux else None, 'STY': None, 'TERMINFO': None,
           'TERMINFO_DIRS': None}
    env.update(extra_env or {})
    out = tui.session(prog, ['waitfor:' + wait, 'rawbytes'] + list(steps),
                      env=env, answer=answer, da=da, rows=rows,
                      status=status)
    if status:
        out, st = out
    shown = out[1:]
    first = out[0]
    all_raw = eval(first)
    return (all_raw, shown, h) if not status else (all_raw, shown, h, st)


def dm(**kw):
    """The Document Manager, with the whole menu session."""
    steps = kw.pop('steps', ['show', 'gfx', 'DOWN', 'DOWN'] +
                   tui.OPEN_SETUP + ['wait', 'ESC', 'wait', 'UP', 'show',
                   'rawbytes'])
    kw.setdefault('wait', MENU)
    return run([strprog, '-H', helpf], steps=steps, **kw)


def log_of(h):
    p = os.path.join(h, 'log', 'current.log')
    return open(p, errors='replace').read() if os.path.exists(p) else ''


# ---- the main menu in a terminal that is not in UTF-8 --------------------
t0 = time.time()
raw, shown, h = dm()
more = eval(shown[-1])
allraw = raw + more
check('xterm, locale C, no answer: the menu has the single-line box with '
      'the title',
      '\u250c' in shown[0] and '\u2510' in shown[0] and '\u2514' in shown[0]
      and '\u2518' in shown[0] and 'Strata Document Manager' in shown[0] and
      'Edit / Preview / Print Document' in shown[0], shown[0])
check('xterm, locale C: the lines were written as the letters l k m j q x '
      'in the graphics set',
      'l' in shown[1] and 'k' in shown[1] and 'q' in shown[1] and 'x' in
      shown[1] and 'm' in shown[1] and 'j' in shown[1], shown[1])
check('xterm, locale C: the text is not in the graphics set',
      'Edit' not in shown[1] and 'Quit' not in shown[1], shown[1])
check('xterm, locale C: no byte of 0x80 or more in the whole session of the '
      'menu (menu, Setup, back)', high(allraw) == 0 and len(allraw) > 500,
      (high(allraw), len(allraw)))
check('xterm, locale C: a run of lines has one switch: ESC ( 0 l, ten q',
      ON + b'lqqqqqqqqqq' in raw, raw[:300])
check('xterm, locale C: no empty switch (ESC ( 0 then ESC ( B)',
      ON + OFF not in allraw, allraw[:200])
check('xterm, locale C: the set is off at the end of every update, before '
      'every move of the cursor and at the end',
      violations(allraw) == [], violations(allraw)[:5])
check('xterm, locale C: the probe (a UTF-8 character) was not written and '
      'the terminal was not asked who it is',
      PROBE not in allraw and b'\x1b[6n' not in allraw and
      DA_QUERY not in allraw, allraw[:120])
check('xterm, locale C: the first message line names the choice and the '
      'setting', NOTICE_VT in shown[0] and 'terminal_charset' in shown[0],
      shown[0])
lg = log_of(h)
check('xterm, locale C: the log names the mode and the reason',
      re.search(r'term: the screen is drawn in VT100, not in UTF-8: the '
                r'locale is not UTF-8; the terminal type has the line '
                r'characters', lg)
      is not None, lg[:600])
check('xterm, locale C: the start does not wait for a probe (%.1f s)' %
      (time.time() - t0), time.time() - t0 < 6.0)

# ---- the Editor: ASCII text, an accented letter, the table of shapes ------
txt = os.path.join(tmp, 'plain.doc')
shutil.copy(os.path.join(root, 'tests', 'data', 'print_panels.doc'), txt)
raw, shown, h = run([stred, txt, '-H', helpf], wait='Print',
                    steps=['text:Hello', 'F12', 'wait', 'show', 'gfx',
                           'ESC', 'show', 'rawbytes'])
more = eval(shown[-1])
allraw = raw + more
check('Editor, ASCII text: no byte of 0x80 or more; the rules of the '
      'sections are lines of the graphics set',
      high(allraw) == 0 and 'q' * 70 in shown[1] and '\u2500' * 70 in
      shown[0], (high(allraw), shown[0]))
check('Editor: the set is off at the end of every update',
      violations(allraw) == [], violations(allraw)[:5])
check('Editor: the typed text is on the screen as text, not in the '
      'graphics set', 'Hello' in shown[0] and 'Hello' not in shown[1],
      shown[0])
check('Editor: the notice of the first screen is not shown by a later '
      'screen', NOTICE_VT not in shown[2], shown[2])

acc = os.path.join(tmp, 'acc.doc')
shutil.copy(sample, acc)
orig = open(acc, 'rb').read()
raw, shown, h = run([stred, acc, '-H', helpf], wait='Character',
                    steps=['text:a\u00e9b', 'C-s', 'wait', 'show', 'rawbytes'])
more = eval(shown[-1])
saved_vt = open(acc, 'rb').read()
check('Editor, accented letter typed: it is drawn as the ASCII fallback '
      '(e), with no byte above 0x7F', 'aeb' in shown[0] and
      high(raw + more) == 0, shown[0][:300])
check('Editor, accented letter typed: the document keeps it (the byte 82)',
      b'a\x82b' in saved_vt, saved_vt[-200:])
acc2 = os.path.join(tmp, 'acc2.doc')
shutil.copy(sample, acc2)
run([stred, acc2, '-H', helpf], force='0', wait='Character',
    steps=['text:a\u00e9b', 'C-s', 'wait'])
check('Editor: the saved document is the same as after the same keys in the '
      'UTF-8 screen', saved_vt == open(acc2, 'rb').read() and
      saved_vt != orig, len(saved_vt))
check('Editor, the table of the characters: the triangles and arrows are '
      'the ASCII shapes > < ^ v, not the signs + , - . of the set',
      re.search(r'10 +> < \| !', shown[0]) is not None and
      re.search(r'10 +> < \| ! P S - \| \^ v > < L - \^ v', shown[0])
      is not None, shown[0])
check('Editor, the table of the characters: no byte above 0x7F and the set '
      'is off at the end', high(raw + more) == 0 and
      violations(raw + more) == [], violations(raw + more)[:3])

# ---- the set is switched off ----------------------------------------------
raw, shown, h = dm(steps=['show', 'signal:TERM', 'rawbytes'])
tail = eval(shown[-1])
check('after SIGTERM: the bytes written by the signal handler start with '
      'ESC ( B and leave the alternate screen',
      tail.startswith(OFF) and tail.endswith(b'\x1b[?1049l') and
      violations(raw + tail) == [], tail)
raw, shown, h = dm(steps=['show', 'C-z', 'wait', 'show', 'rawbytes'])
check('after Ctrl-Z (a key of the Editor, Undo): the set is off',
      violations(raw + eval(shown[-1])) == [], violations(raw)[:3])
raw, shown, h, st = dm(status=True,
    steps=['DOWN', 'DOWN', 'DOWN', 'DOWN', 'DOWN', 'DOWN', 'ENTER', 'INS',
           'wait', 'rawbytes'])
check('Quit: the bytes of the whole run end with ESC ( B and the normal '
      'screen (the echo of the last key follows)',
      OFF + b'\x1b[0m\x1b[?25h\x1b[?1049l' in eval(shown[-1])[-40:] and
      violations(raw + eval(shown[-1])) == [], eval(shown[-1])[-60:])

# A shell escape: the Document Manager runs the Editor as a child program.
fake = os.path.join(tmp, 'fakeed.sh')
open(fake, 'w').write('#!/bin/bash\necho CHILD-RAN-HERE\nsleep 2\n')
os.chmod(fake, 0o700)
raw, shown, h = run([strprog, '-H', helpf, txt], wait=MENU,
                    extra_env={'STRED': fake},
                   steps=['ENTER', 'ENTER', 'ENTER', 'wait', 'rawbytes'])
more = eval(shown[-1])
allraw = raw + more
k = allraw.find(b'CHILD-RAN-HERE')
check('shell escape: the set is off before the child program writes',
      k > 0 and violations(allraw[:k]) == [] and
      OFF in allraw[max(0, k - 60):k], allraw[max(0, k - 80):k + 20])

# ---- UTF-8 as before --------------------------------------------------------
raw, shown, h = dm(lang='en_US.UTF-8', answer='utf8',
                   steps=['show', 'rawbytes'])
more = eval(shown[-1])
check('xterm, UTF-8 locale, the terminal answers one column: UTF-8 (the '
      'double lines), no graphics set',
      '\u2554' in shown[0] and ON not in raw + more and
      high(raw + more) > 100 and raw.count(PROBE) == 1 and
      NOTICE_VT not in shown[0], shown[0])
check('xterm, UTF-8 locale: the log names the reason (debug builds)',
      not NOTES or 'term: the screen is drawn in UTF-8: the terminal moved '
      'the cursor by one column and the locale is UTF-8' in log_of(h),
      log_of(h)[:400])
raw, shown, h = dm(lang='en_US.UTF-8', answer=None,
                   steps=['show', 'rawbytes'])
check('xterm, UTF-8 locale, no answer: UTF-8 (the locale decides)',
      '\u2554' in shown[0] and ON not in raw, shown[0])
raw, shown, h = dm(lang='en_US.UTF-8', answer='latin1',
                   steps=['show', 'rawbytes'])
check('UTF-8 locale, the terminal moves the cursor by three columns: VT100',
      '\u250c' in shown[0] and
      high((raw + eval(shown[-1])).replace(PROBE, b'')) == 0 and
      NOTICE_VT in shown[0], shown[0])

# ---- the setting and the variable -------------------------------------------
for lang, answer in (('en_US.UTF-8', 'utf8'), ('C', None), ('C', 'latin1')):
    raw, shown, h = dm(lang=lang, answer=answer, config='terminal_charset='
                       'vt100\n', steps=['show', 'rawbytes'])
    more = eval(shown[-1])
    check('the setting vt100 forces VT100 (locale %s, answer %s)' %
          (lang, answer), '\u250c' in shown[0] and ON in raw and
          high(raw + more) == 0 and PROBE not in raw and NOTICE_VT not in
          shown[0] and (not NOTES or 'the setting terminal_charset=vt100'
          in log_of(h)), shown[0])
raw, shown, h = dm(lang='C', answer=None, config='terminal_charset=utf8\n',
                   steps=['show', 'rawbytes'])
check('the setting utf8 forces UTF-8 (locale C)', '\u2554' in shown[0] and
      ON not in raw, shown[0])
raw, shown, h = dm(lang='C', answer=None, config='terminal_charset=ascii\n',
                   steps=['show', 'rawbytes'])
check('the setting ascii forces ASCII (locale C, terminal xterm)',
      ON not in raw and high(raw + eval(shown[-1])) == 0 and
      '+==' in shown[0] or '+--' in shown[0], shown[0])
raw, shown, h = dm(lang='en_US.UTF-8', answer='utf8',
                   config='terminal_charset=ascii\n',
                   steps=['show', 'rawbytes'])
check('the setting ascii forces ASCII (UTF-8 locale, UTF-8 terminal)',
      ON not in raw and high(raw + eval(shown[-1])) == 0, shown[0])
raw, shown, h = dm(lang='C', force='0', config='terminal_charset=vt100\n',
                   steps=['show', 'rawbytes'])
check('STRATA_ASCII=0 wins over the setting', '\u2554' in shown[0] and
      ON not in raw, shown[0])
raw, shown, h = dm(lang='C', force='1', config='terminal_charset=vt100\n',
                   steps=['show', 'rawbytes'])
check('STRATA_ASCII=1 wins over the setting (ASCII, no graphics set)',
      ON not in raw and high(raw + eval(shown[-1])) == 0 and
      NOTICE_VT not in shown[0] and NOTICE_ASCII not in shown[0], shown[0])
raw, shown, h = dm(lang='C', config='terminal_charset=auto\n',
                   steps=['show', 'rawbytes'])
check('the setting auto is the automatic choice', ON in raw, shown[0])
raw, shown, h = dm(lang='C', config='terminal_charset=garbage\n',
                   steps=['show', 'rawbytes'])
check('an unknown value of the setting is the automatic choice',
      ON in raw, shown[0])

# ---- the type of the terminal and the answer to who are you ---------------
t0 = time.time()
raw, shown, h = dm(term='nosuchterm-xyz', steps=['show', 'rawbytes'])
took = time.time() - t0
more = eval(shown[-1])
check('unknown TERM, no answer: ASCII (no graphics set, no byte above 0x7F)',
      ON not in raw + more and high(raw + more) == 0 and
      DA_QUERY in raw + more and NOTICE_ASCII in shown[0] and
      'terminal_charset' in shown[0], shown[0])
check('unknown TERM, no answer: the log names the reason',
      'term: the screen is drawn in ASCII, not in UTF-8: the locale is not '
      'UTF-8; the '
      'terminal type has no line characters and the terminal is not a '
      'VT100' in log_of(h), log_of(h)[:500])
check('unknown TERM, no answer: the start is not held up (%.1f s)' % took,
      took < 4.0, took)
raw, shown, h = dm(term='nosuchterm-xyz', da=DA_VT100,
                   steps=['show', 'rawbytes'])
check('unknown TERM, the terminal answers who are you with a VT100: VT100',
      ON in raw and high(raw + eval(shown[-1])) == 0 and
      NOTICE_VT in shown[0] and 'the terminal answers that it is a VT100 or '
      'later' in log_of(h), shown[0])
raw, shown, h = dm(term='nosuchterm-xyz', da=DA_VT220,
                   steps=['show', 'rawbytes'])
check('unknown TERM, the answer of a VT220 or later: VT100', ON in raw,
      shown[0])
raw, shown, h = dm(term='nosuchterm-xyz', da=b'\x1b[?41;1c',
                   steps=['show', 'rawbytes'])
check('unknown TERM, an answer that is no VT100: ASCII', ON not in raw and
      NOTICE_ASCII in shown[0], shown[0])
raw, shown, h = dm(term='xterm', da=DA_VT100, steps=['show', 'rawbytes'])
check('TERM=xterm decides: the terminal is not asked who it is',
      DA_QUERY not in raw + eval(shown[-1]) and ON in raw, shown[0])
raw, shown, h = dm(term='dumb', steps=['show', 'rawbytes'])
check('TERM=dumb: ASCII', ON not in raw and NOTICE_ASCII in shown[0],
      shown[0])
raw, shown, h = dm(term='linux', steps=['show', 'rawbytes'])
check('TERM=linux, no answer: VT100 (the entry has the line characters)',
      ON in raw, shown[0])
raw, shown, h = dm(term='nosuchterm-xyz', answer='utf8', force='0',
                   lang='en_US.UTF-8', steps=['show', 'rawbytes'])
check('STRATA_ASCII=0 gives UTF-8 whatever the type', '\u2554' in shown[0] and
      ON not in raw, shown[0])

# A database folder of its own: TERMINFO names it.
db = os.path.join(tmp, 'ti')
os.makedirs(os.path.join(db, 'n'))
src = '/usr/share/terminfo/x/xterm'
if not os.path.exists(src):
    src = '/lib/terminfo/x/xterm'
if os.path.exists(src):
    shutil.copy(src, os.path.join(db, 'n', 'nosuchterm-xyz'))
    raw, shown, h = dm(term='nosuchterm-xyz', extra_env={'TERMINFO': db},
                       steps=['show', 'rawbytes'])
    check('TERMINFO is searched: a description there gives VT100 and the '
          'terminal is not asked', ON in raw and DA_QUERY not in raw, shown[0])

# ---- a multiplexer ---------------------------------------------------------
raw, shown, h = dm(term='screen', mux=True, answer='utf8',
                   steps=['show', 'rawbytes'])
check('in tmux, locale C, TERM with the line characters: VT100, and no '
      'question asked', ON in raw and PROBE not in raw and
      DA_QUERY not in raw and b'\x1b[6n' not in raw and
      NOTICE_VT in shown[0] and 'multiplexer, locale is not UTF-8, the '
      'terminal type has the line characters' in log_of(h), shown[0])
raw, shown, h = dm(term='nosuchterm-xyz', mux=True, steps=['show',
                   'rawbytes'])
check('in tmux, locale C, TERM without them: ASCII, nothing asked',
      ON not in raw and DA_QUERY not in raw and b'\x1b[6n' not in raw and
      NOTICE_ASCII in shown[0], shown[0])
raw, shown, h = dm(term='screen', mux=True, lang='en_US.UTF-8',
                   answer='latin1', steps=['show', 'rawbytes'])
check('in tmux, UTF-8 locale: UTF-8 whatever the terminal answers',
      ON not in raw and '\u2554' in shown[0] and b'\x1b[6n' not in raw,
      shown[0])

shutil.rmtree(tmp, ignore_errors=True)
print('vt100: %d failures' % fails)
sys.exit(1 if fails else 0)
