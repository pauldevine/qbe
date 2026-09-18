#!/bin/bash
# m.sh 'line' ['line'...] : does this C snippet parse under minic?  (OK or error)
. "$(dirname "$0")/common.sh"
t=$(mktemp); printf '%s\n' "$@" > "$t"
"$Q/minic/minic" -m "$MODEL" ${MINICFLAGS:-} < "$t" >/dev/null && echo OK; rm -f "$t"
