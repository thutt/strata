#!/bin/bash
# Runs the dviview tests under Xvfb and prints one ok or FAIL line each.
# Run it from the directory that holds the Makefile (make test does that).
# The variables DVIEW_PROG (the program), DVIEW_SHOT (the screenshot tool)
# and DVIEW_RESULTS (where the screenshots and differences are written) are
# set by the Makefile; nothing is written into the source tree.

root="${PWD}"
viewer="${DVIEW_PROG:?DVIEW_PROG is not set: use make test}"
shot="${DVIEW_SHOT:?DVIEW_SHOT is not set: use make test}"
tool="${root}/tests/imgtool.py"
out="${DVIEW_RESULTS:?DVIEW_RESULTS is not set: use make test}/tests"
mkdir -p "${out}"
work="$(mktemp -d "${TMPDIR:-/tmp}/dviview-test.XXXXXX")"
# The log socket is named relative to this folder: the path of a socket may
# not be longer than about 100 characters and a work folder can be deeper.
cd "${work}" || exit 1
fails=0
xpid=""
vpid=""
spid=""
win=""

function find_display {
    local n=90

    while [ -e "/tmp/.X11-unix/X${n}" ] || [ -e "/tmp/.X${n}-lock" ]; do
        n=$((n + 1))
    done

    echo "${n}"
}

function start_xvfb {
    local geometry="${1}"
    local n

    n="$(find_display)"
    Xvfb ":${n}" -screen 0 "${geometry}" >"${work}/xvfb.log" 2>&1 &
    xpid=${!}
    export DISPLAY=":${n}"
    sleep 1
    xdotool mousemove 5 5
}

function stop_xvfb {
    if [ -n "${xpid}" ]; then
        kill "${xpid}" 2>/dev/null
        wait "${xpid}" 2>/dev/null
        xpid=""
    fi
}

function cleanup {
    stop_viewer
    stop_xvfb
    rm -rf "${work}"
}

trap cleanup EXIT

function result {
    local name="${1}"
    local status="${2}"

    if [ "${status}" -eq 0 ]; then
        echo "ok   ${name}"
    else
        echo "FAIL ${name}"
        fails=$((fails + 1))
    fi
}

function title {
    xdotool getwindowname "${win}" 2>/dev/null
}

# Waits up to five seconds for the title to match an extended regexp.
function wait_title {
    local pattern="${1}"
    local i

    for i in $(seq 1 50); do
        if title | grep -Eq -- "${pattern}"; then
            return 0
        fi

        sleep 0.1
    done

    return 1
}

# Starts the stand-in for the collector of str: what the viewer logs goes to
# ${1}.err in the work directory, and the name of the socket is in sock.
sock=""

function start_log {
    local name="${1}"
    local i

    if [ -n "${spid}" ]; then
        kill "${spid}" 2>/dev/null
        wait "${spid}" 2>/dev/null
        spid=""
    fi

    sock="${name}.sock"
    python3 -I -B "${root}/tests/logsink.py" "${sock}" "${work}/${name}.err" &
    spid=${!}

    for i in $(seq 1 50); do
        if [ -S "${sock}" ]; then
            break
        fi

        sleep 0.1
    done
}

# Starts the viewer: the log name, the DVI file, then more arguments.
function start_viewer {
    local log="${1}"
    local file="${2}"
    local i

    shift 2

    # What the viewer logs goes to the log of the run, which here is a
    # socket read by logsink.py into ${log}.err (the viewer writes nothing
    # to the console).
    start_log "${log}"
    STRATA_LOG_SOCKET="${sock}" "${viewer}" "${file}" "${@}" &
    vpid=${!}
    win=""

    for i in $(seq 1 100); do
        win="$(xdotool search --name '\.dvi - page' 2>/dev/null | head -1)"

        if [ -n "${win}" ]; then
            break
        fi

        sleep 0.1
    done

    sleep 0.3
    [ -n "${win}" ]
}

function stop_viewer {
    if [ -n "${vpid}" ]; then
        kill "${vpid}" 2>/dev/null
        wait "${vpid}" 2>/dev/null
        vpid=""
    fi

    if [ -n "${spid}" ]; then
        kill "${spid}" 2>/dev/null
        wait "${spid}" 2>/dev/null
        spid=""
    fi
}

# Sends keys and gives the viewer time to draw.
function keys {
    xdotool key --clearmodifiers --delay 30 "${@}"
    sleep 0.25
}

function snap {
    local name="${1}"
    local w="${2:-}"
    local h="${3:-}"

    "${shot}" "${work}/snap.ppm"
    python3 -I "${tool}" png "${work}/snap.ppm" "${out}/${name}.png" ${w} ${h}
}

# Scrolls to the top left corner of the page.
function top_left {
    xdotool key --clearmodifiers --delay 5 --repeat 90 Up
    xdotool key --clearmodifiers --delay 5 --repeat 90 Left
    sleep 0.3
}

function ink_ok {
    local f="${1}"
    local v

    v="$(python3 -I "${tool}" ink "${f}")"
    [ "$(echo "${v} > 0.0005" | bc -l)" -eq 1 ]
}

function pages_of {
    case "${1}" in
    pages30) echo 30 ;;
    *) echo 1 ;;
    esac
}

# Test A: each document opens and shows at four zoom levels.
function test_documents {
    local t
    local n
    local st

    for t in text math table pages30 psnfss color dense lmodern; do
        n="$(pages_of "${t}")"
        st=0
        start_viewer "doc_${t}" "${out}/${t}.dvi" || st=1
        wait_title "page 1/${n} - 100%" || st=1
        snap "${t}_z100"
        keys KP_Add KP_Add KP_Add KP_Add
        wait_title "- 200%" || st=1
        top_left
        snap "${t}_z200"
        keys KP_Add KP_Add KP_Add KP_Add KP_Add KP_Add KP_Add KP_Add
        wait_title "- 400%" || st=1
        top_left
        snap "${t}_z400"
        keys KP_Multiply
        wait_title "- 100%" || st=1
        keys KP_Subtract KP_Subtract
        wait_title "- 50%" || st=1
        snap "${t}_z50"
        for z in 100 200 400 50; do
            ink_ok "${out}/${t}_z${z}.png" || st=1
        done

        if grep -qE "not found|cannot|no TFM" "${work}/doc_${t}.err"; then
            st=1
        fi

        result "document ${t}: opens, zoom 100 200 400 50, title" ${st}
        stop_viewer
    done
}

# Test B: every zoom key and the zoom limits.
function test_zoom {
    local st=0

    start_viewer zoom "${out}/text.dvi" || st=1
    keys KP_Add
    wait_title "- 125%" || st=1
    keys KP_Subtract
    wait_title "- 100%" || st=1
    result "zoom: KP_Add and KP_Subtract step by 25 percent" ${st}

    st=0
    keys plus
    wait_title "- 125%" || st=1
    keys minus
    wait_title "- 100%" || st=1
    keys KP_Add KP_Add
    keys KP_Multiply
    wait_title "- 100%" || st=1
    result "zoom: main keyboard plus and minus, KP_Multiply resets" ${st}

    st=0
    keys KP_Add KP_Add
    keys 0
    wait_title "- 100%" || st=1
    result "zoom: the digit 0 resets to 100 percent" ${st}

    st=0
    xdotool key --clearmodifiers --delay 20 --repeat 12 KP_Subtract
    sleep 0.5
    wait_title "- 25%" || st=1
    snap "text_z25"
    xdotool key --clearmodifiers --delay 20 --repeat 40 KP_Add
    sleep 1
    wait_title "- 800%" || st=1
    snap "text_z800"
    result "zoom: limits are 25 and 800 percent" ${st}
    stop_viewer
}

# Test C: page keys on the 30 page document.
function test_pages {
    local st=0
    local f="${out}/pages30.dvi"
    local k

    start_viewer pagekeys "${f}" || st=1
    for spec in "Next:2" "space:3" "n:4" "Prior:3" "p:2" "End:30" \
                "Prior:29" "Home:1"; do
        k="${spec%%:*}"
        keys "${k}"
        wait_title "page ${spec##*:}/30" || st=1
        snap "pages30_key_${k}"
    done

    result "page keys: Next space n Prior p End Home" ${st}
    stop_viewer

    st=0
    start_viewer startpage "${f}" -p 7 || st=1
    wait_title "page 7/30" || st=1
    result "option -p 7 starts on page 7" ${st}
    stop_viewer
}

