#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The FAILED PRINT: the banner on every page, the marks at the places of
the errors, the special of the DVI, the wording of the messages and the
options.

The document is tests/data/print_errors.doc (three errors and one warning).
The real pdflatex and latex make the pages, the real strprint finds the
faults.  The pages are read with pdftotext.

    STRED=... STRPRINT=... python3 -I -B tests/banner.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
for tool in ('pdflatex', 'latex', 'dvipdfmx', 'pdfinfo', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='bnr')
data = os.path.join(here, 'data')
doc = os.path.join(data, 'print_errors.doc')
clean = os.path.join(data, 'print_orient.doc')
warn = os.path.join(data, 'print_chars.doc')
BANNER = 'PRINT FAILED - 3 ERRORS - THIS PRINT IS INACCURATE'
PREVIEW = 'PRINT FAILED - 3 ERRORS - THIS PREVIEW IS INACCURATE'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def newdir(name):
    d = os.path.join(tmp, '%s%d' % (name, len(os.listdir(tmp))))
    os.makedirs(d)
    shutil.copytree(os.path.join(data, 'pics'), os.path.join(d, 'pics'))
    shutil.copy(os.path.join(data, 'picture.png'), d)
    return d


def fresh(run, src, name='work.doc'):
    path = os.path.join(run, name)
    shutil.copy(src, path)
    return path


def pages(path):
    p = subprocess.run(['pdfinfo', path], capture_output=True, text=True)
    for l in p.stdout.split('\n'):
        if l.startswith('Pages:'):
            return int(l.split()[1])
    return 0


def pdftext(path, first=None, last=None):
    cmd = ['pdftotext', '-layout']
    if first is not None:
        cmd += ['-f', str(first), '-l', str(last)]
    return subprocess.run(cmd + [path, '-'], capture_output=True,
        text=True).stdout


def flat(s):
    return re.sub(r'\s+', ' ', s)


def run_prog(args, env=None, cwd=None):
    e = dict(os.environ)
    e.update(env or {})
    p = subprocess.run(args, capture_output=True, text=True, env=e, cwd=cwd,
        timeout=300)
    return p.returncode, p.stdout, p.stderr


# ---- a failed print: banner on every page, marks at the places -------------
run = newdir('pdf')
d1 = fresh(run, doc)
pdf = os.path.join(run, 'out.pdf')
rc, out, err = run_prog([strprint, d1, '-o', pdf])
n = pages(pdf)
check('failed print: status 4 and the PDF is made', rc == 4 and n >= 3,
    (rc, n, err))
check('failed print: stderr says PRINT FAILED with the count',
    'strprint: PRINT FAILED: 3 errors, 1 warning' in err, err)
check('banner: on every page, the Error Log page too',
    all(BANNER in flat(pdftext(pdf, p, p)) for p in range(1, n + 1)),
    [flat(pdftext(pdf, p, p))[:80] for p in range(1, n + 1)])
check('banner: it is the first line of the page, in the top margin',
    all(pdftext(pdf, p, p).lstrip().startswith('PRINT FAILED') for p in
        range(1, n + 1)))
page1 = pdftext(pdf, 2, 2)
check('marks: each error is marked in front of its place',
    re.search(r'E1\s+EQUATION left', page1) is not None and
    re.search(r'E2\s+Picture file not found:\s+pics/missing.pcx',
              page1) is not None and
    re.search(r'E4\s+EQUATION a \+', page1) is not None, page1)
check('marks: the warning is marked W, in front of its character',
    re.search(r'W3\s*\[U\+2320\]', page1) is not None, page1)
log = flat(pdftext(pdf, n, n))
check('marks: the numbers are those of the entries of the Error Log',
    '1. Error.' in log and '2. Error.' in log and '3. Warning.' in log and
    '4. Error.' in log, log)

# The banner and the marks do not move the text: same pages without them.
pdfx = os.path.join(run, 'nobanner.pdf')
rc, out, err = run_prog([strprint, d1, '-o', pdfx, '--no-banner'])
check('--no-banner: still status 4 and the same number of pages',
    rc == 4 and pages(pdfx) == n, (rc, pages(pdfx), n))
check('--no-banner: no banner on any page',
    'PRINT FAILED' not in pdftext(pdfx) and 'INACCURATE' not in pdftext(pdfx))
check('--no-banner: the marks and the Error Log stay',
    re.search(r'E1\s+EQUATION left', pdftext(pdfx, 2, 2)) is not None and
    'Error Log' in pdftext(pdfx, n, n), pdftext(pdfx, 2, 2))

# The option that turns the Error Log off does not turn off the banner.
pdfl = os.path.join(run, 'nolog.pdf')
rc, out, err = run_prog([strprint, d1, '-o', pdfl, '--no-error-log'])
check('--no-error-log: status 4, one page less, no log', rc == 4 and
    pages(pdfl) == n - 1 and 'Error Log' not in pdftext(pdfl),
    (rc, pages(pdfl), n))
check('--no-error-log: the banner is on every page',
    all(BANNER in flat(pdftext(pdfl, p, p)) for p in range(1, n)))
check('--no-error-log: the marks are in the pages',
    re.search(r'E1\s+EQUATION left', pdftext(pdfl, 2, 2)) is not None and
    re.search(r'W3\s*\[U\+2320\]', pdftext(pdfl, 2, 2)) is not None)

