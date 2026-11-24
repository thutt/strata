#!/bin/bash
# Copyright (c) 2026 Logic Magicians Software
#
# A stand-in for the program that opens a PDF, for the tests of Open to
# print.  The setting pdf_viewer names it, or it is put in PATH as xdg-open,
# gio or open.
#
# It appends one line to the file FAKEOPEN_LOG: its name and every argument,
# separated by "|", and, when the last argument is a file, its size and its
# first four bytes after "size=" and "magic=".  The program that started it
# does not wait for it, so the line may come a moment later.  With
# FAKEOPEN_SLEEP it stays alive that many seconds, so that a test can see
# that it is not waited for.
#
set -o pipefail;
set -o nounset;
set -o errexit;

log="${FAKEOPEN_LOG:-/dev/null}"
args=""
last=""

for a in "${@}"; do
    args="${args}|${a}"
    last="${a}"
done

extra=""

if [ -f "${last}" ]; then
    extra=" size=$(stat -c %s "${last}") magic=$(head -c 4 "${last}")"
fi

echo "$(basename "${0}")${args}${extra}" >> "${log}"
sleep "${FAKEOPEN_SLEEP:-0}"
exit 0
