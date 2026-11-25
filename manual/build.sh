#!/bin/bash
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This script builds the manual.  The product is ONE document, the whole
# Reference Manual, strata-reference-manual.doc, and its PDF,
# strata-reference-manual.pdf (see below for where they are made; the copy
# of the document that the package ships is doc/strata-reference-manual.doc
# in the source tree and is brought up to date by "build.sh publish").  With
# no argument the script builds that
# (the target "single"), and the guides and cards that stay separate (the
# target "cards": Key Reference, Getting Started (with the Tutorial and the
# system requirements), File Formats).
# The pictures are not part of it (the target "pics").  The programs are
# found in ${XBIN} (the x11 build directory) and in the PATH.
#
# How a maintainer works.  The sources are manual/src/*.src, described in
# the header of mkmanual.py, and manual/src/master.txt gives the order of
# the chapters.  There are two ways to change the manual.
#
#   1. Edit a chapter source, then run "build.sh single".  The generator
#      reads the sources as they are on the day it runs, so the whole
#      document, its contents, its index, its cross-references and its
#      page numbers are made again, and nothing that was changed by hand
#      in the .doc is kept.  This is the way that keeps the sources and
#      the document the same.
#   2. For a small correction, open doc/strata-reference-manual.doc in the
#      Editor and change it there.  Contents and index entries are
#      cross-references and their page numbers are made when the document
#      is printed, so they follow the edit.  The next "build.sh single"
#      makes the document from the sources again and the correction is lost
#      unless it is also made in the source.  Make the same correction in
#      the chapter source.
#
# By hand, not in the build (the text it needs is not in the repository):
#   python3 -I -B manual/checkcopy.py PATH-OF-OTHER-TEXT
# measures the runs of eight words that the sources share with that text.
#
# The per-chapter targets (docs and pdf) make one document and one PDF for
# each chapter, as before, and are kept as options.
#
# Editions.  The documents hold conditional text (the If, Else and Endif
# markers, written by the directives .if, .else and .endif of the sources).
# The PDF of the manual that is shipped is printed with the special variable
# .STRATA_BUILD_TYPE set to release, so the text that is for developers is
# left out of it; the target "developer" prints the same document with the
# value debug into pdf/developer, and "check-edition" proves that the text
# of the branches that a release leaves out is not in the release PDF and is
# in the developer PDF.
# Where the products go.  A build never writes a product into the source
# tree.  Every target writes into one directory outside the tree, ${out}:
# the documents and their lists of headlines in ${out}/doc, the sample
# documents in ${out}/samples and the PDF files in ${out}/pdf.  ${out} is
# ${MANUAL_OUT} if that is set, else the manual folder of the build
# directory that scripts/setup exports (STRATA_BUILD_DIR), else the manual
# folder next to the x11 build directory ${XBIN}.  PDFDIR, if it is set,
# receives the PDF files instead.  The documents of doc/ in the source tree
# (the Reference Manual and the cards, which the package ships) and the
# pictures of doc/pictures are sources that are kept in git, and only
# two targets write into the tree, on purpose: "publish" copies the
# documents that were made into the tree, and "pics" takes the pictures.
#
set -o pipefail;
set -o nounset;
set -o errexit;

# A bytecode folder is a product too: never write one into the tree.  The
# option -I of python3 ignores the variable, so every call has -B as well.
export PYTHONDONTWRITEBYTECODE=1

here=$(cd "$(dirname "${0}")" && pwd)
src=${here}/src
srcdoc=${here}/doc
xbin=${XBIN:-/tmp/strata-ref/debug/x11}
out=${MANUAL_OUT:-${STRATA_BUILD_DIR:-$(dirname "${xbin}")}/manual}
doc=${out}/doc
smp=${out}/samples
pdf=${PDFDIR:-${out}/pdf}
mk="python3 -I -B ${here}/mkmanual.py"
cardnames="cheat cheat-short mathcheat GettingStarted"
cardnames="${cardnames} errata strata-file-format"

