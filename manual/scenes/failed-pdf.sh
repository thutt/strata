#!/bin/sh
# The picture print-failed-page: one page of the PDF that the Printer makes
# of the test document, with the banner and the marks.  Usage:
# failed-pdf.sh PRINTER DOC OUT.png [PAGE]
set -e
# The scratch folder is made under the directory for temporary files of the
# run and is removed when the script ends, however it ends.
T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
# The document is copied to the scratch folder, and the Printer runs in it
# through a private view of the file system (made by bwrap: nothing is
# changed on the machine) that shows the folder as /home/writer/documents, so
# that the folders named in the Error Log do not depend on where the tree is.
mkdir "${T}/tmp"
cp "${2}" "${T}/d.doc"
(bwrap --ro-bind / / --dev /dev --proc /proc --tmpfs /home \
    --bind "${T}" /home/writer/documents --chdir /home/writer/documents \
    --bind "${T}/tmp" /home/writer/tmp --setenv TMPDIR /home/writer/tmp \
    "${1}" d.doc -o e.pdf --docdir /home/writer/documents \
    --workingdir /home/writer/documents 2>/dev/null) || [ $? -eq 4 ]
P=${4:-2}
pdftoppm -f "${P}" -l "${P}" -r 100 -x 0 -y 0 -W 850 -H 330 -png \
    -singlefile "${T}/e.pdf" "${T}/e"
cp "${T}/e.png" "${3}"
