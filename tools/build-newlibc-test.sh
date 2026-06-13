#!/bin/bash
# Build a newlibc phase3 test program as a DOS-hosted .EXE (§6b, Phase-6
# step 2).  Compiles the test TU + the newlibc portable subset (libgloss
# wrappers/syscalls, VFS, FAT, block layer) + minic/dos/newlibc/dos_shim.c
# with this toolchain, then links against crt0 + a --no-stdio libstub (the
# newlibc printf family and syscall layer replace libstub's stdio; libstub
# still provides malloc/free, str/mem fns, int86, and the _qbe_* helpers).
#
# The test's own main() is renamed via -Dmain=newlibc_test_main; the shim's
# main() runs vfs_init() first (board_init()'s job on hardware).
#
# Usage: tools/build-newlibc-test.sh [--model=small] <test-name|path.c>
#        test-name resolves to ~/projects/newlibc/phase3_newlib/tests/<name>.c
# Output: build/newlibc-tests/<name>/<name>.exe

set -eu

MODEL="small"
# Default DGROUP stack.  Tests with large static data (e.g. the §6k
# FAT-write tests' multi-KB ramdisk media[]) crowd the 64KB DGROUP and
# need a smaller stack; fat_write.c keeps its sector scratch static, so
# the write path's own stack need stays low.  Overridable per test.
STACK_SIZE=8192
SRC=""
for arg in "$@"; do
	case "$arg" in
		--model=*) MODEL="${arg#--model=}" ;;
		--stack-size=*) STACK_SIZE="${arg#--stack-size=}" ;;
		-h|--help)
			echo "usage: $0 [--model=small] [--stack-size=N] <test-name|path/to/test.c>" >&2
			exit 0 ;;
		--*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
		*) SRC="$arg" ;;
	esac
done
[ -n "$SRC" ] || { echo "usage: $0 [--model=small] [--stack-size=N] <test-name|path.c>" >&2; exit 2; }

case "$MODEL" in
	small) ;;
	# medium = far CODE, near DATA: code splits across multiple <=64KB CS
	# segments (escapes the small model's single-_TEXT 64KB ceiling that
	# blocks fat_write.c), while data stays in one DGROUP.  minic does NOT
	# far_stdlib-mangle calls under NEAR_DATA() (which includes medium), so
	# newlibc's own printf/str/mem stdio is called by its real name — the
	# far_stdlib concern is a far-DATA (compact/large) issue, not medium's.
	medium) ;;
	*) echo "$0: only --model=small|medium is supported; far-DATA models" \
	       "(compact/large) need far_stdlib-aware newlibc stdio" >&2; exit 2 ;;
esac

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NL="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"
if [ ! -f "$SRC" ]; then
	SRC="$NL/tests/$(basename "$SRC" .c).c"
fi
[ -f "$SRC" ] || { echo "$0: cannot find test source: $SRC" >&2; exit 2; }

base="$(basename "$SRC" .c)"
OUT_DIR="$QBE_DIR/build/newlibc-tests/$base"
MINIC="$QBE_DIR/minic/minic"
QBE="$QBE_DIR/qbe"
DOS_DIR="$QBE_DIR/minic/dos"
SHIM="$DOS_DIR/newlibc/shiminc"
INC="$QBE_DIR/minic/include"

# The portable-subset support TUs linked into every test.
SUPPORT_TUS=(
	"$NL/libgloss/printf_wrappers.c"
	"$NL/libgloss/scanf_wrappers.c"
	"$NL/libgloss/syscalls.c"
	"$NL/libgloss/reent_stubs.c"
	"$NL/libgloss/dirent.c"
	"$NL/libgloss/unlink.c"
	"$NL/vfs/vfs.c"
	"$NL/vfs/fat.c"
	"$NL/drivers/block.c"
	"$DOS_DIR/newlibc/dos_shim.c"
)

# fat_write.h pulls the FAT write layer (§6k): chain alloc/free, file
# create/write/truncate/unlink, the writable VFS mounts, and the runtime
# dispatch table it installs into vfs.c via vfs_set_fat_write_ops().  Only
# tests that exercise the write API include it, so read-only tests keep
# their footprint (the linking policy fat_write.h documents).
if grep -q 'fat_write\.h' "$SRC"; then
	SUPPORT_TUS+=("$NL/vfs/fat_write.c"
	              "$NL/libgloss/mkdir.c"
	              "$NL/libgloss/rmdir.c"
	              "$NL/libgloss/rename.c")
