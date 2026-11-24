#!/bin/bash
# Copyright (c) 2026 Logic Magicians Software
#
# Starts the Editor for the pictures that show the path of the document (the
# marker Pathname) so that no folder of the machine that takes the pictures is
# shown.  A private view of the file system (made by bwrap: nothing is
# changed on the machine) shows the folder of the sample documents as
# /home/writer/documents.  The arguments that name a file in that folder are
# changed to the new place.
# Usage: neutral-stred.sh [ARGUMENTS OF stred]
# The variable XBIN is the one of shots.py and of the scene files.
#
set -o pipefail;
set -o nounset;
set -o errexit;

DOCS=$(pwd)
NEW=/home/writer/documents
for a in "${@}"; do
    case "${a}" in
        "${DOCS}"/*) a=${NEW}/${a#"${DOCS}"/} ;;
    esac
    set -- "${@}" "${a}"
    shift
done
# The home directory of the person in the private view is a folder that
# can be written, because the images of equations and keys are made by LaTeX,
# which keeps its font files in the home directory.
# The folder for temporary files can be written for the same reason.
# A folder of templates (the variable NEUTRAL_TEMPLATES) is shown as
# /home/writer/templates and named by STRATA_TEMPLATES, so that New can list
# it and no folder of this machine is shown.
templates=()
if [ -n "${NEUTRAL_TEMPLATES:-}" ]; then
    templates=(--dir /home/writer/templates
               --bind "${NEUTRAL_TEMPLATES}" /home/writer/templates
               --setenv STRATA_TEMPLATES /home/writer/templates)
fi
set -- --ro-bind / / --bind /tmp /tmp --dev /dev --proc /proc --tmpfs /home \
    --dir /home/writer --setenv HOME /home/writer "${templates[@]}" \
    --bind "${DOCS}" "${NEW}" --chdir "${NEW}" "${XBIN}/stred" "${@}"
exec bwrap "${@}"