function help
{
    echo "Every target writes into \${out} (outside the source tree):"
    echo "  \${MANUAL_OUT}, else \${STRATA_BUILD_DIR}/manual, else the"
    echo "  manual folder next to \${XBIN}; PDFDIR names another folder for"
    echo "  the PDF files.  Only publish and pics write into the tree."
    echo "Targets (the default is all, which is single and cards):"
    echo "  single   Make the whole Reference Manual as ONE document,"
    echo "           strata-reference-manual.doc, and its PDF (the"
    echo "           release edition: no text for developers)."
    echo "  publish  Make the documents and the samples and copy them into"
    echo "           the source tree (doc/ and samples/), where the package"
    echo "           takes them from.  The only target that writes there"
    echo "           besides pics."
    echo "  developer  Print the document with the text for developers into"
    echo "           pdf/developer of the products (not shipped)."
    echo "  check-edition  Prove that the release PDF has none of the text"
    echo "           that is for developers and the developer PDF has it."
    echo "  cards    Make the Key Reference, the Math Symbols card, Getting"
    echo "           Started (it has the Tutorial and the system"
    echo "           requirements) and File Formats"
    echo "           documents and their PDF files."
    echo "  docs     Make a .doc file for every chapter."
    echo "  samples  Make the sample documents."
    echo "  pics     Take the pictures with shots.py (needs Xvfb, xwd) into"
    echo "           doc/pictures of the source tree: they are sources."
    echo "           Scene names after the target take only those scenes."
    echo "  pics-scripts DIR  Make the pages that the scripts of scenes/ print"
    echo "           again into DIR (the part of pics-check for them)."
    echo "  pics-check  Take every picture again into a scratch folder (the"
    echo "           scenes of shots.py and the pages that the scripts of"
    echo "           scenes/ print) and"
    echo "           list the pictures that differ from those of doc/pictures"
    echo "           (PICNEW names the folder, PICFOOT rows are left out at the"
    echo "           foot, PICTHRESHOLD is the pixels of shape that differ,"
    echo "           PICCOLOURTHRESHOLD the pixels of colour that differ)."
    echo "  check    Round-trip the single document and the cards,"
    echo "           and run check-names, check-pictable, check-numbers,"
    echo "           check-impl, check-keys, check-style, check-strata,"
    echo "           check-latexspace, check-overfull, check-tables and the"
    echo "           leaflet check."
    echo "  check-numbers  Fail when a headline of the single document has a"
    echo "           number typed into its text, or its section tags are off:"
    echo "           the manual uses the numbering of the product."
    echo "  check-math  Typeset every symbol of the Math Symbols card with"
    echo "           the Printer, compare the symbols with those of the"
    echo "           packages, and check the layout of the card."
    echo "  check-names  Fail on a forbidden word in the sources."
    echo "  check-pictable  Fail when a picture of doc/pictures has no line in"
    echo "           pictures.txt, or a line has no picture."
    echo "  check-keys   Fail on a key that is not written as a keycap."
    echo "  check-style  Fail when the sources break the style rules: a"
    echo "           command, option, path, file name or variable of the"
    echo "           system that is not in the program font, a shell"
    echo "           variable without braces, a command line inside a"
    echo "           sentence, or the words rights, computer and prefix;"
    echo "           and the editorial rules of the Reference Manual"
    echo "           (checkeditorial.py): filler words, narration, future"
    echo "           tense, contractions, units, spaces, spelling and terms,"
    echo "           typed cross references, the case of headlines, the"
    echo "           ends of lists and table cells, and broken tables;"
    echo "           and the facts (checkfacts.py): every message, menu"
    echo "           entry, command line option and marker of the programs"
    echo "           is in the manual; and the spelling (checkspell.py)."
    echo "  check-tables Fail when a table of a document does not have the"
    echo "           borders of the rule (header row: single lines and a"
    echo "           double line below, bold text; other rows: single lines"
    echo "           at the left, the right, between the columns and below)."
    echo "  check-strata Fail on the word Strata as plain text in a document."
    echo "  check-latexspace Fail on vertical space in a LaTeX marker."
    echo "  check-overfull Fail when TeX reports that a line or the text of a"
    echo "           cell of a table is wider than its space: it runs past the"
    echo "           border of the cell or the edge of the text."
    echo "  check-impl   Fail on implementation detail in the sources."
    echo "  check-tree   Build every target that makes products, and fail if"
    echo "           a file was written into the source tree."
    echo "  pdf      Typeset every document of docs with the Printer into"
    echo "           the PDF folder."
    echo "  all      single and cards."
    echo "  clean    Delete the products (the whole of \${out})."
    echo "  help     Show this list."
}

# The basenames of the documents, in the order of the master.
function names
{
    grep -v '^#' "${src}/master.txt" | awk 'NF > 0 { print $2 }'
}

# The parts: documents that other documents include with the Include marker
# (the .includedoc directive).  Each is made from a source of its own that is
# not in the master, and is kept next to the document that includes it.
partnames="terminal-keys"

function parts
{
    local n

    mkdir -p "${doc}"
    for n in ${partnames}; do
        ${mk} "${src}/${n}.src" "${doc}/${n}.doc" || return 1
    done
}

