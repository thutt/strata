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

"""The sample business letters that Strata ships (samples/letters/, made by
tests/mkletters.py).

(a) the folder has every letter of the generator, in both papers, and no
other file; the committed files are what the generator makes, and two runs of
the generator make the same bytes.
(b) the letters use the letter style: every block of every letter carries a
named format, the named formats of the style have the records of the
templates letter-us and letter-a4, a letter adds only formats that it names,
and no block has a format that is set by hand.
(c) every letter opens in the Editor and prints to a PDF of its paper; the
parts are in the order of a letter; the recipient of an A4 letter is in the
window of a DL window envelope for every place of the folded letter; the
letter of two pages has the head on its second page; the table of the
quotation has a bold header row with a double line under it and single lines
under the other rows (the rule of the tables of the manuals); the letter in a
sans serif face prints in it and the others do not; the name of the utility
that has an ampersand is written, and printed, as it is.

    STRPRINT=... STRED=... python3 -I -B tests/letters.py

LETTERS_DIR names another folder of letters to test (the mutations of the
test are run that way).
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
import mkletters
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
for tool in ('pdflatex', 'pdftotext', 'pdftoppm', 'pdfinfo', 'pdffonts'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint) or not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='ltr')
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
CRLF = b'\r\n'
shipped = os.environ.get('LETTERS_DIR') or os.path.join(
    here, '..', 'samples', 'letters')
templates = os.path.join(here, '..', 'templates')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
        timeout=300, **kw)
    return r.returncode, r.stdout, r.stderr


def read(path):
    with open(path, 'rb') as f:
        return f.read()


# ---- (a) the folder -----------------------------------------------------
wanted = mkletters.names()
have = sorted(os.listdir(shipped)) if os.path.isdir(shipped) else []
check('the folder has every letter of the generator, in both papers (%d)'
    % len(wanted), all(n in have for n in wanted), (wanted, have))
check('the folder has no file that the generator does not make (a renamed '
    'letter leaves no old file)', have == wanted, have)
check('there are at least five letters, each in US Letter and in A4',
    len(mkletters.LETTERS) >= 5 and
    all(n + '-us.doc' in wanted and n + '-a4.doc' in wanted
        for n in mkletters.LETTERS), wanted)
gen = os.path.join(tmp, 'gen')
gen2 = os.path.join(tmp, 'gen2')
for g in (gen, gen2):
    subprocess.run([sys.executable, '-I', '-B',
                    os.path.join(here, 'mkletters.py'), g], check=True)
check('the committed letters are what the generator makes',
    all(os.path.exists(os.path.join(shipped, n)) and
        read(os.path.join(shipped, n)) == read(os.path.join(gen, n))
        for n in wanted))
check('two runs of the generator make the same bytes',
    all(read(os.path.join(gen, n)) == read(os.path.join(gen2, n))
        for n in wanted))


# ---- (b) the style --------------------------------------------------------
def records(path):
    return read(path).split(CRLF)


def named_formats(recs):
    """The named formats of a document: name -> list of records."""
    out = {}
    cur = None
    for r in recs:
        if r.startswith(b'*B'):
            break
        if r.startswith(b'FN'):
            cur = r[2:].rstrip(b'\x00').decode()
            if cur:
                out[cur] = []
            else:
                cur = None
        elif cur is not None:
            out[cur].append(r)
    return out


def blocks(recs):
    """The blocks of the body: each is a dictionary with the named format
    (bg), the other records of the block (props) and the columns, a list of
    (records of the column, text)."""
    out = []
    cur = None
    pend = []
    i = 0
    while i < len(recs) and not recs[i].startswith(b'*B'):
        i += 1
    for r in recs[i + 1:]:
        if r in (b'S', b'B', b'*E'):
            if cur is not None and cur['cols']:
                out.append(cur)
            cur = {'bg': None, 'props': [], 'cols': []}
            pend = []
        elif cur is None:
            continue
        elif r.startswith(b'BG'):
            cur['bg'] = r[2:].rstrip(b'\x00').decode()
        elif r.startswith(b'B'):
            cur['props'].append(r)
        elif r.startswith(b'C'):
            pend.append(r)
        elif r.startswith(b'T'):
            cur['cols'].append((pend, r[1:].rstrip(b'\x00')))
            pend = []
    return out


STYLE = mkletters.STYLE
EXTRA = mkletters.EXTRA
COLUMN_OK = (b'CW', b'CB', b'CL', b'CR', b'CF')
for kind, tpl in (('us', 'letter-us.doc'), ('a4', 'letter-a4.doc')):
    tfmt = named_formats(records(os.path.join(templates, 'letters', tpl)))
    bad = []
    unnamed = []
    handset = []
    unknown = []
    for n in wanted:
        if not n.endswith('-%s.doc' % kind) or n not in have:
            continue
        recs = records(os.path.join(shipped, n))
        fmts = named_formats(recs)
        for name in STYLE:
            if fmts.get(name) != tfmt.get(name):
                bad.append((n, name))
        for name in fmts:
            if name not in STYLE and name not in EXTRA:
                unknown.append((n, name))
        for b in blocks(recs):
            if b['bg'] is None:
                unnamed.append((n, b['cols'][0][1][:30]))
            elif b['bg'] not in fmts:
                unknown.append((n, b['bg']))
            if b['props']:
                handset.append((n, b['bg'], b['props']))
            for cprops, _ in b['cols']:
                for c in cprops:
                    if not c.startswith(COLUMN_OK):
                        handset.append((n, b['bg'], c))
    paper = 'US Letter' if kind == 'us' else 'A4'
    check('%s letters: the named formats of the style have the records of '
        'the template %s' % (paper, tpl), not bad, bad[:5])
    check('%s letters: every block carries a named format' % paper,
        not unnamed, unnamed[:5])
    check('%s letters: no block has a format that is set by hand' % paper,
        not handset, handset[:5])
    check('%s letters: the formats that a letter adds are %s, and no other'
        % (paper, ', '.join(EXTRA)), not unknown, unknown[:5])


# ---- (c) open, print, read ---------------------------------------------------
work = os.path.join(tmp, 'work')
os.makedirs(work)
pdfs = {}
for n in wanted:
    src = os.path.join(shipped, n)
    if not os.path.exists(src):
        continue
    shutil.copy(src, work)
    pdf = os.path.join(work, n[:-4] + '.pdf')
    rc, out, err = run([strprint, os.path.join(work, n), '-o', pdf,
                        '--no-banner'])
    check('%s: prints to a PDF with no error' % n,
        rc == 0 and os.path.exists(pdf), (rc, err[-300:]))
    if rc == 0 and os.path.exists(pdf):
        pdfs[n[:-4]] = pdf
    raw = read(src)
    m = re.search(rb'\r\nMD([^\x00]*)\x00', raw)
    check('%s: has a description of a letter' % n, m is not None and
        10 < len(m.group(1)) <= 250 and b'letter' in m.group(1), m)
    low = raw.lower()
    check('%s: does not name the original product' % n,
        b'lotus' not in low and b'manuscript' not in low)


def pages(pdf):
    out = run(['pdfinfo', pdf])[1]
    size = re.search(r'Page size:\s+([\d.]+) x ([\d.]+)', out)
    n = int(re.search(r'Pages:\s+(\d+)', out).group(1))
    return n, float(size.group(1)), float(size.group(2))


def pdf_text(pdf, first=None, last=None, layout=True):
    cmd = ['pdftotext'] + (['-layout'] if layout else [])
    if first:
        cmd += ['-f', str(first), '-l', str(last or first)]
    return run(cmd + [pdf, '-'])[1]


def image(pdf, dpi, page=1):
    """The pixels of a page: (width, height, rgb bytes)."""
    r = subprocess.run(['pdftoppm', '-r', str(dpi), '-f', str(page), '-l',
        str(page), pdf], capture_output=True).stdout
    p = r.split(b'\n', 3)
    w, h = map(int, p[1].split())
    return w, h, p[3]


def ink_blocks(pdf, dpi=100):
    """The boxes of the blocks of ink of page 1 in millimetres: (x0, y0, x1,
    y1) of each group of rows with a gap of less than 10 pixels."""
    w, h, px = image(pdf, dpi)
    out = []
    cur = None
    gap = 0
    for y in range(h):
        row = px[y * w * 3:(y + 1) * w * 3]
        xs = [x for x in range(w) if row[3 * x] < 160 or row[3 * x + 1] < 160
              or row[3 * x + 2] < 160]
        if xs:
            if cur is None:
                cur = [min(xs), y, max(xs), y]
            else:
                cur = [min(cur[0], min(xs)), cur[1], max(cur[2], max(xs)), y]
            gap = 0
        else:
            gap += 1
            if cur is not None and gap > 10:
                out.append(cur)
                cur = None
    if cur is not None:
        out.append(cur)
    mm = 25.4 / dpi
    return [tuple(round(v * mm, 1) for v in g) for g in out]


# DIN 680, the window of a DL window envelope: 90 by 45 mm, 20 mm from the
# left edge and 15 mm from the bottom edge.  A4 folded in three by DIN 5008
# is 210 mm wide and 105 mm high and lies in the 220 by 110 mm envelope with
# 10 mm of play across and 5 mm up and down; the address zone of DIN 5008
# form B begins 17.7 mm below the top of the address field at 45 mm
# (tests/templates.py has the sources).
DIN_ENV = (220.0, 110.0)
DIN_WINDOW = (20.0, DIN_ENV[1] - 15.0 - 45.0, 20.0 + 90.0, DIN_ENV[1] - 15.0)
DIN_FOLD = (210.0, 105.0)
DIN_FIELD = (20.0, 45.0, 20.0 + 85.0, 45.0 + 45.0)
DIN_ADDRESS_TOP = 45.0 + 17.7
MARGIN = 3.0

PAPER = {'us': (612, 792), 'a4': (595.3, 841.9)}
# What each letter must show, in the order of the page: the lines are found
# in the text of the PDF.
SIDES = {'us': 'Fennimore Fabrication Co.', 'a4': 'Fennimore Fabrication Ltd'}
FACTS = {
    'enquiry': ['Enquiry: steel mounting brackets', 'Dear M',
                'Robert Hale'],
    'quotation': ['Quotation 2026-0314', 'Total for one order',
                  'Robert Hale', 'encl: Terms of sale'],
    'complaint-reply': ['Your complaint about delivery', 'Dear M',
                        'Robert Hale', 'cc: Customer service file'],
    'complaint-to-utility': ['Disputed bill: account 40-1183-7752, meter '
                             'GM-2209-4417', 'Dear Sir or Madam,',
                             'Helena Brandt', 'encl: The bill dated',
                             'encl: Photograph of the meter',
                             'encl: The bills of the last year',
                             'cc: Consumer advice office'],
    'order-confirmation': ['Order 2026-0301: confirmation', 'Dear M',
                           'Robert Hale', 'P.S.'],
    'enclosures': ['Renewal of the warehouse lease', 'Dear M', 'Robert Hale',
                   'encl: Renewal form', 'encl: Certificate of insurance',
                   'encl: Floor plan', 'cc: Ms. Irene Vale',
                   'cc: Mr. Paul Reeve'],
    'two-pages': ['Review of the supply agreement', 'Dear M', 'Robert Hale',
                  'cc: Mr. Paul Reeve'],
    'sans-serif': ['New opening hours from April 1', 'Dear M',
                   'Robert Hale'],
}

for name in mkletters.LETTERS:
    for kind in ('us', 'a4'):
        key = '%s-%s' % (name, kind)
        pdf = pdfs.get(key)
        if pdf is None:
            continue
        n, pw, ph = pages(pdf)
        w, h = PAPER[kind]
        check('%s: the page is %s' % (key, 'US Letter' if kind == 'us' else
            'A4'), abs(pw - w) < 1 and abs(ph - h) < 1, (pw, ph))
        check('%s: the letter has %s' % (key, '2 pages' if name ==
            'two-pages' else 'one page'),
            n == (2 if name == 'two-pages' else 1), n)
        txt = pdf_text(pdf)
        party = 'Helena Brandt' if name == 'complaint-to-utility' \
            else SIDES[kind]
        signer = 'Helena Brandt' if name == 'complaint-to-utility' \
            else 'Robert Hale'
        closing = re.search(r'Sincerely,|Yours sincerely,|Yours faithfully,',
                            txt)
        at = [txt.find(party), txt.find(FACTS[name][0]), txt.find('Dear '),
              closing.start() if closing else -1, txt.rfind(signer)]
        check('%s: the parts are in the order of a letter: sender, subject, '
            'salutation, closing, signature' % key,
            all(v >= 0 for v in at) and at == sorted(at), (at, txt[:600]))
        lines = [l for l in txt.split('\n') if l.strip()]
        ind = lambda s: len(s) - len(s.lstrip())
        sender = next((l for l in lines if party in l), '')
        salute = next((l for l in lines if l.strip().startswith('Dear ')), '')
        clos = next((l for l in lines if l.strip().startswith(
            ('Sincerely,', 'Yours sincerely,', 'Yours faithfully,'))), '')
        check('%s: the sender, the date, the closing and the signature are '
            'at the right, the salutation at the left' % key,
            ind(sender) > 30 and ind(clos) > 30 and ind(salute) < 5,
            [ind(sender), ind(clos), ind(salute)])
        for fact in FACTS[name]:
            check('%s: the PDF shows "%s"' % (key, fact), fact in txt,
                txt[:400])
        boxes = ink_blocks(pdf)
        check('%s: the sender block starts 3.5 inches (89 mm) right of the '
            'left margin, within 3 mm' % key,
            abs(boxes[0][0] - (25.4 + 88.9)) < 3.0 and
            abs(boxes[0][1] - 25.4) < 8, boxes[:2])
        if kind == 'a4':
            rb = boxes[1]
            check('%s: the recipient is in the address zone of DIN 5008, '
                '17.7 mm below the top of the field (%.1f mm), within 2 mm, '
                'and in the field' % (key, DIN_ADDRESS_TOP),
                abs(rb[1] - DIN_ADDRESS_TOP) < 2.0 and
                rb[0] >= DIN_FIELD[0] and rb[2] <= DIN_FIELD[2] and
                rb[3] <= DIN_FIELD[3], boxes[:3])
            wx0, wy0, wx1, wy1 = DIN_WINDOW
            worst = []
            for dx in (0.0, DIN_ENV[0] - DIN_FOLD[0]):
                for dy in (0.0, DIN_ENV[1] - DIN_FOLD[1]):
                    if not (rb[0] + dx >= wx0 + MARGIN and
                            rb[2] + dx <= wx1 - MARGIN and
                            rb[1] + dy >= wy0 + MARGIN and
                            rb[3] + dy <= wy1 - MARGIN):
                        worst.append((dx, dy))
            check('%s: folded in three into a DL window envelope the '
                'recipient is 3 mm or more inside the window for every '
                'place of the letter (10 mm of play across, 5 mm up and '
                'down)' % key, not worst, (worst, rb, DIN_WINDOW))
        fonts = run(['pdffonts', pdf])[1]
        check('%s: %s' % (key, 'prints in a sans serif face' if name ==
            'sans-serif' else 'prints in a roman face, with no sans serif '
            'face'), ('Sans' in fonts) == (name == 'sans-serif'), fonts)

# the Editor opens every letter
for n in wanted:
    if n not in have:
        continue
    d = tui.session([stred, '-H', helpf, os.path.join(work, n)],
                    ['wait', 'show'], rows=30)
    scr = d[0]
    first = 'Helena Brandt' if n.startswith('complaint-to-utility') \
        else 'Fennimore Fabrication'
    check('%s: opens in the Editor, with the sender at the top and no '
        'error' % n, first in scr and 'rror' not in scr and
        'annot' not in scr, scr)

# the two pages: the head on the second page, none on the first
for kind in ('us', 'a4'):
    pdf = pdfs.get('two-pages-%s' % kind)
    if pdf is None:
        continue
    p1 = [l.strip() for l in pdf_text(pdf, 1).split('\n') if l.strip()]
    p2 = [l.strip() for l in pdf_text(pdf, 2).split('\n') if l.strip()]
    date = mkletters.DATE[kind].decode()
    check('two-pages-%s: the first page has no head and the second page has '
        'the recipient, the date and the page number' % kind,
        p1[0].startswith('Fennimore') and
        p2[0] == 'Ms%s Dana Whitcombe - %s - Page 2' % (
            '.' if kind == 'us' else '', date), (p1[:1], p2[:1]))

# the table of the quotation
for kind in ('us', 'a4'):
    key = 'quotation-%s' % kind
    pdf = pdfs.get(key)
    if pdf is None:
        continue
    dpi = 200
    w, h, px = image(pdf, dpi)
    mm = 25.4 / dpi
    x0 = int(25.4 / mm) + 4
    x1 = int((w * mm - 25.4) / mm) - 4
    lines = []
    for y in range(h):
        row = px[y * w * 3:(y + 1) * w * 3]
        dark = sum(1 for x in range(x0, x1) if row[3 * x] < 128)
        if dark >= 0.95 * (x1 - x0):
            if lines and y - lines[-1][1] <= 1:
                lines[-1][1] = y
            else:
                lines.append([y, y])
    gaps = [round((b[0] - a[1]) * mm, 2) for a, b in zip(lines, lines[1:])]
    check('%s: the table has seven lines across: a single line at the top, '
        'a double line under the header row and a single line under each '
        'of the four other rows' % key, len(lines) == 7, (lines, gaps))
    check('%s: only the line under the header row is double (the two lines '
        'of it are less than 1.5 mm apart, the others at least 3 mm)' % key,
        len(lines) == 7 and gaps[1] < 1.5 and
        all(g >= 3.0 for i, g in enumerate(gaps) if i != 1), gaps)
    raw = read(os.path.join(shipped, key + '.doc'))
    rows = [b for b in blocks(raw.split(CRLF)) if len(b['cols']) > 1]
    check('%s: the header row of the table is bold, the other rows are not'
        % key, len(rows) == 5 and all(c[1].startswith(b'\x12\x01\x00')
        for c in rows[0]['cols']) and not any(b'\x12\x01\x00' in c[1][:3]
        for r in rows[1:4] for c in r['cols']), rows[:1])
    def masks(row):
        return [[p for p in c[0] if p.startswith(b'CB')] for c in row['cols']]

    check('%s: the rows use the borders of the manual tables (header: 89, 73, '
        '73, 73; other rows: 84, 68, 68, 68)' % key, len(rows) == 5 and
        masks(rows[0]) == [[b'CB89,1'], [b'CB73,1'], [b'CB73,1'],
                           [b'CB73,1']] and
        all(masks(r) == [[b'CB84,1'], [b'CB68,1'], [b'CB68,1'],
                         [b'CB68,1']] for r in rows[1:]), rows[:2])

# the name with the ampersand
for kind in ('us', 'a4'):
    key = 'complaint-to-utility-%s' % kind
    pdf = pdfs.get(key)
    if pdf is None:
        continue
    raw = read(os.path.join(shipped, key + '.doc'))
    check('%s: the address block of the document has the name Pathetic '
        'Gas & Deceptive with a plain ampersand' % key,
        b'Customer Accounts Department\x1dPathetic Gas & Deceptive\x1d'
        in raw)
    check('%s: the text of the PDF has "Pathetic Gas & Deceptive"' % key,
        'Pathetic Gas & Deceptive' in pdf_text(pdf, layout=False),
        pdf_text(pdf)[:400])
    tex = os.path.join(work, key + '.tex')
    run([strprint, os.path.join(work, key + '.doc'), '--tex-only', '-o', tex,
         '--no-banner'])
    src = read(tex).decode('utf-8', 'replace') if os.path.exists(tex) else ''
    check('%s: the LaTeX has the ampersand written as \\&, in the address '
        'and in the head of the next pages' % key,
        re.search(r'Customer Accounts Department.{0,60}Pathetic Gas \\& '
                  r'Deceptive', src, re.S) is not None and
        'Pathetic Gas \\& Deceptive - ' in src and
        'Pathetic Gas & Deceptive' not in src, src[:200])

# the letters name no real company: the names that they use are made up
check('the utility and the firm are made up (example.com addresses only)',
    all(b'@' not in read(os.path.join(shipped, n)) or
        all(a.rstrip(b'.').endswith(b'example.com') for a in re.findall(
            rb'[\w.]+@[\w.]+', read(os.path.join(shipped, n))))
        for n in wanted if os.path.exists(os.path.join(shipped, n))))

print('letters: %s' % ('all passed' if not fails else '%d failed' % fails))
sys.exit(1 if fails else 0)
