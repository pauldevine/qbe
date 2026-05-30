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

# --- .COM runtime tests ----------------------------------------------------
#
# Each entry: `<src>:<golden>:<model>` (model is tiny or small for .COM).
# Built via tools/build-com-test.sh --model=<model>, run headlessly under
# DOSBox via tools/run-dos-exe.sh.  The .COM libstub has STUB printf/file
# I/O (the real versions live in libstub_to_exe.py and are .EXE-only), so
# .COM probes must output via inline-asm INT 21h AH=40h on top of the
# real _sprintf in libstub.asm.  See minic/dos/examples/tinyprobe.c for
# the pattern.
#
COM_RUNTIME_TESTS=(
	"minic/dos/examples/tinyprobe.c:minic/dos/tests/tinyprobe.golden.txt:tiny"
	"minic/dos/examples/huge_arith_probe.c:minic/dos/tests/huge_arith_probe.golden.txt:tiny"
)

# --- DOS runtime tests -----------------------------------------------------
#
# Each entry: `<src>:<golden>:<model>`.  The probe is built via
# tools/build-example.sh --model=<model>, run headlessly under DOSBox
# via tools/run-dos-exe.sh; CRLF-stripped stdout is diff'd against the
# golden.  Skipped (not failed) when DOSBox is missing.
#
RUNTIME_TESTS=(
	"minic/dos/examples/cstrprobe.c:minic/dos/tests/cstrprobe.golden.txt:compact"
	"minic/dos/examples/compactprobe_extra.c:minic/dos/tests/compactprobe_extra.golden.txt:compact"
	"minic/dos/examples/fnptrprobe.c:minic/dos/tests/fnptrprobe.golden.txt:compact"
	"minic/dos/examples/farretprobe.c:minic/dos/tests/farretprobe.golden.txt:compact"
	"minic/dos/examples/cstrprobe.c:minic/dos/tests/cstrprobe.golden.txt:large"
	"minic/dos/examples/compactprobe_extra.c:minic/dos/tests/compactprobe_extra.golden.txt:large"
	"minic/dos/examples/fnptrprobe.c:minic/dos/tests/fnptrprobe.golden.txt:large"
	"minic/dos/examples/farretprobe.c:minic/dos/tests/farretprobe.golden.txt:large"
	"minic/dos/examples/cstrprobe.c:minic/dos/tests/cstrprobe.golden.txt:huge"
	# compactprobe_extra is intentionally a non-normalising-arith probe
	# (`(char *)0x12345000L + 7 == 12345007`).  Phase B of [[huge-mode-plan]]
	# makes huge mode normalise such CAddr arithmetic — so the same source
	# yields a DIFFERENT (correct under huge) result.  Keep the probe gated
	# on compact/large only; huge_norm_probe below pins the normalised
	# semantics under huge instead.
	"minic/dos/examples/fnptrprobe.c:minic/dos/tests/fnptrprobe.golden.txt:huge"
	"minic/dos/examples/farretprobe.c:minic/dos/tests/farretprobe.golden.txt:huge"
	"minic/dos/examples/huge_norm_probe.c:minic/dos/tests/huge_norm_probe.golden.txt:huge"
	"minic/dos/examples/hugeprobe.c:minic/dos/tests/hugeprobe.golden.txt:huge"
	"minic/dos/examples/mediumprobe.c:minic/dos/tests/mediumprobe.golden.txt:medium"
	"minic/dos/examples/mediumprobe.c:minic/dos/tests/mediumprobe.golden.txt:large"
	"minic/dos/examples/mediumprobe.c:minic/dos/tests/mediumprobe.golden.txt:huge"
	"minic/dos/examples/declgram_probe.c:minic/dos/tests/declgram_probe.golden.txt:medium"
	"minic/dos/examples/ellipsis_probe.c:minic/dos/tests/ellipsis_probe.golden.txt:medium"
	"minic/dos/examples/declgram2_probe.c:minic/dos/tests/declgram2_probe.golden.txt:medium"
	"minic/dos/examples/nested_member_probe.c:minic/dos/tests/nested_member_probe.golden.txt:medium"
	"minic/dos/examples/voidfnptr_probe.c:minic/dos/tests/voidfnptr_probe.golden.txt:medium"
	"minic/dos/examples/arraytypedef_probe.c:minic/dos/tests/arraytypedef_probe.golden.txt:medium"
	"minic/dos/examples/comma_probe.c:minic/dos/tests/comma_probe.golden.txt:medium"
	"minic/dos/examples/stdint_probe.c:minic/dos/tests/stdint_probe.golden.txt:medium"
	"minic/dos/examples/aggregate_init_probe.c:minic/dos/tests/aggregate_init_probe.golden.txt:medium"
	"minic/dos/examples/sret_probe.c:minic/dos/tests/sret_probe.golden.txt:medium"
	"minic/dos/examples/sret_probe.c:minic/dos/tests/sret_probe.golden.txt:large"
	"minic/dos/examples/typedef_shadow_probe.c:minic/dos/tests/typedef_shadow_probe.golden.txt:medium"
	"minic/dos/examples/typedef_shadow_probe.c:minic/dos/tests/typedef_shadow_probe.golden.txt:large"
	"minic/dos/examples/scalar_array_probe.c:minic/dos/tests/scalar_array_probe.golden.txt:medium"
	"minic/dos/examples/scalar_array_probe.c:minic/dos/tests/scalar_array_probe.golden.txt:large"
	"minic/dos/examples/mp_aggregate_probe.c:minic/dos/tests/mp_aggregate_probe.golden.txt:medium"
	"minic/dos/examples/mp_grammar_probe.c:minic/dos/tests/mp_grammar_probe.golden.txt:medium"
	"minic/dos/examples/mp_grammar_probe.c:minic/dos/tests/mp_grammar_probe.golden.txt:large"
	"minic/dos/examples/mp_designated_array_probe.c:minic/dos/tests/mp_designated_array_probe.golden.txt:medium"
	"minic/dos/examples/mp_designated_array_probe.c:minic/dos/tests/mp_designated_array_probe.golden.txt:large"
	"minic/dos/examples/void_ptr_cmp_probe.c:minic/dos/tests/void_ptr_cmp_probe.golden.txt:medium"
	"minic/dos/examples/void_ptr_cmp_probe.c:minic/dos/tests/void_ptr_cmp_probe.golden.txt:large"
	"minic/dos/examples/mathprobe.c:minic/dos/tests/mathprobe.golden.txt:medium"
	"minic/dos/examples/mathprobe.c:minic/dos/tests/mathprobe.golden.txt:large"
	"minic/dos/examples/mathprobe.c:minic/dos/tests/mathprobe.golden.txt:huge"
	"minic/dos/examples/dosapi_probe.c:minic/dos/tests/dosapi_probe.golden.txt:medium"
	"minic/dos/examples/dosapi_probe.c:minic/dos/tests/dosapi_probe.golden.txt:large"
	"minic/dos/examples/dosapi_probe.c:minic/dos/tests/dosapi_probe.golden.txt:huge"
	"minic/dos/examples/dos_far_probe.c:minic/dos/tests/dos_far_probe.golden.txt:large"
	"minic/dos/examples/dos_far_probe.c:minic/dos/tests/dos_far_probe.golden.txt:huge"
	"minic/dos/examples/stdio_far_probe.c:minic/dos/tests/stdio_far_probe.golden.txt:compact"
	"minic/dos/examples/stdio_far_probe.c:minic/dos/tests/stdio_far_probe.golden.txt:large"
	"minic/dos/examples/stdio_far_probe.c:minic/dos/tests/stdio_far_probe.golden.txt:huge"
	"minic/dos/examples/caddr_arith_probe.c:minic/dos/tests/caddr_arith_probe.golden.txt:compact"
	"minic/dos/examples/caddr_arith_probe.c:minic/dos/tests/caddr_arith_probe.golden.txt:large"
	"minic/dos/examples/caddr_arith_probe.c:minic/dos/tests/caddr_arith_probe.golden.txt:huge"
	"minic/dos/examples/caddr_logop_probe.c:minic/dos/tests/caddr_logop_probe.golden.txt:compact"
	"minic/dos/examples/caddr_logop_probe.c:minic/dos/tests/caddr_logop_probe.golden.txt:large"
	"minic/dos/examples/caddr_logop_probe.c:minic/dos/tests/caddr_logop_probe.golden.txt:huge"
	"minic/dos/examples/caddr_cmp_probe.c:minic/dos/tests/caddr_cmp_probe.golden.txt:compact"
	"minic/dos/examples/caddr_cmp_probe.c:minic/dos/tests/caddr_cmp_probe.golden.txt:large"
	"minic/dos/examples/caddr_cmp_probe.c:minic/dos/tests/caddr_cmp_probe.golden.txt:huge"
	"minic/dos/examples/caddr_div_probe.c:minic/dos/tests/caddr_div_probe.golden.txt:compact"
	"minic/dos/examples/caddr_div_probe.c:minic/dos/tests/caddr_div_probe.golden.txt:large"
	"minic/dos/examples/caddr_div_probe.c:minic/dos/tests/caddr_div_probe.golden.txt:huge"
	"minic/dos/examples/kl_shift_probe.c:minic/dos/tests/kl_shift_probe.golden.txt:compact"
	"minic/dos/examples/phase_bprime_probe.c:minic/dos/tests/phase_bprime_probe.golden.txt:compact"
	"minic/dos/examples/phase_bprime_probe.c:minic/dos/tests/phase_bprime_probe.golden.txt:large"
	"minic/dos/examples/phase_bprime_probe.c:minic/dos/tests/phase_bprime_probe.golden.txt:huge"
	"minic/dos/examples/huge_stack_arith_probe.c:minic/dos/tests/huge_stack_arith_probe.golden.txt:compact"
	"minic/dos/examples/huge_stack_arith_probe.c:minic/dos/tests/huge_stack_arith_probe.golden.txt:large"
	"minic/dos/examples/huge_stack_arith_probe.c:minic/dos/tests/huge_stack_arith_probe.golden.txt:huge"
	"minic/dos/examples/storefl_probe.c:minic/dos/tests/storefl_probe.golden.txt:compact"
	"minic/dos/examples/storefl_probe.c:minic/dos/tests/storefl_probe.golden.txt:large"
	"minic/dos/examples/storefl_probe.c:minic/dos/tests/storefl_probe.golden.txt:huge"
	"minic/dos/examples/loadfb_alias_probe.c:minic/dos/tests/loadfb_alias_probe.golden.txt:compact"
	"minic/dos/examples/loadfb_alias_probe.c:minic/dos/tests/loadfb_alias_probe.golden.txt:large"
	"minic/dos/examples/loadfb_alias_probe.c:minic/dos/tests/loadfb_alias_probe.golden.txt:huge"
	"minic/dos/examples/storefb_cx_probe.c:minic/dos/tests/storefb_cx_probe.golden.txt:compact"
	"minic/dos/examples/storefb_cx_probe.c:minic/dos/tests/storefb_cx_probe.golden.txt:large"
	"minic/dos/examples/storefb_cx_probe.c:minic/dos/tests/storefb_cx_probe.golden.txt:huge"
	"minic/dos/examples/fprintf_far_probe.c:minic/dos/tests/fprintf_far_probe.golden.txt:compact"
	"minic/dos/examples/fprintf_far_probe.c:minic/dos/tests/fprintf_far_probe.golden.txt:large"
	"minic/dos/examples/fprintf_far_probe.c:minic/dos/tests/fprintf_far_probe.golden.txt:huge"
	"minic/dos/examples/getenv_null_probe.c:minic/dos/tests/getenv_null_probe.golden.txt:compact"
	"minic/dos/examples/getenv_null_probe.c:minic/dos/tests/getenv_null_probe.golden.txt:large"
	"minic/dos/examples/getenv_null_probe.c:minic/dos/tests/getenv_null_probe.golden.txt:huge"
	"minic/dos/examples/ftell_null_probe.c:minic/dos/tests/ftell_null_probe.golden.txt:compact"
	"minic/dos/examples/ftell_null_probe.c:minic/dos/tests/ftell_null_probe.golden.txt:large"
	"minic/dos/examples/ftell_null_probe.c:minic/dos/tests/ftell_null_probe.golden.txt:huge"
)

