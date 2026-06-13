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
	# §6p: the UNMODIFIED upstream SASI-backed FAT tests, run bare-metal on
	# the REAL -scsi:0 disk (hd field -> V9K_HARD_DISK scratch copy) through
	# bm_testhost + the full bm_stdio/VFS/FAT stack.  These can ONLY run
	# bare-metal (the DOS host has no raw SASI), so their goldens are
	# captured from the bare-metal run, not diffed against a DOS golden.
	# The upstream tests include "sasi.h" (build-newlibc-baremetal.sh's
	# SASI TU probe now matches it as well as bm_sasi.h; bm_sasi.c is the
	# byte-for-byte port).  sasi_fat_smoke_test reads CONFIG.SYS read-only
	# (small: 64771B code, just under the 64KB _TEXT ceiling).  The other
	# two need MEDIUM: dir adds dirent.c (66435B small -> over), and write
	# pulls fat_write.c (88797B).  sasi_fat_write_test is the headline —
	# create/write 2000B/read-back/append/unlink on the real disk via
	# vfs_mount_victor_fat_rw + SASI WRITE(6), an UNMODIFIED upstream test
	# (fatwrite_bm was a hand-mirrored minic TU); CONFIG.SYS checked intact
	# before+after.  Phase-8 multi-cluster SASI write on the 5 MHz 8088
	# dominates its budget (240 s, the §6f slowness rule), same as
	# fatwrite_bm.
	"sasi_fat_smoke_test:60:::hd"
	"sasi_fat_dir_test:90:::hd:medium"
	"sasi_fat_write_test:240:::hd:medium"
	# §6q: the UNMODIFIED upstream RAW BLOCK probe, run bare-metal on the
	# real -scsi:0 disk through bm_testhost.  Registers the SASI block
	# device, reads LBA 0 twice (cache-invalidated between) and checks the
	# checksum repeats, dumps the first 32 bytes, and prints the SASI bus
	# diagnostics.  This is the read-only block-layer counterpart to the
	# §6i sasi_bm minic TU and the §6p FAT family — the upstream test
	# itself, not a hand-mirrored port.  Read-only, but still MEDIUM:
	# 65577B code in small is 41B over the 64KB _TEXT ceiling (it wraps and
	# hangs, the §6p sasi_fat_dir_test lesson); medium is 70944B multi-CS.
	# Reads LBA 0 only, so the budget is modest despite medium.
	"sasi_sector_test:60:::hd:medium"
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
	# §6t: read_test -- the raw read(0,...) layer, the third keyboard-input
	# member after stdin_test/scanf_test.  read(0,&ch,1) reads ONE keystroke
	# ('A'); read(0,line,39) reads a cooked line.  The keypost edits with a
	# REAL Backspace (`vx\b9k` -> rubout x -> "v9k"), proving read(0) returns
	# the EDITED buffer with no `\b` byte and stops at the echoed Return; the
	# trailing `z` commits that Return into the IR6 ring (the §6o flush rule).
	# Small-model both hosts; the cooked console echoes (unlike the §6n DOS
	# redirect golden, whose input also omits the Backspace -- raw, no rubout).
	"read_test:35:Avx\b9k\nz::"
	# §6r: the UNMODIFIED upstream Victor-label FAT mount test, run
	# bare-metal through bm_testhost + the bm_stdio/VFS/FAT/block stack.
	# Unlike the §6i/§6p SASI tests this is RAM-disk style (it hand-builds
	# a Victor drive-label + volume-label + FAT12 in a media[] array via
	# block_register_ramdisk -- no -scsi:0), so it ALSO runs DOS-hosted
	# (the test-dos.sh NEWLIBC_TESTS gate) and its bare-metal output is
	# line-identical to the DOS golden between the testhost preamble and
	# result line.  It is the first deterministic golden for the Victor
	# drive-label -> volume-label -> relative-data-start parse path
	# (fat_mount_victor / vfs_mount_victor_fat), which was previously only
	# covered bare-metal-only on real SASI (§6i sasi_bm, §6p sasi_fat_*).
	# Small (60697B code, under the 64KB _TEXT ceiling); 9 output lines, so
	# a 60 s budget is ample.
	"fat_victor_label_test:60:::"
	# §6s: the UNMODIFIED upstream block_test exercises the block-device
	# layer DIRECTLY (one level below FAT): block_register_ramdisk +
	# block_init + read/write/multi-sector-read, write-through cache +
	# block_cache_invalidate, and the EINVAL/ENODEV/EROFS error paths, all
	# over RAM-backed media (no -scsi:0).  Like fat_victor_label_test it is
	# RAM-disk style, so it ALSO runs DOS-hosted (test-dos.sh NEWLIBC_TESTS)
	# and its bare-metal body is line-identical to the DOS golden between
	# the testhost preamble and result line.  This is the first
	# deterministic golden for the block layer in isolation -- the FAT
	# tests reach it transitively, but never assert the cache/error
	# semantics directly.  Small in both hosts (DOS 51811B, bare-metal
	# 60505B, under the 64KB _TEXT ceiling); 18 output lines -> 60 s ample.
	"block_test:60:::"
	# §6u: the UNMODIFIED upstream driver_test — the first DRIVER-layer
	# upstream test gated through bm_testhost (the §6p-§6t family covered
	# the portable stdio/vfs/fat/block surface; the bm_* ports covered the
	# drivers by hand-mirror).  It validates the Phase-1 hardware-fix story
	# against the LIVE bm_timer/PIC: Test 1 measures a real 100 ms delay via
	# timer_delay_ms and asserts ~10 ticks (deterministic 10 in MAME — both
	# the driver's timer_get_ticks() and delay_ms() read the same ISR-driven
	# counter, no tick falls between them), Test 2 asserts timer_get_frequency
	# ()==100, Test 4 reads the live 8259 IMR via the PIC_GET_MASK() MMIO
	# macro (0xBB, IR2 bit clear -> timer unmasked).  All five tests print
	# fixed text + PASS, return 0.  The timer/PIC/serial driver surface
	# resolves through bm_shim.c (timer_* -> bm_timer; PIC macros -> direct
	# volatile-far E000 MMIO), so NO build-script or compiler change — one
	# battery entry + one bare-metal-captured golden, like the §6q SASI
	# probe.  Bare-metal ONLY: the DOS host has no live 8253/8259, so the
	# measured-delay / live-IMR lines have no DOS golden to diff against.
	# Small (59485 B code, under the 64KB _TEXT ceiling); 71 output lines at
	# the §6f display-scroll rate (each printf mirrors to display+serial)
	# need a 90-emulated-second budget (60 truncated mid-Test-5).
	"driver_test:90:::"
	# §6v: the UNMODIFIED upstream simple_interrupt_test — the minimal
	# continuous-timer-interrupt test (read start ticks, then 5x
	# timer_delay_ms(1000) printing elapsed ticks, then unconditional
	# "PASS: Interrupts working!").  Complements §6u driver_test (which
	# measured a SINGLE 100ms delay): this proves 5 seconds of CONTINUOUS
	# timer interrupts keep the ISR tick_counter incrementing monotonically
	# under the full bm_stdio/bm_timer stack, resolving through bm_shim.c
	# (timer_get_ticks/timer_delay_ms -> bm_timer_*) — NOTHING new to link,
	# so like §6q/§6u just one battery entry + one bare-metal golden, zero
	# compiler/build-script change.  Bare-metal ONLY (no live 8253 on DOS).
	# Chosen over interrupt_test, which is unsuitable: its Test 1 reads
	# start_ticks BEFORE four slow display-mirrored printfs, so the
	# accumulated scroll ticks push elapsed past its [90,110] window ->
	# it would print "FAIL", and its Test 3 embeds a raw busy-loop
	# iteration count.  simple_interrupt_test has no pass/fail threshold
	# and no iteration count.  NOTE on the golden's tick values (111 /
	# 155 / 316 / 476 / 637 / 797): MAME models the Victor channel-2 input
	# clock FASTER than 100 Hz (the upstream interrupt_test comment warns
	# of this) and the slow display-mirrored printf between each delay
	# accumulates ~61 extra ticks, so elapsed grows ~161/iteration rather
	# than the nominal 100 — these are DISPLAY-SCROLL-TIMING-derived, not
	# wall-clock.  They are perfectly RUN-STABLE (MAME is cycle-
	# deterministic, verified byte-identical across repeated runs), so the
	# gate passes repeatedly; but unlike §6u's threshold-robust "10 ticks"
	# / "0xBB" they will SHIFT if a future toolchain change alters the
	# bm_tty/printf codegen timing -> re-capture the golden then (the PASS
	# verdict itself is unconditional and toolchain-independent).  Small
	# (58723 B code, under the 64KB _TEXT ceiling); 17 output lines + 5 s
	# of emulated timer delays -> 30 s budget ample.
	"simple_interrupt_test:30:::"
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
