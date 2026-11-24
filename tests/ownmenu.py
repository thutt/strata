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

"""The menu that the Editor shows when it is started with no document.

It has four entries and every one of them works: the list of documents,
Maintain Dictionaries and Compare Document Versions open their panels, and
Quit ends the program.  There is no entry that only shows a notice.

usage: STRED=path/to/stred python3 -I -B tests/ownmenu.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tui  # noqa: E402

BUILD = os.environ.get('STRATA_BUILD_DIR', '')
STRED = os.environ.get('STRED', os.path.join(BUILD, 'term', 'stred'))
HELP = os.environ.get('STRATA_HELP', os.path.join(BUILD, 'strata.hlp'))
FAILS = []


def check(name, ok, detail=None):
    print(('ok   ' if ok else 'FAIL ') + name)

    if not ok:
        FAILS.append(name)

        if detail is not None:
            print('     ' + str(detail))


def main():
    if not os.path.exists(STRED):
        print('SKIP: build the programs first')

        return 0

    d = tui.session([STRED, '-H', HELP],
                    ['wait', 'show', 'DOWN', 'INS', 'wait', 'show', 'ESC',
                     'DOWN', 'DOWN', 'INS', 'wait', 'show'], rows=24)
    check('the menu has four entries and no Setup',
          all(w in d[0] for w in ('Edit / Preview / Print Document',
                                  'Maintain Dictionaries',
                                  'Compare Document Versions', 'Quit')) and
          'Setup' not in d[0], d[0])
    check('Maintain Dictionaries opens its panel',
          'Spelling, your words' in d[1], d[1])
    check('Compare Document Versions opens its panel',
          'First Document:' in d[2] and 'Second Document:' in d[2], d[2])
    check('no entry says that it is not implemented',
          all('not implemented' not in s and 'not available' not in s
              for s in d), d[1])

    print('ownmenu: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
