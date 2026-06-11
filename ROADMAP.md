# QBE C11/C17 8086 Compiler: Implementation Roadmap

**Project:** C11/C17 + GNU-extensions C Compiler for 8086 DOS using QBE Backend
**Standard:** C11 feature set + GNU extensions (`__attribute__`, inline `__asm__`, `__far`); equivalently C17-level (C17 added no new language features over C11).  No C23 language features.  C only — no C++.
**Original Timeline:** 10-12 weeks to production release
**Actual Progress:** Original compiler goal COMPLETE; now in an open-ended hardening phase driven by a real-world MicroPython port (as of 2026-06-07)
**Real target hardware:** Victor 9000 / Sirius 1 (~896 KB RAM), not the IBM-PC 640 KB ceiling
**Last Updated:** 2026-06-07

---

## ⚠️ DOCUMENTATION NOTE

This roadmap was created on 2025-11-21 as a **plan** for implementation. The "Actual Current Status" section below tracks what's actually shipped against that plan; the long Phase 1–4 sections that follow it are the original plan, preserved for reference but not edited as work landed.

**The original 4-phase plan is done.** Since ~2026-05-28 the project has been in a *hardening phase*: porting **MicroPython** as a real-world stress test on real Victor 9000 hardware. Each MicroPython failure is reduced to a focused `minic/dos/examples/*_probe.c`, the underlying compiler/backend/runtime bug is fixed, and the probe is added to the `tools/test-dos.sh` gate (now **215/215**). Day-to-day status lives in `CLAUDE.md` and `NEXT_SESSION.md`; this file is the high-level reconciliation.

---

## Actual Current Status (Updated 2026-06-07)

**Component Status:**

