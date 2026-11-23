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

"""Survey all the executables of a program under study: NE overlay
description, imports, exports, size."""
import sys as _s; _s.dont_write_bytecode = True
import sys, os, glob, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ne
root = sys.argv[1]
rows = []
for p in sorted(glob.glob(os.path.join(root, '*', '*.exe'))
    + glob.glob(os.path.join(root, '*', '*.com'))):
    d = open(p, 'rb').read()
    rel = os.path.relpath(p, root)
    if d[:2] != b'MZ':
        rows.append((rel, len(d), 'COM', '', '', 0, 0)); continue
    nepos = struct.unpack_from('<I', d, 0x3c)[0] if len(d) > 0x40 else 0
    if nepos + 2 <= len(d) and d[nepos:nepos+2] == b'NE':
        try:
            m = ne.parse(p)
        except Exception as e:
            rows.append((rel, len(d), 'NE?', str(e), '', 0, 0)); continue
        desc = m['nonres'][0][1] if m['nonres'] else (m['res'][0][1]
            if m['res'] else '')
        code = sum(s['len'] for s in m['segs'])
        rows.append((rel, len(d), 'overlay', desc, ','.join(x
            for x in m['mods'] if x.isprintable()), len(m['res']), code))
    else:
        rows.append((rel, len(d), 'MZ', '', '', 0, 0))
print('%-26s %7s %-8s %4s %7s' % ('file', 'size', 'kind', 'exp', 'code'))
print('    description / imports')
for r in rows:
    print('%-26s %7d %-8s %4d %7d' % (r[0], r[1], r[2], r[5], r[6]))
    if r[3] or r[4]:
        print('    %s / %s' % (r[3], r[4]))
