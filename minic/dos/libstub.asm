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
