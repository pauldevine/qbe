#!/bin/bash
# Run a built DOS .EXE on a REAL Victor 9000 / Sirius 1 (MAME machine
# `victor9k`, ~896KB RAM) headlessly and stream its captured stdout (CRLF /
# 0x1A stripped) to our stdout.  This is the Victor analog of
# tools/run-dos-exe.sh — use it for the on-target / >640KB / Victor-hardware
# cases that DOSBox (a 640KB IBM PC) cannot run.  Designed for
# `diff -u golden.txt <(tools/run-victor-mame.sh path/to/foo.exe)` checks.
#
# Usage:  tools/run-victor-mame.sh path/to/foo.exe [seconds_to_run]
#         tools/run-victor-mame.sh build/examples/cprobe/cprobe.exe 90
#
# HOW IT WORKS
#   We boot the stable Victor MS-DOS 3.1 floppy (python.img) in MAME, inject
#   the .EXE + a generated AUTOEXEC.BAT, and capture program stdout over the
#   emulated serial line:
#     * AUTOEXEC.BAT does `portset a 9600 none 1 8` then `ctty seriala`,
#       redirecting DOS CON (handle 1 = stdout) to serial port A.  Our qbe
#       programs already write stdout via INT 21h AH=40h to handle 1, so this
#       routes their output to the host with no program change.  9600 is the
#       CEILING — MAME's serial timing breaks above it.
#     * MAME's `-rs232a null_modem -bitbanger <file>` streams everything the
#       Victor transmits on port A to <file> on the host.
#     * Sentinel echoes (__V9BEGIN__ / __V9END__) bracket the program output
#       so we can trim the boot/prompt chatter that precedes and follows it.
#
#   We always operate on a COPY of the base image (never mutate the master).
#   File injection uses `vtg_image_util` (Victor FAT12 read/write).
#
# RESOURCES (overridable by env, mirroring run-dos-exe.sh's $DOSBOX):
#   $VICTOR_DISK  base boot floppy   (default ~/Desktop/randos/python.img)
#   $MAME         MAME binary        (default ~/projects/mame/mame, then PATH)
#   $MAME_ROMS    MAME rompath       (default <mame dir>/roms)
#   $VTG_IMAGE_UTIL  injector        (default `vtg_image_util` on PATH)
#
# Exits 77 (skip-the-test convention) if MAME, its roms, $VICTOR_DISK, or
# vtg_image_util is missing — tools/test-dos.sh treats 77 as skipped-not-failed
# so the gate degrades gracefully on machines without the Victor MAME setup.

set -eu

EXE="${1:-}"
RUN_SECS="${2:-${VICTOR_RUN_SECS:-90}}"

if [ -z "$EXE" ] || [ ! -f "$EXE" ]; then
	echo "usage: $0 <path-to-foo.exe> [seconds_to_run]" >&2
	exit 2
fi

# --- Locate MAME --------------------------------------------------------
if [ -n "${MAME:-}" ] && [ -x "$MAME" ]; then
	MAME_BIN="$MAME"
elif [ -x "$HOME/projects/mame/mame" ]; then
	MAME_BIN="$HOME/projects/mame/mame"
elif command -v mame >/dev/null 2>&1; then
	MAME_BIN="$(command -v mame)"
else
	echo "run-victor-mame: MAME not found (set \$MAME)" >&2
	exit 77
fi
MAME_DIR="$(cd "$(dirname "$MAME_BIN")" && pwd)"
MAME_ROMS="${MAME_ROMS:-$MAME_DIR/roms}"

# --- Locate the base disk + injector ------------------------------------
VICTOR_DISK="${VICTOR_DISK:-$HOME/Desktop/randos/python.img}"
if [ ! -f "$VICTOR_DISK" ]; then
	echo "run-victor-mame: base disk not found: $VICTOR_DISK (set \$VICTOR_DISK)" >&2
	exit 77
fi
VTG="${VTG_IMAGE_UTIL:-vtg_image_util}"
if ! command -v "$VTG" >/dev/null 2>&1 && [ ! -x "$VTG" ]; then
	echo "run-victor-mame: vtg_image_util not found (set \$VTG_IMAGE_UTIL)" >&2
	exit 77
