#!/bin/bash
# build-micropython.sh — first-link bring-up of the MicroPython REPL for
# 8086/8088 DOS, via the minic → qbe(-t i8086) → asm_to_omf → nasm → omf_link
# toolchain.  Modeled on tools/build-stevie.sh + the build/mp-spike harness.
#
# Pipeline per TU:
#   1. C   → preprocessed C   via `clang -E` (conforming cpp) + type-normalize
#   2. C   → SSA              via minic -m medium
#   3. SSA → i8086 asm        via qbe -t i8086 -m medium
#   4. asm → OMF asm          via tools/asm_to_omf.py  (per-TU label prefixing)
#   5. OMF asm → .obj         via nasm -f obj
# Then: crt0_exe.obj + all TU .obj + libstub_exe.obj → omf_link → mpython.exe
#
# This is a SPIKE: the point is to surface the link-layer gap (undefined
# symbols, segment-count limits, setjmp/longjmp).  --keep-going tolerates
# per-TU compile failures and links whatever succeeded.
#
# Under a far-data model (--model=compact|large|huge) the build sets
# QBE_FAR_STATIC_DATA so each TU's statics land in its OWN far segment
# (class FAR_DATA/FAR_BSS) placed outside DGROUP — freeing DGROUP for the
# heap+stack (the §1r runtime-hang fix: medium's single 64KB DGROUP can't
# hold MicroPython's ~55KB of static data PLUS heap+stack).  Far-data
# stdlib/stdio/setjmp routing is handled by minic's far_stdlib[] mangling +
# the FAR_*_EXE helpers in libstub_to_exe.py.
#
# Usage: tools/build-micropython.sh [--keep-going] [--model=<m>]

set -u
KEEP_GOING=0
MODEL=medium
for arg in "$@"; do
	case "$arg" in
		--keep-going) KEEP_GOING=1 ;;
		--model=*) MODEL="${arg#--model=}" ;;
		*) echo "unknown arg '$arg'" >&2; exit 1 ;;
	esac
done

# Far-data models route each module's statics to its own far segment so
# total static data can exceed the single 64KB DGROUP.  DOS_FAR_DATA tells
# the port's inline-asm console HAL (mphalport.c) to read its args under the
# 4-byte-far-pointer ABI.  FAR_DATA tells minic/include/stdint.h to make
# intptr_t/uintptr_t (hence mp_int_t/mp_uint_t/mp_obj_t-tagging) 32-bit so a
# far pointer survives a uintptr_t round-trip — without it gc_setup_area's
# pointer math truncates the segment and gc_alloc's scan loop spins forever.
FARSTATIC_FLAG=""
FARDATA_DEF=""
case "$MODEL" in
	compact|large|huge) FARSTATIC_FLAG="--far-static-data"; FARDATA_DEF="-DDOS_FAR_DATA=1 -DFAR_DATA=1" ;;
esac

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MP="$HOME/projects/micropython"
DOSPORT="$MP/ports/dos8086"
GENHDR="$MP/ports/minimal/build"          # genhdr/qstrdefs.generated.h etc.
INC_DIR="$QBE_DIR/minic/include"
STUB="$QBE_DIR/build/mp-spike/stubinc"
MINIC="$QBE_DIR/minic/minic"
QBE="$QBE_DIR/qbe"
DOS_DIR="$QBE_DIR/minic/dos"
OUT_DIR="$QBE_DIR/build/mp-link"
mkdir -p "$OUT_DIR"
# Default DOS stack = 16384.  The dos8086 port runs the STACKLESS-strict VM
# (ports/dos8086/mpconfigport.h §4b): deep Python recursion chains code_state
# frames on the GC heap instead of the C stack, so the C stack stays shallow.
# A bigger stack would push the compact far-data image over the Victor load
# ceiling (24576 → body 828224 > ~824416 "Program too big"); 16384 → body
# 820096 loads with margin and gives recsum(30) a clean run.  8192 is too small
# — deep GENERATOR recursion still C-recurses (mp_execute_bytecode on resume,
# which STACKLESS does NOT cover) and overflows an 8 KB stack into DGROUP data,
# corrupting it; 16384 degrades that case gracefully instead.  Override with
# MP_STACK_SIZE for a larger-RAM target or a non-stackless build.
MP_STACK_SIZE=${MP_STACK_SIZE:-16384}
MP_STACK_LIMIT=${MP_STACK_LIMIT:-8192}
MP_HEAP_SIZE=${MP_HEAP_SIZE:-49152}
MP_DOS_TINY_STACK_CHECK=${MP_DOS_TINY_STACK_CHECK:-0}
MP_DOS_STACKLESS_RECURSION_RAISE=${MP_DOS_STACKLESS_RECURSION_RAISE:-0}
MP_EXTRA_CPPFLAGS=${MP_EXTRA_CPPFLAGS:-}
# Per-FUNCTION text segments (§4t): with budget=1, asm_to_omf splits .text at
# every function boundary, so omf_link --gc-sections strips each unreachable
# function individually (statics included) instead of whole-TU text blocks,
# and --pack-code re-packs the survivors back-to-back (word-aligned, no
# paragraph waste).  On the curated MicroPython link this cut code 703553 →
# 452461 bytes (-251 KB, -36%): the whole-TU granularity had been retaining
# every dead function in any partially-used TU (mpz, showbc, profile, ...).
# Only set here — the asm_to_omf default stays 56000 for builds that don't
# link with --gc-sections --pack-code (per-function segments without packing
# would ADD paragraph padding per function).
export QBE_TEXT_SEG_BUDGET=${QBE_TEXT_SEG_BUDGET:-1}

NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'

# Curated core subset: all py/*.c EXCEPT the other-architecture native-code
# emitters, inline assemblers, and NLR backends.  Those gate themselves out
# under our config (MICROPY_EMIT_NATIVE=0, MICROPY_NLR_SETJMP=1) and would
# only contribute empty objects or pull in arch symbols.  We keep nlr.c
# (generic dispatcher) and nlrsetjmp.c (our NLR backend).
DROP_RE='^(asmarm|asmbase|asmrv32|asmthumb|asmx64|asmx86|asmxtensa|emitinlinerv32|emitinlinethumb|emitinlinextensa|emitnarm|emitndebug|emitnrv32|emitnthumb|emitnx64|emitnx86|emitnxtensa|emitnxtensawin|nativeglue|nlraarch64|nlrmips|nlrpowerpc|nlrrv32|nlrrv64|nlrthumb|nlrx64|nlrx86|nlrxtensa)$'

CORE_SRCS=()
for f in "$MP"/py/*.c; do
	base=$(basename "$f" .c)
	[[ "$base" =~ $DROP_RE ]] && continue
	CORE_SRCS+=("$f")
done
PORT_SRCS=("$DOSPORT/main.c" "$DOSPORT/mphalport.c")
# softfloat.c provides the single-precision soft-libm: the _sf_add/sub/mul/div
# arithmetic helpers the i8086 backend calls for Ks ops, plus the algebraic +
# transcendental libm surface (floorf/powf/...) MicroPython references under
# MICROPY_FLOAT_IMPL_FLOAT.  Always linked; --gc-sections dead-strips it
# entirely when MICROPY_FLOAT_IMPL is NONE (no _sf_*/sf_* references), so the
# image is byte-identical to a build without it in that case.
SOFTFLOAT_SRC=("$DOS_DIR/softfloat.c")
ALL_SRCS=("${CORE_SRCS[@]}" "${PORT_SRCS[@]}" "${SOFTFLOAT_SRC[@]}")

echo "=== Compiling ${#ALL_SRCS[@]} TUs (${#CORE_SRCS[@]} core + ${#PORT_SRCS[@]} port + ${#SOFTFLOAT_SRC[@]} softfloat) [model=$MODEL${FARSTATIC_FLAG:+, far-static-data}] ==="

pass_objs=()
fail=()
for f in "${ALL_SRCS[@]}"; do
	base=$(basename "$f" .c)
	pp="$OUT_DIR/$base.pp.c"
	ssa="$OUT_DIR/$base.ssa"
	asm="$OUT_DIR/$base.asm"
	omf="$OUT_DIR/$base.omf.asm"
	obj="$OUT_DIR/$base.obj"
	err="$OUT_DIR/$base.err"
	: > "$err"

	if ! clang -E -P -nostdinc -DDOS -D__TURBOC__ $FARDATA_DEF \
			$MP_EXTRA_CPPFLAGS \
			"-I$DOSPORT" "-I$STUB" "-I$INC_DIR" "-I$MP" "-I$GENHDR" \
			"$f" 2>"$err" > "$OUT_DIR/$base.raw.c"; then
		fail+=("$base (cpp)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL cpp: $base"; cat "$err"; exit 1; }; continue
	fi
	tr -d '\r\032' < "$OUT_DIR/$base.raw.c" | sed "$NORMALIZE" > "$pp"
	if [ "$base" = "main" ] && [ "$MP_STACK_LIMIT" != "8192" ]; then
		sed "s/mp_stack_set_limit(8192);/mp_stack_set_limit($MP_STACK_LIMIT);/" "$pp" > "$pp.tmp"
		mv "$pp.tmp" "$pp"
	fi
	if [ "$base" = "main" ] && [ "$MP_HEAP_SIZE" != "49152" ]; then
		sed "s/static char heap\\[(49152)\\];/static char heap[($MP_HEAP_SIZE)];/" "$pp" > "$pp.tmp"
		mv "$pp.tmp" "$pp"
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
}' "$pp" > "$pp.tmp"
		mv "$pp.tmp" "$pp"
	fi
	if [ "$base" = "runtime" ] && [ "$MP_DOS_STACKLESS_RECURSION_RAISE" != "0" ]; then
		cat >> "$pp" <<'EOF'
