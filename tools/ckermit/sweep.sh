#!/bin/bash
# sweep.sh [model] : Watcom-preprocessed $W/pp/*.i -> fixups -> minic -> qbe
# -> asm_to_omf -> nasm, one line per module (PASS or the first failing stage).
# Env: ONLY="<.i files>" restricts the set; MINICFLAGS (e.g. -G); OUTSUFFIX.
set -u
[ $# -ge 1 ] && export MODEL="$1"
. "$(dirname "$0")/common.sh"
mkdir -p "$OUT"
[ -d "$W/pp" ] || { echo "no $W/pp -- run $TOOLS/pp.sh first" >&2; exit 1; }
NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'
for pp in ${ONLY:-"$W"/pp/*.i}; do
  b=$(basename "$pp" .i); err="$OUT/$b.err"; : > "$err"
  # drop pragma lines (+ backslash continuations), Watcom calling-convention noise
  awk 'BEGIN{c=0} { if (c) { c = ($0 ~ /\\$/); next } if ($0 ~ /^[ \t]*#/) { c = ($0 ~ /\\$/); next } print }' "$pp" \
   | perl -pe 's/__declspec\([^)]*\)//g; s/\b(__watcall|__near|__cdecl)\b//g' \
   | perl -p "$TOOLS/fixups.pl" | tr -d '\r\032' | perl -pe "$NORMALIZE" > "$OUT/$b.c"
  if ! "$Q/minic/minic" -m "$MODEL" ${MINICFLAGS:-} < "$OUT/$b.c" > "$OUT/$b.ssa" 2>"$err"; then echo "$b MINIC: $(head -c 300 $err | tr '\n' ' ')"; continue; fi
  if ! "$Q/qbe" -t i8086 -m "$MODEL" "$OUT/$b.ssa" > "$OUT/$b.asm" 2>"$err"; then echo "$b QBE: $(head -c 300 $err| tr '\n' ' ')"; continue; fi
  if ! "$Q/tools/asm_to_omf.py" "--model=$MODEL" "$b" "$OUT/$b.asm" "$OUT/$b.omf.asm" 2>"$err"; then echo "$b OMF: $(head -c 300 $err| tr '\n' ' ')"; continue; fi
  if ! nasm -w-label-redef-late -f obj "$OUT/$b.omf.asm" -o "$OUT/$b.obj" 2>"$err"; then echo "$b NASM: $(head -c 300 $err| tr '\n' ' ')"; continue; fi
  echo "$b PASS"
done
