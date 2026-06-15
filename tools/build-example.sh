#!/bin/bash
# Build a DOS example .EXE from minic/dos/examples/ (or any path).
# Modeled on tools/build-int86x-probe.sh, parameterized by source file.
#
# Usage: tools/build-example.sh path/to/source.c [path/to/extra.c ...]
# Output: build/examples/<name>/<name>.exe

set -eu

MODEL="medium"
SOFTFLOAT=0
SPLITSTACK=0
# --no-libstub (§7q, Phase-6 libstub retirement): build a NORMAL (non-newlibc)
# minic program libstub-free.  Where build-newlibc-test.sh --no-libstub does
# this for the newlibc test tree, this does it for a program compiled in the
# ordinary build-example regime (its own main, minic/include headers) — the
# path stevie and plain examples take.  The program's printf/str/mem/malloc
# resolve to newlibc's portable stdio (printf -> _write -> VFS -> dos_shim
# INT 21h) + the minic-compiled dos_libc.c fill + the qbe_rt/dos_syscall/heap
# runtime, NOT libstub's python printf engine.  See §7n/§7o/§7p.
NO_LIBSTUB=0
SOURCES=()
for arg in "$@"; do
	case "$arg" in
		--model=*) MODEL="${arg#--model=}" ;;
		--softfloat) SOFTFLOAT=1 ;;
		--split-stack) SPLITSTACK=1 ;;
		--no-libstub) NO_LIBSTUB=1 ;;
		-h|--help)
			echo "usage: $0 [--model=<tiny|small|medium|compact|large|huge>] [--softfloat] [--split-stack] [--no-libstub] <source.c> [extra.c ...]" >&2
			exit 0 ;;
		--*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
		*) SOURCES+=("$arg") ;;
	esac
done

# The libstub-free path supports small (qbe_rt/dos_syscall assembled raw in
# near form) and medium (the two pure-code runtime TUs rewritten to the
# far-call ABI by tools/near_to_far_rt.py — the compiler far-calls them under
# medium).  Far-DATA models (compact/large/huge) would need far_stdlib-aware
# newlibc stdio + a far-pointer libc fill — a later step (§7q handoff).
# §7t widened the libstub-free far-DATA path to compact + large (far code +
# far data; the int86/intdos/segread family mangles to _far_* and reads its
# register structs through far pointers — dos_syscall_far_data.asm — and the
# stdio/str/mem stdlib calls mangle to _far_X, bridged by far tail-calls into
# newlibc/dos_libc's plain symbols — far_stdlib_bridge.asm).  huge is NOT yet
# supported libstub-free: printf and the far bridges work, but malloc fails —
# newlibc's _sbrk brackets the heap with `next_heap > __heap_end`, a huge-model
# pointer compare against the UNNORMALIZED __heap_end symbol address, which the
# huge-pointer path mis-evaluates (the §7g/§4s huge-normalization family).  The
# huge *libstub* build is unaffected (libstub's own malloc).  tiny is .COM-only.
if [ "$NO_LIBSTUB" = 1 ]; then
	case "$MODEL" in
		tiny)  echo "$0: --no-libstub does not support --model=tiny" >&2; exit 2 ;;
		huge)  echo "$0: --no-libstub does not yet support --model=huge (malloc/_sbrk huge-pointer heap-compare; printf path works, see §7t)" >&2; exit 2 ;;
	esac
fi

# --split-stack: SS gets its own segment (SS != DS).  Far-data models only:
# qbe -s adds ss: overrides on register-indirect near derefs and omf_link
# --separate-stack points the MZ header's SS at the STACK segment itself.
QBE_SPLIT_FLAG=""
LINK_SPLIT_FLAG=""
if [ "$SPLITSTACK" = "1" ]; then
	case "$MODEL" in
		compact|large|huge) ;;
		*) echo "$0: --split-stack requires --model=compact/large/huge" >&2; exit 2 ;;
	esac
	QBE_SPLIT_FLAG="-s"
	LINK_SPLIT_FLAG="--separate-stack"
fi

if [ "${#SOURCES[@]}" -eq 0 ]; then
	echo "usage: $0 [--model=<m>] <source.c> [extra.c ...]" >&2
	exit 2
fi

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
for i in "${!SOURCES[@]}"; do
	src="${SOURCES[$i]}"
	if [ ! -f "$src" ]; then
		src="$QBE_DIR/$src"
	fi
	if [ ! -f "$src" ]; then
		echo "$0: cannot find source file: ${SOURCES[$i]}" >&2
		exit 2
	fi
	SOURCES[$i]="$src"
done

