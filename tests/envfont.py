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

"""The size of the text of an envelope.

The header record PS names the size of the text in points (8, 9, 10, 11, 12,
14, 17 or 20).  A document with no record and a page of the size of an
envelope (any envelope of the paper list or of the templates, in either
orientation, whatever paper entry or Custom size the record DP names) has 14
points, any other document 11.  An envelope chosen in the Paper Size list of
the Editor also sets the four page margins to half an inch and the record PS,
so that the print of a document made in the Editor has its text in the corner
and in the larger type, as the print of a template has.
The templates and the samples of the envelopes have the record: 14 points,
11 for the German window envelope (the return line of DIN 5008 is small by
design) and 12 for the European C5 (the sender stays above 28 mm).  The
checks look at the LaTeX that the Printer writes and at the size of the
letters in the PDF.
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, glob, os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
for tool in ('pdflatex', 'pdftotext'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix='envfont')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.makedirs(os.environ['STRATA_HOME'], mode=0o700)
os.environ['TMPDIR'] = tmp
fails = 0
count = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def latex(path):
    """The documentclass line of the LaTeX that the Printer writes."""
    global count
    count += 1
    out = os.path.join(tmp, 'o%d.tex' % count)
    subprocess.run([strprint, path, '--tex-only', '-o', out, '--no-banner'],
                   capture_output=True)
    text = open(out, 'rb').read().decode('latin-1') if os.path.exists(out) \
        else ''
    m = re.search(r'\\documentclass\[(\d+)pt\]\{(\w+)\}', text)

    return (int(m.group(1)), m.group(2)) if m else (0, '')


def height(path):
    """The height in points of the first word of the first line of the
    page: the size of the letters."""
    global count
    count += 1
    pdf = os.path.join(tmp, 'p%d.pdf' % count)
    subprocess.run([strprint, path, '--pdf-via-dvi', '-o', pdf,
                    '--no-banner'], capture_output=True)
    box = subprocess.run(['pdftotext', '-bbox', pdf, '-'],
                         capture_output=True, text=True).stdout
    m = re.search(r'yMin="([0-9.]+)" xMax="[0-9.]+" yMax="([0-9.]+)"', box)

    return float(m.group(2)) - float(m.group(1)) if m else 0.0


def variant(src, old, new, name):
    data = open(src, 'rb').read()
    assert old in data or old == b''
    path = os.path.join(tmp, name)
    open(path, 'wb').write(data.replace(old, new, 1))

    return path


templates = os.path.join(root, 'templates')
samples = os.path.join(root, 'samples', 'envelopes')
EXPECT = {'envelope-de-din5008-dl': 11, 'envelope-eu-c5': 12}

for f in sorted(glob.glob(os.path.join(templates, 'envelopes', 'envelope-*.doc'))):
    name = os.path.basename(f)[:-4]
    want = EXPECT.get(name, 14)
    got = latex(f)
    check('template %s is set in %d points' % (name, want),
          got[0] == want, got)

for f in sorted(glob.glob(os.path.join(samples, 'sample-envelope-*.doc'))):
    name = os.path.basename(f)[len('sample-'):-4]
    want = EXPECT.get(name, 14)
    got = latex(f)
    check('sample %s is set in %d points' % (name, want),
          got[0] == want, got)

us10 = os.path.join(templates, 'envelopes', 'envelope-us-ca-10.doc')
letter = os.path.join(templates, 'letters', 'letter-us.doc')
check('a letter is set in 11 points', latex(letter) == (11, 'article'),
      latex(letter))
check('a size above 11 uses the class with the larger sizes',
      latex(us10) == (14, 'extarticle'), latex(us10))

# No record: the envelope entries of the paper list have 14 points, the
# other papers 11; a record names the size; a size that is not one of the
# sizes is not taken.
bare = variant(us10, b'PS14\r\n', b'', 'bare.doc')
check('an envelope paper with no PS record has 14 points',
      latex(bare)[0] == 14, latex(bare))
custom = variant(bare, b'DP13,9500,4125', b'DP16,5125,5125', 'custom.doc')
check('a Custom paper that is no envelope size with no PS record has 11 '
      'points', latex(custom)[0] == 11, latex(custom))
# The size decides, not the index of the record: a Custom paper (16) of the
# size of the number 10 envelope, and the sides the other way round.
cust10 = variant(bare, b'DP13,9500,4125', b'DP16,9500,4125', 'cust10.doc')
check('a Custom paper of the size of an envelope has 14 points',
      latex(cust10)[0] == 14, latex(cust10))
turned = variant(bare, b'DP13,9500,4125', b'DP0,4125,9500', 'turned.doc')
check('an envelope size stored with the sides the other way round has 14 '
      'points', latex(turned)[0] == 14, latex(turned))
small = variant(us10, b'PS14\r\n', b'PS11\r\n', 'small.doc')
check('the record PS11 gives 11 points on an envelope paper',
      latex(small) == (11, 'article'), latex(small))
bad = variant(us10, b'PS14\r\n', b'PS13\r\n', 'bad.doc')
check('a size that is not one of the sizes (13) gives 11 points',
      latex(bad)[0] == 11, latex(bad))
big = variant(letter, b'DP0,8500,11000', b'PS20\r\nDP0,8500,11000', 'big.doc')
check('the record PS20 gives 20 points on a letter', latex(big)[0] == 20,
      latex(big))

# The size shows in the PDF: the letters of the number 10 envelope are higher
# than the letters of the same text at 11 points.
# The templates hold no text, so the letters are measured in the example copy
# of the template (tests/mktemplates.py --example).
exdir = os.path.join(tmp, 'example')
subprocess.run([sys.executable, '-I', '-B',
                os.path.join(here, 'mktemplates.py'), '--example', exdir],
               check=True)
us10ex = os.path.join(exdir, 'envelopes', 'envelope-us-ca-10.doc')
small_ex = variant(us10ex, b'PS14\r\n', b'PS11\r\n', 'small-ex.doc')
h14 = height(us10ex)
h11 = height(small_ex)
check('the letters of the number 10 envelope are higher than at 11 points '
      '(%.1f against %.1f points)' % (h14, h11), h11 > 0 and h14 > h11 * 1.15,
      (h14, h11))

# The number 9 envelope: the template without its PS record and margins, as
# a document of the owner has it when only the paper was chosen.  It is the
# case that a printed envelope showed small type for.
us9 = os.path.join(templates, 'envelopes', 'envelope-us-ca-9.doc')
bare9 = variant(us9, b'PS14\r\n', b'', 'bare9.doc')
check('the number 9 envelope (Custom size 8 7/8 x 3 7/8) with no PS record has '
      '14 points', latex(bare9)[0] == 14, latex(bare9))
SIZES = {'Monarch': (7500, 3875), 'Number 10': (9500, 4125),
         'Number 9': (8875, 3875), 'DL': (8661, 4331), 'C5': (9016, 6378),
         'C6/5': (9016, 4488), 'C6': (6378, 4488), 'Yougata 4': (9252, 4134)}
for name, (w, h) in sorted(SIZES.items()):
    rec = ('DP16,%d,%d' % (w, h)).encode()
    doc = variant(us9, b'DP16,8875,3875', rec, 'size-%s.doc' % name.replace(
        '/', ''))
    doc = variant(doc, b'PS14\r\n', b'', 'nops-%s.doc' % name.replace('/', ''))
    check('the %s envelope size with no PS record has 14 points' % name,
          latex(doc)[0] == 14, latex(doc))

# A business envelope that measures a little more than the list says.  The
# first rule matched the sizes of the table exactly and printed these in 11
# points: the owner's number 10 envelopes are 9.5 by 4.25 inches.  A page
# within 0.3 inch of an envelope of the table, or with the proportions of an
# envelope, is an envelope, in either orientation.
NEAR = {'9.5 x 4.125': (9500, 4125), '9.5 x 4.25': (9500, 4250),
        '9.5 x 4.5': (9500, 4500), '9.25 x 4.0': (9250, 4000),
        '8.875 x 3.875': (8875, 3875), '9.5 x 4.75': (9500, 4750),
        '10 x 4.5': (10000, 4500),
        # The owner's real envelope, 240 by 104 mm (9.449 by 4.094 in): the
        # number 10 of the table, within 0.002 inch of it on the short side.
        '240 x 104 mm': (9449, 4094),
        # Near sizes of the table that the proportions do not catch (a C5
        # and a C6 envelope are 1.4 times as long as they are wide).
        'C5 9.2 x 6.5': (9200, 6500), 'C6 6.5 x 4.5': (6500, 4500)}
for name, (w, h) in sorted(NEAR.items()):
    for turn, (a, b) in (('wide', (w, h)), ('tall', (h, w))):
        rec = ('DP16,%d,%d' % (a, b)).encode()
        tag = name.replace(' ', '')
        doc = variant(us9, b'DP16,8875,3875', rec,
                      'near-%s-%s.doc' % (tag, turn))
        doc = variant(doc, b'PS14\r\n', b'', 'near-ps-%s-%s.doc' % (tag, turn))
        check('the size %s (%s) with no PS record has 14 points' %
              (name, turn), latex(doc)[0] == 14, latex(doc))

# Pages that are not envelopes keep 11 points: the paper of a letter, A4, a
# 6 by 9 inch page, half a letter, an index card and a square.
NOT = {'US Letter': (8500, 11000), 'A4': (8268, 11693),
       '6 x 9 inches': (6000, 9000), 'half letter 5.5 x 8.5': (5500, 8500),
       'index card 4 x 6': (4000, 6000), 'a square 5 x 5': (5000, 5000),
       # Ratio 1.8: below the proportions of an envelope (1.9).
       '6.5 x 3.6 inches': (6500, 3600)}
for name, (w, h) in sorted(NOT.items()):
    for turn, (a, b) in (('tall', (w, h)), ('wide', (h, w))):
        rec = ('DP16,%d,%d' % (a, b)).encode()
        tag = name.replace(' ', '').replace('.', '')
        doc = variant(us9, b'DP16,8875,3875', rec,
                      'not-%s-%s.doc' % (tag, turn))
        doc = variant(doc, b'PS14\r\n', b'', 'not-ps-%s-%s.doc' % (tag, turn))
        check('the page %s (%s) is no envelope: 11 points with no PS record' %
              (name, turn), latex(doc)[0] == 11, latex(doc))

# ---- the Editor: an envelope chosen in the Paper Size list -------------------
MM = 72.0 / 25.4


CORNER = 7.0


def near5(points):
    """True when a distance in points is 7 mm within 0.5 mm: the owner's
    decision of 2026-11-20 about the place of the return address (5 mm at
    first, then 2 mm more after the first print on the printer)."""
    return abs(points / MM - CORNER) <= 0.5


PANEL = ['F12', 'text:G', 'RIGHT', 'ENTER', 'wait']
PAPER_FIELD = ['DOWN'] * 11
CHOOSE = [('Monarch', 12, 7500, 3875), ('Number 10', 13, 9500, 4125),
          ('DL', 14, 8661, 4331), ('C5', 15, 9016, 6378),
          ('Number 9', 16, 8875, 3875), ('C6/5', 17, 9016, 4488),
          ('C6, Yougata 2', 18, 6378, 4488), ('Yougata 4', 19, 9252, 4134)]


def sender_doc(name):
    """A document with only a sender, on letter paper: no PS record and no
    margins, as a new document is."""
    data = open(us9, 'rb').read()
    data = re.sub(rb'PM[LRTB]\d+\r\n', b'', data)
    for old in (b'PS14\r\n', b'MR1000\r\n'):
        data = data.replace(old, b'')
    data = data.replace(b'DP16,8875,3875', b'DP0,8500,11000')
    body = data.index(b'*B0')
    path = os.path.join(tmp, name)
    open(path, 'wb').write(data[:body] + b'*B0\r\nB\r\nTSender line one\x1d'
                           b'Sender line two\x1d\x00\r\n*E\r\n')

    return path


def records(path):
    got = {}
    for line in open(path, 'rb').read().split(b'\r\n'):
        for code in (b'DP', b'PS', b'PML', b'PMR', b'PMT', b'PMB'):
            if line.startswith(code) and code not in got:
                got[code] = line[len(code):].decode('latin-1')

    return got


def first_word(path):
    """The page size in points and the box of the first word of the PDF."""
    pdf = path[:-4] + '.pdf'
    subprocess.run([strprint, path, '-o', pdf, '--no-banner'],
                   capture_output=True)
    info = subprocess.run(['pdfinfo', pdf], capture_output=True,
                          text=True).stdout
    size = re.search(r'Page size:\s+([0-9.]+) x ([0-9.]+)', info)
    box = subprocess.run(['pdftotext', '-bbox', pdf, '-'],
                         capture_output=True, text=True).stdout
    m = re.search(r'xMin="([0-9.]+)" yMin="([0-9.]+)" xMax="[0-9.]+" '
                  r'yMax="([0-9.]+)"', box)
    if size is None or m is None:
        return None

    return (float(size.group(1)), float(size.group(2)), float(m.group(1)),
            float(m.group(2)), float(m.group(3)) - float(m.group(2)))


def choose(path, entry, more=None):
    tui.session([stred, '-H', helpf, path], PANEL + (more or []) +
                PAPER_FIELD + ['F2', 'wait'] + ['DOWN'] * entry +
                ['ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'], rows=30)


# What the letters of 11 points measure, for the comparison: the same text on
# letter paper.
plain = sender_doc('plain.doc')
w11 = first_word(plain)
for name, entry, w, h in CHOOSE:
    path = sender_doc('pick-%d.doc' % entry)
    choose(path, entry)
    rec = records(path)
    check('choosing the %s envelope stores %d,%d,%d' % (name, entry, w, h),
          rec.get(b'DP') == '%d,%d,%d' % (entry, w, h), rec)
    check('choosing the %s envelope sets the type size, the left margin of 7 '
          'mm (276), the top margin that puts the first glyph box 7 mm down '
          '(219 at 14 points) and half an inch right and below (%s)' %
          (name, rec), rec.get(b'PS') == '14' and
          rec.get(b'PML') == '276' and rec.get(b'PMT') == '219' and
          all(rec.get(k) == '500' for k in (b'PMR', b'PMB')), rec)
    got = first_word(path)
    check('the %s envelope prints on a page %d by %d points' %
          (name, round(w * 0.072), round(h * 0.072)), got is not None and
          abs(got[0] - w * 0.072) < 1.5 and abs(got[1] - h * 0.072) < 1.5, got)
    check('the first word of the %s envelope is within 0.5 mm of 7 mm from '
          'the left and the top edge (%s points)' % (name, got and got[2:4]),
          got is not None and near5(got[2]) and near5(got[3]), got)
    check('the type of the %s envelope is larger than 11 points (%s against '
          '%s)' % (name, got and got[4], w11 and w11[4]),
          got is not None and w11 is not None and got[4] > w11[4] * 1.15,
          (got, w11))

# The margins are set once, at the choice.  A margin that the same visit of
# the panel changed stays; choosing a letter after an envelope leaves the
# margins as they are; a PS record that the document has stays.
path = sender_doc('mine.doc')
choose(path, 16, ['text:1.5'])
rec = records(path)
check('a left margin changed in the same visit of the panel stays (1.5) '
      'and the other margins are those of an envelope', rec.get(b'PML') ==
      '1500' and rec.get(b'PMT') == '219' and
      all(rec.get(k) == '500' for k in (b'PMR', b'PMB')), rec)
path = sender_doc('back.doc')
choose(path, 16)
tui.session([stred, '-H', helpf, path], PANEL + PAPER_FIELD + ['F2', 'wait',
            'HOME', 'ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'], rows=30)
rec = records(path)
check('choosing Letter after an envelope leaves the margins and the type '
      'size as they are', rec.get(b'DP') == '0,8500,11000' and
      rec.get(b'PML') == '276' and rec.get(b'PS') == '14', rec)
path = os.path.join(tmp, 'keeps.doc')
open(path, 'wb').write(open(sender_doc('k0.doc'), 'rb').read().replace(
    b'DP0,8500,11000', b'PS20\r\nDP0,8500,11000'))
choose(path, 16)
check('a record PS that the document has stays when an envelope is chosen',
      records(path).get(b'PS') == '20', records(path))
path = sender_doc('plainletter.doc')
choose(path, 8)
check('choosing A4 sets neither margins nor type size',
      b'PML' not in records(path) and b'PS' not in records(path),
      records(path))

# Undo takes the whole choice back.
path = sender_doc('undo.doc')
tui.session([stred, '-H', helpf, path], PANEL + PAPER_FIELD + ['F2', 'wait'] +
            ['DOWN'] * 16 + ['ENTER', 'wait', 'INS', 'wait', 'C-z',
            'wait', 'C-s', 'wait'], rows=30)
rec = records(path)
check('Undo takes the paper, the margins and the type size back together',
      rec.get(b'DP') == '0,8500,11000' and b'PML' not in rec and
      b'PS' not in rec, rec)

# ---- the return address: 7 mm from the left and the top edge ---------------
# The owner's decision of 2026-11-20: the first glyph box of the sender, in
# the PDF, is within 0.5 mm of 7 mm from the left and the top edge, for every
# envelope that has a sender (the Japanese envelopes have none).  The
# templates hold no text, so the example copies of the generator are printed.
for f in sorted(glob.glob(os.path.join(exdir, 'envelopes', 'envelope-*.doc'))):
    name = os.path.basename(f)[:-4]
    if 'yougata' in name:
        continue
    got = first_word(f)
    check('the sender of the example copy of %s is 7 mm from the left and '
          'the top edge (%s points)' % (name, got and got[2:4]),
          got is not None and near5(got[2]) and near5(got[3]), got)

# The owner's real envelope, 240 x 104 mm (9.449 x 4.094 inches), as a Custom
# page in both orientations, with the margins of the template.
for turn, (a, b) in (('wide', (9449, 4094)), ('tall', (4094, 9449))):
    doc = variant(us10ex, b'DP13,9500,4125', ('DP16,%d,%d' % (a, b)).encode(),
                  'mine-%s.doc' % turn)
    got = first_word(doc)
    check('the sender on a Custom page of 240 x 104 mm (%s) is 7 mm from the '
          'left and the top edge (%s points)' % (turn, got and got[2:4]),
          got is not None and near5(got[2]) and near5(got[3]), got)

sys.exit(1 if fails else 0)
