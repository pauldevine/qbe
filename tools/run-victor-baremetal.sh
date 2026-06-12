#!/bin/bash
# Run a RAW BARE-METAL binary (omf_link.py --raw-binary) on the Victor 9000
# under MAME, headlessly, streaming its serial-port-A output (CRLF / 0x1A
# stripped, __V9BEGIN__/__V9END__ sentinel-bracketed) to our stdout.
#
# This is the bare-metal sibling of tools/run-victor-sasi.sh: no DOS, no
# disk.  A MAME Lua autoboot script (the newlibc phase-3 loader pattern)
# pokes the binary into RAM at the load address byte-by-byte, zeroes
# CS/DS/ES/SS, points IP at the load address, and lets it rip.  The image
# head is omf_link's register-setup stub, so the program establishes its
# own SS:SP/DGROUP and prints over the 7201 serial port A, captured via
# `-rs232a null_modem -bitbanger <file>`.
#
# Usage:  tools/run-victor-baremetal.sh path/to/prog.bin [seconds_to_run]
#         tools/run-victor-baremetal.sh build/newlibc-baremetal/hello_bm/hello_bm.bin
#
# RESOURCES (overridable by env):
#   $MAME            MAME binary   (default ~/projects/mame/mame, then PATH)
#   $MAME_ROMS       MAME rompath  (default <mame dir>/roms)
#   $VICTOR_LOAD_ADDR  load address (default 0x3000; must match the link)
#
# Exits 77 (skip) if MAME or its roms are missing.

set -eu

BIN="${1:-}"
RUN_SECS="${2:-${VICTOR_RUN_SECS:-20}}"
LOAD_ADDR="${VICTOR_LOAD_ADDR:-0x3000}"

if [ -z "$BIN" ] || [ ! -f "$BIN" ]; then
	echo "usage: $0 <path-to-prog.bin> [seconds_to_run]" >&2
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
	echo "run-victor-baremetal: MAME not found (set \$MAME)" >&2
	exit 77
fi
MAME_DIR="$(cd "$(dirname "$MAME_BIN")" && pwd)"
MAME_ROMS="${MAME_ROMS:-$MAME_DIR/roms}"
if [ ! -d "$MAME_ROMS" ]; then
	echo "run-victor-baremetal: MAME rompath not found: $MAME_ROMS (set \$MAME_ROMS)" >&2
	exit 77
fi

# --- Scratch workspace --------------------------------------------------
WORK="$(mktemp -d -t run-victor-baremetal.XXXXXX)"
# Same orphaned-emulator protections as run-victor-sasi.sh: MAME ignores
# SIGTERM in its -nothrottle loop, so track its pid + a wall-clock watchdog
# and SIGKILL from the EXIT/INT/TERM trap.
MAME_PID=""
WATCHDOG_PID=""
cleanup() {
	[ -n "$MAME_PID" ] && kill -9 "$MAME_PID" 2>/dev/null || true
	retire_watchdog
	rm -rf "$WORK"
}
retire_watchdog() {
	if [ -n "$WATCHDOG_PID" ]; then
		pkill -P "$WATCHDOG_PID" 2>/dev/null || true
		kill "$WATCHDOG_PID" 2>/dev/null || true
	fi
	WATCHDOG_PID=""
}
trap cleanup EXIT INT TERM

CAP="$WORK/serial.txt"
: > "$CAP"
for d in cfg nvram inp sta snap diff comments; do mkdir -p "$WORK/home/$d"; done

# --- Lua autoboot loader (the newlibc phase-3 pattern) -------------------
BIN_ABS="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
LUA="$WORK/load.lua"
cat > "$LUA" <<EOF
local binary_path = '$BIN_ABS'
local load_addr = $LOAD_ADDR
local run_seconds = $RUN_SECS

local function read_file(path)
    local file, err = io.open(path, 'rb')
    if not file then
        error('failed to open binary: ' .. tostring(err))
    end
    local data = file:read('*all')
    file:close()
    return data
end

local function set_reg(cpu, name, value)
    local reg = cpu.state[name]
    if not reg then
        error('missing CPU register: ' .. name)
    end
    reg.value = value
end

local cpu = manager.machine.devices[':maincpu'] or manager.machine.devices[':8l']
if not cpu then
    error('missing Victor main CPU device')
end
local mem = cpu.spaces['program']
if not mem then
    error('missing main CPU program space')
end

local data = read_file(binary_path)
for i = 1, #data do
    mem:write_u8(load_addr + i - 1, data:byte(i))
end

set_reg(cpu, 'CS', 0)
set_reg(cpu, 'DS', 0)
set_reg(cpu, 'ES', 0)
set_reg(cpu, 'SS', 0)
set_reg(cpu, 'SP', 0xF0EC)
set_reg(cpu, 'IP', load_addr)

emu.wait(run_seconds)
manager.machine:exit()
EOF

# --- Run MAME headless (no disk, no floppy — pure RAM load) --------------
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
	-autoboot_script "$LUA" \
	-autoboot_delay 0 \
	-video none -sound none -nothrottle -skip_gameinfo \
	-seconds_to_run "$(( RUN_SECS + 30 ))" \
	-rs232a null_modem -bitbanger "$CAP" \
	>/dev/null 2>&1 &
MAME_PID=$!

WALL_SECS="${VICTOR_WALL_SECS:-$(( RUN_SECS * 4 + 120 ))}"
( sleep "$WALL_SECS"; kill -9 "$MAME_PID" 2>/dev/null ) &
WATCHDOG_PID=$!

wait "$MAME_PID" 2>/dev/null || true
retire_watchdog
MAME_PID=""

if [ ! -s "$CAP" ]; then
	echo "run-victor-baremetal: serial capture is empty (load failed or no output)" >&2
	exit 1
fi

trimmed="$(tr -d '\r\032' < "$CAP" \
	| awk '/__V9BEGIN__/{f=1;next} /__V9END__/{f=0} f')"

if ! grep -q '__V9BEGIN__' "$CAP"; then
	echo "run-victor-baremetal: never saw __V9BEGIN__ sentinel in serial capture" >&2
	echo "---- raw capture ----" >&2
	tr -d '\r\032' < "$CAP" >&2
	echo "---------------------" >&2
	exit 1
fi

printf '%s\n' "$trimmed"