# Test D: scrolling with the arrow keys.
function test_scroll {
    local st=0

    start_viewer scroll "${out}/text.dvi" || st=1
    keys KP_Add KP_Add KP_Add KP_Add
    wait_title "- 200%" || st=1
    top_left
    snap scroll_0
    cp "${out}/scroll_0.png" "${work}/s0.png"
    keys Down Down Down
    snap scroll_down
    python3 -I "${tool}" same "${work}/s0.png" "${out}/scroll_down.png" && st=1
    keys Up Up Up
    snap scroll_up
    python3 -I "${tool}" same "${work}/s0.png" "${out}/scroll_up.png" || st=1
    result "scroll: Down moves the page, Up returns" ${st}

    st=0
    keys Right Right Right
    snap scroll_right
    python3 -I "${tool}" same "${work}/s0.png" "${out}/scroll_right.png" && st=1
    keys Left Left Left
    snap scroll_left
    python3 -I "${tool}" same "${work}/s0.png" "${out}/scroll_left.png" || st=1
    result "scroll: Right moves the page, Left returns" ${st}

    # The wheel scrolls like the arrow keys: three steps down give the
    # picture of three Down keys, three steps up return to the start.
    st=0
    xdotool mousemove --window "${win}" 60 60
    xdotool click --repeat 3 --delay 60 5
    sleep 0.3
    snap wheel_down
    python3 -I "${tool}" same "${work}/s0.png" "${out}/wheel_down.png" && st=1
    python3 -I "${tool}" same "${out}/scroll_down.png" "${out}/wheel_down.png" || st=1
    xdotool click --repeat 3 --delay 60 4
    sleep 0.3
    snap wheel_up
    python3 -I "${tool}" same "${work}/s0.png" "${out}/wheel_up.png" || st=1
    result "scroll: wheel down moves the page like Down, wheel up returns" ${st}
    stop_viewer
}

# Test E: quit keys.
function test_quit {
    local k
    local st

    for k in q Escape; do
        st=0
        start_viewer "quit_${k}" "${out}/text.dvi" || st=1
        keys "${k}"
        sleep 0.5

        if kill -0 "${vpid}" 2>/dev/null; then
            st=1
        fi

        result "quit: ${k} ends the program" ${st}
        stop_viewer
    done
}

# Test F: specials.
function test_specials {
    local st=0
    local err="${work}/specials.err"

    start_viewer specials "${out}/specials.dvi" || st=1
    sleep 0.3
    [ "$(grep -c "special 'foo'" "${err}")" -eq 1 ] || st=1
    [ "$(grep -c "special 'bar'" "${err}")" -eq 1 ] || st=1
    grep -q "special 'ps'" "${err}" && st=1
    grep -q papersize "${err}" && st=1
    cat "${err}" >"${out}/specials_stderr.txt"
    result "specials: each unknown kind reported once, ps: is run" ${st}
    stop_viewer
}

# Test G: a font that has metrics but no shapes is drawn as boxes.
function test_missing {
    local st=0
    local i
    local err="${work}/missing.err"

    export TFMFONTS="${out}/fakefont:"
    start_viewer missing "${out}/missing.dvi" || st=1
    wait_title "page 1/1" || st=1
    snap missing_z100
    snap missing_crop 850 400

    # The Viewer makes the font first, if it can; the note comes when it could
    # not (there is no source of it), so it is waited for.
    for i in $(seq 1 100); do
        grep -q "zzfake" "${err}" && break
        sleep 0.1
    done

    grep -q "zzfake" "${err}" || st=1
    cp "${err}" "${out}/missing_stderr.txt"
    ink_ok "${out}/missing_z100.png" || st=1
    unset TFMFONTS
    result "missing font: named on stderr, drawn as boxes" ${st}
    stop_viewer
}

# Test H: the PK fallback.
function test_pk {
    local st=0

    DVIVIEW_NOPFB=1 start_viewer pk "${out}/text.dvi" || st=1
    wait_title "page 1/1" || st=1
    sleep 1
    snap text_pk_z100
    python3 -I "${tool}" diff "${out}/text_pk_z100.png" \
        "${out}/text_z100.png" "${out}/text_pk_vs_type1_diff.png" \
        >"${out}/text_pk_vs_type1_diff.txt"
    keys KP_Add KP_Add KP_Add KP_Add
    wait_title "- 200%" || st=1
    top_left
    snap text_pk_z200
    ink_ok "${out}/text_pk_z100.png" || st=1
    ink_ok "${out}/text_pk_z200.png" || st=1
    grep -q "font cmr10 not found" "${work}/pk.err" && st=1
    cp "${work}/pk.err" "${out}/pk_stderr.txt"
    result "PK fallback draws text at 100 and 200 percent" ${st}
    stop_viewer
}

# The Viewer makes the bitmap of a font that has no outline (the EC fonts of
# TeX Live come as METAFONT sources): with the map of outlines hidden and an
# empty cache the first look is boxes and the title says that the font is
# made; when the work is done the letters are drawn (soft edges) and TeX has
# its file in the cache.  A fresh HOME is the cache of this test.
function wait_no_title {
    local pattern="${1}"
    local i

    for i in $(seq 1 300); do
        if ! title | grep -Eq -- "${pattern}"; then
            return 0
        fi

        sleep 0.1
    done

    return 1
}

function mkfont_env {
    mkdir -p "${work}/nomap" "${work}/mkhome"
    : >"${work}/nomap/pdftex.map"
    mkdoc mkfont dvips "Hello, Computer Modern Roman: the quick brown fox.\\par
{\\Large Larger text jumps over the lazy dog.}" "\\usepackage[T1]{fontenc}"
}

function test_makefont {
    local st=0
    local soft0
    local soft1

    mkfont_env || st=1
    dvitype "${out}/mkfont.dvi" | grep -q "^Font [0-9]*: ecrm" || st=1
    rm -rf "${work}/mkhome"
    mkdir -p "${work}/mkhome"
    HOME="${work}/mkhome" TEXFONTMAPS="${work}/nomap" \
        start_viewer mkfont "${out}/mkfont.dvi" || st=1
    wait_title "page 1/1" || st=1
    wait_title "Making the font ecrm" || st=1
    snap mkfont_early
    soft0="$(python3 -I "${tool}" soft "${out}/mkfont_early.png")"
    [ "${soft0}" -eq 0 ] || st=1
    wait_no_title "Making the font" || st=1
    sleep 0.5
    snap mkfont_late
    soft1="$(python3 -I "${tool}" soft "${out}/mkfont_late.png")"
    echo "     soft edges: ${soft0} while making, ${soft1} when done"
    [ "${soft1}" -gt 500 ] || st=1
    find "${work}/mkhome" -name 'ecrm*pk' | grep -q . || st=1
    grep -q "not found" "${work}/mkfont.err" && st=1
    result "a font with no outline is made as a bitmap: boxes, then letters" \
        ${st}
    stop_viewer
}

# With no program that makes fonts the boxes stay and the note says what to
# install.  A debug build reads the folders of the programs from
# STRATA_PROGRAM_DIRS (a release build does not: the test is skipped).
function test_makefont_none {
    local st=0
    local soft1

    if [ "${DVIEW_TYPE:-debug}" = "release" ]; then
        echo "ok   no program that makes fonts: boxes and a note (skipped:" \
            "a release build ignores STRATA_PROGRAM_DIRS)"
        return
    fi

    mkfont_env || st=1
    rm -rf "${work}/mkhome" "${work}/noprog"
    mkdir -p "${work}/mkhome" "${work}/noprog"
    ln -s "$(command -v kpsewhich)" "${work}/noprog/kpsewhich"
    HOME="${work}/mkhome" TEXFONTMAPS="${work}/nomap" \
        STRATA_PROGRAM_DIRS="${work}/noprog" \
        start_viewer mkfontnone "${out}/mkfont.dvi" || st=1
    wait_title "page 1/1" || st=1
    sleep 1
    snap mkfont_none
    soft1="$(python3 -I "${tool}" soft "${out}/mkfont_none.png")"
    [ "${soft1}" -eq 0 ] || st=1
    title | grep -q "Making the font" && st=1
    grep -q "Install the TeX package that makes bitmap fonts" \
        "${work}/mkfontnone.err" || st=1
    find "${work}/mkhome" -name 'ecrm*pk' | grep -q . && st=1
    result "no program that makes fonts: boxes and a note" ${st}
    stop_viewer
}

