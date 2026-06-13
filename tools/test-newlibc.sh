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

# Each entry: `<name>:<run-seconds>:<keypost>:<serial-in-bytes>:<disk>`.
# The keypost field goes through `printf %b`, so \b/\n escapes type the
# Victor Backspace/Return keys (run-victor-baremetal.sh passes any byte
# to MAME's natural keyboard).  A `hd` disk field attaches the known
# MAME Victor hard disk image as SASI target 0 (V9K_HARD_DISK; the
# harness runs against a scratch copy, so WRITE(6) tests are safe).
# Override the image with $V9K_HARD_DISK_IMAGE.
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

	if [ ! -d "$NL" ]; then
		echo "newlibc tree not found: $NL"
		return 77
	fi

	"$QBE_DIR/tools/build-newlibc-baremetal.sh" "$name" >/dev/null

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
	name="${entry%%:*}"; rest="${entry#*:}"
	secs="${rest%%:*}"; rest="${rest#*:}"
	keypost="${rest%%:*}"; rest="${rest#*:}"
	serial_bytes="${rest%%:*}"
	disk="${rest#*:}"
	if [ "${#ONLY[@]}" -gt 0 ]; then
		want=0
		for o in "${ONLY[@]}"; do
			[ "$o" = "$name" ] && want=1
		done
		[ "$want" = 1 ] || continue
	fi
	matched=$((matched + 1))
	run "newlibc bare-metal ($name)" \
		run_bm_test "$name" "$secs" "$keypost" "$serial_bytes" "$disk"
done

if [ "${#ONLY[@]}" -gt 0 ] && [ "$matched" -ne "${#ONLY[@]}" ]; then
	echo "$0: unknown test name(s) in: ${ONLY[*]}" >&2
	echo "  known: ${NEWLIBC_BM_TESTS[*]%%:*}" >&2
	exit 2
fi

echo
echo "newlibc bare-metal battery: $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
