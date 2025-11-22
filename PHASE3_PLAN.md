# Phase 3: DOS Integration - Implementation Plan

**Date:** 2025-11-22
**Status:** 🚀 Starting Implementation
**Goal:** Complete DOS runtime library and create working demonstration programs

## Overview

Phase 3 builds on the completed integer (Phase 1) and FPU (Phase 2) support to create a complete DOS development environment with a comprehensive runtime library and example programs.

## Implementation Strategy

### 1. DOS API Library (Priority: HIGH)

**Core System Calls:**
- ✅ Exit program (INT 21h AH=4Ch)
- ✅ Character output (INT 21h AH=02h)
- 🔨 Character input (INT 21h AH=01h)
- 🔨 String output (INT 21h AH=09h)
- 🔨 Get DOS version (INT 21h AH=30h)
- 🔨 Get/Set file attributes
- 🔨 Get current directory

**File I/O:**
- 🔨 Open file (INT 21h AH=3Dh)
- 🔨 Close file (INT 21h AH=3Eh)
- 🔨 Read from file (INT 21h AH=3Fh)
- 🔨 Write to file (INT 21h AH=40h)
- 🔨 Create file (INT 21h AH=3Ch)
- 🔨 Delete file (INT 21h AH=41h)
- 🔨 Seek in file (INT 21h AH=42h)

**Memory Management:**
- 🔨 Allocate memory (INT 21h AH=48h)
- 🔨 Free memory (INT 21h AH=49h)
- 🔨 Resize memory (INT 21h AH=4Ah)

**Directory Operations:**
- 🔨 Get current directory (INT 21h AH=47h)
- 🔨 Change directory (INT 21h AH=3Bh)
- 🔨 Create directory (INT 21h AH=39h)
- 🔨 Remove directory (INT 21h AH=3Ah)

**Time/Date:**
- 🔨 Get system time (INT 21h AH=2Ch)
- 🔨 Get system date (INT 21h AH=2Ah)
- 🔨 Set system time (INT 21h AH=2Dh)
- 🔨 Set system date (INT 21h AH=2Bh)

### 2. Standard C Library Subset

**String Functions:**
- 🔨 strlen, strcpy, strcat, strcmp
- 🔨 memcpy, memset, memcmp

**Character Functions:**
- 🔨 isalpha, isdigit, isspace
- 🔨 toupper, tolower

**Formatted I/O:**
- 🔨 printf (basic integer/string formatting)
- 🔨 sprintf
- 🔨 puts, putchar

**Math Functions (using 8087):**
- 🔨 Basic operations wrapper
- 🔨 abs, fabs

### 3. Example Programs (10+)

**Basic Examples:**
1. ✅ Hello World (hello_dos.c)
2. 🔨 Echo program (reads and prints input)
3. 🔨 File copy utility
4. 🔨 Directory listing
5. 🔨 Calculator (using FPU)

**Intermediate Examples:**
6. 🔨 Text file viewer
7. 🔨 Simple text editor
8. 🔨 Memory usage display
9. 🔨 System information tool
10. 🔨 Benchmark program

**Advanced Examples:**
11. 🔨 VGA graphics demo (mode 13h)
12. 🔨 Mouse input demo
13. 🔨 TSR (Terminate-Stay-Resident) program

### 4. Build System Improvements

**Enhanced Pipeline:**
- 🔨 Multi-file compilation support
- 🔨 Library linking
- 🔨 Dependency tracking
- 🔨 Makefile generation

## Implementation Tasks

### Task 1: Complete DOS Runtime Library
**File:** `minic/dos/doslib.asm`
**Priority:** HIGH
**Lines:** ~500

Implement essential DOS system calls:
```asm
; String output
_dos_puts:
    push bp
    mov bp, sp
    mov dx, [bp+4]    ; String pointer
    mov ah, 09h       ; DOS print string
    int 21h
    pop bp
    ret

; File operations
_dos_open:
    push bp
    mov bp, sp
    mov dx, [bp+4]    ; Filename
    mov al, [bp+6]    ; Access mode
    mov ah, 3Dh       ; DOS open file
    int 21h
    jc .error
    ; Return file handle in AX
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret
```

### Task 2: Standard C Library
**File:** `minic/dos/libc.c`
**Priority:** HIGH
**Lines:** ~300

Implement C standard library functions:
```c
int strlen(char *s) {
    int len;
    len = 0;
    while (s[len]) {
        len = len + 1;
    }
    return len;
}

void strcpy(char *dest, char *src) {
    int i;
    i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i = i + 1;
    }
    dest[i] = 0;
}

// Basic printf (integers and strings only)
void printf(char *fmt, ...) {
    // Implementation using dos_putchar
}
```

### Task 3: Example Programs
**Directory:** `minic/dos/examples/`
**Priority:** MEDIUM

Create working demonstration programs:

**echo.c:**
```c
main() {
    char ch;
    dos_puts("Echo program. Press ESC to exit.\r\n$");
    while (1) {
        ch = dos_getchar();
        if (ch == 27) break;  // ESC
        dos_putchar(ch);
    }
    return 0;
}
```

**filecopy.c:**
```c
main() {
    int src, dst;
    char buffer[512];
    int bytes;

    src = dos_open("input.txt", 0);  // Read mode
    dst = dos_create("output.txt");

    while ((bytes = dos_read(src, buffer, 512)) > 0) {
        dos_write(dst, buffer, bytes);
    }

    dos_close(src);
    dos_close(dst);
    return 0;
}
```

### Task 4: Graphics Demo
**File:** `minic/dos/examples/vga_demo.c`
**Priority:** LOW

Simple VGA mode 13h graphics:
```c
void set_vga_mode() {
    // INT 10h AH=00h AL=13h
}

void plot_pixel(int x, int y, char color) {
    char *vga;
    vga = 0xA000:0000;  // VGA memory
    vga[y * 320 + x] = color;
}

main() {
    int x, y;
    set_vga_mode();

    // Draw some pixels
    for (y = 0; y < 200; y = y + 1) {
        for (x = 0; x < 320; x = x + 1) {
            plot_pixel(x, y, (x + y) & 0xFF);
        }
    }

    dos_getchar();  // Wait for key
    return 0;
}
```

## Success Criteria

### Minimal Viable Product (MVP)
- [ ] 10+ DOS API functions implemented
- [ ] 5+ working example programs
- [ ] File I/O working
- [ ] String functions available
- [ ] Build system handles multi-file programs

### Phase 3 Complete
- [ ] 20+ DOS API functions
- [ ] 10+ example programs
- [ ] Graphics demo works
- [ ] Mouse support demo
- [ ] Complete C library subset
- [ ] Professional documentation

## Testing Strategy

1. **Unit Tests:** Each DOS function tested individually
2. **Integration Tests:** Multi-file programs
3. **DOSBox Validation:** All programs run in DOSBox
4. **Real Hardware:** Test on actual DOS machine (optional)

## Timeline

**Week 1: Core Library** (Days 1-3)
- Implement essential DOS API functions
- Create basic C library functions
- Test in DOSBox

**Week 2: Examples** (Days 4-5)
- Create demonstration programs
- File I/O examples
- System utilities

**Week 3: Advanced** (Days 6-7)
- Graphics demos
- Mouse support
- TSR example
- Documentation

## Resources

**DOS API Reference:**
- INT 21h (DOS Services)
- INT 10h (Video Services)
- INT 16h (Keyboard Services)
- INT 33h (Mouse Services)

**Documentation:**
- Ralph Brown's Interrupt List
- DOS Programmer's Reference
- NASM Assembly Language Reference

---

**Let's begin with Task 1: DOS Runtime Library**
