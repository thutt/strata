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

"""The sample document "How Strata was made" (samples/story/, made by
tests/mkstory.py from the sources of the same folder, with the charts of
tests/mkstorypics.py).

(a) the file: it is what the generator makes (two runs make the same bytes,
and the committed files are those bytes), it is structured, four levels
deep, with the named formats, a header, a footer and a first page that has
neither; the pictures it names exist.
(b) the Editor opens it in the structured mode; the list of New does not show
it (it is not a template).
(c) the Printer prints it to a PDF with no error and no unresolved reference
("??"); the contents has a row for every chapter with the page on which the
headline is printed; every figure has a caption and a row in the list of
figures; the index and the list of tables are there.
(d) the numbers of the charts and of the tables are those of
tests/storydata.py, and these equal those of the documents they are copied
from (docs/MEASUREMENTS.txt, docs/MEMORY.txt, docs/BIG-DOCUMENTS.txt,
docs/TESTING.txt), and the days of the chart of the commits equal the log; the
tables follow the rule of the borders (manual/checktables.py), and the header
rows are bold.
(e) no name of the original product, of a machine or of a person is in the
document, its sources or its PDF.
(f) the rules of the project maintainer for the text: no repository path or
file name, no word owner, no rule about naming the old program, TeX and
LaTeX typeset with the logos, cross reference where it is meant, none of the
removed sections, the real number of the section that tells where you are,
and the words to a page are a data item.

    STRPRINT=... STRED=... python3 -I -B tests/story.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, importlib.util, os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, '..')
sys.path.insert(0, here)
import tui
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint) or not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='story')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
os.environ.pop('STRATA_TEMPLATES', None)
CRLF = b'\r\n'
EOL = b'\x1d'
SAMPLES = os.path.join(root, 'samples', 'story')
DOC = os.path.join(SAMPLES, 'strata-story.doc')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
        timeout=600, **kw)
    return r.returncode, r.stdout, r.stderr


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def read_text(path):
    with open(path, 'r', encoding='ascii', errors='replace') as f:
        return f.read()


mkstory = load('mkstory', os.path.join(here, 'mkstory.py'))
sd = load('storydata', os.path.join(here, 'storydata.py'))
pics = load('mkstorypics', os.path.join(here, 'mkstorypics.py'))

# ------------------------------------------------------- (a) the file
check('the document and its pictures are in samples/story',
      os.path.isfile(DOC) and os.path.isdir(os.path.join(SAMPLES,
                                                         'pictures')))
data = read(DOC)
gen1 = os.path.join(tmp, 'gen1')
gen2 = os.path.join(tmp, 'gen2')
st1 = subprocess.run([sys.executable, '-I', '-B', os.path.join(here,
                      'mkstory.py'), gen1], capture_output=True, text=True)
st2 = subprocess.run([sys.executable, '-I', '-B', os.path.join(here,
                      'mkstory.py'), gen2], capture_output=True, text=True)
check('the generator runs without a message', st1.returncode == 0 and
      st2.returncode == 0 and st1.stderr == '' and st2.stderr == '',
      st1.stderr + st2.stderr)
names = ['strata-story.doc'] + ['pictures/' + n for n in sorted(
    os.listdir(os.path.join(gen1, 'pictures')))]
check('two runs of the generator make the same bytes (the document and %d '
      'pictures)' % (len(names) - 1), all(
          os.path.isfile(os.path.join(gen2, n)) and
          read(os.path.join(gen1, n)) == read(os.path.join(gen2, n))
          for n in names), names)
check('the committed document and pictures are what the generator makes',
      all(os.path.isfile(os.path.join(SAMPLES, n)) and
          read(os.path.join(SAMPLES, n)) == read(os.path.join(gen1, n))
          for n in names) and sorted(os.listdir(os.path.join(
              SAMPLES, 'pictures'))) == sorted(n[9:] for n in names[1:]),
      names)
check('the file is a document of file format 3 with a description and an author',
      data.startswith(b'*R3' + CRLF) and b'\r\nMDSample document: ' in data
      and b'\r\nMA' + mkstory.AUTHOR.encode() + b'\x00' in data)
check('the body is structured (*B1)', b'\r\n*B1\r\n' in data)
lines = data.split(CRLF)
depth = 0
deepest = 0
top = 0
for ln in lines:
    if ln == b'+':
        depth += 1
        deepest = max(deepest, depth)
    elif ln == b'-':
        depth -= 1
    elif ln == b'S' and depth == 1:
        top += 1
check('the sections go four levels deep and the tree is balanced',
      deepest >= 4 and depth == 0, (deepest, depth))
check('the first level has the nine chapters and nothing else: the title is '
      'the text before the first section, and the contents, the lists and '
      'the index are made by the Printer (%d sections)' % top, top == 9, top)
check('the document has the markers that make the lists: Figure, Table and '
      'Index, and the contents level 2, and no hand made list (no section '
      'of the first level is called Contents, Index or a list)',
      b'\x1311(' in data and b'\x1329(' in data and b'\x133(' in data and
      b'\r\nYCL2\r\n' in data and
      not re.search(rb'\r\nT(Contents|Index|List of [A-Za-z]+)\x1d',
                    data), None)
for fmt in ('Body', 'Heading 1', 'Heading 2', 'Heading 3', 'Heading 4',
            'Listing', 'Quote'):
    check('the named format %s is defined and used' % fmt,
          (b'\r\nFN' + fmt.encode() + b'\x00\r\n') in data and
          (b'\r\nBG' + fmt.encode() + b'\x00\r\n') in data)
# The large logo of the title page is a LaTeX marker that holds only the logo,
# between two Whitespace markers (type 31) that make the space around it: the
# Editor draws the marker as a picture, and vertical space belongs in a
# Whitespace marker.
check('the logo of the title page is a LaTeX marker with only \\stratalogo[3] '
      'between two Whitespace markers',
      re.search(rb'\x1331\([^\x13]*\x13\)[^\x13]*\x1341\(S\r\nT\\stratalogo'
                rb'\[3\]\x1d\x00\r\n\x13\)[^\x13]*\x1331\(', data) is not None,
      None)
check('no LaTeX marker of the story holds vertical space',
      re.search(rb'\x1341\([^\x13]*\\(?:vspace|vskip|vfill|bigskip|medskip|'
                rb'smallskip)', data) is None, None)
check('the line numbering of the print is on and the numbers start again on '
      'every page (FZM1)', b'\r\nFZM1\r\n' in data)
check('a header and a footer are defined, and the title page has none',
      b'\r\nPHB\r\n' in data and b'\r\nPFB\r\n' in data and
      b'\r\nP1HB0\r\n' in data and b'\r\nP1FB0\r\n' in data)
check('the header has the Section marker and the footer the page number',
      b'\x1328(S3' in data and b'\x1322(' in data)
check('numbering runs are made by the product (BZG), no number is typed',
      b'BZG\\1\\ \x00\r\n' in data and b'\r\nFLA0\r\n' not in data)
pictures = [m.decode('ascii') for m in
            re.findall(rb'\x1325\(1([^\x00]*)\x00', data)]
check('two pictures are named, each a PNG that is in the folder',
      len(pictures) == 2 and len(set(pictures)) == 2 and all(
          read(os.path.join(SAMPLES, p)).startswith(b'\x89PNG\r\n\x1a\n')
          for p in pictures if os.path.isfile(os.path.join(SAMPLES, p))) and
      all(os.path.isfile(os.path.join(SAMPLES, p)) for p in pictures),
      pictures)
check('footnotes, notes, keycaps, an equation and index entries are in it',
      data.count(b'\x131(') >= 3 and data.count(b'\x1337(') >= 3 and
      data.count(b'\x139(') == 1 and b'Note.' in data)
check('a quotation, a listing and a table of the manual rule are in it',
      b'\r\nBGQuote\x00\r\n' in data and b'\r\nBGListing\x00\r\n' in data
      and b'\r\nCB89,1\r\n' in data)

# -------------------------------------------------------------- the text
# The text of the T records, without markers and attribute changes.
plain = re.sub(rb'\x13\d+\(.*?\x13\)', b'', data, flags=re.S)
plain = re.sub(rb'\x12..', b'', plain, flags=re.S)
entries = [t.replace(EOL, b'').decode('latin-1') for t in
           re.findall(rb'\r\nT(.*?)\x00\r\n', plain, flags=re.S)]


def table(caption, ncols, nrows):
    """The rows of the table that follows the caption that begins with
    CAPTION: a list of lists of the texts of the cells."""
    for i, e in enumerate(entries):
        if e.startswith(caption):
            cells = entries[i + 1:i + 1 + ncols * nrows]
            return [cells[r * ncols:(r + 1) * ncols] for r in range(nrows)]
    return []


def pair(text):
    return [x.strip() for x in text.split('/')]


# ------------------------------------------------------------ (d) numbers
meas = read_text(os.path.join(root, 'docs', 'MEASUREMENTS.txt'))
memo = read_text(os.path.join(root, 'docs', 'MEMORY.txt'))
bigd = read_text(os.path.join(root, 'docs', 'BIG-DOCUMENTS.txt'))
testing = read_text(os.path.join(root, 'docs', 'TESTING.txt'))


def rows_after(text, marker, count, pattern):
    """The first COUNT lines after the line that has MARKER that match the
    pattern, as match objects."""
    at = text.index(marker)
    found = []
    for ln in text[at:].split('\n')[1:]:
        m = re.match(pattern, ln)
        if m:
            found.append(m)
            if len(found) == count:
                break
    return found


PAIR4 = r'^\s+(\d+)\s+' + r'\s+'.join([r'([\d.]+)/\s*([\d.]+)'] * 4) + r'\s*$'
mem_rows = rows_after(meas, 'Memory (peak resident, megabytes) of the '
                      'terminal Editor: before / after', 5, PAIR4)
time_rows = rows_after(meas, 'Time in milliseconds (median of three runs), '
                       'terminal Editor: before / after', 5, PAIR4)
save_rows = rows_after(meas, 'The time of a save, in milliseconds, of the '
                       'three builds', 5, r'^\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)')
mp_rows = rows_after(meas, 'pages  doc KB  PDF pages  passes', 5,
                     r'^\s+(\d+)\s+\d+\s+\d+\s+(\d)\s+([\d.]+)\s+[\d. ]*?'
                     r'([\d.]+)\s*$')
check('the four tables of MEASUREMENTS.txt were read (5 rows each)',
      len(mem_rows) == 5 and len(time_rows) == 5 and len(save_rows) == 5 and
      len(mp_rows) == 5, (len(mem_rows), len(time_rows), len(save_rows),
                          len(mp_rows)))
PAGES = pics.PAGES
d_mem = pics.DATA['memory-save'][1]
d_time = {'before': [r[0] for r in sd.SAVE],
          'memory work only': [r[1] for r in sd.SAVE],
          'final': [r[2] for r in sd.SAVE]}
d_pt = pics.DATA['print-time'][1]
check('the sizes of the measured documents are 100, 500, 1000, 2000 and 3625 '
      'pages', PAGES == [100, 500, 1000, 2000, 3625], PAGES)
check('chart: the memory after a save is the column save of '
      'MEASUREMENTS.txt (%s)' % ' '.join(str(p) for p in PAGES),
      [int(m.group(1)) for m in mem_rows] == PAGES and
      [float(m.group(9)) for m in mem_rows] == d_mem['after'] and
      [float(m.group(8)) for m in mem_rows] == d_mem['before'],
      (d_mem, [m.groups() for m in mem_rows]))
check('the time of a save of the three builds is that of '
      'MEASUREMENTS.txt',
      [int(m.group(1)) for m in save_rows] == PAGES and
      [int(m.group(2)) for m in save_rows] == d_time['before'] and
      [int(m.group(3)) for m in save_rows] == d_time['memory work only'] and
      [int(m.group(4)) for m in save_rows] == d_time['final'],
      (d_time, [m.groups() for m in save_rows]))
check('chart: the time of a print and its passes are those of '
      'MEASUREMENTS.txt (10, 100, 500, 1000 and 3600 pages)',
      [int(m.group(1)) for m in mp_rows] == d_pt['pages'] and
      [int(m.group(2)) for m in mp_rows] == d_pt['passes'] and
      [float(m.group(3)) for m in mp_rows] == d_pt['seconds'],
      (d_pt, [m.groups() for m in mp_rows]))
shadow_names = ['the text of the document', 'the records of the blocks',
                'the Undo shadow: the text', 'the Undo shadow: lines',
                'the layout of the screen', 'help file, buffers',
                'total alive']
shadow = []
for nm in shadow_names:
    m = re.search(r'^\s+' + re.escape(nm) + r'.*?\s([\d.]+)\s+([\d.]+)\b',
                  memo, re.M)
    shadow.append((m.group(1), m.group(2)) if m else None)
d_sh = [(r[1], r[2]) for r in sd.SHADOW]
check('chart: the parts of the memory of 1000 pages are those of '
      'MEMORY.txt, section 2',
      None not in shadow and
      [float(a) for a, b in shadow] == [v[0] for v in d_sh] and
      [float(b) for a, b in shadow] == [v[1] for v in d_sh], shadow)

# The tables of the document are the same numbers.
t_mem = table('Table 5.2.', 5, 6)
check('table 5.2 of the document has the rows of the memory table',
      len(t_mem) == 6 and t_mem[0][0] == 'Pages' and all(
          t_mem[i + 1][0] == str(PAGES[i]) and all(
              pair(t_mem[i + 1][1 + j]) == [m.group(2 + 2 * j),
                                            m.group(3 + 2 * j)]
              for j in range(4)) for i, m in enumerate(mem_rows)), t_mem)
check('the end of a document of 3625 pages moves in 253 ms before and 142 '
      'ms after (the column End of the time table of MEASUREMENTS.txt)',
      (time_rows[4].group(4), time_rows[4].group(5)) == (
          sd.SCALARS['end_before_ms'], sd.SCALARS['end_after_ms']),
      time_rows[4].groups())
t_save = table('Table 5.3.', 4, 6)
check('table 5.3 of the document has the rows of the save table',
      len(t_save) == 6 and all(
          t_save[i + 1] == [m.group(k) for k in (1, 2, 3, 4)]
          for i, m in enumerate(save_rows)), t_save)
t_sh = table('Table 5.1.', 3, 8)
check('table 5.1 of the document has the rows of the memory of 1000 pages',
      len(t_sh) == 8 and all(
          t_sh[i + 1][1:] == list(shadow[i]) for i in range(7)), t_sh)
t_pass = table('Table 3.1.', 4, 6)
check('table 3.1 of the document has the rows of the prints that settle',
      len(t_pass) == 6 and all(
          t_pass[i + 1][0] == str(d_pt['pages'][i]) and
          t_pass[i + 1][1] == str(d_pt['passes'][i]) and
          t_pass[i + 1][2] == str(d_pt['seconds'][i])
          for i in range(5)), t_pass)
check('the numbers of the early fixes in the text are those of storydata.py '
      '(609 and 92 MB, 3.9 and 0.9 s)',
      (sd.SCALARS['early_mem_before'], sd.SCALARS['early_mem_after']) ==
      tuple(str(v) for v in sd.EARLY_MEMORY) and
      (sd.SCALARS['early_open_before'], sd.SCALARS['early_open_after']) ==
      tuple('%.1f' % (v / 10.0) for v in sd.EARLY_OPEN_TENTHS))
big_lines = {
    'Load the file (ms)': 'load the file',
    'First layout (ms)': 'first layout',
    'Memory after the layout, peak (MB)': 'memory after the layout',
    'Search from the start to the end (ms)': 'Search from the start',
    'Type a character, with the layout (ms)': 'type a character',
    'Save (ms)': 'Save  ',
    'The whole PDF (s)': 'the whole PDF',
    'The largest process, pdfTeX (MB)': 'the largest process',
}
t_big = table('Table 7.1.', 4, 11)
ok = len(t_big) == 11
bad = []
for row in t_big[1:]:
    key = big_lines.get(row[0])
    if key is None:
        continue
    m = re.search(r'^\s*' + re.escape(key) + r'[^\d]*([\d,.]+) *(?:ms|s|MB)?'
                  r'\s+([\d,.]+) *(?:ms|s|MB)?\s+([\d,.]+)', bigd, re.M)
    if m is None or list(m.groups()) != row[1:]:
        ok = False
        bad.append((row, m.groups() if m else None))
check('table 7.1 of the document has the numbers of BIG-DOCUMENTS.txt',
      ok, bad)
counts = {}
for m in re.finditer(r'^\s+(\w+test)\s+(\d+)(?:\s+(\w+test)\s+(\d+))?\s*$',
                     testing, re.M):
    counts[m.group(1)] = int(m.group(2))
    if m.group(3):
        counts[m.group(3)] = int(m.group(4))
t_cnt = table('Table 6.3.', 2, 4)
check('table 6.3 of the document has the counts of the verification: the '
      'sum of the sixteen unit programs of TESTING.txt, the smoke and the X11 '
      'smoke, as storydata.py has them', len(counts) == 16 and
      sum(counts.values()) == sd.UNIT_CHECKS and len(t_cnt) == 4 and
      [r[1] for r in t_cnt[1:]] == ['{:,}'.format(sd.UNIT_CHECKS),
                                    '{:,}'.format(sd.SMOKE_OK),
                                    '{:,}'.format(sd.XSMOKE_OK)],
      (counts, t_cnt))
git = shutil.which('git')
days = {'days': sd.DAYS, 'commits': sd.COMMITS}
if git is not None and os.path.isdir(os.path.join(root, '.git')) or \
        os.path.isfile(os.path.join(root, '.git')):
    st, out, err = run([git, '-C', root, 'log', '--format=%ad',
                        '--date=format:%d'] , env=dict(os.environ))
    st2, out2, err2 = run([git, '-C', root, 'log', '--format=%ad',
                           '--date=format:%Y-%m'])
    # A repository with a minimal history has far fewer commits than the
    # timeline of the story counts: the timeline cannot be compared then.
    if st == 0 and st2 == 0 and \
            len(out.split()) >= sum(days['commits']):
        per = {}
        for d, ym in zip(out.split(), out2.split()):
            if ym == '2026-11':
                per[d] = per.get(d, 0) + 1
        full = list(zip(days['days'], days['commits']))
        check('the commits of the days 08 to 18 November in the timeline are '
              'those of the log', all(per.get(d, 0) == n for d, n in full),
              (per, full))
        st3, out3, err3 = run([git, '-C', root, 'log', '--format=%ad',
                               '--date=format:%Y-%m-%d %H'])
        stamps = [x.split() for x in out3.split('\n') if x]
        upto = [h for d, h in stamps if d <= '2026-11-19']
        check('the commits to 19 November (%d) and the ones made between '
              'midnight and seven (%d) are those of the log' %
              (sd.COMMITS_TO_19, sd.COMMITS_NIGHT),
              len(upto) == sd.COMMITS_TO_19 and
              len([h for h in upto if h < '07']) == sd.COMMITS_NIGHT,
              (len(upto), len([h for h in upto if h < '07'])))
    else:
        print('SKIP: the log of the repository cannot be read or holds less '
              'history than the timeline counts')
else:
    print('SKIP: the source tree has no log of the repository')

# ------------------------------------------------------------- the rules
st, out, err = run([sys.executable, '-I', '-B', os.path.join(root, 'manual',
                    'checktables.py'), DOC])
check('every table follows the border rule, with a bold header row '
      '(manual/checktables.py)', st == 0, out + err)

# ------------------------------------------------- (f) the rules of the text
alltext_entries = ' '.join(entries)
check('no repository path and no file name is in the text (docs/, tests/, '
      'src/, samples/, manual/, tools/, scripts/, .txt .py .c .h .src .sh)',
      re.search(r'\b(docs|tests|src|samples|manual|tools|scripts)/',
                alltext_entries) is None and
      re.search(r'\w\.(txt|py|c|h|src|sh|md)\b', alltext_entries) is None,
      re.findall(r'[\w/]*(?:\b(?:docs|tests|src|samples|manual|tools|'
                 r'scripts)/|\w\.(?:txt|py|c|h|src|sh|md)\b)\S*',
                 alltext_entries)[:8])
check('the word owner is not used: the project maintainer is named, and the '
      'assistants are named Claude',
      re.search(r'\bowner', alltext_entries, re.I) is None and
      'project maintainer' in alltext_entries and
      'Claude' in alltext_entries and
      mkstory.AUTHOR == 'The project maintainer and Claude',
      re.findall(r'.{30}\bowner.{20}', alltext_entries, re.I)[:3])
check('the rule about not naming the old program is not told to the reader',
      re.search(r'no document names|names the original|rule of this project',
                alltext_entries) is None)
body = re.findall(rb'\r\nBGBody\x00\r\nT(.*?)\x00\r\n', plain, flags=re.S)
logos = len(re.findall(rb'\x1341\([^\x13]*?\\(?:La)?TeX', data))
# The title of a section that a cross reference prints in brackets is the
# title as it is in its headline, which is not typeset with the logos.
body = [re.sub(rb'(?:[Ss]ection|[Cc]hapter)\s+\([^)]*\)', b'', b) for b in body]
check('TeX and LaTeX are typeset with the logos in the text (%d logo markers) '
      'and no plain word is left in the paragraphs' % logos,
      logos >= 12 and all(re.search(rb'\b(La)?TeX\b', b) is None
                          for b in body),
      [b for b in body if re.search(rb'\b(La)?TeX\b', b)][:2])
check('a cross reference is called a cross reference, and the sentence about '
      'the question marks says so',
      'A reference that' not in alltext_entries and
      'A cross reference without a target' in alltext_entries)
srcs = [os.path.join(SAMPLES, f) for f in sorted(os.listdir(SAMPLES))
        if f.endswith('.src')]
titles = [re.sub(r'\s*\[[a-z0-9-]+\]$', '', ln.split(' ', 1)[1]) for f in
          srcs for ln in read_text(f).split('\n')
          if re.match(r'\.h[1-4] ', ln)]
gone = ['What was left out on purpose', 'The material', 'The bytes of a '
        'document', 'The file format', 'The tests', 'Where to read more',
        'Things the owner has decided to leave']
check('the sections that the project maintainer asked to remove are gone, '
      'and the nine chapters remain', not [g for g in gone if g in titles]
      and len([t for f in srcs for t in read_text(f).split('\n')
               if t.startswith('.h1 ')]) == 9, titles)
check('the complete works of Mark Twain from Project Gutenberg are named, '
      'and the clean room has its references (Samuelson, Sega v. Accolade, '
      'Canion) in a footnote', 'complete works of Mark Twain' in
      alltext_entries and 'Project Gutenberg' in alltext_entries and
      b'Samuelson' in data and b'Accolade' in data and b'Canion' in data)
check('no word of the review is left that the project maintainer objected '
      'to: the keys do not answer, the tools are not said to be missing, and '
      'the number of drivers is not given',
      not re.search(r'seconds to answer|not installed on the machine|'
                    r'36 of them|36 printer drivers', alltext_entries))
check('the words to a page are a data item: the text says how many words a '
      'page holds with the number of storydata.py (%d), and the document '
      'changes when the number changes' % sd.WORDS_PER_PAGE,
      ('%d words to a page' % sd.WORDS_PER_PAGE) in alltext_entries and
      'A page is 735 words' not in alltext_entries)
saved = dict(mkstory.sd.SCALARS)
mkstory.sd.SCALARS['words_per_page'] = '735'
alt = os.path.join(tmp, 'alt.doc')
mkstory.build(mkstory.SOURCE, alt)
mkstory.sd.SCALARS.clear()
mkstory.sd.SCALARS.update(saved)
check('a changed number of storydata.py changes the document (735 words to '
      'a page)', b'735 words to a page' in read(alt) and
      b'%d words to a page' % sd.WORDS_PER_PAGE not in read(alt))

# ------------------------------------------------------------ (e) names
parts = [('Lo', 'tus'), ('Manu', 'script'), ('MS', '.EXE'),
         ('ma', 'ude'), ('claude', '-alien'),
         ('thu', 'tt'), ('claude-', 'admin'), ('harp', '-project'),
         ('/ho', 'me/'), ('/tmp/', 'claude'), ('interch', 'ange'),
         ('Logic Magi', 'cians')]
denied = [a + b for a, b in parts[:-1]]
sources = [os.path.join(SAMPLES, f) for f in sorted(os.listdir(SAMPLES))
           if f.endswith('.src')]
alltext = {p: read(p).decode('latin-1').lower() for p in sources + [DOC]}
found = [(os.path.basename(p), w) for p, t in alltext.items()
         for w in denied if w.lower() in t]
check('no name of the original product, of a machine, of a person or of a '
      'path with a user name is in the document or its sources (%d words '
      'looked for)' % len(denied), not found, found)
check('no line of a source is longer than 80 columns and every source is '
      'ASCII', all(len(l) <= 80 for p in sources
                   for l in read(p).decode('ascii').split('\n')))

# ---------------------------------------------------- (c) the print
pdf = os.path.join(tmp, 'story.pdf')
st, out, err = run([strprint, DOC, '-o', pdf], cwd=SAMPLES)
check('the Printer prints the document without an error', st == 0 and
      os.path.getsize(pdf) > 0 and 'rror' not in err, out + err)
st, text, err = run(['pdftotext', '-layout', pdf, '-'])
st, info, err = run(['pdfinfo', pdf])
npages = int(re.search(r'Pages:\s+(\d+)', info).group(1))
check('the PDF has no unresolved reference (no ??) and no [no section]',
      '??' not in text and '[no section]' not in text and npages > 15,
      (npages, [l for l in text.split('\n') if '??' in l][:3]))
page_text = []
for p in range(1, npages + 1):
    st, t, err = run(['pdftotext', '-f', str(p), '-l', str(p), '-layout',
                      pdf, '-'])
    page_text.append(t)
def roman(n):
    """The lower case roman numeral of a page of the contents."""
    out = ''
    for v, r in ((10, 'x'), (9, 'ix'), (5, 'v'), (4, 'iv'), (1, 'i')):
        while n >= v:
            out += r
            n -= v
    return out


# The title page is the text before the first section.  The Printer makes the
# contents after it, on pages numbered i, ii, ...; the first chapter then
# starts at page 1.
first = next(p for p in range(1, npages)
             if 'is a document processor for long' in page_text[p])
check('the title page has no header and no footer; the other pages have '
      'both (the name of the document and the page number: i, ii for the '
      'contents, then 1 for the first chapter)',
      'Page' not in page_text[0] and 'How' in page_text[0] and
      all(('Page %s' % roman(p)) in page_text[p] and
          'How Strata Was Made' in page_text[p] for p in range(1, first)) and
      all(('Page %d' % (p - first + 1)) in page_text[p] and
          'How Strata Was Made' in page_text[p]
          for p in range(first, npages)), (first, page_text[0][:200]))
flat = ' '.join(' '.join(page_text).split())
check('the passage that tells where you are names the real number of its '
      'section, 1.1.1.1, and not 1.2.1.1',
      'This passage is \u00a71.1.1.1' in flat and '1.2.1.1' not in flat,
      flat[:0])
def margin_numbers(page):
    """The line numbers in the margin of a page of the print."""
    return [int(m) for m in re.findall(r'^\s{0,3}(\d{1,2})\s{2,}\S',
                                       page_text[page], re.M)]


nums = [margin_numbers(p) for p in (first + 2, first + 3, first + 6,
                                    first + 9)]
check('the print has the line numbers of Global Format in the margin, one '
      'on every fifth line, and they start again on every page (four pages '
      'of the first chapters)',
      all(n[:1] == [5] and n == list(range(5, 5 * len(n) + 1, 5))
          for n in nums) and max(len(n) for n in nums) >= 3, nums)
chapters = []
for f in sorted(sources):
    for ln in read_text(f).split('\n'):
        if ln.startswith('.h1 '):
            chapters.append(re.sub(r'\s*\[[a-z0-9-]+\]$', '', ln[4:]))
contents = '\n'.join(page_text[1:first])
ROW = r'^\s*(?:\d+\s{2,})?%d\s+%s[ .]+\s(\d+)\s*$'
check('the contents has a row for every chapter (%d)' % len(chapters),
      len(chapters) == 9 and all(
          re.search(ROW % (i + 1, re.escape(c)), contents, re.M)
          for i, c in enumerate(chapters)), contents[:400])
rowsok = True
for i, c in enumerate(chapters):
    m = re.search(ROW % (i + 1, re.escape(c)), contents, re.M)
    if m is None:
        rowsok = False
        continue
    pg = int(m.group(1))
    # The contents has the first level, then the sections that follow.
    # The pages of the contents count from the first page of the text.
    if not re.search(r'^\s*(?:\d+\s+)?%d\s+%s\s*$' % (i + 1, re.escape(c)),
                     page_text[first + pg - 1], re.M):
        rowsok = False
check('the page of each chapter in the contents is the page on which its '
      'headline is printed', rowsok)
# Every row of the contents, of every level: the headline is on the page that
# the row says (the pages of the contents count from the first page of the
# text).
allrows = re.findall(r'^\s*(?:\d+\s{2,})?(\d+(?:\.\d+)*)\s+(\S.*?)[ .]+\s(\d+)\s*$',
                     contents.split('List of Figures')[0], re.M)
badrows = []
for tag, title, pg in allrows:
    pat = re.compile(r'^\s*(?:\d+\s+)?' + re.escape(tag) + r'\s+' +
                     re.escape(title[:24]), re.M)
    if not pat.search(page_text[first + int(pg) - 1]):
        badrows.append((tag, title, pg))
check('every row of the contents (%d rows, all levels) names the page on '
      'which its headline is printed' % len(allrows),
      len(allrows) >= 40 and not badrows, badrows[:5])
# A line number of the print (the numbers start again on every page) may
# stand in front of a line of text.
caps = re.findall(r'^\s*(?:\d+\s+)?Figure (\d+\.\d+)\.', text, re.M)
rows = re.findall(r'^\s*(?:\d+\s+)?Figure (\d+\.\d+)\..*\s\d+\s*$', text,
                  re.M)
check('two figures have a caption and a row in the list of figures (the '
      'list that the Printer makes from the Figure markers)',
      len(set(caps)) == 2 and len(caps) == 4 and
      sorted(set(rows)) == sorted(set(caps)) and
      'List of Figures' in text, (caps, rows))
check('the index and the list of tables are printed',
      re.search(r'^\s*(?:\d+\s+)?Index', text, re.M) is not None and
      'List of Tables' in text and 'Table 5.2.' in text)

# ------------------------------------------------------ (f) line numbering
# The sample is printed with line numbers as a further example: mode 1 (the
# count starts again on every page), a number on every fifth line, the other
# settings at their defaults.
check('the document holds the line numbering records FZM1 and FZE5, after '
      'the other global records',
      b'\r\nFZM1\r\n' in data and b'\r\nFZE5\r\n' in data and
      data.index(b'\r\nFU2\r\n') < data.index(b'\r\nFZM1\r\n') <
      data.index(b'\r\nFZE5\r\n') < data.index(b'\r\nDP0,'))


def margin_numbers(pdf):
    """The line numbers in the left margin of every page of a PDF: a list
    by page of the whole numbers that stand left of the text column (the
    text starts one inch from the edge), from the top of the page down."""
    st, xml, err = run(['pdftotext', '-bbox', pdf, '-'])
    pages = []
    for part in xml.split('<page ')[1:]:
        found = []
        for m in re.finditer(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                             r'xMax="([\d.]+)" yMax="[\d.]+">(\d+)</word>',
                             part):
            if float(m.group(3)) < 68.0:
                found.append((float(m.group(2)), int(m.group(4))))
        pages.append([n for y, n in sorted(found)])
    return pages


index_page = next(p for p in range(first, npages)
                  if re.search(r'^\s*(?:\d+\s+)?Index\s*$', page_text[p], re.M))
body = list(range(first, index_page))
allnums = margin_numbers(pdf)
nums = {p: allnums[p] for p in body}
steps = all(all(n % 5 == 0 for n in nums[p]) and all(
    b - a == 5 for a, b in zip(nums[p], nums[p][1:])) for p in body)
numbered_pages = [p for p in body if nums[p]]
check('the printed body pages carry line numbers in the margin: every '
      'number is a multiple of 5 and the numbers of a page follow by 5 '
      '(%d of %d pages)' % (len(numbered_pages), len(body)),
      steps and len(numbered_pages) >= len(body) - 8, [
          (p, nums[p][:6]) for p in body if nums[p] and not steps][:4])
check('the count starts again on every page: the first number on every '
      'numbered body page is 5', len(numbered_pages) > 0 and all(
          nums[p][0] == 5 for p in numbered_pages),
      [(p, nums[p][:3]) for p in numbered_pages if nums[p][0] != 5][:4])
cont = 0
okc = True
for p, q in zip(numbered_pages, numbered_pages[1:]):
    if len(nums[p]) < 2:
        continue
    cont += 1
    if nums[q][0] != 5:
        okc = False
check('the count does not go on from page to page: after a page that ends '
      'at 20 or more the next page starts at 5 again (%d pairs of pages)'
      % cont, okc and cont > 8)
tpage = next(p for p in body if 'Table 1.2.' in page_text[p])
tpage = next(p for p in (tpage, tpage + 1) if 'Borders joined' in page_text[p])
st, xml, err = run(['pdftotext', '-bbox', '-f', str(tpage + 1), '-l',
                    str(tpage + 1), pdf, '-'])
words = [(float(m.group(2)), float(m.group(1)), float(m.group(3)),
          m.group(4)) for m in re.finditer(
    r'<word xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" '
    r'yMax="[\d.]+">(.*?)</word>', xml)]
top = min(y for y, x, x2, w in words if w == 'Date')
bottom = max(y for y, x, x2, w in words if w == 'samples.')
nums_at = [(y, w) for y, x, x2, w in words if x2 < 68.0 and w.isnumeric()]
beside = [w for y, w in nums_at if top - 1.0 <= y <= bottom + 1.0]
check('the twelve rows of a table in a Keep part (table 1.2) take no line '
      'number, though the page has numbers below the table',
      bottom - top > 100.0 and beside == [] and
      any(y > bottom for y, w in nums_at), (top, bottom, beside))
# The control: the same document without the two records is printed without
# any number, so the numbers above come from the records.
ctl = os.path.join(tmp, 'ctl')
os.makedirs(ctl)
shutil.copytree(os.path.join(SAMPLES, 'pictures'),
                os.path.join(ctl, 'pictures'))
saved = mkstory.LINENUMBERING
mkstory.LINENUMBERING = []
mkstory.build(mkstory.SOURCE, os.path.join(ctl, 'strata-story.doc'))
mkstory.LINENUMBERING = saved
ctl_pdf = os.path.join(tmp, 'ctl.pdf')
st, out, err = run([strprint, os.path.join(ctl, 'strata-story.doc'), '-o',
                    ctl_pdf], cwd=ctl)
st, ctl_text, err = run(['pdftotext', '-layout', ctl_pdf, '-'])
ctl_nums = margin_numbers(ctl_pdf)
check('the control without the records prints no margin number on any page '
      'and has the same number of pages', len(ctl_nums) == npages and
      not any(ctl_nums), (len(ctl_nums), npages))

# ---------------------------------------- (g) the maintainer's comments
# The comments that the project maintainer wrote into the document are
# answered and removed, and the sections that he asked to print only in a
# build that is not a release are inside If markers on the build type.
check('no comment marker is left in the document', b'\x136(' not in data and
      b'\x13 6(' not in data, data.count(b'\x136('))


def if_branches(doc):
    """The If markers of a document that have an Else branch: a list of
    (text of the main branch, text of the else branch), the text without
    markers and attribute changes, with the white space made single.  The
    markers If, Else and Endif (33, 34 and 35) stand in blocks of their own
    in the text of the generator of the sample."""
    body = re.sub(rb'\x13(3[345])\(.*?\x13\)',
                  lambda m: b'\x00#' + m.group(1) + b'#\x00', doc,
                  flags=re.S)
    body = re.sub(rb'\x13\d+\(.*?\x13\)', b'', body, flags=re.S)
    body = re.sub(rb'\x12..', b'', body, flags=re.S)
    parts = re.split(rb'\x00#(3[345])#\x00', body)
    stack = []
    done = []
    text = lambda b: ' '.join(re.sub(rb'\r\n[A-Z*][A-Za-z0-9]*|\x1d|\x00', b' ',
                                      b).decode('latin-1').split())
    for i in range(0, len(parts), 2):
        chunk = parts[i]
        if stack:
            stack[-1][-1] += chunk
        if i + 1 < len(parts):
            kind = parts[i + 1]
            if kind == b'33':
                stack.append([b''])
            elif kind == b'34' and stack:
                stack[-1].append(b'')
            elif kind == b'35' and stack:
                done.append([text(x) for x in stack.pop()])
    return [d for d in done if len(d) == 2]


branches = if_branches(data)
check('an If marker with an Else branch is in the document (%d), and no If '
      'marker has two branches of the same text: a paragraph for developer '
      'builds only has no Else branch' % len(branches),
      len(branches) >= 1 and all(m != e and m != '' and e != ''
                                 for m, e in branches), branches)
twin = os.path.join(tmp, 'twin')
shutil.copytree(SAMPLES, twin)
fp = os.path.join(twin, 'chapter-01.src')
src1 = open(fp).read()
same = 'This copy is the release edition: it has eight chapters.'
other = ('This copy is the developer edition: it has nine chapters.  The '
         'release edition\nomits chapter 8 and four sections of other '
         'chapters.')
assert same in src1 and other in src1
open(fp, 'w').write(src1.replace(other, same))
mkstory.build(os.path.join(twin, 'story.src'),
              os.path.join(twin, 'strata-story.doc'))
twins = [m for m, e in if_branches(read(os.path.join(twin, 'strata-story.doc')))
         if m == e]
check('the control: a document whose Else branch repeats the text of the '
      'main branch is found by the check', len(twins) == 1, twins)
alltext = ' '.join(re.sub(r'\s+', ' ', read_text(f)) for f in srcs)
check('the parallel port is attached to a file, it is not sent (the comment '
      'on the word send)', 'attaches the parallel port to a file' in alltext
      and 'send the parallel port' not in alltext)
check('the paragraph on the printer drivers is not the list of short '
      'sentences of the first version', 'Two printer drivers were used.' not
      in alltext and 'The Generic driver writes plain text' in alltext)
SEVEN = ['Defects of the set-up', 'Measurement', 'Borders in print',
         'Faulty tests', 'Organisation of the work']
inside = []
inside_top = []
for f in srcs:
    depth = 0
    for ln in read_text(f).split('\n'):
        if re.match(r'\.if\s+\.STRATA_BUILD_TYPE\s+#\s+release\s*$', ln):
            depth += 1
        elif ln.startswith('.else') or ln.startswith('.endif'):
            depth = 0
        elif depth > 0 and re.match(r'\.h[1-4] ', ln):
            if re.match(r'\.h[12] ', ln):
                inside_top.append(re.sub(r'\s*\[[a-z0-9-]+\]$', '',
                                         ln.split(' ', 1)[1]))
            inside.append(re.sub(r'\s*\[[a-z0-9-]+\]$', '',
                                 ln.split(' ', 1)[1]))
check('the five developer sections are in an If marker on the build '
      'type, for a build that is not a release (%d headlines inside)' %
      len(inside), all(s in inside for s in SEVEN), inside)
devpdf = os.path.join(tmp, 'dev.pdf')
relpdf = os.path.join(tmp, 'rel.pdf')
st1, o1, e1 = run([strprint, DOC, '-o', devpdf, '--special',
                   '.STRATA_BUILD_TYPE', 'debug'], cwd=SAMPLES)
st2, o2, e2 = run([strprint, DOC, '-o', relpdf, '--special',
                   '.STRATA_BUILD_TYPE', 'release'], cwd=SAMPLES)
check('both editions print without an error', st1 == 0 and st2 == 0 and
      os.path.getsize(devpdf) > 0 and os.path.getsize(relpdf) > 0 and
      'rror' not in e1 + e2, o1 + e1 + o2 + e2)
st, devtext, err = run(['pdftotext', '-layout', devpdf, '-'])
st, reltext, err = run(['pdftotext', '-layout', relpdf, '-'])
st, i1, err = run(['pdfinfo', devpdf])
st, i2, err = run(['pdfinfo', relpdf])
def count_words(txt):
    """The words of a printed text: the leader dots of the contents and
    the lists are not words."""
    return len([w for w in txt.split() if w.strip('.')])


dev_words = count_words(devtext)
rel_words = count_words(reltext)
npdev = int(re.search(r'Pages:\s+(\d+)', i1).group(1))
nprel = int(re.search(r'Pages:\s+(\d+)', i2).group(1))
print('     pages: developer edition %d, release edition %d' % (npdev, nprel))
check('the release edition is smaller than the developer edition (at '
      'least 3 pages less)', nprel + 3 <= npdev and nprel > 10,
      (npdev, nprel))
check('the editions are at most half of the size of the editions before the '
      'rewrite: %d pages (before %d) and %d words (before %d) for the '
      'developer edition, %d pages (before %d) and %d words (before %d) for '
      'the release edition' % (npdev, sd.BEFORE['dev'][0], dev_words,
      sd.BEFORE['dev'][1], nprel, sd.BEFORE['rel'][0], rel_words,
      sd.BEFORE['rel'][1]),
      npdev * 2 <= sd.BEFORE['dev'][0] and nprel * 2 <=
      sd.BEFORE['rel'][0] and dev_words * 2 <= sd.BEFORE['dev'][1] and
      rel_words * 2 <= sd.BEFORE['rel'][1],
      (npdev, nprel, dev_words, rel_words))
check('the developer edition has the default print: the same pages as the '
      'print without a build type', npdev == npages, (npdev, npages))


def rows_of(txt):
    """The rows of the contents of a print: (tag, title) of every row."""
    head = txt.split('List of Figures')[0]
    return re.findall(r'^\s*(?:\d+\s{2,})?(\d+(?:\.\d+)*)\s+(\S.*?)[ .]+\s'
                      r'\d+\s*$', head, re.M)


devrows = rows_of(devtext)
relrows = rows_of(reltext)
devtitles = [t for _, t in devrows]
reltitles = [t[:30] for _, t in relrows]
check('every headline of the first two levels that is inside the If markers '
      'has a row in the contents of the developer edition', all(
          any(d.startswith(s[:24]) for d in devtitles) for s in inside_top),
      [s for s in inside_top if not any(d.startswith(s[:24])
                                        for d in devtitles)])
check('no headline that is inside the If markers has a row in the contents '
      'of the release edition and none of them is printed as a headline',
      not [s for s in inside if any(r.startswith(s[:24])
                                    for r in reltitles)] and
      not [s for s in SEVEN if re.search(
          r'^\s*(?:\d+\s+)?\d+(?:\.\d+)*\s+' + re.escape(s[:24]),
          reltext, re.M)],
      [s for s in inside if any(r.startswith(s[:24]) for r in reltitles)])


def consecutive(rows):
    """True when the children of every section are numbered 1, 2, 3 ..."""
    seen = {}
    for tag, _ in rows:
        parts = tag.split('.')
        parent = '.'.join(parts[:-1])
        seen.setdefault(parent, []).append(int(parts[-1]))
    return all(v == list(range(1, len(v) + 1)) for v in seen.values()), seen


ok_dev, seen_dev = consecutive(devrows)
ok_rel, seen_rel = consecutive(relrows)
check('the numbers of the sections follow what is printed: in both '
      'editions the children of every section are numbered 1, 2, 3 ... '
      'with no gap', ok_dev and ok_rel and len(seen_rel['']) == 8 and
      len(seen_dev['']) == 9, (seen_rel, seen_dev))
check('the third chapter of the release edition has four sections numbered '
      '3.1 to 3.4: Hyphenation and templates is 3.4, and 3.5 in the '
      'developer edition',
      re.search(r'^\s*(?:\d+\s{2,})?3\.4\s+Hyphenation and templates',
                reltext, re.M) is not None and
      re.search(r'^\s*(?:\d+\s{2,})?3\.5\s+Hyphenation and templates',
                devtext, re.M) is not None and
      seen_rel.get('3') == [1, 2, 3, 4], seen_rel.get('3'))
tabs_rel = re.findall(r'^\s*(?:\d+\s+)?Table (\d+)\.(\d+)\.', reltext, re.M)
tabs_dev = re.findall(r'^\s*(?:\d+\s+)?Table (\d+)\.(\d+)\.', devtext, re.M)


def table_numbers_ok(found):
    """The table numbers of one edition: in every chapter 1, 2, 3 ... with
    no gap, and the chapters in the order of the document (each table is
    in the list of tables and in the text)."""
    by = {}
    for c, n in found:
        by.setdefault(c, set()).add(int(n))
    return all(sorted(v) == list(range(1, len(v) + 1))
               for v in by.values()), by


ok_t_rel, by_rel = table_numbers_ok(tabs_rel)
ok_t_dev, by_dev = table_numbers_ok(tabs_dev)
check('the numbers of the tables follow what is printed: no gap in either '
      'edition, and the release edition has fewer tables',
      ok_t_rel and ok_t_dev and len(by_rel) == 7 and len(by_dev) == 9 and
      sum(map(len, by_rel.values())) < sum(map(len, by_dev.values())),
      (by_rel, by_dev))
check('the text of the omitted sections is not in the release edition and '
      'is in the developer edition: Table 3.1, Figure 3.1, Table 8.1',
      all(w in devtext for w in ('Table 3.1.', 'Figure 3.1.', 'Table 8.1.'))
      and not any(w in reltext for w in ('Table 3.1.', 'Figure 3.1.',
                                         'Table 8.1.')),
      [w for w in ('Table 3.1.', 'Figure 3.1.', 'Table 8.1.')
       if w in reltext])
flat_dev = re.sub(r'\s+', ' ', re.sub(r'^\s*\d+\s{2,}', '', devtext, flags=
                                      re.M))
flat_rel = re.sub(r'\s+', ' ', re.sub(r'^\s*\d+\s{2,}', '', reltext, flags=
                                      re.M))
check('the edition says which it is, each with its own text, and only the '
      'developer edition describes the work of the chapter that the release '
      'edition omits',
      'This copy is the developer edition: it has nine chapters' in flat_dev
      and 'This copy is the release edition: it has eight chapters' in
      flat_rel and 'This copy is the release edition' not in flat_dev and
      'This copy is the developer edition' not in flat_rel and
      'describes how the work was divided' in flat_dev and
      'describes how the work was divided' not in flat_rel,
      flat_rel[:200])
check('neither edition has an unresolved reference (no ??, no [no section]) '
      'and the release edition names no section that it does not hold',
      all('??' not in x and '[no section]' not in x
          for x in (devtext, reltext)) and
      re.search(r'\bSection\s+9\b|\bsection\s+9\b', flat_rel) is None,
      [x for x in re.findall(r'.{20}[Ss]ection \d+.{20}', flat_rel)][:6])
check('the note on the disassembly is in both editions: no machine code was '
      'disassembled, and what was used and what was observed are named',
      all('No machine code of the old program was disassembled' in x and
          'What was used' in x and 'What was observed' in x
          for x in (flat_dev, flat_rel)), flat_rel[:200])
check('no edition names a database of a disassembler: no IDA, no idb, no '
      'msedit anywhere in the text',
      all(re.search(r'\bIDA\b|\.idb\b|\bidb\b|msedit', x, re.I) is None
          for x in (flat_dev, flat_rel)),
      [m.group(0) for x in (flat_dev, flat_rel) for m in
       re.finditer(r'.{20}(?:IDA|idb|msedit).{20}', x, re.I)][:3])
capsd = set(re.findall(r'^\s*(?:\d+\s+)?Figure (\d+\.\d+)\.', devtext, re.M))
capsr = set(re.findall(r'^\s*(?:\d+\s+)?Figure (\d+\.\d+)\.', reltext, re.M))
def printed_table(pdf, cap, header, rows, right=540.0, tabcolsep=6.0):
    """The columns of a table in the printed PDF, measured with
    pdftotext -bbox: for each column the width of its box (from the text of
    its header to the text of the next one; the last box ends at the right
    margin), the width its widest cell needs (text plus a tabcolsep on both
    sides), and the number of printed lines of the table.  The table is the
    one whose caption (Table N.) and header line are on the same page."""
    st, info, err = run(['pdfinfo', pdf])
    pages = int(re.search(r'Pages:\s+(\d+)', info).group(1))
    for pg in range(1, pages + 1):
        st, xml, err = run(['pdftotext', '-f', str(pg), '-l', str(pg),
                            '-bbox', pdf, '-'])
        words = [(float(a), float(b), float(c), float(d), w) for a, b, c, d, w
                 in re.findall(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                               r'xMax="([\d.]+)" yMax="([\d.]+)">(.*?)</word>',
                               xml)]
        lines = {}
        for w in words:
            lines.setdefault(round(w[1]), []).append(w)
        head = None
        has_cap = any([w[4] for w in sorted(lines[y])][:2] == ['Table', cap]
                      for y in lines)
        for y in sorted(lines):
            ws = sorted(lines[y])
            if has_cap and [w[4] for w in ws] == header:
                head = y
                break
        if head is None:
            continue
        xs = [w[0] for w in sorted(lines[head])]
        body = [sorted(lines[y]) for y in sorted(lines) if y > head and y <
                head + 20 * (len(rows) + 2)]
        # The rows of the table are the lines that follow the header and
        # that stay inside its columns: keep as many as the table has rows
        # plus any line that a wrapped cell would add.
        tokens = [sum(len(c.split()) for c in r.split('|')) for r in rows]
        taken = []
        count = 0
        for ws in body:
            if count < len(rows) or (ws[0][0] > xs[0] + 1 and
                                     len(taken) < len(rows) + 4):
                taken.append(ws)
                if ws[0][0] <= xs[0] + 1:
                    count += 1
            else:
                break
        taken = taken[:len(rows) + 4]
        cols = []
        for i, x0 in enumerate(xs):
            x1 = xs[i + 1] if i + 1 < len(xs) else right + tabcolsep
            # The box of a column runs from a tabcolsep before its text to
            # a tabcolsep before the text of the next column (the last box
            # ends at the right margin).
            width = x1 - x0
            ends = [w[2] for ws in taken + [sorted(lines[head])] for w in ws
                    if x0 - 3 <= w[0] < x1 - tabcolsep - 3]
            need = (max(ends) - x0 + 2 * tabcolsep) if ends else 0.0
            cols.append((width, need))
        flat = [len(ws) for ws in taken]
        return cols, len(taken), tokens, flat
    return None, 0, [], []


for cap, name, hdr in (('5.2', 'memory', ['Pages', 'Open', 'End', 'Search',
                                          'Save']),):
    rws = sd.rows(name)
    cols, nlines, tokens, flat = printed_table(devpdf, '%s.' % cap, hdr,
                                               rws)
    ok = cols is not None
    ratio_fill = max(w / n for w, n in cols if n > 0) if ok else 0
    numeric = [w for w, n in cols[1:]] if ok else [1, 99]
    spread = max(numeric) / min(numeric) if ok else 99
    check('table %s in the print: every row is one printed line and no '
          'number is broken over two lines (the %d rows have %s words)' %
          (cap, len(rws), tokens), ok and nlines == len(rws) and
          flat == tokens, (nlines, flat, tokens))
    check('table %s in the print: every cell fits its column, and no column '
          'is more than 1.6 times as wide as its widest cell needs (the '
          'widths are %s, the need is %s points)' % (cap, [round(w) for w, n
          in cols] if ok else '', [round(n) for w, n in cols] if ok else ''),
          ok and all(n <= w + 0.5 and w <= 1.6 * n for w, n in cols),
          cols)
    check('table %s in the print: the four number columns, which hold the '
          'same kind of text (two numbers and a slash), are within 1.2 of '
          'one another in width (%.2f)' % (cap, spread), ok and spread <= 1.2,
          cols)

# ---- every table of the document is laid out to its text ----------------
# The columns of a table are measured in the printed PDF, whatever the table
# is: the caption line (Table N.M.) is followed by the header line, whose
# cells start the columns; the rows are the lines below that start at the
# same columns.  A column's box runs from its start to the start of the next
# column (the last one to the right margin), and its need is the widest text
# in it plus a column gap on both sides.
TABGAP = 6.0


def table_words(pdf, pg):
    st, xml, err = run(['pdftotext', '-f', str(pg), '-l', str(pg), '-bbox',
                        pdf, '-'])
    return [(float(a), float(b), float(c), float(d), w) for a, b, c, d, w in
            re.findall(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                       r'xMax="([\d.]+)" yMax="([\d.]+)">(.*?)</word>',
                       xml)]


def table_cells(ws):
    out = []
    for w in sorted(ws):
        if out and w[0] - out[-1][1] < 2 * TABGAP - 1:
            out[-1][1] = w[2]
        else:
            out.append([w[0], w[2]])
    return [tuple(c) for c in out]


def scan_tables(pdf):
    """A list of (caption number, page, rows, [(box, need), ...]) for every
    table of the document that is not the list of tables."""
    st, info, err = run(['pdfinfo', pdf])
    pages = int(re.search(r'Pages:\s+(\d+)', info).group(1))
    found = []
    for pg in range(3, pages + 1):
        ws = table_words(pdf, pg)
        if not ws:
            continue
        # The list of tables of the contents is not a table of the text.
        flat_page = ' '.join(w[4] for w in sorted(ws, key=lambda a: (a[1],
                                                                    a[0])))
        if 'List of Tables' in flat_page:
            continue
        right = max(w[2] for w in ws)
        lines = {}
        for w in ws:
            lines.setdefault(round(w[1]), []).append(w)
        ys = sorted(lines)
        for i, y in enumerate(ys):
            t = sorted(lines[y])
            if not (len(t) >= 2 and t[0][4] == 'Table' and
                    re.match(r'\d+\.\d+\.?$', t[1][4])) or i + 1 >= len(ys):
                continue
            hc = table_cells(lines[ys[i + 1]])
            if len(hc) < 2:
                continue
            xs = [c[0] for c in hc]
            rows = []
            for y2 in ys[i + 2:]:
                rc = table_cells(lines[y2])
                match = sum(1 for c in rc if any(abs(c[0] - x) < 2.5
                                                 for x in xs))
                if match >= 2 or (rows and rc and rc[0][0] > xs[0] + 1 and
                                  match >= 1):
                    rows.append(rc)
                else:
                    break
            if not rows:
                continue
            cols = []
            for k, x0 in enumerate(xs):
                x1 = xs[k + 1] if k + 1 < len(xs) else right + TABGAP
                ends = [c[1] for r in [hc] + rows for c in r
                        if abs(c[0] - x0) < 2.5]
                cols.append((x1 - x0, (max(ends) - x0 + 2 * TABGAP)
                             if ends else 0.0))
            found.append((t[1][4].rstrip('.'), pg, len(rows), cols))
    return found


def table_faults(cols):
    """What is wrong with the columns of a table, as a list of words: a text
    wider than its column, a column of short text that is much wider than
    its text, or such columns that differ much in how wide they are for
    their text.  A column whose longest line fills it holds text that wraps,
    and the last column takes the rest of the line."""
    n = len(cols)
    boxes = [c[0] for c in cols]
    need = [c[1] for c in cols]
    wrap = [need[k] >= 0.97 * boxes[k] for k in range(n)]
    bad = []
    if any(need[k] > boxes[k] + 0.5 for k in range(n)):
        bad.append('text wider than its column')
    flex = [boxes[k] / need[k] for k in range(n - 1)
            if not wrap[k] and need[k] > 0]
    if flex and max(flex) > 1.8:
        bad.append('a column %.2f times as wide as its text' % max(flex))
    if flex and max(flex) / min(flex) > 1.3:
        bad.append('columns %.2f apart in width for their text' %
                   (max(flex) / min(flex)))
    if not wrap[-1] and need[-1] > 0 and boxes[-1] > 2.5 * need[-1]:
        bad.append('the last column is %.2f times as wide as its text' %
                   (boxes[-1] / need[-1]))
    return bad


for edition, pdf in (('developer', devpdf), ('release', relpdf)):
    tabs = scan_tables(pdf)
    faulty = [(cap, table_faults(cols), [(round(b), round(n))
                                         for b, n in cols])
              for cap, pg, nr, cols in tabs if table_faults(cols)]
    check('every table of the %s edition (%d of them) is laid out to its '
          'text: no text wider than its column, no column of short text much '
          'wider than its text, and those columns alike in width' %
          (edition, len(tabs)), len(tabs) >= (15 if edition == 'developer'
                                              else 13) and faulty == [],
          faulty)
# The control: the document with the widths that the tables had before they
# were fitted has tables that the check refuses.
OLDW = (('chapter-03.src', '.table 8 9 11 18', '.table 10 10 12 18'),
        ('chapter-01.src', '.table 20 44', '.table 34 12'),
        ('chapter-01.src', '.table 13 48', '.table 34 12'),
        ('chapter-05.src', '.table 41 7 10', '.table 36 10 10'),
        ('chapter-05.src', '.table 8 10 26 14', '.table 10 14 24 14'),
        ('chapter-06.src', '.table 51 12', '.table 34 12'),
        ('chapter-07.src', '.table 33 7 8 12', '.table 34 12 12 12'))
oldsrc = os.path.join(tmp, 'oldw')
shutil.copytree(SAMPLES, oldsrc)
for fn, new, old in OLDW:
    fp = os.path.join(oldsrc, fn)
    txt = open(fp).read()
    assert new in txt, (fn, new)
    open(fp, 'w').write(txt.replace(new, old, 1))
oldpdf = os.path.join(tmp, 'oldw.pdf')
mkstory.build(os.path.join(oldsrc, 'story.src'),
              os.path.join(oldsrc, 'strata-story.doc'))
run([strprint, os.path.join(oldsrc, 'strata-story.doc'), '-o', oldpdf,
     '--special', '.STRATA_BUILD_TYPE', 'debug'], cwd=oldsrc)
oldfaulty = [cap for cap, pg, nr, cols in scan_tables(oldpdf)
             if table_faults(cols)]
check('with the widths of before the tables were fitted, the check finds '
      'faulty tables (%d: %s)' % (len(oldfaulty), ' '.join(oldfaulty)),
      len(oldfaulty) >= 5, oldfaulty)

check('the lists follow the edition: two figures in the developer edition '
      'and one in the release edition (3.1 is left out)',
      len(capsd) == 2 and len(capsr) == 1 and '3.1' in capsd and
      '3.1' not in capsr, (capsd, capsr))

# -------------------------------------------------------------- (b) Editor
d = tui.session([stred, '-H', helpf, DOC], ['wait', 'show'], rows=30)
scr = d[0]
check('the Editor opens the document in the structured mode with the '
      'title of the first section', 'STRUCTURED' in scr and 'How' in scr,
      scr)
d = tui.session([stred, '-H', helpf, DOC], ['F12', 'text:G', 'ENTER',
                'wait', 'PGUP', 'PGUP', 'show', 'ESC'], rows=30)
scr = d[0]
check('the Editor shows the third Global Format panel, Line Numbering, with '
      'the mode Pagewise and a number on every 5th line',
      'Global Format (line numbering)' in scr and 'Pagewise' in scr and
      re.search(r'Number Every\W+5\b', scr) is not None, scr)
NEW = ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'wait']
base = os.path.join(tmp, 'base.doc')
with open(base, 'wb') as f:
    f.write(b'*R3\r\nMR1000\r\nPP\\1\\\x00\r\nP1C33\r\nFHA1\r\nFTT5\r\n'
            b'DP0,8500,11000\r\nYCL99\r\nYIWN2\r\n*B0\r\nS\r\nTBASE\x1d\x00'
            b'\r\n*E\r\n')
# The shipped templates, named by the variable: a copy of the programs in a
# test folder does not have the link of a build folder.
os.environ['STRATA_TEMPLATES'] = os.path.join(root, 'templates')
d = tui.session([stred, '-H', helpf, base], NEW + ['wait', 'show'], rows=30)
scr = d[0]
check('the list of New shows the templates and not the story',
      'Templates' in scr and 'envelope' in scr and
      'story' not in scr.lower()
      and 'Strata Was Made' not in scr, scr)
tpl = os.path.join(root, 'templates')
if os.path.isdir(tpl):
    check('the folder of the templates has no file of the story',
          not any('story' in n.lower() for n in os.listdir(tpl)),
          os.listdir(tpl))
link = os.path.join(build, 'share', 'strata', 'samples', 'story')
if build != '' and os.path.isdir(os.path.join(build, 'share')):
    check('the build folder finds the sample at share/strata/samples/story',
          os.path.isfile(os.path.join(link, 'strata-story.doc')) and
          read(os.path.join(link, 'strata-story.doc')) == data, link)

print('story: %d failures' % fails)
sys.exit(1 if fails else 0)
