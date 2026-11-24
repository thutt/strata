#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The errors of a document: the Printer, the Error Log, the error file,
"stred --print" and the Print Error markers of the Editor.

The document is tests/data/print_errors.doc: an equation in the old equation
language of the original, a picture that is not there, a character that no
font has and an equation that LaTeX rejects.  The real pdflatex and latex
make the pages, the real strprint finds the faults.  The PDF is read with
pdftotext and, with ERRLOG_PNG=DIR, the page of the Error Log is written as
a PNG (pdftoppm) into DIR, to be looked at.

    STRED=... STR=... STRPRINT=... python3 -I -B tests/errlog.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'x11', 'strprint'))
if not os.path.exists(strprint):
    strprint = os.path.join(os.path.dirname(stred), '..', 'x11', 'strprint')
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
for tool in ('pdflatex', 'latex', 'pdfinfo', 'pdftotext', 'pdftoppm'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='elg')
data = os.path.join(here, 'data')
fakeview = os.path.join(here, 'fakeview.sh')
doc = os.path.join(data, 'print_errors.doc')
clean = os.path.join(data, 'print_orient.doc')


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


def run_prog(args, env=None, cwd=None):
    e = dict(os.environ)
    e.update(env or {})
    p = subprocess.run(args, capture_output=True, text=True, env=e, cwd=cwd,
        timeout=300)
    return p.returncode, p.stdout, p.stderr


def parse_file(path):
    """The records of an error file: (lines, records, end line)."""
    lines = open(path, 'rb').read().decode('ascii').split('\n')
    recs = []
    for l in lines:
        if l.startswith('diag '):
            d = {}
            for w in l[5:].split(' '):
                k, v = w.split('=', 1)
                d[k] = re.sub(r'%([0-9A-Fa-f]{2})',
                    lambda m: chr(int(m.group(1), 16)), v)
            recs.append(d)
    return lines, recs, [l for l in lines if l.startswith('end ')]


# ---- strprint on the document with the four defects ------------------------
run = newdir('pdf')
d1 = fresh(run, doc)
pdf = os.path.join(run, 'out.pdf')
errf = os.path.join(run, 'errors.txt')
rc, out, err = run_prog([strprint, d1, '-o', pdf, '--errors', errf])
check('strprint: status 4 (made, with diagnostics)', rc == 4, (rc, err))
check('strprint: the PDF is made', os.path.exists(pdf) and pages(pdf) > 0)
el = [l for l in err.split('\n') if l.startswith('strprint: error:') or
      l.startswith('strprint: warning:')]
check('strprint: one line on stderr for each of the 4 diagnostics',
    len(el) == 4, err)
check('stderr: the equation in the old language, with its position',
    any('Section 1 Purpose, block 3, line 2, Equation marker' in l and
        'equation-invalid' in l for l in el), err)
check('stderr: the missing picture, with its file',
    any('Section 2 Definition, block 3, line 1, Picture marker' in l and
        'pics/missing.pcx' in l for l in el), err)
check('stderr: the missing character, a warning',
    any(l.startswith('strprint: warning:') and 'U+2320' in l and
        'block 4, line 1' in l for l in el), err)
check('stderr: the equation that LaTeX rejects',
    any('equation-refused' in l and 'notamacro' in l for l in el), err)
check('stderr: the count', '3 errors, 1 warning' in err, err)
check('stderr is plain ASCII with lines under 4096', all(ord(c) < 128 for c
    in err), err)

lines, recs, end = parse_file(errf)
check('error file: magic line first', lines[0] == 'STRATA-ERRORS 1', lines[:2])
check('error file: four records and the end line last',
    len(recs) == 4 and len(end) == 1 and lines[-2].startswith('end ') and
    lines[-1] == '', lines)
check('error file: the end line has the counts and the status',
    end == ['end errors=3 warnings=1 dropped=0 status=4'], end)
check('error file: kinds, codes, items, lines, marker types',
    [(r['k'], r['c'], r['i'], r['l'], r['t']) for r in recs] ==
    [('E', 'equation-invalid', '7', '2', '9'),
     ('E', 'picture-missing', '11', '1', '25'),
     ('W', 'char-missing', '12', '1', '0'),
     ('E', 'equation-refused', '12', '1', '9')], recs)
