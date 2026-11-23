#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The test of tools/docversion.py, the tool that changes the file format
# version of documents (the number of the first record, '*R<n>').  Scratch
# directories hold documents of several versions, a document that is not
# one, a read-only file and a file with another extension, and the test
# checks:
#
#   - the reports, the counts and the exit status;
#   - that only the first record of a changed file differs, and that the
#     suffix of the record (the 'h' of the old libraries) is kept;
#   - that no file is made besides the changed ones (no backup, no
#     temporary file), and that the permissions are kept;
#   - that a version above the target is never lowered, and that --check
#     writes nothing;
#   - that the program does the same again on its own output (idempotence);
#   - that every document of the product in the repository is current.
#
# Every check must fail when the feature is off, so the test also runs the
# same checks against copies of the tool in which one feature is removed
# on purpose (a backup file made, the rest of the file changed, a higher
# version lowered, the permissions lost, --check writing, the suffix
# dropped, a read-only file changed, an unrelated extension visited), and
# a mutation that no check finds fails the test.

import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, '..')
tool = os.path.join(root, 'tools', 'docversion.py')

# The directories of the product that the build check visits (the Makefile
# target docversion names the same).  The generated manuals are not in the
# list: the build writes them with the current version.
PRODUCT_DIRS = ('samples', 'templates', 'tests/data', 'tests/samples',
                'manual/samples')

BODY = (b'MR1006\r\nMD A description with bytes \x80\xe9\xff and R2 inside'
        b'\x00\r\n*B0\r\nT *R2 is text here, \x00 and \x1b stays\x00\r\n'
        b'*E\r\n')


def doc(first, body=BODY):
    return first + b'\r\n' + body


def run(prog, args, cwd=None):
    p = subprocess.run([sys.executable, '-I', '-B', prog] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       cwd=cwd)

    return p.returncode, p.stdout.decode('latin-1'), p.stderr.decode('latin-1')


def make_tree(base, readonly=False):
    """The documents of the test below base, and their content."""
    files = {
        'a2.doc': doc(b'*R2'),
        'up.DOC': doc(b'*R2'),
        't.set': doc(b'*R2'),
        'm.mac': doc(b'*R2h'),
        'l.lib': doc(b'*R2h'),
        'sub/deep/x.doc': doc(b'*R2'),
        'sub/b3.doc': doc(b'*R3'),
        'sub/c4.set': doc(b'*R4'),
        'notdoc.doc': b'plain text\r\nno record\r\n',
        'empty.doc': b'',
        'norecord.doc': b'*R2',
        'big.doc': doc(b'*R123456789'),
        'perm.doc': doc(b'*R2'),
        'other.txt': doc(b'*R2'),
    }

    if readonly:
        files['ro.doc'] = doc(b'*R2')

    for rel, content in files.items():
        path = os.path.join(base, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)

        with open(path, 'wb') as f:
            f.write(content)

    os.chmod(os.path.join(base, 'perm.doc'), 0o640)

    if readonly:
        os.chmod(os.path.join(base, 'ro.doc'), 0o444)

    os.symlink('a2.doc', os.path.join(base, 'link.doc'))

    return files


def listing(base):
    out = []

    for top, dirs, names in os.walk(base):
        for n in dirs + names:
            out.append(os.path.relpath(os.path.join(top, n), base))

    return sorted(out)


def read(base, rel):
    with open(os.path.join(base, rel), 'rb') as f:
        return f.read()


def status_lines(out, word):
    return [l for l in out.split('\n') if l.startswith(word + ':')]


