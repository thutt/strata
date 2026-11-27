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

"""An ordinary user installs and removes Strata with the install script
alone, as it is delivered.

    python3 -I -B tests/installuser.py

The variable STRATA_BUILD_DIR names the build directory.  The test makes a
release directory from it (the install script and the directory payload,
with no build script, no Makefile and no source), runs the script of that
directory with a private HOME below TMPDIR, as an ordinary user and never
with sudo, from a directory that holds nothing of the project, and checks
that:

  - the install writes only below the prefix in the home directory, and
    nothing else in the scratch area or in the project changes;
  - the uninstall removes what the install made, files of the user in the
    prefix stay, and nothing else changes;
  - the list of the programs that Strata needs works there and installs
    nothing;
  - a missing optional program never stops the install of an ordinary user:
    the install is made, status 0, and a warning is the last block of the
    output, with the packages, what is lost without each and the command of
    an administrator; nothing about the missing programs is printed earlier;
    with every program present, and with --no-prereqs, there is no warning;
  - no message of the script and no line of its text tells a user to run
    the build script;
  - the specification of the file formats is never installed: not from a
    release directory that holds it, and not from the build directory and
    the tree of a developer.

The variable STRATA_INSTALL_SCRIPT names another copy of the script (a
file name in the scripts folder, or an absolute path), which is how the
mutations of the test are tried."""
import sys as _s; _s.dont_write_bytecode = True
import os
import re
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
tree = os.path.dirname(here)
project = tree
script = os.environ.get('STRATA_INSTALL_SCRIPT', 'strata-install')
if not os.path.isabs(script):
    script = os.path.join(project, 'scripts', script)
build = os.environ.get('STRATA_BUILD_DIR', '')

fails = 0


def check(name, ok, detail=''):
    global fails
    if ok:
        print('ok   %s' % name)
    else:
        fails += 1
        print('FAIL %s' % name)
        if detail:
            print(detail)


def run(cmd, env, cwd, timeout=300):
    """Run a command; return (status, output of both streams)."""
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, cwd=cwd, timeout=timeout)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


def snapshot(top):
    """Every file and directory below top with the size of each file."""
    out = {}
    for d, dirs, files in os.walk(top):
        for n in dirs:
            out[os.path.relpath(os.path.join(d, n), top)] = 'dir'
        for n in files:
            p = os.path.join(d, n)
            out[os.path.relpath(p, top)] = (
                'link' if os.path.islink(p) else os.path.getsize(p))
    return out


