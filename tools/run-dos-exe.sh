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

# DOSBox writes copious chatter to its own stdout/stderr.  We don't need
# any of it for the diff — silence everything except a non-zero exit.
if ! "$DOSBOX_BIN" -conf "$CONF" -exit >/dev/null 2>&1; then
	echo "run-dos-exe: DOSBox exited non-zero" >&2
	exit 1
fi

if [ ! -f "$OUT_PATH" ]; then
	echo "run-dos-exe: $EXE_BASE produced no OUT.TXT" >&2
	exit 1
fi

# Strip CRLF; the golden files use bare LF.
tr -d '\r' < "$OUT_PATH"
