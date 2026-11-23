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

"""Runs a part of tests/smoke.py.

smoke.py is one long script of blocks, each begun by a comment line that starts
with "# ---- ".  With SMOKE_PART=K/N in the environment it runs only the blocks
of part K (see PARTS) and the setup of the others, so that the N parts can run
at the same time (tests/smokerun.py starts them).  The statements of a block
that is not in the part are not run, except the ones that set up what a later
block uses: definitions, imports, assignments and the copying of files and the
making of directories; the statements that start a program (session, fm,
printed and the others in RUNNERS) and the checks are left out.  So every
check of smoke.py is made by exactly one part, and a part prints the same
lines as the whole script does for its blocks.

    SMOKE_PART=2/4 python3 tests/smoke.py
"""
import sys as _s; _s.dont_write_bytecode = True
import ast, os, re, sys, time

# The parts, as lists of words that the header of a block starts with; the block
# before the first header is part 1.  A block that no word names is in part 1.
# The times are the seconds that the blocks took in a serial run.
PARTS = [
    # 280 s: the start, Case, tags, markers, the menu bar, Print, PDF viewer,
    # Tools, the thesaurus, Edit
    ['Case (Ctrl-F6)', 'section tags', 'markers: the MARKER', 'the menu bar',
     'the Print menu', 'PDF viewer', 'the Tools menu -', 'the thesaurus',
     'the Edit menu'],
    # 289 s: Global, section levels, notes
    ['the Global menu', 'Border (Alt-F8)', 'Border on a highlight',
     'Border on the screen', 'section levels',
     'notes: the note editor'],
    # 353 s: Library, Macros, counters, Keycap Format, File
    ['the Tools menu: Library', 'the Tools menu: Macros', 'counters, comments',
     'the Keycap Format', 'the File menu', 'Recovery of a damaged'],
    # 368 s: spell check, the master menu, Setup, the help
    ['the spell check', 'str: the master menu', 'Setup: the print settings',
     'the help (F1)'],
    # 266 s: more help, the markers Filename, the menus at every width,
    # Compare, the colors, Undo
    ['the help of the places', 'the markers Filename', 'every menu is whole',
     'Compare Document Versions', 'the colors of the screen',
     'Undo and Redo'],
    # 243 s: the backups, Initial Macro Set, Maintain Dictionaries
    ['numbered backups', 'Backups, part', 'Setup: the two levels',
     'Setup: Initial Macro Set',
     'Maintain Dictionaries'],
    # 176 s: Search and Replace, the table of contents, Include
    ['Search and Replace', 'the table of contents', 'the Include marker'],
]

# The names that start a program: a statement that calls one of them is not run
# in a block that is not in the part.
RUNNERS = {'session', 'fm', 'printed', 'bk_edit', 'rb_session', 'auto_session',
           'run_prog', 'mgr', 'edit', 'check', 'popen', 'Popen', 'run',
           'system', 'check_output', 'check_call', 'call'}

# Calls of files and directories that prepare what a block needs.
SETUP_CALLS = {'copy', 'copy2', 'copyfile', 'copytree', 'mkdir', 'makedirs',
               'chmod', 'symlink', 'utime', 'write', 'writelines', 'close',
               'rename', 'replace', 'tzset'}


def blocks(src):
    """The source cut at the lines that begin a block: [(header, text)]."""
    lines = src.split('\n')
    cuts = [i for i, l in enumerate(lines) if l.startswith('# ---- ')]
    out = []
    first = 0
    names = ['(the start)'] + [lines[i][7:] for i in cuts]
    starts = [0] + cuts
    ends = cuts + [len(lines)]
    for n, a, b in zip(names, starts, ends):
        out.append((n.strip(), '\n'.join(lines[a:b]) + '\n', a))
    return out


def part_of(name, index):
    """The part (0 to N - 1) that runs the block."""
    if index == 0:
        return 0
    for k, words in enumerate(PARTS):
        for w in words:
            if name.startswith(w):
                return k
    return 0


