; DOS .EXE startup (medium memory model)
;
; On entry from DOS:
;   DS = ES = PSP segment
;   SS:SP = stack segment top (set by DOS from the MZ header)
;   CS:IP = our entry point (this _start)
;
; What we do:
;   1. Parse the PSP command tail (length at PSP:0080h, chars at PSP:0081h,
;      terminated by 0Dh) into argc / argv. argv[0] is set to a fixed
;      placeholder string since DOS doesn't always give us the program
;      name cheaply (the env-block trick is 3.0+ only).
;   2. Set DS to DGROUP so near-data references resolve correctly.
;   3. Push argc / argv for main().
;   4. Far-call _main (medium model: _main lives in MAIN_TEXT, far code).
;   5. Pop args, exit to DOS with AL = main's return code.
;
; The MZ header's SS:SP and CS:IP are filled in by tools/omf_link.py
; from the linker map; we don't need to set them here.

bits 16
cpu 8086

%define MAX_ARGV     16
%define CMDBUF_SIZE  130   ; PSP tail max 127 + slack + NUL

group DGROUP _DATA _BSS

extern _main
global _start

segment _TEXT class=CODE align=2 use16

_start:
    ; Save PSP segment in ES before we clobber DS.
    push ds
    pop es

    ; DS = DGROUP so static buffers below resolve.
    mov ax, DGROUP
    mov ds, ax

    ; Copy PSP command tail (PSP:0081h..) into _cmdbuf, then NUL-terminate.
    ; ES:SI sources from PSP; explicit ds: override on the dest store.
    xor cx, cx
    mov cl, [es:0x80]              ; length byte (0..127)
    cmp cx, CMDBUF_SIZE - 2
    jbe .len_ok
    mov cx, CMDBUF_SIZE - 2
.len_ok:
    mov si, 0x81                   ; ES:SI = PSP cmd tail
    mov di, _cmdbuf                ; DS:DI = our buffer
    jcxz .copy_done
.copy_loop:
    mov al, [es:si]
    mov [ds:di], al
    inc si
    inc di
    loop .copy_loop
.copy_done:
    mov byte [ds:di], 0            ; terminate the buffer

    ; ES = DGROUP now that the PSP read is done.  C runtime string ops
    ; (rep movsb / lodsb+stosb in strcpy, etc.) read DS:SI and write
    ; ES:DI; they need ES==DS or destination writes land in the PSP.
    ; AX was clobbered by the copy loop, so reload DGROUP.
    mov ax, DGROUP
    mov es, ax

    ; argv[0] = program name, argc = 1.
    mov word [_argv_arr], _progname
    mov word [_argc], 1

    ; Tokenize _cmdbuf in place; fill argv[1..MAX_ARGV-1].
    ; Treat space, tab, and CR (0x0D) as separators.
    mov si, _cmdbuf
    mov di, _argv_arr + 2          ; &argv[1]
    mov bx, MAX_ARGV - 1           ; remaining slots

.tok_loop:
.skip_ws:
    mov al, [si]
    cmp al, 0
    je .tok_end
    cmp al, 0x0D
    je .tok_end
    cmp al, ' '
    je .skip_advance
    cmp al, 0x09
    je .skip_advance
    jmp .tok_start
.skip_advance:
    inc si
    jmp .skip_ws

.tok_start:
    test bx, bx
    jz .tok_end_trunc              ; out of argv slots
    mov [di], si
    add di, 2
    inc word [_argc]
    dec bx

.in_token:
    inc si
    mov al, [si]
    cmp al, 0
    je .tok_end
    cmp al, 0x0D
    je .tok_terminate
    cmp al, ' '
    je .tok_terminate
    cmp al, 0x09
    je .tok_terminate
    jmp .in_token

.tok_terminate:
    mov byte [si], 0
    inc si
    jmp .tok_loop

.tok_end_trunc:
    mov byte [si], 0
.tok_end:
    mov word [di], 0               ; argv[argc] = NULL

    ; Call main(argc, argv).  cdecl: push argv (right) then argc (left).
    mov ax, _argv_arr
    push ax
    push word [_argc]
    call far _main
    add sp, 4

    ; Exit to DOS with AL = main's return code.
    mov ah, 0x4C
    int 0x21

    ; If DOS ever returns (it doesn't), spin.
    jmp $

segment _DATA class=DATA align=2 use16

_progname: db "program", 0

segment _BSS  class=BSS  align=2 use16

_cmdbuf:   resb CMDBUF_SIZE
_argv_arr: resw MAX_ARGV
_argc:     resw 1
