; dos_syscall.asm -- the DOS INT 21h syscall primitives (int86 family), as a
; clean STANDALONE near-code TU for the libstub-free DOS-hosted newlibc link
; (§7n, Phase-6 libstub retirement).
;
; On the bare-metal Victor these primitives don't exist (drivers use volatile
; far MMIO).  But a DOS-HOSTED newlibc program still reaches the OS through
; INT 21h: dos_shim.c's console_dev_read/write call int86(0x21,...).  So these
; five are the irreducible "DOS kernel ABI" -- not libc, not retire-able while
; the host is DOS.  This TU lets a program link them WITHOUT pulling the whole
; libstub.asm body.
;
; ⚠ SOURCE-OF-TRUTH DUPLICATION: COPIED VERBATIM (near form) from
; minic/dos/libstub.asm -- _int86 (1359-1396), _intdos (1400-1434),
; _segread (2534-2546), _int86x (2551-2632), _intdosx (2644-2715).  libstub.asm
; is left untouched (zero regression); a future fix must land in BOTH files.
;
; ⚠ ABI INVARIANT (i8086/abi.c dedup_arg_stores, §2y): none of these write
; their incoming arg slots in place (they read args into regs first).
;
; Self-contained: each uses CS-relative self-modifying code and function-local
; inline `dw` data labels (emitted inside _TEXT, read via cs:) -- no shared
; libstub helper or top-level data label is referenced.
;
; cdecl, BX/SI/DI/BP callee-save, near (small-model) pointers.  Assembled raw
; by nasm, NOT routed through libstub_to_exe.py.

bits 16
cpu 8086

; Pure code TU: the int86 family's only data is function-local inline `dw`
; scratch emitted inside _TEXT (read via cs:), so no DGROUP membership is
; needed.  crt0_exe.asm declares `group DGROUP _DATA _BSS` for the link.
segment _TEXT class=CODE align=2 use16

; int int86(int intno, union REGS *in, union REGS *out)
; [bp+4] intno, [bp+6] inregs, [bp+8] outregs.  SMC dispatches the INT.
global _int86
_int86:
    push bp
    mov bp, sp
    push si
    push di
    push bx                     ; BX is callee-save (cdecl/8086)
    mov ax, [bp+4]
    mov [cs:.int_op+1], al
    mov bx, [bp+6]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]
.int_op:
    int 21h
    push bx
    mov bx, [bp+8]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]
    pop bx
    pop di
    pop si
    pop bp
    ret

; int intdos(union REGS *in, union REGS *out) — int86(0x21, in, out).
; [bp+4] inregs, [bp+6] outregs.
global _intdos
_intdos:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov bx, [bp+4]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]
    int 21h
    push bx
    mov bx, [bp+6]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]
    pop bx
    pop di
    pop si
    pop bp
    ret

; void segread(struct SREGS *segs) — snapshot ES/CS/SS/DS.
global _segread
_segread:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov [bx+0], es
    mov [bx+2], cs
    mov [bx+4], ss
    mov [bx+6], ds
    pop bx
    pop bp
    ret

; int int86x(int intno, union REGS *in, union REGS *out, struct SREGS *segs)
; [bp+4] intno, [bp+6] in, [bp+8] out, [bp+10] segs.  Loads DS/ES from segs.
global _int86x
_int86x:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds                      ; callee-save (we will overwrite DS)

    mov ax, [bp+4]
    mov [cs:.x_int_op+1], al      ; patch INT immediate

    ; Read segregs->ds (stash in CS-rel scratch) and segregs->es (load now).
    mov bx, [bp+10]
    mov ax, [bx+6]
    mov [cs:.x_desired_ds], ax
    mov ax, [bx+0]
    mov es, ax

    ; Load GPRs from inregs (DS still ours).
    mov bx, [bp+6]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]

    ; Last step before INT: swap DS to the caller-supplied value.
    push ax
    mov ax, [cs:.x_desired_ds]
    mov ds, ax
    pop ax
.x_int_op:
    int 21h

    ; Snapshot callee's ES/DS to CS-rel scratch, then restore our DS = SS.
    push ax
    push ds
    pop ax
    mov [cs:.x_callee_ds], ax
    push es
    pop ax
    mov [cs:.x_callee_es], ax
    push ss
    pop ds
    pop ax

    ; Write outregs via our DS.
    push bx
    mov bx, [bp+8]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]               ; return = result AX

    ; Write callee's ES/DS back into segregs->{es,ds} (cs/ss untouched).
    push ax
    mov bx, [bp+10]
    mov ax, [cs:.x_callee_es]
    mov [bx+0], ax
    mov ax, [cs:.x_callee_ds]
    mov [bx+6], ax
    pop ax

    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    ret

.x_desired_ds: dw 0
.x_callee_ds:  dw 0
.x_callee_es:  dw 0

; int intdosx(union REGS *in, union REGS *out, struct SREGS *segs)
; [bp+4] in, [bp+6] out, [bp+8] segs.  As int86x(0x21, in, out, segs).
global _intdosx
_intdosx:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds

    mov bx, [bp+8]
    mov ax, [bx+6]
    mov [cs:.dx_desired_ds], ax
    mov ax, [bx+0]
    mov es, ax

    mov bx, [bp+4]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]

    push ax
    mov ax, [cs:.dx_desired_ds]
    mov ds, ax
    pop ax
    int 0x21

    push ax
    push ds
    pop ax
    mov [cs:.dx_callee_ds], ax
    push es
    pop ax
    mov [cs:.dx_callee_es], ax
    push ss
    pop ds
    pop ax

    push bx
    mov bx, [bp+6]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]

    push ax
    mov bx, [bp+8]
    mov ax, [cs:.dx_callee_es]
    mov [bx+0], ax
    mov ax, [cs:.dx_callee_ds]
    mov [bx+6], ax
    pop ax

    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    ret

.dx_desired_ds: dw 0
.dx_callee_ds:  dw 0
.dx_callee_es:  dw 0
