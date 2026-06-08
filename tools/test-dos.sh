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
	"minic/dos/examples/kl_ternary_mul_probe.c:minic/dos/tests/kl_ternary_mul_probe.golden.txt:medium"
	"minic/dos/examples/kl_ternary_mul_probe.c:minic/dos/tests/kl_ternary_mul_probe.golden.txt:compact"
	"minic/dos/examples/kl_ternary_mul_probe.c:minic/dos/tests/kl_ternary_mul_probe.golden.txt:large"
	# Single-precision software float (no 8087): native `float` lowers to
	# _sf_* helper calls.  Medium-only — far-data float access (compact/large)
	# truncates through minic's loadfw/storefw (a deferred follow-up; see
	# softfloat_probe.c + NEXT_SESSION.md).
	"minic/dos/examples/softfloat_probe.c:minic/dos/tests/softfloat_probe.golden.txt:medium"
	"minic/dos/examples/float_literal_probe.c:minic/dos/tests/float_literal_probe.golden.txt:medium"
	"minic/dos/examples/struct_copy_probe.c:minic/dos/tests/struct_copy_probe.golden.txt:medium"
	"minic/dos/examples/static_lptr_return_probe.c:minic/dos/tests/static_lptr_return_probe.golden.txt:medium"
	"minic/dos/examples/lptr_range_probe.c:minic/dos/tests/lptr_range_probe.golden.txt:medium"
	"minic/dos/examples/operator_pending_probe.c:minic/dos/tests/operator_pending_probe.golden.txt:medium"
	"minic/dos/examples/divreg_probe.c:minic/dos/tests/divreg_probe.golden.txt:medium"
	"minic/dos/examples/shlconst_probe.c:minic/dos/tests/shlconst_probe.golden.txt:medium"
	"minic/dos/examples/shlconst_probe.c:minic/dos/tests/shlconst_probe.golden.txt:compact"
	"minic/dos/examples/sigencode_probe.c:minic/dos/tests/sigencode_probe.golden.txt:medium"
	"minic/dos/examples/sigencode_probe.c:minic/dos/tests/sigencode_probe.golden.txt:compact"
	"minic/dos/examples/uchar_widen_probe.c:minic/dos/tests/uchar_widen_probe.golden.txt:medium"
	"minic/dos/examples/uchar_widen_probe.c:minic/dos/tests/uchar_widen_probe.golden.txt:compact"
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
	"minic/dos/examples/structarg_probe.c:minic/dos/tests/structarg_probe.golden.txt:compact"
	"minic/dos/examples/structarg_probe.c:minic/dos/tests/structarg_probe.golden.txt:large"
	"minic/dos/examples/bitfield_far_probe.c:minic/dos/tests/bitfield_far_probe.golden.txt:compact"
	"minic/dos/examples/bitfield_far_probe.c:minic/dos/tests/bitfield_far_probe.golden.txt:large"
	"minic/dos/examples/fnptr_klret_probe.c:minic/dos/tests/fnptr_klret_probe.golden.txt:compact"
	"minic/dos/examples/fnptr_klret_probe.c:minic/dos/tests/fnptr_klret_probe.golden.txt:large"
	"minic/dos/examples/argwiden_probe.c:minic/dos/tests/argwiden_probe.golden.txt:medium"
	"minic/dos/examples/argwiden_probe.c:minic/dos/tests/argwiden_probe.golden.txt:compact"
	"minic/dos/examples/argwiden_probe.c:minic/dos/tests/argwiden_probe.golden.txt:large"
	"minic/dos/examples/nullarg_probe.c:minic/dos/tests/nullarg_probe.golden.txt:medium"
	"minic/dos/examples/nullarg_probe.c:minic/dos/tests/nullarg_probe.golden.txt:compact"
	"minic/dos/examples/nullarg_probe.c:minic/dos/tests/nullarg_probe.golden.txt:large"
	"minic/dos/examples/argmix_probe.c:minic/dos/tests/argmix_probe.golden.txt:medium"
	"minic/dos/examples/argmix_probe.c:minic/dos/tests/argmix_probe.golden.txt:compact"
	"minic/dos/examples/argmix_probe.c:minic/dos/tests/argmix_probe.golden.txt:large"
	"minic/dos/examples/typedef_fnptr_probe.c:minic/dos/tests/typedef_fnptr_probe.golden.txt:medium"
	"minic/dos/examples/typedef_fnptr_probe.c:minic/dos/tests/typedef_fnptr_probe.golden.txt:compact"
	"minic/dos/examples/typedef_fnptr_probe.c:minic/dos/tests/typedef_fnptr_probe.golden.txt:large"
	"minic/dos/examples/uwiden_cast_probe.c:minic/dos/tests/uwiden_cast_probe.golden.txt:medium"
	"minic/dos/examples/uwiden_cast_probe.c:minic/dos/tests/uwiden_cast_probe.golden.txt:compact"
	"minic/dos/examples/uwiden_cast_probe.c:minic/dos/tests/uwiden_cast_probe.golden.txt:large"
	"minic/dos/examples/vararg_probe.c:minic/dos/tests/vararg_probe.golden.txt:medium"
	"minic/dos/examples/vararg_probe.c:minic/dos/tests/vararg_probe.golden.txt:compact"
	"minic/dos/examples/vararg_probe.c:minic/dos/tests/vararg_probe.golden.txt:large"
	"minic/dos/examples/local_zeroinit_probe.c:minic/dos/tests/local_zeroinit_probe.golden.txt:medium"
	"minic/dos/examples/local_zeroinit_probe.c:minic/dos/tests/local_zeroinit_probe.golden.txt:compact"
	"minic/dos/examples/local_zeroinit_probe.c:minic/dos/tests/local_zeroinit_probe.golden.txt:large"
	"minic/dos/examples/slotarray_probe.c:minic/dos/tests/slotarray_probe.golden.txt:compact"
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
	"minic/dos/examples/extern_struct_probe.c:minic/dos/tests/extern_struct_probe.golden.txt:medium"
	"minic/dos/examples/extern_struct_probe.c:minic/dos/tests/extern_struct_probe.golden.txt:large"
	"minic/dos/examples/block_static_probe.c:minic/dos/tests/block_static_probe.golden.txt:medium"
	"minic/dos/examples/for_multidecl_probe.c:minic/dos/tests/for_multidecl_probe.golden.txt:medium"
	"minic/dos/examples/for_multidecl_probe.c:minic/dos/tests/for_multidecl_probe.golden.txt:large"
	"minic/dos/examples/string_array_probe.c:minic/dos/tests/string_array_probe.golden.txt:medium"
	"minic/dos/examples/string_array_probe.c:minic/dos/tests/string_array_probe.golden.txt:large"
	"minic/dos/examples/tentative_def_probe.c:minic/dos/tests/tentative_def_probe.golden.txt:medium"
	"minic/dos/examples/tentative_def_probe.c:minic/dos/tests/tentative_def_probe.golden.txt:large"
	"minic/dos/examples/const_init_probe.c:minic/dos/tests/const_init_probe.golden.txt:medium"
	"minic/dos/examples/const_init_probe.c:minic/dos/tests/const_init_probe.golden.txt:large"
	"minic/dos/examples/for_comma_inc_probe.c:minic/dos/tests/for_comma_inc_probe.golden.txt:medium"
	"minic/dos/examples/for_comma_inc_probe.c:minic/dos/tests/for_comma_inc_probe.golden.txt:large"
	"minic/dos/examples/block_scope_probe.c:minic/dos/tests/block_scope_probe.golden.txt:medium"
	"minic/dos/examples/block_scope_probe.c:minic/dos/tests/block_scope_probe.golden.txt:large"
	"minic/dos/examples/for_multiscalar_probe.c:minic/dos/tests/for_multiscalar_probe.golden.txt:medium"
	"minic/dos/examples/for_multiscalar_probe.c:minic/dos/tests/for_multiscalar_probe.golden.txt:large"
	"minic/dos/examples/for_init_scope_probe.c:minic/dos/tests/for_init_scope_probe.golden.txt:medium"
	"minic/dos/examples/for_init_scope_probe.c:minic/dos/tests/for_init_scope_probe.golden.txt:large"
	"minic/dos/examples/anon_aggr_probe.c:minic/dos/tests/anon_aggr_probe.golden.txt:medium"
	"minic/dos/examples/anon_aggr_probe.c:minic/dos/tests/anon_aggr_probe.golden.txt:large"
	"minic/dos/examples/nested_clit_probe.c:minic/dos/tests/nested_clit_probe.golden.txt:medium"
	"minic/dos/examples/nested_clit_probe.c:minic/dos/tests/nested_clit_probe.golden.txt:large"
	"minic/dos/examples/fnptr_cast_probe.c:minic/dos/tests/fnptr_cast_probe.golden.txt:medium"
	"minic/dos/examples/fnptr_cast_probe.c:minic/dos/tests/fnptr_cast_probe.golden.txt:large"
	"minic/dos/examples/extern_array_expr_probe.c:minic/dos/tests/extern_array_expr_probe.golden.txt:medium"
	"minic/dos/examples/extern_array_expr_probe.c:minic/dos/tests/extern_array_expr_probe.golden.txt:large"
	"minic/dos/examples/codegen_term_probe.c:minic/dos/tests/codegen_term_probe.golden.txt:medium"
	"minic/dos/examples/codegen_term_probe.c:minic/dos/tests/codegen_term_probe.golden.txt:large"
	"minic/dos/examples/dup_label_probe.c:minic/dos/tests/dup_label_probe.golden.txt:medium"
	"minic/dos/examples/dup_label_probe.c:minic/dos/tests/dup_label_probe.golden.txt:large"
	"minic/dos/examples/caddr_slot_probe.c:minic/dos/tests/caddr_slot_probe.golden.txt:medium"
	"minic/dos/examples/uint_widen_cmp_probe.c:minic/dos/tests/uint_widen_cmp_probe.golden.txt:compact"
	"minic/dos/examples/mp_str_int_probe.c:minic/dos/tests/mp_str_int_probe.golden.txt:compact"
	"minic/dos/examples/local_array_memcpy_probe.c:minic/dos/tests/local_array_memcpy_probe.golden.txt:compact"
	"minic/dos/examples/static_linkage_probe.c:minic/dos/tests/static_linkage_probe.golden.txt:medium"
	"minic/dos/examples/static_linkage_probe.c:minic/dos/tests/static_linkage_probe.golden.txt:large"
	"minic/dos/examples/setjmp_probe.c:minic/dos/tests/setjmp_probe.golden.txt:medium"
	"minic/dos/examples/setjmp_probe.c:minic/dos/tests/setjmp_probe.golden.txt:compact"
	"minic/dos/examples/setjmp_probe.c:minic/dos/tests/setjmp_probe.golden.txt:large"
	"minic/dos/examples/fardata_probe.c:minic/dos/tests/fardata_probe.golden.txt:compact"
	"minic/dos/examples/fardata_probe.c:minic/dos/tests/fardata_probe.golden.txt:large"
	"minic/dos/examples/fardata_probe.c:minic/dos/tests/fardata_probe.golden.txt:huge"
	"minic/dos/examples/farglobal_probe.c:minic/dos/tests/farglobal_probe.golden.txt:compact"
	"minic/dos/examples/farglobal_probe.c:minic/dos/tests/farglobal_probe.golden.txt:large"
	"minic/dos/examples/farglobal_probe.c:minic/dos/tests/farglobal_probe.golden.txt:huge"
	"minic/dos/examples/farlocal_probe.c:minic/dos/tests/farlocal_probe.golden.txt:compact"
	"minic/dos/examples/farlocal_probe.c:minic/dos/tests/farlocal_probe.golden.txt:large"
	"minic/dos/examples/farlocal_probe.c:minic/dos/tests/farlocal_probe.golden.txt:huge"
	"minic/dos/examples/farstruct_ptr_probe.c:minic/dos/tests/farstruct_ptr_probe.golden.txt:compact"
	"minic/dos/examples/farstruct_ptr_probe.c:minic/dos/tests/farstruct_ptr_probe.golden.txt:large"
	"minic/dos/examples/longconst_probe.c:minic/dos/tests/longconst_probe.golden.txt:medium"
	"minic/dos/examples/longconst_probe.c:minic/dos/tests/longconst_probe.golden.txt:large"
	"minic/dos/examples/shift_fold_probe.c:minic/dos/tests/shift_fold_probe.golden.txt:medium"
	"minic/dos/examples/shift_fold_probe.c:minic/dos/tests/shift_fold_probe.golden.txt:compact"
	"minic/dos/examples/setjmp_clobber_probe.c:minic/dos/tests/setjmp_clobber_probe.golden.txt:medium"
	"minic/dos/examples/setjmp_clobber_probe.c:minic/dos/tests/setjmp_clobber_probe.golden.txt:compact"
	"minic/dos/examples/local_typedef_probe.c:minic/dos/tests/local_typedef_probe.golden.txt:medium"
	"minic/dos/examples/local_typedef_probe.c:minic/dos/tests/local_typedef_probe.golden.txt:large"
	"minic/dos/examples/array_designate_probe.c:minic/dos/tests/array_designate_probe.golden.txt:medium"
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
	"minic/dos/examples/caddr_store_probe.c:minic/dos/tests/caddr_store_probe.golden.txt:compact"
	"minic/dos/examples/caddr_store_probe.c:minic/dos/tests/caddr_store_probe.golden.txt:large"
	"minic/dos/examples/caddr_store_probe.c:minic/dos/tests/caddr_store_probe.golden.txt:huge"
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
	"minic/dos/examples/oo_designate_probe.c:minic/dos/tests/oo_designate_probe.golden.txt:compact"
	"minic/dos/examples/oo_designate_probe.c:minic/dos/tests/oo_designate_probe.golden.txt:large"
	"minic/dos/examples/objalign_probe.c:minic/dos/tests/objalign_probe.golden.txt:medium"
	"minic/dos/examples/objalign_probe.c:minic/dos/tests/objalign_probe.golden.txt:compact"
	"minic/dos/examples/objalign_probe.c:minic/dos/tests/objalign_probe.golden.txt:large"
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
	# Far-static-data probes opt into the additional-far-segment placement
	# (statics outside DGROUP).  Gated by basename so only these exercise it
	# until far-global direct access is complete (see NEXT_SESSION.md).
	farstatic=0
	case "$base" in fardata_probe|farglobal_probe|farstruct_ptr_probe|slotarray_probe) farstatic=1 ;; esac
	# Soft-float probes link the single-precision soft-float helper library.
	sfflag=""
	case "$base" in softfloat_probe|float_literal_probe) sfflag="--softfloat" ;; esac
	QBE_FAR_STATIC_DATA="$farstatic" \
		"$QBE_DIR/tools/build-example.sh" --model="$model" $sfflag "$QBE_DIR/$src" >/dev/null
	out="$("$QBE_DIR/tools/run-dos-exe.sh" "$exe")" || return $?
	echo "$out" | diff -u "$QBE_DIR/$golden" - >&2
}

