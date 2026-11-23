#!/bin/sh
# The picture print-headfoot-page: one page of the PDF that the Printer makes
# of the example document headfoot.doc (made by mkpicdocs.py), which has the
# running head "Harbour guide | | Tide tables" and the foot "Page N" on a
# small page.  Usage: hf-pdf.sh PRINTER DOC OUT.png
# DOC is the name headfoot.doc; the document is made here.
set -e
HERE=$(dirname "$0")
T=$(mktemp -d)
python3 -I -B "${HERE}/mkpicdocs.py" "$T"
"$1" "$T/$2" -o "$T/d.pdf" --no-banner --contents 0 >/dev/null 2>&1
pdftoppm -f 3 -l 3 -r 100 -png -singlefile "$T/d.pdf" "$T/d"
cp "$T/d.png" "$3"
rm -rf "$T"
