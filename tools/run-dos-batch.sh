#!/bin/bash
# Run MANY built DOS programs under a SINGLE DOSBox launch and collect each
# one's redirected stdout (CRLF-stripped) into a per-test host file.
#
# DOSBox boot (~2-4 s on macOS via `open`) dominated tools/test-dos.sh when
# every runtime probe got its own emulator — the probes themselves finish in
# milliseconds.  Batching all of them into one boot turns ~185 launches into
# one.
#
# Usage:  tools/run-dos-batch.sh <manifest.tsv> [timeout_sec]
#
#   manifest.tsv: one program per line, two or three TAB-separated fields:
#       <path/to/program.exe-or-.com> <TAB> <host-output-file> [<TAB> <host-stdin-file>]
#   Each program is copied into a fresh staging directory as Tnnnn.EXE/.COM
#   (8.3-safe) and run with `Tnnnn.EXE > Tnnnn.TXT`; on completion the
#   CRLF-stripped TXT is written to the host-output-file.  A program that
#   produced no TXT (hang/crash aborted the batch) gets NO output file —
#   callers treat a missing file as failure.
#   An optional third field is a host file fed to the program as DOS stdin
#   (`Tnnnn.EXE < Tnnnn.IN > Tnnnn.TXT`); staged 8.3-safe as Tnnnn.IN.  Used
#   by the keyboard tests (stdin_test/scanf_test) that read handle 0 via INT
#   21h AH=3Fh — a DOS redirect makes that deterministic (no echo).
#
#   timeout_sec: host-side watchdog (default 30 + 1s/program).  The DOS side
#   writes DONE.TXT after the last program; if it doesn't appear in time the
#   DOSBox instance is killed and exit status is 1.  Outputs of programs that
#   completed before the hang are still delivered (each redirect closes its
#   file when its program exits).
#
# Exit status: 0 all programs ran to completion; 1 watchdog killed DOSBox;
# 77 DOSBox not found (skip convention, mirrors run-dos-exe.sh).
#
# DOSBox lookup order (same as run-dos-exe.sh):
#   1. $DOSBOX               — explicit override (full path)
#   2. `command -v dosbox`   — Linux / Homebrew default
#   3. /Applications/dosbox.app/Contents/MacOS/DOSBox — macOS .app bundle
#
# macOS launch particulars: `open -a <binary> -g -j -n --args …` keeps the
# emulator from stealing focus (see run-dos-exe.sh for why the *binary*, not
# the bundle).  We deliberately do NOT use `open -W`: completion is detected
# by polling the DONE.TXT sentinel so a hung program can be timed out — `-W`
# would block forever (the old per-test runner's failure mode on bss_test).

set -eu

MANIFEST="${1:-}"
if [ -z "$MANIFEST" ] || [ ! -f "$MANIFEST" ]; then
	echo "usage: $0 <manifest.tsv> [timeout_sec]" >&2
	exit 2
fi

# Parse the manifest (bash-3.2-safe indexed arrays).  Third field (host
# stdin file) is optional; absent → empty.
EXES=()
OUTS=()
INS=()
n=0
while IFS=$'\t' read -r exe out stdin; do
	[ -n "$exe" ] || continue
	if [ ! -f "$exe" ]; then
		echo "run-dos-batch: missing program: $exe" >&2
		exit 2
	fi
	if [ -n "$stdin" ] && [ ! -f "$stdin" ]; then
		echo "run-dos-batch: missing stdin file: $stdin" >&2
		exit 2
	fi
	EXES[$n]="$exe"
	OUTS[$n]="$out"
	INS[$n]="$stdin"
	n=$((n + 1))
done < "$MANIFEST"
if [ "$n" -eq 0 ]; then
	echo "run-dos-batch: empty manifest" >&2
	exit 2
fi

TIMEOUT_SEC="${2:-$((30 + n))}"

