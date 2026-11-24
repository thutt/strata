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

"""Ins in a list popup chooses the entry, and in the template list of the
New File panel it also finishes the panel (the owner's question: when will
the template selection allow Ins to select and finish the panel).

  (a) a list popup shows 'Ins or Enter selects, Esc cancels' on its border;
  (b) the Editor's New File panel: F2 on the field Template and Ins on a
      template makes the new document at once, from that template; Enter
      only fills the field and leaves the panel open; Esc leaves the panel
      open and the field as it was; Ins on Another file... does not finish
      the panel;
  (c) the list of the templates that the Editor shows before the panel
      chooses with Ins as with Enter;
  (d) the Document Manager: the same for F2 in its New File panel, and a
      type or a template chosen with Ins in the first list makes the
      document at once;
  (e) another list (the Printer list of the Print panel) chooses with Ins and
      leaves the panel open.

    STRED=... STR=... python3 -I -B tests/inslist.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
import mktemplates
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprog = os.environ.get('STR', os.path.join(os.path.dirname(stred), 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
shipped = os.path.join(here, '..', 'templates')
if not os.path.exists(stred) or not os.path.exists(strprog) or \
        not os.path.isdir(shipped):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='insl')
# Copies of the programs in a folder with no share/strata/templates beside
# them: the build directory has a link to the shipped templates, which would
# put all of them in the list.
bindir = os.path.join(tmp, 'bin')
os.makedirs(bindir)
for prog in (stred, strprog):
    shutil.copy(prog, bindir)
stred = os.path.join(bindir, os.path.basename(stred))
strprog = os.path.join(bindir, os.path.basename(strprog))
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


# Two templates in a place that the programs search: the list shows the two
# built-in settings, the two templates and Another file.  Sorted by name
# the first template is envelope-uk-dl, the second letter-us.
places = os.path.join(tmp, 'places')
os.makedirs(places)
for name in ('envelope-uk-dl.doc', 'letter-us.doc'):
    shutil.copy(os.path.join(shipped, mktemplates.relpath(name[:-4])),
                os.path.join(places, name))
os.environ['STRATA_TEMPLATES'] = places

work = os.path.join(tmp, 'work')
os.makedirs(work)
base = os.path.join(work, 'base.doc')
shutil.copy(os.path.join(shipped, 'letters', 'letter-a4.doc'), base)
os.chdir(work)


def read(path):
    return open(path + '.doc', 'rb').read() if os.path.exists(
        path + '.doc') else b''


# The keys of New in the Editor: F12, File, New.  With templates in a place
# the list of them comes first; its Esc asks for nothing and shows the panel
# (the first field is Working Directory, the second Document Name, the third
# Template).
NEW = ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'waitfor:Templates']
PANEL = NEW + ['ESC', 'waitfor:Document Name']
TEMPLATE_LIST = ['DOWN', 'F2', 'waitfor:Another file']

# ---- (a) the hint ---------------------------------------------------------
d = tui.session([stred, '-H', helpf, base], NEW + ['show'], rows=30)
check('(a) the list of the templates shows the keys that choose an entry',
      'Ins or Enter selects' in d[0] or 'Ins selects' in d[0], d[0])

# ---- (b) the Editor's New File panel ----------------------------------------
ins1 = os.path.join(work, 'insone')
tui.session([stred, '-H', helpf, base], PANEL + ['DOWN', 'text:' + ins1,
    'DOWN'] + ['F2', 'waitfor:Another file', 'DOWN', 'DOWN', 'INS', 'wait',
    'wait', 'wait', 'text:ZZZ', 'C-s', 'wait', 'wait'], rows=30)
data = read(ins1)
check('(b) Ins on a template in the list of the field Template finishes the '
      'panel: the document is made from the template (its text size and '
      'blocks) and the first thing typed is in it',
      b'PS14' in data and b'BGRecipient' in data and data.count(b'ZZZ') == 1,
      data[:200])

ent = os.path.join(work, 'entone')
d = tui.session([stred, '-H', helpf, base], PANEL + ['DOWN', 'text:' + ent,
    'DOWN', 'F2', 'waitfor:Another file', 'DOWN', 'DOWN', 'ENTER', 'wait',
    'show'], rows=30)
check('(b) Enter on a template in the list only fills the field: the panel '
      'is still open with the template in it, and no document is made',
      'New File' in d[0] and 'Template' in d[0] and
      'envelope-uk-dl' in d[0] and read(ent) == b'', d[0])

d = tui.session([stred, '-H', helpf, base], PANEL + ['DOWN', 'text:esc',
    'DOWN', 'F2', 'waitfor:Another file', 'DOWN', 'DOWN', 'ESC', 'wait',
    'show'], rows=30)
check('(b) Esc in the list leaves the panel open and the field as it was',
      'New File' in d[0] and 'Template' in d[0] and
      'envelope-uk-dl' not in d[0] and read(os.path.join(work, 'esc')) == b'',
      d[0])

d = tui.session([stred, '-H', helpf, base], PANEL + ['DOWN', 'text:other',
    'DOWN', 'F2', 'waitfor:Another file', 'END', 'INS', 'wait', 'wait',
    'show'], rows=30)
check('(b) Ins on Another file... opens the file list and does not finish '
      'the panel',
      'base.doc' in d[0] and read(os.path.join(work, 'other')) == b'', d[0])

# ---- (c) the list before the panel -------------------------------------------
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'DOWN', 'INS',
    'waitfor:Document Name', 'show'], rows=30)
check('(c) Ins in the list of the templates before the panel chooses the '
      'template as Enter does: the panel shows it',
      'New File' in d[0] and 'envelope-uk-dl' in d[0], d[0])
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'DOWN', 'ENTER',
    'waitfor:Document Name', 'show'], rows=30)
check('(c) Enter chooses the same: the panel shows the template',
      'New File' in d[0] and 'envelope-uk-dl' in d[0], d[0])

# ---- (d) the Document Manager --------------------------------------------
mgr = os.path.join(tmp, 'mgr')
os.makedirs(mgr)
os.chdir(mgr)
DM = [strprog, '-H', helpf, mgr]
dmins = os.path.join(mgr, 'dmins')
tui.session(DM, ['ENTER', 'text:dmins', 'INS', 'waitfor:Templates', 'ENTER',
    'waitfor:Description', 'F2', 'waitfor:Another file', 'DOWN', 'DOWN',
    'INS', 'wait', 'wait', 'wait', 'text:ZZZ', 'C-s', 'wait', 'wait'],
    rows=30)
data = read(dmins)
check('(d) Document Manager: Ins on a template in the list of the field '
      'Template finishes the New File panel and the Editor starts on the '
      'template',
      b'PS14' in data and b'BGRecipient' in data and data.count(b'ZZZ') == 1,
      data[:200])

d = tui.session(DM, ['ENTER', 'text:dment', 'INS', 'waitfor:Templates',
    'ENTER', 'waitfor:Description', 'F2', 'waitfor:Another file', 'DOWN',
    'DOWN', 'ENTER', 'wait', 'show'], rows=30)
check('(d) Document Manager: Enter on a template only fills the field',
      'New File' in d[0] and 'envelope-uk-dl' in d[0] and
      read(os.path.join(mgr, 'dment')) == b'', d[0])

dmtop = os.path.join(mgr, 'dmtop')
tui.session(DM, ['ENTER', 'text:dmtop', 'INS', 'waitfor:Templates', 'DOWN',
    'DOWN', 'INS', 'wait', 'wait', 'wait', 'text:ZZZ', 'C-s', 'wait',
    'wait'], rows=30)
data = read(dmtop)
check('(d) Document Manager: Ins in the first list makes the document at '
      'once from that template, with no New File panel',
      b'PS14' in data and b'BGRecipient' in data and data.count(b'ZZZ') == 1,
      data[:200])

dmtopu = os.path.join(mgr, 'dmtopu')
tui.session(DM, ['ENTER', 'text:dmtopu', 'INS', 'waitfor:Templates', 'DOWN',
    'INS', 'wait', 'wait', 'wait', 'text:ZZZ', 'C-s', 'wait', 'wait'],
    rows=30)
data = read(dmtopu)
check('(d) Document Manager: Ins on Unstructured in the first list makes an '
      'unstructured document at once',
      data.count(b'ZZZ') == 1 and b'*B0' in data, data[:200])

d = tui.session(DM, ['ENTER', 'text:dmtope', 'INS', 'waitfor:Templates',
    'DOWN', 'DOWN', 'ENTER', 'waitfor:Description', 'show'], rows=30)
check('(d) Document Manager: Enter in the first list still shows the New '
      'File panel',
      'New File' in d[0] and 'Description' in d[0], d[0])

d = tui.session(DM, ['ENTER', 'text:dmtopc', 'INS', 'waitfor:Templates',
    'ESC', 'waitfor:document type', 'show'], rows=30)
check('(d) Document Manager: Esc in the first list asks for the type, as '
      'before', 'document type' in d[0], d[0])

# ---- (e) another list ----------------------------------------------------
os.chdir(work)
d = tui.session([stred, '-H', helpf, base], tui.OPEN_PRINT_FINAL + ['DOWN',
    'F2', 'waitfor:PDF viewer', 'DOWN', 'INS', 'wait', 'show'], rows=30)
check('(e) Ins in the Printer list of the Print panel chooses the entry and '
      'leaves the panel open (it is not a template list)',
      'Print Document' in d[0] and 'PDF viewer' in d[0] and
      'Printer' in d[0], d[0])

shutil.rmtree(tmp, ignore_errors=True)
print('inslist: %s' % ('all passed' if fails == 0 else '%d failed' % fails))
sys.exit(1 if fails else 0)
