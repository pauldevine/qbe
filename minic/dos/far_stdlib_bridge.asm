; far_stdlib_bridge.asm -- far-DATA-model name bridges for the libstub-free
; DOS-hosted link (§7t, Phase-6 libstub retirement).
;
; Under far-DATA models (compact/large/huge -- all FAR CODE + FAR DATA here)
; minic's call_target_name() mangles a fixed set of stdio/string/memory calls
; to _far_X (the far_stdlib[] list in minic.y), because in those models the
; stdlib functions take 4-byte far-pointer arguments.  newlibc's printf stack
; (printf_wrappers.c, dos_shim.c) and the minic-compiled dos_libc.c fill DEFINE
; those functions under PLAIN names (_printf, _strcpy, ...): a C function
; definition is emitted by its real name, only CALL SITES are mangled.  And
; because those TUs are themselves compiled -m <far-data-model>, their plain
; definitions ALREADY have the correct far-pointer ABI -- the only mismatch is
; the name.  So each bridge below is a far tail-call `jmp far _X`: it preserves
; the caller's argument frame exactly (essential for the variadic printf
; family -- a `call far` thunk would insert an extra return address between
; _printf and its varargs), and _X's own retf returns straight to the original
; caller.
;
; This is the far-data analogue of how the small/medium libstub-free build just
; links newlibc's plain _printf/_strcpy by name (NEAR_DATA -> no mangling).  No
; compiler change: COPY/ADD only.  Each thunk lives in its own uniquely-named
; CODE segment so --gc-sections drops the unreferenced ones (and never pulls
; their target) -- a printf-only program keeps just printf/strcpy/strlen/...,
; not the whole file-I/O surface.
;
; Only names with a guaranteed plain definition in the always-linked support
; set are bridged: printf_wrappers.c (printf/fprintf/sprintf/puts/fputs/fputc/
; fgets), dos_shim.c (fopen/fclose/fread/fwrite), dos_libc.c (the str*/mem*
; family).  far_stdlib's fflush/strstr/memmove have no plain impl yet and are
; omitted (a referencing program fails the link loudly -> add the impl + thunk
; then); setjmp/longjmp need a far jmp_buf and are deferred.

bits 16
cpu 8086

%macro FAR_BRIDGE 1
segment FSB_%1 class=CODE align=2 use16
extern _%1
global _far_%1
_far_%1:
    jmp far _%1
%endmacro

; --- stdio (newlibc printf_wrappers.c / dos_shim.c) ---
FAR_BRIDGE printf
FAR_BRIDGE fprintf
FAR_BRIDGE sprintf
FAR_BRIDGE puts
FAR_BRIDGE fputs
FAR_BRIDGE fputc
FAR_BRIDGE fgets
FAR_BRIDGE fopen
FAR_BRIDGE fclose
FAR_BRIDGE fread
FAR_BRIDGE fwrite

; --- string / memory (dos_libc.c) ---
FAR_BRIDGE strlen
FAR_BRIDGE strcpy
FAR_BRIDGE strcmp
FAR_BRIDGE strncmp
FAR_BRIDGE strncpy
FAR_BRIDGE strchr
FAR_BRIDGE strcat
FAR_BRIDGE strcspn
FAR_BRIDGE strrchr
FAR_BRIDGE memcpy
FAR_BRIDGE memcmp
FAR_BRIDGE memset
