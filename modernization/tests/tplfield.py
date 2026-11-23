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

"""The field Template of the New File panel, and the templates in the
Document Manager (the report of the owner: the list of the field was empty in
the folder of the shipped templates, and the Document Manager had no way to
apply a template when none was installed).

  (a) the Editor's New File panel: F2 on the field Template lists the built-in
      settings, the templates and a way to another file; the way to another
      file lists the .doc and .set files of a folder (the shipped templates
      are .doc files), and a folder with none lists none; the file that is
      chosen makes the new document;
  (b) a name typed in the field is looked for in the places of the templates,
      with .doc or .set added;
  (c) the Document Manager: a new name shows the New File panel with the
      field Template, whose list works the same, and the Editor that it
      starts has the template applied; the built-in choices still work.

    STRED=... STR=... python3 -I -B tests/tplfield.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
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
tmp = tempfile.mkdtemp(prefix='tplf')
# Copies of the programs in a folder with no share/strata/templates beside
# them: the build directory has a link to the templates, which would hide
# the case of no template in any place.
bindir = os.path.join(tmp, 'bin')
os.makedirs(bindir)
for prog in (stred, strprog):
    shutil.copy(prog, bindir)
stred = os.path.join(bindir, os.path.basename(stred))
strprog = os.path.join(bindir, os.path.basename(strprog))
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ.pop('STRATA_TEMPLATES', None)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


# A working folder with a document, a folder tpl with two shipped templates
# and a folder empty with no template.  No template is in a place that the
# programs search (no STRATA_TEMPLATES, an empty settings folder, no
# share/strata/templates next to a build): this is what the owner had.
work = os.path.join(tmp, 'work')
os.makedirs(os.path.join(work, 'tpl'))
os.makedirs(os.path.join(work, 'empty'))
shutil.copy(os.path.join(shipped, 'envelope-uk-dl.doc'),
            os.path.join(work, 'tpl', 'envelope-uk-dl.doc'))
shutil.copy(os.path.join(shipped, 'letter-us.doc'),
            os.path.join(work, 'tpl', 'letter-us.doc'))
open(os.path.join(work, 'empty', 'notes.txt'), 'w').write('not a template\n')
base = os.path.join(work, 'base.doc')
shutil.copy(os.path.join(shipped, 'letter-a4.doc'), base)
os.chdir(work)

# The keys of New in the Editor: F12, File, New; no list of templates is
# asked because no place has one, so the New File panel comes at once.
NEW = ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'wait']

# ---- (a) the Editor ---------------------------------------------------------
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'DOWN', 'F2',
    'wait', 'show'], rows=30)
scr = d[0]
check('with no template in any place F2 on the field Template says so and '
    'names the places',
    'No templates found' in scr and 'looked for in' in scr and
    'templates' in scr.split('looked for in')[-1], scr)
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'DOWN', 'F2',
    'wait', 'ENTER', 'wait', 'show'], rows=30)
scr = d[0]
check('F2 on the field Template lists the built-in settings and a way to '
    'another file',
    'Structured' in scr and 'Unstructured' in scr and
    'Another file' in scr, scr)

# Another file: the folders and the .doc files; go into tpl.
OTHER = NEW + ['DOWN', 'DOWN', 'F2', 'wait', 'ENTER', 'wait', 'DOWN', 'DOWN', 'ENTER', 'wait']
d = tui.session([stred, '-H', helpf, base], OTHER + ['show'], rows=30)
scr = d[0]
check('the way to another file lists the folders and the .doc files',
    'tpl/' in scr and 'empty/' in scr and 'base.doc' in scr, scr)
d = tui.session([stred, '-H', helpf, base], OTHER + ['DOWN', 'DOWN', 'ENTER',
    'wait', 'show'], rows=30)
scr = d[0]
check('the folder of the shipped templates lists them (the .doc files)',
    'envelope-uk-dl.doc' in scr and 'letter-us.doc' in scr, scr)
d = tui.session([stred, '-H', helpf, base], OTHER + ['DOWN', 'ENTER', 'wait',
    'show'], rows=30)
scr = d[0]
check('a folder with no .doc or .set file lists none, and not the .txt',
    '.doc' not in scr.split('\n', 3)[-1].replace('base.doc', '') and
    'notes.txt' not in scr, scr)

newa = os.path.join(work, 'fromuk')
d = tui.session([stred, '-H', helpf, base], OTHER + ['DOWN', 'DOWN', 'ENTER',
    'wait', 'DOWN', 'ENTER', 'wait', 'show'], rows=30)
scr = d[0]
check('choosing the file puts its path in the field Template',
    'envelope-uk-dl.doc' in scr and 'Template' in scr, scr)
tui.session([stred, '-H', helpf, base], OTHER + ['DOWN', 'DOWN', 'ENTER',
    'wait', 'DOWN', 'ENTER', 'wait', 'UP', 'text:' + newa, 'DOWN', 'DOWN',
    'DOWN', 'INS', 'wait', 'C-s', 'wait', 'wait'], rows=30)
data = open(newa + '.doc', 'rb').read() if os.path.exists(newa + '.doc') \
    else b''
check('the new document is a copy of the template that was chosen: its '
    'description, paper, text size and blocks, and none of the sample '
    'text', b'United Kingdom' in data and b'DP14,8661,4331' in data and
    b'PS14' in data and b'BGSender' in data and b'BGRecipient' in data and
    b'Strata Sample Company' not in data and b'Dear Ms. Example' not in data,
    data[:300])

# The built-in choices of the list still make documents.
newu = os.path.join(work, 'plainu')
d = tui.session([stred, '-H', helpf, base], NEW + ['DOWN', 'text:' + newu,
    'DOWN', 'F2', 'wait', 'ENTER', 'wait', 'DOWN', 'ENTER', 'wait', 'show'], rows=30)
check('Unstructured in the list of the field puts that word in the field',
    'Unstructured' in d[0] and 'Template' in d[0], d[0])

# ---- (b) a name typed in the field -----------------------------------------
tdir = os.path.join(tmp, 'places')
os.makedirs(tdir)
shutil.copy(os.path.join(shipped, 'envelope-uk-dl.doc'),
            os.path.join(tdir, 'envelope-uk-dl.doc'))
os.environ['STRATA_TEMPLATES'] = tdir
newb = os.path.join(work, 'byname')
tui.session([stred, '-H', helpf, base], NEW + ['ESC', 'wait', 'DOWN',
    'text:' + newb, 'DOWN', 'END'] + ['BKSP'] * 11 + ['text:envelope-uk-dl',
    'INS', 'wait',
    'C-s', 'wait', 'wait'], rows=30)
data = open(newb + '.doc', 'rb').read() if os.path.exists(newb + '.doc') \
    else b''
check('a name typed in the field is found in the places of the templates '
    '(the formatting of the template is there, and no sample text)',
    b'PS14' in data and b'BGRecipient' in data and
    b'Strata Sample Company' not in data, data[:200])
# A new document from a template starts the cursor in its first block: what
# is typed goes into the sender of a letter or an envelope (the recipient of
# an envelope that has no sender) and nowhere else.
tdir2 = os.path.join(tmp, 'places2')
os.makedirs(tdir2)
os.environ['STRATA_TEMPLATES'] = tdir2
for tname, fmt in (('envelope-us-ca-9', b'BGSender'),
                   ('envelope-us-ca-monarch', b'BGSender'),
                   ('envelope-jp-yougata-4', b'BGRecipient'),
                   ('letter-us', b'BGSender')):
    shutil.copy(os.path.join(shipped, tname + '.doc'),
                os.path.join(tdir2, tname + '.doc'))
    newc = os.path.join(work, 'typed-' + tname)
    tui.session([stred, '-H', helpf, base], NEW + ['ESC', 'wait', 'DOWN',
        'text:' + newc, 'DOWN', 'END'] + ['BKSP'] * 11 + ['text:' + tname,
        'INS', 'wait', 'wait', 'text:ZZZ', 'C-s', 'wait', 'wait'], rows=30)
    data = open(newc + '.doc', 'rb').read() if os.path.exists(
        newc + '.doc') else b''
    at = data.find(b'ZZZ')
    check('%s: a new document from the template has the cursor in the first '
        'block: ZZZ typed at once is in the %s block and in no other'
        % (tname, fmt[2:].decode()),
        at > 0 and data.count(b'ZZZ') == 1 and
        data.rfind(b'\r\nBG', 0, at) == data.rfind(fmt, 0, at) - 2 and
        data.count(b'Strata Sample Company') == 0, data[-300:])
del os.environ['STRATA_TEMPLATES']

# A template of the user can hold text, such as a return address: a new
# document made from it holds that text, with the cursor at the start of the
# first block; a template that comes with the program holds none, and the
# document made from it holds none (the control).
tdir3 = os.path.join(tmp, 'places3')
os.makedirs(tdir3)
os.environ['STRATA_TEMPLATES'] = tdir3
mine = open(os.path.join(shipped, 'envelope-us-ca-9.doc'), 'rb').read()
at = mine.index(b'BGSender')
at = mine.index(b'\r\nT', at)
end = mine.index(b'\x00', at)
mine = (mine[:at] + b'\r\nTMy Return Address Co\x1d12 Oak Road\x1dTown, ST 00000' +
        mine[end:])
open(os.path.join(tdir3, 'my-envelope.doc'), 'wb').write(mine)
shutil.copy(os.path.join(shipped, 'envelope-us-ca-9.doc'),
            os.path.join(tdir3, 'shipped-envelope.doc'))
for tname, expect in (('my-envelope', True), ('shipped-envelope', False)):
    newt = os.path.join(work, 'text-' + tname)
    tui.session([stred, '-H', helpf, base], NEW + ['ESC', 'wait', 'DOWN',
        'text:' + newt, 'DOWN', 'END'] + ['BKSP'] * 11 + ['text:' + tname,
        'INS', 'wait', 'wait', 'text:ZZZ', 'C-s', 'wait', 'wait'], rows=30)
    data = open(newt + '.doc', 'rb').read() if os.path.exists(
        newt + '.doc') else b''
    z = data.find(b'ZZZ')
    held = b'My Return Address Co' in data
    check('%s: a new document made from a template %s the text of the '
        'template%s' % (tname, 'holds' if expect else 'has none of',
        ', and the cursor starts before it' if expect else ''),
        held == expect and z > 0 and data.count(b'ZZZ') == 1 and
        (not expect or data.find(b'My Return Address Co') == z + 3),
        data[-300:])
del os.environ['STRATA_TEMPLATES']

# ---- (c) the Document Manager ------------------------------------------------
mgr = os.path.join(tmp, 'mgr')
os.makedirs(os.path.join(mgr, 'tpl'))
shutil.copy(os.path.join(shipped, 'envelope-uk-dl.doc'),
            os.path.join(mgr, 'tpl', 'envelope-uk-dl.doc'))
os.chdir(mgr)
DM = [strprog, '-H', helpf, mgr]
# A new name with no template in a place: the type is asked, then the panel
# New File has the field Template.
d = tui.session(DM, ['ENTER', 'text:dmnew', 'INS', 'wait', 'ENTER', 'wait',
    'show'], rows=30)
scr = d[0]
check('the Document Manager: the New File panel has the field Template '
    'with the type that was chosen',
    'Template' in scr and 'Structured' in scr and 'Description' in scr, scr)
d = tui.session(DM, ['ENTER', 'text:dmnew', 'INS', 'wait', 'ENTER', 'wait',
    'F2', 'wait', 'ENTER', 'wait', 'show'], rows=30)
scr = d[0]
check('the Document Manager: F2 on the field Template lists the built-in '
    'settings and a way to another file',
    'Unstructured' in scr and 'Another file' in scr, scr)
d = tui.session(DM, ['ENTER', 'text:dmnew', 'INS', 'wait', 'ENTER', 'wait',
    'F2', 'wait', 'ENTER', 'wait', 'DOWN', 'DOWN', 'ENTER', 'wait', 'DOWN', 'ENTER', 'wait',
    'DOWN', 'ENTER', 'wait', 'show'], rows=30)
scr = d[0]
check('the Document Manager: the way to another file reaches the template '
    'file and puts its path in the field',
    'envelope-uk-dl.doc' in scr and 'Template' in scr, scr)
d = tui.session(DM, ['ENTER', 'text:dmnew', 'INS', 'wait', 'ENTER', 'wait',
    'F2', 'wait', 'ENTER', 'wait', 'DOWN', 'DOWN', 'ENTER', 'wait', 'DOWN', 'ENTER', 'wait',
    'DOWN', 'ENTER', 'wait', 'INS', 'wait', 'wait', 'wait', 'show'],
    rows=30)
scr = d[0]
check('the Document Manager: the Editor that it starts has the template '
    'applied: the place of the sender is there (a Whitespace marker) and no '
    'sample text', '[WS' in scr and 'Strata Sample Company' not in scr, scr)
# a template name that does not exist is refused in the panel
d = tui.session(DM, ['ENTER', 'text:dmnew2', 'INS', 'wait', 'ENTER', 'wait',
    'END', 'text:nosuchtemplate', 'INS', 'wait', 'show'], rows=30)
scr = d[0]
check('the Document Manager: a template that does not exist is refused',
    'Cannot find the template file' in scr, scr)
# the built-in choice makes an Editor with no template text
d = tui.session(DM, ['ENTER', 'text:dmnew3', 'INS', 'wait', 'ENTER', 'wait',
    'INS', 'wait', 'wait', 'wait', 'show'], rows=30)
scr = d[0]
check('the Document Manager: the built-in type starts an Editor with no '
    'template text and no place of an address',
    '[WS' not in scr and 'dmnew3' in scr, scr)
# the name on the command line is a new document too: the same panel, and
# with templates in a place the same list first
d = tui.session([strprog, '-H', helpf, 'cmdnew'], ['ENTER', 'INS', 'wait',
    'ENTER', 'wait', 'show'], rows=30)
scr = d[0]
check('the Document Manager: a name on the command line asks for the '
    'template in the New File panel',
    'Template' in scr and 'Description' in scr and 'Author' in scr, scr)
os.environ['STRATA_TEMPLATES'] = os.path.join(tmp, 'places')
d = tui.session([strprog, '-H', helpf, 'cmdnew'], ['ENTER', 'INS', 'wait',
    'DOWN', 'DOWN', 'wait', 'show'], rows=30)
scr = d[0]
check('the Document Manager: a name on the command line shows the list of '
    'the templates, with their descriptions',
    'envelope-uk-dl' in scr and 'DL envelope' in scr and
    'Structured' in scr, scr)
del os.environ['STRATA_TEMPLATES']
# an existing name opens the document with no list and no panel
open(os.path.join(mgr, 'exists.doc'), 'wb').write(
    open(os.path.join(shipped, 'letter-us.doc'), 'rb').read())
d = tui.session(DM, ['ENTER', 'text:exists', 'INS', 'wait', 'wait', 'wait',
    'show'], rows=30)
scr = d[0]
check('the Document Manager: an existing name opens it with no panel',
    'Template' not in scr and 'Description' not in scr, scr)

shutil.rmtree(tmp, ignore_errors=True)
print('tplfield: %s' % ('all passed' if fails == 0 else '%d failed' % fails))
sys.exit(1 if fails else 0)
