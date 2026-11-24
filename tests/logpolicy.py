#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The tests of the logging policy (docs/LOGGING.txt): no program writes a
# log message to the console in any build, the messages go to the socket of
# the log of the run (here a stand-in for the collector of str receives
# them), and what the Viewer must tell the Editor goes to the report file.
# The program is run as
#
#     python3 -I -B tests/logpolicy.py BUILDDIR
#
# where BUILDDIR holds term/ (the build of one type; the type is the value
# of STRATA_BUILD_TYPE, debug when it is not set).  The tests of the header
# build_type.h compile small programs with each of the three defines.

import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(os.path.dirname(here), 'src')
build = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else ''
btype = os.environ.get('STRATA_BUILD_TYPE', 'debug')
tmp = tempfile.mkdtemp(prefix='logpolicy.')
fails = 0


def abstract_name(prefix):
    """The name of a socket in the abstract namespace of Linux: it is not a
    file, so the length of TMPDIR does not matter (the name of a Unix
    socket that is a file has at most 107 characters).  The programs are
    given the name with an @ in front, as log.c reads it; bind takes the
    same name with a zero byte in front."""
    return '%s%d-%d' % (prefix, os.getpid(), int(time.time() * 1000) % 100000)


def bind_abstract(sock, name):
    sock.bind('\0' + name)
    return '@' + name


def check(name, cond, extra=''):
    global fails
    if cond:
        print('ok   ' + name)
    else:
        fails += 1
        print('FAIL ' + name + (': ' + str(extra) if extra != '' else ''))


def compile_run(defines, body):
    """Compiles a program that includes build_type.h; returns the output."""
    cfile = os.path.join(tmp, 'bt.c')
    exe = os.path.join(tmp, 'bt')
    with open(cfile, 'w') as f:
        f.write('#include <stdio.h>\n#include "build_type.h"\n'
                'int main(void)\n{\n%s\nreturn 0;\n}\n' % body)
    cc = subprocess.run(['cc', '-std=c11', '-Wall', '-Werror',
                         '-iquote', src] + defines + [cfile, '-o', exe],
                        capture_output=True, text=True)
    if cc.returncode != 0:
        return None, cc.stderr
    run = subprocess.run([exe], capture_output=True, text=True)
    return run.stdout, cc.stderr


# ---- build_type.h ---------------------------------------------------------
PRINT = ('printf("%d %d %d %d\\n", strata_debug, strata_release, '
         'strata_asan, strata_log_detail);')
for define, want in (('STRATA_DEBUG', '1 0 0 1'),
                     ('STRATA_RELEASE', '0 1 0 0'),
                     ('STRATA_ASAN', '0 0 1 1')):
    out, err = compile_run(['-D' + define], PRINT)
    check('header: -D%s gives the constants %s' % (define, want),
          out is not None and out.strip() == want, (out, err))

out, err = compile_run([], PRINT)
check('header: no define is an invalid build (#error)',
      out is None and 'invalid build' in err, (out, err))

out, err = compile_run(['-DSTRATA_RELEASE'],
                       'if (strata_debug) { printf("debug code\\n"); }')
check('header: a guarded block is still compiled and type checked in '
      'release', out is not None and out == '', (out, err))

out, err = compile_run(['-DSTRATA_RELEASE'],
                       'if (strata_debug) { int x = "not an int"; '
                       'printf("%d", x); }')
check('header: a type error in a guarded block stops a release build',
      out is None, (out, err))

# ---- the sources do not test the build macros -----------------------------
bad = []
for dirpath, dirs, files in os.walk(os.path.dirname(here)):
    if '/.git' in dirpath or '/tests/data' in dirpath:
        continue
    for name in files:
        if not name.endswith(('.c', '.h')) or name == 'build_type.h':
            continue
        path = os.path.join(dirpath, name)
        with open(path, errors='replace') as f:
            for n, line in enumerate(f, 1):
                s = line.strip()
                if s.startswith('#') and 'STRATA_' in s and \
                        any(w in s for w in ('DEBUG', 'RELEASE', 'ASAN')):
                    bad.append('%s:%d' % (path, n))