def run_checks(prog, report, scratch, repo):
    """Run every check against prog.  report(name, ok, detail) records."""
    base = tempfile.mkdtemp(prefix='t1', dir=scratch)
    files = make_tree(base)
    before = listing(base)
    rc, out, err = run(prog, [base])
    converted = ('a2.doc', 'up.DOC', 't.set', 'm.mac', 'l.lib',
                 'sub/deep/x.doc', 'perm.doc')
    report('a run converts the documents and exits 0', rc == 0, out + err)
    report('seven documents are reported as changed',
           len(status_lines(out, 'changed')) == 7, out)
    report('the count line is right',
           'docversion: 7 changed, 0 would change, 1 unchanged, '
           '6 skipped, 0 failed' in out, out)
    report('the first record is the target and the rest is the same',
           all(read(base, r) ==
               (b'*R3h' if r in ('m.mac', 'l.lib') else b'*R3') +
               files[r][files[r].index(b'\r\n'):] for r in converted), out)
    report('the suffix of the first record is kept',
           read(base, 'm.mac').startswith(b'*R3h\r\n') and
           read(base, 'l.lib').startswith(b'*R3h\r\n'))
    report('the rest of every changed file is the same byte for byte',
           all(read(base, r).split(b'\r\n', 1)[1] ==
               files[r].split(b'\r\n', 1)[1] for r in converted))
    report('a version above the target is not lowered',
           read(base, 'sub/c4.set') == files['sub/c4.set'] and
           read(base, 'big.doc') == files['big.doc'] and
           'skipped: ' + os.path.join(base, 'sub/c4.set') +
           ' (version 4 is above 3)' in out, out)
    report('a file that is not a document is reported and left alone',
           all(read(base, r) == files[r] for r in
               ('notdoc.doc', 'empty.doc', 'norecord.doc')) and
           out.count('(not a document)') == 3, out)
    report('a file of another extension is not visited',
           read(base, 'other.txt') == files['other.txt'] and
           'other.txt' not in out, out)
    report('the document of the right version is left as it is',
           read(base, 'sub/b3.doc') == files['sub/b3.doc'] and
           'unchanged: ' + os.path.join(base, 'sub/b3.doc') in out, out)
    report('a symbolic link is skipped',
           'skipped: ' + os.path.join(base, 'link.doc') +
           ' (symbolic link)' in out and
           os.path.islink(os.path.join(base, 'link.doc')), out)
    report('no file is made: no backup and no temporary file',
           listing(base) == before, sorted(set(listing(base)) ^ set(before)))
    report('the permissions are kept',
           stat.S_IMODE(os.stat(os.path.join(base, 'perm.doc')).st_mode)
           == 0o640)
    snap = {r: read(base, r) for r in files}
    rc2, out2, err2 = run(prog, [base])
    report('the same run again changes nothing and exits 0',
           rc2 == 0 and not status_lines(out2, 'changed') and
           all(read(base, r) == snap[r] for r in files), out2)
    rc3, out3, err3 = run(prog, ['--check', base])
    report('--check exits 0 when every document is current',
           rc3 == 0 and 'would change: ' not in out3, out3)
    rc4, out4, err4 = run(prog, ['--quiet', base])
    report('--quiet leaves out the lines of the unchanged files',
           not status_lines(out4, 'unchanged') and
           'docversion: 0 changed' in out4, out4)

    chk = tempfile.mkdtemp(prefix='t2', dir=scratch)
    cfiles = make_tree(chk)
    cbefore = listing(chk)
    rc, out, err = run(prog, ['--check', chk])
    report('--check exits 1 when a document would change', rc == 1, out)
    report('--check names the files that would change',
           len(status_lines(out, 'would change')) == 7, out)
    report('--check writes nothing',
           all(read(chk, r) == cfiles[r] for r in cfiles) and
           listing(chk) == cbefore)

    to4 = tempfile.mkdtemp(prefix='t3', dir=scratch)
    tfiles = make_tree(to4)
    rc, out, err = run(prog, ['--to', '4', to4])
    report('--to 4 converts the documents of version 2 and 3 to 4',
           rc == 0 and read(to4, 'a2.doc').startswith(b'*R4\r\n') and
           read(to4, 'sub/b3.doc').startswith(b'*R4\r\n'), out)
    report('--to 4 leaves a document of version 4 as it is',
           read(to4, 'sub/c4.set') == tfiles['sub/c4.set'], out)

    ex = tempfile.mkdtemp(prefix='t4', dir=scratch)
    xfiles = make_tree(ex)
    rc, out, err = run(prog, [os.path.join(ex, 'other.txt')])
    report('a file named on the command line is examined whatever its '
           'extension is',
           rc == 0 and read(ex, 'other.txt').startswith(b'*R3\r\n') and
           read(ex, 'a2.doc') == xfiles['a2.doc'], out)

    ro = tempfile.mkdtemp(prefix='t5', dir=scratch)
    rfiles = make_tree(ro, readonly=True)
    rc, out, err = run(prog, [ro])
    report('a read-only file is reported as failed and the exit status is 1',
           rc == 1 and 'failed: ' + os.path.join(ro, 'ro.doc') in out, out)
    report('a read-only file is not changed',
           read(ro, 'ro.doc') == rfiles['ro.doc'])
    report('the other files are converted in the same run',
           read(ro, 'a2.doc').startswith(b'*R3\r\n'), out)

    report('no path is a usage error with status 2',
           run(prog, [])[0] == 2)
    report('--to without a number is a usage error with status 2',
           run(prog, ['--to'])[0] == 2 and
           run(prog, ['--to', 'x', base])[0] == 2)
    report('an unknown option is a usage error with status 2',
           run(prog, ['--bogus', base])[0] == 2)
    rc, out, err = run(prog, [os.path.join(base, 'nothing.doc')])
    report('a path that does not exist is reported as failed with status 1',
           rc == 1 and 'failed: ' in out and 'no such file' in out, out)

    if repo:
        with open(os.path.join(root, 'Makefile')) as f:
            m = re.search(r'^DOCVERSION_DIRS = (.*)$', f.read(), re.M)

        report('the Makefile target docversion visits the directories of '
               'the test', m is not None and
               tuple(m.group(1).split()) == PRODUCT_DIRS,
               (m.group(1) if m else None, PRODUCT_DIRS))
        dirs = [d for d in PRODUCT_DIRS
                if os.path.isdir(os.path.join(root, d))]
        rc, out, err = run(prog, ['--check', '--quiet'] + dirs, cwd=root)
        report('every document of the product is at the current version',
               rc == 0 and not status_lines(out, 'would change'),
               out[:600])
        header = None
        for hdr in sorted(os.listdir(os.path.join(root, 'src'))):
            if hdr.endswith('.h'):
                with open(os.path.join(root, 'src', hdr)) as f:
                    m = re.search(r'#define\s+STRATA_FILE_FORMAT\s+(\d+)',
                                  f.read())

                if m is not None:
                    header = int(m.group(1))

        if header is not None:
            cur = re.search(r'^CURRENT_FORMAT = (\d+)', open(prog).read(),
                            re.M)
            report('the default target is the version of the software',
                   cur is not None and int(cur.group(1)) == header,
                   (cur.group(1) if cur else None, header))


