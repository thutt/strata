#!/bin/bash
# A stand-in for lp, for the tests of F12, Print.  STRATA_LP names it.
#
# It appends one line to the file FAKELP_LOG: every argument in the order
# given, separated by "|".  When the last argument is a file, its size is
# recorded after "size=" and its first four bytes after "magic=".  What it
# does next depends on FAKELP_MODE: "ok" (the default) prints what the real
# lp prints, "fail" prints an error and stops with status 1, and "slow"
# waits for FAKELP_SECONDS seconds first.

log="${FAKELP_LOG:-/dev/null}"
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

echo "lp${args}${extra}" >> "${log}"

case "${FAKELP_MODE:-ok}" in
    fail)
        echo "lp: The printer or class does not exist." >&2
        exit 1
        ;;
    slow)
        sleep "${FAKELP_SECONDS:-30}"
        ;;
    *)
        ;;
esac

echo "request id is fakeq-42 (1 file(s))"
exit 0
