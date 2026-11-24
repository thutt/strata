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

"""The borders of a block in the print: single, double and wide lines at the
sides and the top and bottom of a block and between the columns of a table,
the Top and Bottom Gutter, the Shading, Connect Borders, and the print in
black and white.

The documents are made here and hold no border record but the one that a
check is about.  Page 1 is drawn at 300 dpi and read pixel by pixel: the left
margin is at x = 300 and the top margin at y = 300.

    STRPRINT=... python3 -I -B tests/borderprint.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
for tool in ('pdflatex', 'latex', 'dvipdfmx', 'pdftoppm'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='bprt')
# Nothing of a test goes into the real home of the user.
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
CR = b'\r\n'
DPI = 300


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def trow(t):
    return b'T' + t + b'\x1d\x00' + CR


def doc(cols, props=b'', second=True):
    """One block of the columns (a list of (record bytes, text)), then
    another block with no record."""
    h = (b'*R3' + CR + b'MR1001' + CR + b'PP\\1\\\x00' + CR + b'FHA1' + CR +
         b'DP0,8500,11000' + CR)
    out = h + b'*B0' + CR + b'S' + CR + props
    for k, (rec, text) in enumerate(cols):
        out += (b'CW20' + CR if k < len(cols) - 1 else b'') + rec + trow(text)
    if second:
        out += b'B' + CR + trow(b'Next block')
    return out + b'*E' + CR


def make(name, data, extra=()):
    d = os.path.join(tmp, name)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, 'doc.doc')
    open(path, 'wb').write(data)
    pdf = os.path.join(d, 'doc.pdf')
    subprocess.run([strprint, path, '-o', pdf, '--no-banner', '--no-error-log']
                   + list(extra), capture_output=True)
    tex = os.path.join(d, 'x.tex')
    subprocess.run([strprint, path, '--export-latex', tex] + list(extra),
                   capture_output=True)
    t = open(tex).read() if os.path.exists(tex) else ''

    return pdf, re.sub(r'^% Source document: .*$', '% Source document', t,
                       flags=re.M)


class Img:
    def __init__(self, pdf):
        r = subprocess.run(['pdftoppm', '-r', str(DPI), '-f', '1', '-l', '1',
            pdf], capture_output=True)
        parts = r.stdout.split(b'\n', 3)
        self.w, self.h = map(int, parts[1].split())
        self.px = parts[3]

    def g(self, x, y):
        i = (y * self.w + x) * 3
        return self.px[i], self.px[i + 1], self.px[i + 2]

    def dark(self, x, y):
        return self.g(x, y)[0] < 130

    def row(self, y):
        """The red channel of row y as bytes."""
        return self.px[y * self.w * 3:(y + 1) * self.w * 3:3]

    def hbands(self, minlen=1500, y0=250, y1=1700):
        """The bands of rows that have a long dark line: [first, last]."""
        bands = []
        for y in range(y0, y1):
            n = sum(1 for v in self.row(y) if v < 130)

            if n >= minlen:
                if bands and y - bands[-1][1] <= 1:
                    bands[-1][1] = y
                else:
                    bands.append([y, y])

        return bands

    def runs(self, y, x0, x1):
        out = []
        r = self.row(y)

        for x in range(x0, x1):
            if r[x] < 130:
                if out and x - out[-1][1] <= 1:
                    out[-1][1] = x
                else:
                    out.append([x, x])

        return out

    def text_rows(self, x0, x1, y0, y1):
        """The first and last rows with dark pixels between x0 and x1."""
        ys = [y for y in range(y0, y1) if any(v < 130 for v in
              self.row(y)[x0:x1])]

        return (ys[0], ys[-1]) if ys else (None, None)


BOX = b'CB%d,1' + CR + b'CL1' + CR + b'CR1' + CR
single = BOX % 85
double = BOX % 170
wide = BOX % 255

# ---- no border: nothing is drawn, and the LaTeX text is the same -----------
pdf0, tex0 = make('none', doc([(b'', b'Boxed text here')]))
im0 = Img(pdf0)
check('no border record: no line on the page', im0.hbands() == [])
pdfz, texz = make('zero', doc([(b'CB0,1' + CR, b'Boxed text here')]))
check('a mask of 0 (no side) prints the same LaTeX as no record',
    texz == tex0, [l for l in texz.split('\n') if l not in tex0.split('\n')][:3])

# ---- single, double, wide at the sides and top and bottom ------------------
pdf1, tex1 = make('single', doc([(single, b'Boxed text here')]))
im1 = Img(pdf1)
b1 = im1.hbands()
check('single: two lines (top and bottom), each thin', len(b1) == 2 and
    all(b[1] - b[0] <= 2 for b in b1), b1)
mid = b1[1][0] - 6 if len(b1) == 2 else 400
rl = im1.runs(mid, 280, 340)
rr = im1.runs(mid, 2200, 2400)
check('single: one line at the left of the block and one at the right, at '
    'the edges of the text', len(rl) == 1 and len(rr) == 1 and
    290 <= rl[0][0] <= 312 and 2240 <= rr[0][1] <= 2262, (rl, rr, mid))
pdf2, tex2 = make('double', doc([(double, b'Boxed text here')]))
im2 = Img(pdf2)
b2 = im2.hbands()
check('double: four lines, two at the top and two at the bottom',
    len(b2) == 4, b2)
mid2 = b2[2][0] - 8 if len(b2) == 4 else 400
r2 = im2.runs(mid2, 280, 330)
check('double: two vertical lines at the left edge', len(r2) == 2, r2)
pdf3, tex3 = make('wide', doc([(wide, b'Boxed text here')]))
im3 = Img(pdf3)
b3 = im3.hbands()
check('wide: two lines, each thicker than a single line', len(b3) == 2 and
    all(b[1] - b[0] >= 4 for b in b3) and b3[0][1] - b3[0][0] >
    b1[0][1] - b1[0][0], (b3, b1))
pdf4, tex4 = make('top', doc([(b'CB1,1' + CR, b'Boxed text here')]))
im4 = Img(pdf4)
check('top only: one line, no vertical line at the left', len(im4.hbands()) ==
    1 and not im4.runs(im4.hbands()[0][1] + 40, 280, 330), im4.hbands())
pdf5, tex5 = make('bottom', doc([(b'CB4,1' + CR, b'Boxed text here')]))
im5 = Img(pdf5)
b5 = im5.hbands()
check('bottom only: one line, below the text of the block', len(b5) == 1 and
    im5.text_rows(320, 1400, 280, b5[0][0] - 2)[0] is not None, b5)
pdf6, tex6 = make('left', doc([(b'CB16,1' + CR + b'CL1' + CR, b'Boxed text '
    b'here')]))
im6 = Img(pdf6)
check('left only: no horizontal line, a vertical line at the left',
    im6.hbands() == [] and any(im6.dark(x, 330) for x in range(290, 312)),
    im6.hbands())

# ---- a table of two columns: a line between the columns --------------------
pdf7, tex7 = make('table', doc([(single, b'Left cell'), (single,
    b'Right cell has more words')]))
im7 = Img(pdf7)
b7 = im7.hbands(minlen=900)
mid7 = b7[1][0] - 6 if len(b7) >= 2 else 400
r7 = im7.runs(mid7, 280, 2300)
check('table of two columns: a line at each edge and one between the columns',
    len(r7) >= 3 and 290 <= r7[0][0] <= 312 and 600 <= r7[1][0] <= 1100 and
    2240 <= r7[-1][1] <= 2262, r7)

# ---- the Top and the Bottom Gutter -----------------------------------------
def gap_above(pdf):
    im = Img(pdf)
    b = im.hbands()
    first = im.text_rows(340, 1450, b[0][1] + 2, b[1][0] - 2)[0]
    return first - b[0][1], im


def gap_below(pdf):
    im = Img(pdf)
    b = im.hbands()
    last = im.text_rows(340, 1450, b[0][1] + 2, b[1][0] - 2)[1]
    return b[1][0] - last, im


ga0, _ = gap_above(pdf1)
pdfg, texg = make('gutter', doc([(single, b'Boxed text here')],
    props=b'BT500' + CR + b'BB500' + CR))
ga1, _ = gap_above(pdfg)
check('Top Gutter 0.5 inch: the text is 150 points of pixels lower in the '
    'box', 135 <= ga1 - ga0 <= 165, (ga0, ga1))
gb0, _ = gap_below(pdf1)
gb1, _ = gap_below(pdfg)
check('Bottom Gutter 0.5 inch: the text is 150 pixels higher above the line',
    135 <= gb1 - gb0 <= 165, (gb0, gb1))

# ---- the Shading ------------------------------------------------------------
pdfs, texs = make('shade', doc([(single, b'Boxed text here')],
    props=b'BH40' + CR))
ims = Img(pdfs)
bs = ims.hbands()
yy = (bs[0][1] + bs[1][0]) // 2 if len(bs) == 2 else 400
v = ims.g(1300, yy)
check('Shading 40 percent: the inside of the box is 60 percent white (153)',
    abs(v[0] - 153) <= 6 and v[0] == v[1] == v[2], v)
check('no shading: the inside of the box is white', im1.g(1300, (b1[0][1] +
    b1[1][0]) // 2) == (255, 255, 255))
pdfsb, texsb = make('shadebw', doc([(single, b'Boxed text here')],
    props=b'BH40' + CR), extra=['--black-and-white'])
imb = Img(pdfsb)
v = imb.g(1300, yy)
check('Shading in a print in black and white stays grey', v[0] == v[1] ==
    v[2] and abs(v[0] - 153) <= 6, v)
check('a block with no shading has no colour box in the LaTeX text',
    '\\colorbox[gray]' not in tex1 and '\\colorbox[gray]' in texs)

# ---- Connect Borders --------------------------------------------------------
def next_gap(pdf):
    im = Img(pdf)
    b = im.hbands()
    nxt = im.text_rows(300, 1000, b[-1][1] + 2, im.h - 100)[0]
    return nxt - b[-1][1]


pdfc1, _ = make('conn1', doc([(single, b'Boxed text here')]))
pdfc0, _ = make('conn0', doc([(b'CB85,0' + CR + b'CL1' + CR + b'CR1' + CR,
    b'Boxed text here')]))
n1, n0 = next_gap(pdfc1), next_gap(pdfc0)
check('Connect Borders No: the next block is further away than with Yes',
    n0 - n1 >= 15, (n1, n0))

# ---- a table of three columns and five rows (Border on a highlight) --------
def grid(rows):
    """Blocks of three columns; rows is a list of three CB texts per block."""
    h = (b'*R3' + CR + b'MR1001' + CR + b'PP\\1\\\x00' + CR + b'FHA1' + CR +
         b'DP0,8500,11000' + CR)
    out = h
    for i, cbs in enumerate(rows):
        out += (b'*B0' if i == 0 else b'B') + CR
        out += (b'S' + CR) if i == 0 else b''
        for j, cb in enumerate(cbs):
            rec = (b'CB' + cb + b',1' + CR) if cb != b'' else b''
            out += b'CW20' + CR + rec + trow(b'r%dc%d' % (i + 1, j + 1))
    return out + b'*E' + CR


row1 = [b'89', b'73', b'73']
rowN = [b'84', b'68', b'68']
pdfg, texg = make('grid', grid([row1] + [rowN] * 4))
img = Img(pdfg)
bg = img.hbands(minlen=1200)
# The top line of row 1 (one), the double line below it (two), and one
# single line below each of rows 2 to 5 (four): seven lines, so that there is
# no line above row 2 besides the double line.
check('3x5 table: seven lines: top, a double line below row 1, four below '
      'rows 2 to 5', len(bg) == 7, bg)
check('3x5 table: the double line below row 1 is two dark runs apart, and '
      'row 2 has no line of its own above it',
    len(bg) == 7 and bg[1][1] - bg[1][0] <= 2 and bg[2][1] - bg[2][0] <= 2 and
    3 <= bg[2][0] - bg[1][1] <= 14, bg)
if len(bg) == 7:
    ym = bg[5][0] - 6
    vr = img.runs(ym, 280, 2400)
else:
    vr = []
check('3x5 table: four vertical lines on a row, ONE line between two '
      'columns, each a thin run',
    len(vr) == 4 and all(v[1] - v[0] <= 3 for v in vr), vr)
# Where the neighbours disagree the stronger line is the one that is drawn.
pdfs1, _ = make('strong1', grid([[b'4', b'4', b'4'], [b'2', b'2', b'2']]))
bs1 = Img(pdfs1).hbands(minlen=1200)
pdfs2, _ = make('strong2', grid([[b'8', b'8', b'8'], [b'1', b'1', b'1']]))
bs2 = Img(pdfs2).hbands(minlen=1200)
check('bottom single over top double: the double line is drawn (two lines '
      'between the rows)', len(bs1) == 2 and bs1[0][1] - bs1[0][0] <= 2,
      bs1)
check('bottom double over top single: the double line is drawn (two lines '
      'between the rows)', len(bs2) == 2, bs2)

# ---- corners, tees and crossings: no gap in any line ------------------------
def sides(t, b, l, r):
    """The mask of a cell: a weight (0 to 3) for each side."""
    return t | b << 2 | l << 4 | r << 6


def vgap(im, xs, y0, y1):
    """The first row between y0 and y1 where no pixel of xs is dark, or None
    when a vertical line runs through all of them."""
    for y in range(y0, y1 + 1):
        if not any(im.dark(x, y) for x in xs):
            return y

    return None


def hgap(im, y0, y1, x0, x1):
    """The first column between x0 and x1 where no row of y0 to y1 is dark."""
    for x in range(x0, x1 + 1):
        if not any(im.dark(x, y) for y in range(y0, y1 + 1)):
            return x

    return None


def shape(name, mask, rows=3):
    """A table of three columns and the rows with the same mask in every
    cell: the image, the bands of the lines and the first and last dark
    column of the first row."""
    pdf, _ = make(name, grid([[b'%d' % mask] * 3] * rows))
    im = Img(pdf)
    bands = im.hbands(minlen=1200)
    ym = (bands[0][1] + bands[1][0]) // 2 if len(bands) > 1 else 400
    xs = [x for x in range(280, 2400) if im.dark(x, ym)]

    return im, bands, (xs[0], xs[-1]) if xs else (0, 0), ym


# A table of double lines: the two lines of the top and of the bottom, and
# of every line between two rows, are joined by the vertical lines, which
# run through the space between the two lines.
imd, bd, (xl, xr), ymd = shape('cdouble', sides(2, 2, 2, 2))
check('double 3x3: eight horizontal lines (a pair above row 1, between each '
      'two rows and below row 3)', len(bd) == 8, bd)
if len(bd) == 8:
    ytop, ybot = bd[0][0], bd[7][1]
    # The outer vertical line at the left edge and the one at the right edge
    # go from the first line to the last line with no gap: the corners are
    # closed, and so are the places where the line between two rows meets
    # them (the tees).
    gl = vgap(imd, range(xl, xl + 2), ytop, ybot)
    gr = vgap(imd, range(xr - 1, xr + 1), ytop, ybot)
    check('double 3x3: the outer vertical lines run from the top line to '
          'the bottom line with no gap (closed corners and tees)',
          gl is None and gr is None, (gl, gr, xl, xr, ytop, ybot))
    # The inner vertical line of the double line: also without a gap, in
    # particular in the space between the two lines of a row line.
    gi = vgap(imd, range(xl + 8, xl + 14), bd[0][1], bd[7][0])
    check('double 3x3: the inner vertical line at the left runs from the '
          'inner top line to the inner bottom line with no gap', gi is None,
          (gi, xl))
    runs = imd.runs(ymd, 280, 2400)
    # Two double lines make four runs at the edges; between the columns
    # there is one double line, two runs: four runs at the edges and two
    # lines of two runs between three columns.
    check('double 3x3: ONE double vertical line between two columns (two '
          'thin runs)', len(runs) == 4 + 4, runs)
    if len(runs) == 8:
        mx = [(runs[2][0], runs[3][1]), (runs[4][0], runs[5][1])]
        gm = [vgap(imd, range(a, a + 2), ytop, ybot) for a, _ in mx]
        check('double 3x3: the vertical lines between the columns run '
              'through every line between two rows (crossings)',
              gm == [None, None], gm)
    # Each horizontal line runs the whole width, over the vertical lines.
    gh = [hgap(imd, b[0], b[1], xl, xr) for b in bd]
    check('double 3x3: every horizontal line runs from the left edge to the '
          'right edge with no gap', gh == [None] * 8, gh)

# The same table in single lines is not changed: one line between the rows.
ims, bs_, (xls, xrs), yms = shape('csingle', sides(1, 1, 1, 1))
check('single 3x3: four horizontal lines', len(bs_) == 4, bs_)
if len(bs_) == 4:
    check('single 3x3: the vertical lines run with no gap',
          vgap(ims, range(xls, xls + 2), bs_[0][0], bs_[3][1]) is None and
          vgap(ims, range(xrs - 1, xrs + 1), bs_[0][0], bs_[3][1]) is None)

# Wide lines: thick, joined at the corners.
imw, bw, (xlw, xrw), ymw = shape('cwide', sides(3, 3, 3, 3))
check('wide 3x3: four thick horizontal lines', len(bw) == 4 and
      all(b[1] - b[0] >= 4 for b in bw), bw)
if len(bw) == 4:
    check('wide 3x3: the thick vertical lines run with no gap',
          vgap(imw, range(xlw + 1, xlw + 4), bw[0][0], bw[3][1]) is None and
          vgap(imw, range(xrw - 3, xrw), bw[0][0], bw[3][1]) is None)

# Mixed kinds: double horizontal lines with single vertical ones, and the
# reverse.
imh, bh, (xlh, xrh), ymh = shape('chdouble', sides(2, 2, 1, 1))
check('double lines above and below, single at the sides: eight lines',
      len(bh) == 8, bh)
if len(bh) == 8:
    gh = vgap(imh, range(xlh, xlh + 2), bh[0][0], bh[7][1])
    check('mixed (double horizontal, single vertical): the vertical line runs '
          'from the first line to the last with no gap', gh is None, gh)
imv, bv, (xlv, xrv), ymv = shape('cvdouble', sides(1, 1, 2, 2))
check('single lines above and below, double at the sides: four lines',
      len(bv) == 4, bv)
if len(bv) == 4:
    gv = vgap(imv, range(xlv + 8, xlv + 14), bv[0][0], bv[3][1])
    check('mixed (single horizontal, double vertical): the inner vertical '
          'line runs through the whole table with no gap', gv is None, gv)

# The example of the manual: the top row single with a double line below it,
# four rows below single except the top.  The double line below row 1 meets
# the vertical lines at both ends and the single line between the columns
# runs through it.
if len(bg) == 7:
    xs = [x for x in range(280, 2400) if img.dark(x, bg[0][1] + 8)]
    xl3, xr3 = xs[0], xs[-1]
    yt, yb = bg[0][0], bg[6][1]
    gl = vgap(img, range(xl3, xl3 + 2), yt, yb)
    gr = vgap(img, range(xr3 - 1, xr3 + 1), yt, yb)
    check('3x5 table: the vertical lines at both edges run from the top to '
          'the bottom, through the double line (closed corners)',
          gl is None and gr is None, (gl, gr))
    runs = img.runs(bg[0][1] + 8, 280, 2400)
    gm = [vgap(img, range(r[0], r[1] + 1), yt, yb) for r in runs[1:-1]]
    check('3x5 table: the single vertical lines between the columns run '
          'through the double line', len(runs) == 4 and gm == [None, None],
          (runs, gm))
    check('3x5 table: both lines of the double line run from the left edge '
          'to the right edge', hgap(img, bg[1][0], bg[1][1], xl3, xr3) is None
          and hgap(img, bg[2][0], bg[2][1], xl3, xr3) is None)


shutil.rmtree(tmp)
print('borderprint: %s' % ('all passed' if not fails else '%d FAILURES'
                          % fails))
sys.exit(1 if fails else 0)
