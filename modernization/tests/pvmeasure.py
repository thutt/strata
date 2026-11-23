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

"""Measure the page estimate of the preview against the real page count.

usage: pvmeasure.py OUTDIR FILE.DOC...
       pvmeasure.py --report OUTDIR/measure.json

For each document it runs strprint --dvi --progress (the run that the
preview does), reads the output with pvest -s (the reader of the editor),
and writes OUTDIR/measure.json with the real number of pages, the estimate
from the document, and the marks (position, pages) of the run.  The
environment names the programs: STRPRINT, PVEST (default: the build
directory term/).  Then it prints a table, and with FIT=1 it also looks for
the best values of the two constants of the model.
"""
import sys as _s; _s.dont_write_bytecode = True
import json, os, subprocess, sys, tempfile, time

build = os.path.join(os.environ.get('STRATA_BUILD_DIR', ''), 'term')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'strprint'))
pvest = os.environ.get('PVEST', os.path.join(build, 'pvest'))


def run_one(doc, out):
    t0 = time.time()
    tmp = tempfile.mkdtemp(prefix='pvm')
    env = dict(os.environ, TMPDIR=tmp)
    p = subprocess.run([strprint, doc, '--dvi', '-o', os.path.join(out,
        'x.dvi'), '--progress'], stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, env=env)
    for f in ('x.dvi',):
        try:
            os.unlink(os.path.join(out, f))
        except OSError:
            pass
    q = subprocess.run([pvest, '-s'], input=p.stdout, stdout=subprocess.PIPE)
    marks = []
    pages = 0
    for line in q.stdout.decode().split('\n'):
        w = line.split()
        if w[:1] == ['mark']:
            marks.append((int(w[1]), int(w[2])))
        elif w[:1] == ['end']:
            pages = int(w[1])
    est = subprocess.run([pvest, doc], stdout=subprocess.PIPE).stdout.split()
    return {'doc': doc, 'status': p.returncode, 'pages': pages,
            'estimate': int(est[1]) if len(est) > 1 else 0, 'marks': marks,
            'seconds': round(time.time() - t0, 1)}


def refine(prior, pages, pos):
    if pos <= 0 or pos > 1000 or pages <= 0:
        return prior
    ex = (pages * 1000 + pos // 2) // pos
    t = (ex * pos + prior * (1000 - pos) + 500) // 1000
    return max(t, pages)


def at_fraction(r, frac, prior):
    """The corrected estimate when frac of the pages have been shipped."""
    n = max(1, int(r['pages'] * frac))
    best = None
    for pos, pg in r['marks']:
        if pg <= n:
            best = (pos, pg)
    if best is None:
        return prior
    return refine(prior, best[1], best[0])


def report(results):
    """The mean and worst error of the estimate, before latex and after
    some of the pages, for the documents of at least 3 and at least 10 pages
    (a page more or less in a short document is a big percentage)."""
    for minp in (3, 10):
        good = [r for r in results if r['status'] == 0 and r['pages'] >= minp]
        if not good:
            continue
        print('documents of %d pages or more: %d' % (minp, len(good)))
        for name, fn in (('estimate before latex', lambda r:
                    r['estimate']),
                ('after 10 percent of the pages', lambda r:
                    at_fraction(r, 0.1, r['estimate'])),
                ('after 25 percent', lambda r:
                    at_fraction(r, 0.25, r['estimate'])),
                ('after 50 percent', lambda r:
                    at_fraction(r, 0.5, r['estimate'])),
                ('after 75 percent', lambda r:
                    at_fraction(r, 0.75, r['estimate']))):
            errs = [abs(fn(r) - r['pages']) / r['pages'] for r in good]
            print('  %-32s mean error %5.1f percent, worst %5.1f percent' % (
                name, 100 * sum(errs) / len(errs), 100 * max(errs)))


def main():
    if sys.argv[1] == '--report':
        report(json.load(open(sys.argv[2])))
        return
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    results = []
    for doc in sys.argv[2:]:
        r = run_one(doc, out)
        results.append(r)
        print('%-22s status %d pages %5d estimate %5d  %5.1fs' % (
            os.path.basename(doc), r['status'], r['pages'], r['estimate'],
            r['seconds']), flush=True)
    with open(os.path.join(out, 'measure.json'), 'w') as f:
        json.dump(results, f)
    report(results)


main()
