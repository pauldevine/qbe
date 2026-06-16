#!/bin/bash
# build-micropython.sh — first-link bring-up of the MicroPython REPL for
# 8086/8088 DOS, via the minic → qbe(-t i8086) → asm_to_omf → nasm → omf_link
# toolchain.  Modeled on tools/build-stevie.sh + the build/mp-spike harness.
#
# Pipeline per TU:
#   1. C   → preprocessed C   via `clang -E` (conforming cpp) + type-normalize
#   2. C   → SSA              via minic -m medium
#   3. SSA → i8086 asm        via qbe -t i8086 -m medium
#   4. asm → OMF asm          via tools/asm_to_omf.py  (per-TU label prefixing)
#   5. OMF asm → .obj         via nasm -f obj
# Then: crt0_exe.obj + all TU .obj + the runtime (§8d default = the libstub-FREE
# stack: dos_*/newlibc support + qbe_rt/dos_syscall_far_data/far_stdlib_bridge/
# builtins_rt/setjmp_rt/heap; --libstub anchor = libstub_exe.obj) → omf_link →
# mpython.exe
#
# This is a SPIKE: the point is to surface the link-layer gap (undefined
# symbols, segment-count limits, setjmp/longjmp).  --keep-going tolerates
# per-TU compile failures and links whatever succeeded.
#
# Under a far-data model (--model=compact|large|huge) the build sets
# QBE_FAR_STATIC_DATA so each TU's statics land in its OWN far segment
# (class FAR_DATA/FAR_BSS) placed outside DGROUP — freeing DGROUP for the
# heap+stack (the §1r runtime-hang fix: medium's single 64KB DGROUP can't
# hold MicroPython's ~55KB of static data PLUS heap+stack).  Far-data
# stdlib/stdio/setjmp routing is handled by minic's far_stdlib[] mangling +
# the FAR_*_EXE helpers in libstub_to_exe.py.
#
# Usage: tools/build-micropython.sh [--keep-going] [--model=<m>]

set -u
KEEP_GOING=0
MODEL=medium
# §8c/§8d: libstub retirement for MicroPython.  The libstub-FREE runtime (the
# §7r/§7v stevie path scaled to MP): newlibc's portable stdio + the dos_libc.c
# libc fill + qbe_rt/dos_syscall_far_data/far_stdlib_bridge/heap/setjmp_rt + the
# §8c builtins_rt.asm (___builtin_clz/clzl/expect/unreachable — the one symbol
# set MP needs that libstub provided and the libstub-free runtime did not),
# INSTEAD of libstub_exe.obj.  MP's own main() is renamed to newlibc_test_main
# via -Dmain so dos_shim.c's main wrapper runs first (vfs_init() is a no-op on
# the DOS path), exactly as stevie does.
#
# §8d (2026-06-16): libstub-free is now the DEFAULT.  The regression corpus is
# the libstub-free compact build (default heaps → image 710,352 / body 689,760;
# the old libstub body was 731,088).  --libstub is the opt-out equivalence
# ANCHOR (libstub_to_exe.py's python printf + the .COM-stub libc), kept for the
# cross-check and still used by the probe build scripts; --no-libstub re-asserts
# the default.
NO_LIBSTUB=1
for arg in "$@"; do
	case "$arg" in
		--keep-going) KEEP_GOING=1 ;;
		--model=*) MODEL="${arg#--model=}" ;;
		--no-libstub) NO_LIBSTUB=1 ;;
		--libstub) NO_LIBSTUB=0 ;;
		*) echo "unknown arg '$arg'" >&2; exit 1 ;;
	esac
done

# Under --no-libstub MP's own main() is renamed so dos_shim.c's main() (which
# calls vfs_init() then newlibc_test_main()) is the crt0 entry — the §7r
# stevie pattern.  Renaming via -Dmain is also why dos_shim is linked WITH its
# main(): a libstub-free build that dropped it (-DNO_SHIM_MAIN) hits a minic
# quirk where removing the trailing main() drops the file-scope FILE-layer
# statics (shim_files/_impure_ptr) from the TU — the rename sidesteps it.
MP_LIBC_DEFS=""
[ "$NO_LIBSTUB" = 1 ] && MP_LIBC_DEFS="-Dmain=newlibc_test_main"

