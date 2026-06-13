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
# Built via tools/build-com-test.sh --model=<model>, run in the batched
# DOSBox boot alongside the .EXE runtime tests.  The .COM libstub has STUB printf/file
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
# tools/build-example.sh --model=<model>; all runtime probes then execute
# together under ONE headless DOSBox boot (tools/run-dos-batch.sh, see
# "batched runtime execution" below) and each CRLF-stripped stdout is
# diff'd against its golden.  Skipped (not failed) when DOSBox is missing.
#
RUNTIME_TESTS=(
	# Small model (.EXE, near code + near data).  All code coalesces into
	# one _TEXT frame (omf_link name-coalescing); libstub keeps its native
	# near ABI and the EXE epilogue is reverse-transformed (see
	# libstub_to_exe.py near_code_model/unfar_epilogue).  cstrprobe has a
	# small-specific golden: %p prints a 16-bit near pointer (5678, the
	# C-correct truncation of (char*)0x12345678L).
	"minic/dos/examples/cprobe.c:minic/dos/tests/cprobe.golden.txt:small"
	"minic/dos/examples/cstrprobe.c:minic/dos/tests/cstrprobe.golden.small.txt:small"
	"minic/dos/examples/fnptrprobe.c:minic/dos/tests/fnptrprobe.golden.txt:small"
	"minic/dos/examples/mathprobe.c:minic/dos/tests/mathprobe.golden.txt:small"
	"minic/dos/examples/dosapi_probe.c:minic/dos/tests/dosapi_probe.golden.txt:small"
	"minic/dos/tests/fileio.c:minic/dos/tests/fileio_exe.golden.txt:small"
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
	# _sf_* helper calls.  Arithmetic/compare/convert are model-independent
	# (medium probes below); far-data float load/store goes through the
	# loadfs/storefs ops (float_fardata_probe, compact/large/huge).
	"minic/dos/examples/softfloat_probe.c:minic/dos/tests/softfloat_probe.golden.txt:medium"
	"minic/dos/examples/softfloat_probe.c:minic/dos/tests/softfloat_probe.golden.txt:compact"
	"minic/dos/examples/float_literal_probe.c:minic/dos/tests/float_literal_probe.golden.txt:medium"
	# `double` aliases to single-precision (Ks) on this FPU-less target
	# (sizeof==4, single-precision arithmetic), static float initializers
	# (file-scope global + struct member, incl. negative via the 0-x desugar),
	# and float->long conversion (Ostosi with a Kl result).
	"minic/dos/examples/double_float_probe.c:minic/dos/tests/double_float_probe.golden.txt:medium"
	"minic/dos/examples/double_float_probe.c:minic/dos/tests/double_float_probe.golden.txt:compact"
	# Algebraic soft-float libm (fabs/copysign/floor/ceil/round/nearbyint/fmod
	# + isnan/isinf/signbit) reached via <math.h> macro names.  Prerequisite
	# for MICROPY_FLOAT_IMPL_FLOAT.
	"minic/dos/examples/softlibm_probe.c:minic/dos/tests/softlibm_probe.golden.txt:medium"
	"minic/dos/examples/softlibm_probe.c:minic/dos/tests/softlibm_probe.golden.txt:compact"
	# Transcendental soft-float libm (exp2/log2/exp/log + powf).  powf is the
	# one of these the curated MicroPython core links (objfloat **, parsenum
	# 1eN, round(x,n)); exact integer-exponent fast path + exp2/log2 core.
	"minic/dos/examples/softtrig_probe.c:minic/dos/tests/softtrig_probe.golden.txt:medium"
	"minic/dos/examples/softtrig_probe.c:minic/dos/tests/softtrig_probe.golden.txt:compact"
	# §5b math-module soft-libm: sqrt (correctly rounded, fdlibm bitwise) +
	# sin/cos/tan (4-part 8-bit-chunk pi/4 reduction, exact to y < 2^16) +
	# asin/acos/atan/atan2 + frexp/ldexp/modf/isfinite, AND the bare-symbol
	# fn-POINTER path modmath.c uses (math_generic_1(x, sqrtf)) — which
	# found+fixed the minic fn-ptr float-return DREF/FAR collision: a
	# float-returning fn ptr decoded as int-returning (=w call + swtof).
	# Golden verified line-by-line vs host doubles by build/mathfns-verify.py.
	"minic/dos/examples/mathfns_probe.c:minic/dos/tests/mathfns_probe.golden.txt:medium"
	"minic/dos/examples/mathfns_probe.c:minic/dos/tests/mathfns_probe.golden.txt:compact"
	# Call-argument int<->float conversion (C11 6.5.2.2p7, §4x): an integer
	# argument to a prototyped float param must convert (swtof/sltof), not
	# pass its raw word as a binary32 denormal; float arg to int/long param
	# converts via stosi.  MicroPython surface: parsenum.c mp_decimal_exp's
	# powf(5, -dec_exp) was powf(eps, eps) ~= 1.0 — every float literal
	# mis-parsed (1.5 -> 7.5).
	"minic/dos/examples/float_arg_coerce_probe.c:minic/dos/tests/float_arg_coerce_probe.golden.txt:medium"
	"minic/dos/examples/float_arg_coerce_probe.c:minic/dos/tests/float_arg_coerce_probe.golden.txt:compact"
	# Kw div/rem AX/DX liveness brackets (§4y): the inline idiv/div handlers
	# clobbered AX (dividend staging, quotient) and DX (cwd/xor, remainder)
	# with NO save — a live temp parked in either was destroyed (the §1h
	# found-not-fixed two-div-one-call bug; 21 sites in the MP image via the
	# emit-bracket audit, tools/run-emit-audit.sh).  rega-dependent trigger:
	# green probe is necessary-not-sufficient, the audit is the real guard.
	"minic/dos/examples/div_live_clobber_probe.c:minic/dos/tests/div_live_clobber_probe.golden.txt:medium"
	"minic/dos/examples/div_live_clobber_probe.c:minic/dos/tests/div_live_clobber_probe.golden.txt:compact"
	# Extern/multi-decl sized-array declarators (§4z): a BARE-NUM dimension
	# (extern char a[65024];) reduces through ext_decllist as an op-B node
	# the EXTERN walkers treated as a SCALAR — references then LOADED the
	# first byte instead of decaying to the address (MicroPython gc_add got
	# seg 0:0 and zeroed the IVT).  Also pins the file-scope int a, b[10];
	# op-B gap (wrong-size scalar global) and sizeof on sized extern arrays.
	"minic/dos/examples/extern_array_decay_probe.c:minic/dos/tests/extern_array_decay_probe.golden.txt:medium"
	"minic/dos/examples/extern_array_decay_probe.c:minic/dos/tests/extern_array_decay_probe.golden.txt:compact"
	# Multi-declarator initializers (§5a): the stmt-context multi-decl rule
	# (type IDENT, ext_decllist;) emitted each declarator's init via a direct
	# expr() at parse time = function ENTRY, so `T k, nf = 0;` in a loop body
	# initialized once and accumulated across iterations (bit §4z's debug
	# counter), and `int k, *p = &g[i];` read i before its init.  Also pins
	# the previously-missing stmt rule `int a = 1, b = 2;` (was a parse
	# error) and the dcls _full path `int a[5], b = 3;` (init silently
	# DROPPED), plus side-effecting inits in never-taken branches.
	"minic/dos/examples/multi_decl_init_probe.c:minic/dos/tests/multi_decl_init_probe.golden.txt:medium"
	"minic/dos/examples/multi_decl_init_probe.c:minic/dos/tests/multi_decl_init_probe.golden.txt:compact"
	# Soft-float compare/convert result in CX (§4x): the Ocmps/Ostosi emit
	# brackets pushed/popped CX unconditionally, so a result rega placed in
	# CX was popped over with stale garbage (objfloat.c modulo sign-fix fired
	# on 7.5 % 2.0 -> 3.5; bool(0.0) -> True on Victor).  rega-dependent
	# trigger: green probe is necessary-not-sufficient, the real guard is the
	# dst_in_cx skip in i8086/emit.c.  Verified bug-loud vs the unfixed emit.
	"minic/dos/examples/float_cmp_cx_probe.c:minic/dos/tests/float_cmp_cx_probe.golden.txt:medium"
	"minic/dos/examples/float_cmp_cx_probe.c:minic/dos/tests/float_cmp_cx_probe.golden.txt:compact"
	# §5c float**/type-encoding probe: FLOAT two levels deep no longer
	# collides with FAR (24->26 move) + fnproto.rett direct-call decode.
	"minic/dos/examples/float_dblptr_probe.c:minic/dos/tests/float_dblptr_probe.golden.txt:medium"
	"minic/dos/examples/float_dblptr_probe.c:minic/dos/tests/float_dblptr_probe.golden.txt:compact"
	# §6a extern pointer-returning ANSI prototype (extern char *f(int);)
	# — newlibc hits it via errno.h extern int *__errno(void).
	"minic/dos/examples/extern_ptrret_probe.c:minic/dos/tests/extern_ptrret_probe.golden.txt:small"
	"minic/dos/examples/extern_ptrret_probe.c:minic/dos/tests/extern_ptrret_probe.golden.txt:medium"
	# §6a file-scope prototype param names leaked into the global symtab
	# (bogus double definition on later reuse with a different type).
	"minic/dos/examples/proto_param_leak_probe.c:minic/dos/tests/proto_param_leak_probe.golden.txt:small"
	"minic/dos/examples/proto_param_leak_probe.c:minic/dos/tests/proto_param_leak_probe.golden.txt:medium"
	# §6a array parameter declarators (T a[], T a[11], char *const argv[])
	# decay to pointers per C; par1 had no bracket forms at all.
	"minic/dos/examples/array_param_probe.c:minic/dos/tests/array_param_probe.golden.txt:small"
	"minic/dos/examples/array_param_probe.c:minic/dos/tests/array_param_probe.golden.txt:medium"
	# §6a `void __far __attribute__((interrupt)) f(void);` PROTOTYPE parse
	# (ia16-gcc far-ISR spelling; definitions stay a designed gap).
	"minic/dos/examples/isr_far_attr_probe.c:minic/dos/tests/isr_far_attr_probe.golden.txt:small"
	"minic/dos/examples/isr_far_attr_probe.c:minic/dos/tests/isr_far_attr_probe.golden.txt:medium"
	# §6a scalar global `T *p = &x;` / `char **e = arr;` symbol-address
	# init (cival_eval path).  NOT gated under far-data models: the §1g
	# far static-DATA-ptr reloc gap is REAL at runtime (this probe under
	# compact prints raw offsets 4194/4192 — segment missing).
	"minic/dos/examples/static_sym_init_probe.c:minic/dos/tests/static_sym_init_probe.golden.txt:small"
	"minic/dos/examples/static_sym_init_probe.c:minic/dos/tests/static_sym_init_probe.golden.txt:medium"
	# §6a locals shadow file-scope bindings (global var / function /
	# enum constant) via the extended block_scope_decl alpha-rename.
	"minic/dos/examples/local_shadow_probe.c:minic/dos/tests/local_shadow_probe.golden.txt:small"
	"minic/dos/examples/local_shadow_probe.c:minic/dos/tests/local_shadow_probe.golden.txt:medium"
	"minic/dos/examples/float_fardata_probe.c:minic/dos/tests/float_fardata_probe.golden.txt:compact"
	"minic/dos/examples/float_fardata_probe.c:minic/dos/tests/float_fardata_probe.golden.txt:large"
	"minic/dos/examples/float_fardata_probe.c:minic/dos/tests/float_fardata_probe.golden.txt:huge"
	# Regression: copy.c must not fold a class-narrowing `=w copy` of an `l`
	# temp on i8086 (T.wordsz==2) — `(int)(a>>31) && ...` would invert the
	# sign decision.  Surfaced by the soft-libm floor/fmod/round helpers.
	"minic/dos/examples/kl_narrow_copy_branch_probe.c:minic/dos/tests/kl_narrow_copy_branch_probe.golden.txt:medium"
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
	"minic/dos/examples/struct_align_probe.c:minic/dos/tests/struct_align_probe.golden.txt:compact"
	"minic/dos/examples/struct_align_probe.c:minic/dos/tests/struct_align_probe.golden.txt:large"
	"minic/dos/examples/struct_align_probe.c:minic/dos/tests/struct_align_probe.golden.txt:huge"
	# §4i far-pointer offset arithmetic: `far_ptr + idx` for an in-segment
	# offset >= 0x8000 (addfo/subfo, segment-preserving 16-bit wraparound).
	# compact/large only — under huge `far_ptr +/- idx` routes through
	# huge_ptr_binop/_qbe_huge_add (a different, segment-normalising path that
	# this fix does not touch), and that path has its own pre-existing >=0x8000
	# gap; see the probe header.  See [[project-far-ptr-unsigned-index-bug]].
	"minic/dos/examples/gc_bigheap_probe.c:minic/dos/tests/gc_bigheap_probe.golden.txt:compact"
	"minic/dos/examples/gc_bigheap_probe.c:minic/dos/tests/gc_bigheap_probe.golden.txt:large"
	# §4l self-contained faithful repro of MicroPython's conservative mark/sweep
	# GC on a 49 KB far-data heap (forces ~18 collections under churn).  Guards the
	# §4i far-ptr fix + GC-core far-data correctness (multi-level marking, far-array
	# indexing, conservative dual-aligned scan).  PASSES — it proved the GC core is
	# NOT the source of the MicroPython churn(120) corruption (that's in the
	# MicroPython object/mp_state layer; see NEXT_SESSION §4l/§4m).  Layout-independent
	# output (counts derive from HEAP_BYTES), identical compact/large.
	"minic/dos/examples/gc_churn_probe.c:minic/dos/tests/gc_churn_probe.golden.txt:compact"
	"minic/dos/examples/gc_churn_probe.c:minic/dos/tests/gc_churn_probe.golden.txt:large"
	# §4m STATIC layout audit: every far-POINTER field in MicroPython's live heap
	# object types (qstr_pool/map/dict/list/code_state) must sit at a 4-mod-0
	# (sizeof(void*)) offset so the conservative GC's 4-stride scan finds it.
	# Guards §4g's far-data struct-member alignment — a regression here would
	# silently re-introduce the §4f "freed-while-live" GC bug class.  No GC at
	# runtime (just offsetof prints); layout-independent, identical compact/large.
	"minic/dos/examples/gc_offset_probe.c:minic/dos/tests/gc_offset_probe.golden.txt:compact"
	"minic/dos/examples/gc_offset_probe.c:minic/dos/tests/gc_offset_probe.golden.txt:large"
	# §4r variable-shift count pin (selshift CX pin, mirroring amd64).  The §4q
	# root cause of the churn(120) saga: emit.c read a variable shift count from
	# a register rega had spilled-and-reused (gc_mark_subtree's ATB_GET_KIND
	# computed `atb >> atb`), so a live HEAD block was never marked and the
	# "churn" qstr was freed-while-live.  Recreates the gc_mark_subtree shape
	# (packed 2-bit table, imul count, extub value, register pressure) + Kw/Kl
	# variable-shift edge cases with shift-free expectations.  NOTE: the original
	# miscompile was layout-sensitive — a green probe alone is necessary, not
	# sufficient (the real guard is the isel pin itself + the Victor scale2 run).
	"minic/dos/examples/shift_count_spill_probe.c:minic/dos/tests/shift_count_spill_probe.golden.txt:medium"
	"minic/dos/examples/shift_count_spill_probe.c:minic/dos/tests/shift_count_spill_probe.golden.txt:compact"
	# §4w Kl/Ks stack-slot coloring (spill.c::colorklslots).  i8086 forces every
	# Kl temp slot-resident; pre-§4w each owned a PRIVATE 2-word slot, making
	# frame size proportional to the Kl temp COUNT (mp_execute_bytecode: 1261
	# temps = 5464-byte frame = the ~5.6KB/level generator-resume cost measured
	# in §4v).  §4w colors the interference graph so disjoint live ranges SHARE
	# slots (same fn: 12 colors, 472B frame).  The probe is bug-loud on an
	# interference miss (two overlapping temps sharing a slot = value bleed):
	# 14 longs live across a call, short disjoint chains (these DO share), a
	# loop-carried long swap cycle (pins the phi-web no-share rule — pmgen would
	# otherwise need a slot<->slot Oswap emit doesn't implement), longs live
	# across in-loop calls, and a pointer ping-pong walk (far Kl under compact).
	"minic/dos/examples/kl_slot_color_probe.c:minic/dos/tests/kl_slot_color_probe.golden.txt:medium"
	"minic/dos/examples/kl_slot_color_probe.c:minic/dos/tests/kl_slot_color_probe.golden.txt:compact"
	# §4s pointer RELATIONAL compares are UNSIGNED (C11 6.5.8).  minic lowered
	# ptr <,<=,>,>= to signed cslt/csle (pointers never carry the UNSIGNED type
	# flag), inverting the result whenever the operands straddle the sign bit:
	# near offset >= 0x8000 vs below, or far SEGMENT word >= 0x8000 vs below.
	# Latent in the MicroPython images only because every segment there is
	# >= 0x8000.  medium pins the near Kw path (cultw), compact the far-data Kl
	# path (cultl); far-pointer cases discriminate under both.
	"minic/dos/examples/ptr_relational_probe.c:minic/dos/tests/ptr_relational_probe.golden.txt:medium"
	"minic/dos/examples/ptr_relational_probe.c:minic/dos/tests/ptr_relational_probe.golden.txt:compact"
	# §4t Osub Kw two-address rescue: when rega gives a non-commutative op's
	# result the same register as arg[1], emit.c saves arg[1] through a scratch
	# — which was HARDCODED to BX.  With to==BX the save degenerated to
	# `mov bx, bx`, the dst-mov clobbered it, and the trailing `pop bx`
	# discarded the result: `right_pad -= p` compiled to a NO-OP and
	# MicroPython's mp_print_strn right-pad loop ("%-5d" % 7) hung on Victor.
	# The scratch is now chosen distinct from both the dest and arg[0].
	# pad_out2 recreates the allocation (rega-dependent: green probe is
	# necessary-not-sufficient; the real guard is the scratch chooser itself).
	"minic/dos/examples/sub_arg1_alias_probe.c:minic/dos/tests/sub_arg1_alias_probe.golden.txt:medium"
	"minic/dos/examples/sub_arg1_alias_probe.c:minic/dos/tests/sub_arg1_alias_probe.golden.txt:compact"
	# §4v split stack (SS != DS): built with --split-stack (qbe -s ss:
	# overrides on register-indirect near derefs + omf_link --separate-stack
	# + libstub _dgroup_para DS reloads).  Exercises escaped &local writes,
	# stack struct member chains, _far_sprintf into a stack buffer, fn-ptr
	# callbacks with stack ptrs, setjmp/longjmp, and pins malloc seg ==
	# DGROUP seg != stack seg (ok8 is the discriminator: a default link
	# prints ok8 0).
	"minic/dos/examples/split_stack_probe.c:minic/dos/tests/split_stack_probe.golden.txt:compact"
	"minic/dos/examples/split_stack_probe.c:minic/dos/tests/split_stack_probe.golden.txt:large"
	# §6d __attribute__((interrupt)) — QBE `interrupt` linkage → i8086 ISR
	# prologue/epilogue (static-memory ES save, DS/ES=DGROUP from a CS-local
	# `dw DGROUP` word, all-register save, iret).  Handler on software INT
	# 0xF1: near-data + far MMIO + callee + 32-bit divide inside the ISR;
	# fired 1006 times (stack-balance hammer).  The pre-fix toolchain dies
	# at build ("last block misses jump" — asm-"iret" left the block
	# unterminated); a wrong epilogue dies at first trigger.  small pins the
	# bare-metal model (near ret → iret), medium the far-code path
	# (retf → iret + far fn-ptr IVT install).
	"minic/dos/examples/isr_probe.c:minic/dos/tests/isr_probe.golden.txt:small"
	"minic/dos/examples/isr_probe.c:minic/dos/tests/isr_probe.golden.txt:medium"
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

# --- batched runtime execution ----------------------------------------------
#
# Building a probe and RUNNING it are split into two phases so every runtime
# image executes under a single DOSBox boot (tools/run-dos-batch.sh): the
# emulator launch (~2-4 s) dominated gate wall-clock when each of ~185
# runtime tests booted its own DOSBox, while the probes themselves finish in
# milliseconds.  prep + a build command compile one image; on success
# stage_runtime_case copies it into the batch dir (a private copy — the same
# exe path is rebuilt at several models) alongside its golden.
# flush_runtime_batch then runs the whole set in one boot and prints one
# [ok]/[FAIL] line per test.  Only the run phase counts toward pass/fail
# totals (a build failure counts there instead), so the gate's test count is
# unchanged vs the per-test-launch era.
BATCH_DIR="$(mktemp -d -t test-dos-batch.XXXXXX)"
trap 'rm -rf "$BATCH_DIR"' EXIT
MANIFEST="$BATCH_DIR/manifest.tsv"
CASE_DESC=()
CASE_GOLDEN=()
CASE_OUT=()
ncases=0

# prep <desc> <build-cmd...> — build-phase wrapper: [built] on success (the
# definitive [ok]/[FAIL] line for the test comes from flush_runtime_batch),
# loud [FAIL]/[skip] on failure, counted here since the run phase never
# sees the case.
prep() {
	desc="$1"; shift
	if "$@" >/tmp/test-dos.out 2>&1; then
		printf '%-44s[built]\n' "$desc"
		return 0
	else
		rc=$?
		printf '%-44s' "$desc"
		if [ "$rc" -eq 77 ]; then
			echo "[skip]"
			sed 's/^/    /' /tmp/test-dos.out
			skip=$((skip + 1))
		else
			echo "[FAIL]"
			sed 's/^/    /' /tmp/test-dos.out
			fail=$((fail + 1))
		fi
		return 1
	fi
}

stage_runtime_case() {
	desc="$1"; exe="$2"; golden="$3"; stdin="${4:-}"
	staged="$BATCH_DIR/c$ncases.${exe##*.}"
	cp "$exe" "$staged"
	# Optional 4th arg = host file fed as DOS stdin (3rd manifest field);
	# absent → empty field → no redirect in run-dos-batch.sh.
	printf '%s\t%s\t%s\n' "$staged" "$BATCH_DIR/c$ncases.out" "$stdin" >> "$MANIFEST"
	CASE_DESC[$ncases]="$desc"
	CASE_GOLDEN[$ncases]="$golden"
	CASE_OUT[$ncases]="$BATCH_DIR/c$ncases.out"
	ncases=$((ncases + 1))
}

flush_runtime_batch() {
	[ "$ncases" -gt 0 ] || return 0
	batch_rc=0
	"$QBE_DIR/tools/run-dos-batch.sh" "$MANIFEST" \
		>/tmp/test-dos-batch.log 2>&1 || batch_rc=$?
	i=0
	while [ "$i" -lt "$ncases" ]; do
		printf '%-44s' "${CASE_DESC[$i]}"
		outf="${CASE_OUT[$i]}"
		if [ "$batch_rc" -eq 77 ]; then
			echo "[skip]"
			[ "$i" -eq 0 ] && sed 's/^/    /' /tmp/test-dos-batch.log
			skip=$((skip + 1))
		elif [ ! -f "$outf" ]; then
			echo "[FAIL]"
			echo "    no output captured (an earlier hang/crash aborted the batch; see /tmp/test-dos-batch.log)"
			fail=$((fail + 1))
		elif printf '%s\n' "$(cat "$outf")" \
				| diff -u "${CASE_GOLDEN[$i]}" - >/tmp/test-dos.out 2>&1; then
			echo "[ok]"
			pass=$((pass + 1))
		else
			echo "[FAIL]"
			sed 's/^/    /' /tmp/test-dos.out
			fail=$((fail + 1))
		fi
		i=$((i + 1))
	done
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

# Build a probe at the requested memory model (run+diff happens later via
# the batch; see flush_runtime_batch).
build_runtime_probe() {
	src="$1"
	model="$2"
	base="$(basename "$src" .c)"
	# Far-static-data probes opt into the additional-far-segment placement
	# (statics outside DGROUP).  Gated by basename so only these exercise it
	# until far-global direct access is complete (see NEXT_SESSION.md).
	farstatic=0
	case "$base" in fardata_probe|farglobal_probe|farstruct_ptr_probe|slotarray_probe|gc_bigheap_probe|gc_churn_probe) farstatic=1 ;; esac
	# Soft-float probes link the single-precision soft-float helper library.
	sfflag=""
	case "$base" in softfloat_probe|float_literal_probe|float_fardata_probe|softlibm_probe|softtrig_probe|double_float_probe|float_arg_coerce_probe|float_cmp_cx_probe|mathfns_probe|float_dblptr_probe) sfflag="--softfloat" ;; esac
	# Split-stack probe builds with SS in its own segment (qbe -s +
	# omf_link --separate-stack); its ok8 asserts stack seg != DGROUP seg.
	ssflag=""
	case "$base" in split_stack_probe) ssflag="--split-stack" ;; esac
	QBE_FAR_STATIC_DATA="$farstatic" \
		"$QBE_DIR/tools/build-example.sh" --model="$model" $sfflag $ssflag "$QBE_DIR/$src" >/dev/null
}

