; Minimal libc stubs for stevie-dos prototype build.
;
; These are placeholder implementations that allow the binary to link.
; A real port needs proper printf/fopen/malloc on top of the doslib DOS
; INT 21h wrappers.  For now we just want a successful link.

; void *malloc(size_t sz) — pointer arg is l (32-bit), but size_t is w.
; Stack: [bp+4] = sz (w).  Returns DX:AX where AX=offset, DX=0 (DS-rel).
;
; Bump-pointer allocator.  The heap starts at _heap_end_of_image (a
; label placed at the absolute end of the linked .COM, defined by the
; build script) and ends at SP minus a stack reserve, computed at first
; call.  No bytes are added to the .COM file by this code — the heap
; lives in the ~64KB-_heap_end_of_image region of the loaded segment.
;
; ⚠ This is dead code while .COM is over 64KB: DOSBox truncates the
; image at 0xFFFE so _heap_end_of_image points past the truncation and
; into garbage.  Once the .COM size is fixed (Path A or B), this lights
; up automatically.
global _malloc
_malloc:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    add ax, 1
    and ax, 0xFFFE              ; word-align the size
    cmp word [_heap_initialized], 0
    jne .post_init
    ; First-call init: heap_ptr = end-of-image (rounded up to word),
    ; heap_top = SP - 1024 (leave 1KB stack).
    mov bx, _heap_end_of_image
    add bx, 1
    and bx, 0xFFFE
    mov [_heap_ptr], bx
    mov bx, sp
    sub bx, 1024
    mov [_heap_top], bx
    mov word [_heap_initialized], 1
.post_init:
    mov bx, [_heap_ptr]
    mov cx, bx
    add cx, ax
    cmp cx, [_heap_top]
    ja .fail
    mov [_heap_ptr], cx
    mov ax, bx                  ; offset into DGROUP
    xor dx, dx                  ; segment relative to DS = 0
    pop bp
    ret
.fail:
    xor ax, ax
    xor dx, dx
    pop bp
    ret

global _free
_free:                          ; bump-allocator can't free
    ret

_heap_initialized:  dw 0
_heap_ptr:          dw 0
_heap_top:          dw 0

global _strlen
_strlen:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    xor ax, ax
.l:
    cmp byte [si], 0
    je .d
    inc ax
    inc si
    jmp .l
.d:
    pop si
    pop bp
    ret

; char *strcpy(char *dest, char *src) — near pointers, 2 bytes each.
global _strcpy
_strcpy:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]      ; dest
    mov si, [bp+6]      ; src
.l:
    lodsb
    stosb
    cmp al, 0
    jne .l
    mov ax, [bp+4]
    pop di
    pop si
    pop bp
    ret

global _strcmp
_strcmp:
    mov ax, 0
    ret

global _strncmp
_strncmp:
    mov ax, 0
    ret

global _strchr
_strchr:
    mov ax, 0
    ret

global _strcat
_strcat:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _strcspn
_strcspn:
    mov ax, 0
    ret

global _strncpy
_strncpy:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _atoi
_atoi:
    mov ax, 0
    ret

global _sprintf
_sprintf:
    mov ax, 0
    ret

global _fprintf
_fprintf:
    mov ax, 0
    ret

global _printf
_printf:
    mov ax, 0
    ret

global _fputs
_fputs:
    mov ax, 0
    ret

global _fputc
_fputc:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _fread
_fread:
    mov ax, 0
    ret

global _fwrite
_fwrite:
    mov ax, 0
    ret

global _fseek
_fseek:
    mov ax, 0
    ret

global _ftell
_ftell:
    mov ax, 0
    ret

global _fflush
_fflush:
    ret

global _abort
_abort:
    mov ah, 4Ch
    mov al, 1
    int 21h

global _fopen
_fopen:
    mov ax, 0
    ret

; fopenb defined in dos.c
global _fclose
_fclose:
    mov ax, 0
    ret

global _getc
_getc:
    mov ax, -1
    ret

global _rename
_rename:
    ret

global _putc
_putc:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _putchar
_putchar:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    push bx
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop bx
    mov ax, [bp+4]
    pop bp
    ret

global _isalpha
_isalpha:
    mov ax, 0
    ret

global _isdigit
_isdigit:
    mov ax, 0
    ret

global _toupper
_toupper:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _tolower
_tolower:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _exit
_exit:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov ah, 4Ch
    int 21h

global _fgets
_fgets:
    mov ax, 0
    ret

global _isspace
_isspace:
    mov ax, 0
    ret

global _getenv
_getenv:
    mov ax, 0
    ret

global _system
_system:
    mov ax, 0
    ret