# Far-data models route each module's statics to its own far segment so
# total static data can exceed the single 64KB DGROUP.  DOS_FAR_DATA tells
# the port's inline-asm console HAL (mphalport.c) to read its args under the
# 4-byte-far-pointer ABI.  FAR_DATA tells minic/include/stdint.h to make
# intptr_t/uintptr_t (hence mp_int_t/mp_uint_t/mp_obj_t-tagging) 32-bit so a
# far pointer survives a uintptr_t round-trip — without it gc_setup_area's
# pointer math truncates the segment and gc_alloc's scan loop spins forever.
FARSTATIC_FLAG=""
FARDATA_DEF=""
case "$MODEL" in
	compact|large|huge) FARSTATIC_FLAG="--far-static-data"; FARDATA_DEF="-DDOS_FAR_DATA=1 -DFAR_DATA=1" ;;
esac

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MP="$HOME/projects/micropython"
DOSPORT="$MP/ports/dos8086"
GENHDR="$MP/ports/minimal/build"          # genhdr/qstrdefs.generated.h etc.
INC_DIR="$QBE_DIR/minic/include"
STUB="$QBE_DIR/build/mp-spike/stubinc"
MINIC="$QBE_DIR/minic/minic"
QBE="$QBE_DIR/qbe"
DOS_DIR="$QBE_DIR/minic/dos"
OUT_DIR="$QBE_DIR/build/mp-link"
mkdir -p "$OUT_DIR"
# Default DOS stack = 61440.  The dos8086 port runs the STACKLESS-strict VM
# (ports/dos8086/mpconfigport.h §4b): deep Python recursion chains code_state
# frames on the GC heap instead of the C stack — but deep GENERATOR recursion
# still C-recurses on resume (objgenerator.c → mp_execute_bytecode, which
# STACKLESS does NOT cover) at a §4v-MEASURED ~5.6 KB of C stack per level
# (probe: VM-entry &param far addresses; the earlier "~2 KB/level" §4u
# estimate was derived from overflow-luck endpoints, see below).  With the
# §4v split stack (own SS segment) the old DGROUP cap (64KB − ~37KB data+bss
# ≈ 28.4KB) is GONE; the cap is the 16-bit SP itself, so 61440 ≈ the max
# (SP ≤ 65534) with a round margin.  ~61440/5772 ≈ 10 real generator levels.
# HISTORY WARNING: every pre-§4v "frontier" measurement (gc(8)@16384,
# gc(11)@24576) was stack OVERFLOW past the stack bottom into libstub's
# unused _heap_buf in DGROUP — silent UB that happened to work.  Under the
# split there is no landing pad (overflow wraps SP into the far-data
# segments), so MICROPY_STACK_CHECK=1 (mpconfigport.h §4v) now raises a
# clean RuntimeError at the limit instead.
MP_STACK_SIZE=${MP_STACK_SIZE:-61440}
# §4v split stack (default ON): the stack gets its OWN segment (SS != DS)
# via qbe -s + omf_link --separate-stack + the libstub _dgroup_para DS
# reloads.  This removes the DGROUP cap described above: DGROUP keeps its
# full 64KB for near data+bss, and the stack can grow to the full 64KB SP
# range (65535) independent of near-data size.  Set MP_SPLIT_STACK=0 to
# fall back to the classic SS==DS layout (then the DGROUP cap applies).
MP_SPLIT_STACK=${MP_SPLIT_STACK:-1}
QBE_SPLIT_FLAG=""
LINK_SPLIT_FLAG=""
if [ "$MP_SPLIT_STACK" != "0" ]; then
	QBE_SPLIT_FLAG="-s"
	LINK_SPLIT_FLAG="--separate-stack"