# Locate DOSBox.
DOSBOX_APP=""
if [ -n "${DOSBOX:-}" ] && [ -x "$DOSBOX" ]; then
	DOSBOX_BIN="$DOSBOX"
elif command -v dosbox >/dev/null 2>&1; then
	DOSBOX_BIN="$(command -v dosbox)"
elif [ -x /Applications/dosbox.app/Contents/MacOS/DOSBox ]; then
	DOSBOX_BIN=/Applications/dosbox.app/Contents/MacOS/DOSBox
else
	echo "run-dos-batch: DOSBox not found (set \$DOSBOX or install dosbox)" >&2
	exit 77
fi
case "$DOSBOX_BIN" in
	*.app/Contents/MacOS/*) DOSBOX_APP="${DOSBOX_BIN%.app/Contents/MacOS/*}.app" ;;
esac

STAGE="$(mktemp -d -t run-dos-batch.XXXXXX)"
CONF="$STAGE/dosbox.conf"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

# Stage every program under an 8.3-safe name.  The per-test command lines
# go in a RUNALL.BAT on the mounted drive, NOT in [autoexec]: DOSBox's
# autoexec section has a small fixed internal buffer (~4 KB) and silently
# drops the lot when hundreds of lines exceed it (the whole batch produced
# zero output).  A real batch file has no such limit.
BAT="$STAGE/RUNALL.BAT"
printf 'echo off\r\n' > "$BAT"
i=0
while [ "$i" -lt "$n" ]; do
	exe="${EXES[$i]}"
	ext="$(echo "${exe##*.}" | tr '[:lower:]' '[:upper:]')"   # EXE or COM
	name="$(printf 'T%04d' "$i")"
	cp "$exe" "$STAGE/$name.$ext"
	redir=""
	if [ -n "${INS[$i]}" ]; then
		cp "${INS[$i]}" "$STAGE/$name.IN"
		redir="< $name.IN "
	fi
	printf '%s.%s %s> %s.TXT\r\n' "$name" "$ext" "$redir" "$name" >> "$BAT"
	i=$((i + 1))
done
printf 'echo done > DONE.TXT\r\n' >> "$BAT"

cat > "$CONF" <<EOF
[autoexec]
mount c "$STAGE"
c:
call RUNALL.BAT
exit
EOF

# Launch (hidden/headless) WITHOUT waiting, then poll the sentinel.
DOSBOX_PID=""
if [ -n "$DOSBOX_APP" ]; then
	open -a "$DOSBOX_BIN" -g -j -n --args -conf "$CONF" -exit >/dev/null 2>&1
else
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		"$DOSBOX_BIN" -conf "$CONF" -exit >/dev/null 2>&1 &
	DOSBOX_PID=$!
fi

ticks=0
limit=$((TIMEOUT_SEC * 5))
while [ ! -f "$STAGE/DONE.TXT" ] && [ "$ticks" -lt "$limit" ]; do
	sleep 0.2
	ticks=$((ticks + 1))
done

rc=0
if [ ! -f "$STAGE/DONE.TXT" ]; then
	# Watchdog: kill the instance we launched ($CONF's path is unique).
	if [ -n "$DOSBOX_PID" ]; then
		kill "$DOSBOX_PID" 2>/dev/null || true
	else
		pkill -f "$CONF" 2>/dev/null || true
	fi
	sleep 0.3
	echo "run-dos-batch: TIMEOUT after ${TIMEOUT_SEC}s — DOSBox killed;" \
	     "a program hung (outputs up to the hang are still delivered)" >&2
	rc=1
fi

# Deliver per-program outputs (CRLF-stripped).  A missing TXT means the
# batch never reached that program — leave its host file absent.
i=0
while [ "$i" -lt "$n" ]; do
	txt="$STAGE/$(printf 'T%04d' "$i").TXT"
	if [ -f "$txt" ]; then
		tr -d '\r' < "$txt" > "${OUTS[$i]}"
	fi
	i=$((i + 1))
done

exit "$rc"
