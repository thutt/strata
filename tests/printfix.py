#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Tests of the faults of the print that the check of the manual found:
the Whitespace marker (its size and its treatment), the indent of the
header and of the footer, the style of the cross-reference, the example of the
keycap scale, and the draft print of a Comment.  Each document is made here,
printed with strprint to a PDF, and the PDF is read with pdftotext.

    STRPRINT=... python3 -I -B tests/printfix.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))

for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)

tmp = tempfile.mkdtemp(prefix='printfix')
fails = 0
checks = 0
CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)


def check(what, ok, detail=''):
    global fails, checks
    checks += 1

    if ok:
        print('ok   %s' % what)
    else:
        fails += 1
        print('FAIL %s\n%s' % (what, detail))


def marker(t, recs=()):
    """A marker with its records."""
    return (b'\x13' + str(t).encode() + b'(' +
            b''.join(r + CRLF for r in recs) + b'\x13)')


def document(blocks, header=HEADER):
    """A document of blocks of one column each; a block is bytes."""
    out = header + b'*B0' + CRLF + b'S' + CRLF

    for i, b in enumerate(blocks):
        out += (b'' if i == 0 else b'B' + CRLF) + b'T' + b + b'\x00' + CRLF

    return out + b'*E' + CRLF


def lines(prefix, n):
    """N lines, each with a word that occurs nowhere else: L01 line."""
    return b''.join(b'%s%02d line' % (prefix, i) + EOL
                    for i in range(1, n + 1))


def printed(source, args=()):
    """The path of the PDF of a document, or None."""
    work = tempfile.mkdtemp(prefix='run', dir=tmp)
    src = os.path.join(work, 'doc.doc')

    with open(src, 'wb') as f:
        f.write(source)

    pdf = os.path.join(work, 'out.pdf')
    p = subprocess.run([strprint, src, '-o', pdf] + list(args),
                       capture_output=True, text=True, cwd=work, timeout=600)

    return pdf if p.returncode == 0 and os.path.exists(pdf) else None


def pages(pdf):
    """The words of each page: a list of lists of (text, x, y), x and y the
    top left corner in points."""
    info = subprocess.run(['pdfinfo', pdf], capture_output=True,
                          text=True).stdout
    n = int(re.search(r'Pages:\s+(\d+)', info).group(1))
    out = []

    for k in range(1, n + 1):
        box = subprocess.run(['pdftotext', '-f', str(k), '-l', str(k),
                              '-bbox', pdf, '-'], capture_output=True,
                             text=True).stdout
        out.append([(m.group(3), float(m.group(1)), float(m.group(2)))
                    for m in re.finditer(r'xMin="([\d.]+)" yMin="([\d.]+)"'
                                         r'[^>]*>([^<]*)<', box)])

    return out


def find(pgs, word):
    """The page (from 1) and the position of a word, or None."""
    for k, words in enumerate(pgs):
        for w, x, y in words:
            if w == word:
                return k + 1, x, y

    return None


# ---- Whitespace: the size and the treatment (faults 1 and 2)
def whitespace(size, treatment, before=40, after=30):
    recs = [b'1' + size]

    if treatment != 1:
        recs.append(b'2%d' % treatment)

    return document([lines(b'L', before), marker(31, recs) + EOL,
                     lines(b'A', after)])


def ws_pages(size, treatment, before=40, after=30):
    pdf = printed(whitespace(size, treatment, before, after))

    return pages(pdf) if pdf is not None else None


def all_words(pgs, before, after):
    got = {w for p in pgs for w, x, y in p}

    return all(('%s%02d' % (c, i)) in got for c, n in (('L', before),
                                                     ('A', after))
               for i in range(1, n + 1))