fi
# Stack-check limit (REAL since §4v: MICROPY_STACK_CHECK=1).  mp_cstack_check
# raises RuntimeError when (stack_top - SP) >= limit.  Default leaves 8 KB of
# headroom under MP_STACK_SIZE: checks run at VM/parser entry, so up to one
# ~5.6 KB resume frame plus libstub/ISR transients can land beyond the check.
MP_STACK_LIMIT=${MP_STACK_LIMIT:-$((MP_STACK_SIZE - 8192))}
MP_HEAP_SIZE=${MP_HEAP_SIZE:-49152}
# §4z split heap: size of the SECOND GC area (gcheap2.c, its own far
# segment; mpconfigport.h defines MICROPY_GC_SPLIT_HEAP=1 and a matching
# default).  Total heap = MP_HEAP_SIZE + MP_HEAP2_SIZE.  Each area must
# stay under the 64 KB compact-model segment cap.
MP_HEAP2_SIZE=${MP_HEAP2_SIZE:-65024}
MP_DOS_TINY_STACK_CHECK=${MP_DOS_TINY_STACK_CHECK:-0}
MP_DOS_STACKLESS_RECURSION_RAISE=${MP_DOS_STACKLESS_RECURSION_RAISE:-0}
MP_EXTRA_CPPFLAGS=${MP_EXTRA_CPPFLAGS:-}
# Per-FUNCTION text segments (§4t): with budget=1, asm_to_omf splits .text at
# every function boundary, so omf_link --gc-sections strips each unreachable
# function individually (statics included) instead of whole-TU text blocks,
# and --pack-code re-packs the survivors back-to-back (word-aligned, no
# paragraph waste).  On the curated MicroPython link this cut code 703553 →
# 452461 bytes (-251 KB, -36%): the whole-TU granularity had been retaining
# every dead function in any partially-used TU (mpz, showbc, profile, ...).
# Only set here — the asm_to_omf default stays 56000 for builds that don't
# link with --gc-sections --pack-code (per-function segments without packing
# would ADD paragraph padding per function).
export QBE_TEXT_SEG_BUDGET=${QBE_TEXT_SEG_BUDGET:-1}

NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'

# Curated core subset: all py/*.c EXCEPT the other-architecture native-code
# emitters, inline assemblers, and NLR backends.  Those gate themselves out
# under our config (MICROPY_EMIT_NATIVE=0, MICROPY_NLR_SETJMP=1) and would
# only contribute empty objects or pull in arch symbols.  We keep nlr.c
# (generic dispatcher) and nlrsetjmp.c (our NLR backend).
DROP_RE='^(asmarm|asmbase|asmrv32|asmthumb|asmx64|asmx86|asmxtensa|emitinlinerv32|emitinlinethumb|emitinlinextensa|emitnarm|emitndebug|emitnrv32|emitnthumb|emitnx64|emitnx86|emitnxtensa|emitnxtensawin|nativeglue|nlraarch64|nlrmips|nlrpowerpc|nlrrv32|nlrrv64|nlrthumb|nlrx64|nlrx86|nlrxtensa)$'

CORE_SRCS=()
for f in "$MP"/py/*.c; do
	base=$(basename "$f" .c)
	[[ "$base" =~ $DROP_RE ]] && continue
	CORE_SRCS+=("$f")
done
PORT_SRCS=("$DOSPORT/main.c" "$DOSPORT/mphalport.c" "$DOSPORT/gcheap2.c")
# softfloat.c provides the single-precision soft-libm: the _sf_add/sub/mul/div
# arithmetic helpers the i8086 backend calls for Ks ops, plus the algebraic +
# transcendental libm surface (floorf/powf/...) MicroPython references under
# MICROPY_FLOAT_IMPL_FLOAT.  Always linked; --gc-sections dead-strips it
# entirely when MICROPY_FLOAT_IMPL is NONE (no _sf_*/sf_* references), so the
# image is byte-identical to a build without it in that case.
SOFTFLOAT_SRC=("$DOS_DIR/softfloat.c")
ALL_SRCS=("${CORE_SRCS[@]}" "${PORT_SRCS[@]}" "${SOFTFLOAT_SRC[@]}")

echo "=== Compiling ${#ALL_SRCS[@]} TUs (${#CORE_SRCS[@]} core + ${#PORT_SRCS[@]} port + ${#SOFTFLOAT_SRC[@]} softfloat) [model=$MODEL${FARSTATIC_FLAG:+, far-static-data}] ==="