check('error file: the offset of the character and the checking text',
    recs[2]['p'] == '26' and recs[1]['w'] == 'pics/missing.pcx' and
    recs[3]['w'] == 'a + \\notamacro b' and recs[0]['w'].startswith(
    'left #R[x]'), recs)
check('error file: only ASCII, no long lines', all(ord(c) < 128 and
    len(l) < 2000 for l in lines for c in l), lines)

n_log = pages(pdf)
text = pdftext(pdf, n_log, n_log)
check('PDF: the last page is the Error Log with its heading',
    'Error Log' in text, text)
check('PDF: the count and four numbered entries',
    '3 errors' in text and '1 warning' in text and
    all(re.search(r'^\s*%d\.' % i, text, re.M) for i in (1, 2, 3, 4)), text)
check('PDF: the positions in words',
    'Section 1 Purpose, block 3, line 2' in re.sub(r'\s+', ' ', text) and
    'Section 2 Definition, block 3, line 1' in re.sub(r'\s+', ' ', text),
    text)
check('PDF: the text that is wrong',
    'pics/missing.pcx' in text and 'notamacro' in text, text)
check('PDF: the Error Log is not in the pages before the last',
    'Error Log' not in pdftext(pdf, 1, n_log - 1))
png = os.environ.get('ERRLOG_PNG')
if png:
    os.makedirs(png, exist_ok=True)
    subprocess.run(['pdftoppm', '-png', '-r', '70', '-f', str(n_log),
        '-l', str(n_log), pdf, os.path.join(png, 'errorlog')])

# The same without the Error Log: the same pages but the last one.
pdf2 = os.path.join(run, 'nolog.pdf')
errf2 = os.path.join(run, 'errors2.txt')
rc, out, err = run_prog([strprint, d1, '-o', pdf2, '--no-error-log',
    '--errors', errf2])
check('--no-error-log: still status 4 and the diagnostics on stderr',
    rc == 4 and '3 errors, 1 warning' in err, (rc, err))
check('--no-error-log: one page less and no log text',
    pages(pdf2) == n_log - 1 and 'Error Log' not in pdftext(pdf2),
    (pages(pdf2), n_log))
check('--no-error-log: the error file is the same',
    parse_file(errf2)[1] == recs)

# The progress channel.
rc, out, err = run_prog([strprint, d1, '-o', pdf2, '--progress'])
check('--progress: a "@@strprint diag" line for each, all the data on one line',
    len([l for l in out.split('\n') if l.startswith('@@strprint diag k=')]) ==
    4 and '@@strprint diag k=E i=7 ' in out, out)

# The DVI route has the Error Log as well.
dvi = os.path.join(run, 'out.dvi')
rc, out, err = run_prog([strprint, d1, '--dvi', '-o', dvi])
check('--dvi: status 4 and the DVI is made', rc == 4 and
    os.path.exists(dvi), (rc, err))

# ---- a clean document: no log and no empty section -------------------------
run = newdir('clean')
d2 = fresh(run, clean)
pdfc = os.path.join(run, 'c.pdf')
errc = os.path.join(run, 'e.txt')
rc, out, err = run_prog([strprint, d2, '-o', pdfc, '--errors', errc],
    cwd=run)
lines, recs, end = parse_file(errc)
check('clean: status 0 and nothing on stderr about errors', rc == 0 and
    'error' not in err, (rc, err))
check('clean: no records, the end line says so', recs == [] and
    end == ['end errors=0 warnings=0 dropped=0 status=0'], lines)
pdfc2 = os.path.join(run, 'c2.pdf')
run_prog([strprint, d2, '-o', pdfc2, '--no-error-log'], cwd=run)
check('clean: no Error Log text and the same pages with the option',
    'Error Log' not in pdftext(pdfc) and pages(pdfc) == pages(pdfc2) > 0,
    (pages(pdfc), pages(pdfc2)))

# ---- the error file is complete on failure ---------------------------------
run = newdir('fail')
bad = os.path.join(run, 'bad.doc')
open(bad, 'wb').write(b'this is not a document\n')
f3 = os.path.join(run, 'f.txt')
rc, out, err = run_prog([strprint, bad, '-o', os.path.join(run, 'x.pdf'),
    '--errors', f3])
lines, recs, end = parse_file(f3)
check('failure: status 1, and the file is there and complete', rc == 1 and
    lines[0] == 'STRATA-ERRORS 1' and end == [
    'end errors=0 warnings=0 dropped=0 status=1'], (rc, lines))
