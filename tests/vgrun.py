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

"""Runs the programs under valgrind memcheck and prints a table.

A benchmark, not a test of the smoke runner: valgrind is slow.  Each scenario
starts a program under valgrind (the Editor and the Document Manager through
tests/tui.py with the variable STRATA_WRAP, the Printer and the Viewer
directly), and the table says for each scenario the errors that memcheck
found, the bytes that are definitely lost and the bytes that are possibly
lost.  Memory that the program still holds when it ends is not a leak of a
command and is not counted (docs/VALGRIND.txt).

    VALGRIND=PREFIX/bin/valgrind STRATA_BUILD_DIR=BUILD \\
        python3 -I -B tests/vgrun.py [--log DIR] [--only WORD] [--dry]

The log of every process is DIR/SCENARIO.PID.  --dry runs the Editor
scenarios without valgrind and shows the last screen of each (to see that a
scenario reaches what it means to).  TMPDIR and STRATA_HOME should be in a
scratch folder.  The suppression file is tests/vg.supp.
"""
import sys as _s; _s.dont_write_bytecode = True
import glob, os, re, shutil, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui

build = os.environ.get('STRATA_BUILD_DIR', '')
valgrind = os.environ.get('VALGRIND', 'valgrind')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
                                                   'strprint'))
