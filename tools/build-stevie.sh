#!/bin/bash
# Build script for stevie-dos: minic → QBE i8086 backend → NASM → DOS .COM/.EXE
#
# Stages:
#   1. C   → SSA   via minic_cpp_v2 (preprocessor + minic)
#   2. SSA → ASM   via qbe -t i8086
#   3. ASM → OBJ   via nasm -f obj
#   4. Link        crt0 + sources + doslib → stevie.com (or .exe)
#
# Usage: tools/build-stevie.sh [--keep-going]
#
# With --keep-going, continues past per-file failures and reports the
# final list at the end.  Without it, stops on the first failure.

set -u
KEEP_GOING=0
MODEL=""             # empty until explicitly set; defaulted below based on EXE
EXE=0                # 1 → produce a .EXE via OMF objs + omf_link.py.
NO_LIBSTUB=0         # 1 → §7r: link newlibc stdio + dos_libc.c fill, NOT libstub.
for arg in "$@"; do
	case "$arg" in
		--keep-going) KEEP_GOING=1 ;;
		--model=*) MODEL="${arg#--model=}" ;;
		--exe) EXE=1 ;;
		--com) EXE=0 ;;
		--no-libstub) NO_LIBSTUB=1 ;;
		*) echo "unknown arg '$arg'" >&2; exit 1 ;;
	esac
done

# Default model: small for .COM (flat-loaded), medium for .EXE.
# The .EXE pipeline (crt0_exe.asm + libstub_to_exe.py) uses the far-call
# ABI between crt0 and main, which only matches medium-or-larger memory
# models.  Small-model .EXE compiles `main` with a near `ret`, which
# pops only the IP half of crt0's `call far _main` return address — the
# leftover CS sends the CPU to a wild address on main's return
# (manifesting as DOSBox "Illegal GRP4 Call 5" or a silent immediate
# quit).  Default to medium to keep the .EXE build coherent.
if [ -z "$MODEL" ]; then
	if [ $EXE -eq 1 ]; then
		MODEL="medium"
	else
		MODEL="small"
	fi
fi

# Medium and above can't be flat-loaded as .COM; force .EXE pipeline.
case "$MODEL" in
	medium|compact|large|huge) EXE=1 ;;
esac

# §7r: --no-libstub links newlibc's portable stdio (printf -> _write -> VFS ->
# dos_shim INT 21h) + the minic-compiled dos_libc.c libc fill + the
# qbe_rt/dos_syscall/heap runtime INSTEAD of libstub — the same path
# build-example.sh --no-libstub uses, applied to the full stevie editor.  It is
# an .EXE-only path (omf_link with the qbe_rt far-call runtime) and currently
# supports small + medium (far-DATA models would need far_stdlib stdio + a
# far-pointer libc fill, a later step).
if [ "$NO_LIBSTUB" = 1 ]; then
	EXE=1
	if [ "$MODEL" != small ] && [ "$MODEL" != medium ]; then
		echo "$0: --no-libstub currently requires --model=small|medium" >&2
		exit 2
	fi
fi

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$QBE_DIR/stevie-orig"
OUT_DIR="$QBE_DIR/build/stevie-orig"
MINIC="$QBE_DIR/minic/minic"
INC_DIR="$QBE_DIR/minic/include"
QBE="$QBE_DIR/qbe"
DOS_DIR="$QBE_DIR/minic/dos"
STEVIE_STACK_SIZE="${STEVIE_STACK_SIZE:-4096}"
# §7r: heap for the libstub-free build (dos_libc malloc carves from heap.asm's
# BSS heap via newlibc _sbrk).  Shares the 64KB DGROUP with statics + stack, so
# it is bounded; stevie keeps edited files in malloc'd lines, so it wants more
# than the malloc_probe's 8KB default.  Sized after the DGROUP budget is known.
STEVIE_HEAP_SIZE="${STEVIE_HEAP_SIZE:-32768}"
STEVIE_CPPFLAGS="${STEVIE_CPPFLAGS:-}"
EXTRA_CPPFLAGS=()
if [ -n "$STEVIE_CPPFLAGS" ]; then
	read -r -a EXTRA_CPPFLAGS <<< "$STEVIE_CPPFLAGS"