rc, out, err = run_prog([strprint, os.path.join(run, 'none.doc'), '--errors',
    f3])
check('failure: a document that is not there', rc == 1 and
    parse_file(f3)[2] == ['end errors=0 warnings=0 dropped=0 status=1'])
rc, out, err = run_prog([strprint, d1, '-o', os.path.join(run, 'x.pdf'),
    '--errors', '/no/such/dir/e.txt'])
check('failure: an error file that cannot be written is said', rc != 0 and
    'cannot write /no/such/dir/e.txt' in err, (rc, err))

# ---- the errors that LaTeX itself reports, mapped to the document ---------
# A stand-in for pdflatex whose log has two errors on lines of the .tex file
# that belong to ordinary text (the block "A clean block of text.").
run = newdir('tex')
fakebin = os.path.join(run, 'bin')
os.makedirs(fakebin)
fake = os.path.join(fakebin, 'pdflatex')
open(fake, 'w').write("""#!/bin/bash
n=$(grep -n 'A clean block' doc.tex | head -1 | cut -d: -f1)
{
    echo "This is FakeTeX"
    echo "! Undefined control sequence."
    echo "l.${n} \\foo"
    echo "! Missing } inserted."
    echo "l.$((n + 1)) bar"
} > doc.log
cat doc.log
exit 1
""")
os.chmod(fake, 0o755)
d5 = fresh(run, doc)
errt = os.path.join(run, 'tex.txt')
rc, out, err = run_prog([strprint, d5, '-o', os.path.join(run, 'x.pdf'),
    '--errors', errt], env={'PATH': fakebin + ':' + os.environ['PATH'],
         'STRATA_PROGRAM_DIRS': fakebin + ':' + os.environ['PATH']})
lines, recs, end = parse_file(errt)
tex = [r for r in recs if r['c'] == 'tex-error']
check('LaTeX errors: status 1 (no PDF) and the file is complete', rc == 1 and
    len(end) == 1 and end[0].endswith('status=1'), (rc, err, end))
check('LaTeX errors: each error of the log is a record, with the .tex line',
    len(tex) == 2 and 'Undefined control sequence' in tex[0]['m'] and
    'Missing } inserted' in tex[1]['m'], recs)
check('LaTeX errors: the line of the .tex file is mapped to the block',
    len(tex) == 2 and tex[0]['i'] == '6' and tex[0]['l'] == '1' and
    int(tex[0]['x']) > 0 and int(tex[1]['x']) == int(tex[0]['x']) + 1, tex)
check('LaTeX errors: told on stderr with the place in words',
    'Section 1 Purpose, block 2, line 1' in err and
    'Undefined control sequence' in err, err)
check('LaTeX errors: the errors of the writer are in the file as well (the '
    'fourth one comes from a real LaTeX run, which did not happen)',
    len(recs) == 5, len(recs))

# ---- stred --print ----------------------------------------------------------
run = newdir('cli')
d4 = fresh(run, doc)
outp = os.path.join(run, 'p.pdf')
errp = os.path.join(run, 'pe.txt')
env = {'TMPDIR': run, 'STRATA_HOME': os.path.join(run, 'home'),
       'STRATA_STRPRINT': strprint}
rc, out, err = run_prog([stred, '--print', d4, '-o', outp, '--errors', errp],
    env=env)
check('stred --print: status 5 (made, with errors) and the PDF', rc == 5 and
    pages(outp) > 0, (rc, out, err))
check('stred --print: the errors as text with their places',
    'Section 1 Purpose, block 3, line 2' in err and
    'Section 2 Definition, block 3, line 1' in err and
    '3 errors, 1 warning' in err, err)
lines, recs, end = parse_file(errp)
check('stred --print --errors: the file of the document items',
    len(recs) == 4 and [r['i'] for r in recs] == ['7', '11', '12', '12'] and
    end[0].endswith('status=4'), lines)
errp2 = os.path.join(run, 'pe2.txt')
rc, out, err = run_prog([stred, '--print', d4, '-o', outp, '--no-error-log',
    '--errors', errp2], env=env)
check('stred --print --no-error-log: no log page, same errors',
    rc == 5 and 'Error Log' not in pdftext(outp) and
    parse_file(errp2)[1] == recs, (rc, err))
rc, out, err = run_prog([stred, '--print', fresh(run, clean, 'c.doc'), '-o',
    outp], env=env, cwd=run)
