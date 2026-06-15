; heap.asm -- the BSS heap for the libstub-free newlibc link.
; §7o (Phase-6 libstub retirement, malloc increment).
;
; newlibc's _sbrk (libgloss/syscalls.c) is the primitive allocator malloc/free
; carve from.  It brackets the heap with two extern symbols, the C names
; __heap_start / __heap_end (asm ___heap_start / ___heap_end with the
; leading-underscore convention).  On the bare-metal phase-3 build these come
; from the linker script; for the DOS-hosted libstub-free link there is no
; linker script, so this TU reserves the heap in _BSS and exports the two
; bracket symbols -- __heap_end's ADDRESS must be exactly end-of-heap, which
; only controlled symbol placement (this hand-authored asm) guarantees.
;
; _BSS lives in DGROUP, so the heap shares the 64KB DGROUP with statics + the
; stack (omf_link.py enforces the cap).  HEAP_SIZE is overridable on the nasm
; command line; the default 8KB suits the malloc_probe gate.  When no malloc is
; reached (every other --no-libstub test), --gc-sections drops _sbrk and with
; it the only references to these symbols, so the heap costs those builds
; nothing (verified: snprintf_test data+bss unchanged at 4322 bytes).
;
; Like qbe_rt.asm / dos_syscall.asm this is an all-new standalone object linked
; only on the --no-libstub path; libstub.asm is untouched.

%ifndef HEAP_SIZE
%define HEAP_SIZE 8192
%endif

; A TU that contributes _BSS data must declare its DGROUP membership (a
; pure-code TU must NOT -- see §7n).  Mirror crt0_exe.asm / the minic-emitted
; objects: declare both group members and define both segments (the _DATA one
; stays empty here) so nasm's GRPDEF has no undefined segment.
group DGROUP _DATA _BSS
segment _DATA class=DATA align=2 use16
segment _BSS class=BSS align=2 use16

global ___heap_start
global ___heap_end

___heap_start:
	resb HEAP_SIZE
___heap_end:
