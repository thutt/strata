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

"""Templates: the description of a document, the list of templates of New, and
the templates that Strata ships (templates/, made by tests/mktemplates.py).

(a) the description (the record MD): a document with a description that is too
long or has a control character is refused by the loader; the list shows the
description of a made template, and none for a template that has none or has
a bad one; the panels refuse a description that is too long.
(b) the list: templates in a made folder (STRATA_TEMPLATES) are listed in
order with their descriptions; Enter takes the chosen one (the new document is
a copy of it), Esc takes the built-in settings.
(c) every shipped template is printed to a PDF; pdftotext shows the parts in
order; the pixels show the sender block of a letter at the right and the
address blocks of the envelopes where tests/mktemplates.py puts them.

    STRPRINT=... STRED=... python3 -I -B tests/templates.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
import mktemplates
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
for tool in ('pdflatex', 'pdftotext', 'pdftoppm', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint) or not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='tmpl')
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


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
        timeout=300, **kw)
    return r.returncode, r.stdout, r.stderr


def doc(desc, body, mdraw=None):
    """A document: desc is the text of MD (None: no record); body the text."""
    h = b'*R3' + CRLF
    if mdraw is not None:
        h += b'MD' + mdraw + b'\x00' + CRLF
    elif desc is not None:
        h += b'MD' + desc.encode() + b'\x00' + CRLF
    h += (b'MR1000' + CRLF + b'PP\\1\\\x00' + CRLF + b'P1C33' + CRLF +
          b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF)
    return (h + b'*B0' + CRLF + b'S' + CRLF + b'T' + body + EOL + b'\x00' +
            CRLF + b'*E' + CRLF)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return path


# ---- (a) the description ------------------------------------------------
dd = os.path.join(tmp, 'desc')
d250 = 'd' * 250
d251 = 'd' * 251
write(dd + '/ok250.doc', doc(d250, b'ok'))
write(dd + '/long251.doc', doc(d251, b'long'))
write(dd + '/ctl.doc', doc(None, b'ctl', mdraw=b'bad\x01text'))
write(dd + '/none.doc', doc(None, b'none'))
for name, ok in (('ok250', True), ('none', True), ('long251', False),
                 ('ctl', False)):
    rc, out, err = run([strprint, dd + '/%s.doc' % name, '--tex-only', '-o',
        dd + '/%s.tex' % name])
    if ok:
        check('the loader takes a document with %s' % (
            'a description of 250 characters' if name == 'ok250' else
            'no description'), rc == 0, (rc, err[-200:]))
    else:
        check('the loader refuses a description with %s' % (
            'more than 250 characters' if name == 'long251' else
            'a control character'),
            rc != 0 and 'description' in (out + err), (rc, err[-200:]))

# ---- (b) the list of templates -------------------------------------------
tdir = os.path.join(tmp, 'tpl')
write(tdir + '/alpha.doc', doc('Alpha description text one', b'ALPHA BODY'))
write(tdir + '/beta.doc', doc('Beta description text two', b'BETA BODY'))
write(tdir + '/gamma.doc', doc(None, b'GAMMA BODY'))
write(tdir + '/delta.set', doc('Delta is a set file', b'DELTA BODY'))
write(tdir + '/long.doc', doc(d251[:0] + 'x' * 10, b'LONG BODY',
    mdraw=b'x' * 251))
write(tdir + '/notes.doc', b'just some notes, not a document\n')
write(tdir + '/readme.txt', b'*R2 but not a template name\n')
os.environ['STRATA_TEMPLATES'] = tdir
work = os.path.join(tmp, 'work')
base = write(work + '/base.doc', doc(None, b'BASE TEXT'))
NEW = ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'wait']
d = tui.session([stred, '-H', helpf, base], NEW + ['wait', 'show'], rows=30)
scr = d[0]
check('New shows the list of templates with the built-in settings first',
    'Templates' in scr and 'Structured' in scr and 'Unstructured' in scr,
    scr)
order = [scr.find(w) for w in ('Structured', 'Unstructured', 'alpha', 'beta',
    'delta', 'gamma', 'long')]
check('the list has the templates in order of their names, and not the file '
    'that is no document or the one that is no .doc or .set',
    all(o >= 0 for o in order) and order == sorted(order) and
    'notes' not in scr and 'readme' not in scr, (order, scr))
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'DOWN', 'wait',
    'show'], rows=30)
check('the description of the chosen template is shown below the list',
    'Alpha description text one' in d[0] and 'Beta description' not in d[0],
    d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN'] * 3 + ['wait',
    'show'], rows=30)
check('another template shows its own description',
    'Beta description text two' in d[0] and 'Alpha description' not in d[0],
    d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN'] * 5 + ['wait',
    'show'], rows=30)
check('a template with no description says so',
    '(no description)' in d[0], d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN'] * 6 + ['wait',
    'show'], rows=30)
check('a template with a description that is too long shows none',
    '(no description)' in d[0] and 'xxxxxxxx' not in d[0], d[0])

newa = os.path.join(work, 'fromalpha')
tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'DOWN', 'ENTER',
    'wait', 'DOWN', 'text:' + newa, 'INS', 'wait', 'ENTER', 'wait', 'C-s',
    'wait', 'wait'], rows=30)
data = open(newa + '.doc', 'rb').read() if os.path.exists(newa + '.doc') \
    else b''
check('Enter takes the chosen template: the new document is a copy of it',
    b'ALPHA BODY' in data and b'BASE TEXT' not in data and
    b'MDAlpha description text one\x00' in data, data[:300])
newb = os.path.join(work, 'frombuilt')
tui.session([stred, '-H', helpf, base], NEW + ['ESC', 'wait', 'DOWN',
    'text:' + newb, 'INS', 'wait', 'ENTER', 'wait', 'C-s', 'wait', 'wait'],
    rows=30)
data = open(newb + '.doc', 'rb').read() if os.path.exists(newb + '.doc') \
    else b''
check('Esc takes the built-in settings: a new document that is not a copy',
    data != b'' and b'ALPHA BODY' not in data and b'BETA BODY' not in data and
    b'GAMMA BODY' not in data, data[:300])
# a description typed in the New panel is refused when it is too long
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'ENTER', 'wait',
    'DOWN', 'text:' + os.path.join(work, 'longdesc'), 'DOWN', 'DOWN',
    'text:' + 'q' * 251, 'INS', 'wait', 'wait', 'show'], rows=30)
check('the New panel rejects a description of more than 250 characters',
    'at most 250 characters' in d[0], d[0])
# the Document Manager lists them too
str_prog = os.path.join(os.path.dirname(stred), 'str')
mgr = os.path.join(tmp, 'mgr')
os.makedirs(mgr)
d = tui.session([str_prog, '-H', helpf, mgr], ['ENTER', 'text:mgrnew', 'INS',
    'wait', 'wait', 'show'], rows=30)
check('the Document Manager shows the templates with their descriptions '
    'when a new document is asked for',
    'Templates' in d[0] and 'alpha' in d[0] and 'Structured' in d[0], d[0])
del os.environ['STRATA_TEMPLATES']

# ---- (c) the shipped templates ---------------------------------------------
shipped = os.path.join(here, '..', 'templates')
names = ['letter-us', 'letter-a4'] + sorted(mktemplates.ENVELOPES)
# The label sheets have their own test (tests/labels.py).
import labeldata
labels = [n for n, num, sheet in labeldata.every()]
check('the templates folder has every template of the generator',
    all(os.path.exists(os.path.join(shipped, n + '.doc'))
        for n in names + labels),
    os.listdir(shipped) if os.path.isdir(shipped) else 'no folder')
check('the templates folder has no template that the generator does not '
    'make (a renamed template leaves no old file)',
    sorted(f[:-4] for f in os.listdir(shipped) if f.endswith('.doc')) ==
    sorted(names + labels), sorted(os.listdir(shipped)))
gen = os.path.join(tmp, 'gen')
subprocess.run([sys.executable, '-I', '-B',
     os.path.join(here, 'mktemplates.py'),
    gen], check=True)
same = all(os.path.exists(os.path.join(shipped, n + '.doc')) and
    open(os.path.join(shipped, n + '.doc'), 'rb').read() ==
    open(os.path.join(gen, n + '.doc'), 'rb').read()
    for n in names + labels)
check('the committed templates are what the generator makes', same)

# A template holds no text: the page set-up, the formats and the empty blocks.
# The text of the examples (the sender, the recipient, the body of the
# letter) is made only on request, and the copies that hold it are what the
# places of the blocks below are measured on.
exgen = os.path.join(tmp, 'example')
subprocess.run([sys.executable, '-I', '-B',
    os.path.join(here, 'mktemplates.py'), '--example', exgen], check=True)


def body_texts(raw):
    """The text of the blocks of a document: every T record after *B0 with
    the markers and the ends of line taken out."""
    body = raw[raw.index(b'\r\n*B0') + 2:]
    # A marker has records of its own between its two ends: take it out whole.
    body = re.sub(rb'\x13\d+\(.*?\x13\)', b'', body, flags=re.S)
    out = []
    for rec in body.split(b'\r\n'):
        if rec[:1] == b'T':
            out.append(rec[1:].replace(b'\x1d', b'').replace(b'\x00', b''))
    return out


def records_without_text(raw):
    """The records of a document except the T records (the text)."""
    return [r for r in raw.split(b'\r\n') if r[:1] != b'T']


for n in names:
    raw = open(os.path.join(shipped, n + '.doc'), 'rb').read()
    ex = open(os.path.join(exgen, n + '.doc'), 'rb').read()
    check('%s: the template holds no text of its own (every block is an '
        'empty paragraph, a marker only for a field)' % n,
        all(t == b'' for t in body_texts(raw)) and
        any(t != b'' for t in body_texts(ex)), body_texts(raw)[:4])
    check('%s: the example copy has the same formatting records as the '
        'template, the text apart' % n,
        records_without_text(raw) == records_without_text(ex),
        (len(records_without_text(raw)), len(records_without_text(ex))))
    check('%s: the template still has the paper, the text size and the '
        'blocks of the address' % n,
        re.search(rb'\r\nDP\d+,\d+,\d+\r\n', raw) is not None and
        (n.startswith('letter') or re.search(rb'\r\nPS\d+\r\n', raw)
         is not None) and b'\r\n*B0\r\nS\r\n' in raw and
        b'BGRecipient' in raw, raw[:200])

pdfs = os.path.join(tmp, 'pdfs')
os.makedirs(pdfs)
expdfs = os.path.join(tmp, 'expdfs')
os.makedirs(expdfs)
for n in names:
    shutil.copy(os.path.join(exgen, n + '.doc'), expdfs)
    run([strprint, os.path.join(expdfs, n + '.doc'), '-o',
        os.path.join(expdfs, n + '.pdf'), '--no-banner'])
    shutil.copy(os.path.join(shipped, n + '.doc'), pdfs)
    rc, out, err = run([strprint, os.path.join(pdfs, n + '.doc'), '-o',
        os.path.join(pdfs, n + '.pdf'), '--no-banner'])
    check('%s: prints to a PDF with no error' % n,
        rc == 0 and os.path.exists(os.path.join(pdfs, n + '.pdf')),
        (rc, err[-300:]))
    printed = run(['pdftotext', os.path.join(pdfs, n + '.pdf'), '-'])[1]
    check('%s: the printed template shows none of the example text' % n,
        not any(w in printed for w in ('Strata', 'Example', 'Jane', 'Write',
                                       'Sincerely', 'Main Street',
                                       'Ejemplo', 'Exemple', 'Mustermann',
                                       '123-4567')), printed[:200])
    desc = ''
    raw = open(os.path.join(shipped, n + '.doc'), 'rb').read()
    m = re.search(rb'\r\nMD([^\x00]*)\x00', raw)
    check('%s: has a description' % n, m is not None and
        10 < len(m.group(1)) <= 250, m)
    # The list of New shows the description in three lines of 52 characters
    # (src/widgets.c, popup_list_desc: a list 56 wide with 2 columns of
    # frame and 2 of margin on each side, put_wrapped, three lines): a longer
    # description is cut off, so every template must say all it has to say in
    # three lines.
    d_text = m.group(1).decode('ascii') if m is not None else ''
    d_lines = []
    rest = d_text
    while rest:
        if len(rest) <= 52:
            d_lines.append(rest)
            break
        cut = 52
        while cut > 0 and rest[cut] != ' ':
            cut -= 1
        cut = cut if cut > 0 else 52
        d_lines.append(rest[:cut])
        rest = rest[cut:].lstrip(' ')
    check('%s: the description fits the three lines of 52 characters that '
        'the list of New shows (%d lines)' % (n, len(d_lines)),
        0 < len(d_lines) <= 3, d_text)


def pages(pdf):
    out = run(['pdfinfo', pdf])[1]
    size = re.search(r'Page size:\s+([\d.]+) x ([\d.]+)', out)
    n = int(re.search(r'Pages:\s+(\d+)', out).group(1))
    return n, float(size.group(1)), float(size.group(2))


def ink_blocks(pdf, dpi=100):
    """The boxes of the blocks of ink of page 1 in millimetres: (x0, y0, x1,
    y1) of each group of rows with a gap of less than 10 pixels."""
    r = subprocess.run(['pdftoppm', '-r', str(dpi), '-f', '1', '-l', '1',
        pdf], capture_output=True).stdout
    p = r.split(b'\n', 3)
    w, h = map(int, p[1].split())
    px = p[3]
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
    return [tuple(round(v * mm, 1) for v in g) for g in out], w * mm, h * mm


# DIN 680, the window of a DL window envelope: 90 by 45 mm, 20 mm from the
# left edge and 15 mm from the bottom edge (sources: the data sheets of the
# printers of window envelopes, docs/ENVELOPES.txt).  The folded letter, A4
# folded in three by DIN 5008 (the fold marks at 105 and 210 mm), is 210 mm
# wide and 105 mm high and has 10 mm of play across and 5 mm up and down in
# the 220 by 110 mm envelope.  The address field of DIN 5008 form B is 85 by
# 45 mm, 20 mm from the left and 45 mm from the top edge of the letter, its
# address zone begins 17.7 mm below the top of the field.
DIN_ENV = (220.0, 110.0)
DIN_WINDOW = (20.0, DIN_ENV[1] - 15.0 - 45.0, 20.0 + 90.0, DIN_ENV[1] - 15.0)
DIN_FOLD = (210.0, 105.0)
DIN_FIELD = (20.0, 45.0, 20.0 + 85.0, 45.0 + 45.0)
DIN_ADDRESS_TOP = 45.0 + 17.7
MARGIN = 3.0


# letters
for n, w, h in (('letter-us', 612, 792), ('letter-a4', 595.3, 841.9)):
    pdf = os.path.join(expdfs, n + '.pdf')
    if not os.path.exists(pdf):
        continue
    np_, pw, ph = pages(pdf)
    check('%s: the page is %s' % (n, 'US Letter' if w == 612 else 'A4'),
        abs(pw - w) < 1 and abs(ph - h) < 1, (pw, ph))
    txt = run(['pdftotext', '-layout', pdf, '-'])[1]
    order = ['Strata Sample Company', 'Ms. Jane Example', 'Dear Ms. Example,',
             'Write the first paragraph', 'Write the second paragraph',
             'Sincerely,', 'John Q. Writer', 'cc:', 'encl:', 'P.S.']
    pos = [txt.find(o) for o in order]
    check('%s: the parts are in the order of a letter' % n,
        all(p >= 0 for p in pos) and pos == sorted(pos), (pos, txt))
    lines = [l for l in txt.split('\n') if l.strip()]
    ind = lambda s: len(s) - len(s.lstrip())
    sender = next(l for l in lines if 'Strata Sample Company' in l)
    rcpt = next(l for l in lines if 'Ms. Jane Example' in l)
    clos = next(l for l in lines if 'Sincerely,' in l)
    body = next(l for l in lines if 'Write the first paragraph' in l)
    check('%s: the sender, the date, the closing and the signature are at '
        'the right, the recipient, the salutation and the body at the left'
        % n, ind(sender) > 30 and ind(clos) > 30 and ind(rcpt) < 5 and
        ind(body) < 5, [ind(sender), ind(clos), ind(rcpt), ind(body)])
    boxes, wmm, hmm = ink_blocks(pdf)
    sx = boxes[0][0]
    check('%s: the sender block starts 3.5 inches (89 mm) right of the left '
        'margin, within 3 mm' % n, abs(sx - (25.4 + 88.9)) < 3.0 and
        abs(boxes[0][1] - 25.4) < 8, boxes[:2])
    if n == 'letter-a4':
        # The recipient of the A4 letter is in the address zone of DIN 5008
        # form B (17.7 mm below the top of the field at 45 mm) and shows in
        # the window of a DL window envelope when the letter is folded in
        # three: for every place of the folded letter in the envelope (10 mm
        # of play across, 5 mm up and down) the block is 3 mm or more inside
        # the window of DIN 680.  The numbers are those of the sources
        # (DIN_*), not of the generator.
        rb = boxes[1]
        check('%s: the recipient is in the address zone of DIN 5008, 17.7 mm '
            'below the top of the field at 45 mm (%.1f mm), within 2 mm, '
            'and in the field, 85 mm wide from 20 mm' % (n, DIN_ADDRESS_TOP),
            abs(rb[1] - DIN_ADDRESS_TOP) < 2.0 and rb[0] >= DIN_FIELD[0] and
            rb[2] <= DIN_FIELD[2] and rb[3] <= DIN_FIELD[3], boxes[:3])
        wx0, wy0, wx1, wy1 = DIN_WINDOW
        worst = []
        for dx in (0.0, DIN_ENV[0] - DIN_FOLD[0]):
            for dy in (0.0, DIN_ENV[1] - DIN_FOLD[1]):
                ok = (rb[0] + dx >= wx0 + MARGIN and rb[2] + dx <=
                      wx1 - MARGIN and rb[1] + dy >= wy0 + MARGIN and
                      rb[3] + dy <= wy1 - MARGIN)
                if not ok:
                    worst.append((dx, dy))
        check('%s: folded in three into a DL window envelope the recipient '
            'is 3 mm or more inside the window for every place of the '
            'letter (play of 10 mm across, 5 mm up and down)' % n,
            not worst, (worst, rb, DIN_WINDOW))
    # a long letter: the first page has no head, the next one has it
    raw = open(os.path.join(exgen, n + '.doc'), 'rb').read()
    more = b''.join(b'B' + CRLF + b'BGBody\x00' + CRLF + b'TFiller paragraph '
        + b'%d ' % i + b'of the letter goes on and on and on and on and on '
        b'and on and on and on.' + EOL + b'\x00' + CRLF for i in range(40))
    i = raw.index(b'B' + CRLF + b'BGClosing')
    lg = os.path.join(expdfs, n + '-long.doc')
    write(lg, raw[:i] + more + raw[i:])
    run([strprint, lg, '-o', lg[:-4] + '.pdf', '--no-banner'])
    pg = [[l.strip() for l in run(['pdftotext', '-f', str(k), '-l', str(k),
        '-layout', lg[:-4] + '.pdf', '-'])[1].split('\n') if l.strip()]
        for k in (1, 2)]
    check('%s: the first page has no head, the second has the recipient, the '
        'date and the page number' % n,
        pg[0][0].startswith('Strata Sample Company') and
        re.match(r'^Ms\. Jane Example - .+ - Page 2$', pg[1][0]) is not None,
        (pg[0][:1], pg[1][:1]))

# envelopes
# The sizes of the envelopes in millimetres (width, height) and the text that
# the description names, from the sources of docs/ENVELOPES.txt (not from the
# generator, so that a wrong size in the generator is found), and the rule
# set that the place of the address meets: 'rm' the margins of Royal Mail
# (the address block 15 mm from the top, left and right edge and 18 mm from
# the bottom edge, its last line more than 50 mm from the top edge, the
# return address no lower than 28 mm from the top edge, the address out of
# the stamp area of 40 mm by 75 mm at the top right), 'usps' the reading area
# of the United States Postal Service (12.7 mm from the left and the right
# edge, 15.9 mm from the bottom edge, not higher than 69.9 mm above it),
# 'din' the address field of DIN 5008 (85 mm wide from 20 mm) in the window
# of DIN 680, 'fr' the address zone of La Poste (SP 8855 volume 4: the left
# edge of the address at half the width or more from the left edge, 20 mm
# from the right and the bottom edge, the top of its first line 120 mm or
# less above the bottom edge), 'au' the barcode and delivery address zone of
# Australia Post (PreSort Letters guide 2.5.1: 15 mm from the bottom edge,
# 100 mm or less up from it, 10 mm from the left and right edge, out of the
# postage zone of 90 by 40 mm at the top right), 'jp4' the left window of JIS
# S 5502 for the Yougata 4 (90 by 45 mm, 22 mm from the left and 12 mm from
# the top edge) and 'jp2' the two characters from the left edge of Japan Post
# for an envelope without a window.
SOURCES = {
    'envelope-us-ca-10': (241.3, 104.8, '4 1/8 x 9 1/2 in', 'usps'),
    'envelope-us-ca-9': (225.4, 98.4, '3 7/8 x 8 7/8 in', 'usps'),
    'envelope-us-ca-monarch': (190.5, 98.4, '3 7/8 x 7 1/2 in', 'usps'),
    'envelope-uk-c5': (229.0, 162.0, '162 x 229 mm', 'rm'),
    'envelope-uk-dl': (220.0, 110.0, '110 x 220 mm', 'rm'),
    'envelope-uk-c6': (162.0, 114.0, '114 x 162 mm', 'rm'),
    'envelope-de-din5008-dl': (220.0, 110.0, '110 x 220 mm', 'din'),
    'envelope-fr-dl': (220.0, 110.0, '110 x 220 mm', 'fr'),
    'envelope-eu-dl': (220.0, 110.0, '110 x 220 mm', 'rm'),
    'envelope-eu-c5': (229.0, 162.0, '162 x 229 mm', 'rm'),
    'envelope-eu-c65': (229.0, 114.0, '114 x 229 mm', 'rm'),
    'envelope-latam-dl': (220.0, 110.0, '110 x 220 mm', 'rm'),
    'envelope-au-dl': (220.0, 110.0, '110 x 220 mm', 'au'),
    'envelope-au-c5': (229.0, 162.0, '162 x 229 mm', 'au'),
    'envelope-jp-yougata-4': (235.0, 105.0, '235 x 105 mm', 'jp4'),
    'envelope-jp-yougata-2': (162.0, 114.0, '162 x 114 mm', 'jp2'),
}
check('the sources name every envelope of the generator, and no other',
    sorted(SOURCES) == sorted(mktemplates.ENVELOPES), sorted(SOURCES))
check('there are 12 to 20 envelopes, a list that can be scanned',
    12 <= len(mktemplates.ENVELOPES) <= 20, len(mktemplates.ENVELOPES))
for n in sorted(mktemplates.ENVELOPES):
    pdf = os.path.join(expdfs, n + '.pdf')
    if not os.path.exists(pdf):
        continue
    (desc, code, w, h, sender, rcpt, sl, rl) = mktemplates.ENVELOPES[n]
    sw, sh, token, rules = SOURCES[n]
    has_sender = sender is not None
    check('%s: the description names the size (%s) and is within 250 '
        'characters' % (n, token), token in desc and len(desc) <= 250, desc)
    np_, pw, ph = pages(pdf)
    check('%s: one landscape page of the size of the source, %.0f x %.0f mm,'
        ' within 0.6 mm' % (n, sw, sh), np_ == 1 and
        abs(pw / 72 * 25.4 - sw) < 0.6 and abs(ph / 72 * 25.4 - sh) < 0.6 and
        pw > ph, (np_, pw / 72 * 25.4, ph / 72 * 25.4))
    boxes, wmm, hmm = ink_blocks(pdf)
    if not has_sender:
        # an envelope that has the recipient only (the sender is on the back)
        got_s = None
        ok_r = len(boxes) == 1 and abs(boxes[0][0] - rcpt[0]) < 3.0 and \
            abs(boxes[0][1] - rcpt[1]) < 3.0
    elif n == 'envelope-de-din5008-dl':
        # the sender line and the recipient are one group of rows or two;
        # the first box starts at the sender, the last block ends the recipient
        got_s = boxes[0][:2]
        got_r = (boxes[-1][0], boxes[-1][1] if len(boxes) > 1 else None)
        ok_r = len(boxes) >= 2 and abs(got_r[0] - rcpt[0]) < 3.0 and \
            abs(got_r[1] - rcpt[1]) < 3.0
    else:
        got_s = boxes[0][:2]
        ok_r = len(boxes) == 2 and abs(boxes[1][0] - rcpt[0]) < 3.0 and \
            abs(boxes[1][1] - rcpt[1]) < 3.0
    if has_sender:
        check('%s: the sender block is at (%.0f, %.0f) mm, within 3 mm' % (n,
            sender[0], sender[1]), abs(got_s[0] - sender[0]) < 3.0 and
            abs(got_s[1] - sender[1]) < 3.0, (boxes, sender))
    else:
        check('%s: no sender block on the front (it goes on the back): one '
            'block of ink' % n, len(boxes) == 1, boxes)
    check('%s: the recipient block is at (%.0f, %.0f) mm, within 3 mm' % (n,
        rcpt[0], rcpt[1]), ok_r, (boxes, rcpt))
    sb, rb = boxes[0], boxes[-1]
    if rules == 'din':
        # The address field of DIN 5008 is 85 mm wide and begins 20 mm from
        # the left edge of the letter (docs/ENVELOPES.txt).  The letter lies
        # in the middle of the envelope (5 mm of play across), so the field
        # is 25 mm from the left edge of the envelope.  The sender is in the
        # corner (the owner's decision of 2026-11-20), so the window shows
        # the recipient only: it must not run out of the field.
        fx0 = DIN_FIELD[0] + (DIN_ENV[0] - DIN_FOLD[0]) / 2.0
        check('%s: the recipient lies inside the address field, 85 mm wide '
            'from %.0f mm' % (n, fx0),
            rb[0] >= fx0 - 1.0 and rb[2] <= fx0 + 85.0 + 0.5, boxes)
        # the window of DIN 680 shows what stands in it: the recipient is at
        # least 3 mm inside the window (20 mm from the left, 15 mm from the
        # bottom, 90 by 45 mm), and the sender is outside it, in the corner.
        wx0, wy0, wx1, wy1 = DIN_WINDOW
        check('%s: the recipient is at least %.0f mm inside the window of '
            'DIN 680 (%.0f..%.0f mm across, %.0f..%.0f mm down)' % (n,
            MARGIN, wx0, wx1, wy0, wy1), rb[0] >= wx0 + MARGIN and
            rb[1] >= wy0 + MARGIN and rb[2] <= wx1 - MARGIN and
            rb[3] <= wy1 - MARGIN, (boxes, DIN_WINDOW))
        check('%s: the sender is in the corner, outside the window (it is '
            'a decision of the owner: the window shows the recipient only)'
            % n, sb[3] < wy0, (boxes, DIN_WINDOW))
        check('%s: the recipient is in the address zone: 17.7 mm below the '
            'top of the field, which is the window top plus 5 mm of play' % n,
            rb[1] >= wy0 + 5.0 + 17.7 - 3.0 - 2.0, (boxes, wy0))
    elif rules == 'fr':
        check('%s: the address is in the right half (half the width or more '
            'from the left edge), 20 mm or more from the right and the '
            'bottom edge, its first line 120 mm or less above the bottom '
            'edge (La Poste, SP 8855 volume 4)' % n,
            rb[0] >= wmm / 2.0 and wmm - rb[2] >= 20.0 and
            hmm - rb[3] >= 20.0 and hmm - rb[1] <= 120.0, (boxes, wmm, hmm))
        check('%s: the sender is at the top left, no lower than 28 mm from '
            'the top edge, 20 mm or more from the address' % n,
            sb[3] <= 28.0 and sb[0] < wmm / 2.0 and
            (rb[0] - sb[2] >= 20.0 or rb[1] - sb[3] >= 20.0), boxes)
    elif rules == 'au':
        check('%s: the address is 15 mm or more from the bottom edge, 100 mm '
            'or less up from it, 10 mm or more from the left and right '
            'edge, and out of the postage zone of 90 by 40 mm at the top '
            'right (Australia Post)' % n,
            hmm - rb[3] >= 15.0 and hmm - rb[1] <= 100.0 and
            rb[0] >= 10.0 and wmm - rb[2] >= 10.0 and
            (rb[1] >= 40.0 or rb[2] <= wmm - 90.0), (boxes, wmm, hmm))
        check('%s: the return address is out of the postage zone and no '
            'closer than 12 mm to the address' % n,
            (sb[1] >= 40.0 or sb[2] <= wmm - 90.0) and
            (rb[0] - sb[2] >= 12.0 or rb[1] - sb[3] >= 12.0), boxes)
    elif rules == 'jp4':
        # the left window of JIS S 5502 (22 mm from the left, 12 mm from the
        # top, 90 by 45 mm): the ink 3 mm or more inside it
        check('%s: the address is 3 mm or more inside the left window of JIS '
            'S 5502 (90 by 45 mm, 22 mm from the left, 12 mm from the top)'
            % n, rb[0] >= 22.0 + MARGIN and rb[1] >= 12.0 + MARGIN and
            rb[2] <= 22.0 + 90.0 - MARGIN and rb[3] <= 12.0 + 45.0 - MARGIN,
            boxes)
    elif rules == 'jp2':
        # no window: the address starts about two characters (8 mm or more)
        # from the left edge and in the upper half
        check('%s: the address starts 8 mm or more from the left edge and '
            'in the upper half, 3 mm or more from the top' % n,
            rb[0] >= 8.0 and rb[1] >= 3.0 and rb[3] <= hmm * 0.6, boxes)
    elif rules == 'usps':
        # sb and rb are in millimetres from the top left corner.
        check('%s: the recipient is in the reading area of the Postal '
            'Service: 12.7 mm from the left and the right edge, 15.9 mm '
            'from the bottom edge, not above 69.9 mm from it' % n,
            rb[0] >= 12.7 and wmm - rb[2] >= 12.7 and hmm - rb[3] >= 15.9 and
            hmm - rb[1] <= 69.9, (boxes, wmm, hmm))
    elif rules == 'rm':
        check('%s: the recipient is 15 mm from the top, left and right edge '
            'and 18 mm from the bottom edge, its last line is more than 50 '
            'mm from the top, and it is out of the stamp area (Royal Mail)'
            % n, rb[0] >= 15.0 and rb[1] >= 15.0 and wmm - rb[2] >= 15.0 and
            hmm - rb[3] >= 18.0 and rb[3] - 3.3 > 50.0 and
            (rb[1] >= 40.0 or rb[2] <= wmm - 75.0), (boxes, wmm, hmm))
        check('%s: the sender is no lower than 28 mm from the top edge and '
            'no closer than 12 mm to the recipient (Royal Mail)' % n,
            sb[3] <= 28.0 and (rb[0] - sb[2] >= 12.0 or
            rb[1] - sb[3] >= 12.0), boxes)
    txt = run(['pdftotext', '-layout', pdf, '-'])[1]
    if has_sender:
        check('%s: the sender comes before the recipient in the text' % n,
            txt.find(sl[0]) >= 0 and txt.find(rl[0]) > txt.find(sl[0]), txt)
    else:
        check('%s: the recipient is in the text, the postal code first' % n,
            txt.find(rl[0]) >= 0 and txt.find(rl[1]) > txt.find(rl[0]), txt)

shutil.rmtree(tmp)
print('templates: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
