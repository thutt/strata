#!/bin/bash
# Copyright (c) 2026 Logic Magicians Software
#
# Starts the Editor for the picture of the list that New shows.  The list is
# made from the templates of a folder, so this script gives the folder
# templates of the source tree to the neutral launcher, which shows it to the
# Editor as a folder of the private view and so shows no folder of the machine
# that takes the picture.
# Usage: templates-stred.sh [ARGUMENTS OF stred]
#
set -o pipefail;
set -o nounset;
set -o errexit;

here=$(cd "$(dirname "${0}")" && pwd)
NEUTRAL_TEMPLATES=$(cd "${here}/../../templates" && pwd)
export NEUTRAL_TEMPLATES
exec "${here}/neutral-stred.sh" "${@}"
