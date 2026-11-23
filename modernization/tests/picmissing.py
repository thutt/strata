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

"""A Picture marker whose file cannot be used is shown as an error.

Before, such a marker was drawn as its short or long text, exactly like the
marker of a picture that is set to be shown in that form, so nothing told the
person that the file was missing.  Now the Editor draws it dim and says what
is wrong ("file not found", "cannot be read", "not a picture"), in every
display mode but Hidden; a message line shows so when the cursor is on it; and
the Printer prints a red box with the words "Picture file not found: NAME"
where the picture would be, tells the missing files on a line of their own
and puts the places it looked in into the Error Log.  The Editor and the
Printer look for the file by one rule (docs/PICTURES-PATHS.txt): the
directory of the document, then the working directory, then the directory
the program was started in.

The test runs the Editor in a pseudo-terminal and the Printer on documents
made here.  The dim style is read from the bytes the Editor sends: a dim
marker is drawn with SGR 2 (the sequence 0;2;36), a marker that is not dim
without it (0;36)."""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, re, shutil, subprocess, sys, tempfile, threading, time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui

build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
                                                     'strprint'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))

if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)

fails = 0
tmp = tempfile.mkdtemp(prefix='picmissing')
atexit.register(shutil.rmtree, tmp, True)
home = os.path.join(tmp, 'home')
docs = os.path.join(tmp, 'docs')
away = os.path.join(tmp, 'away')
other = os.path.join(tmp, 'other')

for d in (home, docs, away, other):
    os.makedirs(d, mode=0o700)

os.environ['STRATA_HOME'] = home
os.chdir(away)
png = os.path.join(here, 'data', 'picture.png')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def doc(name, pics, mode=None, where=docs):
    """A document of a line, a Picture marker for each name, and a line."""
    head = HEADER
    if mode is not None:
        head += b'OM25,%d' % mode + CRLF
    out = head + b'*B0' + CRLF + b'S' + CRLF
    blocks = [b'Before.' + EOL]
    for n in pics:
        blocks.append(b'\x1325(1' + n.encode() + b'\x00' + CRLF + b'22000' +
                      CRLF + b'\x13)' + EOL)
    blocks.append(b'After.' + EOL)
    first = True
    for b in blocks:
        out += (b'' if first else b'B' + CRLF) + b'T' + b + b'\x00' + CRLF
        first = False
    path = os.path.join(where, name)
    with open(path, 'wb') as f:
        f.write(out + b'*E' + CRLF)
    return path


def edit(path, steps, rows=14, cols=78):
    """Run the Editor on a document; returns the dumps of the steps."""
    return tui.session([stred, '-H', helpf, path], steps, rows=rows,
                       cols=cols, env={'STRATA_HOME': home})


def sgr_before(raw, text):
    """The numbers of the last colour sequence before text, or None."""
    m = None
    for m in re.finditer(rb'((?:\x1b\[[0-9;]*m)+)' + re.escape(text.encode()),
                         raw):
        pass
    if m is None:
        return None
    last = re.findall(rb'\x1b\[([0-9;]*)m', m.group(1))[-1]
    return [int(v or 0) for v in last.split(b';')]


shutil.copy(png, os.path.join(docs, 'ok.png'))
with open(os.path.join(docs, 'bad.png'), 'wb') as f:
    f.write(b'this is not a picture, only text\n')
shutil.copy(png, os.path.join(away, 'cwdonly.png'))
shutil.copy(png, os.path.join(docs, 'MIXED.PNG'))

# ------------------------------------------------------------ the Editor
d_missing = doc('missing.doc', ['gone.png'])
before = open(d_missing, 'rb').read()
dumps = edit(d_missing, ['waitfor:After.', 'show', 'rawbytes', 'DOWN',
                         'show'])
raw = eval(dumps[1])
check('picmissing: a missing file is shown as an error, in brackets, with '
      'the name and the reason',
      '[PIC gone.png: file not found]' in dumps[0], dumps[0])
check('picmissing: the error marker is dim (SGR 2 with the marker colour)',
      sgr_before(raw, '[PIC gone.png') is not None and
      2 in sgr_before(raw, '[PIC gone.png'), sgr_before(raw, '[PIC gone.png'))
check('picmissing: the message line shows so when the cursor is on the '
      'marker',
      'Picture file not found: gone.png' in dumps[2].split('\n')[-1] or
      'Picture file not found: gone.png' in dumps[2], dumps[2])
check('picmissing: looking at the picture does not change the document',
      open(d_missing, 'rb').read() == before)

# A document with a picture that is not there is saved as any other: the
# marker keeps its name and the old version is kept as a backup.
d_save = doc('save.doc', ['gone.png'])
dumps = edit(d_save, ['waitfor:After.', 'text:xyz', 'F12', 'ENTER', 'ENTER',
                      'INS', 'ENTER', 'wait', 'show'], rows=20)
