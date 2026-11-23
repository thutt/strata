#!/bin/sh
# The picture appchar-print: the first page of the PDF that the Printer makes
# of the character table chars.doc (made by manual/mkchars.py), cut to the
# table.  Usage: appchar-pdf.sh PRINTER DOC OUT.png
# The page is taken at 90 dots to the inch, as the first picture was.  The
# Printer warns of the characters that no font has: the warnings are the
# subject of the picture, so the exit status of the warnings is accepted.
set -e
T=$(mktemp -d)
"$1" "$2" -o "$T/c.pdf" --contents 0 --docdir "$(dirname "$2")" 2>/dev/null || [ $? -eq 4 ]
pdftoppm -f 1 -l 1 -r 90 -x 0 -y 60 -W 760 -H 360 -png -singlefile \
    "$T/c.pdf" "$T/c"
cp "$T/c.png" "$3"
rm -rf "$T"