fi

# §7r: under --no-libstub, rename stevie's main() so dos_shim.c's main() runs
# vfs_init() before tail-calling it (printf can't reach the console until the
# VFS device table is up — the build-example.sh EXAMPLE_DEFS pattern).  Threaded
# into every stevie TU's cpp (harmless on TUs without a main); empty otherwise.
STEVIE_DEFS=""
[ "$NO_LIBSTUB" = 1 ] && STEVIE_DEFS="-Dmain=newlibc_test_main"

# Type/decl normalization sed scripts shared with minic_cpp_v2.
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

# Files compiled in order. From original DOS_MSC.MK; tos/os2/minix/unix
# are platform alternates and are skipped.
SOURCES=(
	alloc.c
	cmdline.c
	dos.c
	edit.c
	enveval.c
	fileio.c
	help.c
	hexchars.c
	linefunc.c
	main.c
	mark.c
	misccmds.c
	normal.c
	ops.c
	param.c
	ptrfunc.c
	regexp.c
	regsub.c
	screen.c
	search.c
	sentence.c
	tagcmd.c
	undo.c
	version.c
)

stage_pass=()
stage_fail_minic=()
stage_fail_qbe=()

for src in "${SOURCES[@]}"; do
	base="${src%.c}"
	ssa="$OUT_DIR/$base.ssa"
	asm="$OUT_DIR/$base.asm"
	obj="$OUT_DIR/$base.obj"
	err="$OUT_DIR/$base.err"

	# Stage 1: C → SSA.  Use cpp with -nostdinc -I minic/include so that
	# we use our stub system headers rather than the host's macOS SDK.
	# Define DOS so env.h selects the right platform branch.
	pp="$OUT_DIR/$base.pp.c"
	# `-isysroot /var/empty` defeats macOS clang's automatic SDK
	# fallback so that `-nostdinc` actually blocks the host system
	# headers.  Without it, <sys/types.h> would still resolve to the
	# Xcode SDK.
	# Stevie's original sources have CRLF line endings and the
	# DOS-style 0x1a (Ctrl-Z, SUB) end-of-file marker.  cpp passes
	# both through; minic's lexer treats 0x1a as a stray byte and
	# silently bails.  Strip both before handing off.
	if ! cpp -P -nostdinc -isysroot/var/empty -DDOS -D__TURBOC__ $STEVIE_DEFS \
			${EXTRA_CPPFLAGS[@]+"${EXTRA_CPPFLAGS[@]}"} \
			"-I$INC_DIR" "-I$SRC_DIR" \
			"$SRC_DIR/$src" 2>"$err" \
			| tr -d '\r\032' \
			| sed "$NORMALIZE_TYPES" > "$pp"; then
		stage_fail_minic+=("$src (cpp)")
		[ $KEEP_GOING -eq 0 ] && { echo "FAIL cpp: $src"; cat "$err"; exit 1; }
		continue
	fi
	if ! "$MINIC" -m "$MODEL" < "$pp" > "$ssa" 2>"$err"; then
		stage_fail_minic+=("$src")
		[ $KEEP_GOING -eq 0 ] && { echo "FAIL minic: $src"; cat "$err"; exit 1; }
		continue
	fi
	# Empty SSA means minic accepted the input but produced no code.
	# That happens today on K&R definitions and other unsupported
	# top-level constructs — minic silently bails.  Treat as failure.
	if [ ! -s "$ssa" ]; then
		echo "(no output from minic — likely unsupported top-level syntax)" >>"$err"
		stage_fail_minic+=("$src (empty)")
		[ $KEEP_GOING -eq 0 ] && { echo "FAIL minic-empty: $src"; cat "$err"; exit 1; }
		continue
	fi

	# Stage 2: SSA → ASM
	if ! "$QBE" -t i8086 -m "$MODEL" "$ssa" >"$asm" 2>"$err"; then
		stage_fail_qbe+=("$src: $(head -1 "$err")")
		[ $KEEP_GOING -eq 0 ] && { echo "FAIL qbe: $src"; cat "$err"; exit 1; }
		continue
	fi

	# Stage 3: convert QBE-emitted MASM/GNU-as mix to NASM syntax.
	# QBE i8086 currently outputs a hybrid: GNU-as section directives
	# (.text, .balign, .globl), MASM module headers (.model small, .code),
	# MASM proc syntax (`name proc near` / `endp`), and MASM operand
	# qualifier `word ptr [...]` / `byte ptr [...]`.  Convert all to NASM.
	asm_clean="$OUT_DIR/$base.nasm.asm"
	# Prefix local labels (l0, l1, ..., l_NNN, glo1, glo2, ...) with the
	# source basename to avoid conflicts when concatenating multiple
	# translation units into a single NASM .COM build.
	#
	# QBE i8086 emits local labels as `lN:` and globals (string literals
	# etc.) as `_gloN:`.  Function names and `_dos_*` runtime symbols are
	# kept unprefixed so cross-TU calls still resolve.
	prefix="${base}_"
	grep -v -E '^\.(text|data|bss|balign|section|globl|type|size|local|file|ident|string|p2align|model|code)' "$asm" \
		| sed -e 's/; TODO: 32-bit op [0-9]*/; XXX 32-bit op stub - codegen incomplete/' \
		      -e 's/^[[:space:]]*\.ascii "\(.*\)"$/.nasm_str \1/' \
		| awk '
			# Re-emit `.nasm_str <text>` lines as NASM backtick strings,
			# escaping any literal backticks in the content.
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
			# Label definitions: lN: or lN_lM: at start of line
			s/^(l\d+(?:_l\d+)?):/${p}$1:/;
			# Jump targets: jXX <label> (one operand).  Matches lN and lN_lM.
			s/^(\s*j[a-z]+\s+)(l\d+(?:_l\d+)?)\b/$1${p}$2/;
			# jnz val, lN, lM (two operands)
			s/^(\s*jnz\s+[^,]+,\s*)(l\d+(?:_l\d+)?)(\s*,\s*)(l\d+(?:_l\d+)?)\b/$1${p}$2$3${p}$4/;
			# Global glo: definitions and references (with or without leading
			# underscore — QBE emits both forms in different contexts).
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
			# `test es, es` (or any segment register self-test) — nonsense
			# from the rname[] segment-register fallback.  Replace with a
			# noop test that the assembler will accept.
			s/^(\s*test\s+)(es|ds|cs|ss),\s*\g{2}\b/$1ax, ax ; XXX was test $2,$2/g;
		' \
		> "$asm_clean"
	# Skip stand-alone NASM object assembly — we concatenate all .asm
	# files in the link stage into one BIN unit.

	stage_pass+=("$src")
