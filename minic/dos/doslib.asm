; DOS Runtime Library for i8086 Backend
; Provides essential DOS system call wrappers
; All functions use cdecl calling convention

BITS 16
CPU 8086

section .text

; ============================================================================
; Character I/O
; ============================================================================

; void dos_putchar(int ch)
; Output a single character to stdout
global _dos_putchar
_dos_putchar:
    push bp
    mov bp, sp
    mov dx, [bp+4]      ; Get character argument
    mov ah, 02h         ; DOS function 02h - write character
    int 21h
    pop bp
    ret

; int dos_getchar(void)
; Read a single character from stdin (with echo)
; Returns: character code in AX
global _dos_getchar
_dos_getchar:
    push bp
    mov bp, sp
    mov ah, 01h         ; DOS function 01h - read character with echo
    int 21h
    xor ah, ah          ; Clear high byte
    pop bp
    ret

; int dos_getch(void)
; Read a single character from stdin (no echo)
; Returns: character code in AX
global _dos_getch
_dos_getch:
    push bp
    mov bp, sp
    mov ah, 08h         ; DOS function 08h - read character without echo
    int 21h
    xor ah, ah          ; Clear high byte
    pop bp
    ret

; void dos_puts(char *str)
; Output a null-terminated string to stdout
; Note: DOS INT 21h AH=09h expects '$' terminated strings
global _dos_puts
_dos_puts:
    push bp
    mov bp, sp
    push si

    mov si, [bp+4]      ; Get string pointer
.loop:
    lodsb               ; Load byte from DS:SI to AL, increment SI
    cmp al, 0           ; Check for null terminator
    je .done
    mov dl, al
    mov ah, 02h         ; DOS function 02h - write character
    int 21h
    jmp .loop
.done:
    pop si
    pop bp
    ret

; ============================================================================
; File I/O
; ============================================================================

; int dos_open(char *filename, int mode)
; Open a file
; Args: filename pointer, mode (0=read, 1=write, 2=read/write)
; Returns: file handle or -1 on error
global _dos_open
_dos_open:
    push bp
    mov bp, sp
    mov dx, [bp+4]      ; Filename pointer
    mov al, [bp+6]      ; Access mode
    mov ah, 3Dh         ; DOS function 3Dh - open file
    int 21h
    jc .error           ; Jump if carry flag set (error)
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_create(char *filename)
; Create a new file
; Args: filename pointer
; Returns: file handle or -1 on error
global _dos_create
_dos_create:
    push bp
    mov bp, sp
    mov dx, [bp+4]      ; Filename pointer
    xor cx, cx          ; Attributes = 0 (normal file)
    mov ah, 3Ch         ; DOS function 3Ch - create file
    int 21h
    jc .error
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; void dos_close(int handle)
; Close a file
; Args: file handle
global _dos_close
_dos_close:
    push bp
    mov bp, sp
    mov bx, [bp+4]      ; File handle
    mov ah, 3Eh         ; DOS function 3Eh - close file
    int 21h
    pop bp
    ret

; int dos_read(int handle, char *buffer, int count)
; Read from a file
; Args: file handle, buffer pointer, byte count
; Returns: number of bytes read, or -1 on error
global _dos_read
_dos_read:
    push bp
    mov bp, sp
    mov bx, [bp+4]      ; File handle
    mov dx, [bp+6]      ; Buffer pointer
    mov cx, [bp+8]      ; Byte count
    mov ah, 3Fh         ; DOS function 3Fh - read from file
    int 21h
    jc .error
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_write(int handle, char *buffer, int count)
; Write to a file
; Args: file handle, buffer pointer, byte count
; Returns: number of bytes written, or -1 on error
global _dos_write
_dos_write:
    push bp
    mov bp, sp
    mov bx, [bp+4]      ; File handle
    mov dx, [bp+6]      ; Buffer pointer
    mov cx, [bp+8]      ; Byte count
    mov ah, 40h         ; DOS function 40h - write to file
    int 21h
    jc .error
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_delete(char *filename)
; Delete a file
; Args: filename pointer
; Returns: 0 on success, -1 on error
global _dos_delete
_dos_delete:
    push bp
    mov bp, sp
    mov dx, [bp+4]      ; Filename pointer
    mov ah, 41h         ; DOS function 41h - delete file
    int 21h
    jc .error
    xor ax, ax          ; Return 0 on success
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; ============================================================================
; Directory Operations
; ============================================================================

