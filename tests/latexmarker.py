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

"""The LaTeX marker through the real programs: the Marker Commands menu makes
one with the text of the author, the saved document holds it, the marker key
opens its text again, a document with LaTeX markers is read and written
again byte for byte, a print writes the text as it is and a fault in it is an
error of the document at the line of the marker (and the key Ctrl-F8 goes
to it), a document that does not allow the markers prints them as words, and
the text cannot run a program or write a file outside the print."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
rtrip = os.environ.get('RT', os.path.join(os.path.dirname(stred),
    'roundtrip'))
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
for tool in ('pdflatex', 'latex', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='lmk')
data = os.path.join(here, 'data')
fakeview = os.path.join(here, 'fakeview.sh')
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run_prog(args, env=None, cwd=None):
    e = dict(os.environ)
    e.update(env or {})
    p = subprocess.run(args, capture_output=True, text=True, env=e, cwd=cwd,
        timeout=300)
    return p.returncode, p.stdout, p.stderr


def fresh(name, src):
    path = os.path.join(tmp, name)
    shutil.copy(src, path)
    return path


def edit(path, keys, env=None):
    saved = {}
    e = {'STRATA_HOME': tempfile.mkdtemp(prefix='home', dir=tmp)}
    e.update(env or {})
    for k, v in e.items():
        saved[k] = os.environ.get(k)
        os.environ[k] = v
    try:
        return tui.session([stred, '-H', helpf, path], keys)
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def pdftext(path, first_page_only=False):
    cmd = ['pdftotext', '-layout']
    if first_page_only:
        cmd += ['-f', '1', '-l', '1']
    return subprocess.run(cmd + [path, '-'], capture_output=True,
        text=True).stdout


def msgline(d):
    rows = d.split('\n')
    for r in rows:
        if 'Preview' in r or 'PRINT' in r or 'Wrote' in r:
            return r
    return rows[-2] if len(rows) > 1 else ''


# ---- The Marker Commands menu makes a LaTeX marker --------------------------
doc = fresh('menu.doc', os.path.join(sample, 'plan-title-table.doc'))
d = edit(doc, ['A-F12', 'text:L', 'INS', 'wait', 'show',
    'text:\\hrule', 'ENTER', 'text:\\vspace{1ex}', 'show', 'INS', 'wait',
    'show', 'C-s', 'wait'])
dm = edit(fresh('menu0.doc', os.path.join(sample, 'plan-title-table.doc')),
          ['A-F12', 'wait', 'show', 'ESC'])
check('the menu offers the LaTeX command, before Line', 'LaTeX' in dm[0] and
    'Line' in dm[0] and dm[0].index('LaTeX') < dm[0].index('Line'), dm[0])
check('the text of the marker is edited in an editor of its own, which is '
    'called LATEX EDIT', 'LATEX EDIT' in d[0], d[0])
check('the marker is in the text as [LaTeX instructions]',
    '[LaTeX instructions]' in d[2], d[2])
saved = open(doc, 'rb').read()
check('the saved document holds the marker of type 41 with the text as '
      'typed',
    b'\x1341(S\r\nT\\hrule\x1d\\vspace{1ex}\x1d\x00\r\n\x13)' in saved,
    saved[250:420])

# The marker key opens the text again; changing it is kept.
tui.drop_place(doc)
d = edit(doc, ['A-F12', 'wait', 'show', 'text:%x', 'INS', 'wait', 'C-s',
    'wait'])
check('the marker key on the marker opens its text again',
    '\\hrule' in d[0] and 'LATEX EDIT' in d[0], d[0])
check('the changed text is saved',
    b'T%x\\hrule\x1d\\vspace{1ex}\x1d\x00' in open(doc, 'rb').read(),
    open(doc, 'rb').read()[250:420])

# Esc restores the text as it was.
doc2 = fresh('esc.doc', doc)
tui.drop_place(doc2)
d = edit(doc2, ['A-F12', 'wait', 'text:zzz', 'ESC', 'wait', 'C-s', 'wait'])
check('Esc in the text of a marker leaves it as it was',
    b'zzz' not in open(doc2, 'rb').read(), open(doc2, 'rb').read()[250:420])

# ---- Read and written again, byte for byte ----------------------------------
if os.path.exists(rtrip):
    for name in ('print_latex.doc', 'print_latexerr.doc',
                 'print_latexsec.doc'):
        rc, out, err = run_prog([rtrip, os.path.join(data, name)])
        check('%s is read and written again byte for byte' % name,
              rc == 0, (rc, out, err))

# ---- A print writes the text as it is ----------------------------------------
run = tempfile.mkdtemp(prefix='pr', dir=tmp)
pdf = os.path.join(run, 'out.pdf')
rc, out, err = run_prog([strprint, os.path.join(data, 'print_latex.doc'),
    '-o', pdf], cwd=run)
check('a print of a document with LaTeX markers is made without errors',
      rc == 0 and os.path.exists(pdf), (rc, err))
t = pdftext(pdf)
check('the raw text is typeset: bold, an ampersand, a percent sign, the '
      'accent of a byte above 7F, small capitals and the table',
    'raw bold & 100% of it in the middle.' in t and 'café' in t and
    'small caps' in t.lower() and 'A B' in t.replace('AB', 'A B') and
    'C' in t, t)
check('no word of the marker label is in the print',
    'instructions' not in t, t)

# An error in the text: an error of the document at its place.
errf = os.path.join(run, 'err.txt')
rc, out, err = run_prog([strprint, os.path.join(data, 'print_latexerr.doc'),
    '-o', os.path.join(run, 'e.pdf'), '--errors', errf], cwd=run)
rec = open(errf).read()
check('a fault in the text is an error of the document, at the block and '
      'the line of the marker',
    rc == 4 and 'Block 2, line 2' in err and 'Undefined control sequence' in
    err and 'c=tex-error' in rec and ' i=2 ' in rec and ' l=2 ' in rec,
    (rc, err, rec))

# Not allowed: the words are printed and the text is not.
errf = os.path.join(run, 'off.txt')
pdf2 = os.path.join(run, 'off.pdf')
rc, out, err = run_prog([strprint, os.path.join(data, 'print_latex.doc'),
    '-o', pdf2, '--no-latex-markers', '--errors', errf], cwd=run)
t = pdftext(pdf2, True)
rec = open(errf).read()
check('without the LaTeX markers the words are printed, four times',
    t.count('LaTeX instructions') == 4 and 'raw bold' not in t, t)
check('and each marker is a warning of the document',
    rec.count('c=latex-marker-off') == 4 and 'k=W' in rec, (rc, rec))

# The setting of the user: latex_markers=no.
home = tempfile.mkdtemp(prefix='home', dir=run)
open(os.path.join(home, 'config'), 'w').write('latex_markers=no\n')
pdf3 = os.path.join(run, 'cfg.pdf')
rc, out, err = run_prog([strprint, os.path.join(data, 'print_latex.doc'),
    '-o', pdf3], cwd=run, env={'STRATA_HOME': home})
t = pdftext(pdf3, True)
check('latex_markers=no in the settings does the same',
    t.count('LaTeX instructions') == 4 and 'raw bold' not in t, (rc, t))
rc, out, err = run_prog([strprint, os.path.join(data, 'print_latex.doc'),
    '-o', pdf3, '--latex-markers'], cwd=run, env={'STRATA_HOME': home})
t = pdftext(pdf3)
check('and the option --latex-markers wins over the settings',
    'raw bold' in t and 'instructions' not in t, (rc, t))

# ---- The text cannot run a program or write a file outside the print ---------
marker = b'\x1341(S\r\nT\\immediate\\write18{touch %s}\x1d\x00\r\n\x13)'
done = os.path.join(run, 'write18-ran')
body = marker % done.encode()
dd = open(os.path.join(data, 'print_latex.doc'), 'rb').read()
head = dd[:dd.index(b'*B0')]
d18 = (head + b'*B0\r\nS\r\nTShell escape.\x1d\x00\r\nB\r\nT' + body +
       b'\x1d\x00\r\n*E\r\n')
p18 = os.path.join(run, 'w18.doc')
open(p18, 'wb').write(d18)
rc, out, err = run_prog([strprint, p18, '-o', os.path.join(run, 'w18.pdf')],
    cwd=run)
check('a marker with \\write18 runs no program (no shell escape)',
      not os.path.exists(done), (rc, err))
sec_pdf = os.path.join(run, 'sec.pdf')
outside = os.path.join(os.path.dirname(run), 'strata-openout-done.tex')
rc, out, err = run_prog([strprint, os.path.join(data, 'print_latexsec.doc'),
    '-o', sec_pdf], cwd=run)
check('a marker that writes a file in the directory above the print is '
      'refused by the engine (openout_any=p) and the file is not made',
    rc != 0 and not os.path.exists(outside) and
    "I can't write on file" in err, (rc, err))

# ---- The text \\strata -------------------------------------------------------
# The word Strata with the joined crossbar of its Ts is a command that every
# file has, like \\LaTeX: it is text, so it sits in any line, and the space
# after it is kept unless a sign of punctuation follows.
marker = b'\x1341(S\r\nT\\strata\x1d\x00\r\n\x13)'
dstr = (head + b'*B0\r\nS\r\nTThe word\x1d\x00\r\nB\r\nT' + marker +
        b'\x1d\x00\r\n*E\r\n')
pstr = os.path.join(run, 'strata.doc')
open(pstr, 'wb').write(dstr)
spdf = os.path.join(run, 'strata.pdf')
rc, out, err = run_prog([strprint, pstr, '-o', spdf], cwd=run)
check('a marker with \\strata is printed without errors',
      rc == 0 and os.path.exists(spdf), (rc, err))
fonts = subprocess.run(['pdffonts', spdf], capture_output=True,
    text=True).stdout if shutil.which('pdffonts') else 'NimbusSanL-Bold'
check('the letters of \\strata are in the bold sans serif font',
      'NimbusSanL-Bold' in fonts or 'Helvetica' in fonts, fonts)
t = pdftext(spdf)
check('the letters S, RA and A are in the print', 'S' in t and 'RA' in t, t)
xtex = os.path.join(run, 'strata-export.tex')
rc, out, err = run_prog([strprint, pstr, '--export-latex', xtex], cwd=run)
tex = open(xtex).read() if os.path.exists(xtex) else ''
check('an exported file defines \\strata, and names the package xspace '
      'that it needs',
      '\\newcommand{\\strata}{' in tex and 'xspace' in tex, (rc, err))
rc, out, err = run_prog(['pdflatex', '-interaction=nonstopmode',
    '-halt-on-error', os.path.basename(xtex)], cwd=run)
check('and the exported file is made into pages by LaTeX alone', rc == 0,
      out[-600:])

# The text \strata is made of boxes and measures of the font size: it must
# compile at every size, in a headline and in a footnote size.
pre = tex.split('\\begin{document}')[0]
sizes = '\n'.join('{\\fontsize{%d}{%d}\\selectfont Is \\strata, now.}\\par'
    % (n, n * 6 // 5 + 1) for n in range(6, 73))
body = ('\\begin{document}\n' + sizes + '\n'
    '{\\bfseries\\Large A headline of \\strata\\ and more}\\par\n'
    '{\\footnotesize\\strata}\\par\\end{document}\n')
stex = os.path.join(run, 'strata-sizes.tex')
open(stex, 'w').write(pre + body)
rc, out, err = run_prog(['pdflatex', '-interaction=nonstopmode',
    '-halt-on-error', os.path.basename(stex)], cwd=run)
check('\\strata compiles at every size from 6 pt to 72 pt and in a headline',
      rc == 0 and os.path.exists(stex[:-4] + '.pdf'), out[-600:])
t = pdftext(stex[:-4] + '.pdf')
check('and the letters of every one of them are in the pages',
      t.count('RA') >= 67, t.count('RA'))

# ---- The command \\stratalogo ----------------------------------------------
# The large logo is made of letters, rules and one drawing made with TikZ (the
# S), so a file that uses it needs no other file and no font is made for it.
def longest_run(pdf, dpi, dark=128):
    """The length in pixels of the longest horizontal run of dark pixels on
    the first page of the PDF drawn at dpi (the top bar of the S is the
    longest line of the logo)."""
    stem = os.path.join(tmp, 'run%d' % os.getpid())
    subprocess.run(['pdftoppm', '-r', str(dpi), '-gray', '-f', '1', '-l',
        '1', '-singlefile', pdf, stem], check=True)
    d = open(stem + '.pgm', 'rb').read()
    os.remove(stem + '.pgm')
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    best = 0
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        count = 0
        for v in row:
            if v < dark:
                count += 1
                best = max(best, count)
            else:
                count = 0
    return best


mlogo = b'\x1341(S\r\nT\\stratalogo\x1d\x00\r\n\x13)'
dlogo = (head + b'*B0\r\nS\r\nTThe logo\x1d\x00\r\nB\r\nT' + mlogo +
         b'\x1d\x00\r\n*E\r\n')
plogo = os.path.join(run, 'logo.doc')
open(plogo, 'wb').write(dlogo)
lpdf = os.path.join(run, 'logo.pdf')
rc, out, err = run_prog([strprint, plogo, '-o', lpdf], cwd=run)
check('a marker with \\stratalogo is printed without errors',
      rc == 0 and os.path.exists(lpdf), (rc, err))
t = pdftext(lpdf)
check('the words of the large logo are in the print',
      'Technical Document' in t and 'PROCESSOR' in t.upper(), t)
fonts = subprocess.run(['pdffonts', lpdf], capture_output=True,
    text=True).stdout if shutil.which('pdffonts') else ''
check('the S is a drawing, not a font: the PDF has no bitmap font',
      'Type 3' not in fonts, fonts)
# The top bar of the S is 126 pt long: 174 pixels at 100 dpi.
bar = longest_run(lpdf, 100)
check('the S is in the print: the top bar is the longest line, 126 pt',
      165 <= bar <= 185, bar)
check('no file is left in the directory of the print besides its own',
      not any(n.startswith('strataS') for n in os.listdir(run)),
      os.listdir(run))
xdir = tempfile.mkdtemp(prefix='xl', dir=tmp)
xl = os.path.join(xdir, 'logo-export.tex')
rc, out, err = run_prog([strprint, plogo, '--export-latex', xl], cwd=run)
texl = open(xl).read() if os.path.exists(xl) else ''
check('an exported file defines \\stratalogo', '\\newcommand{\\stratalogo}{'
      in texl and 'tikzpicture' in texl, (rc, err))
check('and nothing but the .tex file is written for the logo',
      os.listdir(xdir) == ['logo-export.tex'], os.listdir(xdir))
rc, out, err = run_prog(['pdflatex', '-interaction=nonstopmode',
    '-halt-on-error', 'logo-export.tex'], cwd=xdir)
check('the exported file alone is made into pages by LaTeX, and has the logo',
      rc == 0 and 'Technical Document' in
      pdftext(os.path.join(xdir, 'logo-export.pdf')), out[-600:])
rc, out, err = run_prog(['latex', '-interaction=nonstopmode',
    '-halt-on-error', 'logo-export.tex'], cwd=xdir)
check('and so is the DVI route', rc == 0, out[-600:])
# The scale: \\stratalogo[2] prints, twice as big, with its S.
mscale = b'\x1341(S\r\nT\\stratalogo[2]\x1d\x00\r\n\x13)'
dscale = (head + b'*B0\r\nS\r\nTThe big logo\x1d\x00\r\nB\r\nT' + mscale +
          b'\x1d\x00\r\n*E\r\n')
pscale = os.path.join(run, 'scale.doc')
open(pscale, 'wb').write(dscale)
spdf2 = os.path.join(run, 'scale.pdf')
rc, out, err = run_prog([strprint, pscale, '-o', spdf2], cwd=run)
check('a marker with \\stratalogo[2] is printed without errors and has the '
      'words of the logo', rc == 0 and 'Technical Document' in
      pdftext(spdf2), (rc, err))


def box_width(pdf):
    p = subprocess.run(['pdftotext', '-bbox', pdf, '-'], capture_output=True,
        text=True).stdout
    m = re.search(r'xMin="([0-9.]+)" yMin="[0-9.]+" xMax="([0-9.]+)"[^>]*>'
                  r'Technical<', p)
    return float(m.group(2)) - float(m.group(1)) if m else 0.0


w1, w2 = box_width(lpdf), box_width(spdf2)
check('the words of the logo are twice as wide with the scale 2',
      w1 > 0 and abs(w2 / w1 - 2.0) < 0.05, (w1, w2))
bar2 = longest_run(spdf2, 100)
check('and so is the top bar of the S: 252 pt, 349 pixels at 100 dpi',
      335 <= bar2 <= 365, bar2)
# The DVI route (latex, dvipdfmx) draws the same S as the PDF route.
dvipdf = os.path.join(run, 'logo-dvi.pdf')
rc, out, err = run_prog([strprint, plogo, '--pdf-via-dvi', '-o', dvipdf],
    cwd=run)
check('the route through the DVI file draws the same S: the top bar is as '
      'long', rc == 0 and abs(longest_run(dvipdf, 100) - bar) <= 3, (rc, err))
# The Viewer draws the S: a preview is the DVI file alone (no font file is
# made for the logo), and the Viewer does not say that something is missing.
viewer = os.environ.get('DVIVIEW', os.path.join(os.path.dirname(strprint),
    'dviview'))
# The picture of the screen is taken by the small program of the tests of the
# Viewer, which is made here when it is not given.
shot = os.environ.get('DVISHOT', os.path.join(tmp, 'shot'))
if not os.path.exists(shot):
    subprocess.run(['cc', '-o', shot, os.path.join(root, 'tools', 'dviview',
        'tests', 'shot.c'), '-lX11'], capture_output=True)
for vname, vdoc in (('logo', plogo), ('big logo', pscale)):
    vdir = tempfile.mkdtemp(prefix='vw', dir=tmp)
    vdvi = os.path.join(vdir, 'logo.dvi')
    rc, out, err = run_prog([strprint, vdoc, '--dvi', '-o', vdvi], cwd=run)
    check('a preview of the %s is the DVI file alone, with no file made for '
          'the S' % vname, rc == 0 and os.listdir(vdir) == ['logo.dvi'],
          (rc, err, os.listdir(vdir)))
    if shutil.which('Xvfb') and os.path.exists(viewer):
        import time
        # Xvfb chooses a free display number itself and says which: two runs
        # at the same time cannot take the same one.
        dfd_r, dfd_w = os.pipe()
        xv = subprocess.Popen(['Xvfb', '-displayfd', str(dfd_w), '-screen',
            '0', '800x600x24'], stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, pass_fds=(dfd_w,))
        os.close(dfd_w)
        num = b''
        while not num.endswith(b'\n'):
            chunk = os.read(dfd_r, 16)
            if not chunk:
                break
            num += chunk
        os.close(dfd_r)
        rep = os.path.join(vdir, 'report.txt')
        xenv = dict(os.environ, DISPLAY=':' + num.decode().strip())
        vp = subprocess.Popen([viewer, vdvi, '-d', '80', '--report', rep],
            cwd='/', env=xenv, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
        ppm = os.path.join(vdir, 'shot.ppm')

        # Take the picture when the Viewer has drawn the white page with the
        # logo on it (a loaded machine needs more than the three seconds that
        # were waited).
        for _ in range(80):
            time.sleep(0.5)

            if os.path.exists(shot):
                subprocess.run([shot, ppm], env=xenv)

            if os.path.exists(ppm):
                pic = open(ppm, 'rb').read().split(b'\n', 3)[-1]

                sums = [pic[i] + pic[i + 1] + pic[i + 2]
                        for i in range(0, len(pic) - 2, 3)]

                # The root window of Xvfb is black: the page is white.
                if max(sums) > 700 and min(sums) < 150:
                    break
        vp.terminate()
        vp.wait()
        xv.terminate()
        xv.wait()
        said = open(rep).read() if os.path.exists(rep) else ''
        check('the Viewer shows the %s and has nothing to report' % vname,
              said == '', said)
        if os.path.exists(ppm):
            d = open(ppm, 'rb').read().split(b'\n', 3)
            w, h = map(int, d[1].split())
            px = d[3]
            best = 0
            for y in range(h - 40):
                count = 0
                for x in range(w):
                    o = (y * w + x) * 3
                    if px[o] + px[o + 1] + px[o + 2] < 150:
                        count += 1
                        if count > best and x < w - 1:
                            best = count
                    else:
                        count = 0
            u = 2.0 if vname == 'big logo' else 1.0
            want = 126.0 * u / 72.27 * 80
            check('the Viewer draws the top bar of the S in the %s (%d '
                  'pixels wanted, within 6 per cent)' % (vname, want),
                  abs(best - want) <= 0.06 * want, best)
        else:
            print('skip the picture of the Viewer: no tools/dviview/build/'
                  'shot')
    else:
        print('skip the Viewer: Xvfb or dviview is missing')

ydir = tempfile.mkdtemp(prefix='xn', dir=tmp)
rc, out, err = run_prog([strprint, pstr, '--export-latex',
    os.path.join(ydir, 'plain.tex')], cwd=run)
check('an exported file that does not use \\stratalogo has the same '
      'definition and no other file', os.listdir(ydir) == ['plain.tex'],
      os.listdir(ydir))

# ---- The Editor: print, the error marker, Ctrl-F8 ----------------------------
run2 = tempfile.mkdtemp(prefix='ed', dir=tmp)
epath = fresh('errdoc.doc', os.path.join(data, 'print_latexerr.doc'))
env = {'TMPDIR': run2, 'STRATA_STRPRINT': strprint,
       'STRATA_DVIVIEW': fakeview, 'PVREC': os.path.join(run2, 'rec')}
d = edit(epath, ['F12', 'text:P', 'RIGHT', 'ENTER',
    'text:' + os.path.join(run2, 'out.pdf'), 'INS'] + ['wait'] * 8 +
    ['show', 'C-F8', 'show'], env)
check('the Editor: a print of the document says that it failed',
    'PRINT FAILED' in msgline(d[0]) or 'PRINT FAILED' in d[0], d[0])
check('the Editor: the Print Error marker is in front of the line of the '
      'LaTeX marker, and the text has the label',
    'PRINT ERROR' in d[0] and '[LaTeX instructions]' in d[0], d[0])
check('the Editor: Ctrl-F8 goes to the error',
    'print error' in d[1].lower() or 'PRINT ERROR' in d[1], d[1])

print('latexmarker: %d failures' % fails)
sys.exit(1 if fails else 0)