# A mark that carries a number the log lacks would mislead: the log off and
# the marks on still number the faults as the error file does.
errf = os.path.join(run, 'errors.txt')
rc, out, err = run_prog([strprint, d1, '-o', pdfl, '--no-error-log',
    '--errors', errf])
recs = [l for l in open(errf).read().split('\n') if l.startswith('diag ')]
check('--no-error-log: the four faults are in the error file as before',
    len(recs) == 4, recs)

# ---- the DVI: the banner shows PREVIEW, and the special is there ------------
dvi = os.path.join(run, 'out.dvi')
rc, out, err = run_prog([strprint, d1, '--dvi', '-o', dvi])
raw = open(dvi, 'rb').read()
check('DVI: status 4 and the special for the Viewer is there',
    rc == 4 and b'strata:print-failed 3' in raw, (rc, err))
pdf3 = os.path.join(run, 'fromdvi.pdf')
subprocess.run(['dvipdfmx', '-q', '-o', pdf3, dvi], capture_output=True,
    cwd=run)
check('DVI: every page has the banner with the word PREVIEW',
    os.path.exists(pdf3) and pages(pdf3) == n and
    all(PREVIEW in flat(pdftext(pdf3, p, p)) for p in range(1, n + 1)),
    flat(pdftext(pdf3))[:300] if os.path.exists(pdf3) else 'no pdf')
dvi2 = os.path.join(run, 'nb.dvi')
rc, out, err = run_prog([strprint, d1, '--dvi', '-o', dvi2, '--no-banner'])
check('DVI with --no-banner: no special, so the Viewer shows no red area',
    rc == 4 and b'strata:print-failed' not in open(dvi2, 'rb').read())
pdf4 = os.path.join(run, 'via.pdf')
rc, out, err = run_prog([strprint, d1, '--pdf-via-dvi', '-o', pdf4])
check('PDF via DVI: the banner shows PRINT, not PREVIEW',
    rc == 4 and BANNER in flat(pdftext(pdf4, 1, 1)), (rc, err))

# ---- a clean document and a document with warnings only --------------------
run = newdir('clean')
c1 = fresh(run, clean, 'c.doc')
cpdf = os.path.join(run, 'c.pdf')
rc, out, err = run_prog([strprint, c1, '-o', cpdf])
check('clean: status 0, no banner, no marks, no log',
    rc == 0 and 'PRINT FAILED' not in pdftext(cpdf) and
    'INACCURATE' not in pdftext(cpdf) and 'Error Log' not in pdftext(cpdf) and
    'PRINT FAILED' not in err, (rc, err))
tex = os.path.join(run, 'c')
run_prog([strprint, c1, '--tex-only', '-o', tex])
src = open(tex + '.tex').read()
check('clean: the LaTeX file has nothing of the banner or the marks',
    'strmark' not in src and 'print-failed' not in src and
    'INACCURATE' not in src, src[:200])
cdvi = os.path.join(run, 'c.dvi')
rc, out, err = run_prog([strprint, c1, '--dvi', '-o', cdvi])
check('clean DVI: no special', rc == 0 and
    b'strata:print-failed' not in open(cdvi, 'rb').read())
w1 = fresh(run, warn, 'w.doc')
wpdf = os.path.join(run, 'w.pdf')
rc, out, err = run_prog([strprint, w1, '-o', wpdf])
check('warnings only: status 4 but no banner and no marks',
    rc == 4 and 'PRINT FAILED' not in err and
    'INACCURATE' not in pdftext(wpdf) and
    re.search(r'\bW\d+\b', pdftext(wpdf, 1, pages(wpdf) - 1)) is None,
    (rc, err))

# ---- stred --print: the words of the messages ------------------------------
run = newdir('cli')
d4 = fresh(run, doc)
outp = os.path.join(run, 'p.pdf')
env = {'TMPDIR': run, 'STRATA_HOME': os.path.join(run, 'home'),
       'STRATA_STRPRINT': strprint}
rc, out, err = run_prog([stred, '--print', d4, '-o', outp], env=env)
check('stred --print: status 5 and the message shows PRINT FAILED',
    rc == 5 and out.startswith('PRINT FAILED: Wrote ') and
    'PRINT FAILED: the output was made' in err and
    'PRINT FAILED: 3 errors, 1 warning' in err, (rc, out, err))
check('stred --print: the PDF has the banner on every page',
    all(BANNER in flat(pdftext(outp, p, p)) for p in range(1, pages(outp) + 1)))
rc, out, err = run_prog([stred, '--print', d4, '-o', outp, '--no-banner',
    '--no-error-log'], env=env)
check('stred --print --no-banner --no-error-log: status 5, neither in pages',
    rc == 5 and 'INACCURATE' not in pdftext(outp) and
    'Error Log' not in pdftext(outp), (rc, err))
rc, out, err = run_prog([stred, '--print', fresh(run, warn, 'w.doc'), '-o',
    outp], env=env, cwd=run)
check('stred --print: only warnings say no PRINT FAILED',
    'PRINT FAILED' not in out + err, (rc, out, err))

print('banner.py: %d failures' % fails)
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
