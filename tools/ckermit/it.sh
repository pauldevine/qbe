#!/bin/bash
# it.sh <module> : re-sweep one module, then show its minic error (if any).
. "$(dirname "$0")/common.sh"
ONLY="$W/pp/$1.i" "$TOOLS/sweep.sh" "$MODEL" >/dev/null; "$TOOLS/one.sh" "$1"