fi
if [ ! -d "$MAME_ROMS" ]; then
	echo "run-victor-mame: MAME rompath not found: $MAME_ROMS (set \$MAME_ROMS)" >&2
	exit 77
fi

# --- Scratch workspace --------------------------------------------------
WORK="$(mktemp -d -t run-victor-mame.XXXXXX)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

RUN_IMG="$WORK/run.img"
CAP="$WORK/serial.txt"
cp "$VICTOR_DISK" "$RUN_IMG"
chmod u+w "$RUN_IMG"            # master may be read-only (0600)
: > "$CAP"

# A MAME homepath sandbox so we never pollute the user's ~/.mame cfg/nvram.
for d in cfg nvram inp sta snap diff comments; do mkdir -p "$WORK/home/$d"; done

# --- Generate AUTOEXEC.BAT ----------------------------------------------
# Output before `ctty seriala` goes to the video CON (discarded); everything
# after it streams to serial port A (captured).  The sentinels bracket the
# program's own output.  PORTSET syntax: PORTSET <A|B> <baud> <parity> <stop> <bits>.
# NOTE: bare `echo off` (NOT `@echo off`) — MS-DOS 3.1 predates the `@` line
# prefix, so `@echo off` is parsed as an unknown command and leaves echo ON,
# which leaks the `A:\>PROG.EXE` prompt+command into the serial capture.
AUTOEXEC="$WORK/AUTOEXEC.BAT"
{
	printf 'echo off\r\n'
	printf 'portset a 9600 none 1 8\r\n'
	printf 'ctty seriala\r\n'
	printf 'echo __V9BEGIN__\r\n'
	printf 'PROG.EXE\r\n'
	printf 'echo __V9END__\r\n'
} > "$AUTOEXEC"

# --- Inject the program + AUTOEXEC into the scratch image ---------------
# `copy` overwrites an existing name (no -f needed for copy).
"$VTG" copy "$EXE"      "$RUN_IMG:\\PROG.EXE"     >/dev/null 2>&1 \
	|| { echo "run-victor-mame: failed to inject $EXE into image" >&2; exit 1; }
"$VTG" copy "$AUTOEXEC" "$RUN_IMG:\\AUTOEXEC.BAT" >/dev/null 2>&1 \
	|| { echo "run-victor-mame: failed to inject AUTOEXEC.BAT into image" >&2; exit 1; }

# --- Run MAME headless --------------------------------------------------
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" "$MAME_BIN" victor9k \
	-rompath "$MAME_ROMS" \
	-homepath "$WORK/home" \
	-cfg_directory "$WORK/home/cfg" \
	-nvram_directory "$WORK/home/nvram" \
	-input_directory "$WORK/home/inp" \
	-state_directory "$WORK/home/sta" \
	-snapshot_directory "$WORK/home/snap" \
	-diff_directory "$WORK/home/diff" \
	-comment_directory "$WORK/home/comments" \
	-ramsize 896K \
	-flop1 "$RUN_IMG" \
	-video none -sound none -nothrottle -skip_gameinfo \
	-seconds_to_run "$RUN_SECS" \
	-rs232a null_modem -bitbanger "$CAP" \
	>/dev/null 2>&1 || true   # -seconds_to_run forces a non-zero exit; ignore

if [ ! -s "$CAP" ]; then
	echo "run-victor-mame: serial capture is empty (boot failed or no output)" >&2
	exit 1
fi

# Strip CR / 0x1A, then print only the lines strictly between the sentinels.
# The golden files use bare LF.
trimmed="$(tr -d '\r\032' < "$CAP" \
	| awk '/__V9BEGIN__/{f=1;next} /__V9END__/{f=0} f')"

if ! grep -q '__V9BEGIN__' "$CAP"; then
	echo "run-victor-mame: never saw __V9BEGIN__ sentinel in serial capture" >&2
	echo "---- raw capture ----" >&2
	tr -d '\r\032' < "$CAP" >&2
	echo "---------------------" >&2
	exit 1
fi

printf '%s\n' "$trimmed"
