#!/bin/bash
# Run a built DOS .EXE (or .COM) headlessly under DOSBox and stream its
# captured stdout (with CRLF stripped) to our stdout.  Designed for
# `diff -u golden.txt <(tools/run-dos-exe.sh path/to/foo.exe)` style
# regression checks from tools/test-dos.sh.
#
# Usage:  tools/run-dos-exe.sh path/to/foo.exe [maxsecs]
#         tools/run-dos-exe.sh build/examples/cstrprobe/cstrprobe.exe 20
#
# Looks up DOSBox via, in order:
#   1. $DOSBOX               — explicit override (full path)
#   2. `command -v dosbox`   — Linux / Homebrew default
#   3. /Applications/dosbox.app/Contents/MacOS/DOSBox — macOS .app bundle
# Exits 77 (skip-the-test convention) if DOSBox isn't found anywhere —
# test-dos.sh treats that as a "skipped, not failed" outcome.
#
# DOS-side particulars:
#   * 8.3 filename truncation breaks `foo.exe` if the basename is ≥ 9
#     chars (mounting just exposes the LFN as a tilded short).  We copy
#     the .exe into a sibling `RUN.EXE` (always 8.3-safe) before launch.
#   * The .EXE runs with stdout redirected to `OUT.TXT` on the C: mount.
#     DOSBox doesn't propagate the child's stdout to ours, so the
#     redirect file is the only reliable channel.
#   * Cleanup deletes both the renamed copy and OUT.TXT on exit so the
#     next run starts fresh (and stale OUT.TXT from a previous failing
#     run never masquerades as the current invocation's output).

set -eu

EXE="${1:-}"
TIMEOUT_SEC="${2:-15}"

if [ -z "$EXE" ] || [ ! -f "$EXE" ]; then
	echo "usage: $0 <path-to-foo.exe> [timeout_sec]" >&2
	exit 2
fi

# Locate DOSBox.
DOSBOX_APP=""
if [ -n "${DOSBOX:-}" ] && [ -x "$DOSBOX" ]; then
	DOSBOX_BIN="$DOSBOX"
elif command -v dosbox >/dev/null 2>&1; then
	DOSBOX_BIN="$(command -v dosbox)"
elif [ -x /Applications/dosbox.app/Contents/MacOS/DOSBox ]; then
	DOSBOX_BIN=/Applications/dosbox.app/Contents/MacOS/DOSBox
else
	echo "run-dos-exe: DOSBox not found (set \$DOSBOX or install dosbox)" >&2
	exit 77
fi

# If the resolved binary lives inside a macOS .app bundle, remember the bundle
# so we can launch it via `open` (see run_dosbox below).
case "$DOSBOX_BIN" in
	*.app/Contents/MacOS/*) DOSBOX_APP="${DOSBOX_BIN%.app/Contents/MacOS/*}.app" ;;
esac

EXE_DIR="$(cd "$(dirname "$EXE")" && pwd)"
EXE_BASE="$(basename "$EXE")"
SHORT_NAME="RUN.${EXE_BASE##*.}"        # RUN.EXE or RUN.COM
SHORT_PATH="$EXE_DIR/$SHORT_NAME"
OUT_PATH="$EXE_DIR/OUT.TXT"

cleanup() {
	rm -f "$SHORT_PATH" "$OUT_PATH"
}
trap cleanup EXIT

cp "$EXE" "$SHORT_PATH"

CONF="$(mktemp -t run-dos-exe-conf.XXXXXX)"
trap 'rm -f "$CONF"; cleanup' EXIT
cat > "$CONF" <<EOF
[autoexec]
mount c "$EXE_DIR"
c:
$SHORT_NAME > OUT.TXT
exit
EOF

# Launch DOSBox unobtrusively.  DOSBox writes copious chatter to its own
# stdout/stderr; we don't need any of it for the diff, so silence everything
# except a non-zero exit.
#
# On macOS, route through `open` so the emulator never steals foreground focus
# or grabs the keyboard from whatever the user is doing on the same machine:
#   -g  do not bring the application to the foreground
#   -j  launch it hidden
#   -W  wait until it exits (and propagate its exit status)
#   -n  always start a fresh instance (don't reactivate one the user has open)
#   --args  forward the DOSBox flags to the app
# IMPORTANT: point `open -a` at the DOSBox *binary*
# (.../Contents/MacOS/DOSBox), NOT the `.app` bundle — `open -a <bundle>
# --args …` does NOT forward the flags to the emulator (the program never
# runs, so OUT.TXT is never produced).  `open -a <binary> --args …` does.
# (Environment variables do NOT propagate through `open`/launchd, so the SDL
# dummy-video driver can't be used on this path — `-g -j` is what keeps it
# from disturbing the user.)  Output still flows through the in-DOS OUT.TXT
# redirect, which is independent of how the emulator window is presented.
#
# Elsewhere (Linux, or $DOSBOX pointing at a raw binary), exec the binary
# directly with the SDL dummy drivers for a truly headless, windowless run.
run_dosbox() {
	if [ -n "$DOSBOX_APP" ]; then
		open -a "$DOSBOX_BIN" -g -j -W -n --args -conf "$CONF" -exit
	else
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
			"$DOSBOX_BIN" -conf "$CONF" -exit
	fi
}

if ! run_dosbox >/dev/null 2>&1; then
	echo "run-dos-exe: DOSBox exited non-zero" >&2
	exit 1
fi

if [ ! -f "$OUT_PATH" ]; then
	echo "run-dos-exe: $EXE_BASE produced no OUT.TXT" >&2
	exit 1
fi

# Strip CRLF; the golden files use bare LF.
tr -d '\r' < "$OUT_PATH"
