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

"""Pictures print in colour: a PNG (and a JPEG made with pdflatex and
pdftoppm) with a red, a green and a blue part is put in a document with a
Picture marker and printed Final, Draft and in black and white; page 1 is
drawn and the pixels inside the picture are read.  The Final and the Draft
print keep the exact colours, the print in black and white has only greys.

    STRPRINT=... python3 -I -B tests/picturecolour.py
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
tmp = tempfile.mkdtemp(prefix='pcol')
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



def png_of(path, w, h, rows):
    """A PNG of the stripes: rows is a list of (r, g, b) for each third."""
    import struct, zlib

    raw = b''
    for y in range(h):
        raw += b'\x00' + b''.join(bytes(rows[x * 3 // w]) for x in range(w))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)

    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack(
        '>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) +
        chunk(b'IEND', b''))


STRIPES = [(255, 0, 0), (0, 200, 0), (0, 0, 255)]


def pdoc(name, extra=()):
    d = os.path.join(tmp, name)
    os.makedirs(os.path.join(d, 'pics'), exist_ok=True)
    return d


def make_jpeg(png, jpg):
    """Make a JPEG of the PNG with the tools that the Printer needs anyway:
    pdflatex puts the PNG on a page of its size and pdftoppm draws that page
    as a JPEG (no Python module is needed).  Returns True when the JPEG is
    there; the caller must fail the test when it is not."""
    w = os.path.dirname(png)
    open(os.path.join(w, 'j.tex'), 'w').write(
        '\\documentclass{article}\n'
        '\\usepackage[paperwidth=3in,paperheight=1.3in,margin=0pt]{geometry}\n'
        '\\usepackage{graphicx}\n\\pagestyle{empty}\n'
        '\\begin{document}\\noindent'
        '\\includegraphics[width=3in,height=1in]{%s}\\end{document}\n'
        % os.path.basename(png))
    subprocess.run(['pdflatex', '-interaction=nonstopmode', 'j.tex'], cwd=w,
        capture_output=True)
    subprocess.run(['pdftoppm', '-jpeg', '-jpegopt', 'quality=95', '-r',
        '100', '-singlefile', os.path.join(w, 'j.pdf'),
        os.path.splitext(jpg)[0]], cwd=w, capture_output=True)
    return os.path.exists(jpg) and os.path.getsize(jpg) > 100


def printed(kind, picname, args):
    d = os.path.join(tmp, kind)
    os.makedirs(os.path.join(d, 'pics'), exist_ok=True)
    png_of(os.path.join(d, 'pics', 'p.png'), 300, 100, STRIPES)
    if picname.endswith(b'.jpg'):
        make_jpeg(os.path.join(d, 'pics', 'p.png'),
                  os.path.join(d, 'pics', 'p.jpg'))
    data = (b'*R3' + CR + b'MR1001' + CR + b'PP\\1\\\x00' + CR + b'FHA1' + CR +
            b'DP0,8500,11000' + CR + b'*B0' + CR + b'S' + CR + b'T' +
            b'x\x13' + b'25(' + b'1pics/' + picname + b'\x00' + CR + b'22000' +
            CR + b'\x13)' + b'\x1d\x00' + CR + b'*E' + CR)
    path = os.path.join(d, 'doc.doc')
    open(path, 'wb').write(data)
    pdf = os.path.join(d, 'doc.pdf')
    r = subprocess.run([strprint, path, '-o', pdf, '--no-banner',
        '--no-error-log'] + list(args), capture_output=True, text=True, cwd=d)
    return pdf, r.stderr


def counts(pdf):
    r = subprocess.run(['pdftoppm', '-r', '100', '-f', '1', '-l', '1', pdf],
        capture_output=True)
    parts = r.stdout.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    red = green = blue = grey = coloured = 0
    for i in range(0, len(px) - 2, 3):
        a, b, c = px[i], px[i + 1], px[i + 2]
        if (a, b, c) == (255, 0, 0):
            red += 1
        elif (a, b, c) == (0, 200, 0):
            green += 1
        elif (a, b, c) == (0, 0, 255):
            blue += 1
        if max(a, b, c) - min(a, b, c) > 8:
            coloured += 1
        elif (a, b, c) != (255, 255, 255):
            grey += 1
    return red, green, blue, coloured, grey


for kind, args in (('final', []), ('draft', ['--draft']),
                   ('bw', ['--black-and-white'])):
    pdf, err = printed(kind, b'p.png', args)
    ok = os.path.exists(pdf)
    red, green, blue, coloured, grey = counts(pdf) if ok else (0,) * 5
    if kind == 'bw':
        check('Print in black and white: the picture is there and all grey, '
            'no pixel with colour', ok and grey > 1000 and coloured == 0,
            (ok, grey, coloured, err[-200:]))
        check('black and white: no pixel of the exact red, green or blue',
            red == 0 and green == 0 and blue == 0)
    else:
        check('%s: red, green and blue of the picture are there' % kind,
            ok and red > 1000 and green > 1000 and blue > 1000,
            (ok, red, green, blue, err[-200:]))
        check('%s: the three colours are about the same size' % kind,
            ok and abs(red - blue) < 0.1 * max(red, blue, 1) and
            abs(red - green) < 0.1 * max(red, green, 1), (red, green, blue))

for kind, args in (('jfinal', []), ('jdraft', ['--draft']),
                   ('jbw', ['--black-and-white'])):
    pdf, err = printed(kind, b'p.jpg', args)
    check('%s: a JPEG of the test picture could be made (pdflatex and '
          'pdftoppm)' % kind[1:], os.path.exists(
              os.path.join(tmp, kind, 'pics', 'p.jpg')),
          os.listdir(os.path.join(tmp, kind, 'pics')))
    ok = os.path.exists(pdf)
    red, green, blue, coloured, grey = counts(pdf) if ok else (0,) * 5
    if kind == 'jbw':
        check('JPEG, black and white: all grey', ok and grey > 1000 and
            coloured == 0, (ok, grey, coloured, err[-200:]))
    else:
        check('JPEG %s: the picture has colour' % kind[1:],
            ok and coloured > 10000, (ok, coloured, err[-200:]))

# the Getting Started document of the manual: its colour picture has colour
gs = os.path.join(here, '..', 'manual', 'doc', 'GettingStarted.doc')
if os.path.exists(gs):
    d = os.path.join(tmp, 'gs')
    shutil.copytree(os.path.join(here, '..', 'manual', 'doc', 'pictures'),
                    os.path.join(d, 'pictures'))
    shutil.copy(gs, d)
    for kind, args in (('colour', []), ('grey', ['--black-and-white'])):
        out = os.path.join(d, kind + '.pdf')
        subprocess.run([strprint, os.path.join(d, 'GettingStarted.doc'), '-o', out,
            '--no-banner', '--no-error-log'] + args, capture_output=True,
            cwd=d)
        # the page of the picture "print-colour": find it by its colours
        found = 0
        if os.path.exists(out):
            n = int(re.search(r'Pages:\s+(\d+)', subprocess.run(['pdfinfo',
                out], capture_output=True, text=True).stdout).group(1))
            for pg in range(1, n + 1):
                r = subprocess.run(['pdftoppm', '-r', '50', '-f', str(pg),
                    '-l', str(pg), out], capture_output=True)
                px = r.stdout.split(b'\n', 3)[3]
                red = sum(1 for i in range(0, len(px) - 2, 3)
                    if px[i] > 180 and px[i + 1] < 70 and px[i + 2] < 70)
                found += red
        if kind == 'colour':
            check('Getting Started: the manual prints a picture with colour '
                '(a red area of the colour chart)', found > 200, found)
        else:
            check('Getting Started in black and white: no red area',
                os.path.exists(out) and found == 0, found)

shutil.rmtree(tmp)
print('picturecolour: %s' % ('all passed' if not fails else '%d FAILURES'
                            % fails))
sys.exit(1 if fails else 0)
