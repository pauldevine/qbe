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
# 4-byte-far-pointer ABI.
FARSTATIC_FLAG=""
FARDATA_DEF=""
case "$MODEL" in
	compact|large|huge) FARSTATIC_FLAG="--far-static-data"; FARDATA_DEF="-DDOS_FAR_DATA=1" ;;
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
ALL_SRCS=("${CORE_SRCS[@]}" "${PORT_SRCS[@]}")

echo "=== Compiling ${#ALL_SRCS[@]} TUs (${#CORE_SRCS[@]} core + ${#PORT_SRCS[@]} port) [model=$MODEL${FARSTATIC_FLAG:+, far-static-data}] ==="

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
			"-I$DOSPORT" "-I$STUB" "-I$INC_DIR" "-I$MP" "-I$GENHDR" \
			"$f" 2>"$err" > "$OUT_DIR/$base.raw.c"; then
		fail+=("$base (cpp)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL cpp: $base"; cat "$err"; exit 1; }; continue
	fi
	tr -d '\r\032' < "$OUT_DIR/$base.raw.c" | sed "$NORMALIZE" > "$pp"

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
if "$QBE_DIR/tools/omf_link.py" \
		-o "$OUT_DIR/mpython.exe" \
		--map "$OUT_DIR/mpython.map" \
		--entry _start \
		--stack-size 8192 \
		"${OBJS[@]}" 2>"$OUT_DIR/link.err"; then
	echo "  OK: $OUT_DIR/mpython.exe ($(wc -c <"$OUT_DIR/mpython.exe") bytes)"
else
	echo "  LINK FAILED — see $OUT_DIR/link.err:"
	tail -40 "$OUT_DIR/link.err"
	exit 1
fi