# Test I: speed.
function test_speed {
    local st=0
    local ms
    local items
    local t0
    local t1

    DVIVIEW_TIMING=1 start_viewer speed "${out}/dense.dvi" || st=1
    wait_title "page 1/1" || st=1
    sleep 0.5
    ms="$(grep 'ms$' "${work}/speed.err" | head -1 |
          sed 's/.* drawn in //; s/ ms//')"
    items="$(grep 'ms$' "${work}/speed.err" | head -1 |
             sed 's/.* \([0-9]*\) items.*/\1/')"
    echo "     dense page: ${items} items, first draw ${ms} ms"
    [ "${items:-0}" -ge 4500 ] || st=1
    [ "$(echo "${ms:-99999} < 500" | bc -l)" -eq 1 ] || st=1
    grep 'ms$' "${work}/speed.err" >"${out}/speed_stderr.txt"
    result "speed: a page of 5000 characters draws in under 0.5 s" ${st}
    stop_viewer

    st=0
    t0="$(date +%s.%N)"
    start_viewer long "${out}/long.dvi" || st=1
    wait_title "page 1/1500" || st=1
    t1="$(date +%s.%N)"
    t1="$(echo "${t1} - ${t0}" | bc -l | cut -c1-5)"
    echo "     long.dvi (1500 pages): window up after ${t1} s"
    keys End
    wait_title "page 1500/1500" || st=1
    keys Home
    wait_title "page 1/1500" || st=1
    result "speed: 1500 page file opens at once, End and Home work" ${st}
    stop_viewer
}

# Test J: comparison with dvipng at 100 dpi on a tall screen.
function test_dvipng {
    local t
    local st
    local r

    # Shorten the lines that tell about the differences.
    {
        echo 's/.*dx=\(.*\) dy=\(.*\)/shift dvipng by \1,\2/'
        echo 's/ *differ by more than  64.255:/diff>64:/'
        echo 's/ of all pixels,/ of all,/'
        echo 's/ of ink pixels/ of ink/'
    } >"${work}/cmp.sed"

    stop_xvfb
    start_xvfb 1280x1200x24
    for t in text math table psnfss color; do
        st=0
        start_viewer "cmp_${t}" "${out}/${t}.dvi" || st=1
        wait_title "- 100%" || st=1
        sleep 0.3
        snap "cmp_${t}_dviview" 850 1100
        stop_viewer
        dvipng -q -D 100 -T 8.5in,11in -O 1in,1in \
            -o "${out}/cmp_${t}_dvipng.png" "${out}/${t}.dvi" >/dev/null 2>&1
        if [ ! -f "${out}/cmp_${t}_dvipng.png" ]; then
            mv "${out}/cmp_${t}_dvipng1.png" \
                "${out}/cmp_${t}_dvipng.png"
        fi
        r="$(python3 -I "${tool}" diff "${out}/cmp_${t}_dviview.png" \
             "${out}/cmp_${t}_dvipng.png" "${out}/cmp_${t}_diff.png")"
        echo "${r}" >"${out}/cmp_${t}_diff.txt"
        echo "${r}" | grep -E "64/255|shift of" | sed -f "${work}/cmp.sed" |
            sed "s/^/     ${t}: /"
        [ -f "${out}/cmp_${t}_diff.png" ] || st=1
        result "dvipng comparison ${t} (see cmp_${t}_diff.txt)" ${st}
    done
}


# ---------------------------------------------------------------------------
# Graphics tests.  The pictures are made with ghostscript (and pdftoppm for
# the progressive JPEG); the documents with latex using the graphicx option
# dvips or dvipdfmx.  Reference pictures come from dvipdfmx and pdftoppm, or
# from dvips and ghostscript, and the viewer must agree with them.
# ---------------------------------------------------------------------------

# A picture of 200 x 120 bp: red left half, green top right, blue bottom
# right.  The same drawing is made in every file format.
picps="1 0 0 setrgbcolor 0 0 100 120 rectfill 0 0 1 setrgbcolor"
picps="${picps} 100 0 100 60 rectfill 0 1 0 setrgbcolor 100 60 100 60"
picps="${picps} rectfill showpage"
twops="1 0 0 setrgbcolor 0 0 200 120 rectfill showpage"
twops="${twops} 0 0 1 setrgbcolor 0 0 200 120 rectfill"
twops="${twops} 1 1 0 setrgbcolor 50 30 100 60 rectfill showpage"
alphaps="0.6 setgray 0 0 100 120 rectfill 1 0 0 setrgbcolor"
alphaps="${alphaps} 10 10 80 100 rectfill showpage"

function make_images {
    local g="gs -q -dNOPAUSE -dBATCH -dDEVICEWIDTHPOINTS=200"
    local d

    g="${g} -dDEVICEHEIGHTPOINTS=120"
    ${g} -sDEVICE=png16m -r72 -sOutputFile="${out}/pic.png" -c "${picps}"
    ${g} -sDEVICE=jpeg -dJPEGQ=95 -r72 -sOutputFile="${out}/pic.jpg" \
        -c "${picps}"
    ${g} -sDEVICE=pdfwrite -sOutputFile="${out}/pic.pdf" -c "${picps}"
    ${g} -sDEVICE=eps2write -sOutputFile="${out}/pic.eps" -c "${picps}"
    ${g} -sDEVICE=pdfwrite -sOutputFile="${out}/two.pdf" -c "${twops}"
    ${g} -sDEVICE=png256 -r72 -sOutputFile="${out}/pic_pal.png" -c "${picps}"
    ${g} -sDEVICE=png48 -r72 -sOutputFile="${out}/pic_48.png" -c "${picps}"
    ${g} -sDEVICE=png16m -r144 -sOutputFile="${out}/pic_144.png" \
        -c "${picps}"
    pdftoppm -r 72 -jpeg -jpegopt progressive=y,quality=95 -singlefile \
        "${out}/pic.pdf" "${out}/pic_prog"
    pdftoppm -r 72 -jpeg -jpegopt quality=95 -gray -singlefile \
        "${out}/pic.pdf" "${out}/pic_gray"
    ${g} -sDEVICE=pngalpha -r72 -sOutputFile="${out}/pic_alpha.png" \
        -c "${alphaps}"
    cp "${root}/tests/data/keycap.inc" "${out}/"
    for d in pic.png pic.jpg pic.pdf pic.eps; do
        [ -f "${out}/${d}" ] || echo "FAIL images: ${d} was not made"
    done
}

# Makes a document: name, driver, body.  The driver is dvips or dvipdfmx.
function mkdoc {
    local name="${1}"
    local drv="${2}"
    local body="${3}"
    local pre="${4:-}"

    cat >"${out}/${name}.tex" <<EOF
\documentclass{article}
\usepackage[${drv}]{graphicx}
\usepackage[${drv}]{xcolor}
\def\pgfsysdriver{pgfsys-${drv}.def}
\usepackage{tikz,lscape,adjustbox}
\input{keycap.inc}
${pre}
\pagestyle{empty}
\begin{document}
${body}
\end{document}
EOF
    (cd "${out}" && latex -interaction=nonstopmode "${name}.tex" \
        >"${name}.latex.out" 2>&1)
    [ -f "${out}/${name}.dvi" ]
}

# A picture of a format with options; dvips needs the box of PNG, JPEG, PDF.
function picdoc {
    local name="${1}"
    local drv="${2}"
    local file="${3}"
    local opts="${4}"

    case "${drv}:${file}" in
    dvips:*.eps) ;;
    dvips:*) opts="bb=0 0 200 120,${opts}" ;;
    esac

    mkdoc "${name}" "${drv}" "\\noindent\\includegraphics[${opts}]{${file}}"
}

# Reference picture of a document at 100 dpi, from the real tools.
function make_ref {
    local name="${1}"
    local drv="${2}"
    local page="${3:-1}"

    if [ "${drv}" = "dvipdfmx" ]; then
        (cd "${out}" && dvipdfmx -q -o "${name}.pdf" "${name}.dvi" \
            >/dev/null 2>&1 &&
         pdftoppm -r 100 -f "${page}" -l "${page}" -png -singlefile \
            "${name}.pdf" "${name}_ref")
    else
        (cd "${out}" && dvips -q -o "${name}.ps" "${name}.dvi" \
            >/dev/null 2>&1 &&
         gs -q -dNOPAUSE -dBATCH -sDEVICE=png16m -r100 \
            -dTextAlphaBits=4 -dGraphicsAlphaBits=4 -dFirstPage="${page}" \
            -dLastPage="${page}" -sOutputFile="${name}_ref.png" "${name}.ps")
    fi

    [ -f "${out}/${name}_ref.png" ]
}

# Shows a document and keeps the picture of the page (850 x 900 corner).
function show_doc {
    local name="${1}"
    local page="${2:-1}"

    start_viewer "g_${name}" "${out}/${name}.dvi" -p "${page}" -d 100 ||
        return 1
    wait_title "page ${page}/" || return 1
    sleep 0.3
    snap "${name}" 850 900
}

function px { python3 -I "${tool}" "${@}"; }

# True when the colour "r g b" is within tol of "R G B".
function near {
    local a=(${1})
    local b=(${2})
    local i
    local d

    for i in 0 1 2; do
        d=$((a[i] - b[i]))
        [ "${d#-}" -le "${3}" ] || return 1
    done
}

