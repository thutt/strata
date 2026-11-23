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

"""Change the file format version of documents.

    python3 -I -B tools/docversion.py [--check] [--quiet] [--to N] PATH...

Every file that uses the header of a document (the .DOC documents, the .SET
templates, the .LIB library files and the .MAC macro files) starts with the
record '*R<n>', and the number n is the version of the file format.  This
program visits the named files and directories and changes the number of
every such file whose version is lower than the target to the target.  It
writes nothing else: the first record is replaced and every other byte of
the file stays as it is.  No backup file is made.  The text after the
number in the first record (the 'h' of the older library and macro
files) is kept.

The files that are visited:

  - A directory is searched recursively, without following symbolic links,
    and the files whose extension is .doc, .set, .lib or .mac (in any case)
    are examined.  The other files are not visited.
  - A file that is named on the command line is examined whatever its
    extension is.
  - A file whose first record is not '*R<n>' is not a document: it is
    reported and left alone.

The output has one line for each file examined:

    changed: PATH (2 -> 3)       the first record was rewritten
    would change: PATH (2 -> 3)  with --check: nothing was written
    unchanged: PATH (version 3)  the version is the target already
    skipped: PATH (REASON)       not a document, a symbolic link, or a
                                 version above the target (never lowered)
    failed: PATH (REASON)        read-only, or a read or write error

and a last line with the counts.  --quiet leaves out the 'unchanged'
lines.  The exit status is 0 when every file is current or was converted,
1 when a file failed (or with --check when a file would change) and 2 for a
usage error.  The program is idempotent.

The default target is the current file format version of the software.
"""

import os
import re
import stat
import sys
import tempfile

# The current file format version of the software (STRATA_FILE_FORMAT in
# the sources of the programs).  tests/docversion.py compares the two when
# the constant exists.
CURRENT_FORMAT = 3

# The extensions of the files that use the header of a document.
EXTENSIONS = (".doc", ".set", ".lib", ".mac")

# The first record: '*R', the version, and at most eight other bytes that
# are not control characters (the 'h' of the old libraries and macros).
FIRST_RECORD = re.compile(rb"^\*R([0-9]{1,9})([^\x00-\x1f\x7f]{0,8})$")


def usage():
    sys.stderr.write(
        "usage: docversion.py [--check] [--quiet] [--to N] PATH...\n")

    return 2


def split_first_record(data):
    """The first record of data and the rest, or None without a record end."""
    end = data.find(b"\r\n")

    if end < 0:
        return None

    return data[:end], data[end:]


def examine(path):
    """Read path: its version, the suffix of its first record and its data.

    The result is a tuple (version, suffix, rest, data) for a document and
    a string with the reason for anything else.
    """
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        return "unreadable: %s" % e.strerror

    parts = split_first_record(data)

    if parts is None:
        return "not a document"

    m = FIRST_RECORD.match(parts[0])

    if m is None:
        return "not a document"

    return int(m.group(1)), m.group(2), parts[1], data


def write_atomic(path, content, mode):
    """Replace path by content through a temporary file in its directory."""
    fd, tmp = tempfile.mkstemp(prefix=".docversion-",
                               dir=os.path.dirname(path) or ".")

    try:
        with os.fdopen(fd, "wb") as f:
            f.write(content)

        os.chmod(tmp, mode)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)

        raise


def convert(path, target, check):
    """Convert one file.  The result is the status word and the text."""
    if os.path.islink(path):
        return "skipped", "symbolic link"

    info = examine(path)

    if isinstance(info, str):
        if info.startswith("unreadable"):
            return "failed", info

        return "skipped", info

    version, suffix, rest, data = info

    if version == target:
        return "unchanged", "version %d" % version

    if version > target:
        return "skipped", "version %d is above %d" % (version, target)

    text = "%d -> %d" % (version, target)
    mode = os.stat(path).st_mode

    if (mode & stat.S_IWUSR) == 0:
        return "failed", "read-only, version %d" % version

    if check:
        return "would change", text

    new = b"*R" + str(target).encode("ascii") + suffix + rest

    try:
        write_atomic(path, new, stat.S_IMODE(mode))
    except OSError as e:
        return "failed", "%s, version %d" % (e.strerror, version)

    return "changed", text


def visit(args):
    """The files named by args, directories searched, in a fixed order."""
    names = []

    for a in args:
        if os.path.isdir(a) and not os.path.islink(a):
            for top, dirs, files in os.walk(a, followlinks=False):
                dirs.sort()

                for n in sorted(files):
                    if n.lower().endswith(EXTENSIONS):
                        names.append(os.path.join(top, n))
        else:
            names.append(a)

    return names


def main(argv):
    check = False
    quiet = False
    target = CURRENT_FORMAT
    paths = []
    i = 0

    while i < len(argv):
        a = argv[i]

        if a == "--check":
            check = True
        elif a == "--quiet":
            quiet = True
        elif a == "--to":
            i += 1

            if i >= len(argv) or not argv[i].isdigit():
                return usage()

            target = int(argv[i])
        elif a.startswith("-") and a != "-":
            return usage()
        else:
            paths.append(a)

        i += 1

    if not paths:
        return usage()

    counts = {}

    for path in visit(paths):
        if not os.path.lexists(path):
            status, text = "failed", "no such file"
        else:
            status, text = convert(path, target, check)

        counts[status] = counts.get(status, 0) + 1

        if status == "unchanged" and quiet:
            continue

        print("%s: %s (%s)" % (status, path, text))

    would = counts.get("would change", 0)
    print("docversion: %d changed, %d would change, %d unchanged, "
          "%d skipped, %d failed" % (counts.get("changed", 0), would,
                                      counts.get("unchanged", 0),
                                      counts.get("skipped", 0),
                                      counts.get("failed", 0)))

    if counts.get("failed", 0) > 0 or would > 0:
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