# A space of 3 inches after 40 lines does not fit on the rest of the page.
crop = ws_pages(b'3000', 0)
place = ws_pages(b'3000', 1)
flt = ws_pages(b'3000', 2)
# The height of a first line differs a little between machines (the version of
# pdftotext, the fonts), so a line at the top of a page is measured in the same
# PDF: the first line of page 2 when the space is cropped.
top1 = find(crop, 'L01')[2] if crop else 0
top = find(crop, 'A01')[2] if crop else 0

check('whitespace, crop: the page ends and the text follows on the next page '
      'with no space',
      crop is not None and len(crop) == 2 and find(crop, 'L40')[0] == 1 and
      find(crop, 'A01')[0] == 2 and abs(top - top1) < 5,
      crop and (find(crop, 'A01'), top1))
check('whitespace, place all: the whole space is at the top of the next page',
      place is not None and len(place) == 2 and
      find(place, 'L40')[0] == 1 and find(place, 'A01')[0] == 2 and
      216 <= find(place, 'A01')[2] - top <= 236,
      place and (find(place, 'A01'), top))
check('whitespace, float: the text moves up in front of the space, which is '
      'at the top of the next page',
      flt is not None and len(flt) == 2 and find(flt, 'A08')[0] == 1 and
      find(flt, 'A09')[0] == 2 and 216 <= find(flt, 'A09')[2] - top <= 236,
      flt and (find(flt, 'A08'), find(flt, 'A09'), top))

# A space that fits is left in full whatever the treatment says.
fits = [ws_pages(b'1000', t, 10, 10) for t in (0, 1, 2)]
check('whitespace: a space of 1 inch that fits is the same for the three '
      'treatments',
      all(f is not None and len(f) == 1 for f in fits) and
      len({round(find(f, 'A01')[2], 1) for f in fits}) == 1 and
      72 <= find(fits[0], 'A01')[2] - find(fits[0], 'L10')[2] <= 100,
      [f and find(f, 'A01') for f in fits])

# A space taller than the text area (12 inches, 9 inches of text on Letter
# paper with margins of 1 inch): nothing is lost.
for t, name in ((0, 'crop'), (1, 'place all'), (2, 'float')):
    tall = ws_pages(b'12000', t, 5, 5)
    check('whitespace of 12 inches, %s: all the text is printed' % name,
          tall is not None and all_words(tall, 5, 5),
          tall and [len(p) for p in tall])

tall = ws_pages(b'12000', 1, 5, 5)
check('whitespace of 12 inches, place all: a whole page of space, then the '
      'text on the page after it',
      tall is not None and len(tall) == 3 and find(tall, 'L05')[0] == 1 and
      find(tall, 'A01')[0] == 3, tall and find(tall, 'A01'))

# ---- Header Indent and Footer Indent (fault 3)
def hfblock(left, centre, right):
    """The block of a header or a footer: three columns."""
    def trec(t):
        return b'T' + t + EOL + b'\x00'

    return CRLF.join([b'S', b'CW26', b'CFJ0', trec(left), b'CW26', b'CFJ2',
                      trec(centre), b'CFJ1', trec(right)])


def hf_doc(records, n=3):
    """Three lines of text, a header HL HM HR and a footer FL FM FR."""
    head = (HEADER + b'PHB' + CRLF + hfblock(b'HL', b'HM', b'HR') + CRLF +
            b'PFB' + CRLF + hfblock(b'FL', b'FM', b'FR') + CRLF)
    head += b''.join(r + CRLF for r in records)

    return document([lines(b'L', n)], head)


def hf_x(records, word, page=1):
    pdf = printed(hf_doc(records))
    pgs = pages(pdf) if pdf is not None else None
    hit = None

    if pgs:
        for w, x, y in pgs[page - 1]:
            if w == word:
                hit = x

    return hit