check('stred --print: a clean document keeps status 0', rc == 0, (rc, err))

# ---- the Editor: Print Error markers ----------------------------------------
WAIT = ['wait'] * 8
FINAL = tui.OPEN_PRINT_FINAL


def edit(env, path, keys):
    saved = {}
    for k, v in env.items():
        saved[k] = os.environ.get(k)
        os.environ[k] = v
    try:
        return tui.session([stred, '-H', helpf, path], keys)
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


_nout = [0]


def fresh_out(run):
    _nout[0] += 1
    return os.path.join(run, 'e%d.pdf' % _nout[0])


def msgline(screen):
    """The message line: the last row that has text."""
    rows = [r for r in screen.split('\n') if r.strip()]
    return rows[-1] if rows else ''


run = newdir('ed')
path = fresh(run, doc)
out = os.path.join(run, 'e.pdf')
rec = os.path.join(run, 'rec')
os.makedirs(rec)
env = {'TMPDIR': run, 'STRATA_HOME': os.path.join(run, 'home'),
       'STRATA_STRPRINT': strprint, 'STRATA_DVIVIEW': fakeview,
       'PVREC': rec}
os.makedirs(os.path.join(run, 'home'), exist_ok=True)
nxt = ['C-F8', 'show']
d = edit(env, path, FINAL + ['text:' + fresh_out(run), 'INS'] + WAIT +
    ['show'] + nxt * 4 + nxt + ['C-F7', 'show', 'C-F7', 'show'])
check('print: the message line shows what was made and where to go on',
    'PRINT FAILED: Wrote ' in msgline(d[0]) and '3 errors' in
    msgline(d[0]), msgline(d[0]))
check('print: the markers are on the screen as labels, in the text',
    '[PRINT ERROR: The equation is not valid TeX math' in d[0] or
    'PRINT ERROR' in d[1], d[1])
# A long label wraps like text: the equation that follows the marker is not
# cut off.
check('print: the equation after a long marker label is on the screen',
    '[EQN' in d[0] or '[EQN' in d[1], d[0])
# The cursor goes through a marker whose label is over two rows, down and up,
# one row at a time, and is shown where the marker starts.  The document has
# a Print Error marker with a long text in the middle of the second block.
CRLF = b'\r\n'
EOL = b'\x11'
perr = (b'\x1340(' + CRLF + b'1The equation is not valid TeX math, so it is '
        b'printed as text and the line is long' + CRLF + b'2E1' + CRLF +
        b'\x13)')
wdoc = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
        b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
        b'DP0,8500,11000' + CRLF + b'*B0' + CRLF + b'S' + CRLF +
        b'TTop line.' + EOL + b'\x00' + CRLF + b'B' + CRLF +
        b'TThe formula ' + perr + b' ends here.' + EOL + b'\x00' + CRLF +
        b'B' + CRLF + b'TBottom line.' + EOL + b'\x00' + CRLF +
        b'*E' + CRLF)
wrun = newdir('wrapmove')
wpath = os.path.join(wrun, 'w.doc')
with open(wpath, 'wb') as f:
    f.write(wdoc)
os.makedirs(os.path.join(wrun, 'home'), exist_ok=True)
wenv = {'TMPDIR': wrun, 'STRATA_HOME': os.path.join(wrun, 'home')}
wd = edit(wenv, wpath, ['DOWN', 'DOWN', 'DOWN', 'text:1', 'UP', 'text:2', 'UP',
                       'text:3', 'show'])
wl = wd[0].split('\n')
check('wrapped marker: Down passes the marker (the digit 1 is at Bottom)',
    any('1Bottom' in l for l in wl), wd[0])
check('wrapped marker: Up goes onto the marker (2 is where it starts)',
    any('formula 2[PRINT ERROR' in l for l in wl), wd[0])
for i in range(4):
    check('Ctrl-F8 number %d: "Print error %d of 4" in the message line' %
        (i + 1, i + 1), 'Print error %d of 4' % (i + 1) in msgline(d[i + 1]),
        msgline(d[i + 1]))
check('Ctrl-F8 after the last one wraps to the first, with the message',
    'Wrapped to the start' in msgline(d[5]) and
    'Print error 1 of 4' in msgline(d[5]), msgline(d[5]))
check('Ctrl-F7 from the first wraps to the last',
    'Wrapped to the end' in msgline(d[6]) and
    'Print error 4 of 4' in msgline(d[6]), msgline(d[6]))
