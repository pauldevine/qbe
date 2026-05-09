; DOS .EXE startup (medium memory model)
;
; On entry from DOS:
;   DS = ES = PSP segment
;   SS:SP = stack segment top (set by DOS from the MZ header)
;   CS:IP = our entry point (this _start)
;
; What we do:
;   1. Set DS to DGROUP so near-data references resolve correctly.
;   2. Push argc / argv (currently zero) for main().
;   3. Far-call _main (medium model: _main lives in MAIN_TEXT, far code).
;   4. Pop args, exit to DOS with AL = main's return code.
;
; The MZ header's SS:SP and CS:IP are filled in by tools/omf_link.py
; from the linker map; we don't need to set them here.

bits 16
cpu 8086

group DGROUP _DATA _BSS

extern _main
global _start

segment _TEXT class=CODE align=2 use16

_start:
    ; DS = DGROUP so near-data accesses ([_var]) resolve.
    mov ax, DGROUP
    mov ds, ax
    ; ES is left at PSP for now — INT 21h functions that need ES:DI
    ; will have to set it themselves.

    xor ax, ax
    push ax              ; argv = NULL
    push ax              ; argc = 0
    call far _main
    add sp, 4

    ; Exit to DOS with AL = main's return code.
    mov ah, 0x4C
    int 0x21

    ; If DOS ever returns (it doesn't), spin.
    jmp $

segment _DATA class=DATA align=2 use16
segment _BSS  class=BSS  align=2 use16
