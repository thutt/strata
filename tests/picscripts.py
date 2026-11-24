#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test checks that the target pics-check of manual/build.sh makes the
# pages that the Printer prints (the pictures that scripts of manual/scenes
# make, not the scenes of shots.py) again, so that they are compared with the
# pictures of the manual like the others.  A page of a print that was not
# made again would go stale without any check seeing it.  The test starts no
# real program of the product: a fake printer writes a small PDF of three
# pages.
#
#   - the list of "checkpictable.py --scripts" names exactly the pictures
#     that manual/pictures.txt says are made by a script, each with a script
#     that exists;
#   - "build.sh pics-scripts DIR" with a printer that works makes one PNG for
#     every picture of the list, and writes "shot NAME" for each;
#   - the same with a printer that fails makes no picture and writes
#     "shots: NAME failed" for each, which checkpics.py lists;
#   - the target pics-check calls pics-scripts (a text check of build.sh, so
#     that the call cannot be dropped).
#
#     python3 -I -B tests/picscripts.py

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
MANUAL = os.path.join(ROOT, "manual")
BUILD = os.path.join(MANUAL, "build.sh")

fails = 0


def check(name, ok, detail=""):
    """Print one result and count a failure."""
    global fails
    if ok:
        print("ok   %s" % name)
    else:
        fails += 1
        print("FAIL %s" % name)
        if detail != "":
            print(detail)


def pdf_of_pages(count):
    """The bytes of a PDF with COUNT empty pages of 200 by 200 points."""
    objects = []
    kids = " ".join("%d 0 R" % (3 + i) for i in range(count))
    objects.append("<< /Type /Catalog /Pages 2 0 R >>")
    objects.append("<< /Type /Pages /Kids [%s] /Count %d >>" % (kids, count))
    for _ in range(count):
        objects.append("<< /Type /Page /Parent 2 0 R "
                       "/MediaBox [0 0 200 200] >>")
    out = "%PDF-1.4\n"
    offsets = []
    for number, body in enumerate(objects, 1):
        offsets.append(len(out))
        out += "%d 0 obj\n%s\nendobj\n" % (number, body)
    start = len(out)
    out += "xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for offset in offsets:
        out += "%010d 00000 n \n" % offset
    out += ("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n"
            % (len(objects) + 1, start))
    return out.encode("ascii")


def write_printer(path, works, pdf):
    """A fake strprint: with WORKS it writes PDF to the file after -o."""
    with open(path, "w", encoding="ascii") as f:
        f.write("#!/bin/sh\n")
        if not works:
            f.write("exit 1\n")
        else:
            f.write('while [ "$#" -gt 0 ]; do\n')
            f.write('    if [ "$1" = "-o" ]; then\n')
            f.write('        cp "%s" "$2"\n' % pdf)
            f.write("    fi\n")
            f.write("    shift\n")
            f.write("done\n")
            f.write("exit 0\n")
    os.chmod(path, 0o755)


def run_target(xbin, outdir, tmp):
    """Run build.sh pics-scripts OUTDIR with the printer of XBIN."""
    env = dict(os.environ)
    env["XBIN"] = xbin
    env["TMPDIR"] = tmp
    env["MANUAL_OUT"] = os.path.join(tmp, "manual-out")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    return subprocess.run(["bash", BUILD, "pics-scripts", outdir], env=env,
                          capture_output=True, text=True, timeout=300)


def table_scripts():
    """The names that manual/pictures.txt says a script makes."""
    names = set()
    with open(os.path.join(MANUAL, "pictures.txt"), "r",
              encoding="ascii") as f:
        for line in f:
            fields = [x.strip() for x in line.split(" | ")]
            if len(fields) == 6 and fields[1] == "script":
                names.add(fields[0])
    return names


def main():
    """Run the checks."""
    listing = subprocess.run(
        [sys.executable, "-I", "-B",
         os.path.join(MANUAL, "checkpictable.py"), "--scripts"],
        capture_output=True, text=True, timeout=60)
    lines = [x.split() for x in listing.stdout.splitlines()]
    names = [x[0] for x in lines if len(x) == 3]
    check("the list of script pictures has three words on every line",
          listing.returncode == 0 and len(names) == len(lines) and
          len(names) > 0, listing.stdout + listing.stderr)
    check("the list names the pictures that pictures.txt says scripts make",
          set(names) == table_scripts(),
          "list %s, table %s" % (sorted(names), sorted(table_scripts())))
    missing = [x[1] for x in lines
               if not os.path.exists(os.path.join(MANUAL, "scenes", x[1]))]
    check("every script of the list exists", missing == [], str(missing))

    tmp = tempfile.mkdtemp(prefix="strata-picscripts.")
    try:
        pdf = os.path.join(tmp, "three.pdf")
        with open(pdf, "wb") as f:
            f.write(pdf_of_pages(3))

        good = os.path.join(tmp, "good")
        os.makedirs(good)
        write_printer(os.path.join(good, "strprint"), True, pdf)
        outdir = os.path.join(tmp, "new-good")
        os.makedirs(outdir)
        r = run_target(good, outdir, tmp)
        log = open(os.path.join(outdir, "shots.log"), encoding="ascii",
                   errors="replace").read() if os.path.exists(
                       os.path.join(outdir, "shots.log")) else ""
        made = sorted(x[:-4] for x in os.listdir(outdir)
                      if x.endswith(".png"))
        check("pics-scripts makes one picture for every script picture",
              made == sorted(names), "made %s\n%s%s" % (made, r.stdout,
                                                        r.stderr))
        check("pics-scripts writes shot NAME for each picture it made",
              all(("shot %s\n" % n) in log for n in names), log)
        check("pics-scripts writes no failure when the printer works",
              "shots:" not in log, log)

        bad = os.path.join(tmp, "bad")
        os.makedirs(bad)
        write_printer(os.path.join(bad, "strprint"), False, pdf)
        outdir = os.path.join(tmp, "new-bad")
        os.makedirs(outdir)
        r = run_target(bad, outdir, tmp)
        log = open(os.path.join(outdir, "shots.log"), encoding="ascii",
                   errors="replace").read() if os.path.exists(
                       os.path.join(outdir, "shots.log")) else ""
        made = [x for x in os.listdir(outdir) if x.endswith(".png")]
        check("pics-scripts makes no picture when the printer fails",
              made == [], str(made))
        check("pics-scripts writes shots: NAME failed for each picture",
              all(("shots: %s failed\n" % n) in log for n in names), log)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    text = open(BUILD, encoding="ascii").read()
    start = text.index("function pics-check")
    body = text[start:text.index("\n}", start)]
    check("pics-check calls pics-scripts", "pics-scripts " in body, body)

    print("picscripts: %d failures" % fails)
    return 1 if fails != 0 else 0


if __name__ == "__main__":
    sys.exit(main())
