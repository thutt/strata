#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program takes the screenshots of the manual.  It starts the X11 version
# of stred, str or dviview on a virtual display (Xvfb), types the keys of a
# scene with xdotool, and writes the window as a PNG file.  The scenes are in
# the table SCENES at the end of this file.
#
#     python3 -I manual/shots.py OUTDIR [SCENE ...]
#
# The programs are found in $XBIN (the x11 build directory, with strprint
# and dviview next to stred; the scenes of manual/scenes/*.py are added); the
# documents are in manual/samples after "build.sh docs" made them.  Only the
# scenes named on the command line are taken; none means all.
#
# A scene that does not end in SCENE_TIMEOUT seconds (60 by default; the
# environment variable SHOTS_TIMEOUT changes it) is reported as failed with
# the line "shots: NAME failed: ...", its programs and its display are
# stopped, and the run goes on with the next scene.  Every helper command has
# a time limit too, a display that does not stop when it is asked to is
# killed, and a signal (SIGTERM, SIGINT) stops everything that was started.
# Only one run at a time is allowed, because the scenes use the folders
# strata-docs and sh in the directory for temporary files (the variable
# TMPDIR, or /tmp); a second run says so and ends.  The exit status is 1
# when a scene failed.

import fcntl
import glob
import os
import re
import signal
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def scratch(name):
    """The path NAME in the directory for temporary files."""
    return os.path.join(tempfile.gettempdir(), name)
KEY_PAUSE = 0.25
SCENE_TIMEOUT = int(os.environ.get("SHOTS_TIMEOUT", "60"))
HELPER_TIMEOUT = 30


class SceneStopped(Exception):
    """A scene that ran too long, or a run that a signal stops."""


STOPPING = []


