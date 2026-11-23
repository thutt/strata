#!/bin/sh
# The picture print-index-page: the last page of the PDF that the Printer
# makes of the example document indexlist.doc (made by mkpicdocs.py) with
# the setting Index?.  Usage: index-pdf.sh PRINTER DOC OUT.png
# DOC is the name indexlist.doc; the document is made here.
set -e
HERE=$(dirname "$0")
T=$(mktemp -d)
python3 -I -B "${HERE}/mkpicdocs.py" "$T"
# The lists are asked for too, so that the Figure and Table markers are not
# printed as text with a warning.
"$1" "$T/$2" -o "$T/d.pdf" --no-banner --index --list-figures \
    --list-tables >/dev/null 2>&1
LAST=$(pdfinfo "$T/d.pdf" | awk '/^Pages:/ {print $2}')
pdftoppm -f "${LAST}" -l "${LAST}" -r 90 -x 60 -y 60 -W 480 -H 240 -png \
    -singlefile "$T/d.pdf" "$T/d"
cp "$T/d.png" "$3"
rm -rf "$T"
