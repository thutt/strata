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

"""The label templates (docs/LABELS.txt, tests/labeldata.py, tests/mklabels.py).

(a) the data table: the sheets are consistent, every sheet is on its paper,
there are enough of them for each paper, and no number is twice.
(b) docs/LABELS.txt: its table is the one that the data make, and the numbers
read back from its lines are the numbers of the data.
(c) the templates: the committed ones are what the generator makes; an alias
differs from its sheet in the description only; every template reads and
writes again unchanged, has no text, the right paper, the right number of
blocks and the records of its layout.
(d) the print: every sheet is printed to a PDF twice, with a text in every
line of every sticker, and with a probe in the first line of three stickers
only (the first, the middle one and the last one: the empty lines of the
others must still make the pitch).  The probes are inside their sticker, at
the place that the data give within half a millimetre, and the sheet is one
page.
(e) the Editor: the New file list has the label templates and finds a sheet
by the number that is typed.
(f) the manual: its table of the sheets is made from the data.

    STRPRINT=... STRED=... python3 -I -B tests/labels.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
import labeldata as D
import mklabels as L
import mklabelsdoc as T
import mktemplates as MT

build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
rtrip = os.environ.get('RT', os.path.join(os.path.dirname(stred),
                                          'roundtrip'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint) or not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

TOL = 0.5                       # millimetres
PT = 72 / 25.4                  # points in a millimetre
EOL = b'\x1d'
shipped = os.path.join(here, '..', 'templates')
fails = 0
tmp = tempfile.mkdtemp(prefix='lab')
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.environ['HOME'] = os.path.join(tmp, 'uhome')
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
os.makedirs(os.environ['HOME'])
quick = 'QUICK' in os.environ       # only the first sheet of each paper


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)[:600]))
    fails += 0 if cond else 1


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
        timeout=300)
    return r.returncode, r.stdout, r.stderr


def read(path):
    with open(path, 'rb') as f:
        return f.read()


# ---- (a) the data ---------------------------------------------------------
sheets = D.SHEETS
names = [n for n, num, s in D.every()]
check('no product number is on two sheets, no template name is twice',
      len(set(names)) == len(names) == sum(len(D.numbers(s))
                                           for s in sheets),
      [n for n in names if names.count(n) > 1])
for paper, want in (('us', 25), ('a4', 15)):
    have = [s for s in sheets if s['paper'] == paper]
    check('there are at least %d sheets for %s' % (want, paper),
          len(have) >= want, len(have))
check('the sources of the sheets are the ones that the document names',
      all(s['src'] in D.SOURCES and s['unit'] in D.UNIT_MM for s in sheets))
bad = []
for s in sheets:
    code, pw, ph, pname = D.PAPER[s['paper']]
    right = D.mm(s, 'x0') + (s['nx'] - 1) * D.mm(s, 'dx') + D.mm(s, 'w')
    bottom = D.mm(s, 'y0') + (s['ny'] - 1) * D.mm(s, 'dy') + D.mm(s, 'h')
    if not (right <= pw * 25.4 / 1000 + TOL and
            bottom <= ph * 25.4 / 1000 + TOL and
            s['nx'] <= 9 and s['nx'] >= 1 and s['ny'] >= 1 and
            D.mm(s, 'w') > 5 and D.mm(s, 'h') > 5):
        bad.append((s['num'], right, bottom))
check('every sheet fits its paper (stickers across at most nine)', not bad,
      bad)
check('a round sticker has the same width and height',
      all(s['w'] == s['h'] for s in sheets if s['shape'] != 'rect'))
check('a sheet that is missing has no template',
      not any(m in D.numbers(s) for m in D.MISSING for s in sheets))

# ---- (b) the document -----------------------------------------------------
docpath = os.path.join(here, '..', 'docs', 'LABELS.txt')
doc = open(docpath).read()
check('docs/LABELS.txt has the table that the data make',
      T.splice(doc) == doc)
got = {}
cur = None
for line in doc.split('\n'):
    m = re.match(r'^(\S+)  .*, (\d+) per sheet', line)
    if m:
        cur = m.group(1)
        got[cur] = {'count': int(m.group(2))}
        continue
    m = re.match(r'^    size: (?:w=(\S+) h=(\S+)|r=(\S+)) across=(\d+) '
                 r'down=(\d+)', line)
    if m and cur:
        got[cur].update(w=m.group(1), h=m.group(2), r=m.group(3),
                        nx=int(m.group(4)), ny=int(m.group(5)))
    m = re.match(r'^    place: x0=(\S+) y0=(\S+) dx=(\S+) dy=(\S+) '
                 r'unit=(\S+) src=(\S+)', line)
    if m and cur:
        got[cur].update(x0=float(m.group(1)), y0=float(m.group(2)),
                        dx=float(m.group(3)), dy=float(m.group(4)),
                        unit=m.group(5), src=m.group(6))
okdoc = True
for s in sheets:
    g = got.get(s['num'])
    same = g is not None and g.get('nx') == s['nx'] and \
        g.get('ny') == s['ny'] and g.get('count') == D.count(s) and \
        g.get('unit') == s['unit'] and g.get('src') == s['src'] and \
        all(abs(g[k] - s[k]) < 1e-6 for k in ('x0', 'y0', 'dx', 'dy'))
    if not same:
        okdoc = False
        print('     the document differs for', s['num'], g)
check('the numbers that the lines of the document give are the data', okdoc)
check('every source of the data has its address in the document',
      all(u in doc.replace('\n      ', '') for u in D.SOURCES.values()))
check('every number that is missing is named in the document',
      all(m in doc for m in D.MISSING))

# ---- (c) the templates ----------------------------------------------------
gen = os.path.join(tmp, 'gen')
subprocess.run([sys.executable, '-I', '-B', os.path.join(here, 'mklabels.py'),
                gen], check=True)
have = sorted(f[:-4] for d, _, fs in os.walk(shipped) for f in fs
              if f.startswith('label-'))
check('the templates folder has the label templates of the generator and no '
      'other label template', have == sorted(names), set(have) ^ set(names))
check('the committed label templates are what the generator makes',
      all(os.path.exists(os.path.join(shipped, MT.relpath(n))) and
          read(os.path.join(shipped, MT.relpath(n))) ==
          read(os.path.join(gen, MT.relpath(n))) for n in names))


def strip_desc(raw):
    return re.sub(rb'\r\nMD[^\x00]*\x00', b'', raw)


same = True
for s in sheets:
    base = strip_desc(read(os.path.join(shipped,
                                        MT.relpath(D.key(s['num'], s)))))
    for n in s['also']:
        same = same and strip_desc(read(os.path.join(
            shipped, MT.relpath(D.key(n, s))))) == base
check('a template of an alias differs from the one of its sheet in the '
      'description only', same)


def blocks_of(raw):
    """The records of the blocks, from *B0: a list of lists of records."""
    body = raw[raw.index(b'\r\n*B0') + 2:].split(b'\r\n')
    out = []
    for rec in body[2:]:
        if rec == b'B' or rec == b'*E' or rec == b'':
            if rec == b'B':
                out.append([])
            continue
        if not out:
            out.append([])
        out[-1].append(rec)
    return out


texts_ok = True
count_ok = True
rec_ok = True
paper_ok = True
desc_ok = True
trip_ok = True
for name, num, s in D.every():
    raw = read(os.path.join(shipped, MT.relpath(name)))
    g = L.layout(s)
    code, pw, ph, pname = D.PAPER[s['paper']]
    bl = blocks_of(raw)
    ts = [r[1:] for b in bl for r in b if r[:1] == b'T']
    texts_ok = texts_ok and all(
        t == EOL * g['k'] + b'\x00' for t in ts)
    count_ok = count_ok and len(bl) == D.count(s) == len(ts)
    paper_ok = paper_ok and (b'\r\nDP%d,%d,%d\r\n' % (code, pw, ph)) in raw \
        and (b'\r\nPS%d\r\n' % g['ps']) in raw and \
        (b'\r\nPML%d\r\nPMR%d\r\nPMT%d\r\nPMB%d\r\n' % (
            g['left'], g['right'], g['top'], g['bottom'])) in raw
    rec_ok = rec_ok and all(
        b'BWN%d' % s['nx'] in b and
        b'BWG%d' % max(g['gutter'], 0) in b for b in bl)
    m = re.search(rb'\r\nMD([^\x00]*)\x00', raw)
    d_text = m.group(1).decode('ascii') if m else ''
    desc_ok = desc_ok and d_text == D.describe(num, s) and \
        d_text.startswith('Avery ' + num + ' ') and len(d_text) <= 150
for name, ok in (('has no text: every line of every block is empty', texts_ok),
                 ('has one block for each sticker', count_ok),
                 ('has the paper, the text size and the margins of the '
                  'layout', paper_ok),
                 ('winds every block in the stickers across, with the '
                  'gutter of the pitch', rec_ok),
                 ('has the description: product number, kind, count and '
                  'paper', desc_ok)):
    check('every label template %s' % name, ok)
for name, num, s in D.every():
    if quick and s is not sheets[0]:
        continue
    rc, out, err = run([rtrip, os.path.join(shipped, MT.relpath(name))])
    trip_ok = trip_ok and rc == 0
check('every label template is read and written again unchanged', trip_ok)

# ---- (d) the print --------------------------------------------------------


def with_text(raw, lines_of):
    """The document with the text of every block set by lines_of(n)."""
    pre, post = raw.split(b'\r\n*B0\r\n', 1)
    out = []
    n = 0
    for rec in post.split(b'\r\n'):
        if rec[:1] == b'T':
            out.append(b'T' + b''.join(l + EOL for l in lines_of(n)) +
                       b'\x00')
            n += 1
        else:
            out.append(rec)
    return pre + b'\r\n*B0\r\n' + b'\r\n'.join(out)


def printed(raw, tag):
    path = os.path.join(tmp, tag + '.doc')
    with open(path, 'wb') as f:
        f.write(raw)
    pdf = os.path.join(tmp, tag + '.pdf')
    rc, out, err = run([strprint, path, '-o', pdf, '--no-banner'])
    if rc != 0:
        return None, 0, err[-300:]
    pages = int(re.search(r'Pages:\s+(\d+)', run(['pdfinfo', pdf])[1]).group(1))
    t = run(['pdftotext', '-bbox', pdf, '-'])[1]
    # Nothing but the probes is printed: no page number, no header.
    stray = [w for w in re.findall(r'>([^<>]+)</word>', t)
             if not re.fullmatch(r'P\d+x\d+', w)]
    if stray:
        return None, pages, 'text that is not a probe: %s' % stray[:5]
    words = {}
    for m in re.finditer(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                         r'xMax="([\d.]+)" yMax="([\d.]+)">P(\d+)x(\d+)'
                         r'</word>', t):
        words[(int(m.group(5)), int(m.group(6)))] = tuple(
            float(m.group(i)) / PT for i in range(1, 5))
    return words, pages, ''


def sticker(s, n):
    """The rectangle of sticker n (down the columns): left, top, right,
    bottom in millimetres."""
    c, j = divmod(n, s['ny'])
    dy = D.mm(s, 'dy') if s['ny'] > 1 else D.mm(s, 'h')
    x = D.mm(s, 'x0') + c * D.mm(s, 'dx')
    y = D.mm(s, 'y0') + j * dy
    return x, y, x + D.mm(s, 'w'), y + D.mm(s, 'h')


worst = 0.0
for s in sheets:
    if quick and s is not sheets[0]:
        continue
    num = s['num']
    name = D.key(num, s)
    raw = read(os.path.join(shipped, MT.relpath(name)))
    g = L.layout(s)
    k = g['k']
    n_all = D.count(s)
    line_mm = D.LINE_PT[g['ps']] / PT
    gt = D.GLYPH_TOP_PT[g['ps']] / PT
    # every line of every sticker has a text
    words, pages, err = printed(with_text(raw, lambda n: [
        b'P%dx%d' % (n, i) for i in range(k)]), 'full')
    ok = words is not None and pages == 1 and len(words) == n_all * k
    check('%s: printed with a text in every line: one page, %d texts'
          % (num, n_all * k), ok, (pages, len(words or {}), err))
    if not ok:
        continue
    inside = True
    place = True
    bad = None
    for (n, i), b in words.items():
        r = sticker(s, n)
        inside = inside and b[0] >= r[0] - 0.05 and b[2] <= r[2] + 0.05 and \
            b[1] >= r[1] - 0.05 and b[3] <= r[3] + 0.05
        ex = r[0] + g['pad_x']
        ey = r[1] + g['pad_top'] + i * line_mm + gt
        e = max(abs(b[0] - ex), abs(b[1] - ey))
        worst = max(worst, e)
        if e > TOL and bad is None:
            bad = (n, i, e)
        place = place and e <= TOL
    check('%s: every text is inside its sticker' % num, inside)
    check('%s: every text is where the data put it, within %.1f mm'
          % (num, TOL), place, bad)
    # three probes only: the empty lines of the other stickers make the pitch
    pick = sorted({0, n_all // 2, n_all - 1})
    pick = [p for p in pick if p < n_all]
    words, pages, err = printed(with_text(raw, lambda n: [
        b'P%dx0' % n if n in pick else b''] + [b''] * (k - 1)), 'three')
    ok = words is not None and pages == 1 and \
        sorted(n for n, i in words) == pick
    place = ok
    for n in pick:
        if not ok:
            break
        b = words[(n, 0)]
        r = sticker(s, n)
        e = max(abs(b[0] - (r[0] + g['pad_x'])),
                abs(b[1] - (r[1] + g['pad_top'] + gt)))
        worst = max(worst, e)
        place = place and e <= TOL and b[0] >= r[0] and b[2] <= r[2] and \
            b[1] >= r[1] and b[3] <= r[3]
    check('%s: the blank template, with a probe in stickers %s: inside '
          'the sticker, within %.1f mm of the place' % (num, pick, TOL),
          place, (pages, words, err))
print('     the largest distance from the place of the data: %.3f mm'
      % worst)
check('the largest distance of all the probes is within the tolerance',
      worst <= TOL, worst)
# The layout is made to do better than the tolerance: the spaces between the
# stickers are rounded on the running sum, so the rows do not drift.
check('the largest distance of all the probes is within 0.1 mm (the layout '
      'is made to be exact: no drift down the rows)', worst <= 0.1, worst)

# the sample sheet: the document that New makes from label-us-5160 with the
# addresses filled in; every address is on its own sticker
import mklabelsample
sample = os.path.join(here, '..', 'samples', 'labels',
                      mklabelsample.NAME + '.doc')
check('the committed sample sheet is what the generator makes',
      os.path.exists(sample) and read(sample) == mklabelsample.build().bytes())
rc, out, err = run([strprint, sample, '-o', os.path.join(tmp, 'sample.pdf'),
                    '--no-banner'])
t = run(['pdftotext', '-bbox', os.path.join(tmp, 'sample.pdf'), '-'])[1]
pages = int(re.search(r'Pages:\s+(\d+)', run(['pdfinfo', os.path.join(
    tmp, 'sample.pdf')])[1]).group(1))
boxes = [(float(a) / PT, float(b) / PT, float(c) / PT, float(d), w) for
         a, b, c, d, w in re.findall(
             r'<word xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" '
             r'yMax="([\d.]+)">([^<]*)</word>', t)]
boxes = [(a, b, c, float(d) / PT, w) for a, b, c, d, w in boxes]
s5160 = [x for x in sheets if x['num'] == mklabelsample.NUM][0]
good = rc == 0 and pages == 1
for n in range(D.count(s5160)):
    r = sticker(s5160, n)
    inside = [w for a, b, c, d, w in sorted(boxes, key=lambda x: (x[1], x[0]))
              if a >= r[0] and c <= r[2] and b >= r[1] and d <= r[3]]
    good = good and inside == ' '.join(
        mklabelsample.lines_of(n)).split(' ')
check('the sample sheet prints on one page and every address is on its own '
      'sticker (all %d)' % D.count(s5160), good, (rc, pages, err[-200:]))

# ---- (e) the Editor -------------------------------------------------------
# The list of New is made from the shipped templates, whatever the build
# directory or the settings folder has beside the programs.
os.environ['STRATA_TEMPLATES'] = os.path.abspath(shipped)
NEW = ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'wait']
base = os.path.join(tmp, 'work', 'base.doc')
os.makedirs(os.path.dirname(base))
with open(base, 'wb') as f:
    f.write(b'*R3\r\nMR1000\r\nPP\\1\\\x00\r\nP1C33\r\n*B0\r\nS\r\nTbase\x1d'
            b'\x00\r\n*E\r\n')
d = tui.session([stred, '-H', helpf, base], NEW + ['text:5160', 'wait',
                'show'], rows=30)
check('typing a product number in the list of New selects its sheet and '
      'shows its description',
      'Find: 5160' in d[0] and 'Avery 5160 address labels, 30 per sheet, '
      'US Letter' in d[0], d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['text:l7163', 'wait',
                'show'], rows=30)
check('the number of an A4 sheet is found too',
      'Avery L7163 address labels, 14 per sheet, A4' in d[0], d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['text:8160', 'wait',
                'show'], rows=30)
check('the number of a sheet that has the layout of another one is found',
      'Avery 8160 address labels, 30 per sheet, US Letter' in d[0], d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['text:5160', 'BKSP',
                'wait', 'show'], rows=30)
check('Backspace takes the last typed character back',
      'Find: 516 ' in d[0], d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['text:qqq', 'wait',
                'show'], rows=30)
check('a text that no entry has is not taken: the list shows no search',
      'Find:' not in d[0], d[0])
newd = os.path.join(tmp, 'work', 'fromlabel')
tui.session([stred, '-H', helpf, base], NEW + ['text:5160', 'ENTER',
            'wait', 'DOWN', 'text:' + newd, 'INS', 'wait', 'ENTER', 'wait',
            'C-s', 'wait', 'wait'], rows=30)
data = read(newd + '.doc') if os.path.exists(newd + '.doc') else b''
check('Enter takes the sheet: the new document is a copy of the template '
      '(the paper, the blocks, no text)',
      b'DP0,8500,11000' in data and data.count(b'BWN3') == 30 and
      b'MDAvery 5160 address labels, 30 per sheet, US Letter' in data and
      all(t.replace(EOL, b'') == b'\x00' for t in
          [r[1:] for r in data[data.index(b'*B0'):].split(b'\r\n')
           if r[:1] == b'T']),
      data[:300])

# ---- (f) the manual -------------------------------------------------------
import mklabelsman
man = os.path.join(here, '..', 'manual', 'src', 'labels-table.src')
check('the table of the sheets in the manual is made from the data',
      os.path.exists(man) and read(man).decode() == mklabelsman.text())
mtext = read(os.path.join(here, '..', 'manual', 'src', 'file3.src')).decode()
check('the manual chapter reads the table', '.include labels-table.src'
      in mtext)

shutil.rmtree(tmp, ignore_errors=True)
print('labels: %d failures' % fails)
sys.exit(1 if fails else 0)