# Runtime regression for OMF target-frame fixups into grouped near data.
# This needs two translation units: the referenced _BSS globals live in one
# object, while the data guard that old BSS-relative fixups corrupt lives in
# another.  The single-source runtime table cannot express that shape.
run_grouped_bss_probe() {
	src="$QBE_DIR/minic/dos/examples/grouped_bss_probe.c"
	def="$QBE_DIR/minic/dos/examples/grouped_bss_def.c"
	golden="$QBE_DIR/minic/dos/tests/grouped_bss_probe.golden.txt"
	exe="$QBE_DIR/build/examples/grouped_bss_probe/grouped_bss_probe.exe"
	"$QBE_DIR/tools/build-example.sh" --model=medium "$src" "$def" >/dev/null
	out="$("$QBE_DIR/tools/run-dos-exe.sh" "$exe")" || return $?
	echo "$out" | diff -u "$golden" - >&2
}

# Compile-time probe for C `volatile` on named locals.  volatile is a codegen
# property (not runtime-observable in a self-contained program), so this
# inspects the emitted i8086 asm: volf() must KEEP its volatile loads/stores
# and nonvolf() must fold them away.  No DOSBox needed.  Bug-loud against a
# QBE without the volatile gates (volf would fold to `mov ax, 20`).
run_volatile_asm_probe() {
	src="$QBE_DIR/minic/dos/examples/volatile_probe.c"
	ssa=/tmp/volatile_probe.ssa
	asm=/tmp/volatile_probe.asm
	"$QBE_DIR/minic/minic" -m medium < "$src" > "$ssa" 2>/tmp/volatile_probe.err \
		|| { echo "minic failed:"; cat /tmp/volatile_probe.err; return 1; }
	"$QBE_DIR/qbe" -t i8086 -m medium "$ssa" > "$asm" 2>/tmp/volatile_probe.err \
		|| { echo "qbe failed:"; cat /tmp/volatile_probe.err; return 1; }
	# Slice each function body (between its `_name:` label and the next
	# top-level label) and count word-sized memory accesses.
	voln=$(awk '/^_volf:/{p=1;next} /^_[A-Za-z]/{p=0} /^\.section/{p=0} p' "$asm" | grep -c 'word \[')
	nonvoln=$(awk '/^_nonvolf:/{p=1;next} /^_[A-Za-z]/{p=0} /^\.section/{p=0} p' "$asm" | grep -c 'word \[')
	if [ "$voln" -lt 4 ]; then
		echo "volf kept only $voln memory ops (want >=4: 2 stores + 2 loads) — volatile not honored" >&2
		return 1
	fi
	if [ "$nonvoln" -ne 0 ]; then
		echo "nonvolf kept $nonvoln memory ops (should fold to a constant)" >&2
		return 1
	fi
	echo "volf=$voln mem ops (kept), nonvolf=$nonvoln (folded)" >&2
}

