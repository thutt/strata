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

"""Check that the build rebuilds what a header changes, and nothing else.

usage: depcheck.py BUILD_DIR BUILD_TYPE

The sources are copied into a folder of the build directory (the source tree
is never touched).  Some programs are built there, a header of the copy is
touched, the build is run again, and the objects and the programs that were
made again are compared with the ones that include the header according to
the compiler (cc -MM, which does not read the Makefile).  A build with no
change must do nothing.  The Viewer of tools/dviview is checked the same way.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, time

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
fails = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def stamps(paths):
    return {p: os.stat(p).st_mtime_ns for p in paths if os.path.exists(p)}


def includes(src, cfile, extra=()):
    """The headers of the copy that the compiler says cfile includes."""
    r = subprocess.run(['cc', '-MM', '-iquote', os.path.join(src, 'src')] +
        list(extra) + [cfile], capture_output=True, text=True, cwd=src)
    return {os.path.normpath(os.path.join(src, w)) for w in
        r.stdout.replace('\\\n', ' ').split()[2:] if w.endswith('.h')}


def touch_later(path):
    """Make the file newer than everything that was built before."""
    time.sleep(0.1)
    os.utime(path, None)


def main():
    if len(sys.argv) != 3:
        print('usage: depcheck.py BUILD_DIR BUILD_TYPE'); return 2
    bd, btype = os.path.abspath(sys.argv[1]), sys.argv[2]
    work = os.path.join(bd, 'depcheck')
    shutil.rmtree(work, ignore_errors=True)
    tree = os.path.join(work, 'tree', 'modernization')
    out = os.path.join(work, 'build', btype)
    os.makedirs(tree); os.makedirs(out)
    shutil.copytree(os.path.join(root, 'src'), os.path.join(tree, 'src'))
    shutil.copy(os.path.join(root, 'Makefile'), tree)
    os.makedirs(os.path.join(tree, 'tests'))
    for f in os.listdir(os.path.join(root, 'tests')):
        if f.endswith('.c') or f.endswith('.expect'):
            shutil.copy(os.path.join(root, 'tests', f),
                os.path.join(tree, 'tests', f))
    env = dict(os.environ, STRATA_BUILD_DIR=out, STRATA_BUILD_TYPE=btype)
    term = os.path.join(out, 'term')
    progs = [os.path.join(term, 'strprint'), os.path.join(term, 'lxtest')]

    def make(*targets, dry=False):
        cmd = ['make', '-C', tree, '--no-print-directory'] + \
            (['-n'] if dry else []) + list(targets)
        return subprocess.run(cmd, env=env, capture_output=True, text=True)

    r = make(*progs)
    check('depcheck: the first build of strprint and lxtest works',
        r.returncode == 0, r.stdout[-300:] + r.stderr[-600:])
    if r.returncode != 0:
        return 1
    r = make(*progs, dry=True)
    check('depcheck: a build with no change does nothing',
        r.returncode == 0 and 'CC ' not in r.stdout and 'LD ' not in r.stdout
        and 'cc ' not in r.stdout, r.stdout[:300])
    objs = sorted(os.path.join(term, f) for f in os.listdir(term)
        if f.endswith('.o'))
    srcs = {o: os.path.join(tree, 'src', os.path.basename(o)[:-2] + '.c')
        for o in objs}
    mains = {progs[1]: os.path.join(tree, 'tests', 'lxtest.c'),
        progs[0]: os.path.join(tree, 'src', 'strprint.c')}
    deps = {o: includes(tree, srcs[o]) for o in objs}
    deps.update({p: includes(tree, c) for p, c in mains.items()})
    for h in ('note.h', 'util.h', 'build_type.h', 'cond.h'):
        hp = os.path.join(tree, 'src', h)
        before = stamps(objs + progs)
        touch_later(hp)
        r = make(*progs)
        after = stamps(objs + progs)
        changed = {p for p in after if after[p] != before.get(p)}
        # An object is made again when it includes the header; a program is
        # made again when it includes it itself or when an object changed.
        want = {o for o in objs if hp in deps[o]}
        wantp = {p for p in progs if hp in deps[p] or (want & set(objs))}
        check('depcheck: %s: the objects that include it are made again and '
            'the others are not' % h, r.returncode == 0 and
            changed & set(objs) == want, (sorted(want ^ (changed &
            set(objs))), r.stderr[-300:]))
        check('depcheck: %s: the programs that include it or link an object '
            'that changed are linked again' % h,
            changed & set(progs) == wantp, sorted(wantp ^ (changed &
            set(progs))))
    # The Viewer of tools/dviview.
    if os.path.isfile('/usr/include/X11/Xlib.h') and \
            os.path.isdir('/usr/include/freetype2'):
        vsrc = os.path.join(root, 'tools', 'dviview')
        vtree = os.path.join(tree, 'tools', 'dviview')
        shutil.copytree(vsrc, vtree, ignore=shutil.ignore_patterns(
            'build', 'dviview', 'tests'))
        vbuild = os.path.join(work, 'vobj')
        vprog = os.path.join(work, 'vprog')

        def vmake():
            return subprocess.run(['make', '-C', vtree, 'BUILD=' + vbuild,
                'PROG=' + vprog, 'STRATA_BUILD_TYPE=' + btype, vprog],
                capture_output=True, text=True)
        r = vmake()
        check('depcheck: the Viewer builds', r.returncode == 0,
            r.stdout[-300:] + r.stderr[-600:])
        if r.returncode == 0:
            vobjs = sorted(os.path.join(vbuild, f) for f in
                os.listdir(vbuild) if f.endswith('.o'))
            vdeps = {}
            for o in vobjs:
                c = os.path.join(vtree, 'src', os.path.basename(o)[:-2] +
                    '.c')
                if not os.path.exists(c):
                    c = os.path.join(tree, 'src', os.path.basename(o)[:-2] +
                        '.c')
                vdeps[o] = includes(tree, c, ('-I/usr/include/freetype2',
                    '-iquote', os.path.join(vtree, 'src')))
            for hp in (os.path.join(vtree, 'src', 'dviview.h'),
                    os.path.join(tree, 'src', 'log.h')):
                before = stamps(vobjs + [vprog])
                touch_later(hp)
                r = vmake()
                after = stamps(vobjs + [vprog])
                changed = {p for p in after if after[p] != before.get(p)}
                want = {o for o in vobjs if hp in vdeps[o] or
                    os.path.basename(hp) in
                    {os.path.basename(x) for x in vdeps[o]}}
                check('depcheck: Viewer: %s: the objects that include it '
                    'are made again and the others are not' %
                    os.path.basename(hp), r.returncode == 0 and
                    changed & set(vobjs) == want and vprog in changed,
                    (sorted(want ^ (changed & set(vobjs))), r.stderr[-300:]))
    else:
        print('skip: the Viewer needs the headers of X11 and FreeType')
    shutil.rmtree(work, ignore_errors=True)
    print('depcheck: %s' % ('all passed' if not fails else
        '%d FAILURES' % fails))
    return 1 if fails else 0


sys.exit(main())