saved = open(d_save, 'rb').read()
check('picmissing: a document with a missing picture is saved, with the '
      'name of the picture kept in the marker and a backup made',
      b'gone.png' in saved and b'xyz' in saved and
      os.path.exists(os.path.join(docs, '.backup', 'save.bak01')) and
      'Saved' in dumps[0], (dumps[0], saved))

d_ok = doc('present.doc', ['ok.png'])
dumps = edit(d_ok, ['waitfor:After.', 'show', 'rawbytes', 'DOWN', 'show'])
raw = eval(dumps[1])
check('picmissing: a picture whose file is there is shown as before, with '
      'no error', '[PIC ok.png]' in dumps[0] and
      'not found' not in dumps[0], dumps[0])
check('picmissing: its marker is not dim', sgr_before(raw, '[PIC ok.png')
      is not None and 2 not in sgr_before(raw, '[PIC ok.png'),
      sgr_before(raw, '[PIC ok.png'))
check('picmissing: no message when the cursor is on a good picture',
      'Picture file' not in dumps[2], dumps[2])

absname = os.path.join(tmp, 'nowhere', 'nope.png')
d_abs = doc('abs.doc', [absname])
dumps = edit(d_abs, ['waitfor:After.', 'show'])
check('picmissing: a missing file with a full path is an error too',
      'file not found]' in dumps[0] and 'nope.png' in dumps[0], dumps[0])

d_bad = doc('notpic.doc', ['bad.png'])
dumps = edit(d_bad, ['waitfor:After.', 'show'])
check('picmissing: a file that is not a picture says so',
      '[PIC bad.png: not a picture]' in dumps[0], dumps[0])

if hasattr(os, 'geteuid') and os.geteuid() != 0:
    secret = os.path.join(docs, 'secret.png')
    shutil.copy(png, secret)
    os.chmod(secret, 0)
    d_sec = doc('secret.doc', ['secret.png'])
    dumps = edit(d_sec, ['waitfor:After.', 'show'])
    os.chmod(secret, 0o600)
    check('picmissing: a file that cannot be read says so',
          '[PIC secret.png: cannot be read]' in dumps[0], dumps[0])
else:
    print('SKIP picmissing: unreadable file (running as root)')

# Every display mode: the error is shown whatever the mode, but Hidden.
for mode, label in ((0, 'Diamond'), (1, 'Name'), (2, 'Short'), (3, 'Long'),
                    (5, 'Inline')):
    p = doc('mode%d.doc' % mode, ['gone.png'], mode)
    dumps = edit(p, ['waitfor:After.', 'show'])
    check('picmissing: mode %s shows the error' % label,
          'gone.png' in dumps[0] and 'file not found' in dumps[0], dumps[0])

p = doc('hidden.doc', ['gone.png'], 4)
dumps = edit(p, ['waitfor:After.', 'show'])
check('picmissing: mode Hidden still shows nothing (the person chose it)',
      'gone.png' not in dumps[0] and 'After.' in dumps[0], dumps[0])

for mode, label in ((2, 'Short'), (3, 'Long')):
    p = doc('okmode%d.doc' % mode, ['ok.png'], mode)
    dumps = edit(p, ['waitfor:After.', 'show'])
    check('picmissing: mode %s of a picture that is there has no error text'
          % label, 'ok.png' in dumps[0] and 'not found' not in dumps[0] and
          'cannot' not in dumps[0], dumps[0])

# One rule of lookup: next to the document, then the directory the program
# was started in (here: away), and any case of the letters.
rel = doc('rules.doc', ['ok.png', 'cwdonly.png', 'mixed.png', 'gone.png'])
dumps = edit(rel, ['waitfor:After.', 'show'], rows=16)
check('picmissing: a name is found next to the document, in the directory '
      'the Editor was started in, and in any case of the letters',
      '[PIC ok.png]' in dumps[0] and '[PIC cwdonly.png]' in dumps[0] and
      '[PIC mixed.png]' in dumps[0] and
      '[PIC gone.png: file not found]' in dumps[0], dumps[0])

# The file is made while the Editor runs: the marker stops being dim a short
# while after the next key.
d_late = doc('late.doc', ['late.png'])
late = os.path.join(docs, 'late.png')
timer = threading.Timer(2.0, shutil.copy, (png, late))
timer.start()
dumps = edit(d_late, ['waitfor:After.', 'show', 'wait', 'wait', 'wait',
                      'wait', 'wait', 'wait', 'wait', 'RIGHT', 'wait',
                      'show'])
timer.join()
check('picmissing: the marker shows the error at first',
      '[PIC late.png: file not found]' in dumps[0], dumps[0])
check('picmissing: after the file is made and a key is pressed the marker '
      'is shown as a good picture', '[PIC late.png]' in dumps[1] and
      'not found' not in dumps[1], dumps[1])

# ------------------------------------------------------------ the Printer
have_tex = shutil.which('pdflatex') is not None and \
    shutil.which('pdftotext') is not None and os.path.exists(strprint)


