#!/bin/sh
# The picture print-letter-page: the first page of the PDF that the Printer
# makes of the sample letter complaint-to-utility-us.doc (made by
# tests/mkletters.py).  Usage: letter-pdf.sh PRINTER DOC OUT.png
# DOC is the path of the letter from the top of the source tree.
set -e
T=$(mktemp -d)
"$1" "$2" -o "$T/l.pdf" --no-banner >/dev/null 2>&1
pdftoppm -f 1 -l 1 -r 70 -x 0 -y 30 -W 560 -H 700 -png -singlefile \
    "$T/l.pdf" "$T/l"
cp "$T/l.png" "$3"
rm -rf "$T"
