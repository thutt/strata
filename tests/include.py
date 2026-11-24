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

"""The Include marker: a master document that holds the text of its parts.

The documents are made here (the records are those of docs/DOC-FORMAT.txt)
in a directory of their own: a master and two parts, a part that includes a
part, a part that includes itself, a missing part, a damaged part.  The real
strprint and the real pdflatex print them; the PDF is read with pdftotext.

    STRPRINT=... python3 -I -B tests/include.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
for tool in ('pdflatex', 'pdftotext'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='incl')
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
    return d


def run(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
        errors='replace', timeout=240)
    return r.returncode, r.stdout, r.stderr


def marker(t, recs):
    return b'\x13' + str(t).encode() + b'(' + b''.join(
        r + CRLF for r in recs) + b'\x13)'


def INCLUDE(name):
    return marker(16, [b'1' + name.encode() + b'\x00'])


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


def sections(secs):
    """A structured document: secs is a list of lists of block texts, the
    first block of each being the headline."""
    out = HEADER + b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF
    for blocks in secs:
        out += b'SE0' + CRLF + b''.join(blk(b) for b in blocks)
    return out + b'-' + CRLF + b'*E' + CRLF


def write(d, name, data_):
    p = os.path.join(d, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(data_)
    return p


def pdftext(pdf):
    rc, out, err = run(['pdftotext', '-layout', pdf, '-'])
    return ' '.join(out.split())


def printdoc(path, extra=(), name='out', cwd=None):
    """Print a document with strprint; returns (rc, text, stderr)."""
    d = os.path.dirname(path)
    pdf = os.path.join(d, name + '.pdf')
    if os.path.exists(pdf):
        os.remove(pdf)
    rc, out, err = run([strprint, path, '-o', pdf] + list(extra), cwd=cwd)
    return rc, (pdftext(pdf) if os.path.exists(pdf) else ''), err


def errors_of(path, extra=()):
    """The diagnostics of a print: a list of (code, mtype, item)."""
    d = os.path.dirname(path)
    ef = os.path.join(d, 'errors.txt')
    pdf = os.path.join(d, 'e.pdf')
    run([strprint, path, '-o', pdf, '--errors', ef] + list(extra))
    found = []
    if os.path.exists(ef):
        for l in open(ef).read().split('\n'):
            if not l.startswith('diag '):
                continue
            f = dict(x.split('=', 1) for x in l[5:].split(' ') if '=' in x)
            found.append((f['c'], int(f['t']), int(f['i'])))
    return found


def order(t, words):
    """True when the words are in t, in this order."""
    at = 0
    for w in words:
        i = t.find(w, at)
        if i < 0:
            return False
        at = i + len(w)
    return True


# ------------------------------------------------- a master and two parts
d = newdir('parts')
write(d, 'alpha.doc', sections([
    [b'Alpha Heading', b'Alpha body text one.'],
    [b'Alpha Second', b'Alpha body text two.']]))
write(d, 'beta.doc', flat([b'Beta line one.', b'Beta line two.']))
master = write(d, 'master.doc', sections([
    [b'Master Intro', b'Master intro text.'],
    [b'Master Alpha', b'Before the part.', INCLUDE('alpha')],
    [b'Master Beta', INCLUDE('beta.doc'), b'After beta.'],
    [b'Master End', b'See ' + xref('3', 3) + b'.']]))
rc, t, err = printdoc(master)
check('parts: the master prints', rc == 0, (rc, err))
check('parts: the text of the parts is at the markers, in order',
      order(t, ['Master Intro', 'Master intro text.', 'Master Alpha',
                'Before the part.', 'Alpha Heading', 'Alpha body text one.',
                'Alpha Second', 'Alpha body text two.', 'Master Beta',
                'Beta line one.', 'Beta line two.', 'After beta.',
                'Master End']), t)
check('parts: no marker is printed', 'INCLUDE' not in t and
      'Invalid' not in t, t)
check('parts: the sections of a part are below the section of the marker',
      re.search(r'\b2\.1\s+Alpha Heading', t) is not None and
      re.search(r'\b2\.2\s+Alpha Second', t) is not None, t)
check('parts: the numbers go on after the part',
      re.search(r'\b3\s+Master Beta', t) is not None and
      re.search(r'\b4\s+Master End', t) is not None, t)
check('parts: a cross-reference of the master follows its section',
      re.search(r'See 3 Master Beta\.', t) is not None, t)
rc, t, err = printdoc(master, ['--draft'], name='draft')
check('parts: a draft print has the parts too', rc == 0 and
      'Alpha body text two.' in t and 'Beta line two.' in t, (rc, err))
rc, t, err = printdoc(master, ['--contents', '3'], name='toc')
check('parts: the table of contents lists the headlines of the parts',
      rc == 0 and re.search(r'Alpha Heading[ .]+\d', t) is not None and
      re.search(r'Alpha Second[ .]+\d', t) is not None and
      re.search(r'Master End[ .]+\d', t) is not None, (rc, err, t))

# ---------------------------------- the place of the part: the directory
d2 = newdir('where')
write(d2, 'sub/inner.doc', flat([b'Inner text.']))
write(d2, 'sub/top.doc', flat([b'Top text.', INCLUDE('inner')]))
m2 = write(d2, 'm.doc', flat([b'Start.', INCLUDE('sub/top'), b'End.']))
rc, t, err = printdoc(m2)
check('where: a name with a directory, no extension, and a part that '
      'includes a part (looked for beside the part)', rc == 0 and
      order(t, ['Start.', 'Top text.', 'Inner text.', 'End.']), (rc, err, t))
other = newdir('cwd')
write(other, 'only.doc', flat([b'From the working directory.']))
m3 = write(d2, 'm3.doc', flat([b'Start.', INCLUDE('only'), b'End.']))
rc, t, err = printdoc(m3, cwd=other)
check('where: the working directory is looked in second', rc == 0 and
      'From the working directory.' in t, (rc, err, t))
rc, t, err = printdoc(m3, cwd=d2)
check('where: not found in either: told and printed', rc != 0 and
      'Invalid Include request' in t and 'Start.' in t and 'End.' in t,
      (rc, err, t))
m4 = write(d2, 'm4.doc', flat([b'A.', INCLUDE(os.path.join(other,
    'only.doc')), b'B.']))
rc, t, err = printdoc(m4)
check('where: an absolute name is taken as it is', rc == 0 and
      order(t, ['A.', 'From the working directory.', 'B.']), (rc, err, t))
write(d2, 'Mixed.DOC', flat([b'Mixed case.']))
m5 = write(d2, 'm5.doc', flat([INCLUDE('mixed')]))
rc, t, err = printdoc(m5)
check('where: the case of the letters does not matter', rc == 0 and
      'Mixed case.' in t, (rc, err, t))

# ----------------------------------------------------------- the errors
d3 = newdir('errors')
write(d3, 'good.doc', flat([b'Good part.']))
write(d3, 'bad.doc', b'this is not a document at all')
write(d3, 'self.doc', flat([b'Self text.', INCLUDE('self')]))
write(d3, 'ping.doc', flat([b'Ping text.', INCLUDE('pong')]))
write(d3, 'pong.doc', flat([b'Pong text.', INCLUDE('ping')]))
for i in range(1, 10):
    write(d3, 'n%d.doc' % i, flat([b'Level %d.' % i] +
          ([INCLUDE('n%d' % (i + 1))] if i < 9 else [])))
mm = write(d3, 'm.doc', flat([
    b'Top.', INCLUDE('missing'), INCLUDE('bad'), INCLUDE(''),
    b'Text ' + INCLUDE('good') + b' with the marker not alone.',
    INCLUDE('good'), b'Bottom.']))
found = errors_of(mm)
codes = [c for c, _, _ in found]
check('errors: each one is told, with the marker type and the item',
      codes == ['include-missing', 'include-damaged', 'include-noname',
                'include-place'] and all(t == 16 for _, t, _ in found) and
      [i for _, _, i in found] == [2, 3, 4, 5], found)
rc, t, err = printdoc(mm)
check('errors: the print is made and fails', rc != 0 and
      order(t, ['Top.', 'Invalid Include request', 'Good part.', 'Bottom.'])
      and 'PRINT FAILED' in err, (rc, err, t))
check('errors: a visible notice at each place', t.count(
      'Invalid Include request') >= 4, t)
rc, t, err = printdoc(os.path.join(d3, 'self.doc'))
found = errors_of(os.path.join(d3, 'self.doc'))
check('loop: a part that includes itself is stopped', rc != 0 and
      [c for c, _, _ in found] == ['include-loop'] and 'Self text.' in t,
      (rc, found, t))
found = errors_of(os.path.join(d3, 'ping.doc'))
check('loop: two parts that include each other are stopped',
      [c for c, _, _ in found] == ['include-loop'], found)
m8 = write(d3, 'm8.doc', flat([INCLUDE('n2')]))
found = errors_of(m8)
rc, t, err = printdoc(m8)
check('depth: eight levels of parts are fine', found == [] and
      'Level 9.' in t, (found, t))
m9 = write(d3, 'm9.doc', flat([INCLUDE('n1')]))
found = errors_of(m9)
rc, t, err = printdoc(m9)
check('depth: nine levels are too deep, told once, the rest is printed',
      [c for c, _, _ in found] == ['include-deep'] and 'Level 8.' in t and
      'Level 9.' not in t, (found, t))
m10 = write(d3, 'm10.doc', sections([
    [b'Head', b'Text.', INCLUDE('good')],
    [b'Next', INCLUDE('good')]]))
write(d3, 'sec.doc', sections([[b'Sub', b'Sub text.']]))
m11 = write(d3, 'm11.doc', sections([
    [b'Head', INCLUDE('sec'), b'More text after.']]))
found = errors_of(m11)
check('errors: a part with sections must be in the last block of its '
      'section', [c for c, _, _ in found] == ['include-place'], found)
m12 = write(d3, 'm12.doc', sections([[INCLUDE('good')]]))
found = errors_of(m12)
check('errors: the marker cannot be in a headline',
      [c for c, _, _ in found] == ['include-place'], found)

# ------------------------------- the text of a part is read when printed
d4 = newdir('fresh')
part = write(d4, 'part.doc', flat([b'Version one.']))
m13 = write(d4, 'm.doc', flat([INCLUDE('part')]))
rc, t, err = printdoc(m13)
check('fresh: the first print has the first text', 'Version one.' in t, t)
write(d4, 'part.doc', flat([b'Version two.']))
rc, t, err = printdoc(m13)
check('fresh: the next print has the new text of the part',
      'Version two.' in t and 'Version one.' not in t, t)

# ----------------------------------- conditional text applies to the parts
d5 = newdir('cond')
write(d5, 'c.doc', flat([
    marker(33, [b'1who = me\x00']) + b'MINE' + marker(34, []) + b'YOURS' +
    marker(35, [])]))
m14 = write(d5, 'm.doc', flat([b'Is it ' + b'x' + b'?', INCLUDE('c')]))
rc, t, err = printdoc(m14, ['--var', 'who', 'me'])
check('cond: the variables of the master choose in the part', rc == 0 and
      'MINE' in t and 'YOURS' not in t, (rc, err, t))
rc, t, err = printdoc(m14, ['--var', 'who', 'you'])
check('cond: and the other branch', 'YOURS' in t and 'MINE' not in t, t)

# -------------------------------------- a print of a part of the master
# Print from Section to Section (and Print Current Section Only, which is
# made the same way) includes the part of a section that holds a marker.
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
if os.path.exists(stred):
    def partprint(extra):
        pdf = os.path.join(d, 'part.pdf')
        if os.path.exists(pdf):
            os.remove(pdf)
        rc, out, err = run([stred, '--print', master, '-o', pdf] + extra)
        return rc, (pdftext(pdf) if os.path.exists(pdf) else ''), err
    rc, t, err = partprint(['--from', '2', '--to', '2'])
    check('part print: section 2 has the part that its marker names',
          rc == 0 and order(t, ['Master Alpha', 'Before the part.',
          'Alpha Heading', 'Alpha body text one.']) and
          'Master Beta' not in t and 'Beta line' not in t, (rc, err, t))
    rc, t, err = partprint(['--from', '3', '--to', '3'])
    check('part print: section 3 has the other part, and the numbers of '
          'the whole work', rc == 0 and 'Beta line one.' in t and
          re.search(r'\b3\s+Master Beta', t) is not None and
          'Alpha' not in t, (rc, err, t))

print('%d failed' % fails if fails else 'all passed')
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
