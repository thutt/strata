#!/bin/bash
# Copyright (c) 2026 Logic Magicians Software
#
# A stand-in for str, for the test of scripts/str-dev: it records its
# arguments in the directory named by FAKESTR_REC, records whether the link
# current.log existed when it started, waits until the fake terminal has
# recorded itself (if one is expected), makes a log file and the link as str
# does, and ends with the status FAKESTR_STATUS (default 0).
#
set -o pipefail;
set -o nounset;
set -o errexit;

log_dir="${STRATA_HOME}/log"

printf '%s\n' "${@}" > "${FAKESTR_REC}/args"

if [ -L "${log_dir}/current.log" ]; then
    echo yes > "${FAKESTR_REC}/link_at_start"
else
    echo no > "${FAKESTR_REC}/link_at_start"
fi

if [ -n "${FAKETERM_REC:-}" ] && [ "${FAKESTR_WAIT:-0}" = "1" ]; then
    for i in $(seq 1 50); do
        if [ -f "${FAKETERM_REC}/pid" ]; then
            break
        fi

        sleep 0.1
    done
fi

mkdir -p "${log_dir}"
echo "a line of the log" > "${log_dir}/run-20260101-000000.log"
ln -sf run-20260101-000000.log "${log_dir}/current.log"
exit "${FAKESTR_STATUS:-0}"