check('Ctrl-F7 goes back one: 3 of 4, no wrap',
    'Print error 3 of 4' in msgline(d[7]) and 'Wrapped' not in
    msgline(d[7]), msgline(d[7]))
check('the message shows the text of the error',
    'U+2320' in msgline(d[7]), msgline(d[7]))
check('the PDF of the Print has the Error Log',
    'Error Log' in pdftext(os.path.join(run, 'e1.pdf')),
    pdftext(os.path.join(run, 'e1.pdf'))[-300:])
check('the document on disk was not changed by Print',
    open(path, 'rb').read() == open(doc, 'rb').read())

# The Search panel: Find = Print Error, and Search again (Ctrl-F9).
d = edit(env, path, FINAL + ['text:' + fresh_out(run), 'INS'] + WAIT +
    ['F9', 'show', 'DOWN', 'RIGHT', 'show', 'INS', 'show', 'C-F9', 'show',
     'C-F9', 'show'])
check('F9 panel: Search for, Find and Direction fields',
    'Search for' in d[0] and 'Find' in d[0] and 'Direction' in d[0], d[0])
check('F9 panel: Find offers Print Error', 'Print Error' in d[1], d[1])
check('F9 Print Error: lands on the first one',
    'Print error 1 of 4' in msgline(d[2]), msgline(d[2]))
check('Ctrl-F9 repeats with the method: 2 of 4, then 3 of 4',
    'Print error 2 of 4' in msgline(d[3]) and
    'Print error 3 of 4' in msgline(d[4]), (msgline(d[3]), msgline(d[4])))

# The text search still works, and Ctrl-F9 repeats it.
d = edit(env, path, ['F9', 'text:the', 'INS', 'show', 'C-F9', 'show'])
check('F9 Text search finds and Ctrl-F9 finds the next one',
    'Found' in msgline(d[0]) and 'Found' in msgline(d[1]), (d[0], d[1]))

# The marker menu does not list it; the marker is read only.
d = edit(env, path, FINAL + ['text:' + fresh_out(run), 'INS'] + WAIT +
    ['A-F12', 'show', 'ESC', 'C-F8', 'A-F12', 'show', 'ESC', 'show'])
check('Alt-F12 grid: no Print Error in the Marker Commands',
    'Marker Commands' in d[0] and 'Print Error' not in d[0] and
    'Cross-reference' in d[0], d[0])
check('Alt-F12 on a Print Error shows its text, read only',
    'read only' in d[1] and 'is not valid' in d[1], d[1])

# Remove, Remove All (Print, Errors), and not piling up.
menu = ['F12', 'text:P', 'text:E'] + ['RIGHT'] * 3 + ['ENTER']
d = edit(env, path, FINAL + ['text:' + fresh_out(run), 'INS'] + WAIT +
    ['C-F8', 'F12', 'text:P', 'text:E'] + ['RIGHT'] * 2 + ['ENTER', 'show'] +
    ['C-F8', 'show'] + menu + ['show', 'C-F8', 'show'])
check('Remove: one removed (the message shows so)',
    'Print Error removed' in msgline(d[0]), msgline(d[0]))
check('after one is removed there are 3 of them',
    'of 3' in msgline(d[1]), msgline(d[1]))
check('Remove All: all removed', 'print errors removed' in
    msgline(d[2]) or '3 print errors removed' in msgline(d[2]),
    msgline(d[2]))
check('Ctrl-F8 with none: "No print errors"',
    'No print errors' in msgline(d[3]), msgline(d[3]))
d = edit(env, path, FINAL + ['text:' + fresh_out(run), 'INS'] + WAIT + FINAL +
    ['text:' + fresh_out(run), 'INS', 'wait', 'wait', 'wait', 'wait', 'wait',
     'wait', 'wait', 'wait'] + ['C-F8', 'show'])
check('a second Print replaces the markers: still 4 and not 8',
    'of 4' in msgline(d[-1]), msgline(d[-1]))


# No key sequence makes a Print Error: every entry of the Marker Commands grid
# is chosen and accepted, then a few hundred keys (from a fixed seed) are
# typed; Ctrl-F8 must still say that there is no Print Error.
import random
run = newdir('nokey')
cpath = fresh(run, clean)
keys = []
for i in range(40):
    keys += ['A-F12'] + ['DOWN'] * (i % 13) + ['RIGHT'] * (i // 13) + [
        'ENTER', 'INS', 'ESC', 'ESC']
