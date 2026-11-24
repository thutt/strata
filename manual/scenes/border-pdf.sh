#!/bin/sh
# The picture print-border-page: the top of the page of the PDF that the
# Printer makes of the example document border.doc (made by mkpicdocs.py),
# a table whose cells have a double border.  Usage:
# border-pdf.sh PRINTER DOC OUT.png
# DOC is the name border.doc; the document is made here.
set -e
HERE=$(dirname "$0")
T=$(mktemp -d)
python3 -I -B "${HERE}/mkpicdocs.py" "$T"
"$1" "$T/$2" -o "$T/d.pdf" --no-banner >/dev/null 2>&1
pdftoppm -f 1 -l 1 -r 110 -x 60 -y 60 -W 560 -H 230 -png -singlefile \
    "$T/d.pdf" "$T/d"
cp "$T/d.png" "$3"
rm -rf "$T"
