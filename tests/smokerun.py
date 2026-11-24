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

"""Runs the scripts of the smoke target, several at a time.

    smokerun.py [--jobs N] [--quick SECONDS] [--list]

Each script gets a directory of its own (under TMPDIR) for its temporary
files and for its settings (STRATA_HOME), which is removed at its end, and an
output file of its own; the outputs are shown in the order of the list when all
have ended, with a table of the time of each script and of its checks.  Every
script is run, whatever the others do; the status is 1 if one of them failed
(a status other than 0 or a line that begins with FAIL).

--jobs N       scripts at a time (default: the variable SMOKE_JOBS, else the
               number of CPUs, at most 4; 1 is the serial order of the list).
--quick SECONDS  every script is stopped after SECONDS seconds (by the
               variable SMOKE_QUICK too); the checks that it had not made are
               left out, and the table says how many it made.  A script that
               was stopped and had no FAIL line is not a failure.

The programs and the documents are named by the variables STRED, STR,
STRPRINT, RT, SECTAGS, DOCSRC and STRATA_BUILD_TYPE, which the Makefile sets.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, signal, subprocess, sys, tempfile, threading, time

here = os.path.dirname(os.path.abspath(__file__))

# The scripts, longest first so that the slowest does not start last.  The
# order of the output is the order of this list.  smoke.py is cut into the
# parts of tests/smokepart.py, 'smoke.py 3/7' and so on, which run at the same
# time like the other scripts (SMOKE_WHOLE=1 runs it in one piece).
SCRIPTS = ['smoke.py', 'movesect.py', 'windows.py', 'retnamed.py', 'textindent.py', 'ownmenu.py', 'boxtitle.py', 'undokeys.py', 'printpdf.py', 'preview.py', 'xlatex.py',
           'latexmarker.py', 'errlog.py', 'secnum.py', 'banner.py',
           'termstate.py', 'conditional.py', 'tocpdf.py', 'include.py',
           'utf8s0.py', 'nbsp.py', 'shy.py', 'keys.py', 'joins.py', 'hyphlist.py', 'lineno.py', 'runlog.py', 'keep.py', 'tablehead.py',
           'condmanual.py', 'mathcheat.py', 'lastcol.py', 'sheets.py',
           'printcolor.py', 'headfoot.py', 'indexlist.py', 'pagenumlevel.py', 'printfield.py',
           'levelindent.py', 'winding.py', 'romanbig.py', 'borderprint.py',
           'attrtable.py', 'picturecolour.py', 'numrun.py', 'termlag.py',
           'sectionmark.py', 'autolists.py', 'templates.py', 'labels.py', 'hyctest.py', 'pbspace.py',
           'strataname.py', 'namedwind.py', 'lmsmark.py', 'termcheck.py',
           'setuppages.py',
           'gsdoc.py', 'tplfield.py', 'inslist.py', 'cursorpos.py',
           'deepsect.py',
           'letters.py', 'envsamples.py', 'story.py', 'keymap_check.py',
           'keypart.py', 'longnames.py', 'picturecursor.py',
           'markerdefault.py', 'tableedge.py', 'papernames.py',
           'envpreview.py', 'envfont.py', 'picmissing.py', 'picnames.py',
           'structconv.py', 'previewestimate.py', 'lpslow.py',
           'printsearch.py', 'lmsetting.py', 'printchoice.py',
           'keycapfall.py', 'printfix.py', 'tabindent.py', 'keycapscale.py',
           'checklist.py', 'fontsample.py', 'fontprogress.py', 'printmemory.py',
           'fileversion.py',
           'spacechoice.py', 'installuser.py', 'nolatex.py',
           'menuroute.py', 'tplfolders.py', 'installtarball.py',
           'publishclone.py', 'release.py', 'pdfguard.py']


def expand(names):
    out = []
    for n in names:
        if n == 'smoke.py' and not os.environ.get('SMOKE_WHOLE'):
            sys.path.insert(0, here)
            import smokepart
            k = len(smokepart.PARTS)
            out.extend('smoke.py %d/%d' % (i + 1, k) for i in range(k))
        else:
            out.append(n)
    return out


def cpus():
    try:
        n = len(os.sched_getaffinity(0))
    except AttributeError:
        n = os.cpu_count() or 1
    return max(1, min(n, 4))


def run_one(name, root, quick, results):
    script, _, part = name.partition(' ')
    work = tempfile.mkdtemp(prefix=script[:-3] + '-', dir=root)
    home = os.path.join(work, 'home')
    os.mkdir(home, 0o700)
    env = dict(os.environ)
    # The home of the user is a folder of its own too: a program that is run
    # without STRATA_HOME writes its settings and its log into it, and the
    # run fails below, so that the real home is never touched by a test.
    userhome = os.path.join(work, 'userhome')
    os.mkdir(userhome, 0o700)
    env.update({'PYTHONDONTWRITEBYTECODE': '1', 'TMPDIR': work,
                'STRATA_HOME': home, 'HOME': userhome})
    if part:
        env['SMOKE_PART'] = part

    # The scripts change their directory: a path of the Makefile that is
    # relative to the project directory must be a full path for them.
    for var in ('DOCSRC', 'STRED', 'STR', 'STRPRINT', 'RT', 'SECTAGS'):
        if env.get(var):
            env[var] = os.path.abspath(env[var])
    # The build directory has a link share/strata/templates, and the setup
    # script sets STRATA_TEMPLATES, so that a program run in place finds the
    # templates.  The scripts that test the programs without a template get
    # copies of the programs in a folder of their own, with nothing beside
    # them, and no STRATA_TEMPLATES; a script that needs one sets it.
    env.pop('STRATA_TEMPLATES', None)

    if env.get('STRED') and os.path.isdir(os.path.dirname(env['STRED'])):
        pdir = os.path.dirname(env['STRED'])
        cbin = os.path.join(work, 'bin')
        os.mkdir(cbin, 0o700)

        for prog in os.listdir(pdir):
            src = os.path.join(pdir, prog)

            if '.' not in prog and os.path.isfile(src) and \
                    os.access(src, os.X_OK):
                shutil.copy2(src, cbin)

        for var in ('STRED', 'STR', 'STRPRINT', 'RT', 'SECTAGS'):
            if env.get(var) and os.path.dirname(env[var]) == pdir:
                env[var] = os.path.join(cbin, os.path.basename(env[var]))

    out = os.path.join(root, name.replace(' ', '-').replace('/', 'of') + '.out')
    t0 = time.time()
    cut = False
    with open(out, 'wb') as f:
        p = subprocess.Popen([sys.executable, '-I', '-B', '-u',
            os.path.join(here, script)], stdout=f, stderr=subprocess.STDOUT,
            env=env, cwd=os.path.dirname(here), start_new_session=True)
        try:
            rc = p.wait(timeout=quick if quick > 0 else None)
        except subprocess.TimeoutExpired:
            cut = True
            os.killpg(p.pid, signal.SIGTERM)
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
            rc = 0
    text = open(out, 'rb').read().decode('utf-8', 'replace')

    if os.path.exists(os.path.join(userhome, '.strata')):
        text += ('\nFAIL %s left a folder .strata in the home of the user: it '
                 'ran a program without STRATA_HOME\n' % script)
        rc = rc or 1

    ok = len(re.findall(r'(?m)^ok ', text))
    fail = len(re.findall(r'(?m)^FAIL', text))
    results[name] = {'rc': rc, 'secs': time.time() - t0, 'ok': ok,
                     'fail': fail, 'cut': cut, 'text': text}
    if os.environ.get('SMOKE_KEEP') is None:
        shutil.rmtree(work, ignore_errors=True)


def main():
    args = sys.argv[1:]
    jobs = int(os.environ.get('SMOKE_JOBS') or cpus())
    quick = int(os.environ.get('SMOKE_QUICK') or 0)
    names = expand(SCRIPTS)
    while args:
        a = args.pop(0)
        if a == '--jobs':
            jobs = int(args.pop(0))
        elif a == '--quick':
            quick = int(args.pop(0))
        elif a == '--list':
            print('\n'.join(names))
            return 0
        else:
            print('smokerun: unknown option ' + a)
            return 2
    base = os.environ.get('TMPDIR') or tempfile.gettempdir()
    root = tempfile.mkdtemp(prefix='smoke-', dir=base)
    results = {}
    queue = list(names)
    lock = threading.Lock()

    def worker():
        while True:
            with lock:
                if not queue:
                    return
                name = queue.pop(0)
            run_one(name, root, quick, results)

    t0 = time.time()
    threads = [threading.Thread(target=worker) for _ in range(max(1, jobs))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    total = time.time() - t0
    bad = 0
    for name in names:
        r = results[name]
        print('=' * 70)
        print('== %s' % name)
        sys.stdout.write(r['text'])
        if not r['text'].endswith('\n'):
            print()
    print('=' * 70)
    print('%-16s %7s %6s %5s  %s' % ('script', 'seconds', 'ok', 'FAIL',
                                     'status'))
    for name in names:
        r = results[name]
        st = 'stopped at the time limit (the rest is left out)' if r['cut'] \
            and r['fail'] == 0 else ('exit %d' % r['rc'])
        if r['fail'] != 0 or (r['rc'] != 0 and not r['cut']):
            bad += 1
            st += '  <-- FAILED'
        print('%-16s %7.1f %6d %5d  %s' % (name, r['secs'], r['ok'],
                                          r['fail'], st))
    print('%-16s %7.1f %6d %5d  jobs %d%s' % ('total', total,
        sum(r['ok'] for r in results.values()),
        sum(r['fail'] for r in results.values()), jobs,
        ', quick %d s' % quick if quick else ''))
    print('smoke: %s' % ('%d script(s) failed' % bad if bad else 'all ran'))
    shutil.rmtree(root, ignore_errors=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
