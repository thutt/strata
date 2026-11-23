#!/bin/sh
# Starts the Editor for the pictures with a slow progress report of the
# Printer, so that the box of the preview can be photographed.
D=$(dirname "$0")
export STRATA_STRPRINT="$D/slowprogress.sh"
exec "$XBIN/stred" "$@"
