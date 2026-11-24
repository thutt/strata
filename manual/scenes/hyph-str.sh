#!/bin/sh
# Starts the Document Manager for the pictures of Maintain Dictionaries: the
# settings folder holds a personal word list and a personal hyphenation list
# that the picture shows (the panel shows no folder of the machine).
# Usage: hyph-str.sh [ARGUMENTS OF str]
# The variables XBIN and STRATA_HOME are those of shots.py.
set -e
mkdir -p "${STRATA_HOME}"
chmod 700 "${STRATA_HOME}"
printf 'apple\nharbor\nMacPherson\nquorum\nZimmerman\n' \
    > "${STRATA_HOME}/dictionary"
printf 'al-go-rithm\nArm-strong\n=Jacksonville\nMac-Pher-son\nref-er-ence\nZim-mer-man\n' \
    > "${STRATA_HOME}/hyphenation"
chmod 600 "${STRATA_HOME}/dictionary" "${STRATA_HOME}/hyphenation"
exec "${XBIN}/str" "$@"