def run_print(path, extra=(), cwd=away):
    out = os.path.join(tmp, 'out.pdf')
    if os.path.exists(out):
        os.remove(out)
    p = subprocess.run([strprint, path, '-o', out, '--no-banner'] +
                       list(extra), capture_output=True, text=True,
                       cwd=cwd, env=dict(os.environ, TMPDIR=tmp))
    text = ''
    if os.path.exists(out):
        text = subprocess.run(['pdftotext', '-layout', out, '-'],
                              capture_output=True, text=True).stdout
    return p.returncode, p.stderr, text, out


if not have_tex:
    print('SKIP picmissing: the Printer (pdflatex or pdftotext missing)')
else:
    two = doc('two.doc', ['gone.png', 'sub/gone2.png'])
    rc, err, text, pdf = run_print(two)
    check('picmissing: the Printer shows on a line of its own how many '
          'pictures are not found', 'strprint: 2 picture files not found'
          in err, err)
    check('picmissing: the print is made with errors (status 4)', rc == 4 and
          text != '', (rc, err))
    check('picmissing: the Error Log names the file and the places that '
          'were looked in', re.search(
              r'Picture file not found: gone\.png \(looked in [^)]*' +
              re.escape(os.path.realpath(docs)), re.sub(r'\s+', ' ',
                                                          text)) is not None,
          text)
    rc2, err2, text2, pdf2 = run_print(two, ['--workingdir', docs],
                                       cwd=docs)
    flat = re.sub(r'\s+', ' ', text2)
    m = re.search(r'Picture file not found: gone\.png \(looked in ([^)]*)\)',
                  flat)
    check('picmissing: a place that is the document folder, the working '
          'directory and the current directory is named once',
          m is not None and m.group(1).replace('- ', '').count(
              os.path.realpath(docs)) == 1, flat)
    body = text.split('Error Log')[0]
    check('picmissing: the page has a box with the words where the picture '
          'would be (before the Error Log)',
          re.search(r'Picture file not found:\s+gone\.png', body) is not None
          and re.search(r'Picture file not found:\s+sub/gone2\.png',
                        body) is not None, body)
    tex = os.path.join(tmp, 'two.tex')
    subprocess.run([strprint, two, '--tex-only', '-o', tex, '--no-banner'],
                   capture_output=True, cwd=away, env=dict(os.environ,
                                                           TMPDIR=tmp))
    src = open(tex, encoding='utf-8', errors='replace').read() \
        if os.path.exists(tex) else ''
    check('picmissing: the LaTeX has the red box macro for each missing '
          'picture and the macro is defined with a red frame',
          src.count('\\strmissing{Picture file not found: ') == 2 and
          re.search(r'newcommand\{\\strmissing\}.*red', src, re.S)
          is not None, src[:200])

    okdoc = doc('okprint.doc', ['ok.png'])
    rc, err, text, pdf = run_print(okdoc)
    check('picmissing: a picture that is there prints with no error and '
          'no box', rc == 0 and 'not found' not in err and
          'Picture file' not in text, (rc, err))

    # The Printer looks where the Editor looks.
    cw = doc('cwd.doc', ['cwdonly.png'])
    rc, err, text, pdf = run_print(cw)
    check('picmissing: the Printer finds a picture in the directory it was '
          'started in (the Editor does too)', rc == 0 and
          'not found' not in err, (rc, err))
    rc, err, text, pdf = run_print(cw, cwd=other)
    check('picmissing: it does not find that picture from another '
          'directory', rc == 4 and 'picture file not found' in err, (rc, err))
    mx = doc('mixed.doc', ['mixed.png'])
    rc, err, text, pdf = run_print(mx)
    check('picmissing: the Printer matches any case of the letters of a '
          'name (the Editor does too)', rc == 0 and 'not found' not in err,
          (rc, err))
    wd = os.path.join(tmp, 'wd')
    os.makedirs(wd)
    shutil.copy(png, os.path.join(wd, 'inwork.png'))
    ww = doc('work.doc', ['inwork.png'])
    rc, err, text, pdf = run_print(ww, cwd=other)
    check('picmissing: a picture in the working directory is not found '
          'without it', rc == 4 and 'picture file not found' in err,
          (rc, err))
    rc, err, text, pdf = run_print(ww, ['--workingdir', wd], cwd=other)
    check('picmissing: --workingdir names the working directory of the '
          'panels, where it is looked for after the document',
          rc == 0 and 'not found' not in err, (rc, err))
    shutil.copy(png, os.path.join(docs, 'dup.png'))
    with open(os.path.join(wd, 'dup.png'), 'wb') as f:
        f.write(b'not a picture: the working directory is looked at last\n')
    clash = doc('clash.doc', ['dup.png'])
    rc, err, text, pdf = run_print(clash, ['--workingdir', wd])
    check('picmissing: the directory of the document comes before the '
          'working directory (a name that is in both is the document\'s)',
          rc == 0 and 'not found' not in err and 'damaged' not in err and
          'cannot' not in err, (rc, err))

sys.exit(1 if fails else 0)
