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

"""The Print and First Block Type fields of the Global Format settings.

Print: Headlines and Text (no record), Only Headlines (FP1) and Only Text
(FP2).  First Block Type: Joined (FB2) prints the headline and the first block
of text as one paragraph; three cases are refused with a message in the Error
Log.  The PDF is read with pdftotext.

    STRPRINT=... python3 -I -B tests/printfield.py
"""
import sys as _s; _s.dont_write_bytecode = True
import difflib, os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='hdft')
# Nothing of a test goes into the real home of the user.
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
CRLF = b'\r\n'
EOL = b'\x1d'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
        timeout=300)
    return r.returncode, r.stdout, r.stderr


def marker(t, recs=()):
    return b'\x13' + str(t).encode() + b'(' + b''.join(
        r + CRLF for r in recs) + b'\x13)'


PAGE = marker(22)
SECTION = marker(28)


def trec(text):
    return b'T' + text + EOL + b'\x00' + CRLF


def hfblock(left=b'', centre=b'', right=b''):
    """The block of a header or footer: three columns."""
    return (b'S' + CRLF + b'CW26' + CRLF + b'CFJ0' + CRLF + trec(left) +
            b'CW26' + CRLF + b'CFJ2' + CRLF + trec(centre) + b'CFJ1' + CRLF +
            trec(right))


def header(layouts=(), titlefree=True):
    """The header of a document; layouts is a list of (record, blocks)."""
    h = b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF
    if titlefree:
        h += b'P1HB0' + CRLF + b'P1FB0' + CRLF + b'P1C33' + CRLF
    for rec, blocks in layouts:
        h += rec + CRLF + blocks
    return (h + b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF +
            b'YCL0' + CRLF + b'YIWN2' + CRLF)


def flat(hdr, lines, tag=b'Line'):
    out = hdr + b'*B0' + CRLF + b'S' + CRLF
    for i in range(lines):
        out += (b'' if i == 0 else b'B' + CRLF) + trec(
            tag + b' %d' % (i + 1))
    return out + b'*E' + CRLF


def sections(hdr, secs):
    out = hdr + b'*B1' + CRLF + b'S' + CRLF + trec(b'The Title') + b'+' + CRLF
    for head, n in secs:
        out += b'SE0' + CRLF + trec(head)
        for i in range(n):
            out += b'B' + CRLF + trec(b'Text %d of %s' % (i + 1, head))
    return out + b'-' + CRLF + b'*E' + CRLF