base = {w: hf_x([], w) for w in ('HL', 'HM', 'HR', 'FL', 'FM', 'FR')}
head = {w: hf_x([b'PHI500'], w) for w in ('HL', 'HM', 'HR', 'FL', 'FM')}
foot = {w: hf_x([b'PFI1000'], w) for w in ('HL', 'FL', 'FM', 'FR')}
both = {w: hf_x([b'PHI250', b'PFI750'], w) for w in ('HL', 'FL')}
check('no indent: the header starts at the left margin of the text',
      base['HL'] is not None and abs(base['HL'] - 72) < 1 and
      abs(base['FL'] - 72) < 1, base)
check('header indent 0.5 inch: the left part of the header moves in by 36 '
      'points, the right part and the footer stay',
      all(v is not None for v in head.values()) and
      abs(head['HL'] - base['HL'] - 36) < 1 and
      abs(head['HR'] - base['HR']) < 1 and abs(head['FL'] - base['FL']) < 1,
      (base, head))
check('header indent 0.5 inch: the middle part is centred in what is left',
      head['HM'] is not None and abs((head['HM'] - base['HM']) - 18) < 2,
      (base['HM'], head['HM']))
check('footer indent 1 inch: the left part of the footer moves in by 72 '
      'points, the header stays',
      all(v is not None for v in foot.values()) and
      abs(foot['FL'] - base['FL'] - 72) < 1 and
      abs(foot['FR'] - base['FR']) < 1 and abs(foot['HL'] - base['HL']) < 1,
      (base, foot))
check('header indent 0.25 inch and footer indent 0.75 inch together',
      both['HL'] is not None and abs(both['HL'] - base['HL'] - 18) < 1 and
      abs(both['FL'] - base['FL'] - 54) < 1, (base, both))

# The layout of the odd and of the even pages has its own indent.
odd = {w: hf_x([b'P5HI500', b'P4HI1000'], w, 1) for w in ('HL',)}
check('the indent of a layout is its own: the first page is page 1, an odd '
      'page',
      odd['HL'] is not None and abs(odd['HL'] - base['HL'] - 36) < 1, odd)

# Header Whitespace and Footer Whitespace: the distance of the header and of
# the footer from the text (the same records are read through one function,
# so the test reads the vertical positions).
def hf_y(records, word):
    pdf = printed(hf_doc(records))
    pgs = pages(pdf) if pdf is not None else None

    for w, x, y in (pgs[0] if pgs else []):
        if w == word:
            return y

    return None


h_def = hf_y([], 'HL')
h_300 = hf_y([b'PHW300'], 'HL')
h_500 = hf_y([b'PHW500'], 'HL')
f_def = hf_y([], 'FL')
f_300 = hf_y([b'PFW300'], 'FL')
f_700 = hf_y([b'PFW700'], 'FL')
check('header whitespace: the default is 0.5 inch, 0.3 inch puts the header '
      '14.4 points lower, the same as a record of 0.5 inch',
      None not in (h_def, h_300, h_500) and abs(h_def - h_500) < 0.5 and
      abs(h_300 - h_def - 14.4) < 0.7, (h_def, h_300, h_500))
check('footer whitespace: 0.3 inch puts the footer 14.4 points higher than '
      'the default, 0.7 inch 14.4 points lower',
      None not in (f_def, f_300, f_700) and
      abs(f_def - f_300 - 14.4) < 0.7 and abs(f_700 - f_def - 14.4) < 0.7,
      (f_def, f_300, f_700))

# ---- Cross-reference Format and the Style [Tag] (fault 4)
def trec(text):
    return b'T' + text + EOL + b'\x00' + CRLF


def xref_doc(style, oc):
    """A structured document of two sections; the text of the second one has
    a cross-reference to the first, between REFA and REFZ."""
    recs = [b'XN1\x00'] + ([b'XT%d' % style] if style else [])
    head = HEADER + (b'OC%d' % oc + CRLF if oc else b'')
    body = (b'S' + CRLF + trec(b'The Title') + b'+' + CRLF +
            b'SE0' + CRLF + trec(b'Alpha Heading') +
            b'B' + CRLF + trec(b'Text of the first section.') +
            b'SE0' + CRLF + trec(b'Beta Heading') +
            b'B' + CRLF + b'T' + b'REFA ' + marker(2, recs) + b' REFZ' + EOL +
            b'\x00' + CRLF + b'-' + CRLF)

    return head + b'*B1' + CRLF + body + b'*E' + CRLF


