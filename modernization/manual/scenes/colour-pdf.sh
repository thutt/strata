#!/bin/sh
# The picture print-colour: the first page of the PDF that the Printer makes
# of colour.doc (made by manual/mkcolour.py), cut to the headline, the drawing
# and its line of text, at 90 dots to the inch.  Usage:
# colour-pdf.sh PRINTER DOC OUT.png
set -e
T=$(mktemp -d)
"$1" "$2" -o "$T/c.pdf" --contents 0 --docdir "$(dirname "$2")" --no-banner 2>/dev/null
pdftoppm -f 1 -l 1 -r 90 -x 70 -y 80 -W 620 -H 270 -png -singlefile \
    "$T/c.pdf" "$T/c"
cp "$T/c.png" "$3"
rm -rf "$T"