# Compile-time probe for `volatile` on file-scope GLOBALS (§3j extend phase).
# A global has no alloc, so markvol can't reach it; minic emits the QBE
# `volatile` keyword directly on the global's load/store.  Checked at MEDIUM
# (near-data: globals use loadw/storew that loadopt forwards/CSEs — the
# discriminating model).  Volatile funcs must keep every access; the
# identical-bodied non-volatile funcs must optimize.
run_volatile_global_asm_probe() {
	src="$QBE_DIR/minic/dos/examples/volatile_global_probe.c"
	ssa=/tmp/volatile_global_probe.ssa
	asm=/tmp/volatile_global_probe.asm
	"$QBE_DIR/minic/minic" -m medium < "$src" > "$ssa" 2>/tmp/volatile_global_probe.err \
		|| { echo "minic failed:"; cat /tmp/volatile_global_probe.err; return 1; }
	"$QBE_DIR/qbe" -t i8086 -m medium "$ssa" > "$asm" 2>/tmp/volatile_global_probe.err \
		|| { echo "qbe failed:"; cat /tmp/volatile_global_probe.err; return 1; }
	fnops() {
		awk -v fn="^_$1:" '$0~fn{p=1;next} /^_[A-Za-z]/{p=0} /^\.section/{p=0} p' "$asm" | grep -c 'word \['
	}
	# vg_load=2 loads, vg_fwd=store+reload(2), evg_load=2 loads -> 6 word ops.
	volsum=$(( $(fnops vg_load) + $(fnops vg_fwd) + $(fnops evg_load) ))
	# each non-volatile folds (CSE'd load / forwarded store) to 1 word op -> 3.
	nonvolsum=$(( $(fnops ng_load) + $(fnops ng_fwd) + $(fnops eng_load) ))
	if [ "$volsum" -lt 6 ]; then
		echo "volatile globals kept only $volsum mem ops (want >=6) — volatile not honored" >&2
		return 1
	fi
	if [ "$nonvolsum" -ne 3 ]; then
		echo "non-volatile globals kept $nonvolsum mem ops (want 3: each should fold)" >&2
		return 1
	fi
	echo "volatile globals=$volsum mem ops (kept), non-volatile=$nonvolsum (folded)" >&2
}

