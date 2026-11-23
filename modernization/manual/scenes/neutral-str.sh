#!/bin/sh
# Starts the Document Manager for the pictures of Setup so that no folder of
# the machine that takes the pictures is shown.  A private view of the file
# system (made by bwrap: nothing is changed on the machine) shows the folder
# of the sample documents as /home/writer/documents, and the Editor and the
# help file as /usr/local/strata/stred and /usr/local/strata/strata.hlp,
# which is where a package puts them.  The folder of the settings is shown as
# /home/writer/.strata.  The settings Editor program and Help
# File name those places, so Setup shows them.
# Usage: neutral-str.sh [ARGUMENTS OF str]
# The variables XBIN, STRATA_HOME and STRATA_HELP (or STRATA_BUILD_DIR) are
# those of shots.py and of the scene files.
set -e
DOCS=$(pwd)
HELP=${STRATA_HELP:-${STRATA_BUILD_DIR:-/nonexistent}/strata.hlp}
mkdir -p "${STRATA_HOME}"
chmod 700 "${STRATA_HOME}"
printf 'stred=/usr/local/strata/stred\nhelp=/usr/local/strata/strata.hlp\n' \
    > "${STRATA_HOME}/config"
chmod 600 "${STRATA_HOME}/config"
set -- --ro-bind / / --dev /dev --proc /proc --tmpfs /home --tmpfs /usr/local \
    --bind "${DOCS}" /home/writer/documents --chdir /home/writer/documents \
    --bind "${STRATA_HOME}" /home/writer/.strata \
    --setenv STRATA_HOME /home/writer/.strata \
    --ro-bind "${XBIN}/stred" /usr/local/strata/stred \
    --ro-bind "${HELP}" /usr/local/strata/strata.hlp \
    "${XBIN}/str" "${@}"
exec bwrap "${@}"