def write(d, name, data):
    p = os.path.join(d, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(data)
    return p


def newdir(name):
    d = os.path.join(tmp, '%s%d' % (name, len(os.listdir(tmp))))
    os.makedirs(d)
    return d


def tex_of(doc):
    d = os.path.dirname(doc)
    out = os.path.join(d, 'x.tex')
    run([strprint, doc, '--export-latex', out])
    t = open(out).read() if os.path.exists(out) else ''
    # The line that names the file is not part of the comparison.
    return re.sub(r'^% Source document: .*$', '% Source document', t,
        flags=re.M)


def pages_of(doc, extra=()):
    """The text of each page of the print, as a list of lines."""
    pdf = doc[:-4] + '.pdf'
    run([strprint, doc, '-o', pdf, '--no-banner'] + list(extra))
    if not os.path.exists(pdf):
        return []
    n = 0
    for l in run(['pdfinfo', pdf])[1].split('\n'):
        if l.startswith('Pages:'):
            n = int(l.split()[1])
    out = []
    for p in range(1, n + 1):
        t = run(['pdftotext', '-f', str(p), '-l', str(p), '-layout', pdf,
            '-'])[1]
        out.append([' '.join(l.split()) for l in t.split('\n') if l.strip()])
    return out



def doc(records=b'', joined_text=None, fonts=None):
    h = header()
    h = h.replace(b'FHA1' + CRLF, b'FHA1' + CRLF + records, 1)
    out = h + b'*B1' + CRLF + b'S' + CRLF + trec(b'The Title') + b'+' + CRLF
    out += b'SE0' + CRLF + trec(b'Alpha') + b'B' + CRLF + trec(
        b'Body one has some words.')
    out += b'+' + CRLF + b'SE0' + CRLF + trec(b'Beta') + b'B' + CRLF + trec(
        b'Body two has other words.') + b'-' + CRLF
    out += b'SE0' + CRLF + trec(b'Gamma') + b'B' + CRLF + trec(
        b'Body three ends it.') + b'-' + CRLF + b'*E' + CRLF
    return out


def text_of(records, extra=(), name='x'):
    d = write(newdir(name), 'doc.doc', doc(records))
    pg = pages_of(d, list(extra))
    return pg, ' | '.join(l for p in pg for l in p)


# ---- Print --------------------------------------------------------------------
pg, t = text_of(b'')
check('Print: Headlines and Text, both are there', '1 Alpha' in t and
    '1.1 Beta' in t and '2 Gamma' in t and 'Body one' in t and
    'Body three' in t, t)
pg, t = text_of(b'FP1' + CRLF, ['--contents', '2'])
check('Only Headlines: the headlines with their tags, no text',
    '1 Alpha' in t and '1.1 Beta' in t and '2 Gamma' in t and
    'Body' not in t, t)
check('Only Headlines: the table of contents is still made',
    'Contents' in t, t)
pg, t = text_of(b'FP2' + CRLF, ['--contents', '2'])
check('Only Text: the text blocks, no headline, no tag',
    'Body one' in t and 'Body two' in t and 'Body three' in t and
    'Alpha' not in t and 'Gamma' not in t and '1.1' not in t, t)
check('Only Text: no table of contents', 'Contents' not in t, t)
check('Only Text: the title page is still printed', 'The Title' in t, t)

# ---- First Block Type: Joined ------------------------------------------------
def joined_doc(records, fonts=b'', two_cols=False):
    h = header().replace(b'FHA1' + CRLF, b'FHA1' + CRLF + records, 1)
    out = h + b'*B1' + CRLF + b'S' + CRLF + trec(b'The Title') + b'+' + CRLF
    out += b'SE0' + CRLF + trec(b'Alpha') + b'B' + CRLF
    if two_cols:
        out += (b'CW20' + CRLF + b'CFJ0' + CRLF + trec(b'Left') +
                b'CFJ0' + CRLF + trec(b'Right'))
    else:
        out += trec(b'Body one has some words that go on and on and on '
                    b'for a long line so that it wraps onto a second line '
                    b'of the paragraph in the print.')
    out += b'-' + CRLF + b'*E' + CRLF
    return out


def joined_text(records, name='j', extra=(), **kw):
    d = write(newdir(name), 'doc.doc', joined_doc(records, **kw))
    pg = pages_of(d, list(extra))
    return pg, [l for p in pg for l in p]


pg, ls = joined_text(b'')
check('not joined: the headline is on a line of its own',
    '1 Alpha' in ls and any(l.startswith('Body one') for l in ls), ls)
pg, ls = joined_text(b'FB2' + CRLF)
one = [l for l in ls if l.startswith('1 Alpha')]
check('Joined: the headline and the text are on one line',
    len(one) == 1 and 'Body one has' in one[0], ls)
check('Joined: two spaces between the headline and the text',
    len(one) == 1 and re.match(r'^1 Alpha\s+Body one', one[0]) is not None,
    one)
check('Joined: the lines that follow are the rest of the same paragraph',
    any('print.' in l or 'paragraph' in l for l in ls), ls)
pg, ls = joined_text(b'FB2' + CRLF + b'FHF4' + CRLF)
check('Joined with different fonts: refused, two lines',
    any(l == '1 Alpha' for l in ls) and any('Cannot join headline/text '
    'blocks - different fonts' in l for l in ls), ls)
pg, ls = joined_text(b'FB2' + CRLF + b'FP1' + CRLF)
check('Joined with Only Headlines: refused ("unprinted") in the Error Log',
    any('Cannot join headline/text blocks - unprinted' in l for l in ls) and
    not any('Body one' in l for l in ls), ls)
pg, ls = joined_text(b'FB2' + CRLF + b'FP2' + CRLF)
check('Joined with Only Text: refused ("unprinted") in the Error Log',
    any('Cannot join headline/text blocks - unprinted' in l for l in ls) and
    any('Body one' in l for l in ls) and not any(l == '1 Alpha' for l in ls),
    ls)
pg, ls = joined_text(b'FB2' + CRLF, two_cols=True)
check('Joined with a block of two columns: refused',
    any('Cannot join multicolumn headline/text blocks' in l for l in ls) and
    any(l.startswith('1 Alpha') for l in ls), ls)
pg, ls = joined_text(b'F1B2' + CRLF)
check('Joined on the panel of level 1 only', any(
    l.startswith('1 Alpha') and 'Body one' in l for l in ls), ls)
pg, ls = joined_text(b'FB2' + CRLF, extra=['--contents', '1'])
check('Joined: the table of contents is unchanged', any(
    re.match(r'^1 Alpha [. ]+ 1$', l) for l in ls), ls)

# ---- the borders of a table (record CB<mask>,<flag>) --------------------------
def bordered(mask0, mask1):
    h = header()
    out = h + b'*B0' + CRLF + b'S' + CRLF
    out += (b'CW20' + CRLF + b'CB' + mask0 + CRLF + trec(b'Left') +
            b'CB' + mask1 + CRLF + trec(b'Right'))
    return out + b'*E' + CRLF


t = tex_of(write(newdir('bd'), 'doc.doc', bordered(b'170,1', b'170,1')))
DBL = ('\\vrule width\\arrayrulewidth \\kern\\doublerulesep '
       '\\vrule width\\arrayrulewidth')
t = tex_of(write(newdir('bd'), 'doc.doc', bordered(b'170,1', b'170,1')))
tab = t.split('\\begin{longtable}')[1]
check('CB170,1: all sides double: a double group of lines at the sides of the '
    'cells and a row of two lines above and below',
    tab.count(DBL) >= 4 and tab.count('\\vskip\\doublerulesep \\hrule') >= 4,
    tab[:400])
t = tex_of(write(newdir('bd'), 'doc.doc', bordered(b'85,1', b'85,1')))
tab = t.split('\\begin{longtable}')[1]
check('CB85,1: all sides single: single lines, a row above and below, no '
    'double group', '\\vrule width\\arrayrulewidth' in tab and
    '\\vbox to \\arrayrulewidth{' in tab and
    '\\doublerulesep' not in tab and '\\cline' not in tab, tab[:400])
t = tex_of(write(newdir('bd'), 'doc.doc', bordered(b'1,1', b'0,1')))
tab = t.split('\\begin{longtable}')[1]
check('CB1,1: a line above the first column only: one rule, a blank box for '
    'the second column and no vertical line',
    tab.count('\\hrule height\\arrayrulewidth width') == 1 and
    'hrule height0pt width' in tab and '\\vrule' not in tab, tab[:400])
t = tex_of(write(newdir('bd'), 'doc.doc', bordered(b'192,1', b'0,1')))
check('CB192,1: a wide line at the right of the first column',
    '\\vrule width1.2pt' in t and 'hrule height1.2pt' not in t,
    t[t.find('multicolumn') - 80:][:300])

shutil.rmtree(tmp)
print('printfield: %s' % ('all passed' if not fails else '%d FAILURES'
                         % fails))
sys.exit(1 if fails else 0)