done

echo
echo "=== Build summary ==="
echo "  PASS: ${#stage_pass[@]}/${#SOURCES[@]}"
echo
if [ ${#stage_fail_minic[@]} -gt 0 ]; then
	echo "  Failed at minic (${#stage_fail_minic[@]}):"
	for f in "${stage_fail_minic[@]}"; do echo "    $f"; done
fi
if [ ${#stage_fail_qbe[@]} -gt 0 ]; then
	echo "  Failed at qbe (${#stage_fail_qbe[@]}):"
	for f in "${stage_fail_qbe[@]}"; do echo "    $f"; done
fi

if [ ${#stage_pass[@]} -eq 0 ]; then
	echo
	echo "No sources compiled; nothing to link."
	exit 1
fi
if [ ${#stage_pass[@]} -ne ${#SOURCES[@]} ]; then
	echo
	echo "Note: ${#stage_pass[@]}/${#SOURCES[@]} sources compiled."
	echo "Linking with stubs for the missing ones."
fi

echo
if [ $EXE -eq 1 ]; then
	# .EXE pipeline (medium model and up):
	#   per-TU asm  → asm_to_omf.py → nasm -f obj → libobj
	#   crt0_exe.asm + libstub_exe.asm  → nasm -f obj → libobj
	#   omf_link.py → MZ EXE.
	echo "=== Linking (medium .EXE) ==="
	# 1. Per-TU OMF objects (regenerate even if we already have .obj from
	#    the C build path — flags or qbe behaviour may have changed).
	for src in "${stage_pass[@]}"; do
		base="${src%.c}"
		"$QBE_DIR/tools/asm_to_omf.py" "$base" \
			"$OUT_DIR/$base.asm" "$OUT_DIR/$base.omf.asm" 2>>"$OUT_DIR/link.err" || {
			echo "  FAIL omf-wrap: $base"; exit 1; }
		nasm -w-label-redef-late -f obj "$OUT_DIR/$base.omf.asm" \
			-o "$OUT_DIR/$base.obj" 2>>"$OUT_DIR/link.err" || {
			echo "  FAIL nasm-obj: $base"; cat "$OUT_DIR/link.err"; exit 1; }
	done
	# 2. crt0_exe.obj — far-data models need crt0_exe to build argv as
	#    4-byte far ptrs (offset+segment) to match main()'s
	#    `char *argv[]` ABI; the small model reaches _main near (NEAR_CODE).
	CRT0_FLAGS=""
	case "$MODEL" in
		compact|large|huge) CRT0_FLAGS="-DFAR_DATA=1" ;;
		small|tiny)         CRT0_FLAGS="-DNEAR_CODE=1" ;;
	esac
	nasm $CRT0_FLAGS -f obj "$DOS_DIR/crt0_exe.asm" -o "$OUT_DIR/crt0_exe.obj" \
		2>>"$OUT_DIR/link.err" || {
		echo "  FAIL nasm-obj: crt0_exe"; cat "$OUT_DIR/link.err"; exit 1; }

	# 3. The runtime: libstub (default) OR the §7r libstub-free stack.
	GC_FLAG=""
	RUNTIME_OBJS=()
	SUPPORT_OBJS=()
	if [ "$NO_LIBSTUB" = 1 ]; then
		# §7r libstub-free: newlibc's portable stdio (printf -> _write ->
		# VFS -> dos_shim INT 21h) + the dos_libc.c libc fill, compiled in
		# newlibc's own regime (shiminc + newlibc headers, clang -E,
		# -D__ia16__) — distinct from the stevie build-example regime above;
		# the two meet only at the linker (_printf, _write, _vfs_*,
		# _newlibc_test_main, and the dos_libc str/ctype fill).  Mirrors
		# build-example.sh --no-libstub.  --gc-sections drops the FAT/block
		# stdio code stevie never reaches.
		NL="${NEWLIBC_DIR:-$HOME/projects/newlibc/phase3_newlib}"
		[ -d "$NL" ] || { echo "  newlibc tree not found: $NL"; exit 77; }
		SHIM="$DOS_DIR/newlibc/shiminc"
		NL_NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'
		NL_HALT2DOS='s/__asm__[[:space:]]*volatile[[:space:]]*([[:space:]]*"hlt"[[:space:]]*)/{ __asm__ volatile ("mov ax, 0x4c00"); __asm__ volatile ("int 0x21"); }/g'
		compile_newlibc_unit() {
			local unit_src="$1" unit_base="$2"
			clang -E -P -nostdinc -DDOS -D__ia16__ -DNO_LIBSTUB \
				"-I$SHIM" "-I$INC_DIR" \
				"-I$NL/include" "-I$NL/drivers" "-I$NL/libgloss" "-I$NL/vfs" \
				"$unit_src" 2>>"$OUT_DIR/link.err" \
				| tr -d '\r\032' | sed "$NL_NORMALIZE" | sed "$NL_HALT2DOS" \
				> "$OUT_DIR/$unit_base.pp.c"
			"$MINIC" -m "$MODEL" < "$OUT_DIR/$unit_base.pp.c" \
				> "$OUT_DIR/$unit_base.ssa" 2>>"$OUT_DIR/link.err"
			"$QBE" -t i8086 -m "$MODEL" "$OUT_DIR/$unit_base.ssa" \
				> "$OUT_DIR/$unit_base.asm" 2>>"$OUT_DIR/link.err"
			"$QBE_DIR/tools/asm_to_omf.py" "--model=$MODEL" "$unit_base" \
				"$OUT_DIR/$unit_base.asm" "$OUT_DIR/$unit_base.omf.asm" 2>>"$OUT_DIR/link.err"
			nasm -w-label-redef-late -f obj "$OUT_DIR/$unit_base.omf.asm" \
				-o "$OUT_DIR/$unit_base.obj" 2>>"$OUT_DIR/link.err" || {
				echo "  FAIL nasm-obj: $unit_base"; cat "$OUT_DIR/link.err"; exit 1; }
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
			SUPPORT_OBJS+=("$OUT_DIR/$tu_base.obj")
		done
		# qbe_rt (the _qbe_* compiler helpers) + dos_syscall (INT 21h
		# primitives): assembled raw for small (near form); rewritten to the
		# far-call ABI by near_to_far_rt.py for medium (the compiler far-calls
		# them under medium).  heap.asm is the BSS heap dos_libc malloc carves
		# from via newlibc _sbrk.  See §7n/§7o/§7p.
		if [ "$MODEL" = small ]; then
			nasm -f obj "$DOS_DIR/qbe_rt.asm" -o "$OUT_DIR/qbe_rt.obj" 2>>"$OUT_DIR/link.err"
			nasm -f obj "$DOS_DIR/dos_syscall.asm" -o "$OUT_DIR/dos_syscall.obj" 2>>"$OUT_DIR/link.err"
		else
			"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=QBE_RT_TEXT \
				"$DOS_DIR/qbe_rt.asm" "$OUT_DIR/qbe_rt_far.asm" 2>>"$OUT_DIR/link.err"
			nasm -f obj "$OUT_DIR/qbe_rt_far.asm" -o "$OUT_DIR/qbe_rt.obj" 2>>"$OUT_DIR/link.err"
			"$QBE_DIR/tools/near_to_far_rt.py" --seg-name=DOS_SYSCALL_TEXT \
				"$DOS_DIR/dos_syscall.asm" "$OUT_DIR/dos_syscall_far.asm" 2>>"$OUT_DIR/link.err"
			nasm -f obj "$OUT_DIR/dos_syscall_far.asm" -o "$OUT_DIR/dos_syscall.obj" 2>>"$OUT_DIR/link.err"
		fi
		nasm "-DHEAP_SIZE=$STEVIE_HEAP_SIZE" -f obj "$DOS_DIR/heap.asm" \
			-o "$OUT_DIR/heap.obj" 2>>"$OUT_DIR/link.err" || {
			echo "  FAIL nasm-obj: heap"; cat "$OUT_DIR/link.err"; exit 1; }
		RUNTIME_OBJS=("$OUT_DIR/qbe_rt.obj" "$OUT_DIR/dos_syscall.obj" "$OUT_DIR/heap.obj")
		GC_FLAG="--gc-sections"
	else
		# libstub_exe.obj (auto-converted from libstub.asm).  Thread
		# the model so far-data builds (compact/large/huge) get the
		# FAR_STDIO_EXE epilogue with _far_fopen/_far_fclose/_far_fgets/etc.
		"$QBE_DIR/tools/libstub_to_exe.py" "--model=$MODEL" \
			"$DOS_DIR/libstub.asm" \
			"$OUT_DIR/libstub_exe.asm" 2>>"$OUT_DIR/link.err" || {
			echo "  FAIL libstub-conv"; exit 1; }
		nasm -f obj "$OUT_DIR/libstub_exe.asm" -o "$OUT_DIR/libstub_exe.obj" \
			2>>"$OUT_DIR/link.err" || {
			echo "  FAIL nasm-obj: libstub_exe"; cat "$OUT_DIR/link.err"; exit 1; }
		RUNTIME_OBJS=("$OUT_DIR/libstub_exe.obj")
	fi

	# 4. Link.  crt0 must come first so its _TEXT ends up at CS:IP=0:0
	#    (the linker places code segments in input order).
	OBJS=("$OUT_DIR/crt0_exe.obj")
	for src in "${stage_pass[@]}"; do
		OBJS+=("$OUT_DIR/${src%.c}.obj")
	done
	OBJS+=(${SUPPORT_OBJS[@]+"${SUPPORT_OBJS[@]}"})
	OBJS+=("${RUNTIME_OBJS[@]}")

	if "$QBE_DIR/tools/omf_link.py" \
		-o "$OUT_DIR/stevie.exe" \
		--map "$OUT_DIR/stevie.map" \
		--entry _start \
		--stack-size "$STEVIE_STACK_SIZE" \
		$GC_FLAG \
		"${OBJS[@]}" 2>>"$OUT_DIR/link.err"; then
		echo "  OK: $OUT_DIR/stevie.exe ($(wc -c <"$OUT_DIR/stevie.exe") bytes)"
	else
		echo "  FAIL link: $(tail -3 "$OUT_DIR/link.err")"
		exit 1
	fi
else
	# .COM pipeline (tiny/small): NASM-concat into a flat binary.
	echo "=== Linking (.COM) ==="
	LINK_ASM="$OUT_DIR/stevie.full.asm"
	strip_runtime() {
		grep -v -E '^(BITS|CPU|section|global|extern)\b' "$1"
	}

	# Prune libstub to only the chunks transitively referenced by the
	# per-TU asm + crt0.  The .COM pipeline has no linker, so without
	# pruning every libstub symbol shows up in the flat binary.
	PRUNED_LIBSTUB="$OUT_DIR/libstub.pruned.asm"
	PER_TU_ASM=()
	for src in "${stage_pass[@]}"; do
		base="${src%.c}"
		[ -f "$OUT_DIR/$base.nasm.asm" ] && PER_TU_ASM+=("$OUT_DIR/$base.nasm.asm")
	done
	"$QBE_DIR/tools/libstub_prune.py" "$DOS_DIR/libstub.asm" \
		"$PRUNED_LIBSTUB" "$DOS_DIR/crt0.asm" "${PER_TU_ASM[@]}" \
		2>>"$OUT_DIR/link.err" || {
		echo "  FAIL libstub-prune"; cat "$OUT_DIR/link.err"; exit 1; }

	{
		echo "BITS 16"
		echo "CPU 8086"
		echo "ORG 0x100"
		echo
		echo "section .text"
		echo "; ===== crt0 ====="
		strip_runtime "$DOS_DIR/crt0.asm"
		echo
		echo "; ===== libstub (pruned) ====="
		strip_runtime "$PRUNED_LIBSTUB"
		echo
		for src in "${SOURCES[@]}"; do
			base="${src%.c}"
			[ -f "$OUT_DIR/$base.nasm.asm" ] || continue
			echo "; ===== $src ====="
			cat "$OUT_DIR/$base.nasm.asm"
			echo
		done
		echo "; ===== heap marker ====="
		echo "_heap_end_of_image:"
	} > "$LINK_ASM"

	if nasm -w-label-redef-late -w-pp-open-string -f bin "$LINK_ASM" \
			-o "$OUT_DIR/stevie.com" 2>"$OUT_DIR/link.err"; then
		echo "  OK: $OUT_DIR/stevie.com ($(wc -c <"$OUT_DIR/stevie.com") bytes)"
	else
		echo "  FAIL link: $(head -3 "$OUT_DIR/link.err")"
		exit 1
	fi
fi
