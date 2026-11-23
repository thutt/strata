#!/bin/bash
# Copyright (c) 2026 Logic Magicians Software
#
# A stand-in for a terminal program, for the test of scripts/str-dev: it
# records its arguments and its process number in the directory named by
# FAKETERM_REC and then waits until it is killed.
#
set -o pipefail;
set -o nounset;
set -o errexit;

printf '%s\n' "${@}" > "${FAKETERM_REC}/args"
echo "${$}" > "${FAKETERM_REC}/pid"
exec sleep 300
