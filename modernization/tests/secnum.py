#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Tests that the Printer prints the section numbers that the Editor shows.

sectags (tests/sectags.c) writes the tag of every headline as the Editor
makes it, with fmt_resolve.  This test prints each document with strprint (a
whole document, Final and Draft) and with "stred --print" (parts of it), reads
the text of the PDF with pdftotext, and compares the number that is printed
in front of each headline with that tag.  Cross-references that print the tag
of a section are compared with it as well.  "sectags --check" runs the checks
of the numbers of the sections and of the parts in the programs themselves.

    STRPRINT=... STRED=... SECTAGS=... python3 -I tests/secnum.py [DOCSRC]

The documents are tests/data/secnum_*.doc (tests/mksecdata.py makes them; a
headline is one word that occurs nowhere else), and the structured documents
of tests/data and of the sample directory, which are compared in the order of
their headlines.
"""
import sys as _s; _s.dont_write_bytecode = True
import glob, os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
sectags = os.environ.get('SECTAGS', os.path.join(bdir, 'term', 'sectags'))
sample = (sys.argv[1] if len(sys.argv) > 1 else
          os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples')))
for tool in ('pdflatex', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
data = os.path.join(here, 'data')
# The deepest level that has a section number (SECT_MAXDEPTH, msdoc.h).
NUMDEPTH = 99
tmp = tempfile.mkdtemp(prefix='secnum')
fails = 0
checks = 0

# The glyphs that the code page shows for the codes 1 to 7 and the text of a
# tag, as the PDF has them.
GLYPH = {}
for _code, _cp in enumerate((
        0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25d8,
        0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c, 0x25ba,
        0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8, 0x2191,
        0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc), 1):
    GLYPH[_code] = chr(_cp)

# The characters that the Printer draws but that pdftotext cannot read: the
# faces, the box drawing and the blocks.  They are left out of both texts.
UNREADABLE = set(chr(c) for c in (
    0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x25d8, 0x25d9, 0x266b,
    0x263c, 0x25ba, 0x25c4, 0x2195, 0x25ac, 0x21a8, 0x221f, 0x2194, 0x25b2,
    0x25bc)) | set(chr(c) for c in range(0x2500, 0x25a0))


def check(name, cond, extra=''):
    global fails, checks
    checks += 1
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def screen_text(raw):
    """The text of a tag as the screen shows it, in Unicode, without the
    characters that the text of a PDF does not have."""
    out = ''

    for b in raw:
        ch = GLYPH[b] if b in GLYPH else bytes([b]).decode('cp437')
        out += '' if ch in UNREADABLE else ch

    return out


def squeeze(text):
    """A text without its spaces: pdftotext does not always find the space
    between a tag and a large headline."""
    return ''.join(text.split())


def tags_of(path):
    """The headlines of a document: (item, level, tag, headword)."""
    r = subprocess.run([sectags, path], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    rows = []

    if r.returncode != 0:
        return None

    for line in r.stdout.split(b'\n'):
        if line == b'':
            continue

        f = line.split(b'\t')
        rows.append((int(f[0]), int(f[1]), screen_text(f[2]).strip(),
                     f[3].decode('cp437')))

    return rows


def pdf_lines(pdf):
    r = subprocess.run(['pdftotext', '-layout', pdf, '-'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    text = r.stdout.decode('utf-8', 'replace')
    return [' '.join(x.split()) for x in text.split('\n')]


def print_pdf(path, out, extra=()):
    r = subprocess.run([strprint, path, '-o', out, '--no-error-log',
                        '--no-banner'] + list(extra),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return os.path.exists(out), r.stderr.decode('utf-8', 'replace')


def printed_tag(lines, word, tag):
    """The text that the PDF prints in front of the headword of a headline:
    the part of its line before the word.  A narrow column may print the tag
    on a line of its own, and the tag of a deeply nested section, longer than
    a line, is broken after a point: the lines above the word hold it."""
    printed = None
    at = -1

    for k, ln in enumerate(lines):
        parts = ln.split(word)

        if len(parts) > 1:
            printed = parts[0].strip()
            at = k
            break

    if printed is None or at < 1 or tag == '' or \
            squeeze(printed) == squeeze(tag):
        return printed

    chars = set(tag)
    j = at - 1

    while j >= 0 and lines[j] != '' and set(squeeze(lines[j])) <= chars and \
            len(squeeze(printed)) < len(squeeze(tag)):
        printed = lines[j] + ' ' + printed
        j -= 1

    return printed


def compare_unique(name, rows, lines):
    """Documents whose headwords occur only in their headlines: the text in
    front of the headword on its line is the tag."""
    bad = []

    for item, level, tag, word in rows:
        if word == '':
            if tag == '':
                continue

            if tag not in lines:
                bad.append((item, level, tag, '(no line with the tag alone)'))

            continue

        printed = printed_tag(lines, word, tag)

        if printed is None:
            bad.append((item, level, tag, '(the headline is not in the PDF)'))
        elif squeeze(printed) != squeeze(tag):
            bad.append((item, level, tag, printed))

    check('%s: %d headlines print the tag of the screen' % (name, len(rows)),
          bad == [], '\n'.join('  item %d level %d: screen [%s] printed [%s]'
                               % b for b in bad[:12]))


def compare_ordered(name, rows, lines):
    """Any document: the headlines are found in order, a line that begins
    with the tag and the first word of the headline."""
    pos = 0
    bad = []

    sq = [squeeze(ln) for ln in lines]

    for item, level, tag, word in rows:
        if tag == '':
            continue

        want = re.compile(re.escape(squeeze(tag)) + r'\.*' + re.escape(word))
        found = None

        for j in range(pos, len(lines)):
            if want.match(sq[j]) is not None:
                found = j
                break

            # In a narrow column the tag may stand alone on its line and the
            # words of the headline begin the next line.
            if sq[j].startswith(squeeze(tag)) and j + 1 < len(lines) and \
                    word != '' and sq[j + 1].startswith(word):
                found = j
                break

        if found is None:
            if word == '':
                continue

            near = [ln for ln in lines[pos:] if ln.find(word) >= 0][:1]
            bad.append((item, level, tag, near[0] if near else
                        '(the headline is not in the PDF)'))
        else:
            pos = found + 1

    check('%s: %d headlines print the tag of the screen' % (name, len(rows)),
          bad == [], '\n'.join('  item %d level %d: screen [%s] printed [%s]'
                               % b for b in bad[:12]))


def numbers_of(rows):
    """The number of each section, as Go To section takes it (the counters
    of the levels, joined by dots), for the headlines in order."""
    cnt = [0] * (max([r[1] for r in rows] + [1]) + 2)
    out = []

    for item, level, tag, word in rows:
        cnt[level] += 1

        for k in range(level + 1, len(cnt)):
            cnt[k] = 0

        out.append('.'.join(str(cnt[k]) for k in range(1, level + 1)))

    return out


def part_cases(rows):
    """A few ranges: (from, to, the headlines that it holds)."""
    nums = numbers_of(rows)
    cases = []
    # A section nested deeper than NUMDEPTH levels has no number to name it.
    usable = [k for k, r in enumerate(rows) if r[1] <= NUMDEPTH]
    picks = sorted(set([usable[0], usable[len(usable) // 3],
                        usable[len(usable) // 2], usable[-1]]))

    def subtree(k):
        j = k + 1

        while j < len(rows) and rows[j][1] > rows[k][1]:
            j += 1

        return j

    for k in picks:
        cases.append((nums[k], nums[k], list(range(k, subtree(k)))))

    if len(usable) > 3:
        a = usable[len(usable) // 3]
        z = usable[len(usable) // 2]
        cases.append((nums[a], nums[z], list(range(a, subtree(z)))))

    return cases


def run_parts(name, path, rows):
    """Prints parts with "stred --print" and compares the tags of the
    headlines in them with the tags of the whole document."""
    for a, z, held in part_cases(rows):
        out = os.path.join(tmp, 'part.pdf')

        if os.path.exists(out):
            os.unlink(out)

        env = dict(os.environ, STRATA_STRPRINT=strprint)
        r = subprocess.run([stred, '--print', path, '--from', a, '--to', z,
                            '-o', out, '--no-error-log'], env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        label = '%s: print from section %s to %s' % (name, a, z)

        if not os.path.exists(out):
            check(label + ' makes a PDF', False,
                  r.stderr.decode('utf-8', 'replace'))
            continue

        lines = pdf_lines(out)
        want = [rows[k] for k in held]
        bad = []

        for item, level, tag, word in want:
            printed = printed_tag(lines, word, tag)

            if printed is None or squeeze(printed) != squeeze(tag):
                bad.append((item, level, tag, printed))

        extra = []

        for item, level, tag, word in rows:
            if (item, level, tag, word) in want or word == '':
                continue

            if any(word in ln for ln in lines):
                extra.append(word)

        check(label + ': %d headlines have the tag of the whole document'
              % len(want), bad == [] and extra == [],
              '\n'.join('  item %d level %d: screen [%s] printed [%s]' % b
                        for b in bad[:8]) + ('' if not extra else
                                             '\n  not in the part: %s' % extra))


def run_xref(name, path, rows):
    """The cross-references of secnum_xref.doc print the tag."""
    lines = pdf_lines(os.path.join(tmp, 'xref.pdf'))
    bad = []

    for k, (item, level, tag, word) in enumerate(rows):
        want = 'Ref r%d %s' % (k + 1, tag)

        if want not in lines:
            near = [ln for ln in lines if ln.startswith('Ref r%d ' % (k + 1))]
            bad.append((want, near[0] if near else '(no line)'))

    check('%s: a cross-reference prints the tag of its section' % name,
          bad == [], '\n'.join('  want [%s] got [%s]' % b for b in bad))


unique = sorted(glob.glob(os.path.join(data, 'secnum_*.doc')))
others = sorted(glob.glob(os.path.join(data, '*.doc')) +
                glob.glob(os.path.join(sample, '*.doc')))
others = [p for p in others if not os.path.basename(p).startswith('secnum_')]

for path in unique + others:
    name = os.path.basename(path)
    rows = tags_of(path)

    if rows is None or len(rows) == 0:
        continue

    is_unique = path in unique

    # The checks of the numbers and of the parts, in the programs.
    r = subprocess.run([sectags, '--check', path], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    out = r.stdout.decode('utf-8', 'replace').split('\n')
    bad = [ln for ln in out if ln.startswith('FAIL')]
    check('%s: the numbers and the parts of the document (sectags --check)'
          % name, r.returncode == 0 and bad == [], '\n'.join(bad[:6]))

    for mode, extra in (('final', ()), ('draft', ('--draft',))):
        if mode == 'draft' and not is_unique:
            continue

        pdf = os.path.join(tmp, 'xref.pdf' if name == 'secnum_xref.doc' and
                           mode == 'final' else '%s-%s.pdf' % (name, mode))
        ok, err = print_pdf(path, pdf, extra)

        if not ok:
            check('%s: %s print makes a PDF' % (name, mode), False, err)
            continue

        lines = pdf_lines(pdf)

        if is_unique:
            compare_unique('%s (%s)' % (name, mode), rows, lines)
        else:
            compare_ordered('%s (%s)' % (name, mode), rows, lines)

    if is_unique and len(rows) > 1:
        run_parts(name, path, rows)

    if name == 'secnum_xref.doc':
        run_xref(name, path, rows)

shutil.rmtree(tmp, ignore_errors=True)
print('secnum: %d checks, %d failures' % (checks, fails))
sys.exit(1 if fails else 0)
