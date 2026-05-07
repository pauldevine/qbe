; Minimal libc stubs for stevie-dos prototype build.
;
; These are placeholder implementations that allow the binary to link.
; A real port needs proper printf/fopen/malloc on top of the doslib DOS
; INT 21h wrappers.  For now we just want a successful link.

global _malloc
_malloc:
    mov ax, 0
    ret

global _free
_free:
    ret

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

global _strcpy
_strcpy:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]
    mov si, [bp+6]
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

; Standard C / MS-DOS / curses functions stevie expects.  All return 0
; so the binary at least links and runs; real behavior would be added
; in a follow-up DOS runtime.
global _getch
_getch:
    mov ax, 0
    ret
global _int86
_int86:
    mov ax, 0
    ret
global _intdos
_intdos:
    mov ax, 0
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
global _strrchr
_strrchr:
    ; strrchr(s, c) — find last c in s; trivial loop
    push bp
    mov bp, sp
    push si
    push bx
    mov si, [bp+4]   ; s
    mov bl, [bp+6]   ; c
    xor ax, ax       ; result = NULL
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
