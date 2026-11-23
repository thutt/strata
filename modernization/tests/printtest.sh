#!/bin/bash
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# printtest.sh - run strprint on every document of some directories, by
# every route.
#
#     tests/printtest.sh              run all the documents
#     tests/printtest.sh all          the same
#     tests/printtest.sh help         list the targets
#     tests/printtest.sh one FILE     run one document
#
# The environment says where everything is:
#   STRPRINT  the program.
#   SAMPLES   the directories of the documents, separated by colons, searched
#             in all their subdirectories.
#   RESULTS   the directory that gets one subdirectory for every document,
#             with the .tex, the .pdf, the .dvi, the logs and a PNG of the
#             first page, and the file SUMMARY.txt.
#   PATTERN   the names of the documents, default "*.doc" (case is ignored).
#   ROUTES    the routes to run, default "pdf dvi via-dvi": pdflatex writing
#             the PDF, latex writing the DVI, latex followed by dvipdfmx.
#   APPEND    when set, the old lines of SUMMARY.txt are kept.
# Every line of SUMMARY.txt has the name, the route, ok or FAIL, the number
# of pages, the seconds and the marker types that were printed in the long
# form.  The exit status is 1 when a run failed.

function help {
    echo "Targets (the default is all):"
    echo "  all        Run strprint on every document of SAMPLES."
    echo "  one FILE   Run strprint on one document."
    echo "  help       Show this list."
}

# The number of pages of a DVI file.
function dvi_pages {
    dvitype "${1}" 2> /dev/null | grep -c 'beginning of page'
}

# Run one document by one route and print its line of the summary.
function run_one {
    local doc="${1}" route="${2:-pdf}" stem name dir start end pages status
    local list kept opt out
    name="$(basename "${doc}")"
    stem="${name%.*}"
    dir="${RESULTS}/${stem}"
    mkdir -p "${dir}"

    case "${route}" in
        pdf) opt=""; out="${dir}/${stem}.pdf" ;;
        dvi) opt="--dvi"; out="${dir}/${stem}.dvi" ;;
        via-dvi) opt="--pdf-via-dvi"; out="${dir}/${stem}-via-dvi.pdf" ;;
    esac

    rm -f "${out}" "${dir}/${stem}-${route}.log"
    start="$(date +%s%N)"
    list="$("${STRPRINT}" "${doc}" ${opt} -o "${out}" --keep --long-list \
        2> "${dir}/strprint-${route}.err" | sed -n 's/^longform: *//p')"
    status="${PIPESTATUS[0]}"
    end="$(date +%s%N)"
    kept="$(sed -n 's/^strprint: files kept in //p' \
        "${dir}/strprint-${route}.err")"

    if [ -n "${kept}" ] && [ -d "${kept}" ]; then
        if [ "${route}" = pdf ]; then
            cp "${kept}/doc.tex" "${dir}/${stem}.tex" 2> /dev/null
        fi

        cp "${kept}/doc.log" "${dir}/${stem}-${route}.log" 2> /dev/null
        rm -rf "${kept}"
    fi

    if [ "${status}" = 0 ] && [ -f "${out}" ]; then
        if [ "${route}" = dvi ]; then
            pages="$(dvi_pages "${out}")"
        else
            pages="$(pdfinfo "${out}" 2> /dev/null | sed -n 's/^Pages: *//p')"
            pdftoppm -r 60 -png -f 1 -l 1 -singlefile "${out}" \
                "${dir}/${stem}-${route}-page1" 2> /dev/null
        fi

        status=ok
    else
        pages=0
        status=FAIL
        head -3 "${dir}/strprint-${route}.err" >&2
    fi

    line="$(printf '%-14s %-8s %-4s pages=%-4s time=%ss' "${stem}" "${route}" \
        "${status}" "${pages:-0}" "$(( (end - start) / 1000000000 ))")"
    list="long=$(echo ${list} | tr ' ' ',')"

    # A line is at most 80 characters: a long list goes on the next line.
    if [ $(( ${#line} + ${#list} + 1 )) -le 80 ]; then
        echo "${line} ${list}"
    else
        echo "${line}"
        echo "                        ${list}"
    fi
}

function run_all {
    local f r d failed=0

    if [ -z "${APPEND}" ]; then
        : > "${RESULTS}/SUMMARY.txt"
    fi

    for d in ${SAMPLES//:/ }; do
        while IFS= read -r f; do
            for r in ${ROUTES:-pdf dvi via-dvi}; do
                run_one "${f}" "${r}" | tee -a "${RESULTS}/SUMMARY.txt"
            done
        done < <(find "${d}" -type f -iname "${PATTERN:-*.doc}" | sort)
    done

    grep -q ' FAIL ' "${RESULTS}/SUMMARY.txt" && failed=1
    echo "runs: $(grep -c '^[^ ]' "${RESULTS}/SUMMARY.txt")," \
        "failed: $(grep -c ' FAIL ' "${RESULTS}/SUMMARY.txt")"
    return "${failed}"
}

function main {
    : "${STRPRINT:?STRPRINT is not set}"
    : "${SAMPLES:?SAMPLES is not set}"
    : "${RESULTS:?RESULTS is not set}"
    mkdir -p "${RESULTS}"

    case "${1:-all}" in
        help) help ;;
        all) run_all ;;
        one) run_one "${2}" "${3:-pdf}" ;;
        *) help; return 2 ;;
    esac
}

main "${@}"
