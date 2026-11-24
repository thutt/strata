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

"""Export as LaTeX: the Printer option --export-latex and the choice "Export a
LaTeX File" of F12, File, Export in the Editor.  An exported file compiles by
itself, in an empty directory with only its picture folder, to the pages that
Print makes; it has the header comment, the pictures are copied next to it
(or named in place by a setting), the position comments are left out (or kept
by a setting), a document with errors exports and says so, and the file that
the Editor writes is byte for byte the file of the command line.  The check
box Check Validity runs LaTeX on the export and tells its errors; unchecked
no LaTeX is run at all (a stand-in for pdflatex records its calls)."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile, urllib.parse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
data = os.path.join(here, 'data')
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
sample = os.environ.get('DOCSRC', os.path.join(here, 'samples'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint) or not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='xlatex')
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def newdir(name):
    d = os.path.join(tmp, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def run(cmd, cwd=None, env=None):
    e = dict(os.environ)
    e.update(env or {})
    r = subprocess.run(cmd, cwd=cwd, env=e, capture_output=True, text=True,
        errors='replace')
    return r.returncode, r.stdout, r.stderr


def read(path):
    return open(path, 'rb').read() if os.path.exists(path) else b''


def pages(pdf):
    rc, out, err = run(['pdfinfo', pdf])
    m = re.search(r'Pages:\s+(\d+)', out)
    return int(m.group(1)) if m else -1


def words(pdf):
    rc, out, err = run(['pdftotext', pdf, '-'])
    return out.split()


def export_cli(doc, tex, docdir, extra=()):
    return run([strprint, doc, '--export-latex', tex, '--docdir', docdir,
        '--source-name', os.path.basename(doc)] + list(extra))


def compile_alone(tex):
    """Compile the exported file in an empty directory that holds only the
    file and its picture folder.  Returns the PDF or None."""
    d = newdir('compile_' + os.path.basename(tex))
    name = os.path.basename(tex)
    shutil.copy(tex, os.path.join(d, name))
    folder = os.path.splitext(tex)[0] + '_files'
    if os.path.isdir(folder):
        shutil.copytree(folder, os.path.join(d, os.path.basename(folder)))
    for _ in range(2):
        rc, out, err = run(['pdflatex', '-interaction=nonstopmode',
            '-halt-on-error', name], cwd=d)
    pdf = os.path.join(d, os.path.splitext(name)[0] + '.pdf')
    return pdf if rc == 0 and os.path.exists(pdf) else None


def print_pdf(doc, docdir, name):
    out = os.path.join(tmp, name + '.pdf')
    rc, o, e = run([strprint, doc, '-o', out, '--docdir', docdir,
        '--no-banner', '--no-error-log'])
    return rc, out


# ---- The documents of the checks ------------------------------------------
work = newdir('docs')
for f in os.listdir(data):
    if f.endswith('.doc'):
        shutil.copy(os.path.join(data, f), os.path.join(work, f))
shutil.copytree(os.path.join(data, 'pics'), os.path.join(work, 'pics'))

# ---- An export compiles to the pages of a print ----------------------------
names = ['plain_text.doc', 'print_panels.doc', 'equation.doc',
         'equation_inline.doc', 'print_orient.doc', 'print_whitespace.doc',
         'markers.doc', 'keep_notes.doc', 'print_keycap.doc',
         'print_chars.doc']
cns = os.path.join(sample, 'plan-title-table.doc')
if os.path.exists(cns):
    shutil.copy(cns, os.path.join(work, 'plan-title-table.doc'))
    names.append('plan-title-table.doc')
compared = 0
for n in names:
    doc = os.path.join(work, n)
    stem = n[:-4]
    out = newdir('out_' + stem)
    tex = os.path.join(out, stem + '.tex')
    rc, o, e = export_cli(doc, tex, work)
    check('%s: the export is written (status %d)' % (n, rc),
        rc in (0, 4) and os.path.exists(tex), e)
    pdf = compile_alone(tex)
    check('%s: the file compiles by itself, in an empty directory' % n,
        pdf is not None, n)
    prc, ppdf = print_pdf(doc, work, 'print_' + stem)
    if pdf is None or prc != 0:
        # A print with errors has marks on its pages: only the compile counts.
        continue
    compared += 1
    check('%s: the export has the pages of Print (%d)' % (n, pages(ppdf)),
        pages(pdf) == pages(ppdf), (pages(pdf), pages(ppdf)))
    check('%s: the export has the text of Print' % n,
        words(pdf) == words(ppdf), n)
check('at least six documents were compared with Print', compared >= 6,
    compared)

# ---- The header, the folder of the pictures, the settings -------------------
doc = os.path.join(work, 'print_panels.doc')
out = newdir('pics')
tex = os.path.join(out, 'panels.tex')
rc, o, e = export_cli(doc, tex, work)
src = read(tex).decode('latin-1')
check('the header says what made the file, from which document and when',
    src.startswith('% LaTeX file exported by Strata.\n% Source document: '
    'print_panels.doc\n% Exported on: 2023-11-14\n'), src[:120])
check('the header says how to compile it and what it needs',
    '%     pdflatex panels.tex\n' in src and 'pdflscape' in src and
    'amsmath' in src and 'made again each time' in src, src[:1600])
check('every line of the header is a comment', all(
    l.startswith('%') for l in src.split('\\newif')[0].split('\n')[:-1]),
    src[:400])
check('the pictures are copied into the folder next to the file',
    os.path.isfile(os.path.join(out, 'panels_files', 'pic1.png')) and
    'panels_files/pic1.png' in src and work not in src, src[-400:])
check('the file ends with the list of its pictures',
    '% Pictures used by this file' in src and
    '% panels_files/pic1.png = pics/p.png' in src, src[-300:])
check('the position comments are left out by default', '%@P' not in src)
check('the file has no banner, no marks and no Error Log',
    'strmark' not in src and 'print-failed' not in src and
    'Error Log' not in src and '@@strprint' not in src)

out = newdir('keep')
tex = os.path.join(out, 'keep.tex')
rc, o, e = export_cli(doc, tex, work, ['--pictures', 'keep'])
src = read(tex).decode('latin-1')
check('with pictures kept in place the file names the picture where it is',
    rc in (0, 4) and os.path.join(os.path.realpath(work), 'pics', 'p.png') in
    src and not os.path.exists(os.path.join(out, 'keep_files')), src[-300:])

out = newdir('pos')
tex = os.path.join(out, 'pos.tex')
rc, o, e = export_cli(doc, tex, work, ['--position-comments'])
check('the position comments can be kept', '%@P' in read(tex).decode(
    'latin-1'))

out = newdir('nopic')
tex = os.path.join(out, 'n.tex')
rc, o, e = export_cli(os.path.join(work, 'plain_text.doc'), tex, work)
check('a document without pictures makes no folder', rc == 0 and
    os.path.exists(tex) and not os.path.exists(os.path.join(out, 'n_files')),
    os.listdir(out))

# The file is the same each time and replaces the old one whole.
out = newdir('again')
tex = os.path.join(out, 'a.tex')
export_cli(doc, tex, work)
first = read(tex)
open(os.path.join(out, 'a_files', 'pic9.png'), 'wb').write(b'old')
export_cli(doc, tex, work)
check('an export made again is the same file, and an old picture goes',
    read(tex) == first and
    not os.path.exists(os.path.join(out, 'a_files', 'pic9.png')))

# ---- PostScript pictures ---------------------------------------------------
CRLF = b'\r\n'
EOL = b'\x1d'
hdr = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF + b'P1C33' +
       CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF)
pic = (b'\x1325(' + b'1pics/box.eps\x00' + CRLF + b'22000' + CRLF + b'\x13)')
body = hdr + b'*B0' + CRLF + b'S' + CRLF + b'TA PostScript picture: ' + pic + \
    EOL + b'\x00' + CRLF + b'*E' + CRLF
epsdoc = os.path.join(work, 'eps.doc')
open(epsdoc, 'wb').write(body)
open(os.path.join(work, 'pics', 'box.eps'), 'wb').write(
    b'%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n'
    b'0.2 0.4 0.8 setrgbcolor 10 10 80 80 rectfill\nshowpage\n')
if shutil.which('gs') is not None:
    out = newdir('eps')
    tex = os.path.join(out, 'eps.tex')
    rc, o, e = export_cli(epsdoc, tex, work)
    names_made = sorted(os.listdir(os.path.join(out, 'eps_files')))
    check('a PostScript picture is copied and made into a PDF next to it',
        rc == 0 and names_made == ['pic1.eps', 'pic1.pdf'], (rc, e,
        names_made))
    check('the file with a PostScript picture compiles by itself',
        compile_alone(tex) is not None)

# ---- A document with errors -------------------------------------------------
out = newdir('errs')
tex = os.path.join(out, 'e.tex')
errs_file = os.path.join(out, 'errors.txt')
rc, o, e = export_cli(os.path.join(work, 'print_errors.doc'), tex, work,
    ['--errors', errs_file])
src = read(tex).decode('latin-1')
check('a document with errors is exported, with status 4 and the messages',
    rc == 4 and os.path.exists(tex) and 'strprint: ' in e and
    'PRINT FAILED' not in e and re.search(r'\d+ errors?, \d+ warning', e),
    (rc, e))
check('the export of a document with errors has no banner, no marks and no '
    'Error Log', 'PRINT FAILED' not in src and 'strmark' not in src and
    'Error Log' not in src and 'INACCURATE' not in src)
pdf = compile_alone(tex)
check('the file of a document with errors compiles', pdf is not None)
check('its pages carry no banner and no Error Log', pdf is not None and
    'PRINT FAILED' not in ' '.join(words(pdf)) and
    'Error Log' not in ' '.join(words(pdf)))
check('the diagnostics are written for the Editor',
    os.path.getsize(errs_file) > 0 if os.path.exists(errs_file) else False)

# ---- A failure: the reason is in the error file ----------------------------
out = newdir('fail')
errs_file = os.path.join(out, 'errors.txt')
rc, o, e = export_cli(os.path.join(work, 'plain_text.doc'),
    os.path.join(out, 'nodir', 'f.tex'), work, ['--errors', errs_file])
check('an export that cannot be written ends with status 1', rc == 1 and
    not os.path.exists(os.path.join(out, 'nodir')), (rc, e))
why = urllib.parse.unquote(read(errs_file).decode('latin-1'))
check('the reason is a diagnostic in the error file, not a line on the '
    'console', 'c=export-failed' in why and 'Cannot make the folder' in why
    and 'Cannot make the folder' not in o + e, (why, o, e))

# ---- The name: --tex-only is the same export -------------------------------
out = newdir('texonly')
shutil.copy(os.path.join(work, 'plain_text.doc'), os.path.join(out, 'k.doc'))
rc, o, e = run([strprint, os.path.join(out, 'k.doc'), '--tex-only', '-o',
    os.path.join(out, 'k.tex')])
rc2, o2, e2 = run([strprint, os.path.join(out, 'k.doc'), '--tex-only', '-o',
    os.path.join(out, 'm.pdf')])
check('--tex-only with -o k.tex writes k.tex and not k.tex.tex',
    rc == 0 and os.path.exists(os.path.join(out, 'k.tex')) and
    not os.path.exists(os.path.join(out, 'k.tex.tex')), os.listdir(out))
check('--tex-only with -o m.pdf writes m.tex',
    rc2 == 0 and os.path.exists(os.path.join(out, 'm.tex')), os.listdir(out))
rc3, o3, e3 = run([strprint, os.path.join(out, 'k.doc'), '--export-latex',
    os.path.join(out, 'z.tex'), '--pictures', 'sideways'])
check('a wrong value of --pictures is a usage error', rc3 == 2 and
    'usage' in e3, (rc3, e3))

# ---- The Editor: F12, File, Export, Export a LaTeX File --------------------
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
FILE = ['F12', 'ENTER']
PANEL = FILE + ['RIGHT'] * 5 + ['ENTER', 'wait']


def session(doc, keys, home=None):
    os.environ['STRATA_HOME'] = home or tempfile.mkdtemp(prefix='home',
        dir=tmp)
    args = [stred]
    if os.path.exists(helpf):
        args += ['-H', helpf]
    return tui.session(args + [doc], keys)


def editor_export(doc, tex, scope=False, home=None, extra=()):
    keys = PANEL + ['show', 'DOWN', 'text:' + tex, 'DOWN', 'RIGHT',
        'RIGHT', 'show']
    if scope:
        keys += ['DOWN', 'DOWN', 'RIGHT']
    keys += ['INS', 'wait'] + list(extra) + ['show']
    return session(doc, keys, home)


def lines(d, word):
    return [l for l in d.split('\n') if word in l]


edoc = newdir('editor')
for f in ('print_panels.doc', 'plain_text.doc', 'print_errors.doc'):
    shutil.copy(os.path.join(work, f), os.path.join(edoc, f))
shutil.copytree(os.path.join(work, 'pics'), os.path.join(edoc, 'pics'))
doc = os.path.join(edoc, 'print_panels.doc')
tex = os.path.join(edoc, 'panels.tex')
d = editor_export(doc, tex)
check('the Export panel offers Export a LaTeX File',
    any('Export a Screen Image' in l for l in d[0].split('\n')) and
    any('LaTeX' in l for l in d[1].split('\n')), d[1])
check('the Editor writes the file and says so',
    os.path.exists(tex) and lines(d[2], 'Exported'), d[2])
cli = newdir('cli_same')
ctex = os.path.join(cli, 'panels.tex')
export_cli(doc, ctex, edoc)
folder_same = os.path.isfile(os.path.join(edoc, 'panels_files', 'pic1.png'))
check('the file of the Editor is byte for byte the file of the command line',
    read(tex) == read(ctex) and len(read(tex)) > 1000, len(read(tex)))
check('the Editor copies the pictures next to the file', folder_same)
check('the file of the Editor compiles by itself',
    compile_alone(tex) is not None)

# The file exists: a question is shown before it replaces it.
before = read(tex)
os.utime(tex, (1000000000, 1000000000))
d = editor_export(doc, tex, extra=['show', 'text:n', 'wait'])
check('an existing file is not replaced without a question',
    lines(d[2], 'Overwrite old version on disk?') and
    os.stat(tex).st_mtime == 1000000000 and read(tex) == before, d[2])
d = editor_export(doc, tex, extra=['text:y', 'wait'])
check('after an answer of yes the file is written again',
    os.stat(tex).st_mtime != 1000000000 and read(tex) == before)

# The name without an extension gets .tex.
bare = os.path.join(edoc, 'bare')
editor_export(os.path.join(edoc, 'plain_text.doc'), bare)
check('a name without an extension gets .tex', os.path.exists(bare + '.tex')
    and not os.path.exists(bare))

# A document with errors: exported, and the message shows how many.
etex = os.path.join(edoc, 'errors.tex')
d = editor_export(os.path.join(edoc, 'print_errors.doc'), etex,
    extra=['wait'])
shown = '\n'.join(d[2:])
flat = ' '.join(l.split('\u2551')[1] for l in shown.split('\n')
    if l.count('\u2551') >= 2)
flat = re.sub(r'\s+', ' ', flat)
check('a document with errors is exported and the Editor shows so',
    os.path.exists(etex) and re.search(r'Exported .* with \d+ errors? and '
    r'\d+ warnings?', flat), shown)
check('the Editor reports the diagnostics by message',
    'picture' in shown.lower() or 'equation' in shown.lower(), shown)
# The settings of the user: pictures in place, position comments kept.
home = tempfile.mkdtemp(prefix='home', dir=tmp)
os.chmod(home, 0o700)
cfg = os.path.join(home, 'config')
open(cfg, 'w').write('latex_pictures=keep\nlatex_positions=yes\n')
os.chmod(cfg, 0o600)
ktex = os.path.join(edoc, 'kept.tex')
editor_export(doc, ktex, home=home)
ksrc = read(ktex).decode('latin-1')
check('the setting latex_pictures=keep names the picture in place',
    os.path.join(os.path.realpath(edoc), 'pics', 'p.png') in ksrc and
    not os.path.exists(os.path.join(edoc, 'kept_files')), ksrc[-300:])
check('the setting latex_positions=yes keeps the position comments',
    '%@P' in ksrc)

# A structured document: the scope of the current section.
if os.path.exists(cns):
    sdoc = os.path.join(edoc, 'plan-title-table.doc')
    shutil.copy(cns, sdoc)
    whole = os.path.join(edoc, 'whole.tex')
    part = os.path.join(edoc, 'part.tex')
    editor_export(sdoc, whole)
    editor_export(sdoc, part, scope=True)
    check('Export Current Section writes less than Export Entire Document',
        0 < len(read(part)) < len(read(whole)), (len(read(part)),
        len(read(whole))))
    check('the file of the section compiles', compile_alone(part) is not None)

# ---- Check Validity ---------------------------------------------------------
# A stand-in for pdflatex that records its calls, may sleep and may fail, to
# prove what is run and when; and a directory with no pdflatex at all.
fake = newdir('fakebin')
fakelog = os.path.join(tmp, 'fake.log')
open(os.path.join(fake, 'pdflatex'), 'w').write(
    '#!/bin/bash\n'
    'echo "called $*" >> "${FAKE_LOG}"\n'
    'if [ -n "${FAKE_SLEEP:-}" ]; then sleep "${FAKE_SLEEP}"; fi\n'
    'exit "${FAKE_RC:-0}"\n')
os.chmod(os.path.join(fake, 'pdflatex'), 0o755)
nobin = newdir('nobin')
fakeenv = {'PATH': fake + os.pathsep + os.environ['PATH'],
           'STRATA_PROGRAM_DIRS': fake + os.pathsep + os.environ['PATH'],
           'FAKE_LOG': fakelog}
noenv = {'PATH': nobin, 'STRATA_PROGRAM_DIRS': nobin}


def calls():
    return read(fakelog).decode().count('called ') if os.path.exists(
        fakelog) else 0


def tree(d):
    out = {}
    for root, dirs, files in os.walk(d):
        for f in files:
            out[os.path.relpath(os.path.join(root, f), d)] = read(
                os.path.join(root, f))
    return out


plain = os.path.join(work, 'plain_text.doc')
out = newdir('chk_off')
rc, o, e = run([strprint, plain, '--export-latex', os.path.join(out, 'p.tex'),
    '--docdir', work], env=fakeenv)
check('unchecked: no LaTeX is run and the Printer shows nothing of a check',
    rc == 0 and calls() == 0 and 'check' not in o, (rc, calls(), o, e))
out2 = newdir('chk_on')
rc, o, e = run([strprint, plain, '--export-latex', os.path.join(out2, 'p.tex'),
    '--docdir', work, '--check-latex'], env=fakeenv)
check('checked: LaTeX is run once or more and the Printer shows it is ok',
    rc == 0 and calls() >= 1 and '@@strprint check started' in o and
    '@@strprint check ok' in o, (rc, calls(), o, e))
check('the file of a checked export is the file of an unchecked one',
    tree(out) == tree(out2) and len(tree(out)) == 1, (sorted(tree(out)),
    sorted(tree(out2))))

pdoc = os.path.join(work, 'print_panels.doc')
off = newdir('real_off')
on = newdir('real_on')
export_cli(pdoc, os.path.join(off, 'p.tex'), work)
rc, o, e = export_cli(pdoc, os.path.join(on, 'p.tex'), work,
    ['--check-latex'])
check('a document with pictures: LaTeX accepts the export', rc == 0 and
    '@@strprint check ok' in o, (rc, o, e))
check('the files and the pictures of a checked export are those of an '
    'unchecked export', tree(off) == tree(on) and len(tree(on)) >= 2,
    (sorted(tree(off)), sorted(tree(on))))
check('the check leaves nothing behind in the directory', sorted(os.listdir(
    on)) == ['p.tex', 'p_files'], os.listdir(on))

out = newdir('chk_miss')
rc, o, e = run([strprint, plain, '--export-latex', os.path.join(out, 'p.tex'),
    '--docdir', work, '--check-latex'], env=noenv)
check('LaTeX that is not installed: the file is written, the check is '
    'skipped and says why', rc == 0 and os.path.exists(
    os.path.join(out, 'p.tex')) and '@@strprint check skipped pdflatex did '
    'not run (is it installed?' in o, (rc, o, e))

out = newdir('chk_stop')
errs_file = os.path.join(out, 'errors.txt')
rc, o, e = run([strprint, plain, '--export-latex', os.path.join(out, 'p.tex'),
    '--docdir', work, '--check-latex', '--errors', errs_file],
    env=dict(fakeenv, FAKE_RC='1'))
why = urllib.parse.unquote(read(errs_file).decode('latin-1'))
check('an engine that stops without a message is an error of the check',
    rc == 4 and 'c=check-stopped' in why and
    '@@strprint check errors 1' in o and os.path.exists(
    os.path.join(out, 'p.tex')), (rc, why, o))

bad = os.path.join(work, 'print_badpic.doc')
out = newdir('bad_off')
errs_file = os.path.join(out, 'errors.txt')
rc, o, e = export_cli(bad, os.path.join(out, 'b.tex'), work,
    ['--errors', errs_file])
why = urllib.parse.unquote(read(errs_file).decode('latin-1'))
check('a picture that TeX cannot read is not found without the check',
    rc == 0 and 'picture-unreadable' not in why, (rc, why))
out = newdir('bad_on')
errs_file = os.path.join(out, 'errors.txt')
rc, o, e = export_cli(bad, os.path.join(out, 'b.tex'), work,
    ['--check-latex', '--errors', errs_file])
why = urllib.parse.unquote(read(errs_file).decode('latin-1'))
check('with the check the unreadable picture is an error, with its place',
    rc == 4 and 'c=picture-unreadable' in why and 'pic1.pdf' in why and
    '@@strprint check errors 1' in o and 'Block 2' in e, (rc, why, o, e))
check('the file is kept when the check finds errors', os.path.exists(
    os.path.join(out, 'b.tex')) and os.path.isdir(os.path.join(out,
    'b_files')))

out = newdir('errs_on')
errs_file = os.path.join(out, 'errors.txt')
rc, o, e = export_cli(os.path.join(work, 'print_errors.doc'),
    os.path.join(out, 'e.tex'), work, ['--check-latex', '--errors', errs_file])
why = urllib.parse.unquote(read(errs_file).decode('latin-1'))
check('a bad equation is reported and the file that LaTeX gets is accepted',
    rc == 4 and 'c=equation-refused' in why and
    '@@strprint check ok' in o, (rc, why, o))

# The Editor: the box, the waiting box, Esc, the messages.
def with_env(env, fn):
    saved = {k: os.environ.get(k) for k in env}
    os.environ.update(env)
    try:
        return fn()
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


CHECKBOX = ['DOWN', 'DOWN', 'RIGHT']


def editor_check(doc, tex, check_on=True, tail=None, env=None, shows=1):
    keys = PANEL + ['DOWN', 'text:' + tex, 'DOWN', 'RIGHT', 'RIGHT']
    if check_on:
        keys += ['show'] + CHECKBOX + ['show']
    keys += ['INS'] + (tail if tail is not None else ['wait'] * 14)
    keys += ['show']
    return with_env(env or {}, lambda: session(doc, keys))


def flat_of(screen):
    return re.sub(r'\s+', ' ', ' '.join(
        l.split('║')[1] for l in screen.split('\n')
        if l.count('║') >= 2))


cdoc = newdir('editor_check')
for f in ('print_panels.doc', 'plain_text.doc', 'print_badpic.doc'):
    shutil.copy(os.path.join(work, f), os.path.join(cdoc, f))
shutil.copytree(os.path.join(work, 'pics'), os.path.join(cdoc, 'pics'))

d = editor_export(os.path.join(cdoc, 'plain_text.doc'),
    os.path.join(cdoc, 'a.tex'))
check('the box Check Validity is active for the LaTeX file only: with the '
    'other options it is in its place, dim, with no brackets',
    'Check Validity' in d[0] and re.search(r'Check Validity\s*:\s*\[',
    d[0]) is None and re.search(r'Check Validity\s*:\s*\[', d[1]) is not
    None, (d[0], d[1]))
check('the box is not checked at first', re.search(r'Check Validity\s*:\s*'
    r'\[No\]', d[1]) is not None, d[1])

if os.path.exists(fakelog):
    os.unlink(fakelog)
os.makedirs(os.path.join(cdoc, 'off'))
os.makedirs(os.path.join(cdoc, 'on'))
t1 = os.path.join(cdoc, 'off', 'x.tex')
d = with_env(fakeenv, lambda: editor_export(os.path.join(cdoc,
    'plain_text.doc'), t1))
check('the Editor, box not checked: the file is written and no LaTeX is run',
    os.path.exists(t1) and calls() == 0 and 'LaTeX accepts' not in d[2],
    (calls(), d[2]))

t2 = os.path.join(cdoc, 'on', 'x.tex')
d = editor_check(os.path.join(cdoc, 'plain_text.doc'), t2, env=fakeenv)
check('the Editor, box checked: the box shows Yes', re.search(
    r'Check Validity\s*:\s*\[Yes\]', d[1]) is not None, d[1])
check('the Editor, box checked: LaTeX is run and accepts the file',
    calls() >= 1 and re.search(r'Exported .*x\.tex, LaTeX accepts it',
    flat_of(d[2]) + ' ' + d[2]), (calls(), d[2]))
check('the Editor: the file of a checked export is the unchecked one',
    read(t1) == read(t2) and len(read(t1)) > 500,
    (len(read(t1)), len(read(t2))))

t3 = os.path.join(cdoc, 'real.tex')
d = editor_check(os.path.join(cdoc, 'print_panels.doc'), t3)
check('the Editor, real LaTeX: the file with pictures is accepted',
    os.path.exists(t3) and re.search(r'Exported .*real\.tex, LaTeX accepts '
    r'it', d[2]), d[2])

t4 = os.path.join(cdoc, 'badpic.tex')
d = editor_check(os.path.join(cdoc, 'print_badpic.doc'), t4)
shown = flat_of(d[2]) + ' ' + d[2]
check('the Editor reports the picture that LaTeX cannot read, with its place',
    os.path.exists(t4) and 'LaTeX cannot read the picture' in shown and
    re.search(r'Exported .* with 1 error', shown), shown)

t5 = os.path.join(cdoc, 'badpic_off.tex')
d = editor_export(os.path.join(cdoc, 'print_badpic.doc'), t5)
check('the Editor without the check does not find it',
    os.path.exists(t5) and 'cannot read' not in d[2] and
    'LaTeX accepts' not in d[2], d[2])

t6 = os.path.join(cdoc, 'miss.tex')
d = editor_check(os.path.join(cdoc, 'plain_text.doc'), t6, env=noenv)
shown = flat_of(d[2]) + ' ' + d[2]
check('the Editor: LaTeX that is not installed is told, the export is made',
    os.path.exists(t6) and 'not checked' in shown and
    'pdflatex did not run' in shown, shown)

t7 = os.path.join(cdoc, 'stop.tex')
d = editor_check(os.path.join(cdoc, 'plain_text.doc'), t7,
    tail=['wait', 'show', 'ESC', 'wait', 'wait', 'show'],
    env=dict(fakeenv, FAKE_SLEEP='3'))
check('the Editor shows a waiting box while LaTeX runs',
    'Checking the file with LaTeX' in d[2] and 'Esc stops the check' in
    d[2], d[2])
check('Esc stops the check, says so, and the file stays',
    os.path.exists(t7) and 'the check was stopped' in d[3], d[3])

print('xlatex: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