fi

mkdir -p "$OUT_DIR"
ERR="$OUT_DIR/build.err"
: > "$ERR"

# Same int-type normalization the triage sweep / MP build use.
NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'

# DOS-hosted termination: the bare-metal end-of-program idiom is
# `while (1) __asm__ volatile("hlt");` (test tails like bss_test, plus
# _exit/abort in libgloss/syscalls.c) — on hardware MAME kills the machine
# from outside, but under DOSBox the program never returns to COMMAND.COM,
# so the autoexec `exit` never runs and the harness hangs.  Rewrite the
# idle halt into a DOS process terminate (INT 21h AH=4Ch).
HALT2DOS='s/__asm__[[:space:]]*volatile[[:space:]]*([[:space:]]*"hlt"[[:space:]]*)/{ __asm__ volatile ("mov ax, 0x4c00"); __asm__ volatile ("int 0x21"); }/g'

# compile_unit <source.c> <obj-base> <extra-cpp-flags...>
compile_unit() {
	local unit_src="$1" unit_base="$2"
	shift 2
	# -D__ia16__ keeps __far real and selects the GCC MK_FP branch in
	# v9k_hw.h, matching minic semantics (same flags as the §6a sweep).
	clang -E -P -nostdinc -DDOS -D__ia16__ "$@" \
		"-I$SHIM" "-I$INC" \
		"-I$NL/include" "-I$NL/drivers" "-I$NL/libgloss" "-I$NL/vfs" \
		"$unit_src" 2>>"$ERR" \
		| tr -d '\r\032' | sed "$NORMALIZE" | sed "$HALT2DOS" \
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

# Test TU: rename its main so the shim's main can run vfs_init() first.
compile_unit "$SRC" "$base" -Dmain=newlibc_test_main \
	|| fail "compile failed: $base"

OBJ_FILES=("$OUT_DIR/$base.obj")
for tu in "${SUPPORT_TUS[@]}"; do
	tu_base="$(echo "${tu#$NL/}" | sed 's,.*/,,;s/\.c$//')"
	compile_unit "$tu" "$tu_base" || fail "compile failed: $tu_base"
	OBJ_FILES+=("$OUT_DIR/$tu_base.obj")
done

# crt0 + --no-stdio libstub.  small/tiny: -DNEAR_CODE (_main reached by a
# near call, all code in one _TEXT).  medium: far code (crt0 far-calls
# _main; no NEAR_CODE), matching build-example.sh's model gate.
CRT0_FLAGS=""
[ "$MODEL" = small ] && CRT0_FLAGS="-DNEAR_CODE=1"
nasm $CRT0_FLAGS -f obj "$DOS_DIR/crt0_exe.asm" -o "$OUT_DIR/crt0_exe.obj" 2>>"$ERR" \
	|| fail "crt0 assemble failed"
"$QBE_DIR/tools/libstub_to_exe.py" "--model=$MODEL" --no-stdio \
	"$DOS_DIR/libstub.asm" "$OUT_DIR/libstub_exe.asm" 2>>"$ERR" \
	|| fail "libstub_to_exe failed"
nasm -f obj "$OUT_DIR/libstub_exe.asm" -o "$OUT_DIR/libstub_exe.obj" 2>>"$ERR" \
	|| fail "libstub assemble failed"

"$QBE_DIR/tools/omf_link.py" \
	-o "$OUT_DIR/$base.exe" \
	--map "$OUT_DIR/$base.map" \
	--entry _start \
	--stack-size "$STACK_SIZE" \
	--gc-sections \
	"$OUT_DIR/crt0_exe.obj" \
	"${OBJ_FILES[@]}" \
	"$OUT_DIR/libstub_exe.obj" 2>>"$ERR" \
	|| fail "link failed: $base"

echo "  OK: $OUT_DIR/$base.exe ($(wc -c <"$OUT_DIR/$base.exe") bytes)"
