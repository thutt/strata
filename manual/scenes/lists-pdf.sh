#!/bin/sh
# The picture print-lists-page: the page of the PDF that the Printer makes
# of the example document indexlist.doc (made by mkpicdocs.py) with the
# settings List of Figures? and List of Tables?, the first page after the
# title page.  Usage: lists-pdf.sh PRINTER DOC OUT.png
# DOC is the name indexlist.doc; the document is made here.
set -e
HERE=$(dirname "$0")
T=$(mktemp -d)
python3 -I -B "${HERE}/mkpicdocs.py" "$T"
# The index is asked for too, so that the Index markers are not printed as
# text with a warning.
"$1" "$T/$2" -o "$T/d.pdf" --no-banner --contents 0 --list-figures --list-tables \
    --index >/dev/null 2>&1
pdftoppm -f 2 -l 2 -r 90 -x 60 -y 60 -W 650 -H 170 -png -singlefile \
    "$T/d.pdf" "$T/d"
cp "$T/d.png" "$3"
rm -rf "$T"
