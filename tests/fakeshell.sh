#!/bin/bash
# Copyright (c) 2026 Logic Magicians Software
#
# A fake shell for the test that the F12 menu starts no shell: it appends
# a line to the file named by ${FAKESHELL_LOG} and ends at once, so that the
# Editor comes back.  The tests set ${SHELL} to it, so that any call that the
# Editor still made to a shell would be recorded.
#
set -o pipefail;
set -o nounset;
set -o errexit;

echo "fake shell ran" >> "${FAKESHELL_LOG}"