rng = random.Random(1994)
pool = ['F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'A-F2', 'A-F3',
    'A-F5', 'A-F6', 'A-F7', 'A-F9', 'A-F12', 'C-F3', 'C-F4', 'C-F5', 'C-F12',
    'C-F9', 'C-F7', 'ENTER', 'ESC', 'UP', 'DOWN', 'LEFT', 'RIGHT', 'HOME',
    'END', 'PGUP', 'PGDN', 'INS', 'DEL', 'BKSP', 'TAB', 'S-INS', 'C-a',
    'C-b', 'C-c', 'C-e', 'C-n', 'C-u', 'C-t', 'text:x', 'text:hello',
    'text:\\', 'text:ke', 'text:eq', 'text:pe', 'text:PRINT ERROR']
keys += [rng.choice(pool) for _ in range(300)]
keys += ['ESC', 'ESC', 'ESC', 'ESC', 'C-F8', 'show']
d = edit(env, cpath, keys)
check('no key sequence makes a Print Error (40 markers, 300 random keys)',
    'No print errors' in msgline(d[-1]), msgline(d[-1]))

# Preview of a block: only the faults of the block are marked.
d = edit(env, path, ['F9', 'text:bad equation', 'INS', 'F12', 'text:P',
    'RIGHT', 'RIGHT', 'ENTER', 'RIGHT', 'ENTER'] + WAIT + ['show', 'C-F8',
    'show', 'C-F8', 'show'])
check('Preview of a block: done, with no print errors elsewhere in the '
    'message', 'Preview done' in msgline(d[0]) and '1 print error' in
    msgline(d[0]), msgline(d[0]))
check('Preview of a block: the message line shows PRINT FAILED',
    'PRINT FAILED' in msgline(d[0]), msgline(d[0]))
check('Preview of a block: the DVI that the Viewer gets has the special',
    os.path.exists(os.path.join(rec, 'copy.dvi')) and
    b'strata:print-failed 1' in open(os.path.join(rec, 'copy.dvi'),
    'rb').read(), os.listdir(rec))
check('Preview of a block: its fault is marked, and only that one',
    'Print error 1 of 1' in msgline(d[1]) and 'not valid' in msgline(d[1]) and
    'Print error 1 of 1' in msgline(d[2]), (msgline(d[1]), msgline(d[2])))

# The Document Manager's Preview: no document to mark, text at the end.
saved = {}
for k, v in env.items():
    saved[k] = os.environ.get(k)
    os.environ[k] = v
# The program names the document as it was given: a short name, from its own
# directory, keeps the first line of the screen short whatever TMPDIR is.
here_dir = os.getcwd()
os.chdir(os.path.dirname(path))
try:
    d, st = tui.session([stred, '--preview', os.path.basename(path), '-H',
        helpf], WAIT * 2 + ['show'], status=True)
finally:
    os.chdir(here_dir)
    for k, v in saved.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v
rc = os.waitstatus_to_exitcode(st) if st is not None else None
check('stred --preview: the errors as text at the end, status 5',
    rc == 5 and 'the document has errors' in d[0], (rc, d[0]))


def pty_output(argv, env, limit=90):
    """Run a program in a pty and return all it wrote and its exit status."""
    import pty, select, time
    e = dict(os.environ)
    e.update(env)
    e['TERM'] = 'xterm'
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.clear()
        os.environ.update(e)
        os.execv(argv[0], argv)
    buf = b''
    end = time.time() + limit
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.5)
        if r:
            try:
                got = os.read(fd, 65536)
            except OSError:
                break
            if not got:
                break
            buf += got
    st = os.waitpid(pid, 0)[1]
    return buf.decode('latin1').replace('\r', ''), os.waitstatus_to_exitcode(st)


run = newdir('pv')
pvdoc = fresh(run, doc, 'work.doc')
out, rc = pty_output([stred, '--preview', pvdoc], dict(env, TMPDIR=run))
check('stred --preview: the text says PRINT FAILED, after the header line',
    rc == 5 and 'the document has errors or warnings:\nPRINT FAILED: the '
    'preview is inaccurate.\n' in out and '\nPRINT FAILED: 3 errors, 1 '
    'warning\n' in out, (rc, out[-600:]))


print('errlog.py: %d failures' % fails)
sys.exit(1 if fails else 0)
