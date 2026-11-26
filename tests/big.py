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

"""Tests with a synthetic document of more than a thousand pages.

usage: big.py quick|full BINDIR WORKDIR [CHAPTERS]

BINDIR is the build directory of the terminal programs (the one that holds
stred, str, strprint, dviview, roundtrip and bigtest).  The test makes the
document with tests/mkbig.py in WORKDIR and removes what it made at the end.

  quick  (about half a minute)  The document is read and written again with
         no change of a byte; the Editor is run on it in the C test
         (tests/bigtest.c: scrolling, Search, Replace all, Go To section,
         outline, Autosort, typing in the middle, Save and reload) and in a
         pseudo-terminal (tests/bigtui.py).  This part is in "make test".
  full   (about four minutes)  Also the Printer: the whole PDF (more than
         a thousand pages, the contents and the index checked against the
         pages), the same through the Document Manager, a part of it by
         sections, and the DVI file with the Viewer on a display of its own
         (Xvfb).  This part is "make big".

Every step prints its time; the Printer steps also print the largest
resident memory of the program and what it started.
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
root = os.path.dirname(here)
fails = 0
# A debug build and a build with the address checker are slower; the limit
# of a second for a command is for the release build.
SLOW = {"debug": 4, "asan": 12}.get(os.environ.get("STRATA_BUILD_TYPE"), 1)


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


def check(name, cond, shown=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        fails += 1
        if shown != "":
            print(shown)
    sys.stdout.flush()


def timed(argv, env=None, cwd=None):
    """Run a program; return (seconds, peak memory in KB, status, output)."""
    t0 = time.time()
    p = subprocess.Popen(argv, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, env=env, cwd=cwd)
    out = p.stdout.read()
    _, status, usage = os.wait4(p.pid, 0)
    return (time.time() - t0, usage.ru_maxrss, os.waitstatus_to_exitcode(
        status), out.decode("utf-8", "replace"))


def pages_of(pdf):
    p = subprocess.run(["pdfinfo", pdf], capture_output=True, text=True)
    m = re.search(r"Pages:\s+(\d+)", p.stdout)
    return int(m.group(1)) if m else 0


def quick(bindir, work):
    doc = os.path.join(work, "big.doc")
    t, mem, st, out = timed([bindir + "/roundtrip", doc])
    check("roundtrip: the document is read and written again unchanged "
          "(%.2f s, %d KB)" % (t, mem), st == 0, out)
    env = dict(os.environ, BIGTEST_NEEDLE_AFTER="70")
    t, mem, st, out = timed([bindir + "/bigtest", doc, "Zyzzyvaquux",
                             str(1000 * SLOW)],
                            env=env)
    print(out.rstrip())
    check("bigtest: the Editor commands on the big document (%.1f s, "
          "%d KB)" % (t, mem), st == 0)
    t, mem, st, out = timed([sys.executable, "-I", "-B",
                             here + "/bigtui.py", bindir + "/stred", doc,
                             "Zyzzyvaquux", str(1.0 * SLOW), "25.3"])
    print(out.rstrip())
    check("bigtui: the Editor in a terminal on the big document (%.1f s)"
          % t, st == 0)


def full(bindir, work):
    doc = os.path.join(work, "big.doc")
    pdf = os.path.join(work, "big.pdf")
    for tool in ("pdflatex", "pdfinfo", "pdftotext"):
        if shutil.which(tool) is None:
            print("SKIP: %s is not installed" % tool)
            return
    t, mem, st, out = timed([bindir + "/strprint", doc, "--docdir", work,
                             "--progress", "-o", pdf])
    passes = len(re.findall(r"@@strprint pass \d+", out))
    print("Printer: %.1f s, largest process %d KB, %d LaTeX passes"
          % (t, mem, passes))
    n = pages_of(pdf)
    check("Printer: the whole document as a PDF of %d pages" % n,
          st == 0 and n >= 1000, out[-500:])
    t, mem, st, out = timed([sys.executable, "-I", "-B",
                             os.path.join(root, "manual", "checkpdf.py"),
                             pdf, doc + ".meta", "1000"])
    print(out.rstrip())
    check("PDF: contents and index agree with the pages, no blank page, "
          "no unresolved reference", st == 0)

    # A part of the document, the way the Document Manager requests it.
    part = os.path.join(work, "part.pdf")
    env = dict(os.environ, STRATA_HOME=os.path.join(work, "home"))
    os.makedirs(env["STRATA_HOME"], exist_ok=True)
    t, mem, st, out = timed([bindir + "/stred", "--print", doc, "--from",
                             "5", "--to", "6", "--no-error-log", "-o", part],
                            env=env)
    if st != 0 or not os.path.exists(part):
        part = os.path.join(work, "big.pdf")
        n2 = 0
    else:
        n2 = pages_of(part)
    print("Print of sections 5 to 6: %.1f s, %d KB, %d pages" % (t, mem, n2))
    check("Print: a range of sections of the big document", 0 < n2 < n // 4,
          out[-300:])

    # The DVI file and the Viewer.
    dvi = os.path.join(work, "big.dvi")
    t, mem, st, out = timed([bindir + "/strprint", doc, "--docdir", work,
                             "--dvi", "-o", dvi])
    print("Printer, DVI: %.1f s, largest process %d KB" % (t, mem))
    check("Printer: the DVI file", st == 0 and os.path.exists(dvi), out[-300:])
    viewer(bindir, dvi)


def viewer(bindir, dvi):
    """Open the DVI file in the Viewer on a display of its own."""
    for tool in ("Xvfb", "xdotool"):
        if shutil.which(tool) is None:
            print("SKIP: the Viewer needs %s" % tool)
            return
    if not os.path.exists(bindir + "/dviview"):
        print("SKIP: the Viewer was not built")
        return

    # A release build writes no log, and the pages that the Viewer drew are
    # read from its log, so the page by page part of this test needs a build
    # that logs.
    if os.environ.get("STRATA_BUILD_TYPE") == "release":
        print("SKIP: the Viewer of a release build writes no log")
        return
    rfd, wfd = os.pipe()
    xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(wfd), "-screen", "0",
                             "1280x1024x24"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, pass_fds=(wfd,))
    os.close(wfd)
    num = b""
    while not num.endswith(b"\n"):
        chunk = os.read(rfd, 16)
        if chunk == b"":
            check("Viewer: the display starts", False)
            return
        num += chunk
    os.close(rfd)
    log = os.path.join(os.path.dirname(dvi), "viewer.log")
    sink = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sockpath = bind_abstract(sink, abstract_name("viewer-"))
    sink.settimeout(0.2)
    running = [True]

    def collect():
        # The Viewer writes its messages to the log of a run, a socket: this
        # stands in for the collector of str and writes one line for each.
        with open(log, "w", buffering=1) as out:
            while running[0]:
                try:
                    data = sink.recv(4096).decode("latin-1")
                except (socket.timeout, OSError):
                    continue
                out.write(re.sub(r"^<\d+>", "", data).rstrip() + "\n")

    reader = threading.Thread(target=collect)
    reader.start()
    env = dict(os.environ, DISPLAY=":" + num.decode().strip(),
               DVIVIEW_TIMING="1", STRATA_LOG_SOCKET=sockpath)
    try:
        t0 = time.time()
        with open(os.devnull, "w") as lf:
            v = subprocess.Popen([bindir + "/dviview", dvi], env=env,
                                 stdout=lf, stderr=lf)
            win = []
            while win == [] and time.time() - t0 < 30:
                win = subprocess.run(["xdotool", "search", "--onlyvisible",
                                      "--name", "big.dvi"], env=env,
                                     capture_output=True,
                                     text=True).stdout.split()
                time.sleep(0.05)
            opened = time.time() - t0
            print("Viewer: the window after %.2f s" % opened)
            check("Viewer: opens the DVI of the big document in under "
                  "5 seconds", win != [] and opened < 5.0)

            def keys(*names):
                subprocess.run(["xdotool", "key", "--window", win[0]] +
                               list(names), env=env, capture_output=True)
                time.sleep(0.4)

            for _ in range(10):
                keys("Next")
            keys("End")
            for _ in range(4):
                keys("g")
                time.sleep(1.0)
                for digit in "500":
                    keys(digit)
                keys("Return")
                time.sleep(0.5)
                with open(log) as lf2:
                    if "dviview: page 500 has" in lf2.read():
                        break
            keys("Home")
            keys("q")
            try:
                v.wait(timeout=10)
            except subprocess.TimeoutExpired:
                v.terminate()
                v.wait()
        with open(log) as lf:
            lines = lf.read()
        shown = [int(x) for x in re.findall(r"dviview: page (\d+) has", lines)]
        times = [float(x) for x in re.findall(r"drawn in ([\d.]+) ms", lines)]
        print("Viewer: pages drawn %s" % shown[:30])
        print("Viewer: slowest page %.0f ms" % (max(times) if times else -1))
        check("Viewer: page to page, to the last page, to page 500 and "
              "back to the first", 500 in shown and shown[-1] == 1 and
              all(p in shown for p in range(2, 12)) and max(shown) > 500)
        check("Viewer: no page takes more than a second",
              times != [] and max(times) < 1000.0)
    finally:
        running[0] = False
        reader.join()
        sink.close()
        xvfb.terminate()
        xvfb.wait()


def main():
    mode, bindir, work = sys.argv[1], sys.argv[2], sys.argv[3]
    nchap = sys.argv[4] if len(sys.argv) > 4 else "45"
    t0 = time.time()
    shutil.rmtree(work, ignore_errors=True)
    r = subprocess.run([sys.executable, "-I", "-B",
                        here + "/mkbig.py", work, nchap])
    check("mkbig: the document is made", r.returncode == 0)
    if r.returncode == 0:
        quick(bindir, work)
        if mode == "full":
            full(bindir, work)
    shutil.rmtree(work, ignore_errors=True)
    print("big.py %s: %s in %.0f s" % (mode, "FAILED" if fails else "ok",
                                       time.time() - t0))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