__attribute__((noreturn)) void mp_raise_recursion_depth(void) {
    mp_raise_type_arg(&mp_type_RuntimeError, ((mp_obj_t)((((mp_uint_t)(MP_QSTR_maximum_space_recursion_space_depth_space_exceeded)) << 3) | 2)));
}
EOF
	fi

	if ! "$MINIC" -m "$MODEL" < "$pp" > "$ssa" 2>"$err"; then
		fail+=("$base (minic)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL minic: $base"; cat "$err"; exit 1; }; continue
	fi
	# Empty SSA: minic accepted but emitted nothing (arch-gated-out TU).  Skip.
	if [ ! -s "$ssa" ]; then continue; fi

	if ! "$QBE" -t i8086 -m "$MODEL" "$ssa" > "$asm" 2>"$err"; then
		fail+=("$base (qbe)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL qbe: $base"; cat "$err"; exit 1; }; continue
	fi
	if ! "$QBE_DIR/tools/asm_to_omf.py" "--model=$MODEL" $FARSTATIC_FLAG "$base" "$asm" "$omf" 2>"$err"; then
		fail+=("$base (omf-wrap)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL omf-wrap: $base"; cat "$err"; exit 1; }; continue
	fi
	if ! nasm -w-label-redef-late -f obj "$omf" -o "$obj" 2>"$err"; then
		fail+=("$base (nasm)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL nasm: $base"; cat "$err"; exit 1; }; continue
	fi
	pass_objs+=("$obj")
done

echo "  compiled ${#pass_objs[@]} objects; ${#fail[@]} failed"
if [ ${#fail[@]} -gt 0 ]; then
	printf '    FAIL: %s\n' "${fail[@]}"
fi

# crt0 + libstub.  Far-data models need crt0_exe to build argv as 4-byte far
# pointers (the FAR_DATA-gated path in crt0_exe.asm).
CRT0_FLAGS=""
case "$MODEL" in
	compact|large|huge) CRT0_FLAGS="-DFAR_DATA=1" ;;
esac
nasm $CRT0_FLAGS -f obj "$DOS_DIR/crt0_exe.asm" -o "$OUT_DIR/crt0_exe.obj" 2>"$OUT_DIR/crt0.err" || {
	echo "FAIL crt0 nasm"; cat "$OUT_DIR/crt0.err"; exit 1; }
"$QBE_DIR/tools/libstub_to_exe.py" "--model=$MODEL" \
	"$DOS_DIR/libstub.asm" "$OUT_DIR/libstub_exe.asm" 2>"$OUT_DIR/libstub.err" || {
	echo "FAIL libstub conv"; cat "$OUT_DIR/libstub.err"; exit 1; }
nasm -f obj "$OUT_DIR/libstub_exe.asm" -o "$OUT_DIR/libstub_exe.obj" 2>>"$OUT_DIR/libstub.err" || {
	echo "FAIL libstub nasm"; cat "$OUT_DIR/libstub.err"; exit 1; }

echo "=== Linking ==="
OBJS=("$OUT_DIR/crt0_exe.obj" "${pass_objs[@]}" "$OUT_DIR/libstub_exe.obj")
printf '%s\n' "${OBJS[@]}" > /tmp/mp_objs.txt
# --gc-sections dead-strips CODE/FAR_DATA segments unreachable from _start
# (the standard linker --gc-sections model, sound here because every
# cross-segment dependency is an OMF fixup).  This is the biggest size lever:
# print(1+2) touches a small fraction of the curated core, so the whole image
# drops well under the ~896KB Victor 9000 ceiling.  See NEXT_SESSION.md §2b.
# --pack-code coalesces the gc-surviving per-function CODE segments back into a
# few <=64KB buckets, reclaiming the per-function paragraph padding (~5KB on the
# core subset — see NEXT_SESSION.md §2p).  Safe because every code reference is
# an offset-aware OMF fixup and near jumps stay intra-function.
# The VM recurses through C frames for Python calls; 8KB corrupted the return
# path at recsum(8) on Victor.  24KB is the largest tested setting with current
# image size that still loads reliably (28KB reports "Program too big").
if "$QBE_DIR/tools/omf_link.py" \
		-o "$OUT_DIR/mpython.exe" \
		--map "$OUT_DIR/mpython.map" \
		--entry _start \
		--stack-size "$MP_STACK_SIZE" \
		--gc-sections \
		--pack-code \
		"${OBJS[@]}" 2>"$OUT_DIR/link.err"; then
	echo "  OK: $OUT_DIR/mpython.exe ($(wc -c <"$OUT_DIR/mpython.exe") bytes)"
else
	echo "  LINK FAILED — see $OUT_DIR/link.err:"
	tail -40 "$OUT_DIR/link.err"
	exit 1
fi
