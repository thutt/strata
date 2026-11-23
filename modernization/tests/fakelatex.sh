#!/bin/bash
# A stand-in for latex, for the tests of F12, Print, Preview.  It is put
# in a directory at the front of PATH under the name latex.
#
# PVFAKE says what it does.  "error": print a TeX error and stop with
# status 1, without a DVI file.  "errordvi": the same, but a DVI file (a
# copy of the file PVDVI) is left behind, as TeX does in nonstopmode.
# "slow": print pages "[1]", "[2]" and so on, one every tenth of a second,
# for PVPAGES pages, and make a DVI file.  The log file is doc.log.

mode="${PVFAKE:-error}"

function fail {
    echo "This is FakeTeX"
    echo "! Fake error."
    echo "l.1 the fake line"
    {
        echo "This is FakeTeX"
        echo "! Fake error."
        echo "l.1 the fake line"
    } > doc.log

    if [ "${mode}" = "errordvi" ]; then
        cp "${PVDVI}" doc.dvi
    fi

    exit 1
}

function slow {
    local i

    echo "This is FakeTeX"

    for ((i = 1; i <= ${PVPAGES:-10}; i++)); do
        echo -n "[${i}] "
        sleep 0.1
    done

    echo
    echo "Output written on doc.dvi (${PVPAGES:-10} pages, 0 bytes)."
    echo "Output written on doc.dvi" > doc.log
    cp "${PVDVI}" doc.dvi
    exit 0
}

if [ "${mode}" = "slow" ]; then
    slow
fi

fail