SRC="${SOURCES[0]}"
base="$(basename "$SRC" .c)"
OUT_DIR="$QBE_DIR/build/examples/$base"
MINIC="$QBE_DIR/minic/minic"
INC_DIR="$QBE_DIR/minic/include"
QBE="$QBE_DIR/qbe"
DOS_DIR="$QBE_DIR/minic/dos"

NORMALIZE_TYPES='
s/\bunsigned short int\b/unsigned short/g
s/\bunsigned long int\b/unsigned long/g
s/\bsigned short int\b/short/g
s/\bsigned long int\b/long/g
s/\blong long int\b/long long/g
s/\blong int\b/long/g
s/\bshort int\b/short/g
s/\bsigned char\b/char/g
s/\bsigned short\b/short/g
s/\bsigned long long\b/long long/g
s/\bsigned long\b/long/g
s/\bsigned int\b/int/g
s/\bsigned\b//g
'

mkdir -p "$OUT_DIR"
ERR="$OUT_DIR/build.err"
: > "$ERR"

# compile_unit <source.c> <base>: run stages 1-4 (cpp → minic → qbe → asm
# normalize → OMF wrap → nasm) on one C translation unit, producing
# "$OUT_DIR/<base>.obj".  Used for the main source and (with --softfloat) for
# the soft-float helper library.
compile_unit() {
	local unit_src="$1" unit_base="$2"
	local pp asm_clean prefix

# Stage 1: C → preprocessed → SSA.  EXAMPLE_DEFS is empty for an ordinary
# build; under --no-libstub it carries -Dmain=newlibc_test_main so the
# program's main() is renamed and dos_shim.c's main() (which runs vfs_init()
# before tail-calling it) sequences the VFS/console bring-up first.
pp="$OUT_DIR/$unit_base.pp.c"
cpp -P -nostdinc -isysroot/var/empty -DDOS -D__TURBOC__ ${EXAMPLE_DEFS:-} \
	"-I$INC_DIR" "-I$(dirname "$unit_src")" \
	"$unit_src" 2>>"$ERR" | tr -d '\r\032' | sed "$NORMALIZE_TYPES" > "$pp"
"$MINIC" -m "$MODEL" < "$pp" > "$OUT_DIR/$unit_base.ssa" 2>>"$ERR"

# Stage 2: SSA → ASM
"$QBE" -t i8086 -m "$MODEL" $QBE_SPLIT_FLAG "$OUT_DIR/$unit_base.ssa" > "$OUT_DIR/$unit_base.asm" 2>>"$ERR"

# Stage 3: ASM normalize (same sed/awk/perl pipeline as build-int86x-probe.sh).
prefix="${unit_base}_"
asm_clean="$OUT_DIR/$unit_base.nasm.asm"
grep -v -E '^\.(text|data|bss|balign|section|globl|type|size|local|file|ident|string|p2align|model|code)' \
		"$OUT_DIR/$unit_base.asm" \
	| sed -e 's/; TODO: 32-bit op [0-9]*/; XXX 32-bit op stub - codegen incomplete/' \
	      -e 's/^[[:space:]]*\.ascii "\(.*\)"$/.nasm_str \1/' \
	| awk '
		/^\.nasm_str / {
			s = substr($0, length(".nasm_str ")+1)
			out = ""
			for (n = 1; n <= length(s); n++) {
				ch = substr(s, n, 1)
				if (ch == "`") out = out "\\`"
				else out = out ch
			}
			print "db `" out "`"
			next
		}
		{ print }
	' \
	| perl -pe '
		BEGIN { $p = "'"$prefix"'"; }
		s/^(l\d+(?:_l\d+)?):/${p}$1:/;
		s/^(\s*j[a-z]+\s+)(l\d+(?:_l\d+)?)\b/$1${p}$2/;
		s/^(\s*jnz\s+[^,]+,\s*)(l\d+(?:_l\d+)?)(\s*,\s*)(l\d+(?:_l\d+)?)\b/$1${p}$2$3${p}$4/;
		s/^_?glo(\d+):/${p}glo$1:/;
		s/\b_?glo(\d+)\b/${p}glo$1/g;
	' \
	| sed -e 's/word ptr \([a-z][a-z]:\)\[/word \1[/g' \
	      -e 's/byte ptr \([a-z][a-z]:\)\[/byte \1[/g' \
	      -e 's/dword ptr \([a-z][a-z]:\)\[/dword \1[/g' \
	      -e 's/word ptr \[/word [/g' \
	      -e 's/byte ptr \[/byte [/g' \
	      -e 's/dword ptr \[/dword [/g' \
	      -e 's|/\* \(.*\) \*/|; \1|' \
	      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) proc near$/\1:/' \
	      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) proc far$/\1:/' \
	      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) endp$/; \1 endp/' \
	      -e '/^:$/d' \
	      -e 's/^[[:space:]]*\.byte \(.*\)$/db \1/' \
	      -e 's/^[[:space:]]*\.short \(.*\)$/dw \1/' \
	      -e 's/^[[:space:]]*\.long \(.*\)$/dd \1/' \
	      -e 's/^[[:space:]]*\.int \(.*\)$/dw \1/' \
	      -e 's/^[[:space:]]*\.word \(.*\)$/dw \1/' \
	      -e 's/^[[:space:]]*\.quad \(.*\)$/dq \1/' \
	      -e 's/^[[:space:]]*\.zero \(.*\)$/times \1 db 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\),1,0$/times \1 db 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\),2,0$/times \1 dw 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\),4,0$/times \1 dd 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\)$/times \1 db 0/' \
	      -e '/^[[:space:]]*\.section/d' \
	      -e '/^[[:space:]]*\.balign/d' \
	      -e '/^[[:space:]]*\.local/d' \
	      -e '/^[[:space:]]*\.globl/d' \
	      -e '/^[[:space:]]*\.type/d' \
	      -e '/^[[:space:]]*\.size/d' \
	      -e '/^[[:space:]]*\.text/d' \
	      -e '/^[[:space:]]*\.data/d' \
	      -e '/^[[:space:]]*\.bss/d' \
	      -e '/^[[:space:]]*$/d' \
	| perl -pe '
		s/^(\s*test\s+)(es|ds|cs|ss),\s*\g{2}\b/$1ax, ax ; XXX was test $2,$2/g;
	' \
	> "$asm_clean"

# Stage 4: OMF wrap and assemble.  Under a far-data model, opting in with
# QBE_FAR_STATIC_DATA=1 routes this module's statics into its own far
# `<BASE>_DATA`/`<BASE>_BSS` segment (outside DGROUP) so total static data
# can exceed 64KB.  Off by default — see NEXT_SESSION.md (the far-global
# direct-access codegen isn't complete yet, so it's opt-in for now).
FARSTATIC_FLAG=""
if [ "${QBE_FAR_STATIC_DATA:-0}" = "1" ]; then
	FARSTATIC_FLAG="--far-static-data"
fi
"$QBE_DIR/tools/asm_to_omf.py" "--model=$MODEL" $FARSTATIC_FLAG "$unit_base" \
	"$OUT_DIR/$unit_base.asm" "$OUT_DIR/$unit_base.omf.asm" 2>>"$ERR"
nasm -w-label-redef-late -f obj "$OUT_DIR/$unit_base.omf.asm" \
	-o "$OUT_DIR/$unit_base.obj" 2>>"$ERR"
}

# --no-libstub: the program's main() is renamed so dos_shim.c's main() runs
# vfs_init() before tail-calling it (printf can't reach the console until the
# VFS device table is up).  EXAMPLE_DEFS is threaded into every example TU's
# cpp (harmless on TUs without a main); empty for an ordinary libstub build.
EXAMPLE_DEFS=""
[ "$NO_LIBSTUB" = 1 ] && EXAMPLE_DEFS="-Dmain=newlibc_test_main"

# Compile the requested translation units.  The first source controls the
# output directory and executable basename; every source contributes one .obj.
OBJ_FILES=()
for unit_src in "${SOURCES[@]}"; do
	unit_base="$(basename "$unit_src" .c)"
	compile_unit "$unit_src" "$unit_base"
	OBJ_FILES+=("$OUT_DIR/$unit_base.obj")
done

# Optionally compile the soft-float helper library (single-precision Ks ops are
# lowered by the i8086 backend to `call far _sf_*`; this provides those symbols).
LINK_OBJS=("${OBJ_FILES[@]}")
if [ "$SOFTFLOAT" = "1" ]; then
	compile_unit "$DOS_DIR/softfloat.c" softfloat
	LINK_OBJS+=("$OUT_DIR/softfloat.obj")
fi

# --no-libstub: compile newlibc's portable stdio stack (printf -> _write ->
# VFS -> dos_shim INT 21h) + the dos_libc.c libc fill, and append them to the
# link.  These TUs are compiled in newlibc's own regime (shiminc + newlibc
# headers, -D__ia16__, clang -E) — distinct from the example's ordinary
# build-example regime above; the two meet only at the linker (via _printf,
# _write, _vfs_*, _newlibc_test_main).  Mirrors build-newlibc-test.sh's
# compile_unit + SUPPORT_TUS; --gc-sections drops the FAT/block code the
# example never reaches.
if [ "$NO_LIBSTUB" = 1 ]; then
	NL="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"
	# Exit 77 (not 2) when the newlibc tree is absent: the portable stdio
	# stack lives there, so the gate's prep() treats this as [skip], the same
	# graceful degradation as the build-newlibc-test.sh entries.
	[ -d "$NL" ] || { echo "newlibc tree not found: $NL" >&2; exit 77; }
	SHIM="$DOS_DIR/newlibc/shiminc"
	NL_NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'
	NL_HALT2DOS='s/__asm__[[:space:]]*volatile[[:space:]]*([[:space:]]*"hlt"[[:space:]]*)/{ __asm__ volatile ("mov ax, 0x4c00"); __asm__ volatile ("int 0x21"); }/g'
	# compile_newlibc_unit <source.c> <obj-base>
	compile_newlibc_unit() {
		local unit_src="$1" unit_base="$2"
		clang -E -P -nostdinc -DDOS -D__ia16__ -DNO_LIBSTUB \
			"-I$SHIM" "-I$INC_DIR" \
			"-I$NL/include" "-I$NL/drivers" "-I$NL/libgloss" "-I$NL/vfs" \
			"$unit_src" 2>>"$ERR" \
			| tr -d '\r\032' | sed "$NL_NORMALIZE" | sed "$NL_HALT2DOS" \
			> "$OUT_DIR/$unit_base.pp.c"
		"$MINIC" -m "$MODEL" < "$OUT_DIR/$unit_base.pp.c" \
			> "$OUT_DIR/$unit_base.ssa" 2>>"$ERR"
		"$QBE" -t i8086 -m "$MODEL" "$OUT_DIR/$unit_base.ssa" \
			> "$OUT_DIR/$unit_base.asm" 2>>"$ERR"
		"$QBE_DIR/tools/asm_to_omf.py" "--model=$MODEL" "$unit_base" \
			"$OUT_DIR/$unit_base.asm" "$OUT_DIR/$unit_base.omf.asm" 2>>"$ERR"
		nasm -w-label-redef-late -f obj "$OUT_DIR/$unit_base.omf.asm" \
			-o "$OUT_DIR/$unit_base.obj" 2>>"$ERR"
	}
	NL_SUPPORT=(
		"$NL/libgloss/printf_wrappers.c"
		"$NL/libgloss/scanf_wrappers.c"
		"$NL/libgloss/syscalls.c"
		"$NL/libgloss/reent_stubs.c"
		"$NL/libgloss/dirent.c"
		"$NL/libgloss/unlink.c"
		"$NL/libgloss/rename.c"
		"$DOS_DIR/newlibc/dos_vfs.c"
		"$DOS_DIR/newlibc/dos_shim.c"
		"$DOS_DIR/newlibc/dos_libc.c"
	)
	for tu in "${NL_SUPPORT[@]}"; do
		tu_base="$(basename "$tu" .c)"
		compile_newlibc_unit "$tu" "$tu_base"
		LINK_OBJS+=("$OUT_DIR/$tu_base.obj")
	done
fi

# Stage 5: crt0_exe.obj + the runtime.
# Far-data models need crt0_exe to build argv as 4-byte far ptrs to
# match main()'s `char *argv[]` parameter ABI.
CRT0_FLAGS=""
case "$MODEL" in
	compact|large|huge) CRT0_FLAGS="-DFAR_DATA=1" ;;
	small|tiny) CRT0_FLAGS="-DNEAR_CODE=1" ;;
esac
nasm $CRT0_FLAGS -f obj "$DOS_DIR/crt0_exe.asm" -o "$OUT_DIR/crt0_exe.obj" 2>>"$ERR"

RUNTIME_OBJS=()
if [ "$NO_LIBSTUB" = 1 ]; then
	# §7q: NO libstub.  The _qbe_* compiler runtime (qbe_rt.asm) and the
	# INT 21h primitives (dos_syscall.asm) come from standalone pure-code
	# TUs (verbatim copies of the libstub.asm routines; libstub.asm itself
	# is untouched).  small: near form, assembled raw.  medium: the compiler
	# far-calls them, so near_to_far_rt.py rewrites each to the far ABI
	# (ret->retf, [bp+N]->[bp+N+2], unique far-code segment).  heap.asm is
	# the BSS heap dos_libc.c's malloc carves from via newlibc's _sbrk;
	# --gc-sections drops it from a program that never reaches malloc.
	if [ "$MODEL" = small ]; then
		nasm -f obj "$DOS_DIR/qbe_rt.asm" -o "$OUT_DIR/qbe_rt.obj" 2>>"$ERR"
		nasm -f obj "$DOS_DIR/dos_syscall.asm" -o "$OUT_DIR/dos_syscall.obj" 2>>"$ERR"
		RUNTIME_OBJS=("$OUT_DIR/qbe_rt.obj" "$OUT_DIR/dos_syscall.obj" "$OUT_DIR/heap.obj")
	elif [ "$MODEL" = medium ]; then
		# medium: far code, NEAR data — qbe_rt/dos_syscall are far-called
		# (near_to_far_rt.py) but still take near-pointer args; no far_stdlib
		# mangling (NEAR_DATA), so the example calls newlibc's plain _printf.
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=QBE_RT_TEXT \
			"$DOS_DIR/qbe_rt.asm" "$OUT_DIR/qbe_rt_far.asm" 2>>"$ERR"
		nasm -f obj "$OUT_DIR/qbe_rt_far.asm" -o "$OUT_DIR/qbe_rt.obj" 2>>"$ERR"
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=DOS_SYSCALL_TEXT \
			"$DOS_DIR/dos_syscall.asm" "$OUT_DIR/dos_syscall_far.asm" 2>>"$ERR"
		nasm -f obj "$OUT_DIR/dos_syscall_far.asm" -o "$OUT_DIR/dos_syscall.obj" 2>>"$ERR"
		RUNTIME_OBJS=("$OUT_DIR/qbe_rt.obj" "$OUT_DIR/dos_syscall.obj" "$OUT_DIR/heap.obj")
	else
		# §7t far-DATA models (compact/large/huge): far code AND far data.
		# qbe_rt is still far-code (near_to_far_rt.py).  But the INT 21h
		# primitives need the far-POINTER ABI — int86/intdos/segread mangle to
		# _far_int86/... (far_stdlib), so link dos_syscall_far_data.asm (the
		# ES:BX-far-pointer wrappers) instead of dos_syscall.asm's near forms.
		# far_stdlib_bridge.asm tail-calls newlibc/dos_libc's plain _printf/
		# _strcpy/... under their mangled _far_ names; --gc-sections drops the
		# bridges the program doesn't call.
		"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=QBE_RT_TEXT \
			"$DOS_DIR/qbe_rt.asm" "$OUT_DIR/qbe_rt_far.asm" 2>>"$ERR"
		nasm -f obj "$OUT_DIR/qbe_rt_far.asm" -o "$OUT_DIR/qbe_rt.obj" 2>>"$ERR"
		nasm -f obj "$DOS_DIR/dos_syscall_far_data.asm" \
			-o "$OUT_DIR/dos_syscall.obj" 2>>"$ERR"
		nasm -f obj "$DOS_DIR/far_stdlib_bridge.asm" \
			-o "$OUT_DIR/far_stdlib_bridge.obj" 2>>"$ERR"
		RUNTIME_OBJS=("$OUT_DIR/qbe_rt.obj" "$OUT_DIR/dos_syscall.obj" \
			"$OUT_DIR/far_stdlib_bridge.obj" "$OUT_DIR/heap.obj")
	fi
	nasm -f obj "$DOS_DIR/heap.asm" -o "$OUT_DIR/heap.obj" 2>>"$ERR"
else
	"$QBE_DIR/tools/libstub_to_exe.py" "--model=$MODEL" \
		"$DOS_DIR/libstub.asm" "$OUT_DIR/libstub_exe.asm" 2>>"$ERR"
	nasm -f obj "$OUT_DIR/libstub_exe.asm" -o "$OUT_DIR/libstub_exe.obj" 2>>"$ERR"
	RUNTIME_OBJS=("$OUT_DIR/libstub_exe.obj")
fi

# --no-libstub adds --gc-sections so the FAT/block stdio code the program
# never reaches (and the heap, if it never mallocs) is stripped.
GC_FLAG=""
[ "$NO_LIBSTUB" = 1 ] && GC_FLAG="--gc-sections"

# Stage 6: Link
"$QBE_DIR/tools/omf_link.py" \
	-o "$OUT_DIR/$base.exe" \
	--map "$OUT_DIR/$base.map" \
	--entry _start \
	--stack-size 8192 \
	$GC_FLAG \
	$LINK_SPLIT_FLAG \
	"$OUT_DIR/crt0_exe.obj" \
	"${LINK_OBJS[@]}" \
	"${RUNTIME_OBJS[@]}" 2>>"$ERR"

echo "  OK: $OUT_DIR/$base.exe ($(wc -c <"$OUT_DIR/$base.exe") bytes)"
