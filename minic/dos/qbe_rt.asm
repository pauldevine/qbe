; qbe_rt.asm -- the irreducible QBE compiler-runtime helpers, as a clean
; STANDALONE near-code TU for the libstub-free DOS-hosted newlibc link (§7n,
; Phase-6 libstub retirement).
;
; The i8086 backend emits calls to these helpers for 32-bit divide/remainder,
; huge-model pointer arithmetic, and the ISR code-segment read.  They are the
; one part of the old libstub that is NOT "libc" and can never be retired --
; they are compiler runtime.  This file lets a program link them WITHOUT the
; 2884-line libstub.asm (and its python EXE epilogue), so newlibc can supply
; the actual libc surface.
;
; ⚠ SOURCE-OF-TRUTH DUPLICATION: every routine here is COPIED VERBATIM (near
; form) from minic/dos/libstub.asm -- the UDIVMOD32_BODY macro from lines
; 38-61 and the _qbe_* helpers from lines 2229-2506.  libstub.asm is left
; untouched so MicroPython / stevie / every existing gate cannot regress.  A
; future bugfix to any divide/huge/sign logic MUST be applied in BOTH files.
;
; ⚠ ABI INVARIANT (i8086/abi.c dedup_arg_stores, §2y): a helper MUST NOT write
; its incoming stack-argument slots ([bp+4]..) in place... except _qbe_div32s /
; _qbe_rem32s, which negate the in-place num/denom -- that is the verbatim
; libstub behavior, sound here because the compiler never reuses a div/rem
; arg-slot region across calls.
;
; Near form (ret, [bp+4]=arg0): assembled raw by nasm for the small model, NOT
; routed through libstub_to_exe.py's +2/retf rewrite.

bits 16
cpu 8086

; Pure code TU: no DGROUP data of its own (the _qbe_* helpers use only regs +
; their stack args).  crt0_exe.asm declares `group DGROUP _DATA _BSS` for the
; link; this module just contributes to _TEXT.
segment _TEXT class=CODE align=2 use16

; Shared shift-subtract 32-bit divide body (libstub.asm:38-61).  Args at
; [bp+4..11]; caller's prologue must have saved BX/SI.  On exit:
;   DX:AX = quotient   CX:BX = remainder
%macro UDIVMOD32_BODY 0
    mov ax, [bp+4]           ; Q lo (numerator)
    mov dx, [bp+6]           ; Q hi
    xor bx, bx               ; R lo
    xor cx, cx               ; R hi
    mov si, 32
%%loop:
    shl ax, 1
    rcl dx, 1
    rcl bx, 1
    rcl cx, 1
    cmp cx, [bp+10]
    jb  %%skip
    ja  %%sub
    cmp bx, [bp+8]
    jb  %%skip
%%sub:
    sub bx, [bp+8]
    sbb cx, [bp+10]
    or  ax, 1
%%skip:
    dec si
    jnz %%loop
%endmacro

global _qbe_div32u
_qbe_div32u:
    push bp
    mov bp, sp
    push bx
    push si
    UDIVMOD32_BODY
    pop si
    pop bx
    pop bp
    ret

global _qbe_rem32u
_qbe_rem32u:
    push bp
    mov bp, sp
    push bx
    push si
    UDIVMOD32_BODY
    mov ax, bx
    mov dx, cx
    pop si
    pop bx
    pop bp
    ret

global _qbe_div32s
_qbe_div32s:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    xor di, di               ; bit 0 = negate quotient
    test word [bp+6], 0x8000
    jz  .qs_npos
    inc di
    not word [bp+4]
    not word [bp+6]
    add word [bp+4], 1
    adc word [bp+6], 0
.qs_npos:
    test word [bp+10], 0x8000
    jz  .qs_dpos
    xor di, 1
    not word [bp+8]
    not word [bp+10]
    add word [bp+8], 1
    adc word [bp+10], 0
