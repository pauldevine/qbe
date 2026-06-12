#!/bin/bash
# Build a bare-metal Victor 9000 raw binary with this toolchain (§6c,
# Phase-6 step 3).  Compiles the program TU + the bare-metal support TUs
# (minic-built crt0, polled serial console) small-model, links against a
# --no-stdio libstub (for the _qbe_* helpers and str/mem fns; its DOS
# INT 21h entry points are linked but must never be reached), and emits a
# flat binary via omf_link.py --raw-binary for the MAME Lua loader to
# place at 0x3000 (tools/run-victor-baremetal.sh).
#
# Unlike build-newlibc-test.sh there is NO -Dmain rename (bm_crt0's start()
# calls main directly), NO DOS crt0, and NO hlt->INT 21h rewrite (on bare
# metal the hlt idle loop is exactly right; MAME is bounded from outside).
#
# Usage: tools/build-newlibc-baremetal.sh [--load-addr=0x3000] <name|path.c>
#        bare name resolves to minic/dos/newlibc/<name>.c, then to
#        ~/projects/newlibc/phase3_newlib/tests/<name>.c
# Output: build/newlibc-baremetal/<name>/<name>.bin

set -eu

MODEL="small"
LOAD_ADDR="0x3000"
SRC=""
for arg in "$@"; do
	case "$arg" in
		--load-addr=*) LOAD_ADDR="${arg#--load-addr=}" ;;
		-h|--help)
			echo "usage: $0 [--load-addr=0x3000] <name|path/to/prog.c>" >&2
			exit 0 ;;
		--*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
		*) SRC="$arg" ;;
	esac
done
[ -n "$SRC" ] || { echo "usage: $0 [--load-addr=0x3000] <name|path.c>" >&2; exit 2; }

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NL="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"
DOS_DIR="$QBE_DIR/minic/dos"
NLC_DIR="$DOS_DIR/newlibc"
SHIM="$NLC_DIR/shiminc"
INC="$QBE_DIR/minic/include"
MINIC="$QBE_DIR/minic/minic"
QBE="$QBE_DIR/qbe"

if [ ! -f "$SRC" ]; then
	base_try="$(basename "$SRC" .c)"
	if [ -f "$NLC_DIR/$base_try.c" ]; then
		SRC="$NLC_DIR/$base_try.c"
	else
		SRC="$NL/tests/$base_try.c"
	fi
fi
[ -f "$SRC" ] || { echo "$0: cannot find program source: $SRC" >&2; exit 2; }

base="$(basename "$SRC" .c)"
OUT_DIR="$QBE_DIR/build/newlibc-baremetal/$base"

# The bare-metal support TUs linked into every program.
SUPPORT_TUS=(
	"$NLC_DIR/bm_crt0.c"
	"$NLC_DIR/bm_console.c"
)

# Optional driver TUs (§6d), linked only when the program includes their
# headers — keeps driver-free images (hello_bm) byte-stable for
# test_omf_link.sh's raw-structure asserts.
if grep -q 'bm_timer\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_timer.c")
fi
if grep -q 'bm_interrupts\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_interrupts.c" "$NLC_DIR/bm_pic.c")
fi

mkdir -p "$OUT_DIR"
ERR="$OUT_DIR/build.err"
: > "$ERR"

# Same int-type normalization the DOS-hosted newlibc build uses.
NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'

compile_unit() {
	local unit_src="$1" unit_base="$2"
	shift 2
	# -D__ia16__ keeps __far real and selects the GCC MK_FP branch in
	# v9k_hw.h, matching minic semantics.  No -DDOS, no HALT2DOS: this is
	# the bare machine.
	clang -E -P -nostdinc -D__ia16__ "$@" \
		"-I$NLC_DIR" "-I$SHIM" "-I$INC" \
		"-I$NL/include" "-I$NL/drivers" "-I$NL/libgloss" "-I$NL/vfs" \
		"$unit_src" 2>>"$ERR" \
		| tr -d '\r\032' | sed "$NORMALIZE" \
		> "$OUT_DIR/$unit_base.pp.c" \
		|| return 1
	"$MINIC" -m "$MODEL" < "$OUT_DIR/$unit_base.pp.c" \
		> "$OUT_DIR/$unit_base.ssa" 2>>"$ERR" || return 1
	"$QBE" -t i8086 -m "$MODEL" "$OUT_DIR/$unit_base.ssa" \
		> "$OUT_DIR/$unit_base.asm" 2>>"$ERR" || return 1
	"$QBE_DIR/tools/asm_to_omf.py" "--model=$MODEL" "$unit_base" \
		"$OUT_DIR/$unit_base.asm" "$OUT_DIR/$unit_base.omf.asm" 2>>"$ERR" \
		|| return 1
	nasm -w-label-redef-late -f obj "$OUT_DIR/$unit_base.omf.asm" \
		-o "$OUT_DIR/$unit_base.obj" 2>>"$ERR" || return 1
}

fail() { echo "$0: $1 (see $ERR)" >&2; tail -5 "$ERR" >&2; exit 1; }

compile_unit "$SRC" "$base" || fail "compile failed: $base"

OBJ_FILES=("$OUT_DIR/$base.obj")
for tu in "${SUPPORT_TUS[@]}"; do
	tu_base="$(basename "$tu" .c)"
	compile_unit "$tu" "$tu_base" || fail "compile failed: $tu_base"
	OBJ_FILES+=("$OUT_DIR/$tu_base.obj")
done

# --no-stdio libstub (near code): _qbe_* helpers, str/mem fns, malloc.
"$QBE_DIR/tools/libstub_to_exe.py" "--model=$MODEL" --no-stdio \
	"$DOS_DIR/libstub.asm" "$OUT_DIR/libstub_exe.asm" 2>>"$ERR" \
	|| fail "libstub_to_exe failed"
nasm -f obj "$OUT_DIR/libstub_exe.asm" -o "$OUT_DIR/libstub_exe.obj" 2>>"$ERR" \
	|| fail "libstub assemble failed"

"$QBE_DIR/tools/omf_link.py" \
	-o "$OUT_DIR/$base.bin" \
	--map "$OUT_DIR/$base.map" \
	--raw-binary --load-addr "$LOAD_ADDR" \
	--entry _start \
	--stack-size 8192 \
	--gc-sections \
	"${OBJ_FILES[@]}" \
	"$OUT_DIR/libstub_exe.obj" 2>>"$ERR" \
	|| fail "link failed: $base"

echo "  OK: $OUT_DIR/$base.bin ($(wc -c <"$OUT_DIR/$base.bin") bytes) @ $LOAD_ADDR"
