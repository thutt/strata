#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The Print panel keeps the choices that are about the printer, and a print
to a queue leaves no PDF.  The terminal Editor is driven with tests/tui.py; lp
and lpstat are fake programs (tests/fakelp.sh, tests/fakelpstat.sh) and
nothing is sent to a printer.

  (a) a printer and double sided are chosen and printed; the panel opened
      again in the same run has them;
  (b) the Editor is started again: the panel has them;
  (c) the file of the settings holds them (mode 0600) and keeps its other
      keys;
  (d) a cancelled panel saves nothing;
  (e) a print to the fake queue: lp got the queue, the copies and the sides,
      no PDF is left next to the document or in the temporary folder, and no
      question about replacing a file appears even when a file of the name
      of the would-be PDF exists;
  (f) File only: the PDF is written and the question appears;
  (g) a remembered queue that the system does not have any more;
  (h) lp fails: the message shows so and nothing is left behind.

    STRED=... STRATA_HELP=... python3 -I -B tests/printmemory.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')

if not (os.path.exists(stred) and os.path.exists(sample)) or \
        shutil.which('pdflatex') is None:
    print('SKIP: build the programs first, and pdflatex is needed')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='pmem')
os.chmod(tmp, 0o700)
docdir = os.path.join(tmp, 'docs')
os.mkdir(docdir)
doc = os.path.join(docdir, 'chars.doc')
shutil.copy(sample, doc)
wouldbe = os.path.join(docdir, 'chars.pdf')
tmpdir = os.path.join(tmp, 'tmpdir')
os.mkdir(tmpdir)
QUEUES = ('alpha', 'bravo', 'charlie')
ENTRIES = tui.printer_entries(QUEUES)
FINAL = tui.OPEN_PRINT_FINAL
SPACE = 'text: '
LPLOG = os.path.join(tmp, 'lp.log')
LP = os.path.join(here, 'fakelp.sh')
LPSTAT = os.path.join(here, 'fakelpstat.sh')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def home(config=''):
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    if config:
        open(os.path.join(h, 'config'), 'w').write(config)
        os.chmod(os.path.join(h, 'config'), 0o600)
    return h


def run(h, steps, mode='ok', rows=40):
    env = {'STRATA_HOME': h, 'LANG': 'en_US.UTF-8', 'LC_ALL': None,
           'STRATA_ASCII': '0', 'TMUX': None, 'STRATA_LPSTAT': LPSTAT,
           'STRATA_LP': LP, 'FAKELP_LOG': LPLOG, 'FAKELP_MODE': mode,
           'FAKELPSTAT_QUEUES': ' '.join(QUEUES), 'TMPDIR': tmpdir,
           'FAKELPSTAT_DEFAULT': 'bravo'}
    return tui.session([stred, doc, '-H', helpf], steps, env=env, rows=rows)


def field(screen, label):
    for l in screen.split('\n'):
        m = re.search(re.escape(label) + r':\s*\[([^\]]*)\]', l)
        if m:
            return m.group(1)
    return None


def panel(screen):
    return (field(screen, 'Printer'), field(screen, 'Print Double Sided?'),
            field(screen, 'Sort Output Pages?'),
            field(screen, 'Print in Black and White?'))


def out_line(screen):
    """The row of the Output File field of a panel, or None."""
    for l in screen.split('\n'):
        if 'Output File:' in l:
            return l
    return None


def rows_of(screen):
    """The screen row of every label of the panel that has one."""
    labels = ('Output File', 'Printer:', 'Copies', 'Print Double Sided?',
              'Sort Output Pages?', 'Print from Page', 'Error Log?',
              'Print in Black and White?')
    out = {}
    for i, l in enumerate(screen.split('\n')):
        for t in labels:
            if t in l:
                out[t] = i
    return out


def lplines():
    return open(LPLOG).read().split('\n')[:-1] if os.path.exists(LPLOG) \
        else []


def settings(h):
    p = os.path.join(h, 'config')
    return open(p).read() if os.path.exists(p) else ''


def leftovers():
    return sorted(os.listdir(tmpdir))


# From the Printer field: forward to bravo, then the sides (two fields on:
# Copies, Print Double Sided?) to Yes, the sort to No.
TO_BRAVO = ['DOWN'] + [SPACE] * 3
SIDES = ['DOWN', 'DOWN', SPACE]
SORT = ['DOWN', SPACE, SPACE]
CHOOSE = TO_BRAVO + SIDES + SORT
WAIT = ['waitfor:No PDF was kept', 'show']

