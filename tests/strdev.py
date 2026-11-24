#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The test of scripts/str-dev, the wrapper that starts str and opens a
# second console window that follows the log of the run.  A fake terminal
# program (tests/faketerm.sh) records its arguments, and a fake str
# (tests/fakestr.sh) records its own.  Run it from the directory modernization:
#
#     python3 -I -B tests/strdev.py

import os
import shutil
import subprocess
import sys
import tempfile
import time

here = os.path.dirname(os.path.abspath(__file__))
script = os.path.join(os.path.dirname(os.path.dirname(here)), 'scripts',
                      'str-dev')
fails = 0


def check(name, cond, extra=''):
    global fails
    if cond:
        print('ok   ' + name)
    else:
        fails += 1
        print('FAIL ' + name + (': ' + str(extra) if extra != '' else ''))


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return None


def alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def run(extra_args, env_extra, wait_term=True):
    """Runs str-dev with the fakes; returns a dict of what was recorded."""
    work = tempfile.mkdtemp(prefix='strdev.')
    home = os.path.join(work, 'strata')
    term_rec = os.path.join(work, 'term')
    str_rec = os.path.join(work, 'str')
    os.makedirs(os.path.join(home, 'log'))
    os.makedirs(term_rec)
    os.makedirs(str_rec)
    # A link left from an earlier run: the wrapper removes it.
    os.symlink('run-19990101-000000.log', os.path.join(home, 'log',
                                                       'current.log'))
    env = dict(os.environ)
    for name in ('TERMINAL', 'STR', 'STRATA_BUILD_DIR'):
        env.pop(name, None)
    env.update({
        'STRATA_HOME': home,
        'STR': os.path.join(here, 'fakestr.sh'),
        'FAKESTR_REC': str_rec,
        'FAKETERM_REC': term_rec,
        'FAKESTR_WAIT': '1' if wait_term else '0',
    })
    env.update(env_extra)
    p = subprocess.run([script] + extra_args, env=env, capture_output=True,
                       text=True, timeout=60)
    result = {
        'status': p.returncode,
        'stderr': p.stderr,
        'stdout': p.stdout,
        'home': home,
        'term_args': read(os.path.join(term_rec, 'args')),
        'term_pid': read(os.path.join(term_rec, 'pid')),
        'str_args': read(os.path.join(str_rec, 'args')),
        'link_at_start': (read(os.path.join(str_rec, 'link_at_start')) or
                          '').strip(),
        'work': work,
    }
    return result


fake = os.path.join(here, 'faketerm.sh')

# The window: started with tail -F on the link, closed when str ends.
r = run(['one.doc', '--flag'], {'TERMINAL': fake})
log = os.path.join(r['home'], 'log', 'current.log')
check('str-dev starts str with the arguments it was given',
      r['str_args'] == 'one.doc\n--flag\n', r['str_args'])
check('str-dev opens the window with tail -F on current.log',
      r['term_args'] == '-e\ntail\n-F\n%s\n' % log, r['term_args'])
check('the old link is removed before str starts',
      r['link_at_start'] == 'no', r['link_at_start'])
check('the window is closed when str ends',
      r['term_pid'] is not None and
      not alive(int(r['term_pid'].strip())), r['term_pid'])
check('str-dev ends with status 0 and writes nothing when all goes well',
      r['status'] == 0 and r['stdout'] == '' and r['stderr'] == '',
      (r['status'], r['stderr']))
shutil.rmtree(r['work'], ignore_errors=True)

# The status of str is the status of the script, and the window still closes.
r = run([], {'TERMINAL': fake, 'FAKESTR_STATUS': '3'})
check('the status of str is the status of str-dev',
      r['status'] == 3, r['status'])
check('the window is closed when str fails',
      r['term_pid'] is not None and
      not alive(int(r['term_pid'].strip())), r['term_pid'])
shutil.rmtree(r['work'], ignore_errors=True)

# The option that leaves the window out.
r = run(['--no-window', 'x.doc'], {'TERMINAL': fake}, wait_term=False)
check('--no-window opens no window and still starts str',
      r['term_pid'] is None and r['str_args'] == 'x.doc\n', r)
shutil.rmtree(r['work'], ignore_errors=True)

# No terminal program is found: the script shows so and still starts str.
empty = tempfile.mkdtemp(prefix='nobin.')
for name in ('bash', 'basename', 'dirname', 'mkdir', 'rm', 'cat', 'sleep',
             'seq', 'ln', 'echo', 'printf'):
    path = shutil.which(name)
    if path is not None and not os.path.exists(os.path.join(empty, name)):
        os.symlink(path, os.path.join(empty, name))
r = run(['y.doc'], {'PATH': empty}, wait_term=False)
check('with no terminal program the script shows so and starts str',
      'no terminal program found' in r['stderr'] and
      r['str_args'] == 'y.doc\n' and r['status'] == 0, r)
shutil.rmtree(r['work'], ignore_errors=True)
shutil.rmtree(empty, ignore_errors=True)

# str is not found.
r = run([], {'STR': os.path.join(r['work'], 'nothing'), 'TERMINAL': fake},
        wait_term=False)
check('a str that cannot be found is told and the status is not 0',
      r['status'] != 0, (r['status'], r['stderr']))
shutil.rmtree(r['work'], ignore_errors=True)

# The help text.
p = subprocess.run([script, '--help'], capture_output=True, text=True)
check('--help shows the usage', p.returncode == 0 and
      'Usage: str-dev' in p.stdout and '--no-window' in p.stdout, p)

print('strdev: %d failures' % fails)
sys.exit(1 if fails else 0)
