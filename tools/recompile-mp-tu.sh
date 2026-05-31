#!/bin/bash
# recompile-mp-tu.sh — recompile ONE MicroPython TU (compact far-data) and
# relink build/mp-link/mpython.exe (with --gc-sections), reusing every other
# already-built object in build/mp-link/.  This is the fast inner loop for the
# on-target bring-up debugging (a full tools/build-micropython.sh run recompiles
# all 106 TUs; this touches one).
#
# Prereq: a full `bash tools/build-micropython.sh --model=compact --keep-going`
# has populated build/mp-link/ with every TU's .obj, and /tmp/mp_objs.txt lists
# the link object order (crt0_exe.obj + pass objs + libstub_exe.obj).  If that
# file is missing, regenerate it the way the next-session note describes (glob
# the same source list build-micropython.sh uses).
#
# Usage:  tools/recompile-mp-tu.sh <base> <path/to/src.c>
#   e.g.  tools/recompile-mp-tu.sh gc      ~/projects/micropython/py/gc.c
#         tools/recompile-mp-tu.sh runtime ~/projects/micropython/py/runtime.c
#         tools/recompile-mp-tu.sh main    ~/projects/micropython/ports/dos8086/main.c
set -e
base="$1"; src="$2"
QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$QBE_DIR"
MP="$HOME/projects/micropython"; DOSPORT="$MP/ports/dos8086"
GENHDR="$MP/ports/minimal/build"; INC_DIR="minic/include"; STUB="build/mp-spike/stubinc"
MINIC="minic/minic"; QBE="./qbe"; OUT_DIR="build/mp-link"; MODEL=compact
NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'
clang -E -P -nostdinc -DDOS -D__TURBOC__ -DDOS_FAR_DATA=1 -DFAR_DATA=1 \
  "-I$DOSPORT" "-I$STUB" "-I$INC_DIR" "-I$MP" "-I$GENHDR" "$src" 2>"$OUT_DIR/$base.err" > "$OUT_DIR/$base.raw.c"
tr -d '\r\032' < "$OUT_DIR/$base.raw.c" | sed "$NORMALIZE" > "$OUT_DIR/$base.pp.c"
"$MINIC" -m "$MODEL" < "$OUT_DIR/$base.pp.c" > "$OUT_DIR/$base.ssa" 2>"$OUT_DIR/$base.err" || { echo "MINIC_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
"$QBE" -t i8086 -m "$MODEL" "$OUT_DIR/$base.ssa" > "$OUT_DIR/$base.asm" 2>"$OUT_DIR/$base.err" || { echo "QBE_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
tools/asm_to_omf.py "--model=$MODEL" --far-static-data "$base" "$OUT_DIR/$base.asm" "$OUT_DIR/$base.omf.asm" 2>"$OUT_DIR/$base.err" || { echo "OMF_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
nasm -w-label-redef-late -f obj "$OUT_DIR/$base.omf.asm" -o "$OUT_DIR/$base.obj" 2>"$OUT_DIR/$base.err" || { echo "NASM_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
echo "$base.obj rebuilt"
OBJS=(); while IFS= read -r l; do OBJS+=("$l"); done < /tmp/mp_objs.txt
tools/omf_link.py -o "$OUT_DIR/mpython.exe" --map "$OUT_DIR/mpython.map" --entry _start --stack-size 8192 --gc-sections "${OBJS[@]}" 2>&1 | grep -E "image|stripped"
