#!/bin/bash
# one.sh <module> : run minic on $OUT/<module>.c, show the error with context.
. "$(dirname "$0")/common.sh"
f="$OUT/$1.c"; e=$(mktemp)
"$Q/minic/minic" -m "$MODEL" ${MINICFLAGS:-} < "$f" > /dev/null 2> "$e"; rc=$?
if [ $rc -ne 0 ]; then l=$(grep -oE 'error:[0-9]+' "$e" | head -1 | cut -d: -f2); head -3 "$e"; [ -n "$l" ] && sed -n "$((l>3?l-3:1)),$((l+1))p" "$f" | cut -c1-200; else echo OK; fi
rm -f "$e"
