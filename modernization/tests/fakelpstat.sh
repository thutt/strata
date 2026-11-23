#!/bin/bash
# A stand-in for lpstat, for the tests of F12, Print and of Setup.
# STRATA_LPSTAT names it.  "lpstat -a" lists the queues in FAKELPSTAT_QUEUES
# (names separated by spaces; "laser" and "office" when the variable is not
# set, none when it is set and empty); the queues named in FAKELPSTAT_OFF do
# not accept jobs.  "lpstat -d" tells the default queue, FAKELPSTAT_DEFAULT
# ("laser" when the variable is not set, none when it is set and empty).

case "${1}" in
-a)
    for q in ${FAKELPSTAT_QUEUES-laser office}; do
        state="accepting"

        for off in ${FAKELPSTAT_OFF-}; do
            if [ "${off}" = "${q}" ]; then
                state="not accepting"
            fi
        done

        echo "${q} ${state} requests since Mon 01 Jan 2026 10:00:00 AM UTC"
    done
    ;;
-d)
    if [ -n "${FAKELPSTAT_DEFAULT-laser}" ]; then
        echo "system default destination: ${FAKELPSTAT_DEFAULT-laser}"
    else
        echo "no system default destination"
    fi
    ;;
*)
    echo "fakelpstat: only -a and -d are known" >&2
    exit 1
    ;;
esac
