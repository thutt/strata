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

"""The sample document of the standard fonts and sizes (samples/fonts,
tests/mkfonts.py, docs/FONTS-SAMPLE.txt).

(a) the committed document is what the generator makes from the committed
specimen text, and the generator makes the same bytes twice.
(b) the structure: a structured document with the Section Tag "Font \\1\\: "
for the first level (Auto Numbering is Yes), the page break level 0 (no FLP
record), no title page, one section of the first level for each of the 20
numbers of the list of Strata in order, the headline of each being the name
that the program gives to the number, a Keep region around each font, five
blocks of two columns below the headline of each font that has a
configuration, and the line "nothing configured." for the number 20.
(c) the print: the headline of every font is printed as Font N: NAME with N
the number of the font (the counter of the document), no headline stands alone
at the foot of a page, no footnote and no credit anywhere, the last word of
the specimen text is printed once for each size of each configured font, the
fonts of the PDF are the faces of the list, each used from its headline on
(Lato when it is installed), and in every section the size of the printed
text grows from the first block to the last.
(d) the specimen text: a file with three lines is accepted and printed (the
committed text is not changed), and a text with eleven lines, with a
line of 81 columns, with a character that is not ASCII, with an empty line
and an empty file is refused with a message.

    STRPRINT=... python3 -I -B tests/fontsample.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import mkfonts as M

build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo', 'pdffonts'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix='fontsample')
fails = 0
CRLF = b'\r\n'


def check(what, ok, detail=''):
    global fails
    if ok:
        print('ok   %s' % what)
    else:
        fails += 1
        print('FAIL %s\n%s' % (what, str(detail)[:600]))


def run_generator(args):
    """The status and the message of the generator."""
    p = subprocess.run([sys.executable, '-I', '-B',
                        os.path.join(here, 'mkfonts.py')] + args,
                       capture_output=True, text=True)
    return p.returncode, p.stderr


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def print_pdf(doc, name):
    """The path of the PDF that strprint makes of a document, or None."""
    work = os.path.join(tmp, name)
    os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'doc.doc')
    shutil.copyfile(doc, src)
    pdf = os.path.join(work, 'out.pdf')
    p = subprocess.run([strprint, src, '-o', pdf], capture_output=True,
                       text=True, cwd=work, timeout=900)
    return pdf if p.returncode == 0 and os.path.exists(pdf) else None


def page_count(pdf):
    info = subprocess.run(['pdfinfo', pdf], capture_output=True,
                          text=True).stdout
    return int(re.search(r'Pages:\s+(\d+)', info).group(1))


def page_text(pdf, k):
    return subprocess.run(['pdftotext', '-f', str(k), '-l', str(k), '-layout',
                           pdf, '-'], capture_output=True, text=True).stdout


def page_words(pdf, k):
    """The words of a page: (text, x, height)."""
    box = subprocess.run(['pdftotext', '-f', str(k), '-l', str(k), '-bbox',
                          pdf, '-'], capture_output=True, text=True).stdout
    out = []
    for m in re.finditer(r'xMin="([\d.]+)" yMin="([\d.]+)" xMax="[\d.]+" '
                         r'yMax="([\d.]+)">([^<]*)<', box):
        out.append((m.group(4), float(m.group(1)),
                    float(m.group(3)) - float(m.group(2))))
    return out


def page_words_y(pdf, k):
    """The words of a page: (text, x, height, y), y the top of the word."""
    box = subprocess.run(['pdftotext', '-f', str(k), '-l', str(k), '-bbox',
                          pdf, '-'], capture_output=True, text=True).stdout
    out = []
    for m in re.finditer(r'xMin="([\d.]+)" yMin="([\d.]+)" xMax="[\d.]+" '
                         r'yMax="([\d.]+)">([^<]*)<', box):
        out.append((m.group(4), float(m.group(1)),
                    float(m.group(3)) - float(m.group(2)),
                    float(m.group(2))))
    return out


def fonts_of(pdf, first, last):
    out = subprocess.run(['pdffonts', '-f', str(first), '-l', str(last),
                          pdf], capture_output=True, text=True).stdout
    return out


# ------------------------------------------------------------ (a) the file
committed = os.path.join(M.FOLDER, M.NAME)
gen1 = os.path.join(tmp, 'gen1')
gen2 = os.path.join(tmp, 'gen2')
rc1, err1 = run_generator([gen1])
rc2, err2 = run_generator([gen2])
check('the generator runs without a message', rc1 == 0 and rc2 == 0 and
      err1 == '' and err2 == '', err1 + err2)
made = read(os.path.join(gen1, M.NAME))
check('two runs of the generator make the same bytes',
      made == read(os.path.join(gen2, M.NAME)))
check('the committed document is what the generator makes',
      os.path.isfile(committed) and read(committed) == made)
lines = M.read_specimen(M.SPECIMEN)
check('the committed specimen text is the one line of the pangram, with '
      'the period at its end',
      lines == ['The quick brown fox jumps over the lazy dog.'], lines)
check('the committed document has no Note marker (no footnote, no credit)',
      b'\x131(' not in read(committed) and b'urgh' not in read(committed),
      None)

# ------------------------------------------------------- (b) the structure
data = read(committed)
head, _, body = data.partition(CRLF + b'*B1' + CRLF)
check('the document is a structured document (*B1)', body != b'')
check('the sections are numbered: the header has no FLA record (Auto '
      'Numbering is Yes, the default)',
      (CRLF + b'FLA') not in (CRLF + head + CRLF), None)
check('the Section Tag of the first level is "Font \\1\\: ", so that the '
      'first level counts the fonts and prints Font 4: Times',
      (CRLF + b'F1GFont \\1\\: \x00' + CRLF) in (CRLF + head + CRLF), None)
check('the page break level is 0: the header has no FLP record, so no font '
      'is forced to a new page',
      (CRLF + b'FLP') not in (CRLF + head + CRLF))

# The sections of the first level: the S records after the first +.
recs = body.split(CRLF)
depth = 0
sections = []
cur = None
for i, r in enumerate(recs):
    if r == b'+':
        depth += 1
    elif r == b'-':
        depth -= 1
    elif r == b'S' and depth == 1:
        cur = {'head': None, 'blocks': 0, 'cols': [], 'text': []}
        sections.append(cur)
    elif cur is not None and depth == 1:
        if r.startswith(b'T') and cur['head'] is None:
            cur['head'] = r[1:].rstrip(b'\x1d\x00').decode('ascii')
        elif r.startswith(b'T') and b'\x13' not in r:
            cur['text'].append(r[1:])
        elif r == b'B':
            cur['blocks'] += 1
        elif r.startswith(b'CW'):
            cur['cols'].append(int(r[2:]))
NUMBERS = [num for n, f, num in M.FONTS]
want_heads = [n for n, f, num in M.FONTS]
check('the document has 20 sections of the first level, one for each number '
      'of the list of Strata from 1 to 20, in that order, and the headline '
      'of each is the name of the font (the counter of the first level '
      'puts the number in front of it)',
      NUMBERS == list(range(1, 21)) and
      [s['head'] for s in sections] == want_heads,
      [s['head'] for s in sections])

# The names of the program: the table of the fonts in src/fonts.c has one
# line for each font, the first line is the font 1.
fonts_c = open(os.path.join(here, '..', 'src', 'fonts.c')).read()
table = re.findall(r'^    \{"([^"]*)", "[^"]*", "[^"]*", FF_', fonts_c, re.M)
roles = {1: 'Headline', 2: 'Text', 3: 'Header/Footer'}
bad = []
for n, f, num in M.FONTS:
    if num in roles:
        ok = n == '%s (%s)' % (roles[num], table[num - 1])
    elif table[num - 1] == '':
        ok = n == '(not set)' and f is None
    else:
        ok = n == table[num - 1] and f is not None
    if not ok:
        bad.append((num, n, table[num - 1]))
check('the name of every font is the one that the program gives to its '
      'number (src/fonts.c: the role and the name for 1 to 3, the name '
      'for 4 to 19, and (not set) for the number 20 that has none)',
      len(table) == 20 and bad == [], bad)
# The list of the manual and of docs/FONTS.txt has the same numbers.
listed = {}
for ln in open(os.path.join(here, '..', 'docs', 'FONTS.txt')):
    m = re.match(r'^\s+(\d+) (\S.*?)\s{2,}\S', ln)
    if m:
        listed[int(m.group(1))] = m.group(2).strip()
check('the number of every font is the number of its line in the list of '
      'docs/FONTS.txt (the fonts 4 to 19 by name, the roles 1 to 3 by '
      'role)',
      all(listed.get(num) == (roles.get(num) or n)
          for n, f, num in M.FONTS if num <= 19),
      [(num, listed.get(num)) for n, f, num in M.FONTS
       if num <= 19 and listed.get(num) != (roles.get(num) or n)])
configured = [s for s, (n, f, num) in zip(sections, M.FONTS) if f]
check('every font that has a configuration (19 of them) has five blocks of '
      'two columns, one for each size, the left column of 10 cells',
      len(configured) == 19 and
      all(s['cols'] == [10] * 5 for s in configured),
      [s['cols'][:2] for s in configured[:2]])
last = sections[-1]
check('the font 20, which has nothing configured, has the line "nothing '
      'configured." and no block of two columns',
      last['cols'] == [] and last['text'] == [b'nothing configured.\x1d\x00'],
      (last['cols'], last['text']))
KS = b'\x134(\x13)'
KE = b'\x134(12' + CRLF + b'\x13)'
check('a Keep region holds every font: 20 Keep markers of the type Start '
      '(Fixed) and 20 of the type End, and the Start marker of every font '
      'but the first stands before its headline, so that the headline is '
      'never left alone at the foot of a page',
      body.count(KS) == 20 and body.count(KE) == 20 and
      len(re.findall(re.escape(b'B' + CRLF + b'T' + KS + b'\x1d\x00' + CRLF
                               + b'S' + CRLF), body)) == 19,
      (body.count(KS), body.count(KE)))
check('the sizes are 8, 10, 12, 14 and 20 points, and no other',
      [p for p, l in M.SIZES] == [8, 10, 12, 14, 20])
check('there is no title page: the first record of the body starts the first '
      'font section, and nothing stands before it',
      body.startswith(b'+' + CRLF + b'S' + CRLF +
                      b'THeadline (Latin Modern Sans)'), body[:60])

# ---------------------------------------------------------- (c) the print
pdf = print_pdf(committed, 'print')
check('the sample prints', pdf is not None)
if pdf is not None:
    n = page_count(pdf)
    texts = [page_text(pdf, k) for k in range(1, n + 1)]
    # The headlines in the print: the page, the top, the number that the
    # counter made and the name, found by the words Font and N: at the left
    # edge of the page.
    heads = []
    for k in range(1, n + 1):
        ws = page_words_y(pdf, k)
        for i in range(len(ws) - 1):
            if ws[i][0] == 'Font' and re.match(r'^\d+:$', ws[i + 1][0]) \
                    and ws[i][1] < 120 and ws[i][3] > 40:
                name = ' '.join(w[0] for w in sorted(
                    (w for w in ws if abs(w[3] - ws[i][3]) < 2 and
                     w[1] > ws[i + 1][1]), key=lambda w: w[1]))
                heads.append((k, ws[i][3], int(ws[i + 1][0][:-1]), name))
    check('the print has the headline Font N: NAME for every number from 1 '
          'to 20 in order: the counter of the document is the number of the '
          'font',
          [(h[2], h[3]) for h in heads] ==
          [(num, nm) for nm, f, num in M.FONTS], heads)
    alllines = [ln.strip() for t in texts for ln in t.split('\n')]
    check('every headline reads "Font N: NAME" in the text of the PDF, with '
          'one blank after the colon',
          all(any(re.fullmatch(r'Font %d: %s' % (num, re.escape(nm)), ln)
                  for ln in alllines) for nm, f, num in M.FONTS),
          [(num, nm) for nm, f, num in M.FONTS
           if not any(re.fullmatch(r'Font %d: %s' % (num, re.escape(nm)), ln)
                      for ln in alllines)][:3])
    per_page = {}
    for k, y, num, name in heads:
        per_page[k] = per_page.get(k, 0) + 1
    check('no font is forced to a new page: a page holds the headlines of '
          'two fonts or more, and the print has fewer pages than fonts have',
          any(c >= 2 for c in per_page.values()) and n < len(M.FONTS),
          (per_page, n))
    stranded = []
    for k, t in enumerate(texts, 1):
        body_lines = [ln for ln in t.split('\n') if ln.strip() != '' and
                      not re.match(r'^\s*Page \d+\s*$', ln)]
        if body_lines != [] and re.match(r'^Font \d+: ', body_lines[-1]):
            stranded.append((k, body_lines[-1]))
    check('no headline stands alone at the foot of a page (a page does not '
          'end with a headline)', stranded == [], stranded)
    alltext = ' '.join(texts)
    check('the print has no credit, no copyright and no footnote',
          'urgh' not in alltext and '1986' not in alltext and
          'property of' not in alltext and 'Lady' not in alltext,
          alltext[:200])
    check('the first page starts with the first font: its headline is the '
          'first text of the page body',
          heads != [] and heads[0][0] == 1 and heads[0][2] == 1,
          heads[:1])
    check('the font 20 has the text "nothing configured." once in the print, '
          'after its headline, and no size line follows it',
          len(re.findall(r'nothing configured\.', alltext)) == 1 and
          re.search(r'Font 20: \(not set\)\s*\n\s*nothing configured\.\s*\n'
                    r'(\s*\n)*\s*Page \d+\s*$', texts[-1]) is not None,
          texts[-1][-200:])

    NF = len([1 for nm, f, num in M.FONTS if f])
    check('the period is in the PDF text of every block: "dog." stands %d '
          'times' % (NF * 5), len(re.findall(r'\bdog\.', ' '.join(texts)))
          == NF * 5, len(re.findall(r'\bdog\.', ' '.join(texts))))
    last = lines[-1].split()[-1]
    count = sum(len(re.findall(re.escape(last), t)) for t in texts)
    check('the last word of the specimen text is printed once for each size of '
          'each font that has a configuration (%d times)' % (NF * len(M.SIZES)),
          count == NF * len(M.SIZES), count)

    # The fonts: the files that the PDF embeds.  A font is used from its
    # headline to the headline of the next font (the pages that they share
    # have both faces).
    expect = {
        'Times': 'NimbusRomNo9L', 'Helvetica': 'NimbusSanL',
        'Courier': 'NimbusMonL', 'Palatino': 'URWPalladioL',
        'Bookman': 'URWBookmanL', 'New Century Schoolbook': 'CenturySchL',
        'Avant Garde': 'URWGothicL', 'Zapf Chancery': 'URWChanceryL',
        'Charter': 'CharterBT', 'Utopia': 'Utopia',
        'TeX Gyre Termes': 'TeXGyreTermes', 'TeX Gyre Heros': 'TeXGyreHeros',
        'TeX Gyre Pagella': 'TeXGyrePagella',
        'TeX Gyre Cursor': 'TeXGyreCursor',
    }
    spans = {}
    for i, (k, y, num, name) in enumerate(heads):
        end = heads[i + 1][0] if i + 1 < len(heads) else n
        spans[name] = (k, end)
    bad = []
    for name, face in expect.items():
        if name not in spans:
            bad.append((name, 'no section'))
            continue
        first, end = spans[name]

        if face not in fonts_of(pdf, first, end):
            bad.append((name, face, 'not on its pages'))
    check('each of the %d standard fonts of the list is the embedded face on '
          'the pages from its headline to the next one' % len(expect),
          bad == [], bad[:4])
    check('Headline (Latin Modern Sans), the font 1, is set in a Latin '
          'Modern Sans face',
          'LMSans' in fonts_of(pdf, *spans['Headline (Latin Modern Sans)']))
    check('Computer Modern Roman is set in a Latin Modern face',
          'Computer Modern Roman' in spans and
          'LMRoman' in fonts_of(pdf, *spans['Computer Modern Roman']))
    # Lato is not part of a basic TeX Live: when it is not installed the
    # print does not fail and sets the sizes in the font of the text; when it
    # is installed (Install Now of the Document Manager) the face is Lato.
    have_lato = subprocess.run(['kpsewhich', 'lato.sty'],
                               capture_output=True, text=True).stdout.strip()
    if have_lato != '':
        check('Lato is installed: the font 19 is set in the Lato face',
              'Lato' in fonts_of(pdf, *spans['Lato']),
              fonts_of(pdf, *spans['Lato'])[:300])
    else:
        check('Lato is not installed: the print still has the five sizes of '
              'the font 19 (set in the font of the text)',
              len(re.findall(r'\bdog\.', page_text(pdf, spans['Lato'][0])))
              >= 1)

    # The sizes: the first word of the first line of the specimen text is the
    # same word in every block, so its height is the size of the block.  A
    # block belongs to the last headline that stands before it on the pages.
    first_word = lines[0].split()[0]
    found = []
    for k in range(1, n + 1):
        for w, x, h, y in page_words_y(pdf, k):
            if w == first_word and x > 100:
                found.append((k, y, round(h, 2)))
    found.sort()
    by_font = {}
    for k, y, h in found:
        owner = None
        for hk, hy, num, name in heads:
            if (hk, hy) <= (k, y):
                owner = name
        by_font.setdefault(owner, []).append(h)
    sizes_ok = list(by_font.items())
    check('the size of the printed text grows from block to block in every '
          'section that has blocks (%d sizes in %d sections)'
          % (len(M.SIZES), NF),
          len(by_font) == NF and
          all(len(hs) == len(M.SIZES) and
              all(hs[i] < hs[i + 1] for i in range(len(hs) - 1))
              for name, hs in sizes_ok),
          [x for x in sizes_ok if len(x[1]) != len(M.SIZES) or
           any(x[1][i] >= x[1][i + 1] for i in range(len(x[1]) - 1))][:2])
    alltext_size = ' '.join(texts)
    check('the size column names every size, 8 point to 20 point, once for '
          'each font that has blocks', all(
              len(re.findall(r'\b%d point\b' % p, alltext_size)) == NF
              for p, l in M.SIZES),
          [(p, len(re.findall(r'\b%d point\b' % p, alltext_size)))
           for p, l in M.SIZES])

# --------------------------------------------------- (d) the specimen text
NF = len([1 for nm, f, num in M.FONTS if f])
three = os.path.join(tmp, 'three.txt')
with open(three, 'w') as f:
    f.write('Line one of a test text,\nLine two of it,\nLine three.\n')
out3 = os.path.join(tmp, 'three')
rc, err = run_generator(['--specimen', three, out3])
d3 = read(os.path.join(out3, M.NAME)) if rc == 0 else b''
check('a text of three lines is accepted and every size shows it',
      rc == 0 and d3.count(b'Line three.') == NF * len(M.SIZES) and
      d3.count(b'The quick brown fox') == 0, err)
check('the committed document is not changed by the test text',
      read(committed) == made)
pdf3 = print_pdf(os.path.join(out3, M.NAME), 'three-print') if d3 else None
if pdf3 is not None:
    t3 = ' '.join(page_text(pdf3, k) for k in range(1, page_count(pdf3) + 1))
    check('the document with the three lines prints, with those lines',
          t3.count('Line three.') == NF * len(M.SIZES), t3[:200])
else:
    check('the document with the three lines prints', False)


def refuse(name, content, mode='w'):
    path = os.path.join(tmp, name)
    with open(path, mode) as f:
        f.write(content)
    rc, err = run_generator(['--specimen', path,
                             os.path.join(tmp, 'x-' + name)])
    return rc, err


rc, err = refuse('eleven.txt', ''.join('Line %d\n' % i for i in range(11)))
check('a text of eleven lines is refused with a message',
      rc == 1 and 'at most 10' in err, (rc, err))
rc, err = refuse('wide.txt', 'x' * 81 + '\n')
check('a line of 81 columns is refused with a message',
      rc == 1 and 'at most 80' in err, (rc, err))
rc, err = refuse('latin.txt', b'Caf\xc3\xa9\n', 'wb')
check('a text that is not ASCII is refused with a message',
      rc == 1 and 'not ASCII' in err, (rc, err))
rc, err = refuse('blank.txt', 'One\n\nTwo\n')
check('an empty line of the text is refused with a message',
      rc == 1 and 'empty' in err, (rc, err))
rc, err = refuse('none.txt', '')
check('an empty text file is refused with a message',
      rc == 1 and 'no line' in err, (rc, err))
rc, err = run_generator(['--specimen', os.path.join(tmp, 'missing.txt'),
                         os.path.join(tmp, 'x-missing')])
check('a text file that is not there is refused with a message',
      rc == 1 and 'cannot read' in err, (rc, err))

print('fontsample: %d failures' % fails)
sys.exit(1 if fails else 0)
