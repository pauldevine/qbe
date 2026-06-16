; builtins_rt.asm — compiler-builtin helpers for the libstub-FREE runtime.
;
; §8c: MicroPython (and any minic program built --no-libstub) reaches for the
; GCC-style __builtin_clz/__builtin_clzl/__builtin_expect/__builtin_unreachable
; helpers that the i8086 has no single instruction for (no BSR on the 8086).
; libstub.asm provides these in its always-emitted header region; the
; libstub-free runtime had no equivalent, so a libstub-free MP link left
; ___builtin_clzl undefined (103 refs — mp_uint_t/uintptr_t widen to 32 bits
; under the far-data models, so MicroPython's bit-width helpers use the `long`
; form).  This file is the libstub-free home for them.
;
; STRATEGY (the §7n/§7x COPY-NEVER-MUTATE invariant): the four bodies below are
; COPIED VERBATIM from libstub.asm (lines 73-143).  libstub.asm is UNTOUCHED.
; Offsets are the NEAR form ([bp+4] = arg0); for the far-code models this file
; is run through near_to_far_rt.py, which shifts every [bp+N] by +2 (the far
; return CS word) and rewrites ret->retf — exactly as qbe_rt.asm is handled.
; A pure-code TU must NOT declare `group DGROUP` (crt0 declares it for the
; link; the §7n trap) — just a use16 CODE segment.

	segment _TEXT class=CODE align=2 use16

; int __builtin_clz(unsigned int x) — count leading zero bits of a 16-bit
; `unsigned int` (this target's int is 16 bits).  Result 0..15; clz(0) is
; undefined in C, return the bit width (16).  8086 has no BSR, so loop.
global ___builtin_clz
___builtin_clz:
    push bp
    mov bp, sp
    mov cx, [bp+4]             ; x
    or  cx, cx
    jnz .scan
    mov ax, 16                 ; clz(0): undefined; return width
    pop bp
    ret
.scan:
    xor ax, ax                 ; count = 0
.loop:
    test cx, 0x8000
    jnz .done
    inc ax
    shl cx, 1
    jmp .loop
.done:
    pop bp
    ret

; int __builtin_clzl(unsigned long x) — count leading zero bits of a 32-bit
; `unsigned long`.  Result 0..32; clzl(0) is undefined in C, return width (32).
; The 4-byte arg is low word [bp+4], high word [bp+6].
global ___builtin_clzl
___builtin_clzl:
    push bp
    mov bp, sp
    mov dx, [bp+6]             ; high word
    or  dx, dx
    jnz .scan                 ; high nonzero: ax=0, scan high word
    mov dx, [bp+4]            ; low word
    or  dx, dx
    jnz .lowscan
    mov ax, 32                 ; clzl(0): undefined; return width
    pop bp
    ret
.lowscan:
    mov ax, 16                 ; high word contributes 16 leading zeros
    jmp .loop
.scan:
    xor ax, ax
.loop:
    test dx, 0x8000
    jnz .done
    inc ax
    shl dx, 1
    jmp .loop
.done:
    pop bp
    ret

; long __builtin_expect(long exp, long c) — branch-prediction hint; returns
; its first argument.  MicroPython's mp_likely/mp_unlikely wrap a boolean, so
; the low word in AX is all the consumer reads.
global ___builtin_expect
___builtin_expect:
    push bp
    mov bp, sp
    mov ax, [bp+4]             ; exp (low word)
    pop bp
    ret

; void __builtin_unreachable(void) — marks unreachable code.  Reaching it is
; undefined behaviour; just return so a stray call can't hang headless tests.
global ___builtin_unreachable
___builtin_unreachable:
    ret
