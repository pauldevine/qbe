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

# --- .COM smoke tests ------------------------------------------------------
#
# Each entry: a source file in minic/dos/tests/ + a size budget (bytes).
# Budgets are set with a small headroom over the current size so any
# real regression in libstub size or codegen bloat fails the gate.
# Bumping a limit is a code smell -- if a test needs more room on
# purpose, split the heavy bits into a separate .EXE test instead.
#
SMOKE_TESTS=(
	"com_smoke.c:4096"      # printf/sprintf, near-ptr arith, INT 21h
	"long_math.c:6144"      # Kl codegen + libstub divmod32 helpers
	"fileio.c:4096"         # fopen/fputs/fread/fclose roundtrip
)

# --- Compact-model runtime tests -------------------------------------------
#
# Each entry: source path + golden output path.  The probe is built at
# --model=compact via tools/build-example.sh, then run headlessly under
# DOSBox via tools/run-dos-exe.sh; output (CRLF stripped) is diff'd
# against the golden file.  Skipped (not failed) when DOSBox is missing.
#
RUNTIME_TESTS=(
	"minic/dos/examples/cstrprobe.c:minic/dos/tests/cstrprobe.golden.txt"
)

pass=0
fail=0
skip=0

run() {
	desc="$1"; shift
	printf '%-44s' "$desc"
	if "$@" >/tmp/test-dos.out 2>&1; then
		echo "[ok]"
		pass=$((pass + 1))
	else
		rc=$?
		if [ "$rc" -eq 77 ]; then
			echo "[skip]"
			sed 's/^/    /' /tmp/test-dos.out
			skip=$((skip + 1))
		else
			echo "[FAIL]"
			sed 's/^/    /' /tmp/test-dos.out
			fail=$((fail + 1))
		fi
	fi
}

# Build a compact-model probe and diff its DOSBox stdout against a golden.
# Two-step: build (fail fast) then run+diff.  We propagate exit 77 from
# run-dos-exe.sh so missing-DOSBox shows up as "skip", not "fail".
run_compact_probe() {
	src="$1"
	golden="$2"
	base="$(basename "$src" .c)"
	exe="$QBE_DIR/build/examples/$base/$base.exe"
	"$QBE_DIR/tools/build-example.sh" --model=compact "$QBE_DIR/$src" >/dev/null
	out="$("$QBE_DIR/tools/run-dos-exe.sh" "$exe")" || return $?
	echo "$out" | diff -u "$QBE_DIR/$golden" - >&2
}

# Ensure qbe/minic are built before anything else.
run "build qbe + minic" \
	make -C "$QBE_DIR" -s qbe minic/minic

for entry in "${SMOKE_TESTS[@]}"; do
	src="${entry%%:*}"
	limit="${entry##*:}"
	run ".COM smoke ($src, <= ${limit}B)" \
		"$QBE_DIR/tools/build-com-test.sh" \
		"$QBE_DIR/minic/dos/tests/$src" \
		"$limit"
done

for entry in "${RUNTIME_TESTS[@]}"; do
	src="${entry%%:*}"
	golden="${entry##*:}"
	desc="compact runtime ($(basename "$src" .c))"
	run "$desc" run_compact_probe "$src" "$golden"
done

echo
total=$((pass + fail))
if [ "$fail" -eq 0 ]; then
	if [ "$skip" -gt 0 ]; then
		echo "DOS pipeline: $pass/$total ok ($skip skipped)"
	else
		echo "DOS pipeline: $pass/$total ok"
	fi
	exit 0
else
	echo "DOS pipeline: $fail of $total tests FAILED ($skip skipped)"
	exit 1
fi