.qs_dpos:
    UDIVMOD32_BODY           ; DX:AX = |num| / |denom|
    test di, 1
    jz  .qs_done
    not ax
    not dx
    add ax, 1
    adc dx, 0
.qs_done:
    pop di
    pop si
    pop bx
    pop bp
    ret

global _qbe_rem32s
_qbe_rem32s:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    xor di, di               ; bit 0 = negate remainder (sign follows num)
    test word [bp+6], 0x8000
    jz  .rs_npos
    inc di
    not word [bp+4]
    not word [bp+6]
    add word [bp+4], 1
    adc word [bp+6], 0
.rs_npos:
    test word [bp+10], 0x8000
    jz  .rs_dpos
    not word [bp+8]
    not word [bp+10]
    add word [bp+8], 1
    adc word [bp+10], 0
.rs_dpos:
    UDIVMOD32_BODY           ; CX:BX = |num| mod |denom|
    mov ax, bx
    mov dx, cx
    test di, 1
    jz  .rs_done
    not ax
    not dx
    add ax, 1
    adc dx, 0
.rs_done:
    pop di
    pop si
    pop bx
    pop bp
    ret

; ----- huge memory model pointer arithmetic (libstub.asm:2360-2496) -----
; ptr packed low-word=off, high-word=seg.  Return DX:AX (DX=seg, AX=off);
; _qbe_huge_cmp returns DX:AX = signed linear difference.

global _qbe_huge_norm
_qbe_huge_norm:
    push bp
    mov bp, sp
    mov ax, [bp+4]               ; off
    mov dx, [bp+6]               ; seg
    mov cx, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1                    ; cx = off >> 4
    add dx, cx                   ; new_seg = seg + (off >> 4)  (mod 0x10000)
    and ax, 000Fh                ; new_off = off & 0xF
    pop bp
    ret

global _qbe_huge_add
_qbe_huge_add:
    push bp
    mov bp, sp
    push bx
    ; DX:AX = seg << 4   (top 4 bits of seg spill into DX bits 0..3)
    mov ax, [bp+6]               ; seg
    xor dx, dx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add ax, [bp+4]               ; + off (16-bit zero-extended)
    adc dx, 0
    add ax, [bp+8]               ; + offset (signed 32-bit)
    adc dx, [bp+10]
    mov bx, ax                   ; save low word for new_off
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1                    ; DX:AX = new_linear >> 4
    mov dx, ax                   ; new_seg = low 16 of (linear >> 4)
    mov ax, bx
    and ax, 000Fh                ; new_off
    pop bx
    pop bp
    ret

global _qbe_huge_sub
_qbe_huge_sub:
    push bp
    mov bp, sp
    push bx
    mov ax, [bp+6]
    xor dx, dx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add ax, [bp+4]
    adc dx, 0
    sub ax, [bp+8]
    sbb dx, [bp+10]
    mov bx, ax
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    mov dx, ax
    mov ax, bx
    and ax, 000Fh
    pop bx
    pop bp
    ret

global _qbe_huge_cmp
_qbe_huge_cmp:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    ; linear1 → SI:DI
    mov ax, [bp+6]               ; p1.seg
    xor dx, dx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add ax, [bp+4]               ; + p1.off
    adc dx, 0
    mov di, ax
    mov si, dx                   ; SI:DI = linear1 (32-bit)
    ; linear2 → DX:AX
    mov ax, [bp+10]              ; p2.seg
    xor dx, dx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add ax, [bp+8]               ; + p2.off
    adc dx, 0
    ; (SI:DI) - (DX:AX) → BX:CX, then move to return DX:AX
    mov cx, di
    sub cx, ax
    mov bx, si
    sbb bx, dx
    mov ax, cx
    mov dx, bx
    pop di
    pop si
    pop bx
    pop bp
    ret

; unsigned _qbe_get_cs(void) — caller's CS (the call is near).  Used to build
; far IVT entries for __attribute__((interrupt)) handlers.  Near-code only.
global _qbe_get_cs
_qbe_get_cs:
    mov ax, cs
    ret
