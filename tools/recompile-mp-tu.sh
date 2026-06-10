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
# MP_STACK_SIZE default MUST match build-micropython.sh (61440 since §4v) or a
# fast-loop relink silently ships a different stack size (and hence a shifted
# far-data layout) than the full build.  The old 16384-vs-24576 load-ceiling
# concern died with §4t's per-function gc-sections (~240KB headroom).
# MP_SPLIT_STACK must also match (default ON since §4v: qbe -s +
# omf_link --separate-stack), as must MP_STACK_LIMIT (stack check ON).
MINIC="minic/minic"; QBE="./qbe"; OUT_DIR="build/mp-link"; MODEL=compact; MP_STACK_SIZE=${MP_STACK_SIZE:-61440}
MP_SPLIT_STACK=${MP_SPLIT_STACK:-1}
QBE_SPLIT_FLAG=""; LINK_SPLIT_FLAG=""
if [ "$MP_SPLIT_STACK" != "0" ]; then QBE_SPLIT_FLAG="-s"; LINK_SPLIT_FLAG="--separate-stack"; fi
MP_STACK_LIMIT=${MP_STACK_LIMIT:-$((MP_STACK_SIZE - 8192))}
MP_HEAP_SIZE=${MP_HEAP_SIZE:-49152}
MP_DOS_TINY_STACK_CHECK=${MP_DOS_TINY_STACK_CHECK:-0}
MP_DOS_STACKLESS_RECURSION_RAISE=${MP_DOS_STACKLESS_RECURSION_RAISE:-0}
MP_EXTRA_CPPFLAGS=${MP_EXTRA_CPPFLAGS:-}
NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'
clang -E -P -nostdinc -DDOS -D__TURBOC__ -DDOS_FAR_DATA=1 -DFAR_DATA=1 \
  $MP_EXTRA_CPPFLAGS \
  "-I$DOSPORT" "-I$STUB" "-I$INC_DIR" "-I$MP" "-I$GENHDR" "$src" 2>"$OUT_DIR/$base.err" > "$OUT_DIR/$base.raw.c"
tr -d '\r\032' < "$OUT_DIR/$base.raw.c" | sed "$NORMALIZE" > "$OUT_DIR/$base.pp.c"
if [ "$base" = "main" ] && [ "$MP_STACK_LIMIT" != "8192" ]; then
  sed "s/mp_stack_set_limit(8192);/mp_stack_set_limit($MP_STACK_LIMIT);/" "$OUT_DIR/$base.pp.c" > "$OUT_DIR/$base.pp.c.tmp"
  mv "$OUT_DIR/$base.pp.c.tmp" "$OUT_DIR/$base.pp.c"
fi
if [ "$base" = "main" ] && [ "$MP_HEAP_SIZE" != "49152" ]; then
  sed "s/static char heap\\[(49152)\\];/static char heap[($MP_HEAP_SIZE)];/" "$OUT_DIR/$base.pp.c" > "$OUT_DIR/$base.pp.c.tmp"
  mv "$OUT_DIR/$base.pp.c.tmp" "$OUT_DIR/$base.pp.c"
fi
if [ "$base" = "cstack" ] && [ "$MP_DOS_TINY_STACK_CHECK" != "0" ]; then
  sed '/^void mp_cstack_check(void) {/,/^}/c\
void mp_cstack_check(void) {\
    volatile int stack_dummy;\
    unsigned int top = (unsigned int)(mp_state_ctx.thread.stack_top);\
    unsigned int cur = (unsigned int)&stack_dummy;\
    if ((unsigned int)(top - cur) >= (unsigned int)(mp_state_ctx.thread.stack_limit)) {\
        mp_raise_recursion_depth();\
    }\
}' "$OUT_DIR/$base.pp.c" > "$OUT_DIR/$base.pp.c.tmp"
  mv "$OUT_DIR/$base.pp.c.tmp" "$OUT_DIR/$base.pp.c"
fi
if [ "$base" = "runtime" ] && [ "$MP_DOS_STACKLESS_RECURSION_RAISE" != "0" ]; then
  cat >> "$OUT_DIR/$base.pp.c" <<'EOF'
__attribute__((noreturn)) void mp_raise_recursion_depth(void) {
    mp_raise_type_arg(&mp_type_RuntimeError, ((mp_obj_t)((((mp_uint_t)(MP_QSTR_maximum_space_recursion_space_depth_space_exceeded)) << 3) | 2)));
}
EOF
fi
"$MINIC" -m "$MODEL" < "$OUT_DIR/$base.pp.c" > "$OUT_DIR/$base.ssa" 2>"$OUT_DIR/$base.err" || { echo "MINIC_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
"$QBE" -t i8086 -m "$MODEL" $QBE_SPLIT_FLAG "$OUT_DIR/$base.ssa" > "$OUT_DIR/$base.asm" 2>"$OUT_DIR/$base.err" || { echo "QBE_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
# Per-function text segments, matching build-micropython.sh (§4t) — a TU
# rebuilt here must split .text identically or the relink silently reverts
# that TU to whole-TU gc-sections granularity.
QBE_TEXT_SEG_BUDGET=${QBE_TEXT_SEG_BUDGET:-1} \
tools/asm_to_omf.py "--model=$MODEL" --far-static-data "$base" "$OUT_DIR/$base.asm" "$OUT_DIR/$base.omf.asm" 2>"$OUT_DIR/$base.err" || { echo "OMF_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
nasm -w-label-redef-late -f obj "$OUT_DIR/$base.omf.asm" -o "$OUT_DIR/$base.obj" 2>"$OUT_DIR/$base.err" || { echo "NASM_FAIL $base"; cat "$OUT_DIR/$base.err"; exit 1; }
echo "$base.obj rebuilt"
OBJS=(); while IFS= read -r l; do OBJS+=("$l"); done < /tmp/mp_objs.txt
tools/omf_link.py -o "$OUT_DIR/mpython.exe" --map "$OUT_DIR/mpython.map" --entry _start --stack-size "$MP_STACK_SIZE" $LINK_SPLIT_FLAG --gc-sections --pack-code "${OBJS[@]}" 2>&1 | grep -E "image|stripped"
