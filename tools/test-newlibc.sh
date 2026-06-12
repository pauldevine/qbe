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
# Usage:  tools/test-newlibc.sh

set -eu

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NL="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"

# Each entry: `<name>:<run-seconds>:<keypost>:<serial-in-bytes>`.
NEWLIBC_BM_TESTS=(
	"hello_bm:15::"
	"timer_bm:30::"
	"display_bm:20::"
	"keyboard_bm:25:v9k:"
	"serial_bm:25::victor"
)

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
	keypost="$3"
	serial_bytes="$4"

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

	out="$(V9K_KEYPOST="$keypost" V9K_SERIAL_IN="$serial_in" \
		"$QBE_DIR/tools/run-victor-baremetal.sh" \
		"$out_dir/$name.bin" "$secs")" || return $?
	echo "$out" | diff -u "$QBE_DIR/minic/dos/tests/$name.golden.txt" - >&2
}

for entry in "${NEWLIBC_BM_TESTS[@]}"; do
	name="${entry%%:*}"; rest="${entry#*:}"
	secs="${rest%%:*}"; rest="${rest#*:}"
	keypost="${rest%%:*}"
	serial_bytes="${rest#*:}"
	run "newlibc bare-metal ($name)" \
		run_bm_test "$name" "$secs" "$keypost" "$serial_bytes"
done

echo
echo "newlibc bare-metal battery: $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
