#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The smoke test of the log of a run.  str runs in a pty, the user opens a
document in the Editor, makes a Preview (a fake viewer tells that it could
not show something), makes an Export that fails (the folder is not
writable), leaves the Editor and quits str.  Then the log of the run is
read: the lines of str, the Editor and the Printer are in it, in the order
in which things happened, each with the time, the level, the program and its
process number, the file and the line of the call and a message.  Run from
the root of the source tree:

    STR=BUILD/term/str python3 -I -B tests/runlog.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
str_prog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
btype = os.environ.get('STRATA_BUILD_TYPE', 'debug')
fakeview = os.path.join(here, 'fakeview.sh')
fails = 0

if not os.path.exists(str_prog):
    print('SKIP: build the programs first')
    sys.exit(0)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


tmp = tempfile.mkdtemp(prefix='runlog.')
home = os.path.join(tmp, 'cfg')
doc = os.path.join(tmp, 'plain.doc')
shutil.copy(os.path.join(here, 'data', 'print_panels.doc'), doc)
shutil.copytree(os.path.join(here, 'data', 'pics'), os.path.join(tmp, 'pics'))
ro = os.path.join(tmp, 'ro')
os.makedirs(ro)
os.chmod(ro, 0o500)
rec = os.path.join(tmp, 'rec')
os.makedirs(rec)
os.environ['STRATA_HOME'] = home
os.environ['STRATA_DVIVIEW'] = fakeview
os.environ['PVREC'] = rec
os.environ['PVNOTE'] = 'a picture that the fake viewer cannot show'
for name in ('STRATA_LOG_SOCKET',):
    os.environ.pop(name, None)

PANEL = ['F12', 'ENTER'] + ['RIGHT'] * 5 + ['ENTER', 'wait']
# Edit / Preview / Print Document starts on the name: Enter moves to the
# variables and accepts the panel there.
keys = ['ENTER', 'ENTER', 'ENTER', 'wait']
preview = shutil.which('latex') is not None
if preview:
    keys += ['F12', 'text:P', 'text:P', 'ENTER', 'INS', 'wait', 'wait',
             'wait', 'wait']
keys += PANEL + ['DOWN', 'text:' + os.path.join(ro, 'x.tex'), 'DOWN', 'RIGHT',
                 'RIGHT', 'INS', 'wait', 'ENTER', 'F12', 'text:Q', 'wait',
                 'text:Q', 'wait', 'INS', 'wait']
dumps, st = tui.session([str_prog, doc], keys, status=True)

logdir = os.path.join(home, 'log')
link = os.path.join(logdir, 'current.log')
check('str ended by itself with status 0 (Quit was chosen)',
      st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, st)
check('the link current.log points at the log of the run',
      os.path.islink(link) and
      os.readlink(link).startswith('run-') and os.path.isfile(link),
      os.listdir(logdir) if os.path.isdir(logdir) else 'no directory')
leftover = [n for n in os.listdir(logdir) if n.endswith('.sock')] \
    if os.path.isdir(logdir) else []
check('the socket of the run is gone when the run has ended', leftover == [],
      leftover)

try:
    with open(link, encoding='latin-1') as f:
        lines = f.read().split('\n')
except OSError:
    lines = []
lines = [l for l in lines if l != '']
LINE = re.compile(r'^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3}) '
                  r'(FATAL|ERROR|WARN |NOTE |DEBUG) '
                  r'([a-z]+)\[(\d+)\]: ([A-Za-z0-9_./-]+):(\d+): (.+)$')
parsed = [LINE.match(l) for l in lines]
check('there are lines in the log', len(lines) > 0, lines)
check('every line has the time, the level, the program and its process, '
      'the file and the line and a message',
      all(m is not None for m in parsed),
      [l for l, m in zip(lines, parsed) if m is None])
rows = [m.groups() for m in parsed if m is not None]
times = [m[:7] for m in rows]
check('the lines are in the order of their time', times == sorted(times),
      lines)


def find(program, text, level=None):
    for i, m in enumerate(rows):
        if m[8] == program and text in m[12] and (level is None or
                                                    m[7].strip() == level):
            return i
    return -1


export_at = find('stred', 'the export as LaTeX to')
check('the failed export is an error of the Editor, with the file and the '
      'reason',
      export_at >= 0 and 'x.tex' in rows[export_at][12] and
      rows[export_at][7] == 'ERROR', lines)
check('the error names a file and a line of the source',
      export_at >= 0 and rows[export_at][10].endswith('.c'), lines)

if btype == 'release':
    check('a release build records only warnings and errors',
          all(m[7].strip() in ('FATAL', 'ERROR', 'WARN') for m in rows),
          lines)
else:
    start_at = find('str', 'run starts')
    edit_at = find('stred', 'starts')
    check('the run starts with a line of str, then the Editor',
          0 <= start_at < edit_at, lines)
    check('the lines of str, the Editor and the Printer are all in the log',
          {m[8] for m in rows} >= {'str', 'stred'} and
          (not preview or find('strprint', 'starts') >= 0), lines)
    if preview:
        print_at = find('strprint', 'starts')
        viewer_at = find('stred', 'the viewer ended')
        check('a Preview is logged in order: the Printer starts, then the '
              'viewer ends, then the Export fails',
              0 <= edit_at < print_at < viewer_at < export_at, lines)
        check('the Editor shows what the viewer could not show',
              viewer_at >= 0 and '1 things it could not show' in
              rows[viewer_at][12], lines)
    end_at = find('str', 'run ends')
    check('str says that the run ends, and that line is the last',
          end_at == len(rows) - 1, lines[-3:])
    check('str logged the end of the Editor and its status',
          find('str', 'stred') >= 0 and
          any('ended' in m[12] for m in rows if m[8] == 'str'), lines)

proc_numbers = {m[8]: set() for m in rows}
for m in rows:
    proc_numbers[m[8]].add(m[9])
check('every program has a process number of its own',
      len({p for s in proc_numbers.values() for p in s}) >=
      len(proc_numbers), proc_numbers)

# What is left behind: nothing but logs and the link in the directory.
names = sorted(os.listdir(logdir)) if os.path.isdir(logdir) else []
check('the directory of the logs has the log and the link only',
      all(n == 'current.log' or re.match(r'^run-[0-9-]+\.log$', n)
          for n in names), names)

os.chmod(ro, 0o700)
shutil.rmtree(tmp, ignore_errors=True)
print('runlog: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