# --- Stevie size budget ----------------------------------------------------
#
# Stevie is the integration testbed: it exercises far-data, file I/O,
# regex, BIOS keyboard, struct globals, static locals, fn-ptr tables,
# and the malloc heap end-to-end through ~10K lines of K&R-era C.
# A successful link with no codegen-bloat regression is itself a
# meaningful smoke test -- many of this project's worst codegen and
# libstub bugs first surfaced as a stevie size jump or link failure.
#
# Budget set with ~1KB headroom over current size (146992 B at
# 2026-05-23 commit 5508961).  Bumping the limit when libstub grows
# is fine; bumping it because codegen got fatter is not.
STEVIE_BUDGET=148000


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

# Build stevie.exe via tools/build-stevie.sh --exe and fail if the
# resulting binary exceeds the size budget.  Captures the build log
# in /tmp/test-dos.out via the run() wrapper.
run_stevie_size() {
	limit="$1"
	exe="$QBE_DIR/build/stevie-orig/stevie.exe"
	"$QBE_DIR/tools/build-stevie.sh" --exe >/dev/null
	if [ ! -f "$exe" ]; then
		echo "stevie.exe not produced by build-stevie.sh --exe" >&2
		return 1
	fi
	size=$(stat -f %z "$exe" 2>/dev/null || stat -c %s "$exe")
	if [ "$size" -gt "$limit" ]; then
		echo "stevie.exe is $size bytes (over budget $limit)" >&2
		return 1
	fi
	echo "stevie.exe = $size bytes (<= $limit)" >&2
}