# ---- (a), (c), (e): print to the queue, open the panel again ---------------
H = home('autosave=no\nbackups=5\n')
open(wouldbe, 'w').write('old file')
d = run(H, FINAL + ['show'] + CHOOSE + ['show', 'INS'] + WAIT + FINAL +
        ['show'])
check('a first panel starts with File only and the printer default choices',
      panel(d[0])[:3] == ('File only', 'Printer default',
                          'Printer default'), panel(d[0]))
check('the choices are on the panel before the print',
      panel(d[1])[:3] == ('bravo', 'Yes', 'No'), panel(d[1]))
check('(a) the panel opened again in the same run has them',
      panel(d[3])[:3] == ('bravo', 'Yes', 'No'), panel(d[3]))
lp = lplines()
check('(e) lp was called once with the queue, the copies and the sides',
      len(lp) == 1 and lp[0].startswith('lp|-d|bravo|-n|1|') and
      'sides=two-sided-long-edge' in lp[0] and 'collate=false' in lp[0], lp)
check('(e) the message line shows what was sent and that no file was kept',
      'Sent' in d[2] and 'bravo' in d[2] and 'double sided' in d[2] and
      'No PDF was kept' in d[2], d[2])
check('(e) no replace question although a file of the PDF name exists, and '
      'that file is as it was', 'Replace' not in d[2] and
      open(wouldbe).read() == 'old file', d[2])
check('(e) no PDF is left in the temporary folder', leftovers() == [],
      leftovers())
pdfs = [f for f in os.listdir(docdir) if f.endswith('.pdf')]
check('(e) no other PDF is next to the document', pdfs == ['chars.pdf'], pdfs)
os.unlink(wouldbe)

s = settings(H)
mode = oct(os.stat(os.path.join(H, 'config')).st_mode & 0o777)
check('(c) the settings hold the choices', 'print_last_printer=bravo' in s and
      'print_last_sides=two' in s and 'print_last_sort=no' in s, s)
check('(c) the other keys are kept and the mode is 0600',
      'autosave=no' in s and 'backups=5' in s and mode == '0o600',
      (s, mode))

# ---- (a) in memory only: the settings file cannot be written ---------------
H6 = home()
# A folder where the file of the settings should be cannot be replaced by
# the file: the save fails (a mode of the folder would be mended by the
# program).
os.mkdir(os.path.join(H6, 'config'))
d = run(H6, FINAL + CHOOSE + ['INS'] + WAIT + FINAL + ['show'])
nofile = not os.path.isfile(os.path.join(H6, 'config'))
check('(a) with no settings file to write the panel still has the choices '
      'in the same run', panel(d[1])[:3] == ('bravo', 'Yes', 'No') and
      nofile, (panel(d[1]), nofile))

# ---- (b) a new run -----------------------------------------------------------
d = run(H, FINAL + ['show'])
check('(b) the Editor started again: the panel has the choices',
      panel(d[0])[:3] == ('bravo', 'Yes', 'No'), panel(d[0]))
check('Output File is shown, inactive, with a queue: its label and text are '
      'there and the field has no dots', out_line(d[0]) is not None and
      'chars.pdf' in out_line(d[0]) and '\u00b7' not in out_line(d[0]), d[0])
d = run(home(), FINAL + ['show'])
check('another settings folder has no memory', panel(d[0])[0] ==
      'File only', panel(d[0]))

# ---- (d) a cancelled panel saves nothing ------------------------------------
H2 = home('autosave=no\n')
d = run(H2, FINAL + CHOOSE + ['ESC'] + FINAL + ['show'])
check('(d) a cancelled panel is not remembered in the run',
      panel(d[0])[:3] == ('File only', 'Printer default',
                          'Printer default'), panel(d[0]))
check('(d) and not in the settings', 'print_last' not in settings(H2),
      settings(H2))

# ---- (f) File only: the PDF is the product, the question appears -----------
H3 = home()
open(wouldbe, 'w').write('old file')
before = len(lplines())
d = run(H3, FINAL + ['INS', 'waitfor:Replace', 'show', 'text:n', 'show'])
check('(f) File only with an existing file asks to replace it',
      'Replace chars.pdf?' in d[0], d[0])
