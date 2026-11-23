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

"""The paper of the templates and the sample envelopes, as the Paper Size
field of Global, Page shows it.

A size that no named paper has must show Custom with its real width and
length, and never an unrelated named paper; a stored size is matched by its
size, either way round; choosing an envelope stores it with its long side as
the width; choosing Custom keeps the size that the document has; and a panel
that is accepted with no change leaves the file as it was.
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, glob, os, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix='papernames')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.makedirs(os.environ['STRATA_HOME'], mode=0o700)
fails = 0

# What the field must show for every template and every sample envelope.
DL = 'DL Envelope (110 x 220 mm)'
C5 = 'C5 Envelope (162 x 229 mm)'
C6 = 'C6, Yougata 2 Envelope (114 x 162 mm)'
EXPECT = {
    'envelope-au-c5': C5,
    'envelope-au-dl': DL,
    'envelope-de-din5008-dl': DL,
    'envelope-eu-c5': C5,
    'envelope-eu-c65': 'C6/5 Envelope (114 x 229 mm)',
    'envelope-eu-dl': DL,
    'envelope-fr-dl': DL,
    'envelope-jp-yougata-2': C6,
    'envelope-jp-yougata-4': 'Yougata 4 Envelope (105 x 235 mm)',
    'envelope-latam-dl': DL,
    'envelope-uk-c5': C5,
    'envelope-uk-c6': C6,
    'envelope-uk-dl': DL,
    'envelope-us-ca-10': 'Business Envelope (4 1/8 x 9 1/2 in)',
    'envelope-us-ca-9': 'Number 9 Envelope (3 7/8 x 8 7/8 in)',
    'envelope-us-ca-monarch': 'Monarch Envelope (3 7/8 x 7 1/2 in)',
    'letter-a4': 'A4 (210 x 297 mm)',
    'letter-us': 'Letter (8 1/2 x 11 in)',
}
PANEL = ['F12', 'text:G', 'RIGHT', 'ENTER', 'wait']
PAPER_FIELD = ['DOWN'] * 11


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def copy(src, name):
    dst = os.path.join(tmp, name)
    shutil.copy(src, dst)
    return dst


def panel(path, more=None):
    """The screen with the Global Page Layout panel, after the keys more."""
    keys = PANEL + (more or []) + ['show']
    return tui.session([stred, '-H', helpf, path], keys, rows=30)[0]


def paper_line(screen):
    for line in screen.split('\n'):
        if 'Paper Size:' in line:
            rest = line.split('Paper Size:')[1]

            return rest[rest.index('['):rest.rindex(']') + 1]

    return ''


def dp(path):
    for line in open(path, 'rb').read().replace(b'\r', b'\n').split(b'\n'):
        if line.startswith(b'DP'):
            return line.decode()

    return ''


def bytes_of(path):
    return open(path, 'rb').read()


templates = os.path.join(root, 'templates')
samples = os.path.join(root, 'samples', 'envelopes')

for name, want in sorted(EXPECT.items()):
    path = copy(os.path.join(templates, name + '.doc'), name + '.doc')
    got = paper_line(panel(path))
    check('template %s shows [%s]' % (name, want), got == '[%s]' % want, got)

for f in sorted(glob.glob(os.path.join(samples, 'sample-envelope-*.doc'))):
    name = os.path.basename(f)[len('sample-'):-4]
    path = copy(f, os.path.basename(f))
    got = paper_line(panel(path))
    check('sample %s shows the paper of its template' % name,
          got == '[%s]' % EXPECT[name], got)

# A size that belongs to no named paper is Custom with that size, even when
# the index of the record names a paper; a record with no size has the paper
# of its index.
base = copy(os.path.join(templates, 'letter-us.doc'), 'odd.doc')
data = bytes_of(base)
odd = os.path.join(tmp, 'odd-size.doc')
open(odd, 'wb').write(data.replace(b'DP0,8500,11000', b'DP14,5125,5125'))
check('an index that names DL with a size that is not DL shows Custom and '
      'the real size',
      paper_line(panel(odd)) == '[Custom (5 1/8 x 5 1/8 in)]', paper_line(panel(odd)))
nosize = os.path.join(tmp, 'nosize.doc')
open(nosize, 'wb').write(data.replace(b'DP0,8500,11000', b'DP8'))
check('a record with an index and no size shows the paper of the index',
      paper_line(panel(nosize)) == '[A4 (210 x 297 mm)]',
      paper_line(panel(nosize)))
flip = os.path.join(tmp, 'flip.doc')
open(flip, 'wb').write(data.replace(b'DP0,8500,11000', b'DP14,4331,8661'))
check('a named paper stored with its sides the other way round is the '
      'same paper',
      paper_line(panel(flip)) == '[DL Envelope (110 x 220 mm)]',
      paper_line(panel(flip)))

# Accepting the panel with no change leaves the file as it was.
us9 = copy(os.path.join(templates, 'envelope-us-ca-9.doc'), 'keep.doc')
before = bytes_of(us9)
tui.session([stred, '-H', helpf, us9], PANEL + ['INS', 'wait', 'C-s',
            'wait'], rows=30)
check('accepting the panel with no change keeps the stored size',
      bytes_of(us9) == before and dp(us9) == 'DP16,8875,3875', dp(us9))

# Choosing an envelope stores it with the long side as the width.
doc = copy(os.path.join(templates, 'letter-us.doc'), 'pick.doc')
tui.session([stred, '-H', helpf, doc], PANEL + PAPER_FIELD + ['F2', 'wait'] +
            ['DOWN'] * 13 + ['ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'],
            rows=30)
check('choosing the Business Envelope stores the long side as the width',
      dp(doc) == 'DP13,9500,4125', dp(doc))
doc = copy(os.path.join(templates, 'letter-us.doc'), 'pick2.doc')
tui.session([stred, '-H', helpf, doc], PANEL + PAPER_FIELD + ['F2', 'wait'] +
            ['DOWN'] * 14 + ['ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'],
            rows=30)
check('choosing the DL Envelope stores 220 mm as the width',
      dp(doc) == 'DP14,8661,4331', dp(doc))
doc = copy(os.path.join(templates, 'letter-us.doc'), 'pick3.doc')
tui.session([stred, '-H', helpf, doc], PANEL + PAPER_FIELD + ['F2', 'wait'] +
            ['DOWN'] * 8 + ['ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'],
            rows=30)
check('choosing A4 keeps the portrait sides',
      dp(doc) == 'DP8,8268,11693', dp(doc))

# Custom keeps the size that the document has; a document with a named paper
# gets the default custom size.  The size of the first document is one that
# no named paper has.
base9 = bytes_of(os.path.join(templates, 'envelope-us-ca-9.doc'))
doc = os.path.join(tmp, 'cust.doc')
open(doc, 'wb').write(base9.replace(b'DP16,8875,3875', b'DP16,5125,5125'))
tui.session([stred, '-H', helpf, doc], PANEL + PAPER_FIELD + ['F2', 'wait',
            'ENTER', 'wait', 'INS', 'wait', 'F12', 'text:G', 'RIGHT',
            'ENTER', 'wait'] + PAPER_FIELD + ['F2', 'wait'] + ['DOWN'] * 20 +
            ['ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'], rows=30)
check('Custom chosen after another paper keeps the custom size of the '
      'document', dp(doc) == 'DP16,5125,5125', dp(doc))
doc = copy(os.path.join(templates, 'letter-us.doc'), 'cust2.doc')
tui.session([stred, '-H', helpf, doc], PANEL + PAPER_FIELD + ['F2', 'wait'] +
            ['DOWN'] * 20 + ['ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'],
            rows=30)
check('Custom chosen for a named paper has the size 8.5 by 11 inches',
      dp(doc) == 'DP16,8500,11000', dp(doc))
cust = os.path.join(tmp, 'cust.doc')
check('the panel of a document that was set to Custom shows its size',
      paper_line(panel(cust)) == '[Custom (5 1/8 x 5 1/8 in)]',
      paper_line(panel(cust)))

# The Number 9 Envelope is in the list: a document made in the Editor with
# it chosen stores the long side as the width.
doc = copy(os.path.join(templates, 'letter-us.doc'), 'pick9.doc')
tui.session([stred, '-H', helpf, doc], PANEL + PAPER_FIELD + ['F2', 'wait'] +
            ['DOWN'] * 16 + ['ENTER', 'wait', 'INS', 'wait', 'C-s', 'wait'],
            rows=30)
check('choosing the Number 9 Envelope stores 8 7/8 inches as the width',
      dp(doc) == 'DP16,8875,3875', dp(doc))

sys.exit(1 if fails else 0)