# True when the number is within tol of the wanted number.
function within {
    local d=$((${1} - ${2}))

    [ "${d#-}" -le "${3}" ]
}

# The colour at a fraction of the ink box of a picture.
function frac {
    px frac "${out}/${1}.png" "${2}" "${3}" 0 0 850 900
}

function ink_box {
    px bbox "${out}/${1}.png" 0 0 850 900
}

# Agreement with the reference picture; the share of pixels that differ.
function ref_ok {
    local name="${1}"
    local limit="${2:-0.05}"
    local bad
    local ink
    local share

    read -r bad ink share <<<"$(px compare "${out}/${name}_ref.png" \
        "${out}/${name}.png" 1)"
    echo "     ${name}: ${bad} of ${ink} ink pixels differ (${share})"
    [ "$(echo "${share} < ${limit}" | bc -l)" -eq 1 ]
}

# Test K: every picture format, with both drivers.
function test_pictures {
    local fmt
    local drv
    local st
    local box
    local x0
    local y0
    local x1
    local y1
    local f

    for fmt in png jpg pdf eps; do
        for drv in dvipdfmx dvips; do
            st=0
            f="pic.${fmt}"
            picdoc "pic_${fmt}_${drv}" "${drv}" "${f}" "width=6cm" || st=1
            show_doc "pic_${fmt}_${drv}" || st=1
            box="$(ink_box "pic_${fmt}_${drv}")"
            read -r x0 y0 x1 y1 <<<"${box}"
            within $((x1 - x0 + 1)) 236 4 || st=1
            within $((y1 - y0 + 1)) 142 4 || st=1
            near "$(frac "pic_${fmt}_${drv}" 0.25 0.5)" "255 0 0" 40 || st=1
            near "$(frac "pic_${fmt}_${drv}" 0.75 0.25)" "0 255 0" 40 || st=1
            near "$(frac "pic_${fmt}_${drv}" 0.75 0.75)" "0 0 255" 40 || st=1
            if [ "${drv}" = "dvipdfmx" ] || [ "${fmt}" = "eps" ]; then
                make_ref "pic_${fmt}_${drv}" "${drv}" && \
                    ref_ok "pic_${fmt}_${drv}" || st=1
            fi

            result "picture ${f} by ${drv}: 6cm, three colours, reference" \
                ${st}
            stop_viewer
        done
    done
}

# Test L: width, height, scale, angle, trim and clip, bounding boxes.
function test_picture_options {
    local st
    local box
    local x0
    local y0
    local x1
    local y1
    local n
    local drv

    for drv in dvipdfmx dvips; do
        st=0
        picdoc "opt_scale_${drv}" "${drv}" pic.pdf "scale=0.5" || st=1
        show_doc "opt_scale_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "opt_scale_${drv}")"
        within $((x1 - x0 + 1)) 139 3 || st=1
        within $((y1 - y0 + 1)) 83 3 || st=1
        result "option scale=0.5 (${drv}): 139 x 83 pixels" ${st}
        stop_viewer

        st=0
        picdoc "opt_height_${drv}" "${drv}" pic.png "height=2in" || st=1
        show_doc "opt_height_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "opt_height_${drv}")"
        within $((y1 - y0 + 1)) 200 3 || st=1
        within $((x1 - x0 + 1)) 333 4 || st=1
        result "option height=2in (${drv}): 333 x 200 pixels" ${st}
        stop_viewer

        st=0
        picdoc "opt_wh_${drv}" "${drv}" pic.jpg "width=3cm,height=3cm" || st=1
        show_doc "opt_wh_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "opt_wh_${drv}")"
        within $((x1 - x0 + 1)) 118 3 || st=1
        within $((y1 - y0 + 1)) 118 3 || st=1
        near "$(frac "opt_wh_${drv}" 0.25 0.5)" "255 0 0" 40 || st=1
        result "options width and height together (${drv}): 118 x 118" ${st}
        stop_viewer

        st=0
        picdoc "opt_a90_${drv}" "${drv}" pic.png "width=6cm,angle=90" || st=1
        show_doc "opt_a90_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "opt_a90_${drv}")"
        within $((x1 - x0 + 1)) 142 4 || st=1
        within $((y1 - y0 + 1)) 236 4 || st=1
        near "$(frac "opt_a90_${drv}" 0.5 0.75)" "255 0 0" 40 || st=1
        near "$(frac "opt_a90_${drv}" 0.25 0.25)" "0 255 0" 40 || st=1
        near "$(frac "opt_a90_${drv}" 0.75 0.25)" "0 0 255" 40 || st=1
        [ "${drv}" = "dvipdfmx" ] && { make_ref "opt_a90_${drv}" "${drv}" &&
            ref_ok "opt_a90_${drv}" || st=1; }
        result "option angle=90 (${drv}): 142 x 236, colours turned" ${st}
        stop_viewer

        st=0
        picdoc "opt_a30_${drv}" "${drv}" pic.png "width=6cm,angle=30" || st=1
        show_doc "opt_a30_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "opt_a30_${drv}")"
        within $((x1 - x0 + 1)) 275 6 || st=1
        within $((y1 - y0 + 1)) 241 6 || st=1
        [ "${drv}" = "dvipdfmx" ] && { make_ref "opt_a30_${drv}" "${drv}" &&
            ref_ok "opt_a30_${drv}" || st=1; }
        result "option angle=30 (${drv}): 275 x 241, matches reference" ${st}
        stop_viewer
    done

    st=0
    picdoc "opt_trim_dvipdfmx" dvipdfmx pic.pdf \
        "width=5cm,trim=0 0 100 60,clip" || st=1
    show_doc "opt_trim_dvipdfmx" || st=1
    read -r x0 y0 x1 y1 <<<"$(ink_box "opt_trim_dvipdfmx")"
    within $((x1 - x0 + 1)) 197 3 || st=1
    within $((y1 - y0 + 1)) 118 3 || st=1
    n="$(px count "${out}/opt_trim_dvipdfmx.png" 0 255 0 60 0 0 850 900)"
    [ "${n}" -eq 0 ] || st=1
    n="$(px count "${out}/opt_trim_dvipdfmx.png" 255 0 0 60 0 0 850 900)"
    [ "${n}" -gt 20000 ] || st=1
    make_ref opt_trim_dvipdfmx dvipdfmx && ref_ok opt_trim_dvipdfmx || st=1
    result "trim and clip of a PDF (dvipdfmx): only the red corner, 197 x 118" \
        ${st}
    stop_viewer

    st=0
    picdoc "opt_view_dvips" dvips pic.eps \
        "width=5cm,viewport=100 60 200 120,clip" || st=1
    show_doc "opt_view_dvips" || st=1
    read -r x0 y0 x1 y1 <<<"$(ink_box "opt_view_dvips")"
    within $((x1 - x0 + 1)) 197 3 || st=1
    within $((y1 - y0 + 1)) 118 3 || st=1
    n="$(px count "${out}/opt_view_dvips.png" 255 0 0 60 0 0 850 900)"
    [ "${n}" -eq 0 ] || st=1
    n="$(px count "${out}/opt_view_dvips.png" 0 255 0 60 0 0 850 900)"
    [ "${n}" -gt 20000 ] || st=1
    make_ref opt_view_dvips dvips && ref_ok opt_view_dvips || st=1
    result "viewport and clip of an EPS (dvips): only the green corner" ${st}
    stop_viewer
}