# Compile-time probe for POINTER-TO-VOLATILE / MMIO (`volatile T *p`), the
# §3l extend phase.  The qualifier rides on the POINTEE (a QVOLATILE bit in
# the pointer type, recovered by DREF), so the deref `*p` gets the QBE
# `volatile` keyword while p itself stays a plain pointer.  Checked at MEDIUM
# (near deref via loadw/storew that loadopt forwards/CSEs — the discriminating
# model).  Each volatile fn must keep STRICTLY MORE memory ops than its
# identical-bodied plain twin.
run_volatile_ptr_asm_probe() {
	src="$QBE_DIR/minic/dos/examples/volatile_ptr_probe.c"
	ssa=/tmp/volatile_ptr_probe.ssa
	asm=/tmp/volatile_ptr_probe.asm
	"$QBE_DIR/minic/minic" -m medium < "$src" > "$ssa" 2>/tmp/volatile_ptr_probe.err \
		|| { echo "minic failed:"; cat /tmp/volatile_ptr_probe.err; return 1; }
	"$QBE_DIR/qbe" -t i8086 -m medium "$ssa" > "$asm" 2>/tmp/volatile_ptr_probe.err \
		|| { echo "qbe failed:"; cat /tmp/volatile_ptr_probe.err; return 1; }
	fnops() {
		awk -v fn="^_$1:" '$0~fn{p=1;next} /^_[A-Za-z]/{p=0} /^\.section/{p=0} p' "$asm" | grep -c 'word \['
	}
	vread=$(fnops vp_read); nread=$(fnops np_read)
	vfwd=$(fnops vp_fwd);   nfwd=$(fnops np_fwd)
	if [ "$vread" -le "$nread" ]; then
		echo "vp_read kept $vread mem ops, np_read $nread (want vp_read>np_read: CSE prevented) — pointee volatile not honored" >&2
		return 1
	fi
	if [ "$vfwd" -le "$nfwd" ]; then
		echo "vp_fwd kept $vfwd mem ops, np_fwd $nfwd (want vp_fwd>np_fwd: forward prevented) — pointee volatile not honored" >&2
		return 1
	fi
	echo "vp_read=$vread/np_read=$nread, vp_fwd=$vfwd/np_fwd=$nfwd (volatile deref kept, plain folded)" >&2
}

