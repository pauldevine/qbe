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

# §8z: the phase-1 Victor DOS hardware probes (newlibc dos_tests/), run on the
# REAL Victor 9000 from the SASI boot disk (run-victor-sasi.sh).  Unlike
# test_memory_layout — DOSBox-gated in test-dos.sh, it touches only DOS
# PSP/segment/INT-0x12 conventions — these poke Victor MMIO (CRTC 0xE800, 7201
# 0xE040, VIA, screen 0xF000) and the live PIC/8253 that DOSBox lacks, so they
# only run on MAME victor9k.  Each diffs its serial stdout against a golden
# captured from the deterministic MAME run; loader-derived segment/address and
# cycle-derived values are run-stable (re-capture on a codegen/layout change —
# the §6v pattern).
#   Entry: <test-name> | <build-flags> | <emulated-seconds> | <golden-stem>
#
# Only es_preservation and display_dos are gated: they use Victor-native MMIO
# only.  The other four call IBM-PC BIOS services the Victor 9000 does NOT
# implement and crash (the CPU triple-faults, reboots, and AUTOEXEC re-runs the
# program — the serial capture then shows the banner + early tests repeated many
# times, never the later ones).  This is the same class as §8y's note that
# test_memory_layout "hangs only at Test 6's INT 0x12" (an IBM-PC BIOS call):
#   - test_timer_dos / test_integration: crash at the first get_dos_ticks()
#     (INT 1Ah, the IBM-PC BIOS time-of-day service) — timer dies in Test 2,
#     integration in Test 4 (before "Timer baseline" ever prints).
#   - test_keyboard_dos: crashes in Test 3 at kbhit()/getch() (INT 16h, the
#     IBM-PC BIOS keyboard service).
#   - test_serial_dos: crashes in Test 2, and its Test 5 also reprograms the
#     7201 channel A / 8253 channel-0 baud — i.e. the very serial line this
#     harness captures through.
# All four build and run (the vector intrinsics / softfloat / clock fills work —
# test_timer_dos's _dos_getvect prints the INT 0x42 handler before the INT 1Ah
# crash), they are just not Victor-runnable as written (IBM-PC-isms), so they
# cannot be deterministically gated.  (test_timer_dos needs medium + --softfloat
# + the clock() __MINIC__ fill; left wired in build-newlibc-test.sh for when an
# INT-1Ah-free timing path exists.)
DOS_TESTS=(
	"test_es_preservation|--no-libstub|120|es_preservation"
	"test_display_dos|--no-libstub|120|display_dos"
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

# Bare-metal timer-interrupt run (§6d): the compiler-emitted
# __attribute__((interrupt)) ISR (QBE `interrupt` linkage) taking live
# 8253-channel-2 IR2 ticks through the re-initialized memory-mapped PIC.
# Deterministic booleans/ranges only — MAME clocks channel 2 at 125 KHz
# vs the documented 100 KHz, so absolute tick-vs-wall numbers vary.
run_baremetal_timer() {
	if [ ! -d "${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}" ]; then
		echo "newlibc tree not found (set \$NEWLIBC_DIR)"; return 77
	fi
	"$QBE_DIR/tools/build-newlibc-baremetal.sh" timer_bm >/dev/null
	out="$("$QBE_DIR/tools/run-victor-baremetal.sh" \
		"$QBE_DIR/build/newlibc-baremetal/timer_bm/timer_bm.bin" 30)" \
		|| return $?
	echo "$out" | diff -u "$QBE_DIR/minic/dos/tests/timer_bm.golden.txt" - >&2
}

# §8z: build a phase-1 dos_test (newlibc dos_tests/) and diff its Victor/MAME
# SASI-boot serial stdout against a golden.  Exit 77 (skip) propagates from a
# missing newlibc tree or a missing MAME/disk so it shows "skip", not "fail".
run_dos_test_victor() {
	name="$1"; flags="$2"; secs="$3"; golden="$QBE_DIR/minic/dos/tests/dos_test_$4.golden.txt"
	nl="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"
	[ -d "$nl" ] || { echo "newlibc tree not found (set \$NEWLIBC_DIR)"; return 77; }
	# shellcheck disable=SC2086
	"$QBE_DIR/tools/build-newlibc-test.sh" "$name" $flags >/dev/null || return 1
	exe="$QBE_DIR/build/newlibc-tests/$name/$name.exe"
	out="$("$QBE_DIR/tools/run-victor-sasi.sh" "$exe" "$secs")" || return $?
	echo "$out" | diff -u "$golden" - >&2
}

run "build qbe + minic" \
	make -C "$QBE_DIR" -s qbe minic/minic

run "victor bare-metal (hello_bm, raw @ 0x3000)" run_baremetal_hello

run "victor bare-metal (timer_bm, live ISR)" run_baremetal_timer

for entry in "${VICTOR_TESTS[@]}"; do
	src="${entry%%:*}"
	rest="${entry#*:}"
	golden="${rest%%:*}"
	model="${rest##*:}"
	desc="victor ($(basename "$src" .c), $model)"
	run "$desc" run_victor_probe "$src" "$golden" "$model"
done

for entry in "${DOS_TESTS[@]}"; do
	IFS='|' read -r dt_name dt_flags dt_secs dt_stem <<EOF
$entry
EOF
	run "victor dos_test ($dt_name)" \
		run_dos_test_victor "$dt_name" "$dt_flags" "$dt_secs" "$dt_stem"
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