# Test M: other file variants: progressive and gray JPEG, palette, 16 bit,
# resolution 144 dpi (the size halves) and transparent PNG.
function test_variants {
    local n
    local st
    local x0
    local y0
    local x1
    local y1
    local f

    for f in pic_prog.jpg pic_pal.png pic_48.png; do
        st=0
        picdoc "var_${f%.*}" dvipdfmx "${f}" "width=6cm" || st=1
        show_doc "var_${f%.*}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "var_${f%.*}")"
        within $((x1 - x0 + 1)) 236 4 || st=1
        near "$(frac "var_${f%.*}" 0.25 0.5)" "255 0 0" 40 || st=1
        near "$(frac "var_${f%.*}" 0.75 0.25)" "0 255 0" 40 || st=1
        near "$(frac "var_${f%.*}" 0.75 0.75)" "0 0 255" 40 || st=1
        make_ref "var_${f%.*}" dvipdfmx && ref_ok "var_${f%.*}" || st=1
        result "variant ${f}: colours and size agree with the reference" ${st}
        stop_viewer
    done

    st=0
    picdoc var_gray dvipdfmx pic_gray.jpg "width=6cm" || st=1
    show_doc var_gray || st=1
    near "$(frac var_gray 0.25 0.5)" "$(frac var_gray 0.25 0.6)" 3 || st=1
    near "$(frac var_gray 0.25 0.5)" "$(frac var_gray 0.75 0.25)" 3 && st=1
    make_ref var_gray dvipdfmx && ref_ok var_gray || st=1
    result "variant gray JPEG: three different grays, reference" ${st}
    stop_viewer

    st=0
    picdoc var_dpi dvipdfmx pic_144.png "" || st=1
    show_doc var_dpi || st=1
    read -r x0 y0 x1 y1 <<<"$(ink_box var_dpi)"
    within $((x1 - x0 + 1)) 278 3 || st=1
    within $((y1 - y0 + 1)) 167 3 || st=1
    result "PNG of 144 dpi without options: 278 x 167 pixels (size from pHYs)" \
        ${st}
    stop_viewer

    st=0
    body="\\noindent\\colorbox{cyan}"
    body="${body}{\\includegraphics[width=6cm]{pic_alpha.png}}"
    mkdoc var_alpha dvipdfmx "${body}" || st=1
    show_doc var_alpha || st=1
    near "$(frac var_alpha 0.25 0.5)" "255 0 0" 40 || st=1
    near "$(frac var_alpha 0.75 0.5)" "0 255 255" 8 || st=1
    near "$(frac var_alpha 0.02 0.5)" "153 153 153" 8 || st=1
    n="$(px count "${out}/var_alpha.png" 0 255 255 20 0 0 850 900)"
    [ "${n}" -gt 1000 ] || st=1
    make_ref var_alpha dvipdfmx && ref_ok var_alpha || st=1
    result "transparent PNG: the cyan box shows through, red inside" ${st}
    stop_viewer
}

# Test N: a picture file that is not there; the viewer must not fail.
function test_missing_picture {
    local st=0
    local x0
    local y0
    local x1
    local y1
    local n

    body="\\noindent\\special{pdf:image bbox 0 0 100 50 clip 0"
    body="${body} width 100bp (nofile.png) }"
    body="${body}\\hbox to 100bp{}\\vrule height 50bp width 0pt"
    mkdoc missing_pic dvipdfmx "${body}" || st=1
    start_viewer missing_pic "${out}/missing_pic.dvi" -d 100 || st=1
    wait_title "page 1/1" || st=1
    sleep 0.5
    snap missing_pic 850 900
    kill -0 "${vpid}" 2>/dev/null || st=1
    grep -q "nofile.png" "${work}/missing_pic.err" || st=1
    cp "${work}/missing_pic.err" "${out}/missing_pic_stderr.txt"
    read -r x0 y0 x1 y1 <<<"$(ink_box missing_pic)"
    within $((x1 - x0 + 1)) 139 4 || st=1
    within $((y1 - y0 + 1)) 69 4 || st=1
    n="$(px count "${out}/missing_pic.png" 192 0 0 40 0 0 850 900)"
    [ "${n}" -gt 200 ] || st=1
    result "missing PNG (dvipdfmx): named on stderr, marked box 139 x 69" \
        ${st}
    stop_viewer

    st=0
    body="\\noindent\\special{PSfile=\"nofile.eps\" llx=0 lly=0"
    body="${body} urx=100 ury=50 rwi=1000}"
    body="${body}\\hbox to 100bp{}\\vrule height 50bp width 0pt"
    mkdoc missing_ps dvips "${body}" || st=1
    start_viewer missing_ps "${out}/missing_ps.dvi" -d 100 || st=1
    wait_title "page 1/1" || st=1
    sleep 0.5
    snap missing_ps 850 900
    kill -0 "${vpid}" 2>/dev/null || st=1
    grep -q "nofile.eps" "${work}/missing_ps.err" || st=1
    n="$(px count "${out}/missing_ps.png" 192 0 0 40 0 0 850 900)"
    [ "${n}" -gt 200 ] || st=1
    result "missing EPS (dvips special): named on stderr, marked box" ${st}
    stop_viewer
}

# Test O: pictures at other zooms are drawn again from the source.
function test_picture_zoom {
    local st
    local f
    local x0
    local y0
    local x1
    local y1

    for f in pdf eps png; do
        st=0
        picdoc "zoom_${f}" dvipdfmx "pic.${f}" "width=6cm" || st=1
        start_viewer "zoom_${f}" "${out}/zoom_${f}.dvi" -d 100 || st=1
        wait_title "page 1/1 - 100%" || st=1
        keys KP_Add KP_Add KP_Add KP_Add
        wait_title "- 200%" || st=1
        sleep 1
        top_left
        sleep 1
        snap "zoom_${f}_200" 850 900
        read -r x0 y0 x1 y1 <<<"$(px bbox "${out}/zoom_${f}_200.png" \
            0 0 850 900)"
        within $((x1 - x0 + 1)) 472 6 || st=1
        near "$(px frac "${out}/zoom_${f}_200.png" 0.25 0.5 0 0 850 900)" \
            "255 0 0" 40 || st=1
        keys KP_Multiply KP_Subtract KP_Subtract
        wait_title "- 50%" || st=1
        sleep 1
        snap "zoom_${f}_50" 850 900
        read -r x0 y0 x1 y1 <<<"$(px bbox "${out}/zoom_${f}_50.png" \
            215 180 635 720)"
        within $((x1 - x0 + 1)) 118 4 || st=1
        near "$(px frac "${out}/zoom_${f}_50.png" 0.75 0.75 215 180 635 720)" \
            "0 0 255" 40 || st=1
        result "picture ${f}: drawn again at 200 and 50 percent (472, 118)" \
            ${st}
        stop_viewer
    done
}

# Test P: rotated and scaled text (pdf:btrans, x:scale; ps: rotate, scale).
function test_text_transforms {
    local drv
    local st
    local rw
    local rh
    local sw
    local sh
    local x0
    local y0
    local x1
    local y1

    for drv in dvipdfmx dvips; do
        st=0
        mkdoc "tx_plain_${drv}" "${drv}" "\\Huge Hello" || st=1
        show_doc "tx_plain_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "tx_plain_${drv}")"
        rw=$((x1 - x0 + 1))
        rh=$((y1 - y0 + 1))
        stop_viewer

        mkdoc "tx_rot_${drv}" "${drv}" "\\Huge\\rotatebox{90}{Hello}" || st=1
        show_doc "tx_rot_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "tx_rot_${drv}")"
        within $((x1 - x0 + 1)) "${rh}" 3 || st=1
        within $((y1 - y0 + 1)) "${rw}" 3 || st=1
        make_ref "tx_rot_${drv}" "${drv}" && ref_ok "tx_rot_${drv}" || st=1
        result "rotatebox 90 (${drv}): ${rw} x ${rh} becomes ${rh} x ${rw}" \
            ${st}
        stop_viewer

        st=0
        mkdoc "tx_scale_${drv}" "${drv}" "\\Huge\\scalebox{2}[1]{Hello}" || st=1
        show_doc "tx_scale_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "tx_scale_${drv}")"
        sw=$((x1 - x0 + 1))
        sh=$((y1 - y0 + 1))
        within "${sw}" $((rw * 2)) 8 || st=1
        within "${sh}" "${rh}" 3 || st=1
        make_ref "tx_scale_${drv}" "${drv}" && ref_ok "tx_scale_${drv}" || st=1
        result "scalebox 2 by 1 (${drv}): width ${sw} is twice ${rw}" \
            ${st}
        stop_viewer

        st=0
        body="\\Huge\\rotatebox{30}{Hello}\\quad"
        body="${body}\\scalebox{1.5}{\\rotatebox{-20}{Hi}}"
        mkdoc "tx_r30_${drv}" "${drv}" "${body}" || st=1
        show_doc "tx_r30_${drv}" || st=1
        make_ref "tx_r30_${drv}" "${drv}" && ref_ok "tx_r30_${drv}" || st=1
        result "rotatebox 30 and scaled rotatebox -20 (${drv}): reference" ${st}
        stop_viewer
    done
}

