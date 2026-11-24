#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Run the Editor with tests/memshim.c through the steps of measure.py.

usage: python3 -I -B tests/memdrive.py SHIM.so STRED DOC DUMP
The dump (appended) has one block of lines for each step: open, end, search,
save; tests/memsites.py names the sites.
"""
import os, sys, signal, time, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure, bigtui
shim, stred, doc, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
work = tempfile.mkdtemp(prefix='memdrive')
os.environ['LD_PRELOAD'] = os.path.abspath(shim)
os.environ['MEMSHIM_OUT'] = out
open(out, 'w').close()
path = os.path.join(work, 'sh.doc')
shutil.copy(doc, path)
t0 = time.time()
r = bigtui.Run([stred, path])
measure.quiet_pty(r, t0, until=lambda s: 'EDIT' in s.split('\n')[0])
def mark(name):
    time.sleep(0.3)
    open(out, 'a').write('NAME %s\n' % name)
    os.kill(r.pid, signal.SIGUSR1)
    time.sleep(0.5)
mark('open')
measure.send(r, 'C-END'); measure.quiet_pty(r, time.time()); mark('end')
measure.send(r, 'C-HOME'); measure.quiet_pty(r, time.time())
measure.send(r, 'F9'); measure.quiet_pty(r, time.time())
measure.send(r, 'text:' + measure.NEEDLE); measure.quiet_pty(r, time.time())
measure.send(r, 'INS'); measure.quiet_pty(r, time.time()); mark('search')
measure.send(r, 'text:x'); measure.quiet_pty(r, time.time())
measure.send(r, 'C-s'); measure.quiet_pty(r, time.time(), until=lambda s: 'Saved' in s); mark('save')
print('VmHWM', measure.vm(r.pid, 'VmHWM'), 'VmRSS', measure.vm(r.pid, 'VmRSS'))
r.stop()