# The calls that an assignment of a block that is not run may make: they only
# compute a value or prepare a directory; a call that is not here (it reads a
# file that a program made, say) makes the statement be left out.
PURE_CALLS = {
    'join', 'dirname', 'basename', 'abspath', 'realpath', 'expanduser',
    'splitext', 'exists', 'isdir', 'get', 'getcwd', 'mkdtemp', 'getpid',
    'str', 'int', 'len', 'list', 'dict', 'set', 'tuple', 'sorted', 'range',
    'format', 'encode', 'replace', 'split', 'strip', 'lower', 'upper',
    'startswith', 'endswith', 'abs', 'min', 'max', 'sum', 'bytes', 'repr',
    'fresh', 'bk_home', 'bakp', 'which', 'copy', 'mkdir', 'makedirs',
    'chmod', 'copyfile', 'copytree', 'item', 'tool', 'path_join', 'write',
    'writelines'}


def calls_only_pure(node):
    for n in ast.walk(node):
        if isinstance(n, ast.Call):
            f = n.func
            name = f.id if isinstance(f, ast.Name) else \
                f.attr if isinstance(f, ast.Attribute) else None
            if name in RUNNERS or (name not in PURE_CALLS and
                                   name not in LOCAL_DEFS):
                return False
    return True


# The functions that the script defines; they may be called by an assignment.
LOCAL_DEFS = set()


def setup_only(stmt):
    """Is the statement one that a block that is not run must still do?"""
    if isinstance(stmt, ast.FunctionDef):
        LOCAL_DEFS.add(stmt.name)
        return True
    if isinstance(stmt, (ast.ClassDef, ast.Import, ast.ImportFrom, ast.Global,
                         ast.Nonlocal)):
        return True
    if isinstance(stmt, (ast.Assign, ast.AugAssign, ast.AnnAssign)):
        return stmt.value is None or calls_only_pure(stmt.value)
    if isinstance(stmt, ast.With):
        # with open(name, 'w') as f: f.write(text): a file that a block uses.
        for item in stmt.items:
            c = item.context_expr
            if not (isinstance(c, ast.Call) and isinstance(c.func, ast.Name)
                    and c.func.id == 'open' and len(c.args) > 1 and
                    isinstance(c.args[1], ast.Constant) and
                    'w' in str(c.args[1].value) and calls_only_pure(c.args[0])):
                return False
        return all(isinstance(b, ast.Expr) and isinstance(b.value, ast.Call)
                   and isinstance(b.value.func, ast.Attribute) and
                   b.value.func.attr in ('write', 'writelines') and
                   calls_only_pure(b.value) for b in stmt.body)
    if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call):
        f = stmt.value.func
        if isinstance(f, ast.Attribute) and f.attr in ('write', 'writelines') \
                and isinstance(f.value, ast.Call) and \
                isinstance(f.value.func, ast.Name) and \
                f.value.func.id == 'open':
            # open(name, 'w').write(text): a file that a block uses.
            mode = f.value.args[1] if len(f.value.args) > 1 else None
            return isinstance(mode, ast.Constant) and 'w' in str(mode.value) \
                and all(calls_only_pure(a) for a in f.value.args) and \
                all(calls_only_pure(a) for a in stmt.value.args)
        if isinstance(f, ast.Attribute) and f.attr in SETUP_CALLS:
            return calls_only_pure(stmt.value)
        return False
    return False


def run(path, spec):
    k, n = [int(x) for x in spec.split('/')]
    os.environ.pop('SMOKE_PART', None)
    src = open(path).read()
    g = {'__name__': '__main__', '__file__': os.path.abspath(path)}
    todo = blocks(src)
    trailer = [i + 1 for i, l in enumerate(src.split('\n'))
               if l == 'shutil.rmtree(tmp)'][-1]
    for idx, (name, text, first_line) in enumerate(todo):
        mine = part_of(name, idx) == k - 1
        if os.environ.get('SMOKE_PART_TRACE'):
            print('--- block %d %s: %s' % (idx, 'RUN' if mine else 'skip',
                                           name), flush=True)
        tree = ast.parse('\n' * first_line + text)
        for stmt in tree.body:
            if mine or stmt.lineno >= trailer:
                code = compile(ast.Module([stmt], []), path, 'exec')
                exec(code, g)
            elif setup_only(stmt):
                # What a block that is not run prepares may depend on what a
                # program made (a file that is not there): that is no error.
                try:
                    code = compile(ast.Module([stmt], []), path, 'exec')
                    exec(code, g)
                except Exception:
                    pass
    return 0