# Test Q: coloured boxes (rules and colours) and the keycap of the print path.
function test_boxes_keycaps {
    local drv
    local st
    local n
    local x0
    local y0
    local x1
    local y1
    local a

    for drv in dvipdfmx dvips; do
        st=0
        body="\\Huge\\colorbox{yellow}{Box}\\quad"
        body="${body}\\fcolorbox{red}{blue}{\\textcolor{white}{FB}}"
        mkdoc "box_${drv}" "${drv}" "${body}" || st=1
        show_doc "box_${drv}" || st=1
        n="$(px count "${out}/box_${drv}.png" 255 255 0 8 0 0 850 900)"
        [ "${n}" -gt 1500 ] || st=1
        n="$(px count "${out}/box_${drv}.png" 0 0 255 8 0 0 850 900)"
        [ "${n}" -gt 1000 ] || st=1
        n="$(px count "${out}/box_${drv}.png" 255 0 0 8 0 0 850 900)"
        [ "${n}" -gt 100 ] || st=1
        make_ref "box_${drv}" "${drv}" && ref_ok "box_${drv}" || st=1
        result "colorbox and fcolorbox (${drv}): colours, reference" \
            ${st}
        stop_viewer

        st=0
        body="\\Huge Press \\strkey{0}{0.8}{3}{}{Enter}"
        body="${body} then \\strkey{1}{0.2}{2}{}{Esc}"
        mkdoc "key_${drv}" "${drv}" "${body}" || st=1
        show_doc "key_${drv}" || st=1
        n="$(px count "${out}/key_${drv}.png" 204 204 204 2 0 0 850 900)"
        [ "${n}" -gt 9000 ] || st=1
        n="$(px count "${out}/key_${drv}.png" 51 51 51 2 0 0 850 900)"
        [ "${n}" -gt 2500 ] || st=1
        read -r x0 y0 x1 y1 <<<"$(px bbox "${out}/key_${drv}.png" \
            280 100 600 320)"
        near "$(px px "${out}/key_${drv}.png" $((x0 + 1)) $((y0 + 1)))" \
            "255 255 255" 4 || st=1
        a=$(( (x1 - x0 + 1) * (y1 - y0 + 1) ))
        n="$(px count "${out}/key_${drv}.png" 204 204 204 2 \
            "${x0}" "${y0}" $((x1 + 1)) $((y1 + 1)))"
        [ $((n * 100 / a)) -ge 60 ] || st=1
        make_ref "key_${drv}" "${drv}" && ref_ok "key_${drv}" || st=1
        result "keycaps of the print path (${drv}): fills, corner, reference" \
            ${st}
        stop_viewer
    done
}

# Test R: pgf paths: fills, strokes, dashes, rounded corners, clipping.
function test_pgf_paths {
    local drv
    local st
    local n
    local x0
    local y0
    local x1
    local y1

    for drv in dvipdfmx dvips; do
        st=0
        body="\\tikz\\draw[fill=green!50!black,line width=2pt,dashed]"
        body="${body} (0,0) circle (1);\\quad"
        body="${body} \\tikz\\draw[blue,ultra thick,->,rounded corners]"
        body="${body} (0,0) -- (2,1) -- (3,0);\\quad"
        body="${body} \\tikz\\draw[fill=orange,draw=red,very thick]"
        body="${body} (0,0) rectangle (2,1);"
        mkdoc "pgf_${drv}" "${drv}" "${body}" || st=1
        show_doc "pgf_${drv}" || st=1
        n="$(px count "${out}/pgf_${drv}.png" 0 128 0 12 0 0 850 900)"
        [ "${n}" -gt 4000 ] || st=1
        n="$(px count "${out}/pgf_${drv}.png" 0 0 255 12 0 0 850 900)"
        [ "${n}" -gt 100 ] || st=1
        n="$(px count "${out}/pgf_${drv}.png" 255 128 0 12 0 0 850 900)"
        [ "${n}" -gt 2500 ] || st=1
        make_ref "pgf_${drv}" "${drv}" && ref_ok "pgf_${drv}" || st=1
        result "tikz circle, dashes, arrow, filled box (${drv}): reference" \
            ${st}
        stop_viewer

        st=0
        body="\\tikz{\\clip (0,0) rectangle (1,1);"
        body="${body}\\fill[red] (-1,-1) rectangle (2,2);}"
        mkdoc "clip_${drv}" "${drv}" "${body}" || st=1
        show_doc "clip_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "clip_${drv}")"
        within $((x1 - x0 + 1)) 39 2 || st=1
        within $((y1 - y0 + 1)) 39 2 || st=1
        make_ref "clip_${drv}" "${drv}" && ref_ok "clip_${drv}" || st=1
        result "tikz clip (${drv}): a fill of 3 cm is cut to 1 cm (39 pixels)" \
            ${st}
        stop_viewer
    done
}

# Test S: raw operators: pdf:literal (both modes) and the dvips quote special.
function test_literal {
    local st=0
    local n
    local x0
    local y0
    local x1
    local y1

    body="\\hbox to 1pt{}\\special{pdf:literal direct q 0 0 1 rg"
    body="${body} 100 -200 100 50 re f Q}\\vspace*{2in}"
    body="${body}\\hbox{\\hskip 1in\\special{pdf:literal q 1 0 0 rg"
    body="${body} 0 0 60 30 re f Q}\\special{pdf:code q 0 0.6 0 rg"
    body="${body} 80 0 30 30 re f Q}x}"
    mkdoc lit_dvipdfmx dvipdfmx "${body}" || st=1
    show_doc lit_dvipdfmx || st=1
    n="$(px count "${out}/lit_dvipdfmx.png" 0 0 255 6 0 0 850 900)"
    [ "${n}" -gt 6000 ] || st=1
    n="$(px count "${out}/lit_dvipdfmx.png" 255 0 0 6 0 0 850 900)"
    [ "${n}" -gt 2000 ] || st=1
    make_ref lit_dvipdfmx dvipdfmx && ref_ok lit_dvipdfmx || st=1
    result "pdf:literal direct and page mode, pdf:code: as the reference" \
        ${st}
    stop_viewer

    st=0
    body="\\hbox to 1pt{}\\vspace*{2in}\\hbox{\\hskip 1in"
    body="${body}\\special{\" 0 0 1 setrgbcolor 0 0 80 40 rectfill"
    body="${body} 1 0 0 setrgbcolor 0 0 moveto 100 50 lineto"
    body="${body} 3 setlinewidth stroke}x}"
    mkdoc lit_dvips dvips "${body}" || st=1
    show_doc lit_dvips || st=1
    n="$(px count "${out}/lit_dvips.png" 0 0 255 6 0 0 850 900)"
    [ "${n}" -gt 4000 ] || st=1
    make_ref lit_dvips dvips && ref_ok lit_dvips || st=1
    result "dvips quote special (origin at the point): as the reference" \
        ${st}
    stop_viewer
}

# Test T: landscape pages: pdf Rotate 90 and the dvips landscape special.
function geometry_of {
    xdotool getwindowgeometry "${win}" | grep Geometry | sed 's/.*: //'
}

function test_landscape {
    local st=0
    local geom
    local x0
    local y0
    local x1
    local y1
    local rx0
    local ry0
    local rx1
    local ry1

    body="Portrait page one.\\clearpage"
    body="${body}\\special{pdf: put @thispage <</Rotate 90>>}"
    body="${body}\\begin{landscape}Landscape page two, wide text."
    body="${body}\\par\\includegraphics[width=6cm]{pic.png}"
    body="${body}\\end{landscape}"
    mkdoc land_dvipdfmx dvipdfmx "${body}" || st=1
    start_viewer land1 "${out}/land_dvipdfmx.dvi" -p 1 -d 100 || st=1
    wait_title "page 1/2" || st=1
    geom="$(geometry_of)"
    echo "     page 1 window ${geom}"
    [ "${geom}" = "850x900" ] || st=1
    keys Next
    wait_title "page 2/2" || st=1
    sleep 0.5
    snap land_dvipdfmx_next
    kill -0 "${vpid}" 2>/dev/null || st=1
    stop_viewer
    result "landscape (pdf Rotate 90): page 1 portrait, Next to page 2" \
        ${st}

    st=0
    show_doc land_dvipdfmx 2 || st=1
    geom="$(geometry_of)"
    echo "     page 2 window ${geom}"
    [ "${geom}" = "1100x850" ] || st=1
    snap land_dvipdfmx 1100 850
    make_ref land_dvipdfmx dvipdfmx 2 || st=1
    px size "${out}/land_dvipdfmx_ref.png" | grep -q "^1100 850$" || st=1
    read -r x0 y0 x1 y1 <<<"$(px bbox "${out}/land_dvipdfmx.png" 0 0 1100 850)"
    read -r rx0 ry0 rx1 ry1 <<<"$(px bbox "${out}/land_dvipdfmx_ref.png")"
    within "${x0}" "${rx0}" 2 || st=1
    within "${y0}" "${ry0}" 2 || st=1
    within "${x1}" "${rx1}" 2 || st=1
    within "${y1}" "${ry1}" 2 || st=1
    ref_ok land_dvipdfmx || st=1
    result "landscape page 2 (pdf Rotate 90): 1100 x 850, as reference" \
        ${st}
    stop_viewer

    st=0
    body="\\special{papersize=8.5in,11in}\\special{landscape}"
    body="${body}Landscape (dvips).\\includegraphics[width=4cm]{pic.eps}"
    mkdoc land_dvips dvips "${body}" || st=1
    start_viewer land_dvips "${out}/land_dvips.dvi" -d 100 || st=1
    wait_title "page 1/1" || st=1
    geom="$(geometry_of)"
    echo "     dvips landscape window ${geom}"
    [ "${geom}" = "1100x850" ] || st=1
    snap land_dvips 1100 850
    ink_ok "${out}/land_dvips.png" || st=1
    result "dvips landscape special: window 1100 x 850 (swapped)" \
        ${st}
    stop_viewer
}

