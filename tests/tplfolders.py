#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The templates in folders (docs/TEMPLATES.txt, worklist item W92).

    python3 -I -B tests/tplfolders.py

(a) Every template below templates/, at any depth, is found by its bare name
    (the name of the file without .doc), by its path name below templates
    (labels/avery/us/label-us-5160), by either of them in capital letters
    and with the extension.  The old flat names, the 119 names of the time
    before the folders, find the same files.
(b) A subdirectory of a person's own, with a template of a bare name that is also
    shipped, is found first, by the bare name and by the path name; the other
    templates are still the shipped ones.  A template is found at most four
    folders down; a link that makes a loop ends the search.
(c) The tree: the folders envelopes, labels/avery/us, labels/avery/a4 and
    letters, 16 + 58 + 43 + 2 templates, and no template outside them.
(d) The list of New in the Editor: the folders after the built-in settings,
    each with its number of templates; Enter opens a folder, Left and
    Backspace go up, a typed number finds its sheet in any folder; a tree with
    no folder has no folder entry in its list; the field Template has the same
    list and the entry Another file.
(e) The same lookups are made again on spoiled copies of src/tplfind.c, one
    rule at a time (the search below the place of a template off, the bare
    name not looked for below the place, the path name not looked for below
    the place), and every copy has to fail a check.
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import re
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui

tree = os.path.dirname(here)
shipped = os.path.join(tree, 'templates')
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
fails = 0


def check(label, ok, detail=''):
    global fails
    if ok:
        print('ok   ' + label)
    else:
        fails += 1
        print('FAIL ' + label)
        if detail != '':
            print('     ' + str(detail))


tmp = tempfile.mkdtemp(prefix='tpf')


