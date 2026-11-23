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

"""Measure the memory, the time and the disk that Strata needs.

This is a benchmark and not a test: the smoke runner never starts it.  Its
numbers are written to docs/MEASUREMENTS.txt.

usage: python3 -I -B tests/measure.py text   OUTFILE WORDS
       python3 -I -B tests/measure.py multipass BUILD_DIR OUTDIR RESULTS [PAGES,...]
       python3 -I -B tests/measure.py gen    TEXT OUTDIR [PAGES,PAGES,...]
       python3 -I -B tests/measure.py term   BUILD_DIR DOCDIR RESULTS [RUNS]
       python3 -I -B tests/measure.py x11    BUILD_DIR DOCDIR RESULTS [RUNS]
       python3 -I -B tests/measure.py print  BUILD_DIR DOCDIR RESULTS [RUNS]
       python3 -I -B tests/measure.py disk   BUILD_DIR PREFIX
       python3 -I -B tests/measure.py compare BASE_RESULTS NEW_RESULTS
       python3 -I -B tests/measure.py report RESULTS

gen reads a plain text (the owner's Mark Twain collection is the one used) and
writes, for every size in pages, the documents OUTDIR/u-PAGES.doc (one
unstructured block per paragraph) and OUTDIR/s-PAGES.doc (a structured
document: one section of about 25 pages for every chapter, and a section
headline).  A page is WORDS_PER_PAGE words, the number that the Printer
makes at the default layout (the Printer is run on a sample to check it).  The
last paragraph holds the word NEEDLE, which the searches look for.  The
Mark Twain file holds 2.8 million words, which print as about 3,650 pages, so
the largest size (3800 pages) is the whole file: there is no text for more.

term and x11 start a fresh Editor for every run and record, for each phase,
the seconds until the screen was drawn and the Editor was quiet, and the peak
resident memory (VmHWM of /proc) at the end of the phase: open, the end of the
document, a search from the start to the needle, and a save after a change.
print runs the Printer to a PDF and records the seconds, the peak memory of
the Printer and of everything it started (the wait4 usage of the child), and
the number of pages of the PDF.  Each line of RESULTS is one JSON record.

Environment: the build directory must hold term/ and x11/ (see scripts/setup);
STRATA_HOME and TMPDIR should point into a scratch directory.  The X11 runs
need Xvfb and xdotool.
"""
import sys as _s; _s.dont_write_bytecode = True
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

WORDS_PER_PAGE = 500
ONE_PAGE_WORDS = 350
NEEDLE = "zqneedlex"
DEFAULT_PAGES = [1, 10, 50, 100, 500, 1000, 2000, 3800]
CHAPTER_PAGES = 25
CRLF = b"\r\n"
EOL = b"\x1d"
HEADER = [b"*R3", b"MR1001", b"PP\\1\\\x00", b"P1C33", b"FHA1", b"FTT5",
          b"DP0,8500,11000"]
SETTLE_LIMIT = 900.0
MAX_PARA_WORDS = 400


def paragraphs_of(path):
    """The paragraphs of a text file: lines joined, blank lines separate."""
    with open(path, "rb") as f:
        text = f.read().decode("ascii", "replace")

    paras = [re.sub(r"\s+", " ", p).strip()
             for p in re.split(r"\n\s*\n", text)]

    return [p for p in paras if p != ""]


def take(paras, words):
    """The first paragraphs that hold about this many words, with the
    needle added to the last one."""
    out = []
    n = 0

    for p in paras:
        if len(p.split()) > MAX_PARA_WORDS:
            continue

        out.append(p)
        n += len(p.split())

        if n >= words:
            break

    out[-1] = out[-1] + " " + NEEDLE
    return out


def block(para):
    return b"T" + para.encode("ascii", "replace") + EOL + b"\x00"


def unstructured(paras):
    """The records of an unstructured document of one block per paragraph."""
    out = list(HEADER) + [b"*B0", b"S"]

    for i, p in enumerate(paras):
        if i != 0:
            out.append(b"B")

        out.append(block(p))

    out.append(b"*E")
    return CRLF.join(out) + CRLF