# Test X: the other macros of the print path that draw: strpic (adjustbox
# and includegraphics), strsmile (tikz circle and curve) and strlong.
function test_print_macros {
    local drv
    local st
    local n
    local x0
    local y0
    local x1
    local y1

    for drv in dvipdfmx dvips; do
        st=0
        body="\\strpic{width=12cm}{pic.png}"

        if [ "${drv}" = "dvips" ]; then
            body="\\strpic{bb=0 0 200 120,width=12cm}{pic.eps}"
        fi

        mkdoc "macro_pic_${drv}" "${drv}" "${body}" || st=1
        show_doc "macro_pic_${drv}" || st=1
        read -r x0 y0 x1 y1 <<<"$(ink_box "macro_pic_${drv}")"
        within $((x1 - x0 + 1)) 472 5 || st=1
        near "$(frac "macro_pic_${drv}" 0.25 0.5)" "255 0 0" 40 || st=1
        near "$(frac "macro_pic_${drv}" 0.75 0.25)" "0 255 0" 40 || st=1
        make_ref "macro_pic_${drv}" "${drv}" && ref_ok "macro_pic_${drv}" ||
            st=1
        result "strpic of the print path (${drv}): 12cm wide, colours" ${st}
        stop_viewer

        st=0
        body="\\Huge \\strsmile{white}{black} \\strsmile{black}{white}"
        body="${body} \\strlong{some long text}"
        mkdoc "macro_misc_${drv}" "${drv}" "${body}" || st=1
        show_doc "macro_misc_${drv}" || st=1
        n="$(px count "${out}/macro_misc_${drv}.png" 0 0 0 6 0 0 850 900)"
        [ "${n}" -gt 300 ] || st=1
        n="$(px count "${out}/macro_misc_${drv}.png" 242 242 242 4 0 0 850 900)"
        [ "${n}" -gt 500 ] || st=1
        make_ref "macro_misc_${drv}" "${drv}" && ref_ok "macro_misc_${drv}" ||
            st=1
        result "strsmile and strlong of the print path (${drv}): reference" \
            ${st}
        stop_viewer
    done
}

# Test Y: more specials of dvipdfmx: the page background, a page of a PDF
# file, pdf:epdf, pdf:btrans matrix, and adjustbox scaling a picture.
function test_pdf_specials {
    local st=0

    body="\\special{pdf:bgcolor [0.9 0.9 0.6]}"
    body="${body}\\noindent\\includegraphics[page=2,width=4cm]{two.pdf}"
    body="${body}\\special{pdf:epdf page 1 width 3cm (two.pdf) }"
    body="${body}\\hbox{\\special{pdf:btrans matrix 1 0 0.5 1 0 0}Sheared"
    body="${body}\\special{pdf:etrans}}"
    body="${body}\\adjustbox{max width=3cm}"
    body="${body}{\\includegraphics[width=8cm]{pic.png}}"
    mkdoc spec_pdf dvipdfmx "${body}" || st=1
    show_doc spec_pdf || st=1
    near "$(px px "${out}/spec_pdf.png" 5 5)" "230 230 153" 2 || st=1
    near "$(px px "${out}/spec_pdf.png" 200 190)" "0 0 255" 2 || st=1
    near "$(px px "${out}/spec_pdf.png" 260 210)" "255 255 0" 2 || st=1
    near "$(px px "${out}/spec_pdf.png" 360 230)" "255 0 0" 2 || st=1
    make_ref spec_pdf dvipdfmx && ref_ok spec_pdf 0.01 || st=1
    result "bgcolor, PDF page 2, epdf, btrans matrix, adjustbox: reference" \
        ${st}
    stop_viewer
}

# Test U: a 4000 page document with pictures on page 3 opens at once; only
# the page that is shown is drawn.
function test_big_document {
    local st=0
    local t0
    local t1
    local ms
    local n

    cat >"${out}/big.tex" <<'EOF'
\documentclass{article}
\usepackage[dvipdfmx]{graphicx}
\pagestyle{empty}
\begin{document}
\newcount\n
\loop
\advance\n by 1
Page \the\n.
\ifnum\n=3
\includegraphics[width=6cm]{pic.pdf}
\includegraphics[width=6cm]{pic.eps}
\includegraphics[width=6cm]{pic.jpg}\fi
\clearpage
\ifnum\n<4000 \repeat
\end{document}
EOF
    (cd "${out}" && latex -interaction=nonstopmode big.tex >big.latex.out 2>&1)
    [ -f "${out}/big.dvi" ] || st=1

    t0="$(date +%s.%N)"
    DVIVIEW_TIMING=1 start_viewer big "${out}/big.dvi" || st=1
    wait_title "page 1/4000" || st=1
    t1="$(date +%s.%N)"
    t1="$(echo "${t1} - ${t0}" | bc -l | cut -c1-5)"
    echo "     big.dvi (4000 pages): window up after ${t1} s"
    [ "$(echo "${t1} < 1.5" | bc -l)" -eq 1 ] || st=1
    keys Next Next
    wait_title "page 3/4000" || st=1
    sleep 1.5
    snap big_p3 850 900
    ms="$(grep 'page 3 has' "${work}/big.err" | head -1 |
          sed 's/.* drawn in //; s/ ms//')"
    echo "     page 3 (PDF, EPS and JPEG pictures): first draw ${ms} ms"
    [ "$(echo "${ms:-99999} < 3000" | bc -l)" -eq 1 ] || st=1
    n="$(px count "${out}/big_p3.png" 255 0 0 40 0 0 850 900)"
    [ "${n}" -gt 20000 ] || st=1
    keys Prior
    keys Next
    wait_title "page 3/4000" || st=1
    sleep 0.5
    ms="$(grep 'page 3 has' "${work}/big.err" | tail -1 |
          sed 's/.* drawn in //; s/ ms//')"
    echo "     page 3 drawn again (pictures cached): ${ms} ms"
    [ "$(echo "${ms:-99999} < 500" | bc -l)" -eq 1 ] || st=1
    keys End
    wait_title "page 4000/4000" || st=1
    cp "${work}/big.err" "${out}/big_stderr.txt"
    result "4000 pages: opens at once, pictures drawn when shown" \
        ${st}
    stop_viewer
}

# Test V: the temporary pictures of PDF and EPS files are removed.
function test_cleanup {
    local st=0
    local before
    local during
    local after
    local k

    for k in TERM q; do
        st=0
        before="$(ls -d "${TMPDIR:-/tmp}"/dviview-?????? 2>/dev/null | sort)"
        start_viewer clean "${out}/pic_pdf_dvipdfmx.dvi" -d 100 || st=1
        sleep 1
        during="$(ls -d "${TMPDIR:-/tmp}"/dviview-?????? 2>/dev/null | sort)"
        [ "${during}" != "${before}" ] || st=1

        if [ "${k}" = "TERM" ]; then
            kill -TERM "${vpid}"
            wait "${vpid}" 2>/dev/null
        else
            keys q
            sleep 0.5
        fi

        vpid=""
        after="$(ls -d "${TMPDIR:-/tmp}"/dviview-?????? 2>/dev/null | sort)"
        [ "${after}" = "${before}" ] || st=1
        result "temporary pictures: directory made and removed (${k})" \
            ${st}
    done
}

# Test W: what is not drawn is reported once and the viewer goes on.
function test_unsupported {
    local st=0
    local drv

    for drv in dvipdfmx dvips; do
        st=0
        body="\\usetikzlibrary{shadings}"
        body="${body}\\tikz\\shade[left color=red,right color=blue]"
        body="${body} (0,0) rectangle (3,1);"
        body="${body}\\tikz\\fill[opacity=0.5,red] (0,0) rectangle (2,1);"
        mkdoc "shade_${drv}" "${drv}" "${body}" || st=1
        show_doc "shade_${drv}" || st=1
        kill -0 "${vpid}" 2>/dev/null || st=1
        cp "${work}/g_shade_${drv}.err" "${out}/shade_${drv}_stderr.txt"
        [ -s "${out}/shade_${drv}_stderr.txt" ] || st=1
        [ "$(sort "${out}/shade_${drv}_stderr.txt" | uniq -d |
             wc -l)" -eq 0 ] || st=1
        result "shading and opacity (${drv}): reported once, viewer goes on" \
            ${st}
        stop_viewer
    done
}