# §6b newlibc DOS-hosted tests (Phase-6 step 2).  Each builds a newlibc
# phase3 test TU + the portable subset (libgloss/VFS/FAT/block) +
# minic/dos/newlibc/dos_shim.c via tools/build-newlibc-test.sh (small
# model, --no-stdio libstub) and diffs DOSBox stdout against a golden.
# Skipped when the newlibc tree is absent.  memory_test is intentionally
# NOT here: it scans the Victor physical memory map (hardware-flavored,
# MAME bare-metal material, not portable-subset).
NEWLIBC_TESTS=(
	snprintf_test
	fat_bpb_test
	fat_chain_test
	fat_root_test
	fat_dir_test
	fat_file_test
	fat_vfs_test
	ramfs_test
	stdio_route_test
	bss_test
	terminal_meta_test
)

build_newlibc_test() {
	name="$1"; shift
	nl="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"
	if [ ! -d "$nl" ]; then
		echo "newlibc tree not found: $nl"
		return 77
	fi
	# Extra args ($@) forward to build-newlibc-test.sh — e.g. the §6k
	# fat_write_test entry passes --model=medium --stack-size=5120.
	"$QBE_DIR/tools/build-newlibc-test.sh" "$name" "$@" >/dev/null
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

# Same as build_runtime_probe but for .COM via tools/build-com-test.sh.
build_com_runtime_probe() {
	src="$1"
	model="$2"
	"$QBE_DIR/tools/build-com-test.sh" --model="$model" "$QBE_DIR/$src" >/dev/null
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
	base="$(basename "$src" .c)"
	if prep "$model runtime ($base)" build_com_runtime_probe "$src" "$model"; then
		stage_runtime_case "$model runtime ($base)" \
			"$QBE_DIR/build/com-test/$base/$base.com" "$QBE_DIR/$golden"
	fi
done

for entry in "${RUNTIME_TESTS[@]}"; do
	src="${entry%%:*}"
	rest="${entry#*:}"
	golden="${rest%%:*}"
	model="${rest##*:}"
	base="$(basename "$src" .c)"
	if prep "$model runtime ($base)" build_runtime_probe "$src" "$model"; then
		stage_runtime_case "$model runtime ($base)" \
			"$QBE_DIR/build/examples/$base/$base.exe" "$QBE_DIR/$golden"
	fi
done

# OMF target-frame fixups into grouped near data: needs two translation
# units (the referenced _BSS globals in one object, the data guard that old
# BSS-relative fixups corrupt in another) — a shape the single-source
# runtime table cannot express.
if prep "medium runtime (grouped_bss_probe)" \
	"$QBE_DIR/tools/build-example.sh" --model=medium \
	"$QBE_DIR/minic/dos/examples/grouped_bss_probe.c" \
	"$QBE_DIR/minic/dos/examples/grouped_bss_def.c"; then
	stage_runtime_case "medium runtime (grouped_bss_probe)" \
		"$QBE_DIR/build/examples/grouped_bss_probe/grouped_bss_probe.exe" \
		"$QBE_DIR/minic/dos/tests/grouped_bss_probe.golden.txt"
fi

# C internal linkage of file-scope DATA (§6b): two TUs each define
# `static int dir_table` (different values) plus a same-named block static
# behind a same-named static fn; one ordinary global crosses TUs via extern.
# Pre-fix the link died on a duplicate public (`_dir_table`) because
# asm_to_omf auto-promoted data labels.
for model in small medium; do
	if prep "$model runtime (static_data_probe)" \
		"$QBE_DIR/tools/build-example.sh" --model="$model" \
		"$QBE_DIR/minic/dos/examples/static_data_probe.c" \
		"$QBE_DIR/minic/dos/examples/static_data_def.c"; then
		stage_runtime_case "$model runtime (static_data_probe)" \
			"$QBE_DIR/build/examples/static_data_probe/static_data_probe.exe" \
			"$QBE_DIR/minic/dos/tests/static_data_probe.golden.txt"
	fi
done

for t in "${NEWLIBC_TESTS[@]}"; do
	if prep "newlibc small ($t)" build_newlibc_test "$t"; then
		stage_runtime_case "newlibc small ($t)" \
			"$QBE_DIR/build/newlibc-tests/$t/$t.exe" \
			"$QBE_DIR/minic/dos/tests/newlibc_$t.golden.txt"
	fi
done

# §6n: the keyboard-input tests — getchar/fgets (stdin_test) and scanf
# (scanf_test) read DOS handle 0 via INT 21h AH=3Fh through newlibc's
# /dev/console.  A DOS stdin redirect (`< IN.TXT`, the run-dos-batch.sh 3rd
# manifest field via stage_runtime_case's 4th arg) feeds the input
# deterministically — no echo, unlike the cooked CON device on real
# hardware.  Both build small-model (portable stdio, no fat_write bulk).
for t in stdin_test scanf_test; do
	if prep "newlibc small ($t)" build_newlibc_test "$t"; then
		stage_runtime_case "newlibc small ($t)" \
			"$QBE_DIR/build/newlibc-tests/$t/$t.exe" \
			"$QBE_DIR/minic/dos/tests/newlibc_$t.golden.txt" \
			"$QBE_DIR/minic/dos/tests/newlibc_$t.stdin.txt"
	fi
done

# §6k: the upstream FAT WRITE test runs MEDIUM, not small.  fat_write.c
# (~18KB) on top of the FAT/VFS/stdio stack overflows the small model's
# single-_TEXT 64KB code ceiling; medium splits code across far CS
# segments (near data stays in one DGROUP).  Exercises vfs_mount_fat_rw +
# create/write/append/truncate/unlink/mkdir/rmdir/rename over a RAM disk.
# Smaller stack: the test's ramdisk media[] crowds the near-data DGROUP.
if prep "newlibc medium (fat_write_test)" \
	build_newlibc_test fat_write_test --model=medium --stack-size=5120; then
	stage_runtime_case "newlibc medium (fat_write_test)" \
		"$QBE_DIR/build/newlibc-tests/fat_write_test/fat_write_test.exe" \
		"$QBE_DIR/minic/dos/tests/newlibc_fat_write_test.golden.txt"
fi

# §6m: a second medium FAT-write gate, exercising the fat_write.c primitives
# DIRECTLY (no vfs_mount) on hand-built RAM volumes: FAT16 entry write/read +
# both-FAT mirroring + cluster-chain alloc/free + create/write/truncate/
# unlink/mkdir/rename + ENOSPC, and FAT12 entries straddling a FAT sector
# boundary (both parities).  Same small-model 64KB ceiling → medium.
if prep "newlibc medium (fat_write_unit_test)" \
	build_newlibc_test fat_write_unit_test --model=medium --stack-size=5120; then
	stage_runtime_case "newlibc medium (fat_write_unit_test)" \
		"$QBE_DIR/build/newlibc-tests/fat_write_unit_test/fat_write_unit_test.exe" \
		"$QBE_DIR/minic/dos/tests/newlibc_fat_write_unit_test.golden.txt"
fi

# One DOSBox boot for everything staged above.
flush_runtime_batch

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
