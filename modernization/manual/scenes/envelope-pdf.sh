#!/bin/sh
# The picture print-envelope-sample: the page of the PDF that the Printer
# makes of a sample envelope, the number 10 envelope of samples/envelopes.
# The page is the envelope: wide, with the sender at the top left and the
# recipient below the middle.  Usage: envelope-pdf.sh PRINTER DOC OUT.png
# DOC is the path of the sample, relative to the folder modernization.
set -e
T=$(mktemp -d)
"$1" "$2" -o "$T/e.pdf" --no-banner >/dev/null 2>&1
pdftoppm -f 1 -l 1 -r 72 -png -singlefile "$T/e.pdf" "$T/e"
cp "$T/e.png" "$3"
rm -rf "$T"