# Test B: the status bar below the page, with the page, the page count, the
# keys, and the go to page prompt.  The viewer logs every drawn page.
function test_bar {
    local st=0
    local err="${work}/bar.err"
    local geom
    local h
    local i
    local body="Page one.\\clearpage Page two.\\clearpage Page three."

    body="${body}\\clearpage Page four.\\clearpage Page five."
    mkdoc bar5 dvipdfmx "${body}" || st=1
    start_log bar
    env -u DVIVIEW_PLAIN DVIVIEW_TIMING=1 STRATA_LOG_SOCKET="${sock}" \
        "${viewer}" "${out}/bar5.dvi" -d 50 &
    vpid=${!}
    win=""

    for i in $(seq 1 100); do
        win="$(xdotool search --name 'bar5\.dvi' 2>/dev/null | head -1)"
        [ -n "${win}" ] && break
        sleep 0.1
    done

    sleep 1
    [ -n "${win}" ] || st=1
    xdotool mousemove 5 5
    title | grep -q "page" && st=1
    geom="$(geometry_of)"
    h="${geom#*x}"
    echo "     window ${geom}"
    [ "${h}" -gt 580 ] && [ "${h}" -lt 640 ] || st=1
    result "bar: the title is the file name, the window has room for the bar" \
        ${st}

    st=0
    keys g 3 Return
    grep -q "page 3 has" "${err}" || st=1
    result "bar: g, 3, Enter goes to page 3" ${st}

    st=0
    keys g 9 9 Return
    [ "$(grep -c "page 5 has" "${err}")" -ge 1 ] || st=1
    result "bar: a page number past the end goes to the last page" ${st}

    st=0
    keys g 2 Escape
    kill -0 "${vpid}" 2>/dev/null || st=1
    keys g 1 BackSpace 2 Return
    grep -q "page 2 has" "${err}" || st=1
    result "bar: Esc cancels the prompt, Backspace edits the number" ${st}

    st=0
    keys F1
    keys question
    kill -0 "${vpid}" 2>/dev/null || st=1
    snap bar_page2
    result "bar: F1 and ? hide and show the key legend" ${st}

    stop_viewer
}

# Test L: the red status area of a failed print (the special of the DVI).
# Test: an empty special (LaTeX packages write them) is not an unsupported
# special: nothing is said on the console.
# Line numbers: the package lineno puts a number in the margin of every line,
# in a small sans serif font.  The viewer draws the numbers like the rest of
# the text, so the window shows the same ink as the reference made by
# dvipdfmx, and there is ink in the left margin where the numbers stand.
function test_lineno {
    local st=0
    local body
    local box

    body="\\linenumbers Line one of the numbered text.\\par "
    body="${body}Line two of the numbered text.\\par "
    body="${body}Line three of the numbered text.\\par "
    body="${body}Line four of the numbered text."
    mkdoc lineno dvipdfmx "${body}" "\\usepackage[running]{lineno}" || st=1
    show_doc lineno || st=1
    make_ref lineno dvipdfmx 1 || st=1
    ref_ok lineno || st=1
    box="$(px bbox "${out}/lineno.png" 100 90 178 400)"
    echo "     ink of the left margin: ${box}"
    [ "${box}" != "none" ] || st=1
    box="$(px bbox "${out}/lineno.png" 700 90 850 400)"
    [ "${box}" = "none" ] || st=1
    result "line numbers (lineno) are drawn in the margin, as the reference" \
        ${st}
    stop_viewer
}


function test_empty_special {
    local st=0

    mkdoc emptyspecial dvipdfmx "\\special{}Page one." || st=1
    show_doc emptyspecial || st=1
    kill -0 "${vpid}" 2>/dev/null || st=1
    cp "${work}/g_emptyspecial.err" "${out}/emptyspecial_stderr.txt"
    [ ! -s "${out}/emptyspecial_stderr.txt" ] || st=1
    result "an empty special says nothing on the console" ${st}
    stop_viewer
}


function test_failed {
    local st=0
    local i
    local n
    local geom
    local h_failed
    local h_clean
    local body="Page one.\\clearpage Page two."
    local err="${work}/failed.err"

    mkdoc failed dvipdfmx "\\special{strata:print-failed 3}${body}" || st=1
    mkdoc clean2 dvipdfmx "${body}" || st=1
    result "failed: the DVI files are made" ${st}

    st=0
    start_log failed
    env -u DVIVIEW_PLAIN STRATA_LOG_SOCKET="${sock}" "${viewer}" \
        "${out}/failed.dvi" -d 50 &
    vpid=${!}
    win=""

    for i in $(seq 1 100); do
        win="$(xdotool search --name 'failed\.dvi' 2>/dev/null | head -1)"
        [ -n "${win}" ] && break
        sleep 0.1
    done

    sleep 1
    [ -n "${win}" ] || st=1
    xdotool mousemove 5 5
    snap failed_page1
    h_failed="$(geometry_of)"
    h_failed="${h_failed#*x}"
    n="$(python3 -I "${tool}" count "${out}/failed_page1.png" 192 0 0 8)"
    echo "     red pixels of the failed print: ${n}"
    [ "${n}" -gt 1000 ] || st=1
    result "failed: the status area is red when the DVI says so" ${st}

    st=0
    keys Next
    snap failed_page2
    n="$(python3 -I "${tool}" count "${out}/failed_page2.png" 192 0 0 8)"
    [ "${n}" -gt 1000 ] || st=1
    grep -q "special 'strata'" "${err}" && st=1
    result "failed: the area stays on page 2 and the special is not reported" \
        ${st}
    stop_viewer

    st=0
    start_log clean2
    env -u DVIVIEW_PLAIN STRATA_LOG_SOCKET="${sock}" "${viewer}" \
        "${out}/clean2.dvi" -d 50 &
    vpid=${!}
    win=""

    for i in $(seq 1 100); do
        win="$(xdotool search --name 'clean2\.dvi' 2>/dev/null | head -1)"
        [ -n "${win}" ] && break
        sleep 0.1
    done

    sleep 1
    [ -n "${win}" ] || st=1
    xdotool mousemove 5 5
    snap clean_page1
    geom="$(geometry_of)"
    h_clean="${geom#*x}"
    n="$(python3 -I "${tool}" count "${out}/clean_page1.png" 192 0 0 8)"
    [ "${n}" -eq 0 ] || st=1
    [ "${h_failed}" -gt "${h_clean}" ] || st=1
    result "clean: no red area, and the window is lower than the failed one" \
        ${st}
    stop_viewer
}

if [ ! -x "${viewer}" ] || [ ! -x "${shot}" ]; then
    echo "FAIL build: run make first"
    exit 1
fi

# The input documents are made from the TeX files of tests/data by latex, in
# the directory of the results; the font without shapes comes with them.
function make_inputs {
    local f
    local n

    mkdir -p "${out}/fakefont"
    cp "${root}/tests/data/fakefont/zzfake.tfm" "${out}/fakefont/"

    for f in "${root}"/tests/data/*.tex; do
        n="$(basename "${f}" .tex)"
        cp "${f}" "${out}/${n}.tex"
        (cd "${out}" && TFMFONTS="${out}/fakefont:" latex \
            -interaction=nonstopmode "${n}.tex" > "${n}.latex.out" 2>&1)
        if [ ! -f "${out}/${n}.dvi" ]; then
            echo "FAIL input ${n}: latex did not make ${n}.dvi"
            fails=$((fails + 1))
        fi
    done
}

# The tests of the pictures read the page from the title and compare whole
# windows, so they run without the status bar; test_bar runs with it.
export DVIVIEW_PLAIN=1

make_inputs
make_images
start_xvfb 1280x900x24
# TESTS="test_pictures test_landscape" runs only those; the default is all.
all_tests="test_documents test_zoom test_pages test_scroll test_quit \
    test_specials test_missing test_pk test_makefont test_makefont_none \
    test_speed test_pictures \
    test_picture_options test_variants test_missing_picture \
    test_picture_zoom test_text_transforms test_boxes_keycaps test_pgf_paths \
    test_literal test_landscape test_print_macros test_pdf_specials \
    test_big_document test_cleanup \
    test_unsupported test_dvipng test_bar test_failed test_empty_special \
    test_lineno"

for t in ${TESTS:-${all_tests}}; do
    "${t}"
done

if [ "${fails}" -eq 0 ]; then
    echo "all tests passed"
else
    echo "${fails} test(s) failed"
fi

exit "${fails}"
