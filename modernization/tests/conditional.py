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

"""Conditional text: the If, Else and Endif markers, the variables that str
and the Editor pass to the Printer, and the choice of the branch in Print,
Preview and Export as LaTeX.

The documents are made here (the records are those of docs/DOC-FORMAT.txt).
The real strprint and the real pdflatex print them; the PDF is read with
pdftotext.  The unit tests of the rules (the expression, the comparison, the
names and the values) are in tests/condtest.c.

    STRED=... STR=... STRPRINT=... python3 -I -B tests/conditional.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, stat, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
data = os.path.join(here, 'data')
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
strprog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
btype = os.environ.get('STRATA_BUILD_TYPE', 'debug')
for tool in ('pdflatex', 'pdftotext'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint) or not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='cond')
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'

CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1HB0' + CRLF + b'P1FB0' + CRLF + b'P1C33' + CRLF +
          b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF +
          b'YCL0' + CRLF + b'YIWN2' + CRLF)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def newdir(name):
    d = os.path.join(tmp, '%s%d' % (name, len(os.listdir(tmp))))
    os.makedirs(d)
    shutil.copytree(os.path.join(data, 'pics'), os.path.join(d, 'pics'))
    return d


def run(cmd, cwd=None, env=None):
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run(cmd, cwd=cwd, env=e, capture_output=True, text=True,
        errors='replace', timeout=240)
    return r.returncode, r.stdout, r.stderr


def marker(t, recs):
    return b'\x13' + str(t).encode() + b'(' + b''.join(
        r + CRLF for r in recs) + b'\x13)'


def IF(expr):
    return marker(33, [b'1' + expr.encode() + b'\x00'])


ELSE = marker(34, [])
ENDIF = marker(35, [])


def xref(num, style):
    return marker(2, [b'XN' + num.encode() + b'\x00', b'XT%d' % style])


def blk(text):
    return b'T' + text + EOL + b'\x00' + CRLF


def flat(blocks):
    """An unstructured document of blocks (the bytes of their texts)."""
    out = HEADER + b'*B0' + CRLF + b'S' + CRLF
    for i, b in enumerate(blocks):
        out += (b'' if i == 0 else b'B' + CRLF) + blk(b)
    return out + b'*E' + CRLF


def structured(parts):
    """A structured document: parts is the list of the records after *B1."""
    return HEADER + b'*B1' + CRLF + b''.join(parts) + b'*E' + CRLF


def write(d, name, data_):
    p = os.path.join(d, name)
    with open(p, 'wb') as f:
        f.write(data_)
    return p


def pdftext(pdf):
    rc, out, err = run(['pdftotext', '-layout', pdf, '-'])
    return ' '.join(out.split())


def printdoc(path, extra, name='out'):
    """Print a document with strprint; returns (rc, text, stderr)."""
    d = os.path.dirname(path)
    pdf = os.path.join(d, name + '.pdf')
    if os.path.exists(pdf):
        os.remove(pdf)
    rc, out, err = run([strprint, path, '-o', pdf] + extra)
    return rc, (pdftext(pdf) if os.path.exists(pdf) else ''), err


def export(path, extra, name='out'):
    """Export the LaTeX file of a document; returns (rc, tex, stderr)."""
    d = os.path.dirname(path)
    tex = os.path.join(d, name + '.tex')
    if os.path.exists(tex):
        os.remove(tex)
    rc, out, err = run([strprint, path, '--export-latex', tex,
        '--docdir', d] + extra)
    t = open(tex, 'r', errors='replace').read() if os.path.exists(tex) else ''

    return rc, t, err


def errors_of(path, extra):
    """The diagnostics of a print: a list of (code, mtype, line, off)."""
    d = os.path.dirname(path)
    ef = os.path.join(d, 'errors.txt')
    pdf = os.path.join(d, 'e.pdf')
    run([strprint, path, '-o', pdf, '--errors', ef] + extra)
    found = []
    if os.path.exists(ef):
        for l in open(ef).read().split('\n'):
            if not l.startswith('diag '):
                continue
            f = dict(x.split('=', 1) for x in l[5:].split(' ') if '=' in x)
            found.append((f['c'], int(f['t']), int(f['l']), int(f['o'])))
    return found


# --------------------------------------------------------------- the branches
d = newdir('branches')
doc1 = write(d, 'b.doc', flat([
    b'Always. ' + IF('COLOR = red') + b'RED' + ELSE + b'OTHER' + ENDIF +
    b' end.',
    IF('n > 9'),
    b'Block that needs n above nine.',
    ENDIF,
    b'Last block.']))
rc, t, err = printdoc(doc1, ['--var', 'COLOR', 'red', '--var', 'n', '10'])
check('print: the true branch and the chosen block', rc == 0 and
      'Always. RED end.' in t and 'Block that needs n above nine.' in t and
      'Last block.' in t, (rc, t, err))
check('print: the markers are not printed', 'RED' in t and 'OTHER' not in t
      and 'IF' not in t and 'ENDIF' not in t and '[' not in t, t)
rc, t, err = printdoc(doc1, ['--var', 'COLOR', 'blue', '--var', 'n', '9'])
check('print: the else branch and the hidden block', rc == 0 and
      'Always. OTHER end.' in t and 'Block that needs' not in t and
      'Last block.' in t, (rc, t, err))
rc, t, err = printdoc(doc1, [])
check('print: no variables: undefined (n > 9 is a text test and holds)',
      'OTHER' in t and 'Block that needs n above nine.' in t, t)
rc, t, err = printdoc(doc1, ['--var', 'n', '9'])
check('print: 9 > 9 is false', 'Block that needs' not in t, t)
rc, t, err = printdoc(doc1, ['--var', 'n', '010'])
check('print: 010 > 9 is a number test (10 > 9)',
      'Block that needs n above nine.' in t, t)

d = newdir('edition')
doc2 = write(d, 'e.doc', flat([
    b'Shared text. ' + IF('.STRATA_BUILD_TYPE # release') +
    b'DEVELOPER ONLY' + ELSE + b'FOR EVERYONE' + ENDIF + b' done.']))
rc, t, err = printdoc(doc2, ['--special', '.STRATA_BUILD_TYPE', 'release'])
check('edition: release leaves the developer text out', rc == 0 and
      'FOR EVERYONE' in t and 'DEVELOPER ONLY' not in t, (rc, t, err))
rc, t, err = printdoc(doc2, ['--special', '.STRATA_BUILD_TYPE', 'debug'])
check('edition: debug keeps the developer text', rc == 0 and
      'DEVELOPER ONLY' in t and 'FOR EVERYONE' not in t, (rc, t, err))
rc, t, err = printdoc(doc2, [])
check('edition: without the special variable it is undefined (# release)',
      'DEVELOPER ONLY' in t, t)

# ------------------------------------------------------- the rules of options
for opts, what in ((['--var', '9x', 'v'], 'a name that starts with a digit'),
                   (['--var', '.STRATA_X', 'v'], 'a special name as --var'),
                   (['--var', 'a', 'say "hi"'], 'a value with a quote'),
                   (['--var', 'a', 'x' * 256], 'a value of 256 characters'),
                   (['--special', 'plain', 'v'], 'an ordinary --special'),
                   (['--var', 'a'], 'a missing value')):
    rc, t, err = printdoc(doc1, opts)
    check('options: %s is refused' % what, rc != 0 and t == '', (rc, err))
rc, t, err = printdoc(doc1, ['--var', 'a', 'x' * 255])
check('options: a value of 255 characters is taken', rc == 0, (rc, err))

# ------------------------------------------------------------------ the errors
d = newdir('errors')
doc3 = write(d, 'x.doc', flat([
    b'Text ' + ELSE + b' more ' + IF('n <> 5') + b'x' + ENDIF + ENDIF +
    IF('a = 1') + b' open']))
found = errors_of(doc3, [])
codes = sorted(c for c, _, _, _ in found)
check('errors: each one is told', codes == ['else-unmatched',
      'endif-unmatched', 'if-bad-operator', 'if-unclosed'], found)
check('errors: the positions are those of the original (line 1, marker types)',
      all(l == 1 for _, _, l, _ in found) and
      sorted(t for _, t, _, _ in found) == [33, 33, 34, 35], found)
rc, t, err = printdoc(doc3, [])
check('errors: the print is made and fails', rc != 0 and 'Text' in t and
      'PRINT FAILED' in err, (rc, err))
for expr, code in (('', 'if-empty'), ('9 = 1', 'if-no-variable'),
                   ('n =', 'if-no-literal'), ('n = "x"', 'if-bad-literal'),
                   ('n 5', 'if-bad-operator'), ('n == 5', 'if-bad-operator')):
    p = write(d, 'y.doc', flat([IF(expr) + b'a' + ENDIF]))
    found = errors_of(p, [])
    check('errors: expression "%s" is %s' % (expr, code),
          [c for c, _, _, _ in found] == [code], found)
deep = b''.join(IF('x = undefined') for _ in range(11)) + b'deep' + \
    b''.join(ENDIF for _ in range(11))
p = write(d, 'z.doc', flat([deep]))
found = errors_of(p, [])
check('errors: eleven levels are too deep, told once',
      [c for c, _, _, _ in found] == ['if-too-deep'], found)
ten = b''.join(IF('x = undefined') for _ in range(10)) + b'ten' + \
    b''.join(ENDIF for _ in range(10))
p = write(d, 'w.doc', flat([ten]))
check('errors: ten levels are fine', errors_of(p, []) == [])

# ------------------------------ sections, an index marker, a picture, a xref
d = newdir('sections')
picture = marker(25, [b'1pics/p.png\x00', b'22000'])
index = marker(3, [b'1Hidden entry\x00'])
doc4 = write(d, 's.doc', structured([
    b'S' + CRLF, blk(b'Title'),
    b'S' + CRLF, blk(b'Intro'),
    b'+' + CRLF,
    b'SE0' + CRLF, blk(b'Visible One'),
    blk(b'Body one text. ' + IF('edition = developer')),
    b'SE0' + CRLF, blk(b'Hidden Heading'),
    blk(b'Hidden body text.' + index),
    blk(picture),
    blk(ENDIF),
    b'SE0' + CRLF, blk(b'Visible Two'),
    blk(b'See ' + xref('3', 3) + b'.'),
    b'-' + CRLF]))
rc, t, err = printdoc(doc4, [])
check('sections: the document prints', rc in (0, 4), (rc, err))
rc, t, err = printdoc(doc4, ['--var', 'edition', 'user'])
check('sections: a hidden section, its text and its marks are not printed',
      rc == 0 and 'Visible One' in t and 'Visible Two' in t and
      'Hidden' not in t and 'IF' not in t, (rc, err, t))
check('sections: the numbers are those of the sections that are printed',
      re.search(r'\b2\s+Visible Two', t) is not None and
      re.search(r'\b1\s+Visible One', t) is not None, t)
check('sections: the cross-reference follows its section',
      re.search(r'See 2 Visible Two\.', t) is not None, t)
# The Index marker turns the index on by itself now, and then it is not
# printed where it is; with the index off it is told as a warning.
rc, t, err = printdoc(doc4, ['--var', 'edition', 'developer', '--no-index'])
check('sections: the developer edition has everything (the Index marker '
      'is told as a warning there)', rc == 4 and
      'Hidden Heading' in t and 'Hidden body text.' in t and
      re.search(r'\b3\s+Visible Two', t) is not None and
      re.search(r'See 3 Visible Two\.', t) is not None, (rc, err, t))
rc, tex, err = export(doc4, ['--var', 'edition', 'user'])
check('sections: the picture of a hidden section is not in the file',
      rc == 0 and 'pics/p.png' not in tex and 'pic1.png' not in tex, tex)
rc, tex, err = export(doc4, ['--var', 'edition', 'developer'])
check('sections: the picture is in the developer file',
      'pics/p.png' in tex and tex.count('\\strpic{') == 1, tex)

# ----------------------------------------------- positions of the diagnostics
d = newdir('positions')
doc5 = write(d, 'p.doc', flat([
    b'First block.',
    IF('x = 1'),
    b'Hidden block.',
    ENDIF,
    b'Visible with a bad Else ' + ELSE + b'here.']))
found = errors_of(doc5, [])
check('positions: the item of a diagnostic is the item of the original '
      '(block 5)', len(found) == 1 and found[0][0] == 'else-unmatched',
      found)
ef = os.path.join(d, 'errors.txt')
items = re.findall(r' i=(\d+) ', open(ef).read())
check('positions: the item number is that of the original', items == ['5'],
      items)
rc, tex, err = export(doc5, ['--position-comments'])
posm = re.findall(r'%@P (\d+) (\d+) (\d+)', tex)
check('positions: the position comments say the items of the original',
      [p[0] for p in posm] == ['1', '5'], posm)

# ------------------------------------------------ stred --print and --preview
d = newdir('editor')
doc6 = write(d, 'c.doc', flat([b'Edition ' + IF('who = me') + b'MINE' + ELSE +
                               b'THEIRS' + ENDIF + b'.']))
pdf = os.path.join(d, 'c.pdf')
rc, out, err = run([stred, '--print', doc6, '-o', pdf, '--var', 'who', 'me',
                    '--no-error-log'])
check('stred --print passes the variables to the Printer',
      rc == 0 and 'MINE' in pdftext(pdf) and 'THEIRS' not in pdftext(pdf),
      (rc, err))
rc, out, err = run([stred, '--print', doc6, '-o', pdf, '--var', 'who', 'you',
                    '--no-error-log'])
check('stred --print with another value', rc == 0 and
      'THEIRS' in pdftext(pdf), (rc, err))
rc, out, err = run([stred, '--print', doc6, '-o', pdf, '--var', '1x', 'v'])
check('stred --print refuses a bad name', rc != 0 and 'start with a letter' in
      err, (rc, err))
rc, out, err = run([stred, '--var', '.bad', 'v', doc6])
check('stred refuses a name with a period', rc == 2 and 'period' in err,
      (rc, err))

# ---------------------------------------------------- the Edit selection (pty)
d = newdir('str')
docfile = write(d, 'edit.doc', flat([b'Edit me.']))
recfile = os.path.join(d, 'args.txt')
fake = os.path.join(d, 'fakestred.sh')
with open(fake, 'w') as f:
    f.write('#!/bin/sh\nfor a in "$@"; do echo "$a" >> %s; done\n' % recfile)
os.chmod(fake, os.stat(fake).st_mode | stat.S_IXUSR)
home = os.path.join(d, 'home')
os.makedirs(home)
env = {'STRED': fake, 'HOME': home, 'XDG_CONFIG_HOME': home}
saved = dict((k, os.environ.get(k)) for k in env)
os.environ.update(env)
old = os.getcwd()
os.chdir(d)
try:
    dumps = tui.session([strprog], [
        'ENTER',
        # The panel starts on Document Name; Variables follows.
        'text:edit.doc', 'DOWN', 'show',
        'F2', 'show',
        'text:COLOR', 'DOWN', 'text:red', 'DOWN',
        'text:n', 'DOWN', 'text:10', 'show',
        'INS', 'show', 'INS', 'wait'])
finally:
    os.chdir(old)
    for k, v in saved.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v
check('str: the Edit selection has the field Variables',
      'Variables:' in dumps[0] and 'none' in dumps[0], dumps[0])
check('str: F2 opens the panel with a name and a value in two fields',
      'Name 1:' in dumps[1] and 'Value 1:' in dumps[1], dumps[1])
check('str: the panel holds what was typed', 'COLOR' in dumps[2] and
      'red' in dumps[2], dumps[2])
check('str: the field says how many are given', '2 given' in dumps[3],
      dumps[3])
args = open(recfile).read().split('\n') if os.path.exists(recfile) else []
trip = [args[i:i + 3] for i in range(len(args))]
check('str: the editor is started with the variables',
      ['--var', 'COLOR', 'red'] in trip and ['--var', 'n', '10'] in trip,
      args)
check('str: and with the special variable of the build type',
      ['--special', '.STRATA_BUILD_TYPE', btype] in trip, args)

# A bad name is refused with the rule and the panel stays open.
os.remove(recfile) if os.path.exists(recfile) else None
os.environ.update(env)
os.chdir(d)
try:
    dumps = tui.session([strprog], [
        'ENTER', 'text:edit.doc', 'DOWN', 'F2',
        'text:9lives', 'DOWN', 'text:x', 'INS', 'show',
        'ENTER', 'show'])
finally:
    os.chdir(old)
    for k, v in saved.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v
check('str: a name that starts with a digit is refused with the rule',
      'must start with a letter' in dumps[0], dumps[0])
check('str: the panel is still open after the refusal',
      'Name 1:' in dumps[1], dumps[1])

print('conditional: %d failures' % fails)
sys.exit(1 if fails else 0)