def xref_text(style, oc):
    pdf = printed(xref_doc(style, oc))

    if pdf is None:
        return None

    text = subprocess.run(['pdftotext', '-layout', pdf, '-'],
                          capture_output=True, text=True).stdout
    m = re.search(r'REFA\s+(.*?)\s+REFZ', ' '.join(text.split()))

    return m.group(1) if m else None


got = {(s, o): xref_text(s, o) for s in (0, 1, 2, 3) for o in (0, 2, 3)}
check('cross-reference: the style Tag is the tag whatever the format says',
      all(got[(1, o)] == '1' for o in (0, 2, 3)), got)
check('cross-reference: the style Headline is the headline whatever the '
      'format says',
      all(got[(2, o)] == 'Alpha Heading' for o in (0, 2, 3)), got)
check('cross-reference: the style Both is the tag and the headline',
      all(got[(3, o)] == '1 Alpha Heading' for o in (0, 2, 3)), got)
check('cross-reference: the style [Tag] follows the format: tag, headline, '
      'both',
      (got[(0, 0)], got[(0, 2)], got[(0, 3)]) ==
      ('1', 'Alpha Heading', '1 Alpha Heading'), got)
check('cross-reference: the style Page Number is the page of the section '
      'whatever the format says',
      all(xref_text(4, o) == '1' for o in (0, 2)), xref_text(4, 2))

# ---- The keycap with a Scale (fault 7): the print ends without an error
# at the least and the greatest scale and at some in between, and the legend
# is in the PDF.  The Keycap Format of the document gives the scale too.
def keycap_doc(scale, header=HEADER):
    caps = b''.join(marker(37, [b'1Legend%d\x00' % i] +
                           ([b'4%d' % scale] if scale is not None else []))
                    + b' ' for i in range(3))

    return document([b'Press ' + caps + b'to save.' + EOL], header)


def keycap_run(source):
    work = tempfile.mkdtemp(prefix='kc', dir=tmp)
    src = os.path.join(work, 'doc.doc')

    with open(src, 'wb') as f:
        f.write(source)

    pdf = os.path.join(work, 'out.pdf')
    p = subprocess.run([strprint, src, '-o', pdf], capture_output=True,
                       text=True, cwd=work, timeout=600)
    text = subprocess.run(['pdftotext', pdf, '-'], capture_output=True,
                          text=True).stdout if os.path.exists(pdf) else ''

    return p, text


bad = []

for sc in (1, 100, 1000, 1500, 2500, 7777, 10000, None):
    p, text = keycap_run(keycap_doc(sc))

    if (p.returncode != 0 or p.stderr.strip() or
            not all('Legend%d' % i in text for i in range(3))):
        bad.append((sc, p.returncode, p.stderr[:200]))

check('keycap scale 0.001, 0.1, 1, 1.5, 2.5, 7.777, 10 and none: the print '
      'ends with no error and the legends are in the PDF', not bad, bad)

for oks in (b'100', b'2500', b'10000'):
    p, text = keycap_run(keycap_doc(None, HEADER + b'OKS' + oks + CRLF))
    check('keycap scale from the Keycap Format (OKS%s): the print ends with '
          'no error' % oks.decode(),
          p.returncode == 0 and not p.stderr.strip() and 'Legend2' in text,
          (p.returncode, p.stderr[:200]))