def project_state():
    """The state of the project folder, as git sees it."""
    r = subprocess.run(['git', '-C', project, 'status', '--porcelain'],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.stdout.decode('utf-8', 'replace')


if os.getuid() == 0:
    print('installuser: skipped: the test must run as an ordinary user')
    sys.exit(0)

if not build or not os.path.isdir(build):
    print('installuser: STRATA_BUILD_DIR must name the build directory')
    sys.exit(2)

tmp = tempfile.mkdtemp(prefix='su.', dir=os.environ.get('TMPDIR') or '/tmp')
home = os.path.join(tmp, 'home')
work = os.path.join(tmp, 'work')
rel = os.path.join(tmp, 'release')
os.makedirs(home)
os.makedirs(work)
os.makedirs(os.path.join(tmp, 'tmp'))
env = {'PATH': os.environ['PATH'], 'HOME': home, 'LANG': 'C',
       'TMPDIR': os.path.join(tmp, 'tmp')}
state_before = project_state()
all_output = []


def go(args, cwd=work, e=None):
    """Run the delivered script with the arguments; keep its output."""
    st, out = run([os.path.join(rel, 'strata-install')] + args,
                  e if e is not None else env, cwd)
    all_output.append(out)
    return st, out


# ---- the release directory ------------------------------------------------
# A release holds every manual and the PDF file of every manual (it is a
# fatal error to make one without), so the release is made from a build
# directory of stand-ins: the programs, the help file and the version of the
# real build, and the PDF files of the manuals as files of a few bytes.
relbuild = os.path.join(tmp, 'release-build')
os.makedirs(os.path.join(relbuild, 'manual', 'pdf'))
for n in ('term', 'x11', 'strata.hlp', 'version'):
    if os.path.exists(os.path.join(build, n)):
        os.symlink(os.path.join(build, n), os.path.join(relbuild, n))
manual_list = run([script, '--list-manuals'], env, work)[1].split('\n')
for n in [ln.split()[1] for ln in manual_list if ln.startswith('pdf ')]:
    with open(os.path.join(relbuild, 'manual', 'pdf', n), 'w') as f:
        f.write('%PDF-1.4\n% a stand-in for a manual\n%%EOF\n')
st, out = run([script, '--build-dir', relbuild, '--make-release', rel], env,
              work)
all_output.append(out)
check('release: the release directory is made from the build directory',
      st == 0 and os.path.isfile(os.path.join(rel, 'strata-install')) and
      os.path.isdir(os.path.join(rel, 'payload')), out)
top = sorted(os.listdir(rel))
check('release: it holds the install script and the payload and nothing '
      'else: no build script, no Makefile, no source (%s)' % top,
      top == ['payload', 'strata-install'], top)
pay = snapshot(os.path.join(rel, 'payload'))
check('release: the payload has the programs, the help file and the '
      'documents with the layout of an install',
      all(n in pay for n in ('bin/stred', 'bin/str', 'bin/strprint',
                             'share/strata/strata.hlp',
                             'share/strata/VERSION')) and
      any(n.startswith('share/strata/doc/') for n in pay), sorted(pay)[:12])
check('release: no manifest and no launcher is in the payload (the script '
      'makes them)', 'share/strata/manifest' not in pay and
      'bin/strata-run' not in pay)
check('release: the specification of the file formats is not in the payload',
      not [n for n in pay if 'strata-file-format' in n],
      [n for n in pay if 'strata-file-format' in n])

# ---- the install as an ordinary user ---------------------------------------
prefix = os.path.join(home, 'usr')
before_home = snapshot(home)
before_tmp = snapshot(tmp)
st, out = go(['--no-prereqs'])
check('install: an ordinary user installs from the release directory, from '
      'a directory with nothing of the project, with no option but '
      '--no-prereqs', st == 0 and os.path.isdir(prefix), out)
after_tmp = snapshot(tmp)
new = sorted(n for n in after_tmp if n not in before_tmp)
outside = [n for n in new if not (n == 'home/usr' or
                                  n.startswith('home/usr/'))]
check('install: everything that the install made is below the prefix in '
      'the home directory (%d new paths)' % len(new),
      len(new) > 20 and not outside, outside[:10])
check('install: the only entry of the home directory is the prefix: no '
      'settings and no dot file is made there', os.listdir(home) == ['usr'],
      os.listdir(home))
check('install: the release directory and the work directory are '
      'unchanged', all(after_tmp.get(n) == v for n, v in before_tmp.items()
                       if n.startswith(('release/', 'work/'))))
check('install: the project folder is unchanged', project_state() ==
      state_before)
# The pictures that the installed manuals name are in the folder next to them,
# so that a print of a manual finds every one.
named = set()
docdir = os.path.join(prefix, 'share', 'strata', 'doc')
for dn in os.listdir(docdir):
    if dn.endswith('.doc'):
        with open(os.path.join(docdir, dn), 'rb') as fd:
            named.update(re.findall(rb'pictures/([A-Za-z0-9._-]+\.png)',
                                    fd.read()))
missing_pics = sorted(n.decode() for n in named if not os.path.isfile(
    os.path.join(docdir, 'pictures', n.decode())))
check('install: every picture that an installed manual names is in '
      'share/strata/doc/pictures (%d named)' % len(named),
      len(named) > 100 and not missing_pics, missing_pics[:10])
check('install: the programs are executable files in the prefix',
      all(os.access(os.path.join(prefix, 'bin', n), os.X_OK)
          for n in ('str', 'stred', 'strprint', 'strata-run')))
manifest_path = os.path.join(prefix, 'share', 'strata', 'manifest')
manifest = open(manifest_path).read() if os.path.exists(manifest_path) \
    else ''
check('install: the manifest names every file of the prefix',
      manifest != '' and all(
          ('f %s\n' % os.path.join(prefix, n)) in manifest
          for n in snapshot(prefix) if os.path.isfile(os.path.join(prefix, n))
          and n != 'share/strata/manifest'), manifest[:200])
check('install: no file of the specification of the file formats is in the '
      'prefix or in the manifest',
      not [n for n in snapshot(prefix) if 'strata-file-format' in n] and
      'strata-file-format' not in manifest)

# ---- the list of the programs that Strata needs ----------------------------
before_all = snapshot(tmp)
st, out = go(['--list-prereqs'])
check('list: the list of the programs that Strata needs works in the '
      'release directory and shows what is present', st == 0 and
      'What Strata needs to run' in out, out[:300])
check('list: it installs nothing', snapshot(tmp) == before_all)
st, out = go(['--check'])
check('check: --check works in the release directory', st in (0, 1) and
      'ok ' in out or 'MISSING' in out, out[:300])

# ---- an optional program that is missing ------------------------------------
BASICS = ('uniq expr od comm fold stat getconf perl bash sh yes cp tail cat '
          'awk mawk gawk ls grep head chmod mktemp install dirname basename '
          'rm rmdir mkdir tr sed readlink sort env ldd tar gzip xz zstd '
          'uname id ln mv touch date true false test sleep wc cut find '
          'xargs printf tee').split()
OPTIONAL = ('pdflatex latex dvipdfmx dvipng pdftoppm gs kpsewhich aspell lp '
            'lpstat curl unzip').split()
FIRST = 'Optional programs are missing:'
mythes = os.path.isdir('/usr/share/mythes')
opt_count = [0]


def machine(present, tex_missing=(), libs_missing=False):
    """A PATH of the basic tools, a stub apt-get and the stubs of the
    optional programs named in present; the other optional programs are not
    in the PATH.  The stub of kpsewhich knows every file except those named
    in tex_missing.  With libs_missing the stub of ldd says that the two
    libraries of the X11 programs are not found.  Returns the environment and
    the home directory."""
    opt_count[0] += 1
    top = os.path.join(tmp, 'opt%d' % opt_count[0])
    bindir = os.path.join(top, 'bin')
    hm = os.path.join(top, 'home')
    os.makedirs(bindir)
    os.makedirs(hm)
    for n in BASICS:
        src = shutil.which(n)
        if src is not None:
            os.symlink(src, os.path.join(bindir, n))
    for n in ['apt-get'] + list(present):
        path = os.path.join(bindir, n)
        if n == 'kpsewhich':
            text = ('#!/bin/sh\nfor f in "$@"; do\ncase "$f" in\n%s) ;;\n'
                    '*) echo "/x/$f" ;;\nesac\ndone\nexit 0\n' %
                    '|'.join(tex_missing if tex_missing else ['none.none']))
        else:
            text = '#!/bin/sh\nexit 0\n'
        with open(path, 'w') as f:
            f.write(text)
        os.chmod(path, 0o755)
    if libs_missing:
        path = os.path.join(bindir, 'ldd')
        os.unlink(path)
        with open(path, 'w') as f:
            f.write('#!/bin/sh\ncase "$1" in\n*-x11)\n'
                    'echo "\tlibXft.so.2 => not found"\n'
                    'echo "\tlibfontconfig.so.1 => not found" ;;\n'
                    '*) echo "\tlibc.so.6 => /lib/libc.so.6" ;;\nesac\n')
        os.chmod(path, 0o755)
    return ({'PATH': bindir, 'HOME': hm, 'LANG': 'C',
             'TMPDIR': os.path.join(tmp, 'tmp')}, hm)


def block_of(out):
    """The lines from the first line of the warning to the end, or None."""
    lines = out.split('\n')
    for i, ln in enumerate(lines):
        if ln == FIRST:
            return lines[:i], lines[i:]
    return lines, None


def only_block_lines(block):
    """True when every line after the first is blank, indented, or one of
    the two fixed sentences."""
    fixed = ('Strata runs without them.  Each feature that needs one '
             'reports', 'that it is missing when the feature is used.',
             'Strata runs without them, except the programs that are named',
             'above as not starting: they start when the package is',
             'installed.  Each feature that needs one of the others reports',
             'An administrator installs them with:',
             'Install the packages listed above with the package manager',
             'of this system.')
    return all(ln == '' or ln.startswith(' ') or ln in fixed
               for ln in block[1:])


env_none, home_none = machine([])
st, out = run([os.path.join(rel, 'strata-install')], env_none, work)
all_output.append(out)
early, blk = block_of(out)
flat = ' '.join(' '.join(blk or []).split())
check('optional: with no optional program present the install of an '
      'ordinary user is made, status 0, into ${HOME}/usr',
      st == 0 and os.path.isfile(os.path.join(home_none, 'usr', 'bin',
                                              'stred')), out[-400:])
check('optional: the warning block is there and is the last output: every '
      'line after its first line belongs to it', blk is not None and
      only_block_lines(blk), blk)
check('optional: the block names each missing package with what is lost '
      'without it and the command of an administrator',
      blk is not None and all(w in flat for w in (
          'texlive-latex-base', 'printing to a PDF file or to a printer',
          'dvipng', 'equations as images in the window version',
          'poppler-utils',
          'pictures, keycaps and LaTeX markers as images in the window version',
          'ghostscript', 'pictures from PDF and EPS files', 'hunspell',
          'hunspell-en-us', 'the spell check', 'cups-client',
          'printing on a printer', 'curl', 'unzip',
          'installing a font (Install Now)',
          'sudo apt-get install --no-install-recommends')), flat[:600])
check('optional: the command line of the administrator holds every package '
      'of the block', blk is not None and all(
          p in flat.split('sudo apt-get install --no-install-recommends')[-1]
          for p in ('texlive-latex-base', 'dvipng', 'poppler-utils',
                    'ghostscript', 'hunspell', 'hunspell-en-us', 'cups-client',
                    'curl', 'unzip')), flat)
check('optional: nothing about the missing programs is printed before the '
      'block: no word missing, no "is not installed", no package, no '
      'command of an administrator', not [
          w for w in ('missing', 'is not installed', 'sudo', 'texlive',
                      'ghostscript', 'hunspell', 'cups-client')
          if w in '\n'.join(early)], '\n'.join(early)[:500])
check('optional: the install lists where it put everything before the '
      'block', 'Installed under' in '\n'.join(early))

env_q, home_q = machine([])
st, out = run([os.path.join(rel, 'strata-install'), '--quiet'], env_q, work)
all_output.append(out)
check('optional: with --quiet the block is still printed and the status is '
      '0', st == 0 and FIRST in out, out[-300:])

# Only ghostscript is missing.
others = [n for n in OPTIONAL if n != 'gs']
env_gs, home_gs = machine(others)
st, out = run([os.path.join(rel, 'strata-install')], env_gs, work)
all_output.append(out)
early, blk = block_of(out)
flat = ' '.join(' '.join(blk or []).split())
check('optional: with only ghostscript missing the block names ghostscript '
      'and the thesaurus when it is missing, and no other package; status '
      '0', st == 0 and blk is not None and 'ghostscript' in flat and
      all(w not in flat for w in ('texlive', 'dvipng', 'poppler-utils',
                                  'hunspell', 'curl', 'unzip',
                                  'cups-client')) and
      ('mythes-en-us' in flat) == (not mythes), flat)

# Only pdftoppm is missing: the window version shows the pictures, keycaps
# and LaTeX markers in their short form, and the block says so and names the
# package (poppler-utils), and dvipng is not blamed for it.
others = [n for n in OPTIONAL if n != 'pdftoppm']
env_pp, home_pp = machine(others)
st, out = run([os.path.join(rel, 'strata-install')], env_pp, work)
all_output.append(out)
early, blk = block_of(out)
flat = ' '.join(' '.join(blk or []).split())
cmd = flat.split('sudo apt-get install --no-install-recommends')[-1]
check('optional: with only pdftoppm missing the block names poppler-utils '
      'with what is lost and the command of the administrator has it, and '
      'no other package but the thesaurus when it is missing; status 0',
      st == 0 and blk is not None and 'poppler-utils' in flat and
      'pictures, keycaps and LaTeX markers as images in the window version'
      in flat and 'poppler-utils' in cmd and
      all(w not in flat for w in ('texlive', 'dvipng', 'ghostscript',
                                  'hunspell', 'curl', 'unzip',
                                  'cups-client')) and
      ('mythes-en-us' in flat) == (not mythes), flat)

# A shared library that the X11 programs need is missing: the install is made
# all the same, status 0, and the block names the libraries and the programs
# that will not start.  A user without root cannot install a library, and the
# terminal version needs none.
env_lib, home_lib = machine(OPTIONAL, libs_missing=True)
st, out = run([os.path.join(rel, 'strata-install')], env_lib, work)
all_output.append(out)
early, blk = block_of(out)
flat = ' '.join(' '.join(blk or []).split())
check('libraries: with libXft and libfontconfig missing the install of an '
      'ordinary user is made, status 0, with no required item',
      st == 0 and 'required item' not in out and os.path.isfile(
          os.path.join(home_lib, 'usr', 'bin', 'str')), out[-500:])
check('libraries: the block names each library once, its package and the '
      'programs that will not start until it is installed',
      blk is not None and flat.count('libxft2') >= 1 and
      flat.count('libfontconfig1') >= 1 and
      'str-x11, stred-x11 will not start until it is installed' in flat,
      flat[:700])
check('libraries: the block says that only the named programs do not '
      'start, and gives the command of an administrator with the packages',
      blk is not None and 'except the programs that are named above as not '
      'starting' in flat and 'libxft2' in flat.split(
          'sudo apt-get install --no-install-recommends')[-1], flat)
check('libraries: the terminal programs are not named as not starting',
      blk is not None and 'str will not start' not in flat and
      'stred will not start' not in flat, flat)

# Every file and font that a print needs is checked: when one is missing the
# block names its package.
TEX_FILES = {'needspace.sty': 'texlive-latex-extra',
             'extarticle.cls': 'texlive-latex-extra',
             'pmboxdraw.sty': 'texlive-latex-extra',
             'ulem.sty': 'texlive-plain-generic',
             'xcolor.sty': 'texlive-latex-recommended',
             'setspace.sty': 'texlive-latex-recommended',
             'pdflscape.sty': 'texlive-latex-recommended',
             'ptmr8t.tfm': 'texlive-fonts-recommended',
             'phvb8t.tfm': 'texlive-fonts-recommended',
             'pplr8t.tfm': 'texlive-fonts-recommended',
             'tgtermes.sty': 'tex-gyre'}
for fname, pkg in sorted(TEX_FILES.items()):
    env_t, home_t = machine(OPTIONAL, tex_missing=[fname])
    st, out = run([os.path.join(rel, 'strata-install')], env_t, work)
    all_output.append(out)
    early, blk = block_of(out)
    flat = ' '.join(' '.join(blk or []).split())
    others = set(TEX_FILES.values()) - {pkg}
    check('tex: with %s missing the block names the package %s and the '
          'status is 0' % (fname, pkg), st == 0 and blk is not None and
          pkg in flat and not any(o in flat for o in others), flat[:400])

# Every optional program is there.
env_all, home_all = machine(OPTIONAL)
st, out = run([os.path.join(rel, 'strata-install')], env_all, work)
all_output.append(out)
if mythes:
    check('optional: with every optional program present there is no '
          'warning, status 0', st == 0 and FIRST not in out and
          os.path.isfile(os.path.join(home_all, 'usr', 'bin', 'stred')),
          out[-300:])
else:
    early, blk = block_of(out)
    flat = ' '.join(' '.join(blk or []).split())
    check('optional: with every program present only the thesaurus is '
          'named', st == 0 and blk is not None and 'mythes-en-us' in flat and
          'texlive' not in flat, flat)

# --no-prereqs looks for nothing and warns of nothing.
env_np, home_np = machine([])
st, out = run([os.path.join(rel, 'strata-install'), '--no-prereqs'], env_np,
              work)
all_output.append(out)
check('optional: with --no-prereqs there is no warning and nothing is said '
      'about the missing programs, status 0', st == 0 and FIRST not in out
      and 'is not installed' not in out and 'missing' not in out, out[-300:])

# --list-prereqs only reports and installs nothing.
env_l, home_l = machine([])
st, out = run([os.path.join(rel, 'strata-install'), '--list-prereqs'], env_l,
              work)
all_output.append(out)
check('optional: --list-prereqs reports what is missing at once, status 0, '
      'and installs nothing', st == 0 and 'texlive-latex-base' in out and
      not os.path.exists(os.path.join(home_l, 'usr')), out[-300:])

# ---- the uninstall ----------------------------------------------------------
mine = os.path.join(prefix, 'share', 'strata', 'mine.txt')
open(mine, 'w').write('the file of the user\n')
other = os.path.join(home, 'other.txt')
open(other, 'w').write('another file of the user\n')
st, out = go(['--uninstall'])
left = [n for n in snapshot(prefix) if os.path.isfile(os.path.join(prefix, n))]
check('uninstall: the uninstall of the release directory removes every file '
      'that the install made and keeps the file of the user in the prefix',
      st == 0 and left == ['share/strata/mine.txt'], (st, left[:8], out))
check('uninstall: a file of the user outside the prefix is untouched',
      open(other).read() == 'another file of the user\n')
os.remove(mine)
for d, dirs, files in os.walk(prefix, topdown=False):
    try:
        os.rmdir(d)
    except OSError:
        pass
check('uninstall: the install and the uninstall leave the home directory as '
      'it was, besides the files of the user', not [
          n for n in snapshot(home) if n not in before_home and
          n != 'other.txt'], [n for n in snapshot(home)
                              if n not in before_home][:6])
st, out = go(['--uninstall'])
check('uninstall: with nothing installed the uninstall stops with a message '
      'and removes nothing', st != 0 and 'nothing to remove' in out, out)

# ---- the words that tell a user what to run --------------------------------
st, out = go(['--payload', os.path.join(tmp, 'nothing-here'),
              '--no-prereqs'])
check('message: a payload directory that is not there is refused',
      st != 0 and 'no such directory' in out, out)
st, out = go(['--no-such-option'])
check('message: an unknown option is refused', st != 0 and
      'unknown option' in out, out)
st, out = go(['--prefix', '/proc/strata-no', '--no-prereqs'])
check('message: a prefix that cannot be written is refused with the way out',
      st != 0 and '--prefix' in out, out)
st, out = go(['--help'])
check('message: the help text is shown', st == 0 and 'usage:' in out, out)
text = open(os.path.join(rel, 'strata-install')).read()
code = [ln for ln in text.split('\n')
        if not ln.lstrip().startswith('#') and 'strata-build' in ln]
check('message: no line of the script that is not a comment names the build '
      'script', not code, code[:3])
said = [o for o in all_output if 'strata-build' in o]
check('message: no message of any run names the build script',
      not said, said[:1])

# ---- the specification of the file formats is refused ----------------------
bad = os.path.join(tmp, 'bad')
shutil.copytree(os.path.join(rel, 'payload'), os.path.join(bad, 'payload'))
shutil.copy(os.path.join(rel, 'strata-install'), bad)
with open(os.path.join(bad, 'payload', 'share', 'strata', 'doc',
                       'strata-file-format.doc'), 'w') as f:
    f.write('x\n')
bad_prefix = os.path.join(tmp, 'bad-prefix')
st, out = run([os.path.join(bad, 'strata-install'), '--no-prereqs',
               '--prefix', bad_prefix], env, work)
all_output.append(out)
check('format: a release directory that holds the specification of the file '
      'formats is refused, and nothing is installed', st != 0 and
      'refused' in out and not os.path.exists(bad_prefix), out)

# ---- the install of a developer leaves the specification out ---------------
fake = os.path.join(tmp, 'fake-build')
os.makedirs(os.path.join(fake, 'manual', 'pdf'))
for n in ('term', 'x11', 'strata.hlp', 'version'):
    if os.path.exists(os.path.join(build, n)):
        os.symlink(os.path.join(build, n), os.path.join(fake, n))
for n in ('alpha.pdf', 'strata-file-format.pdf'):
    open(os.path.join(fake, 'manual', 'pdf', n), 'w').write('%PDF-1.4\n')
tree_doc = os.path.join(tree, 'manual', 'doc', 'strata-file-format.doc')
dev_prefix = os.path.join(tmp, 'dev-prefix')
st, out = run([script, '--build-dir', fake, '--no-prereqs', '--prefix',
               dev_prefix], env, work)
all_output.append(out)
dev = snapshot(dev_prefix) if os.path.isdir(dev_prefix) else {}
check('format: the document is in the tree of the developer, and an install '
      'from the build directory and the tree still leaves it and its PDF '
      'out', os.path.exists(tree_doc) and st == 0 and
      'share/strata/pdf/alpha.pdf' in dev and
      not [n for n in dev if 'strata-file-format' in n],
      [n for n in dev if 'strata-file-format' in n] or out)

# ---- no delivered document sends the reader to the specification -----------
srcdir = os.path.join(tree, 'manual', 'src')
named = []
for n in sorted(os.listdir(srcdir)):
    if (not n.endswith('.src') or n.startswith('strata-file-format') or
            n.startswith('gs-')):
        continue
    for i, ln in enumerate(open(os.path.join(srcdir, n)).read().split('\n')):
        low = ln.lower()
        if ('strata-file-format' in low or 'file formats' in low or
                'appfmt-' in low):
            named.append('%s:%d' % (n, i + 1))
check('format: no source of a delivered chapter, appendix or card names the '
      'specification of the file formats or one of its labels (the sources '
      'of Getting Started are checked by tests/gsdoc.py)', not named, named)

shutil.rmtree(tmp)
print('installuser: %s' % ('all passed' if not fails else
                           '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
