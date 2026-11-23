#!/bin/sh
# The Printer, with its progress lines passed on slowly, so that the
# box of the preview can be photographed while LaTeX is at work.
"$XBIN/strprint" "$@" | while IFS= read -r line; do
    printf '%s\n' "$line"
    sleep 0.12
done