def structured(paras):
    """A structured document: a section for every CHAPTER_PAGES pages."""
    chunk = CHAPTER_PAGES * WORDS_PER_PAGE
    out = list(HEADER) + [b"*B1", b"S", b"S", b"+"]
    n = 0
    chapter = 0
    open_chapter = False

    for p in paras:
        if (not open_chapter) or n >= chunk:
            chapter += 1
            out.append(b"S")
            out.append(b"T" + (b"Chapter %d" % chapter) + EOL + b"\x00")
            open_chapter = True
            n = 0
        else:
            out.append(b"B")

        out.append(block(p))
        n += len(p.split())

    out += [b"-", b"-", b"*E"]
    return CRLF.join(out) + CRLF


def gen(text, outdir, pages):
    os.makedirs(outdir, exist_ok=True)
    paras = paragraphs_of(text)

    for pg in pages:
        words = ONE_PAGE_WORDS if pg == 1 else pg * WORDS_PER_PAGE
        ps = take(paras, words)
        wc = sum(len(p.split()) for p in ps)

        for kind, fn in (("u", unstructured), ("s", structured)):
            path = os.path.join(outdir, "%s-%d.doc" % (kind, pg))

            with open(path, "wb") as f:
                f.write(fn(ps))

        print("%5d pages: %9d words, %7d paragraphs" % (pg, wc, len(ps)))


def make_text(out, words, seed=7):
    """A text of prose-like paragraphs made from the words of our own
    documents (docs, help, the manual sources): the words are drawn at random
    with their own frequencies, in sentences and in paragraphs of 20 to 150
    words.  The word lengths are those of English text written about this
    subject; this is the substitute for the owner's collection, which is not
    on this machine."""
    import glob
    import random

    root = os.path.dirname(HERE)
    vocab = []

    for pat in ("docs/*.txt", "help/*.txt", "manual/src/*.src", "README.txt"):
        for path in glob.glob(os.path.join(root, pat)):
            with open(path, "rb") as f:
                text = f.read().decode("ascii", "replace")

            vocab += re.findall(r"[A-Za-z][a-z]{1,11}", text)

    vocab = [w.lower() for w in vocab]
    rnd = random.Random(seed)
    lines = []
    n = 0

    while n < words:
        plen = rnd.randint(20, 150)
        sent = []
        left = plen

        while left > 0:
            slen = min(left, rnd.randint(6, 22))
            ws = [rnd.choice(vocab) for _ in range(slen)]
            ws[0] = ws[0].capitalize()

            for i in range(3, slen - 1, 7):
                ws[i] += ","

            ws[-1] += "."
            sent += ws
            left -= slen

        lines.append(" ".join(sent))
        n += len(sent)

    with open(out, "w") as f:
        f.write("\n\n".join(lines) + "\n")

    print("%d words in %d paragraphs, vocabulary %d words" % (
        n, len(lines), len(set(vocab))))


# ---- a print that needs several passes of LaTeX ----------------------------

def marker(t, recs=()):
    return b"\x13" + str(t).encode() + b"(" + b"".join(
        r + CRLF for r in recs) + b"\x13)"


def trec(text):
    return b"T" + text + EOL + b"\x00" + CRLF


def hfblock(left=b"", centre=b"", right=b""):
    """The block of a header or footer: three columns."""
    return (b"S" + CRLF + b"CW26" + CRLF + b"CFJ0" + CRLF + trec(left) +
            b"CW26" + CRLF + b"CFJ2" + CRLF + trec(centre) + b"CFJ1" + CRLF +
            trec(right))


