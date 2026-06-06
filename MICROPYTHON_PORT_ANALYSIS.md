# Porting MicroPython to QBE C11/C17 8086 DOS — Gap Analysis

**Date:** 2026-05-28
**Author:** Claude (Anthropic), with Paul Devine
**Target:** MicroPython on 8086 real-mode DOS, built with the `minic → qbe (-t i8086) → nasm → omf_link` pipeline
**Compiler baseline:** C11/C17 feature set + GNU extensions (see `NEW_FEATURES_DOCUMENTATION.md`)

---

## TL;DR

The C11/C17 **language** gaps are real but mostly *fixable parser work*. The
true ceiling is the **16-bit real-mode segmented memory model**, which violates
MicroPython's two most basic assumptions:

1. a **flat, ≥32-bit address space** (MP's GC scans one contiguous heap as an
   array of machine pointers), and
2. a **uniform machine-word integer/pointer** (`mp_int_t == uintptr_t ≥ 32 bits`).

Real-mode 8086 provides neither. Even with every language gap closed, you'd be
fighting the GC and object model against 64 KB segments. A **minimal REPL** with
a static ~32 KB heap, no float, no bignum, near-data only (medium model) is the
only realistically reachable target — and it would be slow and cramped.

**This is a research-grade effort, not a weekend port.**

---

## How this was assessed

All compiler results below were produced empirically on 2026-05-28 by feeding
constructs through `minic -m medium` (the same invocation `tools/build-example.sh`
uses) and inspecting parse success and emitted SSA. Runtime/header facts come
from `minic/include/`, `minic/dos/libstub.asm`, and `tools/libstub_to_exe.py`.

---

## 1. Compiler / language gaps

The docs list designated initializers, compound literals, etc. as "supported,"
but support is **narrow**: compound-literal *assignment inside a function body*
works; file-scope aggregate initializers and several other pervasive C forms do
not.

| Construct MicroPython relies on | Result | Why it matters for MP |
|---|---|---|
| Adjacent string literals `"foo" "bar"` | ❌ FAIL | Pervasive: error strings, qstr defs, multi-line literals |
| File-scope aggregate init `struct E t[]={{...},{...}}` | ❌ FAIL | How **every** module/method/type table is defined |
| File-scope designated init `{.name=..,.fun=..}` | ❌ FAIL | The core MP module/type-table idiom |
| Function-pointer array init `int(*t[])()={a,b}` | ❌ FAIL | Dispatch / method tables |
| Multi-dimensional arrays `int g[2][3]` | ❌ FAIL | Several lookup tables |
| `long long` *declaration* | ❌ FAIL (parser rejects the decl) | No usable 64-bit integer type |
| Struct copy > 8 bytes | ⚠️ parses, **codegen truncates to 8 bytes** | MP objects are structs → silent corruption |
| Compound-literal *assignment* `a=(struct P){...}` | ✅ OK | The one supported struct-init idiom (function scope only) |
| `int64_t`/`uint64_t` (`<stdint.h>`) | ⚠️ typedef'd to 32-bit `long` — **header lies** | Anything assuming 64-bit silently truncates |
| `double` | ⚠️ mapped to 32-bit (`LNG\|FLOAT`), non-conforming | MP float is optional; can disable |
| `_Static_assert`, `_Generic`, `_Alignof/_Alignas` | ✅ OK | Available if needed |
| Designated/compound literals (function scope) | ✅ OK | Usable with source rewrites |
| GNU `__attribute__`, inline `__asm__`, `__far` | ✅ OK | Useful for the port glue |

These are frontend (`minic.y`) limitations — fixable, but it's a lot of grammar
work, and the **struct-copy-≤8-bytes** truncation is a deeper i8086 backend bug
that would corrupt MP's object structs (`mp_obj_*`) wholesale.

See also the existing memory note on minic parser quirks (file-scope struct
init and fn-ptr-array init both parse-error; workaround is declare-uninitialized
+ assign in `main`).

## 2. Runtime / standard-library gaps