# §volatile-struct: C `volatile` on struct members + a whole volatile
# aggregate reached through a pointer (`volatile struct S *p; p->m`).  Like
# run_volatile_ptr_asm_probe, the discrimination is asm-only and MEDIUM-only
# (a near member access uses loadw/storew that loadopt forwards/CSEs; far goes
# through loadfw/storefw which loadopt leaves alone, so volatile is honored
# there regardless).  Each volatile fn MUST keep strictly more word-mem ops
# than its identical-bodied plain twin.
run_volatile_struct_asm_probe() {
	src="$QBE_DIR/minic/dos/examples/volatile_struct_probe.c"
	ssa=/tmp/volatile_struct_probe.ssa
	asm=/tmp/volatile_struct_probe.asm
	"$QBE_DIR/minic/minic" -m medium < "$src" > "$ssa" 2>/tmp/volatile_struct_probe.err \
		|| { echo "minic failed:"; cat /tmp/volatile_struct_probe.err; return 1; }
	"$QBE_DIR/qbe" -t i8086 -m medium "$ssa" > "$asm" 2>/tmp/volatile_struct_probe.err \
		|| { echo "qbe failed:"; cat /tmp/volatile_struct_probe.err; return 1; }
	fnops() {
		awk -v fn="^_$1:" '$0~fn{p=1;next} /^_[A-Za-z]/{p=0} /^\.section/{p=0} p' "$asm" | grep -c 'word \['
	}
	# Case 1: volatile member.  Case 2: whole volatile aggregate via pointer.
	for pair in vm_read:nm_read vm_fwd:nm_fwd vs_read:ns_read vs_fwd:ns_fwd; do
		v="${pair%%:*}"; n="${pair##*:}"
		vc=$(fnops "$v"); nc=$(fnops "$n")
		if [ "$vc" -le "$nc" ]; then
			echo "$v kept $vc mem ops, $n $nc (want $v>$n) — member volatile not honored" >&2
			return 1
		fi
	done
	echo "volatile struct members + aggregate-via-ptr kept; plain twins folded" >&2
}