def multipass_doc(pages, paras):
    """A structured document with a table of contents (the print option), an
    index, lists of figures and tables, cross references to sections and to
    pages, a header with the Section marker, footnotes and a long table.

    The text is the paragraphs PARAS.  Chapters of CHAPTER_PAGES pages with
    three sections each; every 12th paragraph has an index entry, every 40th
    a figure, every 70th a table caption, every 25th a footnote, and every
    30th a cross reference (a section, with its tag and headline, or the page
    of a section).  The long table has a row of three columns for each 4
    pages.  Returns (bytes, facts) where facts says what must be in the PDF."""
    words = pages * WORDS_PER_PAGE
    chapters = max(2, pages // CHAPTER_PAGES)
    per_chapter = max(1, len(paras) // chapters)
    per_section = max(1, per_chapter // 3)
    head = (b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF +
            b"P1HB0" + CRLF + b"P1FB0" + CRLF + b"P1C33" + CRLF +
            b"PHB" + CRLF + hfblock(marker(28), b"", b"") +
            b"PFB" + CRLF + hfblock(b"", b"", b"Page " + marker(22)) +
            b"FHA1" + CRLF + b"FTT5" + CRLF + b"DP0,8500,11000" + CRLF +
            b"YCL99" + CRLF + b"YIWN2" + CRLF)
    out = [head + b"*B1" + CRLF + b"S" + CRLF + trec(b"The Title") +
           b"+" + CRLF]
    facts = {"headlines": [], "terms": [], "figures": [], "tables": [],
             "xrefs": 0, "notes": 0}
    n = 0
    k = 0
    sect = []

    def text_block(para, k):
        extra = b""

        if k % 12 == 0:
            term = b"Term%05d" % k
            facts["terms"].append(term.decode())
            extra += marker(3, [b"2" + term + b"\x00"])

        if k % 40 == 0:
            name = b"Figure%05d" % k
            facts["figures"].append(name.decode())
            extra += marker(11, [b"1" + name + b"\x00"])

        if k % 70 == 0:
            name = b"Table%05d" % k
            facts["tables"].append(name.decode())
            extra += marker(29, [b"1" + name + b"\x00"])

        if k % 25 == 0:
            facts["notes"] += 1
            extra += marker(1, [b"S", b"TFootnote text %d" % k + EOL +
                                b"\x00"])

        if k % 30 == 0 and sect != []:
            target = sect[(k // 30) % len(sect)]
            style = 4 if (k // 30) % 2 == 0 else 3
            facts["xrefs"] += 1
            extra += (b" See section " + marker(2, [b"XN" + target + b"\x00",
                      b"XT" + str(style).encode() + b"\x00"]))

        return trec(para.encode("ascii", "replace") + extra)

    for ci in range(chapters):
        title = b"Chapter %d" % (ci + 1)
        facts["headlines"].append(title.decode())
        sect.append(b"%d" % (ci + 1))
        out.append(b"SE0" + CRLF + trec(title))
        out.append(b"+" + CRLF)

        for si in range(3):
            stitle = b"Section %d.%d" % (ci + 1, si + 1)
            facts["headlines"].append(stitle.decode())
            sect.append(b"%d.%d" % (ci + 1, si + 1))
            out.append(b"SE0" + CRLF + trec(stitle))

            for _ in range(per_section):
                if n >= len(paras):
                    break

                out.append(b"B" + CRLF + text_block(paras[n], k))
                n += 1
                k += 1

        out.append(b"-" + CRLF)

    # The long table: one block of three columns for each row.
    rows = max(3, pages // 4)
    out.append(b"SE0" + CRLF + trec(b"The Long Table"))

    for r in range(rows):
        out.append(b"B" + CRLF + b"CW20" + CRLF + trec(b"Row %d" % r) +
                   b"CW20" + CRLF + trec(b"Cell %d a" % r) +
                   trec(b"Cell %d b" % r))

    out.append(b"-" + CRLF + b"*E" + CRLF)
    facts["rows"] = rows
    facts["sections"] = len(sect)

    return b"".join(out), facts


def run_tree(cmd, env=None):
    """Run a command; returns (seconds, exit status, peak MB of the largest
    process of the whole tree (wait4 ru_maxrss of the children), the stderr
    lines with the time of each in seconds from the start)."""
    import select
    import resource  # noqa: F401  (documents the unit: ru_maxrss in KB)

    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         env=env)
    lines = []
    buf = {p.stdout: b"", p.stderr: b""}
    open_fds = [p.stdout, p.stderr]

    while open_fds != []:
        ready, _, _ = select.select(open_fds, [], [], 0.2)

        for f in ready:
            data = os.read(f.fileno(), 65536)

            if data == b"":
                open_fds.remove(f)
                continue

            buf[f] += data

            while b"\n" in buf[f]:
                line, buf[f] = buf[f].split(b"\n", 1)
                lines.append((time.time() - t0, line.decode("latin-1")))

    _, status, ru = os.wait4(p.pid, 0)
    p.returncode = 0
    return (time.time() - t0, os.waitstatus_to_exitcode(status),
            ru.ru_maxrss / 1024.0, lines)


def run_tree_clean(cmd):
    """run_tree in a small process of its own.  The peak memory of a child
    counts the memory that its parent had when it was forked (a Python that
    holds the text of 3600 pages is 69 MB or more), so the measurement is made
    by a small program that starts the command."""
    r = subprocess.run([sys.executable, "-I", "-B", os.path.abspath(__file__),
                        "runtree"] + cmd, capture_output=True, text=True)
    secs, status, peak, lines = json.loads(r.stdout.strip().split("\n")[-1])

    return secs, status, peak, [tuple(x) for x in lines]


def pdf_text(pdf):
    r = subprocess.run(["pdftotext", "-layout", pdf, "-"],
                       capture_output=True, text=True, errors="replace")
    return r.stdout


def multipass(build, outdir, results, pages_list):
    """Print the multi-pass documents and record the passes, the times, the
    memory and what is resolved."""
    os.makedirs(outdir, exist_ok=True)
    strprint = os.path.join(build, "term", "strprint")
    text = os.path.join(outdir, "mp-text.txt")

    if not os.path.exists(text):
        make_text(text, 3000000)

    paras = [p for p in paragraphs_of(text)
             if len(p.split()) <= MAX_PARA_WORDS]

    for pages in pages_list:
        words = pages * WORDS_PER_PAGE
        use = []
        n = 0

        for p in paras:
            use.append(p)
            n += len(p.split())

            if n >= words:
                break

        data, facts = multipass_doc(pages, use)
        doc = os.path.join(outdir, "mp-%d.doc" % pages)
        pdf = os.path.join(outdir, "mp-%d.pdf" % pages)

        with open(doc, "wb") as f:
            f.write(data)

        cmd = [strprint, doc, "-o", pdf, "--no-banner", "--contents", "3",
               "--index", "--list-figures", "--list-tables", "--progress"]
        secs, status, peak, lines = run_tree_clean(cmd)
        passes = [(t, l) for t, l in lines if re.search(r"\bpass \d", l)]
        rec = {"kind": "multipass", "pages_nominal": pages, "doc_kb":
               len(data) // 1024, "status": status, "print_s": round(secs, 2),
               "peak_mb": round(peak, 1), "passes": len(passes),
               "pass_starts_s": [round(t, 2) for t, _ in passes]}
        pdf_pages = 0

        if os.path.exists(pdf):
            info = subprocess.run(["pdfinfo", pdf], capture_output=True,
                                  text=True).stdout
            m = re.search(r"Pages:\s+(\d+)", info)
            pdf_pages = int(m.group(1)) if m else 0
            txt = pdf_text(pdf)
            rec["pdf_pages"] = pdf_pages
            rec["qq"] = txt.count("??")
            rec["toc_missing"] = [h for h in facts["headlines"]
                                  if txt.count(h) < 2][:5]
            rec["index_missing"] = [t for t in facts["terms"]
                                    if txt.count(t) < 1][:5]
            rec["figures_missing"] = [t for t in facts["figures"]
                                      if txt.count(t) < 1][:5]
            rec["tables_missing"] = [t for t in facts["tables"]
                                     if txt.count(t) < 1][:5]
            rec["no_section"] = txt.count("[no section]")
            rec["rows"] = len(set(re.findall(r"Row (\d+)", txt)))
            rec["rows_wanted"] = facts["rows"]
            rec["notes"] = len(set(re.findall(r"Footnote text (\d+)", txt)))
            rec["notes_wanted"] = facts["notes"]
            rec["headlines"] = len(facts["headlines"])
            rec["terms"] = len(facts["terms"])
            rec["xrefs"] = facts["xrefs"]

        rec["stderr_tail"] = [l for _, l in lines[-3:]]
        print(json.dumps(rec))

        with open(results, "a") as f:
            f.write(json.dumps(rec) + "\n")


# ---- the terminal version -------------------------------------------------

def vm(pid, key):
    """A line of /proc/PID/status in megabytes, or None."""
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith(key + ":"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        return None

    return None


def send(r, name):
    """Send a key (a name of tui.KEYS, C-x or text:...) without waiting."""
    import tui

    if name.startswith("text:"):
        os.write(r.fd, name[5:].encode())
    elif name in tui.KEYS:
        os.write(r.fd, tui.KEYS[name].encode())
    elif len(name) == 3 and name[:2] == "C-":
        os.write(r.fd, bytes([ord(name[2]) - 96]))
    else:
        raise ValueError("unknown key " + name)


def quiet_pty(r, t0, stable=0.15, until=None, limit=SETTLE_LIMIT):
    """Wait until the Editor has written nothing and used no CPU for STABLE
    seconds (and UNTIL is true for the screen, when given).  A time of
    silence alone is not enough: the Editor computes without writing for as
    long as a search or a layout takes.  The result is the time from t0 to
    the last moment of output or of work."""
    import select

    last_cpu = cpu_ms(r.pid)
    last_busy = time.time()

    while time.time() - t0 < limit:
        ready, _, _ = select.select([r.fd], [], [], 0.01)

        if ready != []:
            try:
                data = os.read(r.fd, 65536)
            except OSError:
                break

            if data == b"":
                break

            r.scr.feed(data)
            last_busy = time.time()

        c = cpu_ms(r.pid)

        if c - last_cpu > 2.0:
            last_busy = time.time()

        last_cpu = c

        if time.time() - last_busy >= stable and (
                until is None or until(r.scr.dump())):
            break

    return last_busy - t0


def term_run(stred, doc, work):
    """One run of the terminal Editor: a dict of phase results."""
    import bigtui

    res = {}
    path = os.path.join(work, "m.doc")
    shutil.copy(doc, path)
    t0 = time.time()
    r = bigtui.Run([stred, path])
    res["open_s"] = quiet_pty(r, t0, until=lambda s: "EDIT" in
                              s.split("\n")[0])
    res["open_mb"] = vm(r.pid, "VmHWM")

    t = time.time()
    send(r, "C-END")
    res["end_s"] = quiet_pty(r, t)
    res["end_mb"] = vm(r.pid, "VmHWM")

    send(r, "C-HOME")
    quiet_pty(r, time.time())
    send(r, "F9")
    quiet_pty(r, time.time())
    send(r, "text:" + NEEDLE)
    quiet_pty(r, time.time())
    t = time.time()
    send(r, "INS")
    res["search_s"] = quiet_pty(r, t)
    res["search_found"] = NEEDLE in r.screen() and "Ins accepts" not in \
        r.screen()
    res["search_mb"] = vm(r.pid, "VmHWM")

    send(r, "text:x")
    quiet_pty(r, time.time())
    t = time.time()
    send(r, "C-s")
    res["save_s"] = quiet_pty(r, t, until=lambda s: "Saved" in s)
    res["save_ok"] = "Saved" in r.screen()
    res["save_mb"] = vm(r.pid, "VmHWM")
    res["peak_mb"] = vm(r.pid, "VmHWM")
    r.stop()
    return res


# ---- the X11 version ------------------------------------------------------

class Xvfb:
    """A private virtual display."""

    def __init__(self):
        rfd, wfd = os.pipe()
        self.proc = subprocess.Popen(
            ["Xvfb", "-noreset", "-displayfd", str(wfd), "-screen", "0",
             "1280x800x24"], stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, pass_fds=(wfd,))
        os.close(wfd)
        num = b""

        while not num.endswith(b"\n"):
            chunk = os.read(rfd, 16)

            if chunk == b"":
                raise RuntimeError("Xvfb did not start")

            num += chunk

        os.close(rfd)
        self.display = ":" + num.decode().strip()

    def stop(self):
        self.proc.terminate()
        self.proc.wait()


def cpu_ms(pid):
    """The CPU time of a process in milliseconds, from /proc."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            fields = f.read().rsplit(")", 1)[1].split()

        return (int(fields[11]) + int(fields[12])) * 1000.0 / \
            os.sysconf("SC_CLK_TCK")
    except (OSError, IndexError, ValueError):
        return 0.0


def quiet(pid, t0, stable=0.4, limit=SETTLE_LIMIT):
    """Wait until the process has used no CPU for STABLE seconds; the result
    is the time from t0 to the last moment it was busy."""
    last = cpu_ms(pid)
    last_busy = time.time()

    while time.time() - t0 < limit:
        time.sleep(0.05)
        c = cpu_ms(pid)

        if c - last > 2.0:
            last_busy = time.time()

        last = c

        if time.time() - last_busy >= stable:
            break

    return last_busy - t0


def x11_run(xbin, helpf, doc, work, display):
    """One run of the X11 Editor under the display; a dict of phases."""
    dump = os.path.join(work, "dump")
    env = dict(os.environ, DISPLAY=display, STRATA_X11_DUMP=dump,
               XDG_CONFIG_HOME=os.path.join(work, "cfg"), SHELL="/bin/true")
    path = os.path.join(work, "m.doc")
    shutil.copy(doc, path)

    if os.path.exists(dump):
        os.remove(dump)

    def screen():
        try:
            with open(dump, encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""

    t0 = time.time()
    p = subprocess.Popen([os.path.join(xbin, "stred"), "-H", helpf, path],
                         env=env, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    win = None

    for _ in range(600):
        r = subprocess.run(["xdotool", "search", "--name", "^stred$"],
                           env=env, capture_output=True, text=True)

        if r.stdout.split() != []:
            win = r.stdout.split()[0]
            break

        time.sleep(0.1)

    res = {}

    def keys(*a):
        subprocess.run(["xdotool", "key", "--window", win] + list(a),
                       env=env, capture_output=True)

    def typed(text):
        subprocess.run(["xdotool", "type", "--window", win, text], env=env,
                       capture_output=True)

    while screen() == "" and time.time() - t0 < SETTLE_LIMIT:
        time.sleep(0.05)

    quiet(p.pid, t0)
    res["open_s"] = quiet(p.pid, t0)
    res["open_mb"] = vm(p.pid, "VmHWM")

    t = time.time()
    keys("ctrl+End")
    res["end_s"] = quiet(p.pid, t)
    res["end_mb"] = vm(p.pid, "VmHWM")

    keys("ctrl+Home")
    quiet(p.pid, time.time())
    keys("F9")
    typed(NEEDLE)
    t = time.time()
    keys("Insert")
    res["search_s"] = quiet(p.pid, t)
    res["search_found"] = NEEDLE in screen()
    res["search_mb"] = vm(p.pid, "VmHWM")

    # The search leaves the Replace? prompt on the screen; it takes the
    # next keys, so it is left with Escape before the character is typed.
    keys("Escape")
    quiet(p.pid, time.time())
    typed("x")
    quiet(p.pid, time.time())
    t = time.time()
    keys("ctrl+s")
    res["save_s"] = quiet(p.pid, t)
    res["save_ok"] = "Saved" in screen()
    res["save_mb"] = vm(p.pid, "VmHWM")
    res["peak_mb"] = vm(p.pid, "VmHWM")
    p.terminate()

    try:
        p.wait(10)
    except subprocess.TimeoutExpired:
        p.kill()

    return res


# ---- the Printer ----------------------------------------------------------

def print_run(strprint, doc, work):
    """One run of the Printer to a PDF: seconds, peak megabytes of the
    Printer and what it started, and the pages of the PDF."""
    out = os.path.join(work, "p.pdf")

    if os.path.exists(out):
        os.remove(out)

    t0 = time.time()
    p = subprocess.Popen([strprint, doc, "-o", out, "--no-error-log"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _, status, usage = os.wait4(p.pid, 0)
    res = {"print_s": time.time() - t0, "print_rc": os.waitstatus_to_exitcode(
        status), "print_mb": usage.ru_maxrss / 1024.0, "pages": 0}

    if os.path.exists(out):
        r = subprocess.run(["pdfinfo", out], capture_output=True, text=True)
        m = re.search(r"Pages:\s*(\d+)", r.stdout)
        res["pages"] = int(m.group(1)) if m else 0
        res["pdf_mb"] = os.path.getsize(out) / 1048576.0

    return res


# ---- the driver -----------------------------------------------------------

def docs_of(docdir):
    """The (kind, pages, path) of the documents that gen made."""
    out = []

    for name in sorted(os.listdir(docdir)):
        m = re.match(r"([us])-(\d+)\.doc$", name)

        if m is not None:
            out.append((m.group(1), int(m.group(2)),
                        os.path.join(docdir, name)))

    return sorted(out, key=lambda x: (x[1], x[0]))


def machine():
    """What the measurement ran on."""
    info = {}
    r = subprocess.run(["lscpu"], capture_output=True, text=True)

    for line in r.stdout.split("\n"):
        if line.startswith("Model name:"):
            info["cpu"] = line.split(":", 1)[1].strip()

        if line.startswith("CPU(s):"):
            info["cpus"] = line.split(":", 1)[1].strip()

    with open("/proc/meminfo") as f:
        info["ram_mb"] = int(f.readline().split()[1]) // 1024

    info["load"] = os.getloadavg()[0]
    return info


def measure(kind, build, docdir, results, runs):
    work = tempfile.mkdtemp(prefix="measure")
    xv = Xvfb() if kind == "x11" else None
    helpf = os.path.join(build, "strata.hlp")
    out = open(results, "a")

    try:
        for dk, pages, path in docs_of(docdir):
            for i in range(runs):
                t0 = time.time()

                if kind == "term":
                    rec = term_run(os.path.join(build, "term", "stred"),
                                   path, work)
                elif kind == "x11":
                    rec = x11_run(os.path.join(build, "x11"), helpf, path,
                                  work, xv.display)
                else:
                    rec = print_run(os.path.join(build, "term", "strprint"),
                                    path, work)

                rec.update({"kind": kind, "doc": dk, "nominal_pages": pages,
                            "run": i, "wall": time.time() - t0,
                            "load": os.getloadavg()[0]})
                out.write(json.dumps(rec) + "\n")
                out.flush()
                print("%-5s %s-%-5d run %d  %.1f s" % (
                    kind, dk, pages, i, rec["wall"]))
                sys.stdout.flush()
    finally:
        out.close()

        if xv is not None:
            xv.stop()

        shutil.rmtree(work, ignore_errors=True)


def dir_kb(path):
    r = subprocess.run(["du", "-sk", path], capture_output=True, text=True)
    return int(r.stdout.split()[0])


def disk(build, prefix):
    """Install under PREFIX and print the sizes."""
    root = os.path.dirname(HERE)
    script = os.path.join(os.path.dirname(root), "scripts", "strata-install")
    env = dict(os.environ, STRATA_BUILD_DIR=build)
    r = subprocess.run([script, "--prefix", prefix, "--build-dir", build,
                        "--no-prereqs", "--quiet"], env=env,
                       capture_output=True, text=True)
    print(r.stdout[-400:], r.stderr[-400:])
    print("whole prefix: %d KB" % dir_kb(prefix))

    for sub in ("bin", "share/strata/doc", "share/strata/pdf",
                "share/strata/samples", "share/strata/templates",
                "share/strata", "share/doc/strata", "lib"):
        p = os.path.join(prefix, sub)

        if os.path.exists(p):
            print("%-28s %8d KB" % (sub, dir_kb(p)))

    for name in sorted(os.listdir(os.path.join(prefix, "bin"))):
        p = os.path.join(prefix, "bin", name)
        print("  program %-14s %8d KB" % (name, os.path.getsize(p) // 1024))


def report(results):
    """Print the median and the range of every measure, per case."""
    rows = {}

    with open(results) as f:
        for line in f:
            rec = json.loads(line)
            key = (rec["kind"], rec["doc"], rec["nominal_pages"])
            rows.setdefault(key, []).append(rec)

    for key in sorted(rows, key=lambda k: (k[0], k[2], k[1])):
        recs = rows[key]
        parts = []

        for name in ("open_s", "end_s", "search_s", "save_s", "print_s",
                     "open_mb", "end_mb", "search_mb", "save_mb",
                     "print_mb", "pages"):
            vals = [r[name] for r in recs if name in r and r[name] is not None]

            if vals != []:
                parts.append("%s %.1f (%.1f-%.1f)" % (
                    name, statistics.median(vals), min(vals), max(vals)))

        print("%s %s %5d: %s" % (key[0], key[1], key[2], "; ".join(parts)))


def compare(base, new):
    """Print the memory (MB) of two result files side by side, for the plain
    documents: open, at the end, after the search, after the save; and the
    time of the steps in milliseconds."""
    def load(path):
        rows = {}

        with open(path) as f:
            for line in f:
                rec = json.loads(line)

                if rec["kind"] == "term" and rec["doc"] == "u":
                    rows.setdefault(rec["nominal_pages"], []).append(rec)

        return rows

    a = load(base)
    b = load(new)

    def med(recs, key):
        v = [r[key] for r in recs if r.get(key) is not None]
        return statistics.median(v) if v != [] else 0.0

    print("memory in MB: before / after")
    print("  pages   open         end          search       save")

    for pg in sorted(set(a) & set(b)):
        cells = ["%5.1f/%5.1f" % (med(a[pg], k), med(b[pg], k))
                 for k in ("open_mb", "end_mb", "search_mb", "save_mb")]
        print("  %5d  %s" % (pg, "  ".join(cells)))

    print("time in ms: before / after")
    print("  pages   open         end          search       save")

    for pg in sorted(set(a) & set(b)):
        cells = ["%5.0f/%5.0f" % (med(a[pg], k) * 1000, med(b[pg], k) * 1000)
                 for k in ("open_s", "end_s", "search_s", "save_s")]
        print("  %5d  %s" % (pg, "  ".join(cells)))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    cmd = argv[1]

    if cmd == "gen" and len(argv) >= 4:
        pages = [int(x) for x in argv[4].split(",")] if len(argv) > 4 \
            else DEFAULT_PAGES
        gen(argv[2], argv[3], pages)
    elif cmd in ("term", "x11", "print") and len(argv) >= 5:
        measure(cmd, argv[2], argv[3], argv[4],
                int(argv[5]) if len(argv) > 5 else 3)
    elif cmd == "multipass" and len(argv) >= 5:
        multipass(argv[2], argv[3], argv[4], [int(x) for x in (
            argv[5] if len(argv) > 5 else "10,100,500,1000").split(",")])
    elif cmd == "runtree" and len(argv) >= 3:
        print(json.dumps(run_tree(argv[2:])))
    elif cmd == "compare" and len(argv) >= 4:
        compare(argv[2], argv[3])
    elif cmd == "text" and len(argv) >= 4:
        make_text(argv[2], int(argv[3]))
    elif cmd == "disk" and len(argv) >= 4:
        disk(argv[2], argv[3])
    elif cmd == "report" and len(argv) >= 3:
        report(argv[2])
    elif cmd == "machine":
        print(json.dumps(machine()))
    else:
        print(__doc__)
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
