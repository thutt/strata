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

"""Tests of F12, Print, Draft and Final (a PDF through strprint, sent with
lp), of "stred --print" and of Setup in str.

lp and lpstat are stand-ins (tests/fakelp.sh, tests/fakelpstat.sh) named by
STRATA_LP and STRATA_LPSTAT: they log their arguments.  The real CUPS is
never called.  The real pdflatex makes the PDFs; a fake pdflatex
(tests/fakepdflatex.sh, first in PATH) makes the failures and the slow runs
that can be cancelled.  TMPDIR is private to every run and must be empty at
the end of it.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, signal, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
strprog = os.environ.get('STR', os.path.join(bdir, 'term', 'str'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(root, '..',
    'lotus-manuscript', 'utility_3', 'strata.hlp'))
for tool in ('pdflatex', 'pdfinfo', 'pdftotext', 'pdfimages'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='prt')
# The guards record a call that no test expects in this file.
os.environ['SHADOW_LOG'] = os.path.join(tmp, 'shadow.log')
data = os.path.join(here, 'data')
fakelp = os.path.join(here, 'fakelp.sh')
fakelpstat = os.path.join(here, 'fakelpstat.sh')
cns = os.path.join(sample, 'plan-title-table.doc')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def newdir(name):
    d = os.path.join(tmp, '%s%d' % (name, len(os.listdir(tmp))))
    os.makedirs(d)
    return d


def read(path, mode='rb'):
    try:
        with open(path, mode) as f:
            return f.read()
    except OSError:
        return b'' if 'b' in mode else ''


def pages(path):
    p = subprocess.run(['pdfinfo', path], capture_output=True, text=True)
    for l in p.stdout.split('\n'):
        if l.startswith('Pages:'):
            return int(l.split()[1])
    return 0


def pdftext(path):
    if not os.path.exists(path):
        return ''
    return subprocess.run(['pdftotext', '-layout', path, '-'],
        capture_output=True, text=True).stdout


def sizes(path):
    """The (width, height) of every page as it is seen: a page that is
    rotated by 90 degrees (landscape) has the sides turned."""
    n = pages(path)
    p = subprocess.run(['pdfinfo', '-f', '1', '-l', str(n), path],
        capture_output=True, text=True)
    out = []
    w = h = 0.0
    for l in p.stdout.split('\n'):
        if l.startswith('Page') and 'size:' in l:
            w, h = l.split('size:')[1].split()[0], l.split('x')[1].split()[0]
            w, h = float(w), float(h)
        elif l.startswith('Page') and 'rot:' in l:
            out.append((h, w) if l.split('rot:')[1].strip() in ('90', '270')
                else (w, h))
    return out


def images(path):
    p = subprocess.run(['pdfimages', '-list', path], capture_output=True,
        text=True)
    return max(0, len(p.stdout.strip().split('\n')) - 2)


def left_over(t):
    return sorted(os.listdir(t))


def procs_of(t):
    p = subprocess.run(['pgrep', '-f', t], capture_output=True, text=True)
    return p.stdout.strip()


def setup(name, **extra):
    """A run: a private TMPDIR, a home, the log of lp and the environment."""
    run = newdir(name)
    t = os.path.join(run, 'tmp')
    os.makedirs(t)
    log = os.path.join(run, 'lp.log')
    env = {'TMPDIR': t, 'STRATA_HOME': os.path.join(run, 'home'),
        'STRATA_LP': fakelp, 'STRATA_LPSTAT': fakelpstat, 'FAKELP_LOG': log,
        'FAKELP_MODE': None, 'FAKELP_SECONDS': None, 'PVFAKE': None,
        'STRATA_STRPRINT': None}
    env.update(extra)
    return run, t, log, env


SHADOWBIN = os.path.join(here, 'shadowbin')


def apply_env(env):
    """Set the environment of a run.  The directory shadowbin is always the
    first in PATH (xdg-open, gio and open are guards there), unless the test
    says NO_SHADOW=1 and has a PATH without any program that opens a PDF."""
    env = dict(env)
    if env.get('NO_SHADOW') != '1':
        base = env.get('PATH')
        if base is None:
            base = os.environ.get('PATH', '')
        env['PATH'] = SHADOWBIN + ':' + base
    # The programs are not looked for in PATH: the folders of the PATH of
    # the test are the folders of the programs (a debug build reads them from
    # STRATA_PROGRAM_DIRS), so a fake program first in PATH is still run.
    if 'STRATA_PROGRAM_DIRS' not in env:
        base = env.get('PATH')
        if base is None:
            base = os.environ.get('PATH', '')
        env['STRATA_PROGRAM_DIRS'] = base
    saved = {}
    for k, v in env.items():
        saved[k] = os.environ.get(k)
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v
    return saved


def restore_env(saved):
    for k, v in saved.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v


def fresh(run, src, name='work.doc'):
    path = os.path.join(run, name)
    shutil.copy(src, path)
    if not os.path.exists(os.path.join(run, 'pics')):
        shutil.copytree(os.path.join(data, 'pics'), os.path.join(run, 'pics'))
        shutil.copy(os.path.join(data, 'picture.png'), run)
    return path


def forget(env):
    """The panels start with the choices of the last print (printmemory.py
    tests that); these checks want the first-use state each time."""
    cfg = os.path.join(env.get('STRATA_HOME', ''), 'config')
    if os.path.exists(cfg):
        keep = [l for l in open(cfg).read().split('\n')
                if not l.startswith('print_last_')]
        open(cfg, 'w').write('\n'.join(keep))


def edit(env, path, keys):
    forget(env)
    saved = apply_env(env)
    try:
        return tui.session([stred, '-H', helpf, path], keys)
    finally:
        restore_env(saved)


def mgr(env, keys, cwd):
    saved = apply_env(env)
    old = os.getcwd()
    os.chdir(cwd)
    try:
        return tui.session([strprog, '-H', helpf], keys)
    finally:
        os.chdir(old)
        restore_env(saved)


def cli(env, args, timeout=120):
    saved = apply_env(env)
    try:
        p = subprocess.run([stred] + args, capture_output=True, text=True,
            timeout=timeout)
    finally:
        restore_env(saved)
    return p.returncode, p.stdout, p.stderr


WAIT = ['wait'] * 8
FINAL = tui.OPEN_PRINT_FINAL
DRAFT = tui.OPEN_PRINT_DRAFT


def lines(log):
    return [l for l in read(log, 'r').split('\n') if l]


# A small real PDF, the result of the fake pdflatex.
run0 = newdir('tpl')
tpl = os.path.join(run0, 'tpl.pdf')
rc, out, err = cli({'STRATA_HOME': os.path.join(run0, 'h')},
    ['--print', os.path.join(data, 'plain_text.doc'), '-o', tpl])
check('setup: the real pdflatex makes a template PDF', rc == 0 and
    os.path.exists(tpl), (rc, out, err))
fb = newdir('fakebin')
# A copy that only the user can write: a program that others can write is
# not started.
shutil.copy(os.path.join(here, 'fakepdflatex.sh'),
            os.path.join(fb, 'pdflatex'))
os.chmod(os.path.join(fb, 'pdflatex'), 0o755)
fpath = fb + ':' + os.environ.get('PATH', '')
nopath = newdir('emptybin')

# ---- the panel, the choice list of the printer, the end message ----------
run, t, log, env = setup('list')
path = fresh(run, cns)
out = os.path.join(run, 'a.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'DOWN', 'F2', 'show', 'DOWN',
    'DOWN', 'ENTER', 'show', 'INS'] + WAIT + ['show', 'ENTER', 'show'])
check('panel: the list of the Printer field has File only and the queues',
    all(w in d[0] for w in ('File only', 'laser', 'office')), d[0])
check('panel: a queue chosen from the list is in the field',
    any('Printer:' in l and 'laser' in l for l in d[1].split('\n')), d[1])
def boxed(screen):
    """The text inside the popup box: between the vertical lines."""
    out = []
    for l in screen.split('\n'):
        if l.count('\u2551') >= 2:
            out.append(l[l.index('\u2551') + 1:l.rindex('\u2551')])
    return ' '.join(' '.join(out).split())


def shows(screen, prefix, path, suffix=''):
    """Does the screen show prefix, the path and suffix?  The words may be
    wrapped (a long path is broken by a panel), so spaces do not count, and
    the Editor shortens a long path in its message line from the front, to
    ...tail, so that the message still ends as it does."""
    def squeeze(t):
        return ''.join(t.split())
    s = squeeze(screen)
    if squeeze(prefix + path + suffix) in s:
        return True
    return re.search(re.escape(squeeze(prefix)) + r'\.\.\.\S*' +
                     re.escape(squeeze(os.path.basename(path))) +
                     re.escape(squeeze(suffix)), s) is not None


# The sample document sets Page Break Level 2: each section of the first two
# levels starts a page, so that a Final print has four pages (it had two when
# the setting was ignored).
flat = boxed(d[2])
check('end: the message line shows the queue, the pages and that no PDF was '
    'kept', 'Sent to laser: 4 pages, 1 copy' in d[2] and
    'No PDF was kept' in d[2], d[2])
check('end: no popup: the editor is back',
    'STRUCTURED EDIT' in d[2] and 'Press any key' not in d[2], d[2])
lg = lines(log)
check('lp: called once with the queue, one copy, the title and a PDF',
    len(lg) == 1 and lg[0].startswith('lp|-d|laser|-n|1|-t|work.doc|') and
    'print.pdf size=' in lg[0] and 'magic=%PDF' in lg[0], lg)
check('pdf: no file is made for a queue', not os.path.exists(out) and
    [n for n in os.listdir(run) if n.endswith('.pdf')] == [], os.listdir(run))
check('run: nothing is left in the temporary directory', left_over(t) == [],
    left_over(t))

# ---- every field: copies, sides, sort, pages reach lp --------------------
run, t, log, env = setup('fields')
path = fresh(run, cns)
out = os.path.join(run, 'b.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'DOWN', *tui.printer_steps('office'), 'DOWN',
    'text:3', 'DOWN', 'RIGHT', 'DOWN', 'RIGHT RIGHT'] + ['DOWN'] * 6 +
    ['text:1', 'DOWN', 'text:1', 'INS'] + WAIT + ['show'])
lg = lines(log)
check('lp: copies, double sided, sort and the page range are passed',
    len(lg) == 1 and lg[0].startswith('lp|-d|office|-n|3|-o|sides=two-sided-'
    'long-edge|-o|collate=false|-P|1-1|-t|work.doc|') and
    'magic=%PDF' in lg[0], lg)
check('lp: no file is left for the range of pages', not os.path.exists(out),
    os.listdir(run))
run, t, log, env = setup('sides1')
path = fresh(run, cns)
out = os.path.join(run, 'c.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'DOWN', *tui.printer_steps('office'), 'DOWN',
    'DOWN', 'RIGHT RIGHT', 'DOWN', 'RIGHT', 'INS'] + WAIT)
lg = lines(log)
check('lp: One sided (the third choice) and sort Yes',
    len(lg) == 1 and '|-o|sides=one-sided|-o|collate=true|' in lg[0], lg)

# ---- refused values ------------------------------------------------------
def refused(name, keys, text, doc=cns):
    run, t, log, env = setup(name)
    path = fresh(run, doc)
    out = os.path.join(run, 'r.pdf')
    d = edit(env, path, FINAL + ['text:' + out] + keys + ['INS', 'wait',
        'show'])
    check('refused: %s' % name, text in d[0] and not os.path.exists(out) and
        lines(log) == [], d[0])


refused('copies abc', ['DOWN', 'DOWN', 'text:abc'], 'Copies must be a number '
    'from 1 to 99')
refused('copies 0', ['DOWN', 'DOWN', 'text:0'], 'Copies must be a number')
refused('copies 100', ['DOWN', 'DOWN', 'text:100'], 'Copies must be a number')
refused('copies 2 without a printer', ['DOWN', 'DOWN', 'text:2'],
    'More than one copy needs a printer')
refused('page range without a printer', ['DOWN'] * 10 + ['text:1'],
    'A page range needs a printer')
refused('double sided without a printer', ['DOWN'] * 3 + ['RIGHT'],
    'Double sided needs a printer')
refused('first page after the last', ['DOWN', *tui.printer_steps('laser')] + ['DOWN'] * 9 +
    ['text:5', 'DOWN', 'text:2'], 'first page is after the last')
refused('page 0', ['DOWN', *tui.printer_steps('laser')] + ['DOWN'] * 9 + ['text:0'],
    'A page must be a number from 1 to 99999')
refused('page abc', ['DOWN', *tui.printer_steps('laser')] + ['DOWN'] * 9 + ['text:abc'],
    'A page must be a number from 1 to 99999')
run, t, log, env = setup('nodir')
path = fresh(run, cns)
d = edit(env, path, FINAL + ['text:/no/such/dir/x.pdf', 'INS', 'wait', 'show'])
check('refused: an output file in a directory that is not there',
    'Cannot find the directory /no/such/dir' in d[0], d[0])
d = edit(env, path, FINAL + ['text:' + run, 'INS', 'wait', 'show'])
check('refused: an output file that is a directory', 'is a directory' in
    d[0], d[0])
d = edit(env, path, FINAL + ['text:' + path, 'INS', 'wait', 'show'])
check('refused: the PDF cannot replace the document itself',
    'cannot replace the document' in d[0], d[0])

# ---- the output file that exists: the confirm panel ------------------------
run, t, log, env = setup('exists')
path = fresh(run, cns)
out = os.path.join(run, 'e.pdf')
open(out, 'w').write('OLD')
d = edit(env, path, FINAL + ['text:' + out, 'INS', 'wait', 'show', 'DOWN',
    'ENTER', 'wait', 'show', 'ESC', 'wait'])
check('exists: an existing file asks before it is replaced',
    'Replace e.pdf?' in d[0] and 'Yes' in d[0] and 'No' in d[0], d[0])
check('exists: No returns to the panel and the file is not touched',
    'Print Document' in d[1] and 'Output File:' in d[1] and
    read(out) == b'OLD', d[1])
d = edit(env, path, FINAL + ['text:' + out, 'INS', 'wait', 'ENTER'] + WAIT +
    ['show'])
check('exists: Yes replaces it with the PDF', read(out)[:4] == b'%PDF' and
    'Wrote ' in d[0], d[0])

# ---- the wait panel and the cancel with Esc --------------------------------
run, t, log, env = setup('slow', PATH=fpath, PVFAKE='slow', PVPAGES='30',
    PVPDF=tpl)
path = fresh(run, cns)
out = os.path.join(run, 's.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'INS', 'wait', 'show'] +
    ['wait'] * 6 + ['show'])
check('panel: it shows Print, the stage, the page, the gauge and Esc',
    'Print' in d[0] and 'Running LaTeX (pass 1)' in d[0] and 'Page ' in d[0]
    and ' of about ' in d[0] and '%' in d[0] and 'Esc cancels' in d[0], d[0])
check('panel: the run ends with the message of the pages shipped',
    shows(d[1], 'Wrote ', out, ' (30 pages)'), d[1])
check('panel: the PDF is in place and nothing is left', read(out)[:4] ==
    b'%PDF' and left_over(t) == [], left_over(t))

run, t, log, env = setup('cancel', PATH=fpath, PVFAKE='slow', PVPAGES='300',
    PVPDF=tpl)
path = fresh(run, cns)
out = os.path.join(run, 'x.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'INS', 'wait', 'show', 'ESC',
    'wait', 'show'])
check('cancel: the panel was up while LaTeX ran', 'Running LaTeX' in d[0] and
    'Esc cancels' in d[0], d[0])
check('cancel: Esc returns to the editor with a message',
    'Print cancelled' in d[1] and 'Running LaTeX' not in d[1], d[1])
check('cancel: no PDF is left', not os.path.exists(out) and
    [n for n in os.listdir(run) if n.endswith('.pdf')] == [])
check('cancel: nothing is left in the temporary directory', left_over(t) == [],
    left_over(t))
time.sleep(0.3)
check('cancel: no process of the run is left', procs_of(t) == '', procs_of(t))
check('cancel: lp was not called', lines(log) == [])
open(out, 'w').write('OLD')
d = edit(env, path, FINAL + ['text:' + out, 'INS', 'wait', 'ENTER', 'wait',
    'wait', 'ESC', 'wait', 'show'])
check('cancel: a file that was there before is left as it was',
    read(out) == b'OLD' and 'Print cancelled' in d[0], read(out))

# ---- cancel while lp runs --------------------------------------------------
run, t, log, env = setup('lpslow', FAKELP_MODE='slow', FAKELP_SECONDS='37.5')
path = fresh(run, cns)
out = os.path.join(run, 'l.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'DOWN', *tui.printer_steps('laser'), 'INS'] +
    WAIT + ['show', 'ESC', 'wait', 'show'])
check('send: the panel shows it is sending to the printer',
    'Sending to the printer' in d[0], d[0])
check('send: Esc stops lp, and the message shows no PDF was kept',
    'Cancelled while sending to laser' in boxed(d[1]) and
    'no PDF was kept' in boxed(d[1]) and not os.path.exists(out), d[1])
time.sleep(0.3)
check('send: the lp that was stopped is gone', procs_of('sleep 37.5') == '',
    procs_of('sleep 37.5'))
check('send: nothing is left in the temporary directory', left_over(t) == [],
    left_over(t))

# ---- errors ----------------------------------------------------------------
run, t, log, env = setup('texerr', PATH=fpath, PVFAKE='error')
path = fresh(run, cns)
out = os.path.join(run, 'f.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'INS'] + WAIT + ['show'])
check('error: the first line of the error of LaTeX is in a popup',
    'LaTeX failed' in d[0] and '! Fake error.' in d[0] and
    'l.1 the fake line' in d[0], d[0])
check('error: no PDF and no file are left', not os.path.exists(out) and
    left_over(t) == [], left_over(t))

run, t, log, env = setup('lpfail', FAKELP_MODE='fail')
path = fresh(run, cns)
out = os.path.join(run, 'g.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'DOWN', *tui.printer_steps('laser'), 'INS'] +
    WAIT + ['show'])
# The popup breaks its text into lines where the room ends, and a long path
# changes where: the words are looked for in the text of the box, joined.
flat = boxed(d[0])
check('error: the error of lp is shown with its text, and no PDF is kept',
    'lp failed (status 1)' in flat and 'does not exist' in flat and
    not os.path.exists(out) and 'no PDF was kept' in flat, d[0])

run, t, log, env = setup('nolp', STRATA_LP='/no/such/lp')
path = fresh(run, cns)
out = os.path.join(run, 'h.pdf')
d = edit(env, path, FINAL + ['show', 'text:' + out, 'DOWN', *tui.printer_steps('laser'),
    'INS'] + WAIT + ['show'])
check('missing lp: the panel shows so', 'lp not found' in d[0], d[0])
check('missing lp: the PDF is made and the message shows it was not sent',
    read(out)[:4] == b'%PDF' and 'lp was not found' in boxed(d[1]), d[1])
out2 = os.path.join(run, 'h2.pdf')
d = edit(env, path, FINAL + ['text:' + out2, 'INS'] + WAIT + ['show'])
check('missing lp: with File only the PDF is made and there is no complaint',
    shows(d[0], 'Wrote ', out2) and 'not found' not in d[0], d[0])

run, t, log, env = setup('nostrprint', STRATA_STRPRINT='/no/such/strprint')
path = fresh(run, cns)
out = os.path.join(run, 'i.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'INS', 'wait', 'show'])
check('missing strprint: a popup says what is needed',
    'Print needs strprint' in d[0] and not os.path.exists(out), d[0])
run, t, log, env = setup('nolatex', PATH=nopath)
path = fresh(run, cns)
out = os.path.join(run, 'j.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'INS', 'wait', 'show'])
check('missing LaTeX: a popup says pdflatex is needed',
    'Print needs LaTeX' in d[0] and 'pdflatex' in d[0] and
    not os.path.exists(out), d[0])

# ---- the document in the editor is printed, saved or not -------------------
run, t, log, env = setup('unsaved')
path = fresh(run, cns)
before = read(path)
out = os.path.join(run, 'u.pdf')
d = edit(env, path, ['text:ZZUNSAVEDZZ'] + FINAL + ['text:' + out, 'INS'] +
    WAIT)
check('unsaved: the text that is not saved is in the PDF',
    'ZZUNSAVEDZZ' in pdftext(out), pdftext(out)[:200])
check('unsaved: the file on the disk is not changed', read(path) == before)

# ---- Draft and Final differ ------------------------------------------------
run, t, log, env = setup('draft')
path = fresh(run, os.path.join(data, 'print_panels.doc'))
fo = os.path.join(run, 'final.pdf')
do = os.path.join(run, 'draft.pdf')
edit(env, path, FINAL + ['text:' + fo, 'INS'] + WAIT)
edit(env, path, DRAFT + ['text:' + do, 'INS'] + WAIT)
check('draft: Final and Draft both print the picture (in colour)',
    images(fo) >= 1 and images(do) == images(fo) and pages(fo) >= 1 and
    pages(do) >= 1, (images(fo), images(do)))
d = edit(env, path, DRAFT + ['show', 'ESC'])
check('draft: the panel shows what a Draft print is',
    'Draft Print' in d[0] and 'Print Double Spaced?' in d[0], d[0])

# ---- landscape and picture documents ---------------------------------------
run, t, log, env = setup('orient')
path = fresh(run, os.path.join(data, 'print_orient.doc'))
out = os.path.join(run, 'o.pdf')
edit(env, path, FINAL + ['text:' + out, 'INS'] + WAIT)
sz = sizes(out)
check('landscape: the pages of a landscape document are wider than high',
    any(w > h for w, h in sz) and any(h > w for w, h in sz), sz)
path = fresh(run, os.path.join(data, 'print_panels.doc'), 'pic.doc')
out = os.path.join(run, 'p.pdf')
edit(env, path, FINAL + ['text:' + out, 'INS'] + WAIT)
check('picture: the picture of the document is in the PDF',
    images(out) >= 1, images(out))

# ---- the command line ------------------------------------------------------
run, t, log, env = setup('cli')
doc = fresh(run, cns, 'c.doc')
rc, out, err = cli(env, ['--print', doc])
pdf = os.path.join(run, 'c.pdf')
check('cli: exit 0 and one line that says what was made',
    rc == 0 and out == 'Wrote %s (4 pages)\n' % pdf and err == '', (rc, out,
    err))
check('cli: a valid PDF with the pages that were said', pages(pdf) == 4 and
    read(pdf)[:4] == b'%PDF')
check('cli: nothing is left in the temporary directory', left_over(t) == [],
    left_over(t))
check('cli: no printer, no lp', lines(log) == [])
rc, out, err = cli(env, ['--print', doc, '-v', '-o', os.path.join(run,
    'v.pdf')])
check('cli: -v tells the pages on the standard error',
    rc == 0 and 'Page ' in err and ' of ' in err and out.startswith('Wrote '),
    (rc, out, err))
o2 = os.path.join(run, 'named.pdf')
rc, out, err = cli(env, ['--print', doc, '--printer', 'laser', '--copies',
    '4', '--sides', 'two', '--sort', 'yes', '-o', o2, '--from-page', '2',
    '--to-page', '2'])
lg = lines(log)
check('cli: the answer of lp is in the line, the exit status is 0',
    rc == 0 and 'sent it to laser (request id is fakeq-42 (1 file(s)))' in
    out, (rc, out, err))
check('cli: lp got the copies, queue, sides, order, pages and file',
    lg == ['lp|-d|laser|-n|4|-o|sides=two-sided-long-edge|-o|collate=true|'
    '-P|2-2|-t|c.doc|%s size=%d magic=%%PDF' % (o2, os.path.getsize(o2))], lg)
rc, out, err = cli(env, ['--print', doc, '--sides', 'one', '--sort', 'no',
    '--printer', 'office', '-o', o2])
check('cli: one sided and not sorted', rc == 0 and '|-o|sides=one-sided|-o|'
    'collate=false|' in lines(log)[-1], lines(log))
for name, args, code, text in (
        ('no file', ['--print'], 2, 'usage'),
        ('copies 0', ['--print', doc, '--copies', '0'], 2, 'Copies must be'),
        ('copies 100', ['--print', doc, '--copies', '100'], 2,
         'Copies must be'),
        ('copies abc', ['--print', doc, '--copies', 'abc'], 2,
         'Copies must be'),
        ('copies 2 and no printer', ['--print', doc, '--copies', '2'], 2,
         'needs a printer'),
        ('sides three', ['--print', doc, '--sides', 'three'], 2, 'one or two'),
        ('sort maybe', ['--print', doc, '--sort', 'maybe'], 2, 'yes or no'),
        ('pages without a printer', ['--print', doc, '--from-page', '1'], 2,
         'needs a printer'),
        ('page 0', ['--print', doc, '--printer', 'x', '--from-page', '0'], 2,
         'A page must be'),
        ('pages backwards', ['--print', doc, '--printer', 'x', '--from-page',
         '3', '--to-page', '1'], 2, 'first page is after'),
        ('bad queue', ['--print', doc, '--printer', '-x'], 2, 'not the name'),
        ('unknown option', ['--print', doc, '--frobnicate'], 2, 'usage'),
        ('output directory missing', ['--print', doc, '-o', '/no/dir/x.pdf'],
         2, 'Cannot find the directory'),
        ('no such document', ['--print', '/no/such.doc'], 1, 'such.doc'),
        ('section that is not there', ['--print', doc, '--from', '9.9'], 2,
         'section')):
    rc, out, err = cli(env, args)
    check('cli: %s is refused (status %d)' % (name, code),
        rc == code and text in err and out == '', (rc, out, err))
check('cli: the refused calls made nothing', left_over(t) == [] and
    len(lines(log)) == 2, (left_over(t), lines(log)))
rc, out, err = cli(dict(env, FAKELP_MODE='fail'), ['--print', doc,
    '--printer', 'laser', '-o', o2])
check('cli: an lp that fails gives status 3 and its text on stderr',
    rc == 3 and 'lp failed (status 1)' in err and 'does not exist' in err and
    out == '' and read(o2)[:4] == b'%PDF', (rc, out, err))
rc, out, err = cli(dict(env, STRATA_LP='/no/such/lp'), ['--print', doc,
    '--printer', 'laser', '-o', o2])
check('cli: no lp gives status 3 and the PDF is made',
    rc == 3 and 'lp was not found' in err, (rc, out, err))
# An envelope is sent with print-scaling=none, a letter with the default of
# the system: a scaled envelope has small type and a corner that moved.
envsrc = os.path.join(os.path.dirname(here), 'templates',
    'envelope-us-ca-9.doc')
envdoc = fresh(run, envsrc, 'env9.doc')
envout = os.path.join(run, 'env9.pdf')
nlines = len(lines(log))
rc, out, err = cli(env, ['--print', envdoc, '--printer', 'laser', '-o',
    envout])
sent = lines(log)[nlines:]
check('cli: an envelope is sent with print-scaling=none and no media option',
    rc == 0 and len(sent) == 1 and 'print-scaling=none' in sent[0] and
    'media' not in sent[0], (rc, err, sent))
nlines = len(lines(log))
rc, out, err = cli(env, ['--print', doc, '--printer', 'laser', '-o', o2])
sent = lines(log)[nlines:]
check('cli: a letter is sent with the default of the system',
    rc == 0 and len(sent) == 1 and 'print-scaling' not in sent[0],
    (rc, err, sent))
rc, out, err = cli(dict(env, STRATA_STRPRINT='/no/such/strprint'),
    ['--print', doc])
check('cli: no strprint gives status 1', rc == 1 and 'needs strprint' in err,
    (rc, out, err))
rc, out, err = cli(dict(env, PATH=nopath), ['--print', doc])
check('cli: no pdflatex gives status 1', rc == 1 and 'needs LaTeX' in err,
    (rc, out, err))
rc, out, err = cli(dict(env, PATH=fpath, PVFAKE='error', PVPDF=tpl),
    ['--print', doc, '-o', o2])
check('cli: an error of LaTeX gives status 1 and the PDF is not replaced',
    rc == 1 and 'LaTeX failed' in err and '! Fake error.' in err and
    read(o2)[:4] == b'%PDF' and left_over(t) == [], (rc, err))

sd = fresh(run, os.path.join(data, 'sort_sections.doc'), 's.doc')
so = os.path.join(run, 's.pdf')
rc, out, err = cli(env, ['--print', sd, '--from', '1.2', '--to', '1.2'])
tx = pdftext(so)
check('cli: --from and --to print those sections only',
    rc == 0 and 'Apple body' in tx and 'cherry body' not in tx, (rc, err, tx))
td = fresh(run, os.path.join(data, 'sections_se.doc'), 'sec.doc')
sp = os.path.join(run, 'sec.pdf')
cli(env, ['--print', td])
withtitle = pdftext(sp)
cli(env, ['--print', td, '--no-title'])
notitle = pdftext(sp)
check('cli: --no-title leaves out the title page', 'Title page' in withtitle
    and 'Title page' not in notitle and 'Purpose body.' in notitle,
    notitle)
cd = fresh(run, os.path.join(data, 'comment.doc'), 'cm.doc')
cp_ = os.path.join(run, 'cm.pdf')
cli(env, ['--print', cd])
fin = pdftext(cp_)
rc, out, err = cli(env, ['--print', cd, '--draft'])
dr = pdftext(os.path.join(run, 'cm-draft.pdf'))
check('cli: --draft prints the comment and makes cm-draft.pdf, final does not',
    'remember the milk' in dr and 'remember' not in fin and 'Hello' in fin,
    (dr, fin))
dd = fresh(run, os.path.join(sample, 'brief-long.doc'), 'dd.doc')
cli(env, ['--print', dd])
n1 = pages(os.path.join(run, 'dd.pdf'))
cli(env, ['--print', dd, '--double-spaced', '-o', o2])
check('cli: --double-spaced makes more pages', pages(o2) > n1, (pages(o2),
    n1))
lo = fresh(run, os.path.join(data, 'print_orient.doc'), 'ori.doc')
rc, out, err = cli(env, ['--print', lo])
sz = sizes(os.path.join(run, 'ori.pdf'))
check('cli: a landscape document is printed (wider than high pages)',
    rc == 0 and any(w > h for w, h in sz), (rc, err, sz))
pc = fresh(run, os.path.join(data, 'print_panels.doc'), 'pan.doc')
rc, out, err = cli(env, ['--print', pc])
check('cli: a document with a picture is printed with the picture',
    rc == 0 and images(os.path.join(run, 'pan.pdf')) >= 1, (rc, err))
check('cli: nothing is left of any run', left_over(t) == [], left_over(t))

# Pairs of characters that TeX would join into a ligature are printed as
# typed: two hyphens are two characters, not an en dash.
hd = fresh(run, os.path.join(data, 'print_hyphens.doc'), 'hy.doc')
rc, out, err = cli(env, ['--print', hd])
ht = pdftext(os.path.join(run, 'hy.pdf'))
check('cli: the hyphens of keycaps and text are printed as typed',
    rc == 0 and 'Keys: Alt-- Alt-+ Ctrl-- Shift-- Off Fill' in ht and
    'Text: a -- b --- c and x-y.' in ht, (rc, err, ht))
check('cli: no en dash and no em dash are made of hyphens',
    '\u2013' not in ht and '\u2014' not in ht, ht)
check('cli: the other pairs are printed as typed', ',, << >> ' in ht and
    '!\u2018 ?\u2018 ok.' in ht and '\u2018\u2018a\u2019\u2019' in ht, ht)
check('cli: words keep their ligatures', 'office fish waffle.' in ht, ht)

# A no-break space (the character 1E, or the byte FF) is never a place for a
# line to end; the control pair has a plain space and does break somewhere.
nd = fresh(run, os.path.join(data, 'print_nbsp.doc'), 'nb.doc')
rc, out, err = cli(env, ['--print', nd])
nt = pdftext(os.path.join(run, 'nb.pdf'))
ends = [l.rstrip() for l in nt.split('\n')]


def broken(word):
    return sum(1 for l in ends if l.endswith(' ' + word) or l == word)


check('cli: a no-break space is printed and keeps its two words together',
    rc == 0 and nt.replace('\u00a0', ' ').find('Mr. Smith, 10 km.') >= 0,
    (rc, err, nt[:300]))
check('cli: no line ends between the words of a pair tied by 1E',
    rc == 0 and broken('alpha') == 0 and 'alpha' in nt, broken('alpha'))
check('cli: no line ends between the words of a pair tied by FF',
    rc == 0 and broken('iota') == 0 and 'iota' in nt, broken('iota'))
check('cli: the control pair with a plain space is broken at a line end',
    broken('gamma') > 0, broken('gamma'))

# A no-break space keeps two words on one line.  The document has 80 shifts of
# the line, each with a pair tied by the character 1E, a pair tied by the byte
# FF and a pair with a plain space as the control: the plain pair is split
# between two lines at some shift (else the test shows nothing), the tied
# pairs never.
nd = fresh(run, os.path.join(data, 'print_nbsp.doc'), 'nb.doc')
rc, out, err = cli(env, ['--print', nd])
nt = pdftext(os.path.join(run, 'nb.pdf'))
ends = [l.rstrip() for l in nt.split('\n')]


def split_pairs(word):
    return sum(1 for l in ends if l.endswith(word))


check('cli: a document with no-break spaces is printed', rc == 0 and
    'Smith' in nt and '10' in nt, (rc, err, nt[:300]))
check('cli: a plain space splits a pair between two lines (the control)',
    split_pairs('gamma') >= 1, split_pairs('gamma'))
check('cli: a pair tied by the character 1E is never split',
    split_pairs('alpha') == 0 and 'alpha' in nt, split_pairs('alpha'))
check('cli: a pair tied by the byte FF is never split',
    split_pairs('iota') == 0 and 'iota' in nt, split_pairs('iota'))

# The signals: SIGTERM while LaTeX runs leaves nothing and no PDF.
run, t, log, env = setup('sig', PATH=fpath, PVFAKE='slow', PVPAGES='300',
    PVPDF=tpl)
doc = fresh(run, cns, 'c.doc')
saved = apply_env(env)
p = subprocess.Popen([stred, '--print', doc], stdout=subprocess.PIPE,
    stderr=subprocess.PIPE)
restore_env(saved)
time.sleep(1.5)
check('cli signal: the run has its files while it works', left_over(t) != [],
    left_over(t))
p.send_signal(signal.SIGTERM)
try:
    p.wait(10)
except subprocess.TimeoutExpired:
    p.kill()
    p.wait()
check('cli signal: stred ends by the signal', p.returncode == -signal.SIGTERM,
    p.returncode)
time.sleep(0.3)
check('cli signal: nothing is left and no process either',
    left_over(t) == [] and procs_of(t) == '' and
    not os.path.exists(os.path.join(run, 'c.pdf')), (left_over(t),
    procs_of(t)))

# ---- str: Setup ------------------------------------------------------------
run, t, log, env = setup('strsetup')
home = env['STRATA_HOME']
d = mgr(env, tui.OPEN_SETUP + ['show'], run)
check('str: Setup has Print To, Printer Queue and PDF Directory, not a '
    'Printer field or the three drivers',
    'Print To:' in d[0] and 'Printer Queue:' in d[0] and
    'PDF Directory:' in d[0] and 'Printer:' not in d[0].replace(
    'Print To:', '').replace('Printer Queue:', '') and
    'Primary Printer Driver' not in d[0] and
    'Secondary Printer Driver' not in d[0] and 'Port:' not in d[0], d[0])
d = mgr(env, tui.OPEN_SETUP + ['DOWN'] * 4 + ['LEFT', 'DOWN', 'F2',
    'show', 'DOWN', 'DOWN', 'ENTER', 'INS', 'wait', 'show'], run)
conf = read(os.path.join(home, 'config'), 'r')
check('str: the queue of Setup is chosen from the list of the system and '
    'kept in ~/.strata/config', 'laser' in d[0] and 'office' in d[0] and
    'print_to=printer' in conf and 'print_queue=office' in conf, (d[0], conf))
d = mgr(env, tui.OPEN_SETUP + ['DOWN'] * 5 + ['text:ab', 'show'], run)
check('str: a queue name cannot be made up in Setup: it is a choice',
    any('Printer Queue:' in l and 'office' in l for l in d[0].split('\n')),
    d[0])
d = edit(env, fresh(run, cns, 'ed.doc'), FINAL + ['show'])
check('editor: the Printer of the panel starts as the one of Setup',
    any('Printer:' in l and 'office' in l for l in d[0].split('\n')), d[0])

# ---- PDF viewer: the PDF is made, then the program that opens a PDF ------
# fakeopen.sh stands for that program: the setting pdf_viewer names it, or it
# is xdg-open in PATH.  It records what it was given in FAKEOPEN_LOG.
# The setting names a program that others cannot change (the policy of
# Setup), so the stand-in is copied to a folder of its own, mode 0755.
safebin = tempfile.mkdtemp(prefix='safebin')
fakeopen = os.path.join(safebin, 'fakeopen.sh')
shutil.copy(os.path.join(here, 'fakeopen.sh'), fakeopen)
os.chmod(fakeopen, 0o755)


def put_config(env, text):
    home = env['STRATA_HOME']
    os.makedirs(home, mode=0o700, exist_ok=True)
    with open(os.path.join(home, 'config'), 'w') as f:
        f.write(text)
    os.chmod(os.path.join(home, 'config'), 0o600)


def opened(log, count=1):
    """The lines of the log of the opener, after a moment: it is started
    without being waited for."""
    for _ in range(100):
        got = lines(log)
        if len(got) >= count:
            return got
        time.sleep(0.05)
    return lines(log)


def open_setup(name, **extra):
    more = {'DISPLAY': ':99', 'WAYLAND_DISPLAY': None,
            'FAKEOPEN_SLEEP': None}
    more.update(extra)
    run, t, lplog, env = setup(name, **more)
    oplog = os.path.join(run, 'open.log')
    env['FAKEOPEN_LOG'] = oplog
    put_config(env, 'pdf_viewer=%s\n' % fakeopen)
    return run, t, lplog, oplog, env


run, t, lplog, oplog, env = open_setup('opn', FAKEOPEN_SLEEP='3')
doc = os.path.join(data, 'plain_text.doc')
out = os.path.join(run, 'o1.pdf')
rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer', '-o',
    out])
got = opened(oplog)
check('open: the Printer "PDF viewer" makes the PDF and opens it',
    rc == 0 and read(out)[:4] == b'%PDF' and so.startswith('Wrote ' + out)
    and so.rstrip().endswith('and opened it to print'), (rc, so, se))
check('open: the program of the setting got the full path of the PDF',
    got == ['fakeopen.sh|%s size=%d magic=%%PDF' % (out,
    os.path.getsize(out))], got)
check('open: the program is not waited for (it sleeps three seconds)',
    procs_of(fakeopen) != '', procs_of(fakeopen))
check('open: lp is not run', lines(lplog) == [], lines(lplog))
time.sleep(3.2)

run, t, lplog, oplog, env = open_setup('opn2')
out = os.path.join(run, 'o2.pdf')
rc, so, se = cli(env, ['--print', doc, '--printer', 'File only', '-o', out])
time.sleep(0.5)
check('open: with File only the PDF is made and nothing is opened',
    rc == 0 and read(out)[:4] == b'%PDF' and 'opened' not in so and
    lines(oplog) == [], (rc, so, lines(oplog)))
out = os.path.join(run, 'o2d.pdf')
rc, so, se = cli(env, ['--print', doc, '--draft', '--printer',
    'open to PRINT', '-o', out])
got = opened(oplog)
check('open: a Draft opens its PDF too, and the words need no capitals',
    rc == 0 and so.rstrip().endswith('and opened it to print') and
    got == ['fakeopen.sh|%s size=%d magic=%%PDF' % (out,
    os.path.getsize(out))], (rc, so, got))

run, t, lplog, oplog, env = open_setup('opn3')
put_config(env, 'pdf_viewer=/no/such/opener\n')
out = os.path.join(run, 'o3.pdf')
rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer', '-o',
    out])
check('open: a program of the setting that is not there: the PDF is saved '
    'and the message shows so (it is no failure)', rc == 0 and
    read(out)[:4] == b'%PDF' and 'but the program of the setting '
    'pdf_viewer was not found: it is saved' in so and lines(oplog) == [],
    (rc, so, se))

# The programs of the system: a PDF viewer is the first of a list in the
# fixed directories /usr/bin, /usr/local/bin and /bin, and NEVER one that is
# in PATH.  The guard of tests/shadowbin under the name evince stands for a
# poisoned program in PATH; the test allows it to be called, and it would
# record the call like the fake opener.  Where the machine has a real viewer
# in a fixed directory, the checks that need none are not made.
viewbin = tempfile.mkdtemp(prefix='viewbin')
shutil.copy(os.path.join(SHADOWBIN, 'shadow'), os.path.join(viewbin,
    'evince'))
os.chmod(os.path.join(viewbin, 'evince'), 0o755)
viewpath = viewbin + ':' + os.environ.get('PATH', '')
run, t, lplog, oplog, env = open_setup('opn4', SHADOW_ALLOW='1',
    PATH=viewpath)
put_config(env, '')
out = os.path.join(run, 'o4.pdf')
rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer', '-o',
    out])
got = opened(oplog)
sysview = any(os.path.exists(os.path.join(d, v)) for d in ('/usr/bin',
    '/usr/local/bin', '/bin') for v in ('evince', 'okular', 'atril',
    'zathura', 'qpdfview', 'mupdf', 'xpdf', 'epdfview'))
time.sleep(0.5)
check('open: without the setting a viewer that is only in PATH (here a '
    'poisoned evince) is never run: the PDF is saved and the message shows '
    'no viewer was found', sysview or (rc == 0 and
    read(out)[:4] == b'%PDF' and 'but no PDF viewer was found: set one in '
    'Setup: it is saved' in so and lines(oplog) == []), (rc, so, got))
run, t, lplog, oplog, env = open_setup('opn5', SHADOW_ALLOW='1',
    DISPLAY=None, PATH=viewpath)
put_config(env, '')
out = os.path.join(run, 'o5.pdf')
rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer', '-o',
    out])
time.sleep(0.5)
check('open: without a display the PDF is saved and the message shows so',
    rc == 0 and read(out)[:4] == b'%PDF' and 'but no PDF viewer was found: '
    'set one in Setup: it is saved' in so and lines(oplog) == [],
    (rc, so, se))
# A PATH of the directories of this machine that hold no program that shows
# PDF files (and none of the guards).
VIEWERS = ('evince', 'okular', 'atril', 'zathura', 'qpdfview', 'mupdf', 'xpdf',
    'epdfview')
cleanbin = newdir('cleanbin')
for d in os.environ.get('PATH', '').split(':'):
    if d == SHADOWBIN or not os.path.isdir(d):
        continue
    for n in os.listdir(d):
        if n in VIEWERS or n in ('xdg-open', 'gio', 'open') or \
                os.path.lexists(os.path.join(cleanbin, n)):
            continue
        os.symlink(os.path.join(d, n), os.path.join(cleanbin, n))
cleanpath = cleanbin
run, t, lplog, oplog, env = open_setup('opn6', PATH=cleanpath, NO_SHADOW='1')
put_config(env, '')
out = os.path.join(run, 'o6.pdf')
rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer', '-o',
    out])
check('open: no program that shows PDF files: the PDF is saved and the '
    'message shows so', sysview or (rc == 0 and read(out)[:4] == b'%PDF' and
    'but no PDF viewer was found: set one in Setup: it is saved' in so),
    (rc, so, se))

# A word processor as the opener is refused, and not started.
for word in ('soffice', 'libreoffice', 'lowriter', 'wps'):
    run, t, lplog, oplog, env = open_setup('opn6' + word)
    fakeword = os.path.join(run, word)
    os.symlink(fakeopen, fakeword)
    put_config(env, 'pdf_viewer=%s\n' % fakeword)
    out = os.path.join(run, 'o6.pdf')
    rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer',
        '-o', out])
    time.sleep(0.5)
    check('open: the setting pdf_viewer = %s is refused: the PDF is saved, '
        'nothing is started and the message is that of no viewer' % word,
        rc == 0 and read(out)[:4] == b'%PDF' and 'but no PDF viewer was '
        'found: set one in Setup: it is saved' in so and lines(oplog) == [],
        (rc, so, se, lines(oplog)))

run, t, lplog, oplog, env = open_setup('opn7')
out = os.path.join(run, 'o7.pdf')
rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer',
    '--copies', '2', '-o', out])
check('open: copies need a real queue, and the message shows why', rc == 2 and
    'More than one copy needs a printer' in se and 'print dialog' in se and
    not os.path.exists(out), (rc, so, se))
rc, so, se = cli(env, ['--print', doc, '--printer', 'Open  to print', '-o',
    out])
check('open: other words with a space are not a queue', rc == 2 and
    'not the name of a printer queue' in se, (rc, so, se))
rc, so, se = cli(env, ['--print', doc, '--printer', 'PDF viewer',
    '--double-spaced', '-o', out])
got = opened(oplog)
check('open: Print Double Spaced works with it, because it changes the PDF',
    rc == 0 and len(got) == 1, (rc, so, se, got))

# The panel of the Editor: Final and Draft.
run, t, lplog, oplog, env = open_setup('opn8')
path = fresh(run, cns)
out = os.path.join(run, 'p1.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'DOWN', 'F2', 'show', 'DOWN',
    'ENTER', 'INS'] + WAIT + ['show'])
check('panel: the list of the Printer field has PDF viewer',
    all(w in d[0] for w in ('File only', 'PDF viewer', 'laser',
    'office')), d[0])
got = opened(oplog)
check('panel: Final with PDF viewer makes the PDF and opens it',
    read(out)[:4] == b'%PDF' and shows(d[1], 'Wrote ', out) and
    'and opened it to print' in d[1] and len(got) == 1 and
    got[0].startswith('fakeopen.sh|' + out + ' '), (d[1], got))
out = os.path.join(run, 'p2.pdf')
d = edit(env, path, DRAFT + ['text:' + out, 'DOWN', 'F2', 'DOWN', 'ENTER',
    'INS'] + WAIT + ['show'])
got = opened(oplog, 2)
check('panel: Draft with PDF viewer makes the PDF and opens it',
    read(out)[:4] == b'%PDF' and 'and opened it to print' in d[0] and
    len(got) == 2 and got[1].startswith('fakeopen.sh|' + out + ' '),
    (d[0], got))
out = os.path.join(run, 'p3.pdf')
d = edit(env, path, FINAL + ['text:' + out, 'DOWN', 'F2', 'DOWN', 'ENTER',
    'DOWN', 'text:2', 'INS', 'show'])
check('panel: copies are refused with PDF viewer, and the panel stays',
    'More than one copy needs a printer' in d[0] and
    not os.path.exists(out), d[0])

# Setup of the Document Manager: PDF viewer as the default.
run, t, lplog, oplog, env = open_setup('opn9')
home = env['STRATA_HOME']
d = mgr(env, tui.OPEN_SETUP + ['DOWN'] * 4 + ['F2', 'show', 'DOWN',
    'DOWN', 'ENTER', 'INS', 'wait', 'show'], run)
conf = read(os.path.join(home, 'config'), 'r')
check('str: PDF viewer is in the list of Print To of Setup and is kept '
    'in the settings, with the other keys', 'PDF viewer' in d[0] and
    'print_to=viewer' in conf and 'pdf_viewer=' + fakeopen in conf,
    (d[0], conf))
d = edit(env, fresh(run, cns, 'ed.doc'), FINAL + ['show'])
check('editor: the Printer of the panel starts as PDF viewer',
    any('Printer:' in l and 'PDF viewer' in l for l in d[0].split('\n')),
    d[0])

# ---- the markers Filename, Pathname, Filedate and Filetime ------------------
# They show the file that is printed: its name, its path and its time, not
# the copy that the Printer is given, and a document with no file keeps its
# labels.  The file is given a fixed time of the day, so that the text is
# known.
run, t, log, env = setup('fname')
fdoc = (b'*R3\r\nMR1001\r\nPP\\1\\\x00\r\nP1C33\r\nFHA1\r\nFTT5\r\n'
    b'DP0,8500,11000\r\n*B1\r\nS\r\n+\r\nS\r\nTONE\x1d\x00\r\nB\r\n'
    b'TNAME \x1313(\x13)\x1d PATH \x1324(\x13)\x1d DATE \x1312(\x13)\x1d '
    b'TIME \x1314(\x13)\x1d\x00\r\nS\r\nTTWO\x1d\x00\r\n-\r\n*E\r\n')
sub = os.path.join(run, 'where', 'deep')
os.makedirs(sub)
named = os.path.join(sub, 'My Report.doc')
open(named, 'wb').write(fdoc)
stamp = time.mktime((2001, 2, 3, 4, 5, 0, 0, 0, -1))
os.utime(named, (stamp, stamp))


def flat(text):
    """The text of a PDF on one line, with the line breaks of a long path
    and the hyphens of the breaks taken out."""
    return ''.join(text.split())


def shown(text):
    f = flat(text)
    return ('MyReport.doc' in f or 'MYREPORT.DOC' in f,
        flat(sub) in f or flat(os.path.realpath(sub)) in f,
        'February3,2001' in f, '4:05am' in f)


rc, out, err = cli(env, ['--print', named, '-o', os.path.join(run, 'f.pdf')])
text = pdftext(os.path.join(run, 'f.pdf'))
check('markers: a print from a file shows its name, path, date and time',
    rc == 0 and all(shown(text)) and 'part.doc' not in text.lower(),
    (shown(text), text[:400]))
rc, out, err = cli(env, ['--print', named, '--from', '1', '--to', '1', '-o',
    os.path.join(run, 'g.pdf')])
text = pdftext(os.path.join(run, 'g.pdf'))
check('markers: a print of a part (a range of sections) shows the real file',
    rc == 0 and all(shown(text)) and 'part.doc' not in text.lower(),
    (shown(text), text[:400]))
epdf = os.path.join(run, 'h.pdf')
edit(env, named, FINAL + ['text:' + epdf, 'INS'] + WAIT)
text = pdftext(epdf)
check('markers: Print, Final of the Editor shows the real file',
    all(shown(text)) and 'part.doc' not in text.lower(),
    (shown(text), text[:400]))
# A document with no file name of its own: the labels, not a copy.
sprint = os.path.join(bdir, 'term', 'strprint')
if os.path.exists(sprint):
    part = os.path.join(run, 'part.doc')
    shutil.copy(named, part)
    p = subprocess.run([sprint, part, '-o', os.path.join(run, 'n.pdf'),
        '--source-path', ''], capture_output=True, text=True)
    text = pdftext(os.path.join(run, 'n.pdf'))
    f = flat(text)
    check('markers: with no file the Printer prints FILENAME and PATHNAME',
        p.returncode == 0 and 'FILENAME' in f and 'PATHNAME' in f and
        'part.doc' not in text.lower() and '2001' not in f, (p.stderr,
        text[:300]))
    p = subprocess.run([sprint, part, '-o', os.path.join(run, 'm.pdf'),
        '--source-path', named], capture_output=True, text=True)
    check('markers: strprint --source-path names the file of the markers',
        p.returncode == 0 and all(shown(pdftext(os.path.join(run,
        'm.pdf')))), p.stderr)
    p = subprocess.run([sprint, named, '-o', os.path.join(run, 'k.pdf')],
        capture_output=True, text=True)
    check('markers: strprint on a file with no option shows that file',
        p.returncode == 0 and all(shown(pdftext(os.path.join(run,
        'k.pdf')))), p.stderr)

# ---- the settings Print To, Printer Queue and PDF Directory (a block of its
# own): the Print panel and stred --print follow them; an old settings file
# with only the key printer keeps its meaning.  lp and lpstat are the fakes.
def printer_of(screen):
    for l in screen.split('\n'):
        if 'Printer:' in l:
            return l
    return ''


def output_of(screen):
    for l in screen.split('\n'):
        if 'Output File:' in l:
            return l
    return ''


for conf, want in (
        ('print_to=printer\nprint_queue=office\n', 'office'),
        ('print_to=printer\n', 'System default printer'),
        ('print_to=open\n', 'PDF viewer'),
        ('print_to=file\n', 'File only'),
        ('printer=laser\n', 'laser'),
        ('printer=PDF viewer\n', 'PDF viewer'),
        ('printer=\n', 'File only'),
        ('', 'File only')):
    run, t, log, env = setup('pt')
    put_config(env, conf)
    path = fresh(run, cns)
    d = edit(env, path, FINAL + ['show', 'ESC'])
    check('panel: the settings %r give the Printer %s' % (conf, want),
        want in printer_of(d[0]), d[0])

run, t, log, env = setup('ptnone', FAKELPSTAT_QUEUES='')
put_config(env, 'print_to=printer\nprint_queue=laser\n')
path = fresh(run, cns)
d = edit(env, path, FINAL + ['show', 'ESC'])
check('panel: Print To = Printer and no queue on the system: File only, and '
    'the message shows so', 'File only' in printer_of(d[0]) and
    'No printer is available' in d[0], d[0])

# The default of the Output File: the directory of the document, or the PDF
# Directory when it can be used.
run, t, log, env = setup('pd')
path = fresh(run, cns)
put_config(env, '')
d = edit(env, path, FINAL + ['show', 'ESC'])
check('panel: with no PDF Directory the Output File is in the directory of '
    'the document', output_of(d[0]).rstrip('\u00b7 \u2551').endswith(
    'work.pdf') and '/pdfs/' not in output_of(d[0]), d[0])
pdir = os.path.join(run, 'pdfs')
os.makedirs(pdir)
put_config(env, 'pdf_dir=%s\n' % pdir)
d = edit(env, path, FINAL + ['show', 'ESC'])
check('panel: with a PDF Directory the Output File is NAME.pdf in it',
    '/pdfs/work.pdf' in output_of(d[0]), d[0])
d = edit(env, path, DRAFT + ['show', 'ESC'])
check('panel: and NAME-draft.pdf for a Draft',
    '/pdfs/work-draft.pdf' in output_of(d[0]), d[0])
put_config(env, 'pdf_dir=~/pdfs\n')
home = os.path.join(run, 'userhome')
os.makedirs(os.path.join(home, 'pdfs'))
env['HOME'] = home
d = edit(env, path, FINAL + ['show', 'ESC'])
check('panel: ~/ in the PDF Directory is the home directory',
    '/userhome/pdfs/work.pdf' in output_of(d[0]), d[0])
put_config(env, 'pdf_dir=%s\n' % os.path.join(run, 'nothere'))
d = edit(env, path, FINAL + ['show', 'ESC'])
check('panel: a PDF Directory that does not exist: the directory of the '
    'document is used, and the message shows so',
    '/nothere/' not in output_of(d[0]) and
    'does not exist' in d[0] and "document's directory" in d[0], d[0])
put_config(env, 'pdf_dir=relative/dir\n')
d = edit(env, path, FINAL + ['show', 'ESC'])
check('panel: a relative PDF Directory is not used either',
    '/relative/' not in output_of(d[0]) and 'relative' in d[0], d[0])
ro = os.path.join(run, 'ro')
os.makedirs(ro)
os.chmod(ro, 0o555)
put_config(env, 'pdf_dir=%s\n' % ro)
d = edit(env, path, FINAL + ['show', 'ESC'])
check('panel: a PDF Directory that cannot be written: the document\'s and a '
    'message', '/ro/' not in output_of(d[0]) and
    'cannot be written' in d[0], d[0])
os.chmod(ro, 0o755)
env.pop('HOME', None)

# stred --print without --printer goes where Setup shows.
def lpcalls(log):
    return [l.split('|')[1:] for l in lines(log)]


run, t, log, env = setup('cp1')
put_config(env, 'print_to=printer\nprint_queue=office\n')
out = os.path.join(run, 'c1.pdf')
rc, so, se = cli(env, ['--print', cns, '-o', out])
lg = lines(log)
check('cli: Print To = Printer and the queue office: lp -d office',
    rc == 0 and len(lg) == 1 and lg[0].startswith('lp|-d|office|'), (rc, lg))
run, t, log, env = setup('cp2')
put_config(env, 'print_to=printer\n')
out = os.path.join(run, 'c2.pdf')
rc, so, se = cli(env, ['--print', cns, '-o', out])
lg = lines(log)
check('cli: Printer with the default queue of the system: lp with no -d',
    rc == 0 and len(lg) == 1 and lg[0].startswith('lp|-n|1|') and
    '-d' not in lg[0].split('|'), (rc, lg))
run, t, log, env = setup('cp3')
put_config(env, 'printer=laser\n')
out = os.path.join(run, 'c3.pdf')
rc, so, se = cli(env, ['--print', cns, '-o', out])
lg = lines(log)
check('cli: an old settings file with printer=laser: lp -d laser',
    rc == 0 and len(lg) == 1 and lg[0].startswith('lp|-d|laser|'), (rc, lg))
run, t, log, env = setup('cp4')
put_config(env, 'print_to=printer\nprint_queue=laser\n')
out = os.path.join(run, 'c4.pdf')
rc, so, se = cli(env, ['--print', cns, '-o', out, '--printer', 'office'])
lg = lines(log)
check('cli: --printer wins over the settings',
    rc == 0 and len(lg) == 1 and lg[0].startswith('lp|-d|office|'), (rc, lg))
run, t, log, env = setup('cp5')
put_config(env, 'print_to=printer\nprint_queue=laser\n')
out = os.path.join(run, 'c5.pdf')
rc, so, se = cli(env, ['--print', cns, '-o', out, '--printer', 'File only'])
check('cli: --printer "File only" makes a PDF and prints nothing',
    rc == 0 and lines(log) == [] and read(out)[:4] == b'%PDF', (rc, so, se))
run, t, log, env = setup('cp6', FAKELPSTAT_QUEUES='')
put_config(env, 'print_to=printer\nprint_queue=laser\n')
out = os.path.join(run, 'c6.pdf')
rc, so, se = cli(env, ['--print', cns, '-o', out])
check('cli: Print To = Printer and no queue on the system: the PDF is saved, '
    'nothing is sent, and the message shows so', rc == 0 and lines(log) == []
    and read(out)[:4] == b'%PDF' and 'No printer is available' in se,
    (rc, so, se))
run, t, log, env = setup('cp7')
pdir = os.path.join(run, 'pdfs')
os.makedirs(pdir)
put_config(env, 'pdf_dir=%s\n' % pdir)
docpath = fresh(run, cns, 'cp7.doc')
rc, so, se = cli(env, ['--print', docpath])
check('cli: with no -o the PDF is NAME.pdf in the PDF Directory',
    rc == 0 and read(os.path.join(pdir, 'cp7.pdf'))[:4] == b'%PDF' and
    not os.path.exists(os.path.join(run, 'cp7.pdf')), (rc, so, se,
    os.listdir(run)))
run, t, log, env = setup('cp8')
put_config(env, 'pdf_dir=%s\n' % os.path.join(run, 'nothere'))
docpath = fresh(run, cns, 'cp8.doc')
rc, so, se = cli(env, ['--print', docpath])
check('cli: a PDF Directory that does not exist: the directory of the '
    'document, and a message', rc == 0 and
    read(os.path.join(run, 'cp8.pdf'))[:4] == b'%PDF' and
    'does not exist' in se, (rc, so, se))

# No test started a real program that opens a PDF, and none is left over.
check('guard: no call of xdg-open, gio or open that a test did not expect',
    read(os.environ['SHADOW_LOG'], 'r') == '',
    read(os.environ['SHADOW_LOG'], 'r'))
left = subprocess.run(['pgrep', '-f', tmp], capture_output=True,
    text=True).stdout.split()
left = [x for x in left if x != str(os.getpid())]
check('guard: no process of a test is left', left == [], left)

shutil.rmtree(tmp, ignore_errors=True)
print('printpdf: %d failures' % fails)
sys.exit(1 if fails else 0)