# Build a probe at the requested memory model and diff its DOSBox stdout
# against a golden.  Two-step: build (fail fast) then run+diff.  Exit 77
# from run-dos-exe.sh propagates so missing-DOSBox shows "skip", not "fail".
run_runtime_probe() {
	src="$1"
	golden="$2"
	model="$3"
	base="$(basename "$src" .c)"
	exe="$QBE_DIR/build/examples/$base/$base.exe"
	"$QBE_DIR/tools/build-example.sh" --model="$model" "$QBE_DIR/$src" >/dev/null
	out="$("$QBE_DIR/tools/run-dos-exe.sh" "$exe")" || return $?
	echo "$out" | diff -u "$QBE_DIR/$golden" - >&2
}

# Same as run_runtime_probe but for .COM via tools/build-com-test.sh.
run_com_runtime_probe() {
	src="$1"
	golden="$2"
	model="$3"
	base="$(basename "$src" .c)"
	com="$QBE_DIR/build/com-test/$base/$base.com"
	"$QBE_DIR/tools/build-com-test.sh" --model="$model" "$QBE_DIR/$src" >/dev/null
	out="$("$QBE_DIR/tools/run-dos-exe.sh" "$com")" || return $?
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

for entry in "${COM_RUNTIME_TESTS[@]}"; do
	src="${entry%%:*}"
	rest="${entry#*:}"
	golden="${rest%%:*}"
	model="${rest##*:}"
	desc="$model runtime ($(basename "$src" .c))"
	run "$desc" run_com_runtime_probe "$src" "$golden" "$model"
done

for entry in "${RUNTIME_TESTS[@]}"; do
	src="${entry%%:*}"
	rest="${entry#*:}"
	golden="${rest%%:*}"
	model="${rest##*:}"
	desc="$model runtime ($(basename "$src" .c))"
	run "$desc" run_runtime_probe "$src" "$golden" "$model"
done

run "stevie.exe size (<= ${STEVIE_BUDGET}B)" \
	run_stevie_size "$STEVIE_BUDGET"

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
