#!/bin/bash
# run-victor-wp.sh — like run-victor-sasi.sh, but boots the Victor under MAME's
# headless debugger (`-debug -debugger none -debugscript <file>`) so a watchpoint
# can catch the PC of a wild memory write.
#
# WHY this works headless (verified against MAME 0.287 source):
#   * A watchpoint's ACTION string runs via debugger_console::execute_command()
#     inside debug_watchpoint::triggered() REGARDLESS of the debugger front-end,
#     so `-debugger none` (which otherwise just auto-`go`s) still runs the action.
#   * `printf` in the action goes to the in-memory console buffer that `none`
#     discards — but `trace`/`tracelog` write straight to a FILE.  So the action
#     opens a trace file in APPEND mode, `tracelog`s the registers (incl. cs/ip),
#     then `trace off` (flush+close) and `go`.  The trace file then holds one
#     "WPHIT ..." line per firing.
#
# Usage:  tools/run-victor-wp.sh <exe> <debugscript> <trace_out> [seconds_to_run]
#   The <debugscript> is a MAME debugger command file; bake the absolute
#   <trace_out> path into its `trace >>...` lines.  <trace_out> is pre-created
#   (truncated) so append-mode `trace >>` can open it.
#
# Env: same overrides as run-victor-sasi.sh ($MAME, $MAME_ROMS,
#      $VICTOR_SASI_DISK, $VTG_IMAGE_UTIL, $VICTOR_SRC).
# Exits 77 (skip) if a resource is missing.

set -eu

EXE="${1:-}"
DBGSCRIPT="${2:-}"
TRACE_OUT="${3:-}"
RUN_SECS="${4:-${VICTOR_RUN_SECS:-260}}"

if [ -z "$EXE" ] || [ ! -f "$EXE" ] || [ -z "$DBGSCRIPT" ] || [ ! -f "$DBGSCRIPT" ] || [ -z "$TRACE_OUT" ]; then
	echo "usage: $0 <exe> <debugscript> <trace_out> [seconds_to_run]" >&2
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
	echo "run-victor-wp: MAME not found (set \$MAME)" >&2
	exit 77
fi
MAME_DIR="$(cd "$(dirname "$MAME_BIN")" && pwd)"
MAME_ROMS="${MAME_ROMS:-$MAME_DIR/roms}"

SASI_DISK="${VICTOR_SASI_DISK:-$HOME/projects/qbe/victor_python.img}"
[ -f "$SASI_DISK" ] || { echo "run-victor-wp: SASI disk not found: $SASI_DISK" >&2; exit 77; }
VTG="${VTG_IMAGE_UTIL:-vtg_image_util}"
if ! command -v "$VTG" >/dev/null 2>&1 && [ ! -x "$VTG" ]; then
	echo "run-victor-wp: vtg_image_util not found (set \$VTG_IMAGE_UTIL)" >&2
	exit 77
fi
[ -d "$MAME_ROMS" ] || { echo "run-victor-wp: MAME rompath not found: $MAME_ROMS" >&2; exit 77; }

WORK="$(mktemp -d -t run-victor-wp.XXXXXX)"
MAME_PID=""
WATCHDOG_PID=""
cleanup() {
	[ -n "$MAME_PID" ] && kill -9 "$MAME_PID" 2>/dev/null || true
	[ -n "$WATCHDOG_PID" ] && kill "$WATCHDOG_PID" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

RUN_IMG="$WORK/run.img"
CAP="$WORK/serial.txt"
cp "$SASI_DISK" "$RUN_IMG"
chmod u+w "$RUN_IMG"
: > "$CAP"
: > "$TRACE_OUT"          # pre-create so append-mode `trace >>` can open it

for d in cfg nvram inp sta snap diff comments; do mkdir -p "$WORK/home/$d"; done

"$VTG" copy "$EXE" "$RUN_IMG:0:\\PROG.EXE" >/dev/null 2>&1 \
	|| { echo "run-victor-wp: failed to inject $EXE" >&2; exit 1; }
if [ -n "${VICTOR_SRC:-}" ]; then
	[ -f "$VICTOR_SRC" ] || { echo "run-victor-wp: VICTOR_SRC not found: $VICTOR_SRC" >&2; exit 2; }
	"$VTG" copy "$VICTOR_SRC" "$RUN_IMG:0:\\PROG.PY" >/dev/null 2>&1 \
		|| { echo "run-victor-wp: failed to inject PROG.PY" >&2; exit 1; }
fi

# Headless debugger: -debug -debugger none -debugscript <file>.  The debugscript
# sets the watchpoint(s) and ends with `go` so the machine runs to the wp.
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
	-debug -debugger none -debugscript "$DBGSCRIPT" \
	-rs232a null_modem -bitbanger "$CAP" \
	>/dev/null 2>&1 &
MAME_PID=$!

WALL_SECS="${VICTOR_WALL_SECS:-$(( RUN_SECS * 4 + 180 ))}"
( sleep "$WALL_SECS"; kill -9 "$MAME_PID" 2>/dev/null ) &
WATCHDOG_PID=$!

wait "$MAME_PID" 2>/dev/null || true
kill "$WATCHDOG_PID" 2>/dev/null || true
WATCHDOG_PID=""
MAME_PID=""

echo "==== serial (between sentinels) ===="
tr -d '\r\032' < "$CAP" | awk '/__V9BEGIN__/{f=1;next} /__V9END__/{f=0} f' || true
echo "==== trace file: $TRACE_OUT ===="
cat "$TRACE_OUT" 2>/dev/null || true