function docs
{
    local n
    local front
    mkdir -p "${doc}"
    # The first pass writes the .meta files; the second knows every label.
    for pass in 1 2; do
        if [ "${pass}" = 2 ]; then
            cat "${doc}"/*.doc.meta | grep '^H' > "${doc}/labels.txt"
        else
            : > "${doc}/labels.txt"
        fi
        for n in $(names); do
            if [ -f "${src}/${n}.src" ]; then
                ${mk} --labels "${doc}/labels.txt" "${src}/${n}.src" \
                    "${doc}/${n}.doc" || return 1
            fi
        done
    done
    ${mk} --toc "${src}/master.txt" "${doc}/contents.doc" || return 1
    ${mk} --figures "${src}/master.txt" "${doc}/figures.doc" || return 1
    ${mk} --index "${src}/master.txt" "${doc}/index.doc" || return 1
    parts || return 1
}

# The single document and its PDF.  The generator reads every chapter source
# of the master as it is now.
function single
{
    mkdir -p "${doc}" "${pdf}"
    parts || return 1
    ${mk} --single "${src}/master.txt" \
        "${doc}/strata-reference-manual.doc" || return 1
    # The Printer looks for an included part in the directory that --docdir
    # names and then in the working directory, so the print runs in ${doc},
    # where the parts that were just made are.
    (cd "${doc}" && "${xbin}/strprint" strata-reference-manual.doc \
        --docdir "${srcdoc}" --special .STRATA_BUILD_TYPE release \
        -o "${pdf}/strata-reference-manual.pdf") || return 1
}

# The developer edition of the manual: the same document printed with the
# build type debug, so that the text for developers is in it.  It is kept
# in its own directory and is not part of what is shipped.
function developer
{
    ensure_single
    parts || return 1
    mkdir -p "${pdf}/developer"
    (cd "${doc}" && "${xbin}/strprint" strata-reference-manual.doc \
        --docdir "${srcdoc}" --special .STRATA_BUILD_TYPE debug \
        -o "${pdf}/developer/strata-reference-manual.pdf") || return 1
}

# The edition check: the text of the branches that a release leaves out is
# not in the release PDF and is in the developer PDF, and no command line of
# a program is in the text of the release PDF or of the guides.
function check-edition
{
    local n

    ensure_single
    if [ ! -f "${pdf}/strata-reference-manual.pdf" ]; then
        single
    fi
    if [ ! -f "${pdf}/developer/strata-reference-manual.pdf" ]; then
        developer
    fi
    python3 -I -B "${here}/checkedition.py" "${src}/master.txt" \
        "${pdf}/strata-reference-manual.pdf" \
        "${pdf}/developer/strata-reference-manual.pdf"
    python3 -I -B "${here}/checkpdf.py" "${pdf}/strata-reference-manual.pdf" \
        "${doc}/strata-reference-manual.doc.meta" --release
    python3 -I -B "${here}/checkpdf.py" \
        "${pdf}/developer/strata-reference-manual.pdf" \
        "${doc}/strata-reference-manual.doc.meta"
    python3 -I -B "${here}/checknames.py" --pdf \
        "${pdf}/strata-reference-manual.pdf"
    for n in GettingStarted mathcheat; do
        if [ -f "${pdf}/${n}.pdf" ]; then
            python3 -I -B "${here}/checknames.py" --pdf "${pdf}/${n}.pdf"
        fi
    done
}

# The documents that stay separate: they are cards and guides, not part of
# the reference.  Their references to the manual use the labels of the
# single document, and their references to each other the labels of the
# first pass.
function cards
{
    local n
    local pass
    local label=${doc}/single-labels.txt

    ensure_single
    mkdir -p "${doc}" "${pdf}"
    if [ -f "${doc}/strata-reference-manual.doc.meta" ]; then
        grep '^H' "${doc}/strata-reference-manual.doc.meta" > "${label}"
    else
        : > "${label}"
    fi
    for pass in 1 2; do
        for n in ${cardnames}; do
            ${mk} --labels "${label}" "${src}/${n}.src" "${doc}/${n}.doc" ||
                return 1
        done
        if [ "${pass}" = 1 ]; then
            for n in ${cardnames}; do
                grep '^H' "${doc}/${n}.doc.meta" >> "${label}"
            done
        fi
    done
    for n in ${cardnames}; do
        "${xbin}/strprint" "${doc}/${n}.doc" --docdir "${srcdoc}" \
            --special .STRATA_BUILD_TYPE release \
            -o "${pdf}/${n}.pdf" || echo "build.sh: ${n} failed"
    done
}

function samples
{
    local s
    mkdir -p "${smp}"
    for s in "${here}"/samples/src/*.src; do
        ${mk} "${s}" "${smp}/$(basename "${s}" .src).doc" || return 1
    done
}

# The single document is needed by the targets that read it.
function ensure_single
{
    if [ ! -f "${doc}/strata-reference-manual.doc" ]; then
        single
    fi
}

# The documents that the checks read: the single document, the cards and
# the samples.
function ensure_all
{
    ensure_single
    if [ ! -f "${doc}/GettingStarted.doc" ]; then
        cards
    fi
    if [ ! -d "${smp}" ]; then
        samples
    fi
}

# The only target besides pics that writes into the source tree: the
# documents that the package ships (doc/ and samples/) are made again from
# the sources and copied there.
function publish
{
    local f
    local n

    single
    cards
    samples
    for n in strata-reference-manual ${cardnames} ${partnames}; do
        cp -f "${doc}/${n}.doc" "${srcdoc}/${n}.doc"
    done
    for f in "${smp}"/*.doc; do
        cp -f "${f}" "${here}/samples/$(basename "${f}")"
    done
}

# The pictures are sources: a picture is taken from the running program and
# looked at before it replaces the old one.  They are kept in git in doc/
# of the source tree, so this target writes there.
function pics
{
    mkdir -p "${srcdoc}/pictures"
    XBIN=${xbin} python3 -I -B "${here}/shots.py" "${srcdoc}/pictures" "${@}"
}

# The pictures that are pages printed by the Printer are made by the scripts
# of manual/scenes, not by shots.py.  They are made again into the folder
# that is given, from the list of checkpictable.py --scripts, so that
# pics-check compares them like the others.  A script that fails is written
# to the log of shots.py as a failed scene, which pics-check lists.
function pics-scripts
{
    local new=${1}
    local list
    local name
    local script
    local document

    list=$(python3 -I -B "${here}/checkpictable.py" --scripts)
    while read -r name script document; do
        if (cd "${here}/.." &&
            sh "${here}/scenes/${script}" "${xbin}/strprint" \
               "${document}" "${new}/${name}.png") \
            >> "${new}/shots.log" 2>&1; then
            echo "shot ${name}" >> "${new}/shots.log"
        else
            echo "shots: ${name} failed" >> "${new}/shots.log"
        fi
    done <<< "${list}"
}


# The pictures taken again, compared with the pictures of the manual.  The
# new pictures are put in a scratch folder (PICNEW, or a new folder in TMPDIR)
# outside the source tree, which is kept so that the differences can be looked
# at.
function pics-check
{
    local new=${PICNEW:-}

    if [ -z "${new}" ]; then
        new=$(mktemp -d "${TMPDIR:-/tmp}/strata-pics.XXXXXX")
    fi

    mkdir -p "${new}"
    XBIN=${xbin} python3 -I -B "${here}/shots.py" "${new}" \
        > "${new}/shots.log" 2>&1 || true
    pics-scripts "${new}"
    echo "The new pictures are in ${new}"
    python3 -I -B "${here}/checkpics.py" "${srcdoc}/pictures" "${new}" \
        --log "${new}/shots.log" --ignore-foot "${PICFOOT:-0}" \
        --threshold "${PICTHRESHOLD:-30}" \
        --colour-threshold "${PICCOLOURTHRESHOLD:-300}"
}


function check-names
{
    python3 -I -B "${here}/checknames.py"
}


# Every picture file has a line in pictures.txt and every line a file.
function check-pictable
{
    python3 -I -B "${here}/checkpictable.py"
}


function check-numbers
{
    ensure_single
    python3 -I -B "${here}/checknumbers.py" docs \
        "${doc}/strata-reference-manual.doc"
}


function check-keys
{
    python3 -I -B "${here}/checkkeys.py"
}


# The style rules of every document (manual/checkstyle.py) and the editorial
# rules of the Reference Manual (manual/checkeditorial.py).
function check-style
{
    local stred="${xbin}/stred";

    python3 -I -B "${here}/checkstyle.py" || return 1
    python3 -I -B "${here}/checkeditorial.py" || return 1
    STRED="${stred}" python3 -I -B "${here}/checkfacts.py" || return 1
    STRED="${stred}" python3 -I -B "${here}/checkfacts.py" \
        --self-test || return 1
    python3 -I -B "${here}/checkspell.py" || return 1
    python3 -I -B "${here}/checkspell.py" --self-test
}


# The borders and the bold header of every table of the documents follow the
# rule of the generator (manual/checktables.py), and the check can fail.
function check-tables
{
    ensure_all
    python3 -I -B "${here}/checktables.py" --self-test
    python3 -I -B "${here}/checktables.py" "${doc}"/*.doc
}


function check-strata
{
    ensure_all
    python3 -I -B "${here}/checkstrata.py" "${doc}"/*.doc "${smp}"/*.doc
}


function check-latexspace
{
    ensure_all
    python3 -I -B "${here}/checklatexspace.py"
}


# TeX measures the text of every document of the manual; a line, or the
# text of a cell, that is wider than the space that it has is an error.  The
# LaTeX file of each document is made by the Printer and typeset twice in a
# private folder, and manual/checkoverfull.py reads the logs.
function check-overfull
{
    local dir
    local f
    local n
    local rc

    ensure_all
    python3 -I -B "${here}/checkoverfull.py" --self-test || return 1
    dir=$(mktemp -d "${out}/overfull.XXXXXX") || return 1
    for f in "${doc}"/*.doc; do
        n=$(basename "${f}" .doc)
        mkdir -p "${dir}/${n}"
        (cd "${doc}" && "${xbin}/strprint" "${f}" --docdir "${srcdoc}" \
            --special .STRATA_BUILD_TYPE release \
            --export-latex "${dir}/${n}/${n}.tex") >/dev/null 2>&1 ||
            echo "build.sh: ${n}: the LaTeX file was not made"
        (cd "${dir}/${n}" && pdflatex -interaction=nonstopmode "${n}.tex" \
            >/dev/null 2>&1; pdflatex -interaction=nonstopmode "${n}.tex" \
            >/dev/null 2>&1)
    done
    python3 -I -B "${here}/checkoverfull.py" "${dir}"/*/*.log
    rc=${?}
    rm -rf "${dir}"
    return ${rc}
}


