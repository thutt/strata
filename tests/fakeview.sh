#!/bin/bash
# A stand-in for the DVI viewer, for the tests of F12, Print, Preview.
#
# The preview starts it as "viewer FILE.DVI" (STRATA_DVIVIEW names it).  It
# records what it was given in the directory named by PVREC: the argument,
# whether the file exists, a copy of the DVI file and of the document part
# that was written next to it, the mode of the directory, and its own process
# id.  Then it waits PVHOLD seconds (default 0) and ends with the status
# PVSTATUS (default 0).  The preview also gives it "--report FILE", where the
# real viewer tells what it could not show: with PVNOTE set the fake writes
# the line "note: $PVNOTE" there, with PVFAIL set the line "fail: $PVFAIL".
# The record "args" holds one argument of the command line on each line, and
# "argc" the number of arguments, so that a test can tell that the list of
# arguments that the Editor gave was complete and ended where it should.

rec="${PVREC}"
dvi="${1}"

function record {
    echo "${dvi}" > "${rec}/arg"

    if [ -f "${dvi}" ]; then
        echo yes > "${rec}/exists"
        cp "${dvi}" "${rec}/copy.dvi"
    else
        echo no > "${rec}/exists"
    fi

    cp "$(dirname "${dvi}")/part.doc" "${rec}/part.doc"
    stat -c %a "$(dirname "${dvi}")" > "${rec}/mode"
    echo "${$}" > "${rec}/pid"
    echo "${#}" > "${rec}/argc"
    printf '%s\n' "${@}" > "${rec}/args"
    ls "$(dirname "${dvi}")" > "${rec}/listing"
}

function report {
    local file=""

    while [ "${#}" -gt 0 ]; do
        if [ "${1}" = "--report" ]; then
            file="${2}"
        fi

        shift
    done

    if [ -n "${file}" ]; then
        : > "${file}"

        if [ -n "${PVNOTE:-}" ]; then
            echo "note: ${PVNOTE}" >> "${file}"
        fi

        if [ -n "${PVFAIL:-}" ]; then
            echo "fail: ${PVFAIL}" >> "${file}"
        fi
    fi
}

record "${@}"
report "${@}"
sleep "${PVHOLD:-0}"
exit "${PVSTATUS:-0}"