def write_png(path, width, height, rows):
    """Write 8 bit RGB rows as a PNG file."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data)))
    raw = b"".join(b"\x00" + r for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2,
                                           0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(b"".join([raw]), 9)))
        f.write(chunk(b"IEND", b""))


def xwd_to_png(data, path, crop=None):
    """Convert the output of xwd (32 bit pixels) to a PNG file."""
    head = struct.unpack(">25I", data[:100])
    offset, width, height, bpl, ncolors = (head[0], head[4], head[5],
                                           head[12], head[19])
    pixels = data[offset + ncolors * 12:]
    x0, y0, x1, y1 = crop if crop else (0, 0, width, height)
    # A crop that reaches past the window is cut at the edge of the window.
    x1 = min(x1, width)
    y1 = min(y1, height)
    rows = []
    for y in range(y0, y1):
        line = pixels[y * bpl:y * bpl + width * 4]
        out = bytearray()
        for x in range(x0, x1):
            out += bytes((line[x * 4 + 2], line[x * 4 + 1], line[x * 4]))
        rows.append(bytes(out))
    write_png(path, x1 - x0, y1 - y0, rows)


def ppm_to_png(data, path, crop=None):
    """Convert a binary PPM (P6, 8 bits) to a PNG file."""
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    width, height = int(fields[1]), int(fields[2])
    x0, y0, x1, y1 = crop if crop else (0, 0, width, height)
    x1 = min(x1, width)
    y1 = min(y1, height)
    rows = []
    for y in range(y0, y1):
        start = pos + (y * width + x0) * 3
        rows.append(data[start:start + (x1 - x0) * 3])
    write_png(path, x1 - x0, y1 - y0, rows)


def stop(p):
    """Stop a process that this program started: ask it to end, wait a few
    seconds, then kill it, and reap it."""
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(3)
        except subprocess.TimeoutExpired:
            p.kill()
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                pass


class Display:
    """A virtual display with the programs that run on it."""

    def __init__(self):
        r, w = os.pipe()
        self.xvfb = subprocess.Popen(
            ["Xvfb", "-displayfd", str(w), "-screen", "0", "1280x1024x24"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            pass_fds=(w,))
        os.close(w)
        num = b""
        try:
            while not num.endswith(b"\n"):
                more = os.read(r, 16)
                if not more:
                    raise RuntimeError("Xvfb did not start")
                num += more
        except BaseException:
            # The display must not be left behind when the scene is stopped
            # before it has told its number.
            stop(self.xvfb)
            raise
        finally:
            os.close(r)
        self.name = ":" + num.decode().strip()
        self.tmp = tempfile.mkdtemp(prefix="strata-shots")
        # The pictures must not depend on the print queues of the machine
        # that takes them: a lpstat that lists no queue is named, so that
        # the panels show No printers found everywhere.
        self.lpstat = os.path.join(self.tmp, "lpstat")
        with open(self.lpstat, "w") as f:
            f.write("#!/bin/sh\nexit 0\n")
        os.chmod(self.lpstat, 0o755)
        self.env = dict(os.environ, DISPLAY=self.name, SHELL="/bin/true",
                        STRATA_HOME=scratch("sh"), USER="writer",
                        LOGNAME="writer", STRATA_LPSTAT=self.lpstat,
                        XDG_CONFIG_HOME=os.path.join(self.tmp, "cfg"))
        self.procs = []
        self.win = None

    def close(self):
        """Stop every program that was started, and the display.  Nothing
        here may wait for ever: a program or a display that does not stop
        when it is asked to is killed."""
        for p in self.procs:
            stop(p)
        self.procs = []
        stop(self.xvfb)
        shutil.rmtree(self.tmp, ignore_errors=True)
        shutil.rmtree(scratch("sh"), ignore_errors=True)

    def xdo(self, *args):
        """Run xdotool and return what it prints."""
        try:
            r = subprocess.run(["xdotool"] + list(args), env=self.env,
                               capture_output=True, text=True,
                               timeout=HELPER_TIMEOUT)
        except subprocess.TimeoutExpired:
            return ""
        return r.stdout.strip()

    def start(self, argv, cwd):
        """Start a program and find its window."""
        before = set(self.xdo("search", "--onlyvisible", "--name",
                              "").split())
        p = subprocess.Popen(argv, env=self.env, cwd=cwd,
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        self.procs.append(p)
        for _ in range(60):
            time.sleep(0.2)
            now = set(self.xdo("search", "--onlyvisible", "--name",
                               "").split())
            new = sorted(now - before, key=int)
            if new:
                self.win = new[-1]
                break
        time.sleep(0.8)
        return p

    def keys(self, script):
        """Type a script: words are key names, text:ABC types text, wait:S
        waits, height:PIXELS sets the height of the window."""
        for item in script.split():
            if item.startswith("text:"):
                self.xdo("type", "--window", self.win, "--delay", "60",
                         item[5:].replace("_", " "))
            elif item.startswith("wait:"):
                time.sleep(float(item[5:]))
            elif item.startswith("height:"):
                # Make the window as high as the given number of pixels;
                # the width of the window of the X11 version is fixed.
                self.xdo("windowsize", self.win, "800", item[7:])
                time.sleep(1.0)
            elif item.startswith("win:"):
                found = self.xdo("search", "--name", item[4:]).split()
                if found:
                    self.win = found[-1]
            elif item.startswith("hold:"):
                # a function key with its modifier held down for a while
                self.xdo("windowfocus", self.win)
                subprocess.run(["python3", "-I", os.path.join(HERE, "xk.py"),
                                "--hold"] +
                               item[5:].lower().replace("f", "F").split("+"),
                               env=self.env, timeout=HELPER_TIMEOUT)
            elif re.match(r"^((shift|ctrl|alt)\+)*F\d+$", item):
                # A function key sent with xdotool gets a stray Alt on this
                # keymap, so it goes by keycode through XTest instead.
                self.xdo("windowfocus", self.win)
                subprocess.run(["python3", "-I", os.path.join(HERE, "xk.py")]
                               + item.lower().replace("f", "F").split("+"),
                               env=self.env, timeout=HELPER_TIMEOUT)
            else:
                self.xdo("key", "--window", self.win, "--delay", "120",
                         item)
            time.sleep(KEY_PAUSE)
        time.sleep(0.6)

    def shot(self, path, crop=None):
        """Write the window as a PNG file.  Without xwd the program named by
        the environment variable SHOT (it writes the root window to a PPM
        file, see tools/dviview/tests/shot.c) is used; the windows have no
        frame and stand at the top left, so the root window is the window."""
        if shutil.which("xwd") is None and os.environ.get("SHOT"):
            ppm = os.path.join(self.tmp, "shot.ppm")
            subprocess.run([os.environ["SHOT"], ppm], env=self.env,
                           check=True, timeout=HELPER_TIMEOUT)
            with open(ppm, "rb") as f:
                ppm_to_png(f.read(), path, crop)
            return
        # A picture of the text (a keycap, an equation, a picture marker) is
        # made by a job and arrives some time after the key that needs it, so
        # the window is taken again until two shots in a row are the same
        # (at most twelve times).
        last = None
        for _ in range(12):
            r = subprocess.run(["xwd", "-silent", "-id", self.win],
                               env=self.env, capture_output=True,
                               timeout=HELPER_TIMEOUT)
            if r.stdout == last:
                break
            last = r.stdout
            time.sleep(0.7)
        xwd_to_png(last if last is not None else r.stdout, path, crop)


def run_scene(scene, outdir, xbin, samples):
    """Take one screenshot."""
    name, program, args, script = scene[:4]
    crop = scene[4] if len(scene) > 4 else None
    docs = scratch("strata-docs")
    shutil.rmtree(docs, ignore_errors=True)
    shutil.copytree(samples, docs, ignore=shutil.ignore_patterns("src"))
    # The test document of the faults of a print (a bad equation, a missing
    # picture and a missing character) is kept with the tests; each picture
    # works on a fresh copy of it beside the samples.
    # The folder of a picture that lists the documents must not hold it, so
    # only the scenes that name it get it.
    if any("print_errors.doc" in a for a in args):
        shutil.copy(os.path.join(HERE, "..", "tests", "data",
                                 "print_errors.doc"), docs)
    # The examples of the Math, Border and Winding Column pictures are made
    # by scenes/mkpicdocs.py; only the scenes that name one of them get it.
    if any(a.endswith(("numbers.doc", "border.doc", "table.doc",
                       "headfoot.doc", "indexlist.doc", "secmark.doc",
                       "picmissing.doc"))
           for a in args):
        subprocess.run([sys.executable, "-I", "-B",
                        os.path.join(HERE, "scenes", "mkpicdocs.py"), docs],
                       check=True, timeout=HELPER_TIMEOUT)
    # The picture of the sample letters works on a copy of the letter to the
    # utility; only the scene that names it gets it.
    if any(a.endswith("sample-letter.doc") for a in args):
        shutil.copy(os.path.join(HERE, "..", "samples", "letters",
                                 "complaint-to-utility-us.doc"),
                    os.path.join(docs, "sample-letter.doc"))
    # The picture of Compare Document Versions needs a second version of the
    # memo: the same document with a few words changed, each word replaced by
    # one of the same length, so that the records stay valid.  Only the scenes
    # that name it get it.
    if name.startswith("str-compare") or any("memo-v2.doc" in a for a in args):
        with open(os.path.join(docs, "memo.doc"), "rb") as f:
            memo = f.read()
        for old_text, new_text in (
                (b"12 November 2026", b"19 November 2026"),
                (b"Move the documents", b"Copy the documents"),
                (b"Remove the old copies", b"Delete the old copies"),
                (b"begins with the year.", b"begins with the date.")):
            memo = memo.replace(old_text, new_text, 1)
        with open(os.path.join(docs, "memo-v2.doc"), "wb") as f:
            f.write(memo)
    # The pictures of Restore Backup need a document that has backups: two
    # older versions of the memo in the folder .backup, with the times of
    # their files set to earlier days.  Only the scenes that name them get
    # them.
    if name.startswith("str-restore"):
        with open(os.path.join(docs, "memo.doc"), "rb") as f:
            memo = f.read()
        folder = os.path.join(docs, ".backup")
        os.makedirs(folder, exist_ok=True)
        for number, when, old_text, new_text in (
                (1, (2026, 11, 9, 16, 45), b"12 November 2026",
                 b"09 November 2026"),
                (2, (2026, 11, 2, 9, 30), b"Move the documents",
                 b"Copy the documents")):
            path = os.path.join(folder, "memo.bak%02d" % number)
            with open(path, "wb") as f:
                f.write(memo.replace(old_text, new_text, 1))
            stamp = time.mktime(when + (0, 0, 0, -1))
            os.utime(path, (stamp, stamp))
    extra = os.environ.get("SAMPLE_DOCS", "")
    # The reference manual is the long document that the pictures of a slow
    # preview are taken on: it is made by "build.sh single".
    manual = os.path.join(HERE, "doc")
    # The Editor works on a document of the sample folder through the
    # neutral launcher, so that no folder of this machine is shown in a
    # panel or a marker (the launcher shows the folder as
    # /home/writer/documents).  A scene that opens a file elsewhere (the
    # manual, the folder SAMPLE_DOCS) keeps the plain program.
    envelopes = os.path.join(HERE, "..", "samples", "envelopes")
    story = os.path.join(HERE, "..", "samples", "story")
    if program == "stred" and not any("%M" in a or "%S" in a or "%E" in a or
                                      "%Y" in a for a in args):
        program = os.path.join(HERE, "scenes", "neutral-stred.sh")
    argv = [os.path.join(xbin, program)]
    for a in args:
        a = a.replace("@", docs).replace("%S", extra).replace("%M", manual)
        a = a.replace("%E", envelopes).replace("%Y", story)
        if a.startswith("/") and not os.path.exists(a):
            sys.stderr.write("shots: %s needs %s: skipped\n" % (name, a))
            return
        argv.append(a)
    d = Display()
    try:
        d.start(argv, docs)
        d.keys(script)
        d.shot(os.path.join(outdir, name + ".png"), crop)
    finally:
        d.close()


# Each scene: file name, program, arguments ("@" is the sample directory),
# the keys to type, and optionally the part of the window to keep as
# (left, top, right, bottom) in pixels.
# The Document Manager as a user sees it after an installation: no folder of
# the machine that takes the picture is shown (scenes/neutral-str.sh).
NEUTRAL_STR = os.path.join(HERE, "scenes", "neutral-str.sh")


SCENES = [
    ("stred-memo", "stred", ["@/memo.doc"], "", (0, 0, 800, 490)),
    ("stred-menu", "stred", ["@/memo.doc"], "F12", (0, 0, 800, 490)),
    ("stred-file-menu", "stred", ["@/memo.doc"], "F12 Return",
     (0, 0, 800, 490)),
    ("stred-save-panel", "stred", ["@/memo.doc"], "F12 Return Return"),
    ("stred-outline", "stred", ["@/report.doc"], "alt+1", (0, 0, 800, 520)),
    ("stred-outline-open", "stred", ["@/report.doc"], "alt+2",
     (0, 0, 800, 700)),
    ("stred-marker-menu", "stred", ["@/showcase.doc"], "alt+F12"),
    ("stred-format-panel", "stred", ["@/memo.doc"], "Down Down F6"),
    ("stred-showcase", "stred", ["@/showcase.doc"], "wait:4"),
    ("stred-global-format", "stred", ["@/memo.doc"], "F12 Right Return"),
    ("stred-preview-panel", "stred", ["@/showcase.doc"],
     "F12 Right Right Right Return Right Right Return Return"),
    ("stred-draft-panel", "stred", ["@/showcase.doc"],
     "F12 Right Right Right Return Return"),
    ("stred-preview-wait", os.path.join(HERE, "scenes", "wrapstred.sh"),
     ["%M/strata-reference-manual.doc"],
     "F12 Right Right Right Return Right Right Return Return Insert wait:9",
     (0, 250, 800, 700)),
    ("dviview-page", "stred", ["@/showcase.doc"],
     "F12 Right Right Right Return Right Right Return Return Insert wait:25 "
     "win:preview.dvi wait:1"),
    ("str-menu", NEUTRAL_STR, [], "", (0, 320, 800, 600)),
    ("str-setup", NEUTRAL_STR, [], "End Up Return"),
    ("str-fonts", NEUTRAL_STR, [], "End Up Up Return"),
]


def extra_scenes():
    """The scenes of the files manual/scenes/*.py, each of which defines a
    list named SCENES; every chapter keeps its pictures in its own file."""
    found = []
    for path in sorted(glob.glob(os.path.join(HERE, "scenes", "*.py"))):
        space = {"HERE": HERE}
        with open(path, "r", encoding="ascii") as f:
            exec(compile(f.read(), path, "exec"), space)
        found += space.get("SCENES", [])
    return found


def on_alarm(signum, frame):
    """The time of a scene is over."""
    raise SceneStopped("took more than %d seconds" % SCENE_TIMEOUT)


def on_signal(signum, frame):
    """SIGTERM or SIGINT: stop the scene, clean up and end the run."""
    STOPPING.append(signum)
    raise SceneStopped("stopped by a signal")


def main(argv):
    """Entry point."""
    if len(argv) < 2:
        sys.stderr.write("usage: shots.py OUTDIR [SCENE ...]\n")
        return 2
    outdir = argv[1]
    wanted = set(argv[2:])
    xbin = os.environ.get("XBIN", "")
    if not os.path.exists(os.path.join(xbin, "stred")):
        sys.stderr.write("shots: set XBIN to the x11 build directory\n")
        return 2
    lock = open(scratch("strata-shots.lock"), "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        sys.stderr.write("shots: another run is taking pictures (the scenes "
                         "use " + scratch("strata-docs") + "): wait for it\n")
        return 2
    samples = os.path.join(HERE, "samples")
    os.makedirs(outdir, exist_ok=True)
    signal.signal(signal.SIGALRM, on_alarm)
    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)
    failed = 0
    for scene in SCENES + extra_scenes():
        if wanted and scene[0] not in wanted:
            continue
        signal.alarm(SCENE_TIMEOUT)
        try:
            run_scene(scene, outdir, xbin, samples)
            print("shot", scene[0])
        except SceneStopped as e:
            print("shots: %s failed: %s" % (scene[0], e))
            failed += 1
        except Exception as e:
            print("shots: %s failed: %s" % (scene[0], e))
            failed += 1
        finally:
            signal.alarm(0)
        sys.stdout.flush()
        if STOPPING:
            break
    if failed:
        print("shots: %d scene(s) failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
