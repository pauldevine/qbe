# MicroPython Port — Phase 0 Feasibility Spike Report

**Date:** 2026-05-29
**Method:** Built MicroPython `ports/minimal` on the host (generates `genhdr/`, proves the
minimal config compiles with clang: ~114 KB text / 32 KB data). Then pushed all 132 `py/*.c`
core files through the real target pipeline — system `cpp -P -nostdinc` + our `minic/include`
headers (+ spike stub headers for `assert/stdarg/limits/setjmp/errno/math/alloca`) → `minic -m medium`
— and cataloged failures. MicroPython v1.29.0-preview.

## Headline result

**132 / 132 core files fail in `minic`.** They all fail at the **same shared header,
`py/obj.h`** (included transitively by essentially every core file), on **standard-C
declaration-grammar constructs that minic does not parse** — *before any initializer is even
reached*.

**This revises the plan's Phase 1 scope.** The dominant blocker is **basic C declaration
grammar**, not (only) aggregate initializers. Aggregate/designated initializers remain
required, but they are reached only after the declaration-grammar gaps are closed.

## Ranked gap inventory (empirically confirmed)

Each tested with a minimal snippet through `minic -m medium`.

### Tier 1 — universal blockers (in `py/obj.h`; block all 132 files)

1. **Unnamed / abstract parameters in prototypes & function-pointer typedefs.**
   `void *alloca(unsigned long);` ❌ but `...(unsigned long n)` ✅.
   `typedef mp_obj_t (*mp_fun_2_t)(mp_obj_t, mp_obj_t);` ❌ (unnamed params).
   minic requires a name for every parameter. *Pervasive in all C headers.*

2. **Forward `typedef struct Foo Foo;`** (opaque/recursive struct idiom).
   `typedef struct _mp_obj_type_t mp_obj_type_t;` ❌. (Self-referential `struct node *next`
   inside a definition ✅ — only the standalone forward typedef fails.) *Universal in MP.*

3. **Flexible array members** `Type name[];` as the last struct member.
   `struct s { int n; char data[]; };` ❌. Used by `mp_obj_type_t.slots[]`, tuples, dicts, etc.

### Tier 2 — pervasive, block many files once Tier 1 is fixed

4. **`enum Tag` used as a type-specifier.** `enum E e;` / `enum E f(...)` ❌.
   (Anonymous `enum {…}` constants ✅; `typedef enum {…} name` ✅ — only the named-tag-as-type
   form fails.)

5. **File-scope aggregate & designated initializers** (the original "crux," still required):
   `struct P g={1,2};` ❌; `const struct E t[]={{"a",1},{"b",2}};` ❌; the
   `mp_rom_map_elem_t` / `mp_obj_type_t` designated tables ❌. (Design already drafted in the
   approved plan, §1a.)

### Tier 3 — minor / low-frequency

6. **Bare `_Bool` keyword** ❌ — but `bool` works (our `stdbool.h` does `typedef int bool`),
   so this only matters if MP uses `_Bool` directly (it mostly uses `bool`).
7. **`long long` declaration** ❌ (planned, §1b; aliases to 32-bit).
8. **Adjacent string literals**, **multi-dim arrays** — confirmed rare in MP; defer.

### Confirmed working (no action needed)

Named abstract-pointer params (`int f(const char *s, int n)`), fn-ptr typedefs with *named*
params, `static inline`, anonymous enums, `typedef enum {…} name`, self-referential struct
pointers, `const` qualifiers, C99 for-loop decls, cast-to-void statements, `do {…} while(0)`,
`bool` via stdbool.

## Missing libc headers (Phase 2 confirmation)

`cpp` stopped immediately on `#include <assert.h>`. Our `minic/include` lacks:
`assert.h`, `stdarg.h`, `limits.h`, `setjmp.h`, `errno.h`, `math.h`, `alloca.h`.
(Stubbed for the spike; real versions are Phase 2 — `setjmp.h` is the substantive one.)

## Revised Phase 1 scope (supersedes plan §1 ordering)

Do the **declaration-grammar** features *first* (they gate everything), then initializers:

- **1a (new, do first):** parameter abstract-declarators (unnamed params) in prototypes and
  fn-ptr typedefs; forward `typedef struct X X;`; flexible array members; `enum Tag` as a type.
  These are localized grammar additions in `minic.y`'s declarator / type-specifier / struct-member
  rules. They unblock parsing of `py/obj.h` → all 132 files get past the shared headers.
- **1b:** the aggregate/designated-initializer emitter (approved plan §1a, the `dataitem()`
  design) — reached only after 1a.
- **1c:** `long long` type rule; limit raises (16→64 members, NGlo 256→4096); `_Bool` if needed.

Re-run this spike harness (`build/mp-spike/run-spike.sh`) after 1a to get the *next* layer of
per-file failures (the first layer is entirely masked by the `py/obj.h` header wall today).

## Reusable spike harness

- `build/mp-spike/run-spike.sh` — cpp+minic over a file list, tallies OK / CPP_FAIL / MINIC_FAIL
  with the failing source line.
- `build/mp-spike/stubinc/` — temporary stub libc headers.
- `~/projects/micropython/ports/minimal/build/genhdr/` — generated qstr/module headers (host build).

## Bottom line

Feasibility is **confirmed but the frontend work is larger and more fundamental than first
scoped**: minic is a restricted K&R-flavored C parser, and MicroPython needs a meaningful slice
of standard-C *declaration* grammar (unnamed params, forward typedefs, flexible arrays,
enum-tag types) on top of the aggregate-initializer work. None of it is research-grade — it's
bounded grammar work in a single file (`minic.y`) — but Phase 1 should be re-estimated upward
and sequenced declaration-grammar-first.