dviview = os.environ.get('DVIVIEW', os.path.join(build, 'term', 'dviview'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
x11dir = os.path.join(build, 'x11')
samples = os.path.join(os.path.dirname(here), 'tests', 'samples')
data = os.path.join(here, 'data')
supp = os.path.join(here, 'vg.supp')

args = sys.argv[1:]
logdir = None
only = None
dry = False
while args:
    if args[0] == '--log':
        logdir = args[1]
        args = args[2:]
    elif args[0] == '--only':
        only = args[1]
        args = args[2:]
    elif args[0] == '--dry':
        dry = True
        args = args[1:]
    else:
        print(__doc__)
        sys.exit(2)
tmp = tempfile.mkdtemp(prefix='vgrun')
logdir = logdir or os.path.join(tmp, 'logs')
os.makedirs(logdir, exist_ok=True)
os.environ.setdefault('STRATA_HOME', os.path.join(tmp, 'home'))
os.makedirs(os.environ['STRATA_HOME'], exist_ok=True)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'

VG_OPTS = ['--error-exitcode=9', '--leak-check=full', '--track-origins=yes',
           '--num-callers=16', '--suppressions=' + supp]


def vg(name, trace=False):
    """The valgrind command words for a scenario."""
    return [valgrind] + VG_OPTS + ['--log-file=%s/%s.%%p' % (logdir, name)] + \
        (['--trace-children=yes'] if trace else [])


def copy(src, name=None):
    """A copy of a document in a folder of its own."""
    d = tempfile.mkdtemp(prefix='doc', dir=tmp)
    path = os.path.join(d, name or os.path.basename(src))
    shutil.copy(src, path)
    for sub in ('pics',):
        if os.path.isdir(os.path.join(data, sub)):
            shutil.copytree(os.path.join(data, sub), os.path.join(d, sub))
    shutil.copy(os.path.join(data, 'picture.png'), d)
    return path


PLAN = os.path.join(samples, 'plan-title-table.doc')
MEMO = os.path.join(samples, 'memo-date-table.doc')
REPORT = os.path.join(samples, 'report-plain.doc')
FEATURES = os.path.join(data, 'print_features.doc')
MARKERS = os.path.join(data, 'markers.doc')
SPELLDOC = os.path.join(data, 'spell.doc')
TABLEDOC = os.path.join(data, 'table_rows.doc')

# The columns of the marker grid hold 13, 13 and 13 entries; entry i is at
# the row i % 13 and the column i // 13.  Each marker is made with the panel
# left as it is (Ins), and the panel is left with Esc when it keeps open.


def marker_keys(first, last):
    keys = []
    for i in range(first, last):
        keys += ['C-F12', 'wait'] + ['RIGHT'] * (i // 13) + \
                ['DOWN'] * (i % 13) + ['ENTER', 'wait', 'INS', 'wait',
                                       'ESC', 'ESC', 'ESC']
    return keys


EDIT = ['F12', 'text:E']
SCENARIOS = [
    ('ed-open-type', PLAN, ['text:hello world', 'ENTER', 'text:two',
                            'BKSP', 'BKSP', 'ENTER', 'UP', 'DOWN', 'PGDN',
                            'PGUP', 'C-END', 'C-HOME']),
    ('ed-search', PLAN, ['F9', 'text:Orchard', 'INS', 'wait', 'F9',
                         'text:zzzz', 'INS', 'wait']),
    ('ed-replace', PLAN, ['F12', 'text:E', 'wait', 'ESC', 'F9',
                          'text:Orchard', 'INS', 'wait', 'F12',
                          'text:E', 'text:R', 'wait', 'ESC']),
    ('ed-block', PLAN, ['DOWN', 'F3', 'DOWN', 'DOWN', 'ENTER', 'wait',
                        'F5', 'wait', 'UP', 'F4', 'DOWN', 'ENTER', 'wait',
                        'F7', 'DOWN', 'ENTER', 'wait', 'F5']),
    ('ed-undo-redo', PLAN, ['text:abc', 'ENTER', 'text:def', 'C-F10',
                            'C-F10', 'C-F10', 'C-F11', 'C-F11', 'C-F6',
                            'ENTER', 'ENTER', 'ENTER', 'C-F10']),
    ('ed-markers-a', REPORT, marker_keys(0, 8)),
    ('ed-markers-b', REPORT, marker_keys(8, 16)),
    ('ed-markers-c', REPORT, marker_keys(16, 24)),
    ('ed-markers-d', REPORT, marker_keys(24, 32)),
    ('ed-markers-e', REPORT, marker_keys(32, 39)),
    ('ed-markers-file', MARKERS, ['wait', 'C-END', 'C-HOME', 'F12',
                                  'text:E', 'wait', 'ESC']),
    ('ed-tables', TABLEDOC, ['F8', 'wait', 'DOWN', 'ENTER', 'wait', 'ESC',
                             'RIGHT', 'RIGHT', 'text:x', 'A-F2', 'wait',
                             'ESC', 'A-F3', 'wait', 'ESC']),
    ('ed-borders', PLAN, ['DOWN', 'DOWN', 'DOWN', 'F12', 'text:E', 'wait',
                          'ESC', 'F6', 'wait', 'ESC', 'C-F6', 'ESC']),
    ('ed-pictures', FEATURES, ['wait', 'C-END', 'C-HOME', 'PGDN', 'PGDN',
                               'wait', 'C-F12', 'wait', 'ESC']),
    ('ed-structure', PLAN, EDIT + ['text:S', 'wait', 'wait'] + EDIT +
     ['text:S', 'wait', 'wait', 'text:q', 'C-s']),
    ('ed-save', PLAN, ['text:x', 'C-s', 'wait', 'C-s', 'wait', 'text:y',
                       'C-s', 'wait']),
    ('ed-print-panel', PLAN, ['F12', 'text:P', 'wait', 'ESC', 'F12',
                              'text:P', 'RIGHT', 'ENTER', 'wait', 'DOWN',
                              'DOWN', 'ESC']),
    ('ed-spell', SPELLDOC, ['F12', 'text:t', 'text:s', 'text:d', 'wait',
                            'INS', 'wait', 'wait', 'text:Q', 'wait']),
    ('ed-thesaurus', PLAN, ['C-t', 'wait', 'wait', 'ESC', 'F12', 'text:t',
                            'text:t', 'wait', 'ESC']),
    ('ed-menus', PLAN, ['F12', 'wait', 'RIGHT', 'wait', 'RIGHT', 'wait',
                        'RIGHT', 'wait', 'RIGHT', 'wait', 'RIGHT', 'wait',
                        'ESC', 'F12', 'text:G', 'ENTER', 'wait', 'PGUP',
                        'PGUP', 'PGUP', 'PGUP', 'wait', 'ESC']),
    ('ed-help', PLAN, ['F1', 'wait', 'DOWN', 'ENTER', 'wait', 'ESC',
                       'C-F1', 'wait', 'ESC']),
]


def parse(name):
    """(errors, definitely lost, possibly lost, processes) from the logs of
    a scenario, summed over its processes."""
    errors = definite = possible = 0
    procs = 0
    for path in glob.glob(os.path.join(logdir, name + '.*')):
        text = open(path, errors='replace').read()
        m = re.search(r'ERROR SUMMARY: (\d+) errors', text)
        if m is None:
            continue
        procs += 1
        errors += int(m.group(1))
        m = re.search(r'definitely lost: ([\d,]+) bytes', text)
        definite += int(m.group(1).replace(',', '')) if m else 0
        m = re.search(r'possibly lost: ([\d,]+) bytes', text)
        possible += int(m.group(1).replace(',', '')) if m else 0
    return errors, definite, possible, procs


rows = []


def record(name):
    e, d, p, n = parse(name)
    rows.append((name, e, d, p, n))
    print('%-22s errors %4d  definitely %7d  possibly %7d  (%d process%s)' %
          (name, e, d, p, n, '' if n == 1 else 'es'))
    sys.stdout.flush()


def editor_scenarios():
    for name, doc, keys in SCENARIOS:
        if only and only not in name:
            continue
        path = copy(doc)
        prog = [stred, '-H', helpf, path]
        if dry:
            d = tui.session(prog, keys + ['show'], rows=30)
            print('==', name)
            print('\n'.join(d[-1].split('\n')[:4]))
            continue
        os.environ['STRATA_WRAP'] = ' '.join(vg(name))
        try:
            tui.session(prog, keys + ['wait'] * 30, rows=30, delay=0.4)
        finally:
            os.environ.pop('STRATA_WRAP', None)
        record(name)


def manager_scenarios():
    """The Document Manager: the list, the panels, a backup, the Setup."""
    d = tempfile.mkdtemp(prefix='mgr', dir=tmp)
    for n in ('a.doc', 'b.doc'):
        shutil.copy(PLAN, os.path.join(d, n))
    os.makedirs(os.path.join(d, '.backup'))
    shutil.copy(PLAN, os.path.join(d, '.backup', 'a.bak01'))
    shutil.copy(MEMO, os.path.join(d, '.backup', 'a.bak02'))
    scen = [
        ('mgr-list', ['wait', 'DOWN', 'DOWN', 'UP', 'wait']),
        ('mgr-edit', ['ENTER', 'wait', 'text:a.doc', 'INS', 'wait',
                      'wait', 'text:Z', 'C-s', 'wait', 'F12', 'text:Q',
                      'wait']),
        ('mgr-restore', ['DOWN'] * 3 + ['ENTER', 'wait', 'DOWN', 'text:a.doc',
                         'INS', 'wait', 'ENTER', 'ENTER', 'wait', 'ESC',
                         'ENTER', 'DOWN', 'ENTER', 'wait', 'ESC']),
        ('mgr-compare', ['DOWN', 'DOWN', 'ENTER', 'wait', 'text:a.doc',
                         'DOWN', 'text:b.doc', 'INS', 'wait', 'wait']),
        ('mgr-dictionary', ['DOWN', 'ENTER', 'wait', 'wait', 'ESC']),
        ('mgr-setup', ['END', 'UP', 'ENTER', 'wait', 'PGDN', 'wait', 'PGDN',
                       'wait', 'PGDN', 'wait', 'ESC']),
    ]
    cwd0 = os.getcwd()
    os.chdir(d)
    try:
        for name, keys in scen:
            if only and only not in name:
                continue
            prog = [strprog, '-H', helpf]
            if dry:
                dd = tui.session(prog, keys + ['show'], rows=30)
                print('==', name)
                print('\n'.join(dd[-1].split('\n')[:4]))
                continue
            os.environ['STRATA_WRAP'] = ' '.join(vg(name, True))
            try:
                tui.session(prog, keys + ['wait'] * 30, rows=30, delay=0.4)
            finally:
                os.environ.pop('STRATA_WRAP', None)
            record(name)
    finally:
        os.chdir(cwd0)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True,
                          errors='replace', **kw)


def unit_scenarios():
    """The unit test programs of strata-build test: the code under their
    own drivers, with the fuzz tests of the Editor among them."""
    t = os.path.join(build, 'term')
    docsrc = os.path.join(os.path.dirname(here), 'tests', 'samples')
    cases = [
        ('undotest', []), ('listtest', []), ('edtest', [docsrc, os.path.join(
            tmp, 'edtest_out.doc'), data]),
        ('lxtest', [data]), ('condtest', []), ('incltest', []),
        ('cmptest', []), ('perrtest', [data, t]), ('searchtest', []),
        ('backuptest', []), ('recovertest', [data]), ('loadtest', [data,
                                                                   docsrc]),
        ('sptest', []), ('calctest', []), ('bordertest', []),
        ('pngtest', [data]), ('dicttest', []), ('pvtest', [data]),
    ]
    for prog, extra in cases:
        sc = 'unit-' + prog
        if (only and only not in sc) or dry:
            continue
        exe = os.path.join(t, prog)
        if not os.path.exists(exe):
            print('SKIP', sc, ': not built')
            continue
        run(vg(sc) + [exe] + extra, cwd=os.path.dirname(here), timeout=7200)
        record(sc)


def make_big(path, pages=100):
    """A structured document of about PAGES pages of text (the generator of
    tests/measure.py)."""
    import measure
    words = ('the quick brown fox jumps over the lazy dog and the cat sat on '
             'the mat while it rained').split()
    paras = []
    for i in range(pages * 8):
        paras.append(' '.join(words[(i + k) % len(words)]
                              for k in range(60)))
    with open(path, 'wb') as f:
        f.write(measure.structured(paras))


def printer_scenarios():
    d = tempfile.mkdtemp(prefix='prn', dir=tmp)
    docs = []
    for sub in ('letters', 'envelopes', 'story'):
        folder = os.path.join(os.path.dirname(here), 'samples', sub)
        for p in sorted(glob.glob(os.path.join(folder, '*.doc'))):
            docs.append((sub + '-' + os.path.basename(p)[:-4], p))
    big = os.path.join(d, 'big100.doc')
    make_big(big)
    docs.append(('big100', big))
    for name, path in docs:
        work = tempfile.mkdtemp(prefix='w', dir=d)
        doc = os.path.join(work, os.path.basename(path))
        shutil.copy(path, doc)
        src = os.path.dirname(path)
        if os.path.isdir(os.path.join(src, 'pictures')):
            shutil.copytree(os.path.join(src, 'pictures'),
                            os.path.join(work, 'pictures'))
        for mode, extra in (('tex', ['--tex-only']),
                            ('pdf', ['--no-error-log'])):
            sc = 'prn-%s-%s' % (name, mode)
            if only and only not in sc:
                continue
            if dry:
                continue
            out = os.path.join(work, 'o.' + ('tex' if mode == 'tex' else
                                             'pdf'))
            r = run(vg(sc) + [strprint, doc, '-o', out] + extra, cwd=work,
                    timeout=3600)
            record(sc)
    return d


def viewer_scenario(prn):
    """The Viewer on a DVI file that the Printer made (a Preview), under a
    private X display, for a few seconds."""
    sc = 'viewer-dvi'
    if (only and only not in sc) or dry:
        return
    if shutil.which('Xvfb') is None:
        print('SKIP viewer: Xvfb is not installed')
        return
    work = tempfile.mkdtemp(prefix='v', dir=tmp)
    doc = copy(PLAN)
    dvi = os.path.join(work, 'v.dvi')
    run([strprint, doc, '--dvi', '-o', dvi, '--no-error-log'], cwd=work)
    if not os.path.exists(dvi):
        print('SKIP viewer: no DVI file')
        return
    disp = ':%d' % (90 + os.getpid() % 100)
    xv = subprocess.Popen(['Xvfb', disp, '-screen', '0', '1024x768x24'],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    env = dict(os.environ, DISPLAY=disp)
    p = subprocess.Popen(vg(sc) + [dviview, dvi], env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(25)
    if shutil.which('xdotool'):
        for k in ('Next', 'Prior', 'plus', 'minus', 'q'):
            run(['xdotool', 'key', k], env=env)
            time.sleep(1.5)
    try:
        p.wait(20)
    except subprocess.TimeoutExpired:
        p.terminate()
        p.wait()
    xv.terminate()
    xv.wait()
    record(sc)


def x11_scenario():
    """The window version of the Editor, a few keys, under a private
    display."""
    sc = 'x11-editor'
    if (only and only not in sc) or dry:
        return
    if shutil.which('Xvfb') is None or shutil.which('xdotool') is None \
            or not os.path.exists(os.path.join(x11dir, 'stred')):
        print('SKIP x11: Xvfb, xdotool or the x11 build is missing')
        return
    doc = copy(PLAN)
    disp = ':%d' % (190 + os.getpid() % 100)
    xv = subprocess.Popen(['Xvfb', disp, '-screen', '0', '1280x1024x24'],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    env = dict(os.environ, DISPLAY=disp, SHELL='/bin/true',
               XDG_CONFIG_HOME=os.path.join(tmp, 'xcfg'))
    p = subprocess.Popen(vg(sc) + [os.path.join(x11dir, 'stred'), '-H',
                                   helpf, doc], env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    win = None
    for _ in range(600):
        r = run(['xdotool', 'search', '--name', '^stred$'], env=env)
        if r.stdout.split():
            win = r.stdout.split()[0]
            break
        time.sleep(0.5)
    if win is not None:
        time.sleep(8)
        for k in ('ctrl+End', 'ctrl+Home', 'F9', 'Escape', 'F12', 'Escape',
                  'ctrl+s'):
            run(['xdotool', 'key', '--window', win, k], env=env)
            time.sleep(3)
        run(['xdotool', 'type', '--window', win, 'hello'], env=env)
        time.sleep(5)
    p.terminate()
    try:
        p.wait(60)
    except subprocess.TimeoutExpired:
        p.kill()
    xv.terminate()
    xv.wait()
    record(sc)


def main():
    if not dry and not os.path.exists(valgrind) and \
            shutil.which(valgrind) is None:
        print('no valgrind: set VALGRIND')
        return 2
    editor_scenarios()
    manager_scenarios()
    prn = printer_scenarios()
    unit_scenarios()
    viewer_scenario(prn)
    x11_scenario()
    if rows:
        print()
        print('%-24s %7s %12s %10s' % ('scenario', 'errors', 'definitely',
                                       'possibly'))
        for name, e, d, p, n in rows:
            print('%-24s %7d %12d %10d' % (name, e, d, p))
        print('%-24s %7d %12d %10d' % ('total', sum(r[1] for r in rows),
                                       sum(r[2] for r in rows),
                                       sum(r[3] for r in rows)))
    shutil.rmtree(tmp, ignore_errors=True) if not os.environ.get(
        'VGRUN_KEEP') else None
    return 1 if any(r[1] for r in rows) else 0


if __name__ == '__main__':
    sys.exit(main())
