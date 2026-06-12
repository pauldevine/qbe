#!/bin/bash
# On-TARGET runtime gate: build a probe and run it on a REAL Victor 9000
# (MAME machine `victor9k`, ~896KB RAM) via tools/run-victor-mame.sh, diffing
# its serial-captured stdout against a golden.
#
# This is the Victor analog of the DOSBox path in tools/test-dos.sh.  It is a
# SEPARATE gate because each MAME run boots MS-DOS 3.1 from floppy (~1 min
# wall-clock), so we don't want it slowing the fast DOSBox gate.  Use it for
# the on-target / >640KB / Victor-hardware cases DOSBox (a 640KB IBM PC)
# cannot run; keep the small near/far probes on the DOSBox gate.
#
# Each MAME run degrades to "skip" (exit 77) when MAME, its roms, the base
# Victor disk, or vtg_image_util is missing — see run-victor-mame.sh.
#
# Usage:  tools/test-victor.sh
# Env:    $VICTOR_DISK / $MAME / $MAME_ROMS / $VTG_IMAGE_UTIL (see
#         run-victor-mame.sh); $VICTOR_RUN_SECS (per-run emulated seconds).

set -eu

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Each entry: `<src>:<golden>:<model>`.  Built via tools/build-example.sh
# --model=<model>, run on the Victor under MAME, serial stdout diff'd against
# the golden.  Start with the harness smoke test (cprobe) and grow as the
# >640KB / on-target cases come online (e.g. mpython.exe once shrunk).
VICTOR_TESTS=(
	"minic/dos/examples/cprobe.c:minic/dos/tests/cprobe.golden.txt:medium"
)

pass=0
fail=0
skip=0

run() {
	desc="$1"; shift
	printf '%-44s' "$desc"
	if "$@" >/tmp/test-victor.out 2>&1; then
		echo "[ok]"
		pass=$((pass + 1))
	else
		rc=$?
		if [ "$rc" -eq 77 ]; then
			echo "[skip]"
			sed 's/^/    /' /tmp/test-victor.out
			skip=$((skip + 1))
		else
			echo "[FAIL]"
			sed 's/^/    /' /tmp/test-victor.out
			fail=$((fail + 1))
		fi
	fi
}

# Build a probe at the requested model and diff its Victor/MAME serial stdout
# against a golden.  Two-step: build (fail fast) then run+diff.  Exit 77 from
# run-victor-mame.sh propagates so a missing MAME/disk shows "skip", not "fail".
run_victor_probe() {
	src="$1"
	golden="$2"
	model="$3"
	base="$(basename "$src" .c)"
	exe="$QBE_DIR/build/examples/$base/$base.exe"
	"$QBE_DIR/tools/build-example.sh" --model="$model" "$QBE_DIR/$src" >/dev/null
	out="$("$QBE_DIR/tools/run-victor-mame.sh" "$exe")" || return $?
	echo "$out" | diff -u "$QBE_DIR/$golden" - >&2
}

# Bare-metal raw-binary run (§6c): build hello_bm via the raw-binary link
# path, load it at 0x3000 with the MAME Lua loader (no DOS, no disk), diff
# serial output against the golden.  Skips (77) when the newlibc tree is
# absent (v9k_hw.h) or MAME is missing.
run_baremetal_hello() {
	if [ ! -d "${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}" ]; then
		echo "newlibc tree not found (set \$NEWLIBC_DIR)"; return 77
	fi
	"$QBE_DIR/tools/build-newlibc-baremetal.sh" hello_bm >/dev/null
	out="$("$QBE_DIR/tools/run-victor-baremetal.sh" \
		"$QBE_DIR/build/newlibc-baremetal/hello_bm/hello_bm.bin" 15)" \
		|| return $?
	echo "$out" | diff -u "$QBE_DIR/minic/dos/tests/hello_bm.golden.txt" - >&2
}

run "build qbe + minic" \
	make -C "$QBE_DIR" -s qbe minic/minic

run "victor bare-metal (hello_bm, raw @ 0x3000)" run_baremetal_hello

for entry in "${VICTOR_TESTS[@]}"; do
	src="${entry%%:*}"
	rest="${entry#*:}"
	golden="${rest%%:*}"
	model="${rest##*:}"
	desc="victor ($(basename "$src" .c), $model)"
	run "$desc" run_victor_probe "$src" "$golden" "$model"
done

echo
total=$((pass + fail))
if [ "$fail" -eq 0 ]; then
	if [ "$skip" -gt 0 ]; then
		echo "Victor pipeline: $pass/$total ok ($skip skipped)"
	else
		echo "Victor pipeline: $pass/$total ok"
	fi
	exit 0
else
	echo "Victor pipeline: $fail of $total tests FAILED ($skip skipped)"
	exit 1
fi