pass_objs=()
fail=()
for f in "${ALL_SRCS[@]}"; do
	base=$(basename "$f" .c)
	pp="$OUT_DIR/$base.pp.c"
	ssa="$OUT_DIR/$base.ssa"
	asm="$OUT_DIR/$base.asm"
	omf="$OUT_DIR/$base.omf.asm"
	obj="$OUT_DIR/$base.obj"
	err="$OUT_DIR/$base.err"
	: > "$err"

	if ! clang -E -P -nostdinc -DDOS -D__TURBOC__ $FARDATA_DEF $MP_LIBC_DEFS \
			"-DMP_GC_HEAP2_SIZE=$MP_HEAP2_SIZE" \
			$MP_EXTRA_CPPFLAGS \
			"-I$DOSPORT" "-I$STUB" "-I$INC_DIR" "-I$MP" "-I$GENHDR" \
			"$f" 2>"$err" > "$OUT_DIR/$base.raw.c"; then
		fail+=("$base (cpp)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL cpp: $base"; cat "$err"; exit 1; }; continue
	fi
	tr -d '\r\032' < "$OUT_DIR/$base.raw.c" | sed "$NORMALIZE" > "$pp"
	if [ "$base" = "main" ] && [ "$MP_STACK_LIMIT" != "8192" ]; then
		sed "s/mp_stack_set_limit(8192);/mp_stack_set_limit($MP_STACK_LIMIT);/" "$pp" > "$pp.tmp"
		mv "$pp.tmp" "$pp"
	fi
	if [ "$base" = "main" ] && [ "$MP_HEAP_SIZE" != "49152" ]; then
		sed "s/static char heap\\[(49152)\\];/static char heap[($MP_HEAP_SIZE)];/" "$pp" > "$pp.tmp"
		mv "$pp.tmp" "$pp"
	fi
	if [ "$base" = "cstack" ] && [ "$MP_DOS_TINY_STACK_CHECK" != "0" ]; then
		sed '/^void mp_cstack_check(void) {/,/^}/c\
void mp_cstack_check(void) {\
    volatile int stack_dummy;\
    unsigned int top = (unsigned int)(mp_state_ctx.thread.stack_top);\
    unsigned int cur = (unsigned int)&stack_dummy;\
    if ((unsigned int)(top - cur) >= (unsigned int)(mp_state_ctx.thread.stack_limit)) {\
        mp_raise_recursion_depth();\
    }\
}' "$pp" > "$pp.tmp"
		mv "$pp.tmp" "$pp"
	fi
	if [ "$base" = "runtime" ] && [ "$MP_DOS_STACKLESS_RECURSION_RAISE" != "0" ]; then
		cat >> "$pp" <<'EOF'
__attribute__((noreturn)) void mp_raise_recursion_depth(void) {
    mp_raise_type_arg(&mp_type_RuntimeError, ((mp_obj_t)((((mp_uint_t)(MP_QSTR_maximum_space_recursion_space_depth_space_exceeded)) << 3) | 2)));
}
EOF
	fi

	if ! "$MINIC" -m "$MODEL" < "$pp" > "$ssa" 2>"$err"; then
		fail+=("$base (minic)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL minic: $base"; cat "$err"; exit 1; }; continue
	fi
	# Empty SSA: minic accepted but emitted nothing (arch-gated-out TU).  Skip.
	if [ ! -s "$ssa" ]; then continue; fi

	if ! "$QBE" -t i8086 -m "$MODEL" $QBE_SPLIT_FLAG "$ssa" > "$asm" 2>"$err"; then
		fail+=("$base (qbe)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL qbe: $base"; cat "$err"; exit 1; }; continue
	fi
	if ! "$QBE_DIR/tools/asm_to_omf.py" "--model=$MODEL" $FARSTATIC_FLAG "$base" "$asm" "$omf" 2>"$err"; then
		fail+=("$base (omf-wrap)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL omf-wrap: $base"; cat "$err"; exit 1; }; continue
	fi
	if ! nasm -w-label-redef-late -f obj "$omf" -o "$obj" 2>"$err"; then
		fail+=("$base (nasm)"); [ $KEEP_GOING -eq 0 ] && { echo "FAIL nasm: $base"; cat "$err"; exit 1; }; continue
	fi
	pass_objs+=("$obj")
