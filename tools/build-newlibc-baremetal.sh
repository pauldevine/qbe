#!/bin/bash
# Build a bare-metal Victor 9000 raw binary with this toolchain (§6c,
# Phase-6 step 3).  Compiles the program TU + the bare-metal support TUs
# (minic-built crt0, polled serial console) small-model, links against a
# --no-stdio libstub (for the _qbe_* helpers and str/mem fns; its DOS
# INT 21h entry points are linked but must never be reached), and emits a
# flat binary via omf_link.py --raw-binary for the MAME Lua loader to
# place at 0x3000 (tools/run-victor-baremetal.sh).
#
# Unlike build-newlibc-test.sh there is NO DOS crt0 and NO hlt->INT 21h
# rewrite (on bare metal the hlt idle loop is exactly right; MAME is
# bounded from outside).  A program whose source resolves into newlibc's
# tests/ directory is an UNMODIFIED upstream test TU (§6j test-host mode):
# it gets the same -Dmain=newlibc_test_main rename the DOS-hosted gate
# uses, with bm_testhost.c (driver bring-up + vfs init + result line) as
# main() and the full bm_stdio stack linked.  Everything else keeps
# bm_crt0's start()-calls-main-directly arrangement with no rename.
#
# Usage: tools/build-newlibc-baremetal.sh [--model=small|medium]
#          [--load-addr=0x3000] <name|path.c>
#        bare name resolves to minic/dos/newlibc/<name>.c, then to
#        ~/projects/newlibc/phase3_newlib/tests/<name>.c
# Output: build/newlibc-baremetal/<name>/<name>.bin
#
# --model=medium (§6l) is the far-CODE/near-DATA port: code splits across
# per-TU <=64KB CS segments (escaping the small model's single-_TEXT 64KB
# ceiling that blocks fat_write.c on bare metal too — measured 81KB), while
# data stays in one DGROUP.  The same medium changes the DOS-hosted gate
# uses apply: minic does NOT far_stdlib-mangle under NEAR_DATA() (= medium),
# so newlibc stdio links by real name under --no-stdio; asm_to_omf splits
# every relocatable .long _sym into dw off + dw seg in medium (far CODE
# pointers in static device-ops tables, §6k); the raw-binary stub far-jumps
# to the entry symbol's CS:IP, which omf_link already resolves per-CS.

set -eu

MODEL="small"
LOAD_ADDR="0x3000"
# Default DGROUP stack.  Mirrors build-newlibc-test.sh: tests with large
# static data (e.g. fat_write_unit_test's hand-built RAM-volume media[]
# arrays, on top of the full bm_stdio driver set) crowd the near-data
# DGROUP and need a smaller stack.  Overridable per test.
STACK_SIZE=8192
SRC=""
for arg in "$@"; do
	case "$arg" in
		--model=*) MODEL="${arg#--model=}" ;;
		--load-addr=*) LOAD_ADDR="${arg#--load-addr=}" ;;
		--stack-size=*) STACK_SIZE="${arg#--stack-size=}" ;;
		-h|--help)
			echo "usage: $0 [--model=small|medium] [--stack-size=N] [--load-addr=0x3000] <name|path/to/prog.c>" >&2
			exit 0 ;;
		--*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
		*) SRC="$arg" ;;
	esac
done
[ -n "$SRC" ] || { echo "usage: $0 [--model=small|medium] [--stack-size=N] [--load-addr=0x3000] <name|path.c>" >&2; exit 2; }

case "$MODEL" in
	small|medium) ;;
	*) echo "$0: only --model=small|medium is supported (medium = far code," \
	       "near data; far-DATA models need far_stdlib-aware newlibc stdio)" >&2
	   exit 2 ;;
esac

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

