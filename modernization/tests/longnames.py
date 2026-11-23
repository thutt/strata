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

"""Names of files that are not 8.3 names, in all four programs.

Documents are named with 40, 120, 240 and 255 bytes, with spaces, with several
dots, with a leading dot, with letters that are not ASCII (an a with a
diaeresis, Japanese) and with the signs that a shell or TeX reads in their
own way (quote, ampersand, dollar, percent, hash, underscore, parentheses,
brackets, braces, tilde, caret, semicolon).  For each name:

  the Printer makes the PDF, the PDF holds the text and the name that the
  Filename marker prints; an Include of a part and a Picture with such a name
  print; the Editor opens the document and saves it, makes the backup when
  the name leaves room for .bak01 and says so when it does not, and shows
  the end of a long name; the Document Manager lists the name, chooses it
  and lists its backups; the Retrieve panel finds it; the PDF viewer of Setup
  is started with the name as one argument; the Viewer opens a DVI file of
  such a name; a name with the signs of a shell is never given to a shell
  (the name would make a file in the folder of the test if it were).

Every check fails when its feature is turned off; mutations of the programs
for each part were run (docs/LONG-NAMES.txt).

    STRATA_BUILD_DIR=... python3 -I -B tests/longnames.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, re, shutil, signal, struct, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strmgr = os.environ.get('STR', os.path.join(build, 'term', 'str'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
dviview = os.environ.get('DVIVIEW', os.path.join(build, 'term', 'dviview'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
if not (os.path.exists(stred) and os.path.exists(strprint)):
    print('SKIP: build the programs first')
    sys.exit(0)
if not shutil.which('pdflatex') or not shutil.which('pdftotext'):
    print('SKIP: pdflatex or pdftotext is missing')
    sys.exit(0)

fails = 0
# The scratch folder is made where TMPDIR says, and is removed at the end.
tmp = tempfile.mkdtemp(prefix='longnames')
atexit.register(shutil.rmtree, tmp, True)
home = os.path.join(tmp, 'home')
os.makedirs(home, mode=0o700)
os.environ['STRATA_HOME'] = home
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
CRLF = b'\r\n'
PIC = os.path.join(here, 'data', 'pics', 'p.png')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)[:1500]))
    fails += 0 if cond else 1


# ---------------------------------------------------------------- the names
def sized(n, ch='n', ext='.doc'):
    """A name of exactly n bytes that ends in ext."""
    return ch * (n - len(ext)) + ext


LONG = [sized(40, 'a'), sized(120, 'b'), sized(240, 'c'), sized(255, 'd')]
SPECIAL = ['my report final.doc', 'a.b.c version 1.2.doc', '.lead.doc',
           'café äö.doc', '日本語の名.doc',
           "q'uote.doc", 'am&per.doc', 'dol$lar.doc', 'per%cent.doc',
           'ha#sh.doc', 'under_score.doc', 'par(en)thesis.doc',
           'br[ack]et.doc', 'br{ace}.doc', 'til~de.doc', 'car^et.doc',
           'semi;colon.doc', 'UPPER.DOC']
# Names that would make a file in the folder of the test if a shell read them.
HOSTILE = [('x; touch pwned1.doc', 'pwned1.doc'),
           ('y $(touch pwned2).doc', 'pwned2'),
           ('z `touch pwned3`.doc', 'pwned3'),
           ('w & touch pwned4.doc', 'pwned4.doc'),
           ('v | touch pwned5.doc', 'pwned5.doc')]
ALL = LONG + SPECIAL + [h[0] for h in HOSTILE]


def cp437_name(name):
    """The name as the text of a document holds it: the code page of the
    format, one byte for a letter that has a place in it."""
    out = b''
    for ch in name:
        if ord(ch) < 128:
            out += ch.encode()
        else:
            try:
                out += ch.encode('cp437')
            except UnicodeEncodeError:
                out += b'?'
    return out


def shown_name(name):
    """What the Filename marker prints: capitals for the ASCII letters, the
    letters of the code page as they are, a question mark for the rest."""
    return ''.join(c.upper() if ord(c) < 128 else
                   (c if cp437_name(c) != b'?' else '?') for c in name)


# ------------------------------------------------------------- the documents
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'FHA1' + CRLF +
          b'DP0,8500,11000' + CRLF)


def marker(t, recs=()):
    return b'\x13' + str(t).encode() + b'(' + b''.join(
        r + CRLF for r in recs) + b'\x13)'


def flat(blocks):
    """An unstructured document: each item of blocks is the bytes of a
    block."""
    out = HEADER + b'*B0' + CRLF + b'S' + CRLF
    for i, b in enumerate(blocks):
        out += (b'' if i == 0 else b'B' + CRLF) + b'T' + b + b'\x1d\x00' + CRLF
    return out + b'*E' + CRLF


def base_doc(name):
    return flat([b'Zebra alpha text.', b'File: ' + marker(13)])


def newdir(label):
    d = os.path.join(tmp, '%s%d' % (label, len(os.listdir(tmp))))
    os.makedirs(d)
    return d


def put(d, name, data):
    with open(os.path.join(d, name), 'wb') as f:
        f.write(data)
    return os.path.join(d, name)


def run(cmd, cwd=None, env=None, timeout=240):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True,
                       timeout=timeout)
    return (r.returncode, r.stdout.decode('utf-8', 'replace'),
            r.stderr.decode('utf-8', 'replace'))


def pdftext(pdf):
    rc, out, err = run(['pdftotext', pdf, '-'])
    return ''.join(out.split())


def images(pdf):
    rc, out, err = run(['pdfimages', '-list', pdf])
    return max(0, len(out.strip().split('\n')) - 2) if rc == 0 else 0


def stem(name):
    return name[:-4] if name.lower().endswith('.doc') else name


# ------------------------------------------------------------ the Printer
d = newdir('print')
for name in ALL:
    put(d, name, base_doc(name))
    pdf = os.path.join(d, stem(name) + '.pdf')
    rc, out, err = run([strprint, name, '--no-banner'], cwd=d)
    text = pdftext(pdf) if os.path.exists(pdf) else ''
    check('printer: %r (%d bytes) makes %s' % (name[:24], len(name.encode()),
                                               'its PDF'),
          rc == 0 and os.path.exists(pdf), (rc, err[-300:]))
    check('printer: %r the PDF holds the text' % name[:24],
          'Zebraalphatext.' in text, text[:200])
    want = shown_name(name)
    if "'" not in name and '--' not in name and '`' not in name and \
            len(name) < 100:
        check('printer: %r the Filename marker prints the name' % name[:24],
              ''.join(want.split()) in text, (want, text[:200]))
    elif len(name) >= 100:
        # A long word is cut by the margin of the page: its start is there.
        check('printer: %r the Filename marker prints the name' % name[:24],
              ''.join(want.split())[:60] in text, (want[:60], text[:200]))
for name, flag in HOSTILE:
    check('printer: %r is not given to a shell (no %s)' % (name, flag),
          not os.path.exists(os.path.join(d, flag)),
          os.listdir(d))

# The extension handling of the name of the PDF: several dots, no dot, a
# leading dot, a dot at the end.
d = newdir('ext')
for name, pdfname in (('a.b.c.doc', 'a.b.c.pdf'), ('noext', 'noext.pdf'),
                      ('.notes', '.notes.pdf'), ('end.', 'end.pdf'),
                      ('two.dots.txt', 'two.dots.pdf')):
    put(d, name, base_doc(name))
    rc, out, err = run([strprint, name, '--no-banner'], cwd=d)
    check('extension: %r makes %s' % (name, pdfname),
          os.path.exists(os.path.join(d, pdfname)), (rc, err[-200:],
                                                     os.listdir(d)))

# ------------------------------------------------- Include and Picture
d = newdir('parts')
for name in LONG[:3] + SPECIAL + [h[0] for h in HOSTILE]:
    part = flat([b'Quokka part text.'])
    put(d, name, part)
    for how, field in (('as written', cp437_name(name)),
                       ('utf-8', name.encode())):
        if how == 'utf-8' and field == cp437_name(name):
            continue
        if how == 'as written' and b'?' in field and '?' not in name:
            # A letter that is not in the code page of a document cannot be
            # written in the name of a part: the name is UTF-8 only.
            continue
        m = put(d, 'master.doc', flat([b'Master zebra.',
                marker(16, [b'1' + field + b'\x00'])]))
        rc, out, err = run([strprint, 'master.doc', '--no-banner'], cwd=d)
        text = pdftext(os.path.join(d, 'master.pdf')) if os.path.exists(
            os.path.join(d, 'master.pdf')) else ''
        check('include: %r (%s) brings the part' % (name[:24], how),
              'Quokkaparttext.' in text, (rc, err[-200:], text[:200]))
        if os.path.exists(os.path.join(d, 'master.pdf')):
            os.remove(os.path.join(d, 'master.pdf'))
    os.remove(os.path.join(d, name))
for name in [sized(40, 'p', '.png'), sized(120, 'q', '.png'),
             sized(240, 'r', '.png')] + [
        s[:-4] + '.png' for s in SPECIAL if not s.endswith('.DOC')] + [
        'x; touch pwned6.png']:
    shutil.copy(PIC, os.path.join(d, name))
    for how, field in (('as written', cp437_name(name)),
                       ('utf-8', name.encode())):
        if how == 'utf-8' and field == cp437_name(name):
            continue
        if how == 'as written' and b'?' in field and '?' not in name:
            continue
        put(d, 'pic.doc', flat([b'Picture zebra.', marker(25,
            [b'1' + field + b'\x00', b'22000'])]))
        rc, out, err = run([strprint, 'pic.doc', '--no-banner'], cwd=d)
        pdf = os.path.join(d, 'pic.pdf')
        check('picture: %r (%s) prints' % (name[:24], how),
              rc == 0 and os.path.exists(pdf) and images(pdf) >= 1,
              (rc, err[-300:]))
        if os.path.exists(pdf):
            os.remove(pdf)
    os.remove(os.path.join(d, name))
check('picture: no shell read a name (pwned6)',
      not os.path.exists(os.path.join(d, 'pwned6.png')), os.listdir(d))

# A picture in an export that is compiled by hand: the name goes into the TeX
# file so that TeX takes the file (spaces, percent, hash, ...).
d = newdir('export')
for name in ('p q.png', 'am&p.png', 'dol$l.png', 'per%c.png', 'ha#s.png',
             'un_der.png', 'til~d.png', 'car^e.png'):
    shutil.copy(PIC, os.path.join(d, name))
    put(d, 'e.doc', flat([b'Export zebra.', marker(25,
        [b'1' + name.encode() + b'\x00', b'22000'])]))
    for mode in ('keep', 'copy'):
        rc, out, err = run([strprint, 'e.doc', '--no-banner', '--export-latex',
                            'e.tex', '--pictures', mode], cwd=d)
        rc2, out2, err2 = run(['pdflatex', '-interaction=nonstopmode',
                               '-halt-on-error', 'e.tex'], cwd=d)
        pdf = os.path.join(d, 'e.pdf')
        check('export: %r pictures %s: TeX reads the picture' % (name, mode),
              rc == 0 and os.path.exists(pdf) and images(pdf) >= 1,
              (rc, err[-200:], out2[-300:]))
        if os.path.exists(pdf):
            os.remove(pdf)
    os.remove(os.path.join(d, name))
    shutil.rmtree(os.path.join(d, 'e_pictures'), True)



# ------------------------------------------------------------ the Editor
SHADOW = os.path.join(here, 'shadowbin')
ENV = {'STRATA_HOME': home, 'TMPDIR': tmp}


def cells(name):
    """The cells that the screen has for a name: one for a character."""
    return ''.join(c if ord(c) < 128 or cp437_name(c) != b'?' else '?'
                   for c in name)


def tail(name, width):
    c = cells(name)
    return c if len(c) <= width else '...' + c[len(c) - (width - 3):]


def backup_names(d):
    b = os.path.join(d, '.backup')
    return sorted(os.listdir(b)) if os.path.isdir(b) else []


def in_dir(d, fn, *a, **k):
    old = os.getcwd()
    os.chdir(d)
    try:
        return fn(*a, **k)
    finally:
        os.chdir(old)


def edit(d, name, keys, **k):
    return in_dir(d, tui.session, [stred, '-H', helpf, name], keys,
                  env=ENV, **k)


SAVE2 = ['show', 'text:X', 'C-s', 'waitfor:Saved', 'show', 'text:Y', 'C-s',
         'wait', 'waitfor:Saved', 'show', 'C-q', 'wait']
d = newdir('editor')
for name in LONG + SPECIAL + [HOSTILE[0][0], HOSTILE[1][0]]:
    put(d, name, base_doc(name))
    dm = edit(d, name, SAVE2)
    row0 = dm[0].split('\n')[0]
    data = open(os.path.join(d, name), 'rb').read()
    check('editor: %r opens, and saves what was typed' % name[:24],
          b'XY' in data, dm[1][-300:])
    check('editor: %r the status line shows the end of the name' %
          name[:24], tail(name, 14)[-8:] in row0 and
          'UNSTRUCTURED' in row0, row0)
    check('editor: %r the fields after the name keep their place' %
          name[:24], 'Attr:' in row0 and 'Pos:' in row0, row0)
    stem_b = name[:-4] if name.lower().endswith('.doc') else name
    made = [b for b in backup_names(d) if b.startswith(stem_b)]
    if len((stem_b + '.bak01').encode()) <= 255:
        check('editor: %r the backup is made' % name[:24], len(made) >= 1,
              backup_names(d))
        check('editor: %r the message does not refuse the backup' %
              name[:24], 'too long for a backup' not in dm[2], dm[2][-200:])
    else:
        check('editor: %r no backup, and the message shows why' % name[:24],
              made == [] and 'too long for a backup' in dm[2].replace(
                  '\n', ' '), (backup_names(d), dm[2][-300:]))
    shutil.rmtree(os.path.join(d, '.backup'), True)
for name, flag in HOSTILE[:2]:
    check('editor: %r is not given to a shell' % name,
          not os.path.exists(os.path.join(d, flag)), os.listdir(d))

# The largest name that has backups and the smallest that has none.
d = newdir('limit')
ok_name = sized(250, 'k')       # stem 246 + .bak01 = 252
no_name = sized(254, 'm')       # stem 250 + .bak01 = 256
for name, want in ((ok_name, True), (no_name, False)):
    put(d, name, base_doc(name))
    dm = edit(d, name, SAVE2)
    check('limit: a name of %d bytes %s' % (len(name),
          'has backups' if want else 'has none, and the message shows so'),
          (len(backup_names(d)) > 0) == want and
          ('too long for a backup' in dm[2].replace('\n', ' ')) != want,
          (backup_names(d), dm[2][-300:]))
    shutil.rmtree(os.path.join(d, '.backup'), True)

# Save As with a typed name that has an accent (UTF-8 in the file system),
# and with a name that is longer than a file system allows.
d = newdir('saveas')
put(d, 'short.doc', base_doc('short.doc'))
dm = edit(d, 'short.doc', ['F12', 'ENTER', 'ENTER', 'TAB', 'text:caf\u00e9 ',
                           'text:nouveau', 'INS', 'wait', 'show'])
check('save as: a typed accent makes a name of the file system',
      os.path.exists(os.path.join(d, 'caf\u00e9 nouveau.doc')),
      os.listdir(d))
check('save as: the status line shows the name as letters, whole',
      'caf\u00e9 nouveau.doc' in dm[0].split('\n')[0], dm[0][:200])

# The room of the name in the status line is what the line has left: the mode
# stands at the right edge, so on a terminal of 80 columns a name of up to 30
# cells is shown whole, and a longer one by its end.
dr = newdir('statusroom')
for n in (10, 19, 30):
    nm = sized(n, 'r')
    put(dr, nm, base_doc(nm))
    dm = edit(dr, nm, ['show', 'C-q', 'wait'])
    row0 = dm[0].split('\n')[0]
    check('status line: a name of %d cells is shown whole' % n,
          nm in row0 and 'Attr:' in row0 and 'UNSTRUCTURED' in row0, row0)
nm = sized(100, 's')
put(dr, nm, base_doc(nm))
dm = edit(dr, nm, ['show', 'C-q', 'wait'])
row0 = dm[0].split('\n')[0]
check('status line: a name of 100 cells is shown by its end, with dots',
      '...' + nm[-27:] in row0 and nm not in row0 and 'Pos:' in row0, row0)
dm = edit(d, 'short.doc', ['F12', 'ENTER', 'ENTER', 'TAB', 'text:' + 'z' * 300,
                           'INS', 'wait', 'wait', 'show'])
check('save as: a name over 255 bytes is refused with a message',
      not any(len(n) > 255 for n in os.listdir(d)) and
      ('too long' in dm[0] or 'cannot' in dm[0].lower()), dm[0])

# ------------------------------------------- Retrieve with typed names
d = newdir('retrieve')
put(d, 'short.doc', base_doc('short.doc'))
for typed, real in (('a.b.c version 1.2', 'a.b.c version 1.2.doc'),
                    ('noext', 'noext'), ('.lead', '.lead.doc'),
                    ('caf\u00e9 \u00e4\u00f6', 'caf\u00e9 \u00e4\u00f6.doc'),
                    ('my report final', 'my report final.doc'),
                    ('q\'uote;&', 'q\'uote;&.doc')):
    put(d, real, flat([b'Retrieved %s text.' % real.encode('cp437', 'replace')
                       ]))
    dm = edit(d, 'short.doc', ['F12', 'ENTER', 'RIGHT', 'ENTER', 'TAB',
                               'text:' + typed, 'INS', 'wait', 'show'])
    check('retrieve: %r finds %r' % (typed, real),
          'Retrieved' in dm[0] and 'Retrieved ' in dm[0].replace(
              'Retrieved text', ''), dm[0])
    check('retrieve: %r shows the text of the file' % typed,
          'Zebra alpha' not in dm[0].split('\n')[2], dm[0])

# ------------------------------------------------- the Document Manager
d = newdir('manager')
names = LONG[:3] + SPECIAL
for name in names:
    put(d, name, base_doc(name))
dm = in_dir(d, tui.session, [strmgr, '-H', helpf], ['INS', 'F2', 'show'],
            rows=50, env=ENV)
box = dm[0]
for name in names:
    check('manager: the list shows %r' % name[:24], tail(name, 74) in box,
          box)
order = sorted(names, key=lambda n: n.encode())
for name in ('b' * 116 + '.doc', 'caf\u00e9 \u00e4\u00f6.doc',
             '\u65e5\u672c\u8a9e\u306e\u540d.doc', 'my report final.doc',
             'a.b.c version 1.2.doc', '.lead.doc', 'br{ace}.doc'):
    idx = 1 + order.index(name)
    dm = in_dir(d, tui.session, [strmgr, '-H', helpf],
                ['INS', 'F2'] + ['DOWN'] * idx + ['ENTER', 'show', 'INS',
                'waitfor:UNSTRUCTURED', 'show'], rows=50, env=ENV)
    field = [l for l in dm[0].split('\n') if 'Document Name:' in l]
    check('manager: %r is chosen and the field shows its end' % name[:24],
          field and tail(name, 20)[-12:] in field[0], dm[0][:600])
    row0 = dm[1].split('\n')[0]
    check('manager: %r is chosen and the Editor opens that file' % name[:24],
          'UNSTRUCTURED' in row0 and tail(name, 14)[-8:] in row0, row0)

# The Restore Backup list of a long name and of a name with an accent.
for name in (sized(120, 'r'), 'caf\u00e9 \u00e4\u00f6.doc',
             'my report final.doc', sized(250, 'k')):
    d = newdir('restore')
    os.makedirs(os.path.join(d, '.backup'))
    put(d, name, flat([b'New zebra']))
    put(os.path.join(d, '.backup'), stem(name) + '.bak01',
        flat([b'Older zebra']))
    dm = in_dir(d, tui.session, [strmgr, '-H', helpf],
                ['DOWN', 'DOWN', 'DOWN', 'ENTER', 'wait', 'DOWN',
                 'text:' + name, 'INS', 'wait', 'show'], env=ENV)
    check('restore: the list of %r shows its backup' % name[:24],
          'Older zebra' in dm[0] and 'Backups of' in dm[0], dm[0])
    check('restore: the title of the list ends with the name %r' %
          name[:24], tail(name, 40)[-12:] in dm[0], dm[0])

# ------------------------------------------ the PDF viewer of Setup
safebin = tempfile.mkdtemp(prefix='safebin')
fakeopen = os.path.join(safebin, 'fakeopen.sh')
shutil.copy(os.path.join(here, 'fakeopen.sh'), fakeopen)
os.chmod(fakeopen, 0o755)


def cli(args, env, cwd, timeout=120):
    e = dict(os.environ)
    e.update(env)
    e['PATH'] = SHADOW + ':' + e.get('PATH', '')
    r = subprocess.run([stred] + args, cwd=cwd, env=e,
                       capture_output=True, timeout=timeout)
    return r.returncode, r.stdout.decode('utf-8', 'replace'), \
        r.stderr.decode('utf-8', 'replace')


for name in ('my report final.doc', 'caf\u00e9 \u00e4\u00f6.doc',
             '\u65e5\u672c\u8a9e\u306e\u540d.doc', 'per%cent #hash.doc',
             sized(200, 'v'), HOSTILE[0][0], HOSTILE[1][0], HOSTILE[2][0]):
    d = newdir('viewer')
    h = os.path.join(d, 'h')
    os.makedirs(h, mode=0o700)
    with open(os.path.join(h, 'config'), 'w') as f:
        f.write('pdf_viewer=%s\n' % fakeopen)
    os.chmod(os.path.join(h, 'config'), 0o600)
    log = os.path.join(d, 'open.log')
    put(d, name, base_doc(name))
    out = os.path.join(d, 'out ' + stem(name)[:40] + '.pdf')
    rc, so, se = cli(['--print', name, '--printer', 'PDF viewer', '-o', out],
                     {'STRATA_HOME': h, 'DISPLAY': ':99', 'FAKEOPEN_LOG': log,
                      'WAYLAND_DISPLAY': '', 'TMPDIR': tmp}, d)
    got = []
    for _ in range(100):
        got = [l for l in open(log, errors='replace').read().split('\n')
               if l] if os.path.exists(log) else []
        if got:
            break
        time.sleep(0.05)
    line = got[0] if got else ''
    check('pdf viewer: %r the program is started with the PDF as one '
          'argument' % name[:24],
          rc == 0 and line.startswith('fakeopen.sh|' + out + ' size=') and
          line.count('|') == 1 + out.count('|'), (rc, se[-200:], got))
    for flag in ('pwned1.doc', 'pwned2', 'pwned3', 'pwned1.pdf'):
        check('pdf viewer: %r no shell read a name (%s)' % (name[:24], flag),
              not os.path.exists(os.path.join(d, flag)), os.listdir(d))

# The default name of the PDF that the Editor makes (Print, File only): a name
# with several dots, no dot, a leading dot, a dot at the end.
d = newdir('defout')
for name, pdfname in (('a.b.c.doc', 'a.b.c.pdf'), ('noext', 'noext.pdf'),
                      ('.notes', '.notes.pdf'), ('end.', 'end.pdf'),
                      ('two.dots.txt', 'two.dots.pdf'),
                      ('caf\u00e9 \u00e4.doc', 'caf\u00e9 \u00e4.pdf')):
    put(d, name, base_doc(name))
    rc, so, se = cli(['--print', name, '--printer', 'File only'],
                     {'STRATA_HOME': os.path.join(d, 'h'), 'TMPDIR': tmp}, d)
    check('print: the default output name of %r is %s' % (name, pdfname),
          os.path.exists(os.path.join(d, pdfname)) and
          not os.path.exists(os.path.join(d, '.pdf')),
          (rc, so[-200:], se[-200:], os.listdir(d)))

# The name of the output file is longer than a file system allows.
d = newdir('outlong')
put(d, 'a.doc', base_doc('a.doc'))
rc, so, se = cli(['--print', 'a.doc', '-o', 'o' * 252 + '.pdf'], {}, d)
check('print: an output name over 255 bytes is refused with a message',
      rc != 0 and ('255' in se + so or 'too long' in se + so),
      (rc, so[-200:], se[-200:]))

# ------------------------------------------------------------ the Viewer
def net_name(win, env):
    """The property _NET_WM_NAME of a window, which is UTF-8 by its type."""
    out = subprocess.run(['xprop', '-id', win, '_NET_WM_NAME'], env=env,
                         capture_output=True).stdout
    i = out.find(b'= "')
    if b'UTF8_STRING' not in out or i < 0:
        return ''
    raw = out[i + 3:out.rindex(b'"')]
    raw = re.sub(rb'\\([0-7]{3})', lambda m: bytes([int(m.group(1), 8)]), raw)
    return raw.decode('utf-8', 'replace')


def viewer_test():
    if not (shutil.which('Xvfb') and shutil.which('xdotool')) or \
            not os.path.exists(dviview):
        print('SKIP: the Viewer (Xvfb, xdotool or dviview is missing)')
        return
    rfd, wfd = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(wfd), '-screen', '0',
        '800x600x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        pass_fds=(wfd,))
    os.close(wfd)
    num = b''
    while not num.endswith(b'\n'):
        chunk = os.read(rfd, 16)
        if not chunk:
            check('viewer: Xvfb starts', False)
            return
        num += chunk
    os.close(rfd)
    disp = ':' + num.decode().strip()
    env = dict(os.environ, DISPLAY=disp, DVIVIEW_PLAIN='')
    env.pop('DVIVIEW_PLAIN')
    d = newdir('dvi')
    put(d, 'v.doc', base_doc('v.doc'))
    rc, so, se = run([strprint, 'v.doc', '--dvi', '-o', 'v.dvi',
                      '--no-banner'], cwd=d)
    try:
        if not os.path.exists(os.path.join(d, 'v.dvi')):
            check('viewer: a DVI file is made', False, (rc, se[-300:]))
            return
        for name in ('my report.dvi', 'caf\u00e9 \u00e4.dvi',
                     '\u65e5\u672c\u8a9e.dvi', sized(120, 'w', '.dvi'),
                     '-leading.dvi', 'x; touch pwned7.dvi', "q'uote&$%#.dvi"):
            shutil.copy(os.path.join(d, 'v.dvi'), os.path.join(d, name))
            args = [dviview, '--', name] if name.startswith('-') else \
                [dviview, name]
            p = subprocess.Popen(args, cwd=d, env=env,
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL)
            title = ''
            for _ in range(100):
                time.sleep(0.1)
                w = subprocess.run(['xdotool', 'search', '--name', '.'],
                    env=env, capture_output=True, text=True).stdout.split()
                titles = [net_name(x, env) for x in w]
                titles = [t for t in titles if t]
                if name in titles:
                    title = name
                    break
                title = ' / '.join(titles)
            p.terminate()
            p.wait()
            check('viewer: %r opens, with its name as the title' % name[:24],
                  title == name, title)
        check('viewer: no shell read a name (pwned7)',
              not os.path.exists(os.path.join(d, 'pwned7.dvi')),
              os.listdir(d))
    finally:
        xvfb.terminate()
        xvfb.wait()


viewer_test()

print('longnames: %d failures' % fails)
sys.exit(1 if fails else 0)