# §volatile-direct: C `volatile` on a DIRECT (non-pointer) `volatile struct S s`
# object accessed at an OFFSET>0 member (§3m limitation (a)).  Before the fix a
# directly-declared volatile aggregate honored volatile only for its offset-0
# member; an offset>0 member became a computed `$s+off` address whose load/store
# carried no volatile keyword and was CSE'd / store-forwarded.  Same asm-only,
# MEDIUM-only discrimination as the other volatile probes.  The GLOBAL pairs
# (vg_read/vg_fwd) are the bug-loud guards (fold to match plain pre-fix); the
# local pair adds coverage of a direct volatile LOCAL struct.
run_volatile_direct_asm_probe() {
	src="$QBE_DIR/minic/dos/examples/volatile_direct_probe.c"
	ssa=/tmp/volatile_direct_probe.ssa
	asm=/tmp/volatile_direct_probe.asm
	"$QBE_DIR/minic/minic" -m medium < "$src" > "$ssa" 2>/tmp/volatile_direct_probe.err \
		|| { echo "minic failed:"; cat /tmp/volatile_direct_probe.err; return 1; }
	"$QBE_DIR/qbe" -t i8086 -m medium "$ssa" > "$asm" 2>/tmp/volatile_direct_probe.err \
		|| { echo "qbe failed:"; cat /tmp/volatile_direct_probe.err; return 1; }
	fnops() {
		awk -v fn="^_$1:" '$0~fn{p=1;next} /^_[A-Za-z]/{p=0} /^\.section/{p=0} p' "$asm" | grep -c 'word \['
	}
	for pair in vg_read:ng_read vg_fwd:ng_fwd vl_read:nl_read; do
		v="${pair%%:*}"; n="${pair##*:}"
		vc=$(fnops "$v"); nc=$(fnops "$n")
		if [ "$vc" -le "$nc" ]; then
			echo "$v kept $vc mem ops, $n $nc (want $v>$n) — direct-struct member volatile not honored" >&2
			return 1
		fi
	done
	echo "direct volatile struct offset>0 members kept; plain twins folded" >&2
}