| Need | Status |
|---|---|
| **`setjmp` / `longjmp`** | ❌ **Absent entirely** — no `<setjmp.h>`. MP's exception mechanism (NLR, non-local return) needs this *or* a hand-written `nlr8086.S`. MP ships NLR for 32-bit x86/thumb/etc. but **not** 16-bit 8086. **Hard blocker.** |
| `realloc` / `calloc` | ❌ Only `malloc` + `free` (freelist allocator in `libstub_to_exe.py`). MP's allocator wants `realloc`; you'd force a **static GC heap pool** instead. |
| Available headers | `ctype, stdio, stdlib, string, strings, stdint, stdbool, stddef, signal, dos, sys` only. **Missing:** `math.h`, `assert.h`, `errno.h`, `time.h`. |
| Heap size | **~34 KB** (`_heap_size = 34816`, medium model), and it lives *inside* the 64 KB DGROUP shared with all near data + the stack. |
| `printf`/`sprintf` family | ✅ real impl (width/precision/`l` modifier); MP mostly uses its own `mp_printf` anyway. |
| string/mem functions | ✅ near + `_far_*` variants in libstub. |

## 3. The fundamental wall: 16-bit real-mode segmented memory

This is the part that is **not** "just more work":

- **MicroPython assumes a flat address space.** Its mark-sweep GC treats the
  heap as one contiguous block and scans it as an array of machine pointers.
  Here the heap is capped at ~34 KB inside DGROUP. To exceed 64 KB you must use
  **huge model**, where every pointer is a 32-bit far pointer requiring segment
  *normalization* on each arithmetic op (the `_qbe_huge_add`/`_qbe_huge_sub`
  path). MP dereferences and does arithmetic on heap pointers constantly — high
  correctness risk and enormous speed cost.
- **MicroPython assumes `mp_int_t` == machine word ≥ 32 bits.** The 8086 machine
  word is 16-bit, so small ints would be ~15-bit (useless). You'd force 32-bit
  `mp_int_t` → every integer op routes through DX:AX register pairs plus libstub
  helpers (`_qbe_div32`, etc.). Slow, and true 64-bit (`time`, bignum interop)
  is unavailable.
- **Object representation.** MP's pointer tagging assumes a uniform
  `uintptr_t`-sized pointer with known alignment. The 8086 near/far split
  (2-byte near vs 4-byte far, with non-canonical seg:off encodings) breaks those
  assumptions; under huge model the GC would have to normalize every scanned
  far pointer.
- **Tiny stack.** Lexer/parser/compiler are recursive and the VM nests. A ~4 KB
  real-mode stack with 4-byte far-call return frames overflows on modest
  nesting depth.
- **Code size.** Multi-segment code works (stevie links at 268 KB), so a stripped
  REPL at ~80–150 KB is *plausible to link* — but the bytecode interpreter inner
  loop pays far-call overhead per dispatch.

## 4. Toolchain / build

Mostly tractable. MP's qstr and frozen-module generation is **host-side Python**
and emits C, which you'd then run through `minic_cpp → minic → qbe → nasm →
omf_link` (mirroring `tools/build-stevie.sh`). The work is wiring ~50+ core
`.c` files through that pipeline — but each must first survive the §1 parser
gaps. MP's preprocessor usage (`#`/`##` token paste/stringize, X-macros) is
**supported** by `minic_cpp` (verified), which is a bright spot.

---

## Verdict & tiers

**Tier 1 — hard blockers (nothing runs until these are solved):**
1. `setjmp`/`longjmp` (or a custom 8086 NLR `.S`).
2. File-scope aggregate / designated-initializer tables (module/type/method tables).
3. Adjacent string-literal concatenation.
4. Struct-copy > 8 bytes codegen (currently truncates → object corruption).

**Tier 2 — heavy but mechanical:**
- Multi-dimensional arrays.
- `realloc` (or commit to a static GC heap pool).
- Missing headers (`assert.h`, `errno.h`, `math.h`, `time.h` — most can be stubbed).
- Force 32-bit `mp_int_t`; disable float and bignum.

**Tier 3 — the real ceiling (architectural):**
- Segmented memory vs. MP's flat-heap + machine-word-int assumptions.
- Far-pointer GC correctness/speed under huge model.
- Tiny stack vs. recursive parser/compiler/VM.

**Realistically reachable target:** a minimal REPL — medium model, static
~32 KB heap, near-data only, no float, no bignum, no filesystem `import`. It
would be slow and memory-cramped, and getting there still requires all of
Tier 1.

---

## Highest-leverage first step

If pursued, start with the **frontend initializer/aggregate gaps**
(file-scope designated tables, adjacent strings, multi-dim arrays). These are
prerequisites for MP *and* exactly what the self-host track (k,
`tools/build-example.sh --model=huge minic/y.tab.c`) would exercise — so the
effort compounds. Solve the struct-copy-≤8-bytes backend bug alongside, since
both MP and any nontrivial C program hit it.