# The values that a keycap leaves out are those of the Keycap Format of the
# document (OKF, OKB, OKS, OKT), in the print as on the screen.
def keys_of(source):
    """The arguments of every \\strkey in the LaTeX text of a document."""
    work = tempfile.mkdtemp(prefix='kx', dir=tmp)
    src = os.path.join(work, 'doc.doc')
    out = os.path.join(work, 'doc.tex')

    with open(src, 'wb') as f:
        f.write(source)

    subprocess.run([strprint, src, '--export-latex', out],
                   capture_output=True, text=True, cwd=work, timeout=600)
    text = open(out).read() if os.path.exists(out) else ''

    return re.findall(r'\\strkey\{([\d.]+)\}\{([\d.]+)\}\{([\d.]+)\}', text)


fmt = (HEADER + b'OKF200' + CRLF + b'OKB800' + CRLF + b'OKS2500' + CRLF)
own = marker(37, [b'1Own\x00', b'2300', b'4500'])
plain = marker(37, [b'1Plain\x00'])
keys = keys_of(document([b'a ' + plain + b' b ' + own + EOL], fmt))
check('keycap: a value that the marker leaves out is the one of the Keycap '
      'Format; a value of its own stays',
      keys == [('0.200', '0.800', '2.500'), ('0.300', '0.800', '0.500')],
      keys)
keys = keys_of(document([b'a ' + plain + EOL]))
check('keycap: a document with no Keycap Format has black on white and '
      'scale 1',
      keys == [('0.000', '1.000', '1.000')], keys)

# ---- The Draft print of a Comment (fault 9): the sample document
sample = os.path.join(os.path.dirname(here), 'samples', 'comments',
                      'sample-comments.doc')
COMMENTS = [('Ada Example', '2026-05-02 09:15', 'Check that the meadow is '
             'free that day.'),
            ('Ben Example', '2026-05-02 10:40', 'Four helpers is too few at '
             'noon; ask two more.'),
            ('Ada Example', '2026-05-03 08:05', 'Ask the baker for the list '
             'of cakes.'),
            ('Ben Example', '2026-05-03 08:30', 'Tea and cake: one price, as '
             'last year.')]


def sample_text(args):
    with open(sample, 'rb') as f:
        pdf = printed(f.read(), args)

    if pdf is None:
        return None, 0

    n = int(re.search(r'Pages:\s+(\d+)', subprocess.run(
        ['pdfinfo', pdf], capture_output=True, text=True).stdout).group(1))
    text = subprocess.run(['pdftotext', '-layout', pdf, '-'],
                          capture_output=True, text=True).stdout

    return ' '.join(text.split()), n


final, final_pages = sample_text([])
draft, draft_pages = sample_text(['--draft'])
check('comments, final: no comment is printed',
      final is not None and 'Comment by' not in final and
      'meadow is free' not in final and 'baker' not in final, final)
check('comments, draft: each comment is printed in brackets with its '
      'author, its time and its text',
      draft is not None and all(
          '[Comment by %s, %s: %s]' % c in draft for c in COMMENTS) and
      draft.count("[Comment by") == len(COMMENTS) and
      '[[' not in draft and ']]' not in draft, draft)
if final is not None and draft is not None:
    rest = re.sub(r'\[Comment by [^\]]*\]', '', draft)
    check('comments: the draft is the final and the comments, nothing else '
          'of the text is different',
          re.sub(r'\W+', '', rest) == re.sub(r'\W+', '', final),
          (rest, final))
check('comments: the two prints have the same number of pages',
      final_pages == draft_pages == 1, (final_pages, draft_pages))

# The committed sample is what its program makes.
regen = os.path.join(tmp, 'regen')
gen = subprocess.run([sys.executable, '-I', '-B',
                      os.path.join(here, 'mkcommentsample.py'), regen],
                     capture_output=True, text=True)
made = os.path.join(regen, 'sample-comments.doc')
check('comments: samples/comments/sample-comments.doc is what '
      'tests/mkcommentsample.py makes',
      gen.returncode == 0 and os.path.exists(made) and
      open(made, 'rb').read() == open(sample, 'rb').read(), gen.stderr)

print('%d checks, %d failed' % (checks, fails))
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