; int dos_chdir(char *path)
; Change current directory
; Args: path pointer
; Returns: 0 on success, -1 on error
global _dos_chdir
_dos_chdir:
    push bp
    mov bp, sp
    mov dx, [bp+4]      ; Path pointer
    mov ah, 3Bh         ; DOS function 3Bh - change directory
    int 21h
    jc .error
    xor ax, ax
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_mkdir(char *path)
; Create a directory
; Args: path pointer
; Returns: 0 on success, -1 on error
global _dos_mkdir
_dos_mkdir:
    push bp
    mov bp, sp
    mov dx, [bp+4]      ; Path pointer
    mov ah, 39h         ; DOS function 39h - create directory
    int 21h
    jc .error
    xor ax, ax
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_rmdir(char *path)
; Remove a directory
; Args: path pointer
; Returns: 0 on success, -1 on error
global _dos_rmdir
_dos_rmdir:
    push bp
    mov bp, sp
    mov dx, [bp+4]      ; Path pointer
    mov ah, 3Ah         ; DOS function 3Ah - remove directory
    int 21h
    jc .error
    xor ax, ax
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; ============================================================================
; System Functions
; ============================================================================

; int dos_get_version(void)
; Get DOS version
; Returns: AH = major version, AL = minor version
global _dos_get_version
_dos_get_version:
    push bp
    mov bp, sp
    mov ah, 30h         ; DOS function 30h - get DOS version
    int 21h
    ; AX now contains version (AH=major, AL=minor)
    pop bp
    ret

; void dos_exit(int code)
; Exit to DOS with return code
; Args: exit code
global _dos_exit
_dos_exit:
    mov ah, 4Ch         ; DOS function 4Ch - exit program
    mov al, [sp+2]      ; Get exit code from stack
    int 21h
    ; Never returns

; ============================================================================
; Memory Functions
; ============================================================================

; void *dos_malloc(int paragraphs)
; Allocate memory
; Args: number of paragraphs (16-byte blocks)
; Returns: segment address or 0 on error
global _dos_malloc
_dos_malloc:
    push bp
    mov bp, sp
    mov bx, [bp+4]      ; Number of paragraphs
    mov ah, 48h         ; DOS function 48h - allocate memory
    int 21h
    jc .error
    ; AX contains segment address
    pop bp
    ret
.error:
    xor ax, ax          ; Return 0 on error
    pop bp
    ret

; int dos_free(void *segment)
; Free allocated memory
; Args: segment address
; Returns: 0 on success, -1 on error
global _dos_free
_dos_free:
    push bp
    mov bp, sp
    push es
    mov ax, [bp+4]      ; Segment address
    mov es, ax
    mov ah, 49h         ; DOS function 49h - free memory
    int 21h
    pop es
    jc .error
    xor ax, ax
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; ============================================================================
; Time/Date Functions
; ============================================================================

; int dos_get_time(void)
; Get system time
; Returns: CH=hours, CL=minutes, DH=seconds, DL=hundredths
global _dos_get_time
_dos_get_time:
    push bp
    mov bp, sp
    push cx
    push dx
    mov ah, 2Ch         ; DOS function 2Ch - get time
    int 21h
    ; Pack time into return value
    mov ax, cx          ; AX = hours:minutes
    pop dx              ; Restore seconds:hundredths
    pop cx
    pop bp
    ret

; int dos_get_date(void)
; Get system date
; Returns: CX=year, DH=month, DL=day
global _dos_get_date
_dos_get_date:
    push bp
    mov bp, sp
    mov ah, 2Ah         ; DOS function 2Ah - get date
    int 21h
    mov ax, cx          ; AX = year
    pop bp
    ret