# The Math Symbols card: every entry through the Printer, the symbols of the
# packages against the list, and (when the PDF of the card is made) its layout.
function check-math
{
    local card=${pdf}/mathcheat.pdf

    if [ -f "${card}" ]; then
        python3 -I -B "${here}/checkmathsyms.py" "${xbin}/strprint" "${card}"
    else
        python3 -I -B "${here}/checkmathsyms.py" "${xbin}/strprint"
    fi
}


function check-impl
{
    python3 -I -B "${here}/checkimpl.py"
}


# The build must not write into the source tree (tests/srctree.py).
function check-tree
{
    XBIN="${xbin}" python3 -I -B "${here}/../tests/srctree.py" --full
}

function check
{
    local rt=${RT:-$(dirname "${xbin}")/term/roundtrip}

    ensure_all
    "${rt}" "${doc}/strata-reference-manual.doc"
    for n in ${cardnames}; do
        "${rt}" "${doc}/${n}.doc"
    done

    check-names
    check-pictable
    check-numbers
    check-impl
    check-keys
    check-style
    check-strata
    check-latexspace
    check-overfull
    check-tables
    check-math
    python3 -I -B "${here}/checkcheat.py" "${here}/KEYS.txt" \
        "${src}/cheat.src"
}

function pdf
{
    local f
    local n
    mkdir -p "${pdf}"
    for f in "${doc}"/*.doc; do
        n=$(basename "${f}" .doc)
        "${xbin}/strprint" "${f}" --docdir "${srcdoc}" \
            --special .STRATA_BUILD_TYPE release \
            -o "${pdf}/${n}.pdf" || echo "build.sh: ${n} failed"
    done
}

function clean
{
    rm -rf "${out}"
    rm -f "${pdf}"/*.pdf "${pdf}"/developer/*.pdf
}

target=${1:-all}
case "${target}" in
    help) help ;;
    single) single ;;
    developer) developer ;;
    check-edition) check-edition ;;
    cards) cards ;;
    docs) docs ;;
    samples) samples ;;
    publish) publish ;;
    pics) shift; pics "${@}" ;;
    pics-check) pics-check ;;
    pics-scripts) pics-scripts "${2:?a folder is needed}" ;;
    check) check ;;
    check-names) check-names ;;
    check-pictable) check-pictable ;;
    check-numbers) check-numbers ;;
    check-keys) check-keys ;;
    check-style) check-style ;;
    check-strata) check-strata ;;
    check-latexspace) check-latexspace ;;
    check-overfull) check-overfull ;;
    check-tables) check-tables ;;
    check-impl) check-impl ;;
    check-tree) check-tree ;;
    check-math) check-math ;;
    pdf) pdf ;;
    all) single && cards ;;
    clean) clean ;;
    *) help; exit 2 ;;
esac
