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

"""The table of the keys that terminals and desktops keep, which the
Reference Manual includes with the Include marker (manual/src/
terminal-keys.src, the directives .part and .includedoc of manual/
mkmanual.py).

(a) the generator: a part is a document with no structure; .includedoc
writes the Include marker alone in a block; a bad use of the directives is an
error of the generator;
(b) the documents of the tree: the part is next to the Reference Manual and is
what the generator makes of its source; the Reference Manual holds the
marker with the right file name, alone in the last block of its section;
the installed list and the appendix name the part;
(c) the Printer: a master with the marker prints the text of the table, both
as LaTeX and as a PDF, and the same holds for the real Reference Manual; a
master with no part beside it is an error ("Invalid Include request") and
the table is not in the print;
(d) the Editor opens a master with the marker and shows the marker;
(e) the checks above can fail (a marker with another name, a master with no
part beside it).

    STRPRINT=... STRED=... python3 -I -B tests/keypart.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
manual = os.path.join(here, '..', 'manual')
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
if not os.path.exists(strprint) or not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='keypart')
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
PART = 'terminal-keys.doc'
# Words that only the table has: a row of it, not found in the appendix.
ROWS = ('its command key (in', 'xfce4-terminal', 'xterm')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
                       timeout=600, **kw)
    return r.returncode, r.stdout, r.stderr


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def write(path, text):
    with open(path, 'w') as f:
        f.write(text)


def generate(src, out):
    """Run the manual generator; the status, and what it said."""
    rc, o, e = run([sys.executable, '-I', '-B',
                    os.path.join(manual, 'mkmanual.py'), src, out])
    return rc, o + e


def records(data):
    return data.split(b'\r\n')


def marker_blocks(data, name):
    """The places of the Include marker for NAME in the records: a list of
    (index, alone, last): the index of the record that opens it, whether it
    is the only thing in its block, and whether its block is the last of the
    section (the record after the block opens or closes a section)."""
    recs = records(data)
    out = []
    want = b'1' + name.encode() + b'\x00'
    for i in range(len(recs) - 1):
        if recs[i] == b'T\x1316(' + want and recs[i + 1] == b'\x13)\x1d\x00':
            alone = recs[i - 1] in (b'B', b'S') or recs[i - 1][:1] == b'B'
            nxt = recs[i + 2] if i + 2 < len(recs) else b'*E'
            out.append((i, alone, nxt in (b'S', b'+', b'-', b'*E')))
    return out


# ---- (a) the generator ------------------------------------------------------
src_dir = os.path.join(tmp, 'src')
os.makedirs(src_dir)
write(os.path.join(src_dir, 'part.src'),
      '.part\n.table 12 30\nKey | Use\n{k:Ctrl-Z} | Undo.\n.end\n')
write(os.path.join(src_dir, 'master.src'),
      '.h1 The master [m]\nThe part follows.\n.includedoc ' + PART + '\n')
rc, said = generate(os.path.join(src_dir, 'part.src'),
                    os.path.join(src_dir, PART))
check('generator: a source that starts with .part makes a document', rc == 0,
      said)
data = read(os.path.join(src_dir, PART)) if rc == 0 else b''
recs = records(data)
check('generator: a part has no structure (the record *B0, no section '
      'marks +, -)', b'*B0' in recs and b'+' not in recs and b'-' not in recs
      and b'*B1' not in recs, recs[:3])
rc, o, e = run([sys.executable, '-I', '-B',
                os.path.join(manual, 'checktables.py'),
                os.path.join(src_dir, PART)])
check('generator: the table of a part follows the border rule of the '
      'manual', rc == 0 and '1 table(s) follow the rule' in o, o + e)
rc, said = generate(os.path.join(src_dir, 'master.src'),
                    os.path.join(src_dir, 'master.doc'))
mdata = read(os.path.join(src_dir, 'master.doc')) if rc == 0 else b''
where = marker_blocks(mdata, PART)
check('generator: .includedoc writes the Include marker with the file name, '
      'alone in a block that is the last of its section',
      rc == 0 and len(where) == 1 and where[0][1] and where[0][2], where)
for what, text, want in (
        ('an .includedoc with no name', '.h1 A [a]\n.includedoc\n', False),
        ('an .includedoc with a name that has a space',
         '.h1 A [a]\n.includedoc a b\n', False),
        ('a .part after the text', '.h1 A [a]\ntext\n.part\n', False),
        ('a headline in a part', '.part\n.h1 A [a]\n', False),
        ('text before the first headline when it is not a part',
         'text\n', False),
        ('a document with no .part and a headline',
         '.h1 A [a]\ntext\n', True)):
    write(os.path.join(src_dir, 'bad.src'), text)
    rc, said = generate(os.path.join(src_dir, 'bad.src'),
                        os.path.join(src_dir, 'bad.doc'))
    check('generator: ' + what + (' is made' if want else ' is refused'),
          (rc == 0) == want, said)

# ---- (b) the documents of the tree -----------------------------------------
tree_doc = os.path.join(manual, 'doc')
tree_part = os.path.join(tree_doc, PART)
check('tree: the part is a document next to the Reference Manual',
      os.path.exists(tree_part) and os.path.exists(os.path.join(
          tree_doc, 'strata-reference-manual.doc')))
fresh = os.path.join(tmp, 'fresh.doc')
rc, said = generate(os.path.join(manual, 'src', 'terminal-keys.src'), fresh)
check('tree: the part is what the generator makes of manual/src/'
      'terminal-keys.src (build.sh publish makes it so)',
      rc == 0 and os.path.exists(tree_part) and read(fresh) ==
      read(tree_part), said)
rc, said = generate(os.path.join(manual, 'src', 'terminal-keys.src'), fresh)
second = os.path.join(tmp, 'fresh2.doc')
generate(os.path.join(manual, 'src', 'terminal-keys.src'), second)
check('tree: two runs of the generator make the same bytes',
      read(fresh) == read(second))
ref = read(os.path.join(tree_doc, 'strata-reference-manual.doc'))
where = marker_blocks(ref, PART)
check('tree: the Reference Manual holds the Include marker for %s once, '
      'alone in the last block of its section' % PART,
      len(where) == 1 and where[0][1] and where[0][2], where)
check('tree: the part holds the rows of the table',
      os.path.exists(tree_part) and
      all(r.encode() in read(tree_part) for r in ROWS))
check('tree: the Reference Manual does not hold the rows itself (they are '
      'in the part)', not any(r.encode() in ref for r in ROWS[:1]))
appterm = open(os.path.join(manual, 'src', 'appterm.src')).read()
check('tree: the appendix source includes the part with .includedoc',
      '.includedoc ' + PART in appterm)
inst = open(os.path.join(here, '..', '..', 'scripts',
                         'strata-install')).read()
check('tree: the install script installs every document of manual/doc '
      '(so the part is installed beside the Reference Manual)',
      'manual/doc/*.doc' in inst)

# ---- (c) the Printer ---------------------------------------------------------
master_dir = os.path.join(tmp, 'master')
os.makedirs(master_dir)
shutil.copy(os.path.join(src_dir, 'master.doc'),
            os.path.join(master_dir, 'master.doc'))
shutil.copy(tree_part, os.path.join(master_dir, PART))
tex = os.path.join(master_dir, 'm.tex')
rc, o, e = run([strprint, os.path.join(master_dir, 'master.doc'),
                '--tex-only', '-o', tex, '--no-banner'])
text = read(tex).decode('latin-1') if os.path.exists(tex) else ''
check('printer: a master with the marker prints the rows of the table '
      '(LaTeX)', rc == 0 and all(r.replace('&', '\\&') in text
                                 for r in ROWS), (o + e)[:300])
if shutil.which('pdflatex') and shutil.which('pdftotext'):
    pdf = os.path.join(master_dir, 'm.pdf')
    rc, o, e = run([strprint, os.path.join(master_dir, 'master.doc'),
                    '-o', pdf, '--no-banner'])
    rc2, txt, _ = run(['pdftotext', pdf, '-']) if rc == 0 else (1, '', '')
    check('printer: the PDF of the master has the rows of the table',
          rc == 0 and rc2 == 0 and all(r in txt for r in ROWS),
          (o + e)[:300])
else:
    print('SKIP: pdflatex or pdftotext is not installed')
tex = os.path.join(tmp, 'ref.tex')
rc, o, e = run([strprint, os.path.join(tree_doc,
                                       'strata-reference-manual.doc'),
                '--tex-only', '-o', tex, '--no-banner'])
text = read(tex).decode('latin-1') if os.path.exists(tex) else ''
check('printer: the Reference Manual prints the rows of the table from '
      'the part (LaTeX)', rc == 0 and all(
          r.replace('&', '\\&') in text for r in ROWS), (o + e)[:300])
# The same print with no part beside the master.
alone_dir = os.path.join(tmp, 'alone')
os.makedirs(alone_dir)
shutil.copy(os.path.join(src_dir, 'master.doc'),
            os.path.join(alone_dir, 'master.doc'))
tex = os.path.join(alone_dir, 'm.tex')
rc, o, e = run([strprint, os.path.join(alone_dir, 'master.doc'),
                '--tex-only', '-o', tex, '--no-banner'])
text = read(tex).decode('latin-1') if os.path.exists(tex) else ''
check('printer: a master with no part beside it is an error (Invalid '
      'Include request) and the print has a notice and no table',
      rc != 0 and 'Invalid Include request' in o + e and
      'Invalid Include request' in text and
      not any(r in text for r in ROWS), (rc, (o + e)[:300]))

# ---- (d) the Editor ----------------------------------------------------------
d = tui.session([stred, '-H', helpf, os.path.join(master_dir, 'master.doc')],
                ['wait', 'show'], rows=25)
check('editor: a master with the marker opens with no error and shows the '
      'Include marker', '[INCLUDE "%s"]' % PART in d[0] and
      'rror' not in d[0] and 'nvalid' not in d[0], d[0])

# ---- (e) the checks can fail ------------------------------------------------
tampered = ref.replace(b'1' + PART.encode() + b'\x00',
                       b'1other-keys.doc\x00', 1)
check('self-test: a marker with another file name is not the one of the '
      'part', len(marker_blocks(tampered, PART)) == 0)
no_part = ref.replace(b'T\x1316(', b'T\x1317(', 1)
check('self-test: a document with no Include marker is found out',
      len(marker_blocks(no_part, PART)) == 0)

shutil.rmtree(tmp, True)
sys.exit(1 if fails else 0)
