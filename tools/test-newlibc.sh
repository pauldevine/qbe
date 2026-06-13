#!/bin/bash
# The newlibc BARE-METAL battery (§6e, Phase-6 step 4b): build every
# minic-built bare-metal test as a raw binary, run it on the Victor 9000
# under MAME (no DOS, no disk -- the Lua RAM loader), and golden-diff its
# serial output.  This is the standing newlibc robustness gate the
# Phase-6 plan called for; the DOS-hosted newlibc tests live in the fast
# DOSBox gate (tools/test-dos.sh) instead.
#
# Per-test input injection goes through run-victor-baremetal.sh:
#   keyboard_bm   V9K_KEYPOST=v9k        (MAME natural-keyboard typing)
#   serial_bm     V9K_SERIAL_IN=<bytes>  (7201 channel-B byte stream)
#
# Each run degrades to "skip" (exit 77) when MAME/roms or the newlibc
# tree is missing.
#
# Usage:  tools/test-newlibc.sh [--show] [test-name ...]
#   --show      run MAME in a WINDOW, throttled to authentic 5 MHz speed
#               (V9K_SHOW=1 in run-victor-baremetal.sh), so the Victor
#               screen is watchable.  Golden-diffing works as usual, but
#               each test takes its full seconds budget in wall time —
#               combine with a test name to watch just one.
#   test-name   run only the named test(s), e.g.:
#               tools/test-newlibc.sh --show interrupt_bm

set -eu

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NL="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"

SHOW=0
ONLY=()
for arg in "$@"; do
	case "$arg" in
		--show) SHOW=1 ;;
		-h|--help)
			cat >&2 <<-'USAGE'
			usage: tools/test-newlibc.sh [--show] [test-name ...]
			  --show      run MAME in a WINDOW, throttled to authentic
			              5 MHz speed, so the Victor screen is watchable;
			              combine with a test name to watch just one
			  test-name   run only the named test(s), e.g.:
			              tools/test-newlibc.sh --show interrupt_bm
			USAGE
			exit 0 ;;
		--*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
		*) ONLY+=("$arg") ;;
	esac
done
export V9K_SHOW="$SHOW"

# Each entry: `<name>:<run-seconds>:<keypost>:<serial-in-bytes>:<disk>:<model>`.
# The keypost field goes through `printf %b`, so \b/\n escapes type the
# Victor Backspace/Return keys (run-victor-baremetal.sh passes any byte
# to MAME's natural keyboard).  A `hd` disk field attaches the known
# MAME Victor hard disk image as SASI target 0 (V9K_HARD_DISK; the
# harness runs against a scratch copy, so WRITE(6) tests are safe).
# Override the image with $V9K_HARD_DISK_IMAGE.  The optional `model`
# field (default small) forces the build memory model — fatwrite_bm
# needs `medium` because fat_write.c overflows the small model's
# single-_TEXT 64KB code ceiling on bare metal too (§6l).
NEWLIBC_BM_TESTS=(
	"hello_bm:15:::"
	"timer_bm:30:::"
	"display_bm:20:::"
	"keyboard_bm:25:v9k::"
	"serial_bm:25::victor:"
	"memory_bm:15:::"
	"crtc_bm:20:::"
	"pic_bm:35:::"
	"interrupt_bm:120:::"
	"tty_bm:30:vx\b9k\nz::"
	"stdio_bm:45:vx\b9k\nz::"
	"sasi_bm:90:::hd"
	# §6j: UNMODIFIED upstream newlibc tests re-run bare-metal through
	# bm_testhost + the bm_stdio stack (build-newlibc-baremetal.sh
	# test-host mode).  Output is line-identical to the DOS-hosted
	# goldens between the testhost preamble and result line.  Budgets
	# follow output length (display scroll ~1.5 emulated s/line, §6f);
	# ramfs_test's 103 lines need the 300 s budget.
	"snprintf_test:45:::"
	"stdio_route_test:45:::"
	"fat_bpb_test:60:::"
	"fat_chain_test:60:::"
	"fat_root_test:60:::"
	"fat_dir_test:60:::"
	"fat_file_test:60:::"
	"fat_vfs_test:60:::"
	"terminal_meta_test:90:::"
	"ramfs_test:300:::"
	# §6l: the bare-metal FAT WRITE test — the first MEDIUM-model bare-metal
	# program (fat_write.c overflows small's 64KB _TEXT here too; measured
	# 88KB code).  Writes to a scratch copy of the SASI disk through the §6i
	# bm_sasi WRITE(6) path.  Phase 8's multi-cluster write over real SASI on
	# the 5 MHz 8088 dominates the budget (the §6f slowness lesson): 240
	# emulated seconds (90 truncated mid-write — slowness, not a hang).
	"fatwrite_bm:240:::hd:medium"
	# §6m: the FAT-write UNIT test re-run bare-metal — a second MEDIUM gate
	# exercising fat_write.c's primitives DIRECTLY (FAT16 entry/mirror/chain
	# + FAT12 sector-straddle, both parities) on hand-built RAM volumes, so
	# no SASI disk.  Its media[] arrays crowd the near-data DGROUP on top of
	# the full bm_stdio driver set, so it needs a 4096 stack (data+bss
	# 60688; 8192 overflows 64KB).  RAM-only ops are fast; output is short.
	"fat_write_unit_test:60::::medium:4096"
	# §6o: the two KEYBOARD-INPUT tests, driven bare-metal through the
	# cooked bm_tty console (NOT the §6n DOS `< IN.TXT` redirect — on
	# hardware they read CON, echoed, via the interrupt-driven keyboard).
	# getchar/fgets/scanf reach _read(0,...) -> console_dev_read ->
	# bm_tty_read; getchar reads ONE keystroke (count=1, no Enter), the
	# line readers stop at the echoed Return.  The keypost is one
	# natkeyboard burst into the IR6 ring, so timing is irrelevant -- but
	# a trailing throwaway char AFTER the final `\n` is REQUIRED to commit
	# that Return into the ring before the program blocks on it (the §6h
	# stdio_bm lesson: a `\n` at the very end of a post is not flushed).
	# stdin_test: `Ahello\nz` -> getchar='A', fgets="hello\n" (z unused).
	# scanf_test: `victor 42\nz` -> %15s="victor" %d=42 (\n ends %d).
	# Goldens differ from the §6n DOS redirect goldens because the cooked
	# console ECHOES the typed input; build small-model (no fat_write.c).
	"stdin_test:35:Ahello\nz::"
	"scanf_test:35:victor 42\nz::"
)
HARD_DISK_IMAGE="${V9K_HARD_DISK_IMAGE:-$HOME/projects/mame/victor_30mb.img}"

