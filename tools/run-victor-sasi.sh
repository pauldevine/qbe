#!/bin/bash
# Run a built DOS .EXE on a REAL Victor 9000 / Sirius 1 (MAME machine
# `victor9k`, ~896KB RAM) from a SASI HARD DISK image, headlessly, streaming its
# captured stdout (CRLF / 0x1A stripped) to our stdout.
#
# This is the hard-disk sibling of tools/run-victor-mame.sh.  Use it for images
# TOO BIG to fit on a Victor floppy: `vtg_image_util` caps single-file floppy
# writes ~440KB, and mpython.exe is >800KB.  The dedicated bootable SASI disk
# holds the program in partition 0 (the boot drive) and we boot from it with NO
# floppy attached (a present floppy is always preferred for boot on the Victor).
#
# Usage:  tools/run-victor-sasi.sh path/to/foo.exe [seconds_to_run]
#         tools/run-victor-sasi.sh build/mp-link/mpython.exe 220
#
# HOW IT WORKS (same serial-capture path as run-victor-mame.sh)
#   We boot the Victor MS-DOS 3.1 SASI disk in MAME, inject the .EXE as
#   partition 0's PROG.EXE (the disk already carries the AUTOEXEC.BAT that does
#   `portset a 9600 none 1 8` / `ctty seriala` / sentinel echoes), and capture
#   program stdout over the emulated serial line via `-rs232a null_modem
#   -bitbanger <file>`.  Sentinels (__V9BEGIN__ / __V9END__) bracket the output.
#
#   We operate on a COPY of the base image (never mutate the master).
#
# RESOURCES (overridable by env):
#   $VICTOR_SASI_DISK  base SASI hard disk (default ~/projects/qbe/victor_python.img)
#   $MAME              MAME binary         (default ~/projects/mame/mame, then PATH)
#   $MAME_ROMS         MAME rompath        (default <mame dir>/roms)
#   $VTG_IMAGE_UTIL    injector            (default `vtg_image_util` on PATH)
#
# Exits 77 (skip) if MAME, its roms, the disk, or vtg_image_util is missing.

set -eu

EXE="${1:-}"
RUN_SECS="${2:-${VICTOR_RUN_SECS:-220}}"

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
	echo "run-victor-sasi: MAME not found (set \$MAME)" >&2
	exit 77
fi
MAME_DIR="$(cd "$(dirname "$MAME_BIN")" && pwd)"
MAME_ROMS="${MAME_ROMS:-$MAME_DIR/roms}"

# --- Locate the base disk + injector ------------------------------------
SASI_DISK="${VICTOR_SASI_DISK:-$HOME/projects/qbe/victor_python.img}"
if [ ! -f "$SASI_DISK" ]; then
	echo "run-victor-sasi: SASI disk not found: $SASI_DISK (set \$VICTOR_SASI_DISK)" >&2
	exit 77
fi
VTG="${VTG_IMAGE_UTIL:-vtg_image_util}"
if ! command -v "$VTG" >/dev/null 2>&1 && [ ! -x "$VTG" ]; then
	echo "run-victor-sasi: vtg_image_util not found (set \$VTG_IMAGE_UTIL)" >&2
	exit 77
fi
if [ ! -d "$MAME_ROMS" ]; then
	echo "run-victor-sasi: MAME rompath not found: $MAME_ROMS (set \$MAME_ROMS)" >&2
	exit 77
fi

# --- Scratch workspace --------------------------------------------------
WORK="$(mktemp -d -t run-victor-sasi.XXXXXX)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

RUN_IMG="$WORK/run.img"
CAP="$WORK/serial.txt"
cp "$SASI_DISK" "$RUN_IMG"
chmod u+w "$RUN_IMG"
: > "$CAP"

# A MAME homepath sandbox so we never pollute the user's ~/.mame cfg/nvram.
for d in cfg nvram inp sta snap diff comments; do mkdir -p "$WORK/home/$d"; done

# --- Inject the program into partition 0 (the boot drive) ---------------
# The disk already carries the correct AUTOEXEC.BAT (echo off / portset /
# ctty seriala / sentinel echoes / prog).  `copy` overwrites an existing name.
"$VTG" copy "$EXE" "$RUN_IMG:0:\\PROG.EXE" >/dev/null 2>&1 \
	|| { echo "run-victor-sasi: failed to inject $EXE into image partition 0" >&2; exit 1; }

# --- Run MAME headless (SASI hard disk, NO floppy) ----------------------
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
	-scsi:0 harddisk -hard1 "$RUN_IMG" \
	-video none -sound none -nothrottle -skip_gameinfo \
	-seconds_to_run "$RUN_SECS" \
	-rs232a null_modem -bitbanger "$CAP" \
	>/dev/null 2>&1 || true   # -seconds_to_run forces a non-zero exit; ignore

if [ ! -s "$CAP" ]; then
	echo "run-victor-sasi: serial capture is empty (boot failed or no output)" >&2
	exit 1
fi

# Strip CR / 0x1A, then print only the lines strictly between the sentinels.
trimmed="$(tr -d '\r\032' < "$CAP" \
	| awk '/__V9BEGIN__/{f=1;next} /__V9END__/{f=0} f')"

if ! grep -q '__V9BEGIN__' "$CAP"; then
	echo "run-victor-sasi: never saw __V9BEGIN__ sentinel in serial capture" >&2
	echo "---- raw capture ----" >&2
	tr -d '\r\032' < "$CAP" >&2
	echo "---------------------" >&2
	exit 1
fi

printf '%s\n' "$trimmed"