done

echo "  compiled ${#pass_objs[@]} objects; ${#fail[@]} failed"
if [ ${#fail[@]} -gt 0 ]; then
	printf '    FAIL: %s\n' "${fail[@]}"
fi

# crt0 + libstub.  Far-data models need crt0_exe to build argv as 4-byte far
# pointers (the FAR_DATA-gated path in crt0_exe.asm).
CRT0_FLAGS=""
case "$MODEL" in
	compact|large|huge) CRT0_FLAGS="-DFAR_DATA=1" ;;
esac
nasm $CRT0_FLAGS -f obj "$DOS_DIR/crt0_exe.asm" -o "$OUT_DIR/crt0_exe.obj" 2>"$OUT_DIR/crt0.err" || {
	echo "FAIL crt0 nasm"; cat "$OUT_DIR/crt0.err"; exit 1; }

SUPPORT_OBJS=()
RUNTIME_OBJS=()
if [ "$NO_LIBSTUB" = 1 ]; then
	# §8c libstub-free MP: the §7r/§7v stevie recipe.  newlibc's portable
	# stdio + dos_libc.c libc fill compiled in newlibc's own regime (shiminc +
	# newlibc headers, clang -E -D__ia16__), meeting the MP TUs only at the
	# linker.  MP is far-data (compact/large/huge; FARSTATIC_FLAG set above)
	# or near-data medium — the runtime shape follows, mirroring
	# build-stevie.sh.  --gc-sections drops the FAT/block/printf code MP never
	# reaches.
	case "$MODEL" in
		tiny|small) echo "FAIL: --no-libstub needs medium or a far-data model (MP is not small)"; exit 1 ;;
	esac
	NL="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"
	[ -d "$NL" ] || { echo "FAIL: --no-libstub needs the newlibc tree: $NL"; exit 77; }
	SHIM="$DOS_DIR/newlibc/shiminc"
	NL_NORMALIZE="$NORMALIZE"
	NL_HALT2DOS='s/__asm__[[:space:]]*volatile[[:space:]]*([[:space:]]*"hlt"[[:space:]]*)/{ __asm__ volatile ("mov ax, 0x4c00"); __asm__ volatile ("int 0x21"); }/g'
	compile_newlibc_unit() {
		local unit_src="$1" unit_base="$2"
		clang -E -P -nostdinc -DDOS -D__ia16__ -DNO_LIBSTUB \
			"-I$SHIM" "-I$INC_DIR" \
			"-I$NL/include" "-I$NL/drivers" "-I$NL/libgloss" "-I$NL/vfs" \
			"$unit_src" 2>>"$OUT_DIR/nl.err" \
			| tr -d '\r\032' | sed "$NL_NORMALIZE" | sed "$NL_HALT2DOS" \
			> "$OUT_DIR/$unit_base.pp.c"
		"$MINIC" -m "$MODEL" < "$OUT_DIR/$unit_base.pp.c" \
			> "$OUT_DIR/$unit_base.ssa" 2>>"$OUT_DIR/nl.err"
		"$QBE" -t i8086 -m "$MODEL" "$OUT_DIR/$unit_base.ssa" \
			> "$OUT_DIR/$unit_base.nlasm" 2>>"$OUT_DIR/nl.err"
		"$QBE_DIR/tools/asm_to_omf.py" "--model=$MODEL" "$unit_base" \
			"$OUT_DIR/$unit_base.nlasm" "$OUT_DIR/$unit_base.nlomf.asm" 2>>"$OUT_DIR/nl.err"
		nasm -w-label-redef-late -f obj "$OUT_DIR/$unit_base.nlomf.asm" \
			-o "$OUT_DIR/$unit_base.obj" 2>>"$OUT_DIR/nl.err" || {
			echo "FAIL nasm-obj newlibc: $unit_base"; tail -20 "$OUT_DIR/nl.err"; exit 1; }
	}
	: > "$OUT_DIR/nl.err"
	NL_SUPPORT=(
		"$DOS_DIR/newlibc/dos_printf.c"     # §8b libstub-compatible %p/%o shadow
		"$NL/libgloss/scanf_wrappers.c"
		"$NL/libgloss/syscalls.c"
		"$NL/libgloss/reent_stubs.c"
		"$NL/libgloss/dirent.c"
		"$NL/libgloss/unlink.c"
		"$NL/libgloss/rename.c"
		"$DOS_DIR/newlibc/dos_vfs.c"        # real-DOS VFS (INT 21h), §7s
		"$DOS_DIR/newlibc/dos_shim.c"       # FILE layer + std streams + main wrapper
		"$DOS_DIR/newlibc/dos_libc.c"       # str/mem/ctype/malloc fill
	)
	for tu in "${NL_SUPPORT[@]}"; do
		compile_newlibc_unit "$tu" "$(basename "$tu" .c)"
		SUPPORT_OBJS+=("$OUT_DIR/$(basename "$tu" .c).obj")
	done
	# Runtime asm: qbe_rt (_qbe_* helpers) + dos_syscall (INT 21h) +
	# far_stdlib_bridge (_far_X -> _X tail calls) + setjmp_rt + heap +
	# builtins_rt (§8c ___builtin_clz/clzl/expect/unreachable).  Three shapes
	# mirror build-stevie.sh; builtins_rt rides with qbe_rt (near raw / far via
	# near_to_far_rt.py) since its bodies are NEAR form with [bp+N] args.
	MP_DOSLIBC_HEAP_SIZE=${MP_DOSLIBC_HEAP_SIZE:-8192}
	nasm "-DHEAP_SIZE=$MP_DOSLIBC_HEAP_SIZE" -f obj "$DOS_DIR/heap.asm" \
		-o "$OUT_DIR/heap.obj" 2>>"$OUT_DIR/nl.err" || { echo "FAIL heap nasm"; exit 1; }
	if [ "$MODEL" = medium ]; then
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=QBE_RT_TEXT "$DOS_DIR/qbe_rt.asm" "$OUT_DIR/qbe_rt_far.asm" 2>>"$OUT_DIR/nl.err"
		nasm -f obj "$OUT_DIR/qbe_rt_far.asm" -o "$OUT_DIR/qbe_rt.obj" 2>>"$OUT_DIR/nl.err"
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=DOS_SYSCALL_TEXT "$DOS_DIR/dos_syscall.asm" "$OUT_DIR/dos_syscall_far.asm" 2>>"$OUT_DIR/nl.err"
		nasm -f obj "$OUT_DIR/dos_syscall_far.asm" -o "$OUT_DIR/dos_syscall.obj" 2>>"$OUT_DIR/nl.err"
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=BUILTINS_RT_TEXT "$DOS_DIR/builtins_rt.asm" "$OUT_DIR/builtins_rt_far.asm" 2>>"$OUT_DIR/nl.err"
		nasm -f obj "$OUT_DIR/builtins_rt_far.asm" -o "$OUT_DIR/builtins_rt.obj" 2>>"$OUT_DIR/nl.err"
		nasm -dSJ_FAR_CODE -f obj "$DOS_DIR/setjmp_rt.asm" -o "$OUT_DIR/setjmp_rt.obj" 2>>"$OUT_DIR/nl.err"
		RUNTIME_OBJS=("$OUT_DIR/qbe_rt.obj" "$OUT_DIR/dos_syscall.obj" "$OUT_DIR/builtins_rt.obj" "$OUT_DIR/setjmp_rt.obj" "$OUT_DIR/heap.obj")
	else
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=QBE_RT_TEXT "$DOS_DIR/qbe_rt.asm" "$OUT_DIR/qbe_rt_far.asm" 2>>"$OUT_DIR/nl.err"
		nasm -f obj "$OUT_DIR/qbe_rt_far.asm" -o "$OUT_DIR/qbe_rt.obj" 2>>"$OUT_DIR/nl.err"
		nasm -f obj "$DOS_DIR/dos_syscall_far_data.asm" -o "$OUT_DIR/dos_syscall.obj" 2>>"$OUT_DIR/nl.err"
		nasm -f obj "$DOS_DIR/far_stdlib_bridge.asm" -o "$OUT_DIR/far_stdlib_bridge.obj" 2>>"$OUT_DIR/nl.err"
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=BUILTINS_RT_TEXT "$DOS_DIR/builtins_rt.asm" "$OUT_DIR/builtins_rt_far.asm" 2>>"$OUT_DIR/nl.err"
		nasm -f obj "$OUT_DIR/builtins_rt_far.asm" -o "$OUT_DIR/builtins_rt.obj" 2>>"$OUT_DIR/nl.err"
		nasm -dSJ_FAR_DATA -f obj "$DOS_DIR/setjmp_rt.asm" -o "$OUT_DIR/setjmp_rt.obj" 2>>"$OUT_DIR/nl.err"
		RUNTIME_OBJS=("$OUT_DIR/qbe_rt.obj" "$OUT_DIR/dos_syscall.obj" "$OUT_DIR/far_stdlib_bridge.obj" "$OUT_DIR/builtins_rt.obj" "$OUT_DIR/setjmp_rt.obj" "$OUT_DIR/heap.obj")
	fi
	echo "  libstub-free runtime: ${#SUPPORT_OBJS[@]} support + ${#RUNTIME_OBJS[@]} runtime objs"
