#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Name the allocation sites of a dump of tests/memshim.c.

usage: python3 -I -B tests/memsites.py DUMP BINARY [PHASE [LINES]]
Prints, for each phase of the dump (or one), the megabytes that were alive per
site, as the first four functions of the call.  Needs nm and readelf.
"""
import sys, subprocess, bisect, re
dump, binary = sys.argv[1], sys.argv[2]
phase = sys.argv[3] if len(sys.argv) > 3 else None
top = int(sys.argv[4]) if len(sys.argv) > 4 else 25
nm = subprocess.run(['nm', '-n', '--defined-only', binary], capture_output=True, text=True).stdout.split('\n')
syms = [(int(l.split()[0], 16), l.split()[2]) for l in nm if len(l.split()) == 3 and l.split()[1] in 'tTwW']
addrs = [a for a, _ in syms]
rl = subprocess.run(['readelf', '-lW', binary], capture_output=True, text=True).stdout
seg = None
for l in rl.split('\n'):
    p = l.split()
    if len(p) > 7 and p[0] == 'LOAD' and 'E' in p[6]:
        seg = (int(p[2], 16), int(p[1], 16))   # vaddr, offset
base = None; cur = None; phases = {}; order = []
for line in open(dump):
    if line.startswith('MAPS'):
        m = re.match(r'MAPS ([0-9a-f]+)-[0-9a-f]+ r-xp ([0-9a-f]+)', line)
        start, off = int(m.group(1), 16), int(m.group(2), 16)
        base = start - off          # load bias (for offset == file offset)
    elif line.startswith('NAME'):
        cur = line.split()[1]; phases[cur] = []; order.append(cur)
    elif line.startswith('PHASE'):
        phases[cur].append(('PHASE', line.split()))
    elif line.startswith('S '):
        p = line.split()
        phases[cur].append(('S', int(p[1]), int(p[2]), int(p[3]), [int(x, 16) for x in p[4:]]))
def fn(a):
    v = a - base - 0 + (seg[0] - seg[1] if seg else 0)
    i = bisect.bisect_right(addrs, v) - 1
    return syms[i][1] if i >= 0 and v - addrs[i] < 0x20000 else '?%x' % a
for name in order:
    if phase and name != phase: continue
    rows = [r for r in phases[name] if r[0] == 'S']
    ph = [r for r in phases[name] if r[0] == 'PHASE'][0][1]
    print('== %s: live %.1f MB, peak %.1f MB' % (name, int(ph[2]) / 1048576, int(ph[4]) / 1048576))
    agg = {}
    for _, live, cnt, tot, fr in rows:
        key = ' < '.join(fn(a) for a in fr[:4])
        k = agg.setdefault(key, [0, 0]); k[0] += live; k[1] += cnt
    for key, (live, cnt) in sorted(agg.items(), key=lambda kv: -kv[1][0])[:top]:
        print('  %8.2f MB  %9d allocs  %s' % (live / 1048576, cnt, key))
