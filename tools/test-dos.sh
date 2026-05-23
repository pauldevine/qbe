#!/bin/bash
# Regression gate for the i8086 DOS pipelines.
#
# Each entry below names a source file and a size budget (in bytes).
# Each test rebuilds the program through the relevant pipeline (.COM
# for tiny model, .EXE for medium model) and fails if either the build
# fails or the resulting image exceeds its size budget.
#
# Run alongside tools/test.sh for full coverage:
#   tools/test.sh all       # QBE SSA-level tests
#   tools/test-dos.sh       # DOS pipeline + size budgets

set -u

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# --- .COM smoke test -------------------------------------------------------
#
# Bumping the limit is a code smell -- the point of the test is to catch
# regressions that bloat tiny-model output.  If com_smoke.c grows past
# this limit on purpose, split the heavy bits into a separate .EXE test
# instead of raising the budget.
COM_SMOKE_LIMIT=4096

pass=0
fail=0
log() { printf '  %s\n' "$*"; }

run() {
	desc="$1"; shift
	printf '%-40s' "$desc"
	if "$@" >/tmp/test-dos.out 2>&1; then
		echo "[ok]"
		pass=$((pass + 1))
	else
		echo "[FAIL]"
		sed 's/^/    /' /tmp/test-dos.out
		fail=$((fail + 1))
	fi
}

# Ensure qbe/minic are built before anything else.
run "build qbe + minic" \
	make -C "$QBE_DIR" -s qbe minic/minic

run ".COM smoke (com_smoke.c, <= ${COM_SMOKE_LIMIT}B)" \
	"$QBE_DIR/tools/build-com-test.sh" \
	"$QBE_DIR/minic/dos/tests/com_smoke.c" \
	"$COM_SMOKE_LIMIT"

echo
if [ "$fail" -eq 0 ]; then
	echo "DOS pipeline: $pass/$((pass + fail)) ok"
	exit 0
else
	echo "DOS pipeline: $fail of $((pass + fail)) tests FAILED"
	exit 1
fi