pass=0
fail=0
skip=0

run() {
	desc="$1"; shift
	printf '%-44s' "$desc"
	if "$@" >/tmp/test-newlibc.out 2>&1; then
		echo "[ok]"
		pass=$((pass + 1))
	else
		rc=$?
		if [ "$rc" -eq 77 ]; then
			echo "[skip]"
			sed 's/^/    /' /tmp/test-newlibc.out
			skip=$((skip + 1))
		else
			echo "[FAIL]"
			sed 's/^/    /' /tmp/test-newlibc.out
			fail=$((fail + 1))
		fi
	fi
}

run_bm_test() {
	name="$1"
	secs="$2"
	keypost="$(printf '%b' "$3")"
	serial_bytes="$4"
	disk="$5"
	model="${6:-small}"
	stack="${7:-}"   # empty -> build script's default DGROUP stack

	if [ ! -d "$NL" ]; then
		echo "newlibc tree not found: $NL"
		return 77
	fi

	stack_arg=""
	[ -n "$stack" ] && stack_arg="--stack-size=$stack"
	"$QBE_DIR/tools/build-newlibc-baremetal.sh" "--model=$model" $stack_arg "$name" >/dev/null

	out_dir="$QBE_DIR/build/newlibc-baremetal/$name"
	serial_in=""
	if [ -n "$serial_bytes" ]; then
		serial_in="$out_dir/serial_in.bin"
		printf '%s' "$serial_bytes" > "$serial_in"
	fi
	hard_disk=""
	if [ "$disk" = "hd" ]; then
		hard_disk="$HARD_DISK_IMAGE"
	fi

	out="$(V9K_KEYPOST="$keypost" V9K_SERIAL_IN="$serial_in" \
		V9K_HARD_DISK="$hard_disk" \
		"$QBE_DIR/tools/run-victor-baremetal.sh" \
		"$out_dir/$name.bin" "$secs")" || return $?
	echo "$out" | diff -u "$QBE_DIR/minic/dos/tests/$name.golden.txt" - >&2
}

matched=0
for entry in "${NEWLIBC_BM_TESTS[@]}"; do
	# <name>:<secs>:<keypost>:<serial>:<disk>:<model>:<stack> — the last
	# two are optional (model default small, stack default per build
	# script).  No field contains a colon, so IFS splitting is exact and
	# preserves empty middle fields (the `::` keypost/serial gaps).
	IFS=: read -r name secs keypost serial_bytes disk model stack <<EOF
$entry
EOF
	[ -n "$model" ] || model="small"
	if [ "${#ONLY[@]}" -gt 0 ]; then
		want=0
		for o in "${ONLY[@]}"; do
			[ "$o" = "$name" ] && want=1
		done
		[ "$want" = 1 ] || continue
	fi
	matched=$((matched + 1))
	run "newlibc bare-metal ($name)" \
		run_bm_test "$name" "$secs" "$keypost" "$serial_bytes" "$disk" "$model" "$stack"
done

if [ "${#ONLY[@]}" -gt 0 ] && [ "$matched" -ne "${#ONLY[@]}" ]; then
	echo "$0: unknown test name(s) in: ${ONLY[*]}" >&2
	echo "  known: ${NEWLIBC_BM_TESTS[*]%%:*}" >&2
	exit 2
fi

echo
echo "newlibc bare-metal battery: $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