; DOS API helpers (stubs — real impl belongs in doslib.asm).
global _dos_cls
_dos_cls:
    ret
global _dos_gotoxy
_dos_gotoxy:
    ret
global _dos_kbhit
_dos_kbhit:
    mov ax, 0
    ret
global _dos_putch
_dos_putch:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov ah, 02h
    int 21h
    pop bp
    ret
global _dos_getvidmode
_dos_getvidmode:
    mov ax, 0
    ret

; Stubs for stevie globals that don't have a clear single-file home.
; _params now lives in param.c (compiles via struct-array initializers).
; _gchar and _inc live in ptrfunc.c.

; FILE* stdio handles.  Near pointers (2 bytes) — values are non-zero
; sentinels (DOS handle numbers) so NULL checks pass.
global _stdin, _stdout, _stderr
_stdin:  dw 1   ; sentinel (DOS handle 0 = stdin)
_stdout: dw 2   ; sentinel (DOS handle 1 = stdout)
_stderr: dw 3   ; sentinel (DOS handle 2 = stderr)

global _updatetabstoptable
_updatetabstoptable:
    ret
; Turbo C delay()/disable()/enable() stubs — stevie compiles with
; __TURBOC__ defined so it expects these.
global _delay
_delay:
    ret
global _disable
_disable:
    cli
    ret
global _enable
_enable:
    sti
    ret
; NOTE: filemess, readfile, writeit, renum live in fileio.c.
; updatescreen, updateline, cursupdate, s_ins, s_del live in screen.c.
; All compile to QBE asm now, so we don't stub them.

; regerror is now in search.c which compiles.

; Standard C / MS-DOS / curses functions stevie expects.

; int getch(void) — read keystroke from BIOS, no echo.
; Uses INT 16h AH=00h to also capture function/arrow keys via the
; extended scancode pattern (returns scancode<<8 | 0 for function keys,
; the way Microsoft C's getch did it).
global _getch
_getch:
    push bp
    mov bp, sp
    xor ah, ah
    int 16h          ; AH=scancode, AL=ASCII
    cmp al, 0
    jne .ascii
    ; Function key: return 0 first, save scancode for next call
    mov [cs:.fn_pending], ah
    mov byte [cs:.fn_flag], 1
    xor ax, ax
    pop bp
    ret
.ascii:
    xor ah, ah       ; return only the ASCII char in AX
    pop bp
    ret
.fn_pending: db 0
.fn_flag:    db 0

; int int86(int intno, union REGS *in, union REGS *out)
; REGS layout: ax(0), bx(2), cx(4), dx(6), si(8), di(10), cflag(12), flags(14).
;
; minic emits near pointers as 16-bit (`w`), so each REGS* arg occupies
; 2 bytes on the stack.
;
; Stack layout (after push bp / mov bp, sp, near-call return addr):
;   [bp+4]  intno (int = 16-bit)
;   [bp+6]  inregs offset
;   [bp+8]  outregs offset
;
; Dispatches the requested INT via self-modifying code.
global _int86
_int86:
    push bp
    mov bp, sp
    push si
    push di
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
    pop di
    pop si
    pop bp
    ret

; int intdos(union REGS *in, union REGS *out) — int86(0x21, in, out).
; Stack (near pointers, 2 bytes each): [bp+4] inregs, [bp+6] outregs.
global _intdos
_intdos:
    push bp
    mov bp, sp
    push si
    push di
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
    pop di
    pop si
    pop bp
    ret
global _signal
_signal:
    mov ax, 0
    ret
global _sleep
_sleep:
    ret
global _stat
_stat:
    mov ax, -1
    ret
global _chmod
_chmod:
    mov ax, 0
    ret
global _mktemp
_mktemp:
    ; mktemp(template) — return template unchanged
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret
global _islower
_islower:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
    mov ax, 1
    pop bp
    ret
.no:
    xor ax, ax
    pop bp
    ret
global _isupper
_isupper:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    ja .no
    mov ax, 1
    pop bp
    ret
.no:
    xor ax, ax
    pop bp
    ret
; char *strrchr(char *s, int c) — near pointer (2 bytes), c is w (2 bytes).
global _strrchr
_strrchr:
    push bp
    mov bp, sp
    push si
    push bx
    mov si, [bp+4]   ; s
    mov bl, [bp+6]   ; c (low byte)
    xor ax, ax
.loop:
    mov dl, [si]
    cmp dl, 0
    je .done
    cmp dl, bl
    jne .skip
    mov ax, si
.skip:
    inc si
    jmp .loop
.done:
    pop bx
    pop si
    pop bp
    ret

; remove() is referenced but not in any source file we compile.
global _remove
_remove:
    mov ax, 0
    ret
