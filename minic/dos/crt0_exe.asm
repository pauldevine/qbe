; DOS .EXE startup (medium / compact / large / huge memory models)
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
;
; Define FAR_DATA on the nasm command line (-DFAR_DATA) when building
; for compact/large/huge memory models.  In those models C `char *` is a
; 4-byte far pointer, so argv[i] slots must be 4 bytes (offset+segment)
; and the argv array address passed to main() is itself a 4-byte far
; pointer.  Without FAR_DATA, argv is the medium-model 2-byte near form.
;
; Define NEAR_CODE (-DNEAR_CODE) for the small model: _main is reached
; with a near call (it returns with a near ret), and all code coalesces
; into this single _TEXT segment at link time.

bits 16
cpu 8086

; argv slot size doubles under FAR_DATA (4-byte far ptrs vs 2-byte near).
; Cap MAX_ARGV at 8 under FAR_DATA so total argv_arr size stays at 32
; bytes (same DGROUP footprint as the medium build) — DGROUP+stack is
; right at the 64KB ceiling for stevie under far-data.
%ifdef FAR_DATA
  %define MAX_ARGV  8
  %define ARGV_SLOT 4
%else
  %define MAX_ARGV  16
  %define ARGV_SLOT 2
%endif
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
%ifdef FAR_DATA
    mov ax, ds
    mov word [_argv_arr + 2], ax   ; argv[0] segment = DGROUP
%endif
    mov word [_argc], 1

    ; Tokenize _cmdbuf in place; fill argv[1..MAX_ARGV-1].
    ; Treat space, tab, and CR (0x0D) as separators.
    mov si, _cmdbuf
    mov di, _argv_arr + ARGV_SLOT  ; &argv[1]
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
%ifdef FAR_DATA
    mov ax, ds
    mov [di + 2], ax               ; argv[i] segment = DGROUP (_cmdbuf lives in DGROUP)
%endif
    add di, ARGV_SLOT
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
    mov word [di], 0               ; argv[argc] = NULL (offset)
%ifdef FAR_DATA
    mov word [di + 2], 0           ; argv[argc] = NULL (segment)
%endif

    ; Call main(argc, argv).  cdecl: push argv (right) then argc (left).
%ifdef FAR_DATA
    mov ax, ds                     ; argv segment (DGROUP — argv_arr is here)
    push ax
    mov ax, _argv_arr              ; argv offset
    push ax
%else
    mov ax, _argv_arr
    push ax
%endif
    push word [_argc]
%ifdef NEAR_CODE
    call _main                     ; small model: near _main, near ret
%else
    call far _main
%endif
%ifdef FAR_DATA
    add sp, 6
%else
    add sp, 4
%endif

    ; Exit to DOS with AL = main's return code.
    mov ah, 0x4C
    int 0x21

    ; If DOS ever returns (it doesn't), spin.
    jmp $

segment _DATA class=DATA align=2 use16

_progname: db "program", 0

segment _BSS  class=BSS  align=2 use16

_cmdbuf:   resb CMDBUF_SIZE
_argv_arr: resb MAX_ARGV * ARGV_SLOT
_argc:     resw 1