# The mutations: the text of the tool that is replaced, and what it is
# replaced by.  A mutation that no check finds fails the test.
MUTATIONS = (
    ('a backup file is made',
     '        os.replace(tmp, path)',
     '        open(path + ".bak", "wb").write(content)\n'
     '        os.replace(tmp, path)'),
    ('the rest of the file is changed',
     'new = b"*R" + str(target).encode("ascii") + suffix + rest',
     'new = b"*R" + str(target).encode("ascii") + suffix + rest[:-2] + b"X"'),
    ('the suffix of the first record is dropped',
     'new = b"*R" + str(target).encode("ascii") + suffix + rest',
     'new = b"*R" + str(target).encode("ascii") + rest'),
    ('a higher version is lowered',
     '    if version > target:',
     '    if False:'),
    ('the permissions are lost',
     '        os.chmod(tmp, mode)',
     '        pass'),
    ('--check writes the files',
     '    if check:\n        return "would change", text',
     '    if False:\n        return "would change", text'),
    ('a read-only file is changed',
     '    if (mode & stat.S_IWUSR) == 0:',
     '    if False:'),
    ('a file of another extension is visited',
     '                    if n.lower().endswith(EXTENSIONS):',
     '                    if True:'),
    ('a symbolic link is followed',
     '    if os.path.islink(path):\n        return "skipped", "symbolic link"',
     '    if False:\n        return "skipped", "symbolic link"'),
)


def main():
    fails = 0
    scratch = tempfile.mkdtemp(prefix='docversion')

    def report(name, ok, detail=''):
        nonlocal fails
        print(('ok   ' if ok else 'FAIL ') + name +
              ('' if ok else '\n' + str(detail)))
        fails += 0 if ok else 1

    run_checks(tool, report, scratch, True)

    with open(tool) as f:
        source = f.read()

    for name, old, new in MUTATIONS:
        mdir = tempfile.mkdtemp(prefix='m', dir=scratch)
        mtool = os.path.join(mdir, 'docversion.py')
        anchored = source.count(old) == 1

        if not anchored:
            report('a mutation can be made: ' + name, False,
                   'the text of the tool to change was not found once')
            continue

        with open(mtool, 'w') as f:
            f.write(source.replace(old, new))

        found = []

        def note(n, ok, detail=''):
            if not ok:
                found.append(n)

        run_checks(mtool, note, mdir, False)
        report('a check fails when the feature is off: ' + name,
               len(found) > 0, 'no check found the mutation')

    shutil.rmtree(scratch, ignore_errors=True)
    print('docversion: %s' % ('all passed' if fails == 0 else
                              '%d FAILURES' % fails))

    return 1 if fails else 0


sys.exit(main())
