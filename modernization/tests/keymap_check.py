#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Test of tests/keymap.py and of docs/KEY-MAP.txt.
#
# The key map is made from the source.  This test shows that it follows the
# source: every command of the function key table has a line in the key list,
# the document in docs/ is what the script makes today, and each change of
# the source (or of the menu tree) that the tables do not know is found: a
# check that cannot fail would not prove anything.
#
#   python3 -I -B tests/keymap_check.py
#
# STRED names the Editor (the menu tree comes from it); STRATA_BUILD_DIR can
# name the build folder instead.  Without the Editor the test says SKIP.
import atexit
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPT = os.path.join(HERE, 'keymap.py')
fails = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name +
          ('' if cond else '\n' + str(extra)))
    fails += 0 if cond else 1


def find_stred():
    s = os.environ.get('STRED')

    if s and os.path.exists(s):
        return s

    b = os.environ.get('STRATA_BUILD_DIR')

    if b and os.path.exists(os.path.join(b, 'term', 'stred')):
        return os.path.join(b, 'term', 'stred')

    return None


stred = find_stred()

if stred is None:
    print('SKIP: build the programs first (STRED or STRATA_BUILD_DIR)')
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix='keymap')
atexit.register(shutil.rmtree, tmp, True)


def run(args, env_extra=None, stred_path=None):
    env = dict(os.environ)
    env['STRED'] = stred_path or stred
    env.pop('KEYMAP_SRC', None)
    env.pop('KEYMAP_DROP', None)
    env.update(env_extra or {})

    return subprocess.run([sys.executable, '-I', '-B', SCRIPT] + args,
                          capture_output=True, text=True, env=env,
                          timeout=120)


def source_copy(name, edit):
    """A folder with the three sources and one of them changed."""
    d = os.path.join(tmp, name)
    os.makedirs(d)

    for f in ('ui.c', 'term.c', 'term_x11.c'):
        shutil.copy(os.path.join(ROOT, 'src', f), os.path.join(d, f))

    p = os.path.join(d, 'ui.c')
    text = open(p, encoding='utf-8').read()
    new = edit(text)

    if new == text:
        raise SystemExit('the mutation %s changed nothing' % name)

    open(p, 'w', encoding='utf-8').write(new)

    return d


def fake_stred(name, edit):
    """An Editor that prints the menu tree changed by edit."""
    script = os.path.join(tmp, name)
    filt = script + '.py'

    with open(filt, 'w') as f:
        f.write('import sys\n')
        f.write('t = sys.stdin.read()\n')
        f.write(edit)
        f.write('sys.stdout.write(t)\n')

    with open(script, 'w') as f:
        f.write('#!/bin/bash\n')
        f.write('"%s" "$@" | python3 -I -B "%s"\n' % (stred, filt))

    os.chmod(script, 0o755)

    return script


# 1. The check passes on the real source.
r = run(['--check'])
check('the tables and the source agree', r.returncode == 0 and
      'all in order' in r.stdout, r.stdout + r.stderr)

# 2. The document is made, and it is the same twice.
a = run(['--text'])
b = run(['--text'])
check('the document is made and has the five parts',
      a.returncode == 0 and all(('PART %d.' % i) in a.stdout
                                for i in (1, 2, 3, 4)) and
      '1.9  The soft hyphen' in a.stdout, a.stderr)
check('two runs give the same bytes', a.stdout == b.stdout)

# 3. The lines are at most 80 columns, ASCII.
lines = a.stdout.splitlines()
check('no line is longer than 80 columns and all is ASCII',
      all(len(x) <= 80 and all(ord(c) < 127 for c in x) for x in lines))

# 4. docs/KEY-MAP.txt is what the script makes.
doc = os.path.join(ROOT, 'docs', 'KEY-MAP.txt')
check('docs/KEY-MAP.txt is what tests/keymap.py makes now',
      os.path.exists(doc) and open(doc).read() == a.stdout,
      'run: python3 -I -B tests/keymap.py --write docs/KEY-MAP.txt')