check('(f) No keeps the old file and sends nothing', open(wouldbe).read() ==
      'old file' and len(lplines()) == before, open(wouldbe).read())
d = run(H3, FINAL + ['INS', 'waitfor:Replace', 'text:y',
                     'waitfor:Wrote', 'show'])
check('(f) Yes writes the PDF', os.path.getsize(wouldbe) > 100 and
      open(wouldbe, 'rb').read(4) == b'%PDF' and 'Wrote' in d[0], d[0])
check('(f) File only is remembered too', 'print_last_printer=File only' in
      settings(H3), settings(H3))
os.unlink(wouldbe)

# ---- (g) a remembered queue that is gone ------------------------------------
H4 = home('print_last_printer=ghostq\nprint_last_sides=one\n')
d = run(H4, FINAL + ['show'])
check('(g) a queue that is not there shows File only, with a message',
      panel(d[0])[0] == 'File only' and 'ghostq' in d[0] and
      'not in the list' in d[0], d[0])
check('(g) the other remembered choices stay', panel(d[0])[1] == 'No',
      panel(d[0]))

# ---- (h) lp fails -----------------------------------------------------------
H5 = home()
d = run(H5, FINAL + CHOOSE + ['INS', 'waitfor:lp failed', 'show'],
        mode='fail')
check('(h) the message shows that lp failed and that no PDF was kept',
      'lp failed' in d[0] and 'no PDF was kept' in d[0], d[0])
check('(h) nothing is left behind', leftovers() == [] and
      not os.path.exists(wouldbe), (leftovers(), wouldbe))

# ---- the Output File stays in its place, inactive, with a queue --------------
H7 = home()
steps = list(FINAL) + ['DOWN', 'wait', 'wait']
d = run(H7, steps + ['show'] + [SPACE, 'show'] * 3 + [SPACE, 'show'] * 2 +
        ['LEFT'] * 6 + ['show'])
first = d[0]
alll = rows_of(first)
check('inactive: the panel has all its rows with File only', len(alll) == 8,
      alll)
# d[1] is PDF viewer, d[2] alpha, d[3] bravo, d[4] charlie, d[5] default,
# d[6] search entry.
for k in range(1, 6):
    check('inactive: entry %d of the Printer: the same rows' % k,
          rows_of(d[k]) == alll, (rows_of(d[k]), alll))
check('inactive: a queue: the field is still there, with its text',
      all('chars.pdf' in (out_line(d[k]) or '') for k in (2, 3, 4, 5)), d[3])
check('inactive: back to File only: the text it had',
      'chars.pdf' in (out_line(d[-1]) or ''), d[-1])
# The look: the text of an inactive field is dim; an active one is not.  The
# printer comes from the settings, so that no list of queues is waited for.
def out_attr(config):
    Hc = home(config)
    d0 = run(Hc, FINAL + ['DOWN', 'show'])
    lines = d0[0].split('\n')
    row = [i for i, l in enumerate(lines) if 'Output File:' in l][0]
    col = lines[row].index('Output File:') + len('Output File:') + 4
    d = run(Hc, FINAL + ['DOWN', 'wait', 'dim:%d,%d' % (row, col)])
    return eval(d[0])


a_only = out_attr('')
a_view = out_attr('print_last_printer=PDF viewer\n')
a_queue = out_attr('print_last_printer=bravo\n')
check('look: File only and PDF viewer: the text is not dim; a queue: dim',
      a_only == 0 and a_view == 0 and a_queue != 0,
      (a_only, a_view, a_queue))
# The cursor skips it and a typed key does not change it.
H8 = home('print_last_printer=bravo\n')
d = run(H8, FINAL + ['text:QQQ', 'show', 'UP', 'text:QQQ', 'UP', 'UP', 'DOWN',
                     'DOWN', 'text:QQQ', 'show'])
check('inactive: typed keys and the cursor keys do not reach the field',
      all('chars.pdf' in (out_line(x) or '') and 'QQQ' not in
      (out_line(x) or '') for x in d), d[1])
d = run(home(), FINAL + ['text:QQQ', 'show'])
check('active: with File only a typed key changes the field',
      'QQQ' in (out_line(d[0]) or ''), d[0])

shutil.rmtree(tmp, ignore_errors=True)
print('printmemory: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