# §volatile-copy: C `volatile` on a struct-to-struct COPY `*d = *s` / `d = s`
# (§3m limitation (b)).  Before the fix emit_struct_copy emitted plain
# loadw/storew regardless of either operand's volatility, so a volatile struct
# copy (e.g. MMIO register-block snapshot/program) was treated as ordinary
# memory.  Unlike the scalar volatile probes this checks the IR, not an asm
# op-count: QBE does not fold a multi-word aggregate copy (no CSE / dead-store
# across the copy's own stores), so there is no asm fold to prevent — the fix
# lives in minic's emit_struct_copy and is bug-loud at the .ssa level (pre-fix:
# no `volatile` keyword on the copy; post-fix: present on the right side).
# SRC volatile -> volatile LOADS; DST volatile -> volatile STORES.  Checked
# under MEDIUM (near loadw/storew); far-data rides the same keyword on
# loadfw/storefw.
run_volatile_copy_asm_probe() {
	src="$QBE_DIR/minic/dos/examples/volatile_copy_probe.c"
	ssa=/tmp/volatile_copy_probe.ssa
	asm=/tmp/volatile_copy_probe.asm
	"$QBE_DIR/minic/minic" -m medium < "$src" > "$ssa" 2>/tmp/volatile_copy_probe.err \
		|| { echo "minic failed:"; cat /tmp/volatile_copy_probe.err; return 1; }
	# qbe must still accept + lower the volatile copy (no crash, no reorder).
	"$QBE_DIR/qbe" -t i8086 -m medium "$ssa" > "$asm" 2>/tmp/volatile_copy_probe.err \
		|| { echo "qbe failed:"; cat /tmp/volatile_copy_probe.err; return 1; }
	# Extract a function body from the .ssa by name (literal "$NAME(" match).
	fnbody() {
		awk -v fn="\$$1(" 'index($0,fn){p=1} p{print} p&&/^}/{exit}' "$ssa"
	}
	# Count `volatile` keywords on load vs store lines within a function body.
	vload()  { fnbody "$1" | grep -E 'load[a-z]* volatile' | wc -l | tr -d ' '; }
	vstore() { fnbody "$1" | grep -E 'store[a-z]* volatile' | wc -l | tr -d ' '; }
	vall()   { fnbody "$1" | grep -c 'volatile'; }
	# Volatile-SOURCE copies: loads volatile, stores plain.
	for f in vcsrc vpcopy_src; do
		if [ "$(vload "$f")" -lt 1 ] || [ "$(vstore "$f")" -ne 0 ]; then
			echo "$f: want volatile LOADS only, got loads=$(vload "$f") stores=$(vstore "$f")" >&2
			return 1
		fi
	done
	# Volatile-DEST copies: stores volatile, loads plain.
	for f in vcdst vpcopy_dst; do
		if [ "$(vstore "$f")" -lt 1 ] || [ "$(vload "$f")" -ne 0 ]; then
			echo "$f: want volatile STORES only, got loads=$(vload "$f") stores=$(vstore "$f")" >&2
			return 1
		fi
	done
	# Plain twins: NO volatile keyword anywhere in the copy.
	for f in ncsrc ncdst; do
		if [ "$(vall "$f")" -ne 0 ]; then
			echo "$f: plain copy unexpectedly carries volatile ($(vall "$f"))" >&2
			return 1
		fi
	done
	echo "volatile struct copy honors src->loads / dst->stores; plain twins clean" >&2
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

run "medium runtime (grouped_bss_probe)" \
	run_grouped_bss_probe

run "volatile asm (named local)" \
	run_volatile_asm_probe

run "volatile asm (named global)" \
	run_volatile_global_asm_probe

run "volatile asm (pointer-to-volatile)" \
	run_volatile_ptr_asm_probe

run "volatile asm (struct members + aggregate)" \
	run_volatile_struct_asm_probe

run "volatile asm (direct struct offset>0)" \
	run_volatile_direct_asm_probe

run "volatile asm (struct copy)" \
	run_volatile_copy_asm_probe

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