check('sources: no #if or #ifdef on STRATA_DEBUG, STRATA_RELEASE, '
      'STRATA_ASAN outside build_type.h', bad == [], bad)

# ---- the Viewer -----------------------------------------------------------


def dvi(specials):
    """A DVI file of one page; each element of specials is the bytes of an
    xxx special."""
    pre = b'\xf7\x02' + struct.pack('>III', 25400000, 473628672, 1000) + \
        b'\x00'
    bop = b'\x8b' + struct.pack('>10i', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0) + \
        struct.pack('>i', -1)
    body = b''
    for s in specials:
        body += b'\xef' + bytes([len(s)]) + s
    page = bop + body + b'\x8c'
    bop_at = len(pre)
    post_at = len(pre) + len(page)
    post = b'\xf8' + struct.pack('>I', bop_at) + \
        struct.pack('>III', 25400000, 473628672, 1000) + \
        struct.pack('>II', 0, 0) + struct.pack('>HH', 1, 1)
    tail = b'\xf9' + struct.pack('>I', post_at) + b'\x02'
    data = pre + page + post + tail
    data += b'\xdf' * (4 + (-len(data) - 4) % 4)
    return data


class Sink:
    """A stand-in for the collector of str: a Unix datagram socket whose
    name is given to the programs in STRATA_LOG_SOCKET."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.path = bind_abstract(self.sock, abstract_name('logpolicy-'))
        self.sock.setblocking(False)

    def take(self):
        """The messages that have arrived, as text."""
        out = []
        while True:
            try:
                out.append(self.sock.recv(4096).decode('latin-1'))
            except BlockingIOError:
                return out


sink = Sink()


def viewer(path, extra=None):
    """Runs the Viewer without a display; returns (status, stderr, report,
    messages) where messages are those that it sent to the log of the run."""
    report = os.path.join(tmp, 'report.txt')
    if os.path.exists(report):
        os.unlink(report)
    sink.take()
    env = dict(os.environ)
    env['DISPLAY'] = ':65000'
    env['STRATA_LOG_SOCKET'] = sink.path
    cmd = [os.path.join(build, 'term', 'dviview'), path, '--report', report]
    run = subprocess.run(cmd + (extra or []), capture_output=True, text=True,
                         env=env, timeout=60)
    text = ''
    if os.path.exists(report):
        with open(report) as f:
            text = f.read()
    return run.returncode, run.stderr, text, sink.take()


def write(name, data):
    path = os.path.join(tmp, name)
    with open(path, 'wb') as f:
        f.write(data)
    return path


if build != '' and os.path.exists(os.path.join(build, 'term', 'dviview')):
    st, err, rep, msgs = viewer(write('good.dvi', dvi([b'foo bar baz'])))
    check('viewer: an unsupported special is in the report file',
          "note: special 'foo' is not supported" in rep, rep)
    check('viewer: the report says the page, the position and the text',
          'first on page 1' in rep and 'in from the left' in rep and
          'in from the top' in rep and 'text "foo bar baz"' in rep, rep)
    check('viewer: the report names the file and what to do',
          'good.dvi' in rep and 'PDF is not affected' in rep, rep)
    check('viewer: the report is never the empty quote',
          "special ''" not in rep, rep)
    check('viewer: no build writes anything to the console',
          err == '', err)
    check('viewer: the log of the run has the details, with the program, '
          'the file and the line',
          any(m.startswith('<4>dviview[') and 'src/' in m and
              "special 'foo' is not supported" in m and 'good.dvi' in m
              for m in msgs), msgs)

    st, err, rep, msgs = viewer(write('long.dvi', dvi(
        [b'zz ' + b'x' * 50 + b'\x01\\ ' + b'y' * 60])))
    check('viewer: a long text is cut and unprintable bytes are escaped',
          '\\x01' in rep and '...' in rep and
          'y' * 60 not in rep, rep)

    st, err, rep, msgs = viewer(write('empty.dvi', dvi([b'', b'  '])))
    check('viewer: an empty special says nothing at all',
          'is not supported' not in rep and
          "special ''" not in rep and "special ''" not in err and
          not any('special' in m for m in msgs),
          (rep, err, msgs))

    st, err, rep, msgs = viewer(write('bad.dvi', b'this is not a DVI file '
                                      b'at all, it is a line of text.\n'))
    check('viewer: a broken file fails with a status and a report line',
          st == 1 and 'fail: ' in rep and 'is not a DVI file' in rep,
          (st, rep))
    check('viewer: a broken file writes nothing to the console, and the '
          'log of the run has an error with the reason',
          err == '' and any(m.startswith('<3>dviview[') and
                            'is not a DVI file' in m for m in msgs),
          (err, msgs))

    cut = dvi([b'foo'])[:-12]
    st, err, rep, msgs = viewer(write('cut.dvi', cut))
    check('viewer: a cut file fails and the report says to print again',
          st == 1 and 'fail: ' in rep and 'print the document again' in rep
          or 'is not a DVI file' in rep, (st, rep))

    st, err, rep, msgs = viewer(os.path.join(tmp, 'missing.dvi'))
    check('viewer: a missing file fails with the name in the report',
          st == 1 and 'fail: cannot read the file' in rep and
          'missing.dvi' in rep, (st, rep))
    check('viewer: a missing file writes nothing to the console, and the '
          'log of the run names it',
          err == '' and any('cannot read the file' in m and
                            'missing.dvi' in m for m in msgs), (err, msgs))

    st, err, rep, msgs = viewer(write('nodisplay.dvi', dvi([b'foo'])))
    check('viewer: no display fails and the report says which display',
          st == 1 and 'cannot open the X display' in rep and
          ':65000' in rep, (st, rep))
    check('viewer: no display is in the log of the run and not on the '
          'console', err == '' and any('cannot open the X display' in m and
                                       ':65000' in m for m in msgs),
          (err, msgs))

    st = subprocess.run([os.path.join(build, 'term', 'dviview')],
                        capture_output=True, text=True)
    check('viewer: the usage text is command line output and stays',
          st.returncode == 2 and 'usage: dviview' in st.stderr, st)
else:
    print('skip viewer tests: no dviview in the build')

# ---- the Printer: a good run writes nothing to the console ----------------
strprint = os.path.join(build, 'term', 'strprint')
if build != '' and os.path.exists(strprint) and shutil.which('pdflatex'):
    doc = os.path.join(here, 'data', 'print_hyphens.doc')
    out = os.path.join(tmp, 'p.pdf')
    sink.take()
    env = dict(os.environ, STRATA_LOG_SOCKET=sink.path)
    run = subprocess.run([strprint, doc, '-o', out], capture_output=True,
                         text=True, timeout=300, env=env)
    msgs = sink.take()
    check('printer: a good print writes nothing to the console',
          run.returncode == 0 and run.stderr == '' and run.stdout == '' and
          os.path.exists(out), (run.returncode, run.stderr, run.stdout))
    check('printer: the log of the run has the start and the end of the '
          'print, with the program, the file and the line (not in a '
          'release build, which records only warnings and errors)',
          (msgs == [] if btype == 'release' else
           any(m.startswith('<5>strprint[') and 'src/strprint.c:' in m and
               'starts' in m for m in msgs) and
           any('ends normally' in m for m in msgs)), msgs)
    run = subprocess.run([strprint, os.path.join(tmp, 'nothing.doc'), '-o',
                          out], capture_output=True, text=True, timeout=60,
                         env=env)
    msgs = sink.take()
    check('printer: a document that cannot be read ends with a status and '
          'an error in the log of the run',
          run.returncode != 0 and
          any(m.startswith('<3>strprint[') and 'ends with status' in m and
              'nothing.doc' in m for m in msgs), (run.returncode, msgs))
    run = subprocess.run([strprint, doc, '-o', out], capture_output=True,
                         text=True, timeout=300)
    check('printer: without a run (no socket in the environment) nothing is '
          'logged and nothing is written to the console',
          run.returncode == 0 and run.stderr == '' and run.stdout == '',
          (run.returncode, run.stderr))

shutil.rmtree(tmp, ignore_errors=True)
print('logpolicy: %d failures' % fails)
sys.exit(1 if fails else 0)