# 5. Every command of fkeys[] has its line in the list, with words.
ui = open(os.path.join(ROOT, 'src', 'ui.c'), encoding='utf-8').read()
a0 = ui.index('static const FKey fkeys[] = {')
rows = re.findall(r'\{"([^"]+)",\s*(\d),\s*(\d+),\s*\w+\}',
                  ui[a0:ui.index('};', a0) + 2])
missing = []

for name, mod, n in rows:
    key = ('Ctrl-' if mod == '1' else 'Alt-' if mod == '2' else '') + \
        'F' + n

    if not re.search(r'^  %s +[A-Za-z]' % re.escape(key), a.stdout, re.M):
        missing.append(key)

check('every command of fkeys[] (%d keys) has a line of the key list'
      % len(rows), len(rows) >= 30 and missing == [], missing)
check('no line of the key list has an unknown word', '  ?  ' not in
      a.stdout and ' ? ' not in a.stdout.split('1.2  Cursor')[0])

# 6. The menu routes: every command without an entry has a proposal.
part4 = a.stdout.split('PART 4.')[1]
check('PART 4 lists the commands with no menu entry and a proposal for each',
      'Commands with a function key and NO menu entry (24)' in part4 and
      'Border ' in part4 and 'Tools, Library, Copy' in part4 and
      ' ?\n' not in part4)

# 7. Mutations: each change that the tables do not know must be found.
r = run(['--check'], {'KEYMAP_DROP': 'Border'})
check('MUTATION: a command taken out of the key list is found',
      r.returncode == 1 and 'command Border of fkeys[]' in r.stdout,
      r.stdout)

d = source_copy('m_rename', lambda t: t.replace('{"Join", 2, 3,',
                                                  '{"Joinx", 2, 3,'))
r = run(['--check'], {'KEYMAP_SRC': d})
check('MUTATION: a command renamed in fkeys[] is found',
      r.returncode == 1 and 'Joinx' in r.stdout, r.stdout)

d = source_copy('m_ctrl', lambda t: t.replace(
    '            case 26:\n', '            case 28:\n                return;\n'
    '\n            case 26:\n', 1))
r = run(['--check'], {'KEYMAP_SRC': d})
check('MUTATION: a new control key in the source is found',
      r.returncode == 1 and 'control key 28' in r.stdout, r.stdout)

d = source_copy('m_alt', lambda t: t.replace("case '*':", "case '%':", 1))
r = run(['--check'], {'KEYMAP_SRC': d})
check('MUTATION: a changed Alt key in the source is found',
      r.returncode == 1 and 'Alt-%' in r.stdout and 'Alt-*' in r.stdout,
      r.stdout)

d = source_copy('m_move', lambda t: t.replace('MV_COL_LEFT',
                                                'MV_COL_LEFTX', 1))
r = run(['--check'], {'KEYMAP_SRC': d})
check('MUTATION: a changed cursor key in the source is found',
      r.returncode == 1 and 'MV_COL_LEFT' in r.stdout, r.stdout)

fk = fake_stred('stred_add', "t += 'menu\\tTools\\tCount|Frobnicate\\n'\n")
r = run(['--check'], stred_path=fk)
check('MUTATION: a new entry in the menu tree is found',
      r.returncode == 1 and 'Tools:Frobnicate' in r.stdout, r.stdout)

fk = fake_stred('stred_del',
                "t = t.replace('Count|Undo|Redo', 'Undo|Redo')\n")
r = run(['--check'], stred_path=fk)
check('MUTATION: an entry that left the menu tree is found',
      r.returncode == 1 and 'Tools:Count' in r.stdout, r.stdout)

# 8. The document of a mutated source differs from docs/KEY-MAP.txt.
d = source_copy('m_doc', lambda t: t.replace('{"Border", 2, 8,',
                                               '{"Borderx", 2, 8,'))
r = run(['--text'], {'KEYMAP_SRC': d})
check('MUTATION: the document of a changed source is not the one in docs/',
      r.returncode == 0 and r.stdout != a.stdout)

print('keymap_check: %s' % ('all passed' if fails == 0 else
                            '%d failed' % fails))
sys.exit(1 if fails else 0)
