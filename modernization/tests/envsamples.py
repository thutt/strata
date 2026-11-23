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

"""The sample envelopes (samples/envelopes/, made by tests/mksamples.py).

(a) the folder has one sample for each envelope template and no other file,
and every sample is what the generator makes; two runs of the generator
make the same bytes;
(b) a sample has a description of its own, names its paper, is not a copy of
its template, and names no product of another maker;
(c) every sample opens in the Editor and shows its address;
(d) every sample prints to one page of the size of its envelope, with the
address in the zone of its style (tests/envzones.py: the numbers of the
sources, not of the generator), sender before recipient, and no other text;
(e) the zone check finds an address that lies outside its zone (a check that
cannot fail checks nothing);
(f) a program that runs in place from a build folder finds the samples in
share/strata/samples/envelopes, as an installed one does.

    STRPRINT=... STRED=... python3 -I -B tests/envsamples.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
import mktemplates
import mksamples
import envzones
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
tmp = tempfile.mkdtemp(prefix='envs')
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
tree = os.path.join(here, '..', 'samples', 'envelopes')
names = sorted(mksamples.sample_name(t) for t in mktemplates.ENVELOPES)


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


# ---- (a) the folder and the generator ------------------------------------
have = sorted(f[:-4] for f in os.listdir(tree)) if os.path.isdir(tree) else []
check('the folder has one sample for each of the %d envelope templates and '
      'no other file' % len(names), have == names and
      all(f.endswith('.doc') for f in os.listdir(tree)), have)
gen = [os.path.join(tmp, 'gen%d' % i) for i in (1, 2)]
for g in gen:
    subprocess.run([sys.executable, '-I', '-B',
                    os.path.join(here, 'mksamples.py'), g], check=True)
check('the committed samples are what the generator makes', all(
    os.path.exists(os.path.join(tree, n + '.doc')) and
    read(os.path.join(tree, n + '.doc')) ==
    read(os.path.join(gen[0], n + '.doc')) for n in names))
check('two runs of the generator make the same bytes (nothing changes from '
      'one run to the next)', all(read(os.path.join(gen[0], n + '.doc')) ==
      read(os.path.join(gen[1], n + '.doc')) for n in names))

# ---- (b) description, template, names ------------------------------------
for t in sorted(mktemplates.ENVELOPES):
    n = mksamples.sample_name(t)
    raw = read(os.path.join(tree, n + '.doc')) if n in have else b''
    traw = read(os.path.join(here, '..', 'templates', t + '.doc'))
    m = re.search(rb'\r\nMD([^\x00]*)\x00', raw)
    d = m.group(1).decode('ascii') if m else ''
    size = envzones.SOURCES[t][2]
    check('%s: has a description of its own: it shows Sample, names the '
          'paper (%s) and has at most 250 characters' % (n, size),
          d.startswith('Sample: ') and size in d and len(d) <= 250 and
          m is not None and m.group(1) !=
          re.search(rb'\r\nMD([^\x00]*)\x00', traw).group(1), d)
    trcpt = mktemplates.ENVELOPES[t][7]
    check('%s: is not a copy of the template: another address, the same '
          'paper' % n, raw != traw and
          not all(l.encode() in raw for l in trcpt) and
          re.search(rb'\r\nDP([^\r]*)\r\n', raw).group(1) ==
          re.search(rb'\r\nDP([^\r]*)\r\n', traw).group(1), n)
    low = raw.lower()
    check('%s: names no product of another maker' % n,
          b'lotus' not in low and b'manuscript' not in low and
          b'samna' not in low, n)

# ---- (c) every sample opens in the Editor ---------------------------------
work = os.path.join(tmp, 'work')
os.makedirs(work)
for t in sorted(mktemplates.ENVELOPES):
    n = mksamples.sample_name(t)
    if n not in have:
        continue
    path = shutil.copy(os.path.join(tree, n + '.doc'), work)
    rl = mksamples.SAMPLES[t][3]
    d = tui.session([stred, '-H', helpf, path], ['wait', 'show'], rows=40)
    scr = d[0]
    check('%s: opens in the Editor with no error and shows the address '
          '(%s)' % (n, rl[1] if len(rl) > 1 else rl[0]),
          rl[-1] in scr and rl[0] in scr and 'rror' not in scr and
          'annot' not in scr and 'nvalid' not in scr, scr)

# ---- (d) every sample prints to the page of its envelope -------------------
for t in sorted(mktemplates.ENVELOPES):
    n = mksamples.sample_name(t)
    if n not in have:
        continue
    pdf = os.path.join(tmp, n + '.pdf')
    rc, out, err = run([strprint, os.path.join(tree, n + '.doc'), '-o', pdf,
                        '--no-banner'])
    check('%s: prints to a PDF with no error' % n,
          rc == 0 and os.path.exists(pdf), (rc, err[-300:]))
    if not os.path.exists(pdf):
        continue
    sw, sh, token, rules = envzones.SOURCES[t]
    np_, pw, ph = pages(pdf)
    check('%s: one landscape page of the size of the envelope, %.0f x %.0f '
          'mm, within 0.6 mm' % (n, sw, sh), np_ == 1 and
          abs(pw / 72 * 25.4 - sw) < 0.6 and
          abs(ph / 72 * 25.4 - sh) < 0.6 and pw > ph,
          (np_, pw / 72 * 25.4, ph / 72 * 25.4))
    boxes, wmm, hmm = ink_blocks(pdf)
    sl, rl = mksamples.SAMPLES[t][2:]
    sb, rb = boxes[0], boxes[-1]
    check('%s: %s' % (n, 'the sender is not on the front (it goes on the '
                      'back): one block of ink' if not sl else
                      'the sender and the recipient are two blocks of ink '
                      '(the German envelope may have more rows of them)'),
          (len(boxes) == 1) if not sl else
          (len(boxes) >= 2 if t == 'envelope-de-din5008-dl' else
           len(boxes) == 2), boxes)
    faults = envzones.zone_faults(rules, sb, rb, wmm, hmm)
    check('%s: the address lies in the zone of its style (%s)' % (n, rules),
          not faults, (faults, boxes, wmm, hmm))
    txt = run(['pdftotext', '-layout', pdf, '-'])[1]
    flat = re.sub(r'\s+', ' ', txt)
    check('%s: the PDF has every line of the address, the sender first' % n,
          all(re.sub(r'\s+', ' ', l) in flat for l in sl + rl) and
          (not sl or flat.find(sl[0]) < flat.find(rl[0])), txt)
    extra = [l for l in txt.split('\n') if l.strip() and
             re.sub(r'\s+', ' ', l.strip()) not in
             [re.sub(r'\s+', ' ', x) for x in sl + rl]]
    check('%s: the PDF has no other text (no marker, no page number)' % n,
          not extra, extra)

# ---- (e) the zone check can fail ------------------------------------------
styles = {}
for t, (sw, sh, token, rules) in envzones.SOURCES.items():
    styles.setdefault(rules, (t, sw, sh))
for rules, (t, sw, sh) in sorted(styles.items()):
    # an address at the very left and top corner, a box of 30 by 20 mm: no
    # zone of any style has the address there
    out_of_zone = (0.5, 0.5, 30.5, 20.5)
    fl = envzones.zone_faults(rules, out_of_zone, out_of_zone, sw, sh)
    check('the zone check of the style %s finds an address at the top left '
          'corner (%s)' % (rules, t), fl != [], fl)

# ---- (f) a build folder finds the samples as an installed one does -------
# The smoke runs a script on copies of the programs, so the folder is taken
# from the build folder when the variable names it.
shared = os.path.join(build if build else os.path.join(
    os.path.dirname(os.path.abspath(stred)), '..'), 'share', 'strata',
    'samples', 'envelopes')
check('a program in place finds the samples in ../share/strata/samples/'
      'envelopes: the same %d files as the tree' % len(names),
      os.path.isdir(shared) and
      sorted(os.listdir(shared)) == sorted(n + '.doc' for n in names) and
      all(read(os.path.join(shared, n + '.doc')) ==
          read(os.path.join(tree, n + '.doc')) for n in names), shared)

# ---- (g) the manual names every sample and has its pictures ----------------
man = read(os.path.join(here, '..', 'manual', 'src', 'file3.src')).decode()
files_list = read(os.path.join(here, '..', 'manual', 'src',
                               'gs-install.src')).decode()
check('the manual lists every sample, one line each (file3.src)',
      all(('{c:%s} |' % n) in man for n in names),
      [n for n in names if ('{c:%s} |' % n) not in man])
check('the install chapter of Getting Started names no sample '
      '(gs-install.src)',
      'share/strata/samples/' not in files_list,
      [l for l in files_list.split('\n') if 'share/strata/samples/' in l][:3])
for pic, fig in (('edit-envelope-sample', 'file-sample-edit-figure'),
                 ('print-envelope-sample', 'file-sample-print-figure')):
    png = os.path.join(here, '..', 'manual', 'doc', 'pictures', pic + '.png')
    check('the manual shows %s: the file is a picture and the text points '
          'to it' % pic, os.path.exists(png) and
          read(png)[:8] == b'\x89PNG\r\n\x1a\n' and
          ('pictures/%s.png' % pic) in man and ('{fig:%s}' % fig) in man,
          png)

shutil.rmtree(tmp)
print('envsamples: %s' % ('all passed' if not fails else
                          '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