else
	"$QBE_DIR/tools/libstub_to_exe.py" "--model=$MODEL" \
		"$DOS_DIR/libstub.asm" "$OUT_DIR/libstub_exe.asm" 2>"$OUT_DIR/libstub.err" || {
		echo "FAIL libstub conv"; cat "$OUT_DIR/libstub.err"; exit 1; }
	nasm -f obj "$OUT_DIR/libstub_exe.asm" -o "$OUT_DIR/libstub_exe.obj" 2>>"$OUT_DIR/libstub.err" || {
		echo "FAIL libstub nasm"; cat "$OUT_DIR/libstub.err"; exit 1; }
	RUNTIME_OBJS=("$OUT_DIR/libstub_exe.obj")
fi

echo "=== Linking ==="
OBJS=("$OUT_DIR/crt0_exe.obj" "${pass_objs[@]}" ${SUPPORT_OBJS[@]+"${SUPPORT_OBJS[@]}"} "${RUNTIME_OBJS[@]}")
printf '%s\n' "${OBJS[@]}" > /tmp/mp_objs.txt
# --gc-sections dead-strips CODE/FAR_DATA segments unreachable from _start
# (the standard linker --gc-sections model, sound here because every
# cross-segment dependency is an OMF fixup).  This is the biggest size lever:
# print(1+2) touches a small fraction of the curated core, so the whole image
# drops well under the ~896KB Victor 9000 ceiling.  See NEXT_SESSION.md §2b.
# --pack-code coalesces the gc-surviving per-function CODE segments back into a
# few <=64KB buckets, reclaiming the per-function paragraph padding (~5KB on the
# core subset — see NEXT_SESSION.md §2p).  Safe because every code reference is
# an offset-aware OMF fixup and near jumps stay intra-function.
# The VM recurses through C frames for generator resumes; 8KB corrupted the
# return path at recsum(8) on Victor.  The stack cap is DGROUP (see the
# MP_STACK_SIZE comment above), not image size.
if "$QBE_DIR/tools/omf_link.py" \
		-o "$OUT_DIR/mpython.exe" \
		--map "$OUT_DIR/mpython.map" \
		--entry _start \
		--stack-size "$MP_STACK_SIZE" \
		$LINK_SPLIT_FLAG \
		--gc-sections \
		--pack-code \
		"${OBJS[@]}" 2>"$OUT_DIR/link.err"; then
	echo "  OK: $OUT_DIR/mpython.exe ($(wc -c <"$OUT_DIR/mpython.exe") bytes)"
else
	echo "  LINK FAILED — see $OUT_DIR/link.err:"
	tail -40 "$OUT_DIR/link.err"
	exit 1
fi