def compile_driver(src, out):
    """Build tests/tplfinddrv.c with the file src (a copy of src/tplfind.c)."""
    r = subprocess.run(
        ['cc', '-std=gnu11', '-DSTRATA_DEBUG', '-iquote',
         os.path.join(tree, 'src'),
         os.path.join(here, 'tplfinddrv.c'), src, '-o', out],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        print(r.stdout.decode('ascii', 'replace'))
        sys.exit(1)


def find(drv, names, env_templates, home, workdir):
    """The paths that the driver finds for names: a list, NOTFOUND for none."""
    env = dict(os.environ, STRATA_HOME=home)
    env.pop('STRATA_TEMPLATES', None)
    if env_templates is not None:
        env['STRATA_TEMPLATES'] = env_templates
    r = subprocess.run([drv, workdir] + list(names), env=env,
                       stdout=subprocess.PIPE, timeout=120)
    return r.stdout.decode('utf-8', 'replace').split('\n')[:-1]


def shipped_files():
    """(folder below templates, name without .doc, full path) of each file."""
    out = []
    for d, _, fs in os.walk(shipped):
        for f in sorted(fs):
            if f.endswith('.doc'):
                rel = os.path.relpath(d, shipped)
                out.append(('' if rel == '.' else rel, f[:-4],
                            os.path.join(d, f)))
    return sorted(out, key=lambda t: t[2])


def lookups(drv, label_prefix):
    """The failures of the lookups of (a) and (b) with the driver drv."""
    bad = []
    files = shipped_files()
    home = os.path.join(tmp, 'home')
    os.makedirs(home, exist_ok=True)
    work = os.path.join(tmp, 'work')
    os.makedirs(work, exist_ok=True)
    forms = (('bare', lambda f, n: n),
             ('path', lambda f, n: f + '/' + n),
             ('bare, capitals', lambda f, n: n.upper()),
             ('path, capitals', lambda f, n: (f + '/' + n).upper()),
             ('bare with .doc', lambda f, n: n + '.doc'),
             ('path with .doc', lambda f, n: f + '/' + n + '.doc'))
    for form, fn in forms:
        names = [fn(f, n) for f, n, p in files]
        got = find(drv, names, shipped, home, work)
        for (f, n, p), g, nm in zip(files, got, names):
            ok = g != 'NOTFOUND' and os.path.basename(g).lower() == \
                os.path.basename(p).lower()
            if form.startswith('path'):
                ok = ok and os.path.samefile(g, p)
            if not ok:
                bad.append('%s: %s -> %s' % (form, nm, g))
                break
    # The bare names of the time before the folders are the names of the
    # files, so the same bare names are looked up: every one must be found.
    got = find(drv, [n for f, n, p in files], shipped, home, work)
    if 'NOTFOUND' in got:
        bad.append('an old flat name is not found: %s' %
                   [n for (f, n, p), g in zip(files, got)
                    if g == 'NOTFOUND'][:3])
    # A name that no file has is not found, and a path that is wrong is not.
    got = find(drv, ['no-such-template', 'labels/avery/no-such',
                     'nofolder/label-us-5160'], shipped, home, work)
    if got != ['NOTFOUND'] * 3:
        bad.append('a missing template is found: %s' % got)
    return bad


def shadow_cases(drv):
    """The failures of (b): the folder of a person, the depth, a loop."""
    bad = []
    base = tempfile.mkdtemp(dir=tmp)
    user = os.path.join(base, 'user')
    mine = os.path.join(user, 'labels', 'avery', 'us')
    os.makedirs(mine)
    with open(os.path.join(mine, 'label-us-5160.doc'), 'wb') as f:
        f.write(b'*R3\r\nMDmine\x00\r\n*B0\r\n*E\r\n')
    home = os.path.join(base, 'home2')
    os.makedirs(home)
    # The shipped templates are the place of the settings: a link.
    os.symlink(shipped, os.path.join(home, 'templates'))
    work = os.path.join(base, 'work2')
    os.makedirs(work)
    got = find(drv, ['label-us-5160', 'labels/avery/us/label-us-5160',
                     'label-us-5161', 'labels/avery/us/label-us-5161'],
               user, home, work)
    mine_path = os.path.join(mine, 'label-us-5160.doc')
    if not (got[0] != 'NOTFOUND' and os.path.samefile(got[0], mine_path)):
        bad.append('the bare name does not find the template of the person '
                   'first: %s' % got[0])
    if not (got[1] != 'NOTFOUND' and os.path.samefile(got[1], mine_path)):
        bad.append('the path name does not find the template of the person '
                   'first: %s' % got[1])
    for i in (2, 3):
        want = os.path.join(shipped, 'labels', 'avery', 'us',
                            'label-us-5161.doc')
        if not (got[i] != 'NOTFOUND' and os.path.samefile(got[i], want)):
            bad.append('a template that the person has not is not the '
                       'shipped one: %s' % got[i])
    # The depth: a template four folders down is found, five is not.
    deep = os.path.join(base, 'deep')
    d4 = os.path.join(deep, 'a', 'b', 'c', 'd')
    d5 = os.path.join(d4, 'e')
    os.makedirs(d5)
    for d, n in ((d4, 'four'), (d5, 'five')):
        with open(os.path.join(d, n + '.doc'), 'wb') as f:
            f.write(b'*R3\r\n*B0\r\n*E\r\n')
    got = find(drv, ['four', 'a/b/c/d/four', 'five'], deep,
               os.path.join(base, 'home3'), work)
    if got[0] == 'NOTFOUND' or got[1] == 'NOTFOUND':
        bad.append('a template four folders down is not found: %s' % got[:2])
    if got[2] != 'NOTFOUND':
        bad.append('a template five folders down is found: %s' % got[2])
    # A link that points to the folder above it makes a loop: the search ends.
    loop = os.path.join(base, 'loop')
    os.makedirs(os.path.join(loop, 'x'))
    os.symlink(loop, os.path.join(loop, 'x', 'back'))
    got = find(drv, ['no-such-template'], loop, os.path.join(base, 'home3'),
               work)
    if got != ['NOTFOUND']:
        bad.append('the search in a loop of links did not end: %s' % got)
    return bad


def spoiled(src_text):
    """Copies of src/tplfind.c that each break one rule: (label, text)."""
    breaks = (
        ('the search below the place of a template is off',
         'find_below(dir, full, TEMPLATE_DEPTH, out, cap)',
         'find_below(dir, full, 0, out, cap)'),
        ('a bare name is not looked for below the place',
         'if (find_deep(dirs[i], name, out, cap) != 0) {',
         'if (find_with_extension(dirs[i], name, out, cap) != 0) {'),
        ('a path name is not looked for below the place',
         'if (find_relative(dirs[i], name, out, cap) != 0) {',
         'if (0) {'))
    out = []
    for label, old, new in breaks:
        out.append((label, src_text.replace(old, new, 1) if old in src_text
                    else None))
    return out


def lookup_failures(drv, label):
    bad = lookups(drv, label)
    bad += shadow_cases(drv)
    return bad


# ---- (a) and (b): the lookups ------------------------------------------
srcfile = os.path.join(tree, 'src', 'tplfind.c')
src_text = open(srcfile).read()
drv = os.path.join(tmp, 'drv')
compile_driver(srcfile, drv)
bad = lookup_failures(drv, 'src/tplfind.c')
check('every template is found by its bare name, its path name, in capitals '
      'and with the extension; the old flat names, the folder of a person, '
      'the depth of four and a loop of links give the right result',
      bad == [], bad[:5])

# ---- (c) the tree --------------------------------------------------------
files = shipped_files()
count = {}
for f, n, p in files:
    count[f] = count.get(f, 0) + 1
check('the templates are in envelopes (16), labels/avery/us (58), '
      'labels/avery/a4 (43) and letters (2), and nowhere else',
      count == {'envelopes': 16, 'labels/avery/us': 58,
                'labels/avery/a4': 43, 'letters': 2}, count)
check('a file of the folder envelopes is an envelope, of letters a letter, '
      'of labels/avery/us a sheet for US Letter and of labels/avery/a4 a sheet '
      'for A4', all(
          (f == 'envelopes') == n.startswith('envelope-') and
          (f == 'letters') == n.startswith('letter-') and
          (f == 'labels/avery/us') == n.startswith('label-us-') and
          (f == 'labels/avery/a4') == n.startswith('label-a4-')
          for f, n, p in files))


def first_words(path):
    with open(path, 'rb') as fh:
        data = fh.read()
    m = re.search(rb'\r\nMD([^\x00]*)\x00', data)
    return m.group(1).decode('ascii') if m else ''


check('every label sheet below labels/avery has a description that begins '
      'with Avery', all(first_words(p).startswith('Avery ')
                        for f, n, p in files
                        if f.startswith('labels/avery')))

# ---- (d) the list of New in the Editor ------------------------------------
if not os.path.exists(stred) or not os.path.exists(helpf):
    print('SKIP the list of New: build the programs first')
else:
    # A copy of the Editor in a folder with no share/strata/templates beside
    # it: the build directory has a link to the templates.
    bindir = os.path.join(tmp, 'bin')
    os.makedirs(bindir)
    shutil.copy(stred, bindir)
    stred = os.path.join(bindir, 'stred')
    os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
    os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
    os.environ['STRATA_TEMPLATES'] = os.path.abspath(shipped)
    work = os.path.join(tmp, 'listwork')
    os.makedirs(work)
    base = os.path.join(work, 'base.doc')
    with open(base, 'wb') as fh:
        fh.write(b'*R3\r\nMR1000\r\nPP\\1\\\x00\r\nP1C33\r\n*B0\r\nS\r\n'
                 b'Tbase\x1d\x00\r\n*E\r\n')
    os.chdir(work)
    NEW = ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'wait']

    def screen(keys):
        return tui.session([stred, '-H', helpf, base], NEW + keys + ['wait',
                           'show'], rows=30)[0]

    scr = screen([])
    check('the list of New has the built-in settings, then the folders '
          'envelopes/, labels/ and letters/, and no template of a folder',
          'Structured' in scr and 'Unstructured' in scr and
          scr.find('Structured') < scr.find('envelopes/') <
          scr.find('labels/') < scr.find('letters/') and
          'label-us-5160' not in scr and 'envelope-uk-dl' not in scr, scr)
    scr = screen(['DOWN', 'DOWN'])
    check('the line below the list says how many templates the folder '
          'envelopes has', 'A subdirectory of 16 templates' in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN'])
    check('the folder labels has 101 templates',
          'A subdirectory of 101 templates' in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN', 'DOWN'])
    check('the folder letters has 2 templates',
          'A subdirectory of 2 templates' in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'ENTER'])
    check('Enter on the folder envelopes opens it: the title names the '
          'folder and the envelopes are listed, with no folder entry',
          'Templates: envelopes' in scr and 'envelope-us-ca-10' in scr and
          'envelope-uk-dl' in scr and 'labels/' not in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN', 'ENTER'])
    check('the folder labels holds the folder avery/',
          'Templates: labels' in scr and 'avery/' in scr and
          'A subdirectory of 101 templates' in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN', 'ENTER', 'ENTER'])
    check('the folder avery holds the folders a4/ and us/, of 43 and 58 '
          'templates', 'Templates: labels/avery' in scr and 'a4/' in scr and
          'us/' in scr and 'label-us-5160' not in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN', 'ENTER', 'ENTER', 'DOWN', 'ENTER'])
    check('the folder us has the sheets for US Letter: the first one is '
          'label-us-3112 and the description is below the list',
          'Templates: labels/avery/us' in scr and 'label-us-3112' in scr and
          'label-us-5160' in scr and 'label-a4' not in scr and
          'Avery 3112' in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN', 'ENTER', 'ENTER', 'LEFT'])
    check('Left goes up one folder: the list shows labels again, with the '
          'cursor on avery/',
          'Templates: labels' in scr and 'Templates: labels/avery' not in scr
          and 'A subdirectory of 101 templates' in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN', 'ENTER', 'ENTER', 'BKSP'])
    check('Backspace with no typed text goes up one folder too',
          'Templates: labels' in scr and 'Templates: labels/avery' not in scr
          and 'A subdirectory of 101 templates' in scr, scr)
    scr = screen(['DOWN', 'DOWN', 'DOWN', 'ENTER', 'LEFT', 'LEFT'])
    check('Left twice from labels/avery is the top: the built-in settings '
          'are there again', 'Structured' in scr and 'Templates:' not in
          scr.replace('Templates ', ''), scr)
    scr = screen(['text:5160'])
    check('a number typed at the top finds its sheet in the folder '
          'labels/avery/us: the folder is open, the text is kept and the '
          'description is shown',
          'Templates: labels/avery/us' in scr and 'Find: 5160' in scr and
          'Avery 5160 address labels, 30 per sheet, US Letter' in scr, scr)
    scr = screen(['text:l7163'])
    check('the number of an A4 sheet is found in the folder labels/avery/a4',
          'Templates: labels/avery/a4' in scr and
          'Avery L7163 address labels, 14 per sheet, A4' in scr, scr)
    scr = screen(['text:5160', 'LEFT'])
    check('Left after a find goes up from the folder that the find opened',
          'Templates: labels/avery' in scr and
          'Templates: labels/avery/us' not in scr, scr)
    scr = screen(['ESC', 'DOWN', 'DOWN', 'F2'])
    check('the field Template of the New File panel has the same list: the '
          'folders and the entry Another file',
          'envelopes/' in scr and 'labels/' in scr and 'letters/' in scr and
          'Another file...' in scr, scr)
    newd = os.path.join(work, 'fromfolder')
    tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'DOWN', 'DOWN',
                'ENTER', 'ENTER', 'DOWN', 'ENTER', 'ENTER', 'wait', 'DOWN',
                'text:' + newd, 'INS', 'wait', 'ENTER', 'wait', 'C-s', 'wait',
                'wait'], rows=30)
    data = open(newd + '.doc', 'rb').read() if os.path.exists(newd + '.doc') \
        else b''
    check('a template chosen in a folder of the list is the copy that the new '
          'document is: the description of the sheet and its paper',
          re.search(rb'MDAvery 3112 ', data) is not None and
          b'DP0,8500,11000' in data, data[:200])

    # A tree with no folder has no folder entry in its list.
    flat = os.path.join(tmp, 'flat')
    os.makedirs(flat)
    for n in ('alpha', 'beta'):
        with open(os.path.join(flat, n + '.doc'), 'wb') as fh:
            fh.write(b'*R3\r\nMD' + n.encode() + b' text\x00\r\n*B0\r\n*E\r\n')
    os.environ['STRATA_TEMPLATES'] = flat
    scr = screen([])
    check('a tree with no folder has none in its list: the templates follow '
          'the built-in settings',
          'alpha' in scr and 'beta' in scr and 'A subdirectory of' not in scr and
          'envelopes/' not in scr and 'labels/' not in scr, scr)
    os.environ['STRATA_TEMPLATES'] = os.path.abspath(shipped)

# ---- (e) spoiled copies of src/tplfind.c ------------------------------------
for label, text in spoiled(src_text):
    if text is None:
        check('mutation: %s (the text to spoil is in src/tplfind.c)' % label,
              False)
        continue
    path = os.path.join(tmp, 'bad.c')
    with open(path, 'w') as fh:
        fh.write(text)
    bdrv = os.path.join(tmp, 'baddrv')
    compile_driver(path, bdrv)
    bad = lookup_failures(bdrv, label)
    check('mutation: a copy where %s fails a check' % label, bad != [])

shutil.rmtree(tmp, ignore_errors=True)
print('tplfolders: %s' % ('all passed' if fails == 0 else
                          '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