# Upstream test TU -> test-host mode (§6j).
TESTHOST=0
case "$SRC" in
	"$NL"/tests/*.c) TESTHOST=1 ;;
esac

base="$(basename "$SRC" .c)"
OUT_DIR="$QBE_DIR/build/newlibc-baremetal/$base"

# Per-test extra preprocessor defines, applied to EVERY TU.  Only the bare-
# metal support TUs react (via #ifdef); the program and the newlibc TUs ignore
# them.  serial_loopback_test (§7i) commandeers 7201 channel A as a hardware
# TXD->RXD loopback for the newlibc console_* API it exercises, so bm_console.c
# moves the captured harness debug console (bm_putc) to channel B and bm_shim.c
# routes /dev/tty + the unprefixed console_* names to the raw channel-A serial
# path; tools/run-victor-baremetal.sh's V9K_SERIAL_LOOPBACK mode supplies the
# matching MAME wiring (-rs232a loopback + channel-B capture).
EXTRA_CFLAGS=()
case "$base" in
	serial_loopback_test) EXTRA_CFLAGS+=(-DBM_SERIAL_LOOPBACK) ;;
esac

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
# §8l: a hand-written bare-metal program that includes newlibc's OWN
# drivers/timer.h (NOT the bm_timer.h mirror) links the UPSTREAM
# drivers/timer.c -- the §8k gas->nasm in-place port, now RUN on hardware
# (timer_upstream_bm).  Excluded for test-host / bm_stdio programs: those
# pull bm_timer.c + bm_shim.c's timer_* aliases, which would collide with
# upstream timer.c's timer_* symbols.
if [ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h' "$SRC" \
   && grep -q '"timer\.h"' "$SRC"; then
	SUPPORT_TUS+=("$NL/drivers/timer.c")
fi
# §8m: same pattern for the UPSTREAM display driver -- a hand-written
# bare-metal program that includes newlibc's OWN drivers/display.h (NOT the
# bm_display.h mirror) links drivers/display.c + drivers/font_data.c (display.c
# copies victor_font into font RAM).  Same guard as timer.c: test-host /
# bm_stdio programs pull bm_display.c + bm_shim.c's display_* aliases, which
# would collide with upstream display.c's display_* symbols.  The `"display.h"`
# pattern (leading quote) does NOT match `"bm_display.h"`.
if [ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h' "$SRC" \
   && grep -q '"display\.h"' "$SRC"; then
	SUPPORT_TUS+=("$NL/drivers/display.c" "$NL/drivers/font_data.c")
fi
# §8n: same pattern for the UPSTREAM keyboard driver -- a hand-written
# bare-metal program that includes newlibc's OWN drivers/keyboard.h (NOT the
# bm_keyboard.h mirror) links drivers/keyboard.c (the §8k gas->nasm in-place
# port).  It is INTERRUPT-DRIVEN: the program installs an IR6 ISR routing each
# KBINT to keyboard_irq_handler (like §8l's timer routes IR2 to
# timer_tick_handler).  Same guard as timer.c/display.c: test-host / bm_stdio
# programs pull bm_keyboard.c + bm_shim.c's keyboard_* aliases, which would
# collide with upstream keyboard.c's keyboard_* symbols.  The `"keyboard.h"`
# pattern (leading quote) does NOT match `"bm_keyboard.h"`.
if [ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h' "$SRC" \
   && grep -q '"keyboard\.h"' "$SRC"; then
	SUPPORT_TUS+=("$NL/drivers/keyboard.c")
fi
# §8p: same pattern for the UPSTREAM serial console driver -- a hand-written
# bare-metal program that includes newlibc's OWN drivers/console.h (NOT a
# bm_*.h mirror) links drivers/console.c (the §8k gas->nasm in-place port: its
# intel_dev_write_byte + SAVE_ES/RESTORE_ES sites fork `#if __MINIC__`).  No
# symbol collision with the always-linked bm_console.c: bm_console.c defines
# only bm_*-prefixed names, and the unprefixed console_* aliases live in
# bm_shim.c -- which is test-host / bm_stdio only, so the guard excludes the
# §7i serial_loopback_test that uses them.  The `"console.h"` pattern (leading
# quote) does NOT match `"bm_console.h"`.  console.c compiles to one TU code
# segment, so --gc-sections keeps its unused cooked-console paths
# (console_dev_read/console_echo_input) live; the test supplies stub
# keyboard_getc/display_putc for them.
if [ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h' "$SRC" \
   && grep -q '"console\.h"' "$SRC"; then
	SUPPORT_TUS+=("$NL/drivers/console.c")
fi
if grep -q 'bm_interrupts\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_interrupts.c" "$NLC_DIR/bm_pic.c")
fi
if grep -q 'bm_pic\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_pic.c")
fi
if grep -q 'bm_display\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_display.c" "$NLC_DIR/bm_font_data.c")
fi
if grep -q 'bm_keyboard\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_keyboard.c"
	              "$NLC_DIR/bm_interrupts.c" "$NLC_DIR/bm_pic.c")
fi
if grep -q 'bm_serial\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_serial.c"
	              "$NLC_DIR/bm_interrupts.c" "$NLC_DIR/bm_pic.c")
fi
if grep -q 'bm_tty\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_tty.c"
	              "$NLC_DIR/bm_display.c" "$NLC_DIR/bm_font_data.c"
	              "$NLC_DIR/bm_keyboard.c"
	              "$NLC_DIR/bm_interrupts.c" "$NLC_DIR/bm_pic.c")
fi
# Test-host mode links bm_testhost.c as main() and always needs the
# stdio stack (the hosted upstream test prints through it).
if [ "$TESTHOST" = 1 ]; then
	SUPPORT_TUS+=("$NLC_DIR/bm_testhost.c")
fi
# bm_stdio.h pulls the whole newlibc stdio stack (§6h): printf/scanf
# wrappers -> libgloss syscalls -> VFS /dev/console -> bm_shim -> bm_tty.
# Same portable-subset TU set as build-newlibc-test.sh, with bm_shim.c
# in dos_shim.c's seat.
if [ "$TESTHOST" = 1 ] || grep -q 'bm_stdio\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_shim.c"
	              "$NLC_DIR/bm_tty.c"
	              "$NLC_DIR/bm_display.c" "$NLC_DIR/bm_font_data.c"
	              "$NLC_DIR/bm_keyboard.c"
	              "$NLC_DIR/bm_timer.c"
	              "$NLC_DIR/bm_interrupts.c" "$NLC_DIR/bm_pic.c"
	              "$NL/libgloss/printf_wrappers.c"
	              "$NL/libgloss/scanf_wrappers.c"
	              "$NL/libgloss/syscalls.c"
	              "$NL/libgloss/reent_stubs.c"
	              "$NL/libgloss/dirent.c"
	              "$NL/libgloss/unlink.c"
	              "$NL/vfs/vfs.c"
	              "$NL/vfs/fat.c"
	              "$NL/drivers/block.c")
fi
# bm_sasi.h pulls the SASI/Xebec block driver (§6i) + the block-device
# registry it registers with.  Unmodified upstream SASI tests (§6p) include
# the API header by its upstream name "sasi.h"; bm_sasi.h is a byte-for-byte
# compatible port (same struct layout, names, constants), so either include
# links bm_sasi.c.  The `sasi\.h` pattern matches both `sasi.h` and
# `bm_sasi.h`.
#
# §8o: same upstream-driver pattern as timer.c (§8l) / display.c (§8m) /
# keyboard.c (§8n) -- a non-test-host, non-bm_stdio program including newlibc's
# OWN drivers/sasi.h (the leading-quote `"sasi\.h"`, which does NOT match
# `"bm_sasi.h"`) links the UPSTREAM drivers/sasi.c (the §8k gas->nasm in-place
# port) instead of bm_sasi.c.  The guard is REQUIRED: the §6p/§6q upstream SASI
# tests run test-host (they print through bm_stdio, which pulls no sasi symbols
# but is the wrong harness for this) and the §6i sasi_bm includes bm_stdio.h --
# both must keep bm_sasi.c, so they fall to the elif.  Mutually exclusive with
# the bm_sasi.c branch so the two SASI drivers never both link.
if [ "$TESTHOST" = 0 ] && ! grep -q 'bm_stdio\.h' "$SRC" \
   && grep -q '"sasi\.h"' "$SRC"; then
	SUPPORT_TUS+=("$NL/drivers/sasi.c"
	              "$NL/drivers/block.c")
elif grep -q 'sasi\.h' "$SRC"; then
	SUPPORT_TUS+=("$NLC_DIR/bm_sasi.c"
	              "$NL/drivers/block.c")
fi
# fat_write.h pulls the FAT write layer (§6k): chain alloc/free, file
# create/write/truncate/unlink, the writable VFS mounts, and the runtime
# dispatch table it installs into vfs.c via vfs_set_fat_write_ops().  The
# read-only fat.c/vfs.c/block.c it builds on come from the bm_stdio set.
if grep -q 'fat_write\.h' "$SRC"; then
	SUPPORT_TUS+=("$NL/vfs/fat_write.c")
fi
# A program may pull the same support TU via more than one header probe
# (keyboard + serial both need interrupts/pic); dedup, preserving order.
DEDUP_TUS=()
for tu in "${SUPPORT_TUS[@]}"; do
	seen=0
	for d in "${DEDUP_TUS[@]:-}"; do
		[ "$d" = "$tu" ] && seen=1 && break
	done
	[ "$seen" = 0 ] && DEDUP_TUS+=("$tu")
done
SUPPORT_TUS=("${DEDUP_TUS[@]}")

mkdir -p "$OUT_DIR"
ERR="$OUT_DIR/build.err"
: > "$ERR"

# Same int-type normalization the DOS-hosted newlibc build uses.
NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'

compile_unit() {
	local unit_src="$1" unit_base="$2"
	shift 2
	# -D__ia16__ keeps __far real and selects the GCC MK_FP branch in
	# v9k_hw.h, matching minic semantics.  -D__MINIC__ is the §8k
	# convention: upstream driver TUs (e.g. drivers/timer.c, §8l) fork
	# their inline asm `#if defined(__MINIC__)` to Intel/nasm idioms
	# (HW_WRITE_BYTE etc.); harmless for every other TU (none of the bm_*.c
	# or portable stdio TUs test the macro).  No -DDOS, no HALT2DOS: this
	# is the bare machine.
	clang -E -P -nostdinc -D__ia16__ -D__MINIC__ "$@" \
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

# Test-host mode: same rename as the DOS-hosted gate, so bm_testhost's
# main() owns bring-up and calls the test as newlibc_test_main().
if [ "$TESTHOST" = 1 ]; then
	compile_unit "$SRC" "$base" -Dmain=newlibc_test_main \
		${EXTRA_CFLAGS[@]+"${EXTRA_CFLAGS[@]}"} \
		|| fail "compile failed: $base"
else
	compile_unit "$SRC" "$base" ${EXTRA_CFLAGS[@]+"${EXTRA_CFLAGS[@]}"} \
		|| fail "compile failed: $base"
fi

OBJ_FILES=("$OUT_DIR/$base.obj")
for tu in "${SUPPORT_TUS[@]}"; do
	tu_base="$(basename "$tu" .c)"
	compile_unit "$tu" "$tu_base" ${EXTRA_CFLAGS[@]+"${EXTRA_CFLAGS[@]}"} \
		|| fail "compile failed: $tu_base"
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
	--stack-size "$STACK_SIZE" \
	--gc-sections \
	"${OBJ_FILES[@]}" \
	"$OUT_DIR/libstub_exe.obj" 2>>"$ERR" \
	|| fail "link failed: $base"

echo "  OK: $OUT_DIR/$base.bin ($(wc -c <"$OUT_DIR/$base.bin") bytes) @ $LOAD_ADDR"