| Component | Status | Details | Evidence |
|-----------|--------|---------|----------|
| **MiniC Compiler** | ✅ Complete | C89/C99 + C11 features | minic/minic.y |
| **i8086 Backend** | ✅ Complete | All integer + FPU ops | i8086/*.c |
| **8087 FPU Support** | ✅ **COMPLETE** | Full hardware float/double (PR #11) | i8086/emit.c:76-131 |
| **Inline Assembly** | ✅ **COMPLETE** | GCC-style extended asm (commits d44ea80, c0ddbff) | minic/minic.y:2124-3292 |
| **C11 Features** | ✅ **COMPLETE** | All 6 target features (PR #12) | minic/test/c11/ |
| **Far Pointers** | ✅ **COMPLETE** | Small model support (PR #13) | commit 6492370 |
| **ANSI Functions** | ✅ **COMPLETE** | Function definitions (PR #15) | commit 03d0b81 |
| **32-bit Long** | ✅ Complete | DX:AX pairs + div/rem via libstub helpers (c53ce0a) | i8086/README.md:320 |
| **Function Pointers** | ✅ Complete | Full support with typedef | i8086/README.md:321 |
| **Struct Bitfields** | ✅ Complete | Packing and read/write | i8086/README.md:322 |
| **DOS Runtime** | ✅ **COMPLETE** | crt0_exe.asm + real printf/sprintf, freelist malloc/free, file I/O | minic/dos/libstub.asm |
| **Memory Models** | ✅ Tiny + Medium + Compact + Large + Huge incl. >64K arrays + 32-bit far load/store (Small .EXE still broken) | Per-model runtime gate at **215/215** in tools/test-dos.sh (was 59/59 at 2026-05-25; grown by the MicroPython-driven probes).  Huge >64K data via paragraph-aligned `_HUGE_<sym>` segments; huge stack ptr arith via `_qbe_huge_add`; `long` and `float` through a far pointer round-trip full 32 bits via `Oloadfl`/`Ostorefl` / `Oloadfs`/`Ostorefs`.  Small .EXE still hangs DOSBox (libstub_to_exe ret→retf rewrite mismatches small's near-call ABI). | tools/build-com-test.sh, tools/build-stevie.sh, tools/build-example.sh |
| **DOS API Library** | ✅ **COMPLETE** | int86/int86x/intdos/intdosx/segread + video/keyboard/mouse wrappers | minic/dos/libstub.asm, minic/include/dos.h |
| **Soft-Float (no-8087)** | ✅ **MODEL-COMPLETE** | Native `float` (Ks) lowered to `_sf_*` helper calls (no 8087 needed); arithmetic/compare/int↔float (medium) + far-data load/store via `loadfs`/`storefs` (compact/large/huge).  Double (Kd) deliberately unimplemented — single-precision only. | minic/dos/softfloat.c, i8086/emit.c, softfloat_probe.c / float_fardata_probe.c |
| **MicroPython Port** | ⚠️ **In hardening** | Builds (106/106 TUs), links, and runs on a real Victor 9000 over a SASI disk.  Language surface working: classes/inheritance, generators, exceptions/finally, comprehensions, closures, string methods, slicing, `str()`/`repr()`, dict/list rendering, GC.  Bounded by image-size ceiling and a deep-recursion stack tradeoff. | MICROPYTHON_PORT.md, tools/build-micropython.sh, tools/run-victor-sasi.sh |

**Phase Completion (vs original plan):**
- Phase 0 (Validation): ✅ **100% COMPLETE**
- Phase 1 (Integer DOS): ✅ **100% COMPLETE** (crt0_exe.asm + real runtime in libstub.asm)
- Phase 2 (8087 FPU): ✅ **100% COMPLETE** (PR #11)
- Phase 3 (DOS Integration): ✅ **~95% COMPLETE** (runtime, API wrappers, examples, full memory-model matrix incl. far DOS-API + far stdio FILE* under large/huge all done; small .EXE ABI mismatch + tiny .COM stevie shrink still pending)
- Phase 4 (C11 Features): ✅ **100% COMPLETE** (PR #12)
- Phase 5 (MicroPython hardening, not in original plan): ⚠️ **In progress** — see the dedicated section below

**Completed Features NOT in Original Roadmap:**
- ✅ Inline assembly with clobber lists
- ✅ Far pointer support
- ✅ ANSI C function definitions
- ✅ Variadic function support
- ✅ OMF link toolchain (`tools/omf_link.py`, `tools/asm_to_omf.py`, `tools/libstub_to_exe.py`)
- ✅ Stevie editor (`stevie.exe`) ports cleanly as the flagship integration test
- ✅ Soft-float (no-8087 single-precision `float`), model-complete incl. far-data
- ✅ MicroPython port — builds, links, and runs on real Victor 9000 hardware

**What's Actually Missing (compiler / backend / toolchain):**
1. **Small .EXE architecturally broken** — `tools/libstub_to_exe.py` rewrites every `ret` to `retf`, which mismatches small model's near-call ABI → DOSBox hangs.  Needs either near+far libstub variants or model-conditional `ret` rewrite.  See `[[per-model-gate]]`.  *This is the only open item from the original-plan backend list; the rest below are closed.*
2. **211-commit upstream-qbe rebase** — pure plumbing; deferred until the i8086 backend stabilises.
3. **Polish on the legacy examples** — 16 older `dos_putchar`-style files in `minic/dos/examples/` predate the `<dos.h>` API; modern dialect now demonstrated by `mouse_demo.c` / `vga_pixels.c` / `kbtest.c` / `cprobe.c` / `cstrprobe.c`.

**Parked / proven-out (kept for history):**
- ~~Tiny .COM stevie shrink~~ — **PARKED**: stevie is a medium-model program by design, ships as `.EXE` (148 KB).  Tiny pipeline still gated by `com_smoke.c` etc.
- ~~Latent Kl-CAddr arith matrix~~ — **CLOSED 2026-05-25 (aa–ee)**: full Kl-CAddr matrix portable across compact/large/huge.
- ~~Huge Phase B / B' storel-via-Kl-slot~~ — **CLOSED (ff/gg)**: spilled-Kl-ptr slots deref through ES:BX; Var-operand carveout removed.
- ~~storefar/loadfar lacks 32-bit width~~ — **CLOSED (hh)**: `Oloadfl`/`Ostorefl`; `long` round-trips full 32 bits through a far ptr.
- ~~i8086 compact loadfb AX-aliasing~~ — **CLOSED (jj)**: narrow far-loads bracketed with `kl_save_axdx`.
- ~~Phase B'' narrower stores~~ — **CLOSED as PROVEN UNREACHABLE 2026-05-28 (pp)**: cannot be produced by the current rega/isel; symmetric fix written, SSA-green, then reverted rather than ship untested-unreachable code.

**Note on tiny model:** Stevie itself doesn't fit in .COM (currently ~87KB; see `[[minic-pointer-bloat]]` for history) and that's expected — stevie is a medium-model program by design. The tiny-model pipeline is gated end-to-end by `tools/test-dos.sh` against `minic/dos/tests/com_smoke.c` at a 4 KB ceiling, which catches regressions in libstub size, codegen bloat, or the memref-base rega hint without holding stevie hostage to the 64 KB cap.

---

## Phase 5: MicroPython Port (hardening) ⚠️ IN PROGRESS

**Goal:** Use a real, large, third-party C program (MicroPython) as a stress test to flush out remaining compiler/backend/runtime bugs — and ultimately run Python on a real Victor 9000.
**Status:** ⚠️ MicroPython builds, links, and runs interactively on real Victor 9000 hardware; an open-ended list of edge-case bugs is being driven out one reduced probe at a time.

**Method (the working loop):**
1. Run a real MicroPython feature/script on the compact far-data Victor build.
2. When it fails, reduce the failure to a tiny `minic/dos/examples/*_probe.c`.
3. Fix the underlying QBE / MiniC / i8086 / runtime bug.
4. Add the reduced probe to `tools/test-dos.sh` before relying on the behavior.
5. Watch image size against the ~896 KB Victor load ceiling.

**Working today (verified on real Victor hardware):**
- Build/link: 106/106 translation units → OMF objects → linked `.exe`, under the ceiling.
- Bring-up keystones: `static`-linkage emission, `setjmp`/`longjmp` (NLR), per-symbol code-segment splitting, far-data placement, GC.
- Language surface: classes & inheritance, generators, exceptions/finally + tracebacks, list/dict/filter comprehensions, closures & `nonlocal`, kwargs/defaults, tuple unpacking, string methods, slicing, `str()`/`repr()`, dict/list rendering, allocation churn / GC.
- Interactive REPL with a DOS-native line editor (history, arrow keys) and disk-loaded `PROG.PY`.

**Open MicroPython-side items:**
1. **Enable floats** (`MICROPY_FLOAT_IMPL_FLOAT`) — the far-data soft-float backend is now ready (§3w); next is turning it on in the MicroPython build and running a float feature probe on Victor.  *Risk:* pulls in `objfloat.c` + float formatting and may bump the image-size ceiling.
2. **Image-size ceiling (~896 KB)** — builds run close to it; this constrains heap size and how many features ship at once.  No free shrink lever found; gains come from feature/config trims or dead-stripping.
3. **Deep-recursion frontier** — bounded by a transient-C-stack vs. image-size tradeoff, not a clean compiler bug.  `MICROPY_STACKLESS_STRICT` fixes recursion depth but doesn't fit the ceiling as the default build.
4. **Config-gated features** (e.g. `%` string formatting, extended `[::-1]` slices, `str.count`) are MicroPython minimal-config decisions, not compiler gaps — enable per need vs. image budget.

**Reference docs:** `MICROPYTHON_PORT.md`, `NEXT_SESSION.md` (per-session detail), `CLAUDE.md` (live status header).

---

## Original Planned Roadmap (for reference)

The sections below show the **original plan** from 2025-11-21. Many of these phases have been completed ahead of schedule.

---

## Phase 0: Validation & Setup (Week 1) ✅ COMPLETE

**Goal:** Verify the integer-only pipeline works end-to-end
**Status:** ✅ All tasks completed

### Tasks

- [x] **Build QBE with i8086 backend** ✅
  ```bash
  make clean && make
  ./qbe -h  # Verify i8086 target listed
  ```
  **Owner:** Build system
  **Time:** 1 hour

- [x] **Build MiniC compiler** ✅
  ```bash
  cd minic && make
  ./minic < test/simple_test.c > test.ssa
  ```
  **Owner:** Frontend
  **Time:** 1 hour

- [x] **Test integer-only compilation** ✅
  ```bash
  # Create simple test: hello_int.c
  echo 'int main() { return 42; }' > test.c
  ./minic < test.c > test.ssa
  ../qbe -t i8086 test.ssa > test.asm
  cat test.asm  # Verify assembly looks correct
  ```
  **Owner:** Integration
  **Time:** 2 hours

- [x] **Install DOS toolchain** ✅
  - NASM: `sudo apt-get install nasm` or download from nasm.us
  - OpenWatcom: Download v2 from github.com/open-watcom/open-watcom-v2
  - DOSBox: `sudo apt-get install dosbox`
  **Owner:** DevOps
  **Time:** 2 hours

- [x] **Assemble and link test program** ✅
  ```bash
  nasm -f obj test.asm -o test.obj
  # Note: Will fail without crt0.obj - that's expected for now
  ```
  **Owner:** Build system
  **Time:** 1 hour

### Deliverables

- [x] QBE builds successfully with i8086 support
- [x] MiniC compiles test programs to QBE IL
- [x] QBE generates 8086 assembly from IL
- [x] Toolchain installed (NASM, OpenWatcom, DOSBox)
- [x] Build issues documented

**Success Criteria:** Can generate assembly from C (even if not linkable yet)

---

## Phase 1: Integer-Only DOS Compilation (Week 2) ⚠️ ~80% COMPLETE

**Goal:** Full integer C programs compile and run on DOS
**Status:** ⚠️ crt0.asm exists, need complete DOS runtime library

### Task 1.1: DOS Startup Code (crt0.asm)

Create `minic/dos/crt0.asm`:

```asm
; DOS startup code (_start)
.model small
.stack 100h

.code
    public _start
_start:
    ; Set up segments
    mov ax, @data
    mov ds, ax
    mov es, ax

    ; Call main
    call _main

    ; Exit to DOS (return value in AX)
    mov ah, 4Ch
    int 21h

end _start
```

**Owner:** Runtime team
**Time:** 4 hours
**Files:** `minic/dos/crt0.asm`

### Task 1.2: Basic DOS Runtime (dos_runtime.c)

Create `minic/dos/dos_runtime.c`:

```c
// Basic DOS I/O functions
void putchar(int c) {
    // INT 21h, AH=02h - Write character to stdout
    asm volatile(
        "mov ah, 0x02\n"
        "mov dl, %0\n"
        "int 0x21\n"
        : : "r"((char)c) : "ah", "dl"
    );
}

void exit(int code) {
    // INT 21h, AH=4Ch - Terminate program
    asm volatile(
        "mov ah, 0x4C\n"
        "mov al, %0\n"
        "int 0x21\n"
        : : "r"((char)code) : "ah", "al"
    );
}

// Simple printf (integers only for now)
void printf(const char *fmt, ...) {
    // Basic implementation
    const char *p = fmt;
    while (*p) {
        if (*p == '%' && *(p+1) == 'd') {
            // Handle %d - left as TODO for now
            p += 2;
        } else {
            putchar(*p);
            p++;
        }
    }
}
```

**Owner:** Runtime team
**Time:** 8 hours
**Files:** `minic/dos/dos_runtime.c`, `minic/dos/dos_runtime.h`

### Task 1.3: Build Script (build-dos.sh)

Create `tools/build-dos.sh`:

```bash
#!/bin/bash
# Complete DOS build pipeline

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <program.c> [output.exe]"
    exit 1
fi

INPUT="$1"
OUTPUT="${2:-program.exe}"

echo "Building DOS executable: $INPUT -> $OUTPUT"

# Step 1: Compile C to QBE IL
echo "[1/5] MiniC: C -> QBE IL"
./minic/minic < "$INPUT" > temp.ssa

# Step 2: QBE: IL -> Assembly
echo "[2/5] QBE: IL -> Assembly"
./qbe -t i8086 temp.ssa > temp.asm

# Step 3: Assemble
echo "[3/5] NASM: Assembly -> Object"
nasm -f obj temp.asm -o temp.obj
nasm -f obj minic/dos/crt0.asm -o crt0.obj
nasm -f obj minic/dos/dos_runtime.asm -o runtime.obj

# Step 4: Link
echo "[4/5] Linker: Object -> Executable"
wlink system dos file temp.obj,crt0.obj,runtime.obj name "$OUTPUT"

# Step 5: Cleanup
echo "[5/5] Cleanup"
rm -f temp.ssa temp.asm temp.obj crt0.obj runtime.obj

echo "✓ Built: $OUTPUT"
```

**Owner:** Build team
**Time:** 4 hours
**Files:** `tools/build-dos.sh`

### Task 1.4: Test Programs

Create test programs in `minic/dos/examples/`:

1. **hello_basic.c** - No library functions
   ```c
   // Direct DOS INT 21h call
   int main() {
       char *msg = "Hello, DOS!\r\n$";
       asm volatile(
           "mov ah, 0x09\n"
           "mov dx, %0\n"
           "int 0x21\n"
           : : "r"(msg) : "ah", "dx"
       );
       return 0;
   }
   ```

2. **return_code.c** - Test return values
   ```c
   int main() {
       return 42;
   }
   ```

3. **arithmetic.c** - Integer math
   ```c
   int add(int a, int b) { return a + b; }
   int main() {
       int x = add(10, 32);
       return x;  // Should return 42
   }
   ```

**Owner:** QA team
**Time:** 4 hours

### Task 1.5: Fix 32-bit Long Support

Currently incomplete in i8086 backend. Add DX:AX register pair operations.

**Files to modify:**
- `i8086/isel.c` - Add Kl (long) instruction selection
- `i8086/emit.c` - Emit 32-bit operations using register pairs

**Owner:** Backend team
**Time:** 8 hours

### Deliverables

- [x] DOS startup code (crt0.asm)
- [x] Basic DOS runtime (putchar, exit)
- [x] Build script (build-dos.sh)
- [x] 3+ test programs compile and run
- [x] Return codes work correctly
- [x] 32-bit long support working

**Success Criteria:**
- Hello World runs in DOSBox
- Return codes verified (ERRORLEVEL in DOS)
- Integer arithmetic programs work

---

## Phase 2: 8087 Floating-Point Support (Weeks 3-5) ✅ COMPLETE

**Goal:** Full float/double support matching MiniC's capabilities
**Status:** ✅ Fully implemented in PR #11 (commit e01104b)
**Evidence:** i8086/emit.c:76-131, i8086/isel.c:141-196, i8086/examples/09_float.c

### Week 3: Foundation

#### Task 2.1: FPU Instruction Encoding (i8086/fpu.c)

Create new file `i8086/fpu.c` with basic FPU instruction emission:

```c
#include "all.h"

// Emit FPU instruction with no operands
static void emitfpu0(char *op) {
    fprintf(of, "\t%s\n", op);
}

// Emit FPU instruction with memory operand
static void emitfpum(char *op, Ref r) {
    fprintf(of, "\t%s ", op);
    // Emit memory operand (register or [mem])
    emitref(r);
    fprintf(of, "\n");
}

// Initialize FPU
void emitfpuinit() {
    emitfpu0("finit");  // Initialize FPU
}

// Load float from memory
void emitflds(Ref r) {
    emitfpum("fld dword ptr", r);
}

// Store float to memory and pop
void emitfstps(Ref r) {
    emitfpum("fstp dword ptr", r);
}

// Load double from memory
void emitfldd(Ref r) {
    emitfpum("fld qword ptr", r);
}

// Store double to memory and pop
void emitfstpd(Ref r) {
    emitfpum("fstp qword ptr", r);
}

// Basic arithmetic
void emitfadd() { emitfpu0("fadd"); }   // ST(0) += ST(1), pop
void emitfsub() { emitfpu0("fsub"); }
void emitfmul() { emitfpu0("fmul"); }
void emitfdiv() { emitfpu0("fdiv"); }
void emitfneg() { emitfpu0("fchs"); }   // Change sign
```

**Owner:** Backend team
**Time:** 16 hours
**Files:** `i8086/fpu.c` (new)

#### Task 2.2: Instruction Selection for FPU (i8086/isel.c)

Modify `i8086/isel.c` to handle float/double operations:

```c
// In sel() function, add cases for float operations
case Oadd:
    if (k == Ks || k == Kd) {
        // Float/double addition
        sel_fpu_binop(i, "add");
    } else {
        // Integer addition (existing code)
        ...
    }
    break;

case Osub:
    if (k == Ks || k == Kd) {
        sel_fpu_binop(i, "sub");
    } else {
        ...
    }
    break;

// Similar for mul, div

case Oloadw:
case Oloadl:
    if (k == Ks) {
        // Load float
        emit_fpu_load(i, "fld dword ptr");
    } else if (k == Kd) {
        // Load double
        emit_fpu_load(i, "fld qword ptr");
    } else {
        // Integer load (existing)
        ...
    }
    break;
```

**Owner:** Backend team
**Time:** 16 hours
**Files:** `i8086/isel.c` (modify)

### Week 4: Operations & Conversions

#### Task 2.3: FPU Comparisons

Add float/double comparison support:

```c
// In i8086/fpu.c
void emitfcmp() {
    fprintf(of, "\tfcom\n");       // Compare ST(0) with ST(1)
    fprintf(of, "\tfstsw ax\n");   // Store status word to AX
    fprintf(of, "\tsahf\n");        // Transfer AH to flags
    // Now can use regular conditional jumps (JB, JE, etc.)
}
```

Map QBE comparisons to FPU:
- `csltd/cslts` → `fcom` + `jb`
- `csled/csles` → `fcom` + `jbe`
- `ceqd/ceqs` → `fcom` + `je`
- `cned/cnes` → `fcom` + `jne`

**Owner:** Backend team
**Time:** 12 hours

#### Task 2.4: Type Conversions (int ↔ float)

```c
// In i8086/fpu.c

// Integer to float: fild (FPU Integer LoaD)
void emitfild(Ref r) {
    fprintf(of, "\tfild word ptr ");
    emitref(r);
    fprintf(of, "\n");
}

// Float to integer: fistp (FPU Integer STore and Pop)
void emitfistp(Ref r) {
    fprintf(of, "\tfistp word ptr ");
    emitref(r);
    fprintf(of, "\n");
}

// In isel.c, add cases:
case Ostosi:  // Float/double to int
    emit(Inone, kw(to), i->to, R, R);
    emitfistp(i->to);
    break;

case Oswtof:  // Int to float
    emitfild(i->arg[0]);
    emit(Inone, ks(to), i->to, R, R);
    break;
```

**Owner:** Backend team
**Time:** 8 hours

#### Task 2.5: FPU Register Allocation

8087 uses a register stack (ST(0)-ST(7)). Need stack management:

```c
// Track FPU stack depth
static int fpudepth = 0;

void fpupush() {
    fpudepth++;
    if (fpudepth > 8) die("FPU stack overflow");
}

void fpupop() {
    fpudepth--;
    if (fpudepth < 0) die("FPU stack underflow");
}

// When loading: fld -> push
// When storing: fstp -> pop
// Binary ops: fadd, fsub, etc. pop one operand
```

**Owner:** Backend team
**Time:** 12 hours

### Week 5: Testing & Polish

#### Task 2.6: FPU Test Suite

Create comprehensive tests in `minic/test/dos_fpu/`:

1. **basic_float.c** - Simple arithmetic
2. **float_compare.c** - Comparisons
3. **float_convert.c** - Int/float conversions
4. **double_ops.c** - Double precision
5. **mixed_types.c** - Mixed int/float expressions
6. **mandelbrot.c** - Real-world test

**Owner:** QA team
**Time:** 16 hours

#### Task 2.7: Bug Fixes & Edge Cases

- FPU initialization in function prologue
- FPU stack cleanup in epilogue
- Handle NaN, Infinity
- Rounding mode control
- Denormalized numbers

**Owner:** Backend team
**Time:** 16 hours

### Deliverables

- [x] FPU instruction encoding (i8086/fpu.c)
- [x] Instruction selection for float/double
- [x] FPU comparisons working
- [x] Type conversions (int ↔ float)
- [x] FPU register allocation
- [x] 16+ FPU tests passing
- [x] All 84 MiniC tests pass on DOS

**Success Criteria:**
- All float/double operations work
- Mandelbrot renderer produces correct output
- IEEE 754 compliance (basic)
- Performance within 10x of x86-32

---

## Phase 3: DOS System Integration (Weeks 6-8) ✅ ~95% COMPLETE

**Goal:** Production-quality DOS programs with full API access
**Status:** ✅ Runtime, API wrappers, examples, and the full memory-model matrix (tiny/medium/compact/large/huge incl. >64K data) all shipped (commits fc8d2bc, 28941ae, d36f103, 0d8ccba, 1fb83d7, 540e511, b22f92c); small .EXE ABI mismatch and tiny .COM stevie shrink still outstanding.

### Week 6: Complete DOS Runtime

#### Task 3.1: Enhanced printf

Full printf implementation supporting:
- `%d` - signed int
- `%u` - unsigned int
- `%x` - hexadecimal
- `%f` - float
- `%s` - string
- `%c` - char
- `%p` - pointer

**Owner:** Runtime team
**Time:** 16 hours
**Files:** `minic/dos/dos_runtime.c`

#### Task 3.2: File I/O

Implement DOS file operations:

```c
// In dos_runtime.c
int open(const char *path, int flags);
int close(int fd);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
long lseek(int fd, long offset, int whence);
```

Using DOS INT 21h:
- AH=3Dh - Open file
- AH=3Eh - Close file
- AH=3Fh - Read from file
- AH=40h - Write to file
- AH=42h - Move file pointer

**Owner:** Runtime team
**Time:** 16 hours

#### Task 3.3: Memory Allocation

Basic malloc/free using DOS memory allocation:

```c
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
```

**Owner:** Runtime team
**Time:** 12 hours

### Week 7: DOS Interrupt Library

#### Task 3.4: DOS Interrupt Interface (dos_int.h)

```c
// dos_int.h
union REGS {
    struct {
        unsigned int ax, bx, cx, dx, si, di, cflag;
    } x;
    struct {
        unsigned char al, ah, bl, bh, cl, ch, dl, dh;
    } h;
};

struct SREGS {
    unsigned int es, cs, ss, ds;
};

int int86(int intno, union REGS *in, union REGS *out);
int int86x(int intno, union REGS *in, union REGS *out, struct SREGS *seg);
```

**Owner:** Runtime team
**Time:** 8 hours

#### Task 3.5: Video Functions

Direct VGA memory access and BIOS interrupts:

```c
// Set video mode
void set_video_mode(int mode);  // INT 10h, AH=00h

// Write pixel in mode 13h (320x200x256)
void putpixel(int x, int y, unsigned char color);

// Direct VGA memory access
#define VGA_MEM 0xA000
unsigned char far *vga = (unsigned char far *)MK_FP(VGA_MEM, 0);
vga[y * 320 + x] = color;
```

**Owner:** Runtime team
**Time:** 8 hours

#### Task 3.6: Keyboard & Mouse

```c
// Keyboard (INT 16h)
int kbhit();              // Check if key pressed
int getch();              // Get character without echo
int getche();             // Get character with echo

// Mouse (INT 33h)
int mouse_reset();
void mouse_show();
void mouse_hide();
void mouse_get_pos(int *x, int *y, int *buttons);
```

**Owner:** Runtime team
**Time:** 12 hours

### Week 8: Memory Models

#### Task 3.7: Implement Memory Models

Currently only small model (code + data < 64K each). Add:

1. **Tiny model** (.COM files)
   - Single 64K segment for code+data+stack
   - CS=DS=ES=SS
   - ORG 100h (PSP overhead)

2. **Medium model**
   - Large code (far calls)
   - Small data (near pointers)

3. **Large model**
   - Large code (far calls)
   - Large data (far pointers)

**Compiler flag:** `./qbe -t i8086:tiny|small|medium|large`

**Owner:** Backend team
**Time:** 20 hours

### Deliverables

- [x] Complete printf / sprintf (all format specifiers, including `l` modifier; commit 775fd38)
- [x] File I/O (fopen/fclose/fread/fwrite/fprintf via INT 21h; commit fc8d2bc)
- [x] Memory allocation (freelist malloc/free; commits 76c213e, 19f6029)
- [x] DOS interrupt interface (int86 / int86x / intdos / intdosx / segread; commit 28941ae)
- [x] Video functions (VGA mode 13h via `set_video_mode` + `putpixel`; commit 28941ae)
- [x] Keyboard/mouse support (`kbhit`/`getche` in commit 28941ae; INT 33h mouse in d36f103)
- [x] Multiple memory models — tiny (.COM, gated by tools/test-dos.sh), medium, compact, large, huge (incl. >64K arrays via per-symbol `_HUGE_<sym>` segments) all done; small .EXE still broken by libstub `ret→retf` rewrite
- [x] 10+ DOS example programs (16 legacy + 3 modern `<dos.h>` demos = 19 total)

**Success Criteria:**
- File I/O programs work (stevie `:w` round-trips edits)
- Graphics programs display correctly (`vga_pixels.exe` gradient verified)
- Keyboard/mouse input responsive (`kbtest.exe`, `mouse_demo.exe`)
- Memory allocation reliable (freelist with ~39 KB heap)

---

## Phase 4: C11 Features (Weeks 9-12) ✅ COMPLETE

**Goal:** 60% C11 compliance with DOS-relevant features
**Status:** ✅ All 6 target features implemented in PR #12
**Evidence:** minic/minic.y (lines 1240, 2460, 3468), minic/test/c11/, NEW_FEATURES_DOCUMENTATION.md

### Week 9: High-Priority Features

#### Task 4.1: _Static_assert (1 day)

```c
// In minic.y, add to parser:
_Static_assert '(' expr ',' STR ')' ';'
{
    if (!eval_constant_expr($3)) {
        die($5);  // Emit error with user message
    }
}
```

**Owner:** Frontend team
**Time:** 8 hours

#### Task 4.2: Compound Literals (2 days)

```c
// Allow: function_call((struct Point){.x=10, .y=20})
// In minic.y:
postfix: '(' type ')' '{' initlist '}'
{
    // Allocate temporary
    // Initialize with initlist
    // Return lvalue to temporary
}
```

**Owner:** Frontend team
**Time:** 16 hours

#### Task 4.3: Designated Initializers (3 days)

```c
// Allow: struct Point p = {.x = 10, .y = 20};
// In minic.y, modify initlist:
initlist: inititem
        | initlist ',' inititem
        ;
inititem: '.' IDENT '=' expr
        | '[' expr ']' '=' expr
        | expr
        ;
```

**Owner:** Frontend team
**Time:** 24 hours

### Week 10: Medium-Priority Features

#### Task 4.4: Anonymous Struct/Union (2 days)

```c
// Allow unnamed nested struct/union
struct Packet {
    int type;
    union {  // No name
        int int_value;
        float float_value;
    };
};
// Access: packet.int_value (not packet.unnamed.int_value)
```

**Owner:** Frontend team
**Time:** 16 hours

#### Task 4.5: _Alignof/_Alignas (3 days)

```c
// _Alignof operator (like sizeof)
size_t align = _Alignof(double);  // Returns 8

// _Alignas specifier
_Alignas(16) char buffer[256];  // Align to 16 bytes

// In minic.y:
// Add ALIGNOF token
// In expr: ALIGNOF '(' type ')' -> returns alignment
// In dcls: ALIGNAS '(' NUM ')' type IDENT -> set alignment
```

**Owner:** Frontend team
**Time:** 24 hours

### Week 11: Advanced Features

#### Task 4.6: _Generic (5 days)

```c
// Type-generic macros
#define abs(x) _Generic((x), \
    int: abs_int, \
    float: abs_float, \
    double: abs_double)(x)

// Implementation: Template-like expansion
// Need type inference and multiple instantiation
```

**Owner:** Frontend team
**Time:** 40 hours

### Week 12: Polish & Documentation

#### Task 4.7: C11 Test Suite

Create C11 compliance tests:
- `test_static_assert.c`
- `test_compound_literals.c`
- `test_designated_init.c`
- `test_anonymous_union.c`
- `test_alignof.c`
- `test_generic.c`

**Owner:** QA team
**Time:** 16 hours

#### Task 4.8: Documentation

Complete documentation:
- C11 feature guide
- DOS API reference
- Example program collection
- Porting guide (Turbo C → QBE)

**Owner:** Documentation team
**Time:** 24 hours

### Deliverables

- [x] _Static_assert implemented and tested
- [x] Compound literals working
- [x] Designated initializers working
- [x] Anonymous struct/union working
- [x] _Alignof/_Alignas working
- [x] _Generic working
- [x] 20+ C11 tests passing
- [x] Complete documentation

**Success Criteria:**
- 60% C11 compliance measured by test suite
- All implemented features work correctly
- Documentation complete
- Example programs demonstrate all features

---

## Milestones & Checkpoints

### Milestone 1: Integer DOS Programs (End of Week 2)
- ✅ Build pipeline works end-to-end
- ✅ Hello World runs in DOSBox
- ✅ Integer arithmetic programs work

### Milestone 2: Float/Double Support (End of Week 5)
- ✅ All 84 MiniC tests pass on DOS
- ✅ 8087 FPU operations work
- ✅ Mandelbrot renderer produces correct output

### Milestone 3: DOS Integration (End of Week 8)
- ✅ File I/O works
- ✅ Graphics programs display correctly
- ✅ 10+ example programs complete

### Milestone 4: C11 Compliance (End of Week 12)
- ✅ 60% C11 compliance
- ✅ All target features implemented
- ✅ Documentation complete
- ✅ Ready for release

---

## Risk Management

### High-Risk Areas

1. **8087 FPU complexity** (Weeks 3-5)
   - **Risk:** Stack-based architecture is complex
   - **Mitigation:** Incremental testing, one instruction at a time
   - **Fallback:** Software FP emulation (very slow but functional)

2. **Memory model bugs** (Week 8)
   - **Risk:** Far pointers and segment management tricky
   - **Mitigation:** Start with small model only, add others later
   - **Fallback:** Ship with small model only if needed

3. **C11 _Generic complexity** (Week 11)
   - **Risk:** Type inference and instantiation complex
   - **Mitigation:** Study existing implementations (GCC, Clang)
   - **Fallback:** Skip _Generic, still achieve 50% C11

---

## Resource Requirements

### Team

- **Backend developers:** 1-2 people (8087 FPU, memory models)
- **Frontend developers:** 1-2 people (C11 features)
- **Runtime developers:** 1 person (DOS API library)
- **QA engineers:** 1 person (testing, validation)
- **Documentation:** 1 person (part-time)

### Tools

- NASM assembler (free)
- OpenWatcom v2 (free, open-source)
- DOSBox (free, for testing)
- 86Box (optional, for accurate testing)
- GNU make, yacc (standard Unix tools)

### Hardware (Optional)

- Real 80286/80386 system for final validation
- DOS 6.22 or FreeDOS

---

## Success Metrics

### Quantitative

- [x] 200+ tests, 95%+ pass rate
- [x] 95% C89, 70% C99, 60% C11 compliance
- [x] 10+ DOS example programs
- [x] Build time < 5 seconds for typical program
- [x] Code size < 50KB for basic programs

### Qualitative

- [x] Professional documentation
- [x] Clear error messages
- [x] Easy to use (single build script)
- [x] Reliable (no crashes on valid input)
- [x] Fast (comparable to Turbo C)

---

## Next Steps

**Immediate actions (this week):**

1. Build QBE and validate pipeline
2. Create DOS startup code (crt0.asm)
3. Create build script (build-dos.sh)
4. Test Hello World in DOSBox

**Decision points:**

- [ ] Approve 8087 FPU approach (vs software emulation)
- [ ] Set C11 feature priority
- [ ] Decide on memory model support (all vs small only for MVP)

**Tracking:**

- Weekly progress reports
- Test pass rate monitoring
- Documentation completeness
- Community feedback

---

---

## Addendum: Features Completed Outside Original Roadmap

The following features were implemented but were not part of the original roadmap:

### Inline Assembly Support ✅ COMPLETE
**Commits:** d44ea80, c0ddbff
**Evidence:** minic/minic.y:2124-3292, minic/test/asm_clobber_test.c

- GCC-style extended inline assembly
- Support for output/input operands
- Clobber lists with `__asm__` and `__asm__ volatile` keywords
- Full test suite demonstrating all features

### Far Pointer Support ✅ COMPLETE
**PR:** #13 (commit 6492370)
**Evidence:** i8086/ backend code

- Far pointer support for i8086 small memory model
- Proper handling of segment:offset addressing

### ANSI C Function Definitions ✅ COMPLETE
**PR:** #15 (commit 03d0b81)

- ANSI C-style function parameter declarations
- Compatibility with modern C syntax

### OMF Link Toolchain ✅ COMPLETE
**Commits:** 40f836c, d5d46d7, ea48d9e
**Files:** `tools/omf_link.py`, `tools/asm_to_omf.py`, `tools/libstub_to_exe.py`, `minic/dos/crt0_exe.asm`

- Python OMF linker emits MZ .EXE images directly (no Watcom dependency)
- `libstub_to_exe.py` rewrites near-call libstub into far-call form for medium-model EXE builds
- `crt0_exe.asm` parses PSP command tail into argv and sets ES=DGROUP

### Real DOS Runtime in libstub.asm ✅ COMPLETE
**Commits:** 6a90dc3, 775fd38, 004c9d8, fc8d2bc, 76c213e, 19f6029, c53ce0a
**File:** `minic/dos/libstub.asm`

- Full sprintf/printf with `%d %u %x %X %o %s %c`, width/precision/flags, and `l` 32-bit modifier
- File I/O: fopen (mode-aware), fread, fwrite, fclose, fputc, fputs, fprintf, fgetc, getc
- Freelist malloc/free (~39 KB heap) with first-fit allocation
- 32-bit div/rem helpers (`_qbe_div32{u,s}`, `_qbe_rem32{u,s}`) called from i8086 backend
- String helpers (strncpy, strchr, strcat, strcspn, etc.)

### DOS API (int86 family + high-level wrappers) ✅ COMPLETE
**Commits:** 28941ae (int86x trio + cheap wrappers), d36f103 (mouse)
**Files:** `minic/include/dos.h`, `minic/dos/libstub.asm`, `stevie-orig/int86x_probe.c`

- `int86`, `int86x`, `intdos`, `intdosx`, `segread` with full `union REGS` / `struct SREGS` support
- `set_video_mode` (INT 10h AH=00h) + `putpixel` (mode 13h far-poke at 0xA000)
- `kbhit` (INT 16h AH=01h), `getche` (INT 16h AH=00h + echo)
- `bdos` (Microsoft C compat shim over INT 21h)
- INT 33h mouse: `mouse_reset`, `mouse_show`, `mouse_hide`, `mouse_get_pos`
- Probe `int86x_probe.c` exercises the segment-aware path including AH=09h print-string

### Parameterized Example Build ✅ COMPLETE
**Commit:** d36f103
**File:** `tools/build-example.sh`

- One-shot build script for any `#include <dos.h>` demo
- Three new demos: `mouse_demo.c`, `vga_pixels.c`, `kbtest.c`
- Produces `build/examples/<name>/<name>.exe` linked against libstub_exe

### Stevie Editor Port ✅ COMPLETE
**Build:** `tools/build-stevie.sh --exe`
**Status:** stevie.exe is the flagship integration test; 26 modules link into a 148 KB EXE

- All 24 stevie source files compile through minic + qbe i8086 + OMF link
- File load/edit/`:w` round-trips real DOS files
- `/search` and regex work after the regex caller-save fix (`[[qbe-rega-avoid-mask-ignored]]`)
- Render loop fixed (commit c95dd44, see `[[qbe-gcm-sinks-load-past-call]]`)
- Currently medium-model .EXE only; tiny .COM build still over the 64 KB ceiling

---

**Roadmap Version:** 4.0 (original 4-phase compiler goal complete; reframed around the MicroPython hardening phase + soft-float model-complete)
**Last Updated:** 2026-06-07
**Original Date:** 2025-11-21
**Actual Status:** Original compiler goal (Phases 0–4) COMPLETE.  tiny/medium/compact/large/huge runtime-verified via a **215/215** per-model probe gate; soft-float single-precision is model-complete (medium + far-data, no 8087); MicroPython builds, links, and runs interactively on real Victor 9000 hardware.  Now in an open-ended hardening phase (Phase 5) where MicroPython surfaces edge-case bugs that get reduced to gated probes and fixed.
**Next Priority:** (1) **Enable MicroPython floats** (`MICROPY_FLOAT_IMPL_FLOAT`) now that far-data soft-float is ready (§3w) — watch the ~896 KB Victor image ceiling; (2) MicroPython image-size / deep-recursion tradeoff; (3) small .EXE ABI mismatch (`[[per-model-gate]]`); (4) 211-commit upstream-qbe rebase (deferred).
