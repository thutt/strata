#!/bin/bash
# A stand-in for aspell, for the tests of the spell check.
#
# It speaks the ispell pipe protocol (STRATA_SPELL names it): a banner, then
# for every line that begins with ^ the words that it does not know, and a
# blank line.  The words it does not know are fixed:
#
#   teh        two suggestions: the, tech
#   quikc      quick, quirk
#   wrold      world
#   xcase      the one suggestion Xcase, which only differs in case
#   zzq        none ("#" answer)
#   dont       don't (a word of dont-stop: the part is reported)
#   foootnote  a word of a note: footnote
#
# Every line it receives is appended to the file named by FAKESPELL_LOG, so
# that a test can see that the document was sent line by line.

log="${FAKESPELL_LOG}"

function suggestions {
    case "${1}" in
        teh) echo "& teh 2 ${2}: the, tech" ;;
        quikc) echo "& quikc 2 ${2}: quick, quirk" ;;
        wrold) echo "& wrold 1 ${2}: world" ;;
        xcase) echo "& xcase 1 ${2}: Xcase" ;;
        zzq) echo "# zzq ${2}" ;;
        dont) echo "& dont 1 ${2}: don't" ;;
        foootnote) echo "& foootnote 1 ${2}: footnote" ;;
        *) ;;
    esac
}

function answer {
    local line="${1}"
    local pos=0
    local word
    local rest="${line}"

    # The offset counts the ^ in front of the line, so it starts at 1.
    while [ -n "${rest}" ]; do
        word="${rest%% *}"

        if [ "${word}" = "${rest}" ]; then
            rest=""
        else
            rest="${rest#* }"
        fi

        # A hyphenated word is looked at in its parts, as aspell does.
        local part="${word%%-*}"

        suggestions "${part}" "$((pos + 1))"
        pos=$((pos + ${#word} + 1))
    done

    echo
}

echo "@(#) International Ispell Version 3.1.20 (fake)"

while IFS= read -r line; do
    if [ -n "${log}" ]; then
        echo "${line}" >> "${log}"
    fi

    case "${line}" in
        ^*) answer "${line#^}" ;;
        *) ;;
    esac
done
