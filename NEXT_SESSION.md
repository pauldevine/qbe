# Next session — MicroPython port, Phase 1 continued (declaration grammar, layer 2)

> Master tracker: `MICROPYTHON_PORT.md`. Spike findings: `MICROPYTHON_SPIKE_REPORT.md`.
> **Tier 1 is DONE** (2026-05-29). This document is the work plan for the *next* layer the
> spike re-run revealed.

## Where we are

Tier 1 (unnamed params, forward `typedef struct X X;`, flexible array members, member cap 16→64)
plus three trivially-adjacent bonus rules (`long long`, `const`/`volatile <typedef>`, bare
forward `struct Tag;` decl) and a lexer tag-vs-typedef namespace fix all landed. Gate **72/72**
(new `declgram_probe.c` pins them). Conflicts 113→117 s/r (all benign `TLNGLNG` joining the
existing dangling-decl family), 0 r/r.

**Spike re-run: 0 → 12/132 core files fully parse.** `py/obj.h`'s declaration grammar is fully
cleared. The remaining 120 files now fail **later**, clustered in `py/misc.h` (~lines 1770–1845).

## Scope (in order; same discipline as Tier 1)

All edits in `minic/minic.y`. After each: `cd minic && make minic`, **watch yacc conflict counts
(no new reduce/reduce; justify any new shift/reduce)**, run `tools/test-dos.sh` (must stay 72/72),
re-run the spike to peel the next layer. Add/extend a probe per feature.

### Step 1 — `...` ellipsis in function prototypes (the current universal blocker)

`void vstr_printf(vstr_t *vstr, const char *fmt, ...);` and `int DEBUG_printf(const char *fmt, ...);`
fail — minic supports varargs at the **call** site but not in a prototype's parameter list.

- Confirm/lex the `...` token. Check whether minic already lexes `...` (the `'.'`-run in the
  lexer) and whether an `ELLIPSIS` token exists; add one if not (three `.` chars).
- Add ellipsis alternatives to the **regular prototype** param list `par1`/`par0`
  (`minic.y` ~4705) and to the **fn-ptr** param list `fptpar1` (~4724): a trailing `, ...` after
  named/abstract params, and bare `...`. The proto registration paths ignore extra param shape,
  so this is grammar-only (no IR effect for the `...` itself).
- **Probe:** `int f(const char *, ...); void g(int a, ...); int main(){return 0;}` → OK; then a
  variadic *definition* that actually consumes args via the existing varargs mechanism if minic
  has one for definitions.

### Step 2 — `enum Tag` as a type-specifier (Tier 2 #4)

Spike report flags this as the next pervasive layer once protos parse. `enum E e;` / `enum E f(...)`
fail today (anonymous `enum {…}` and `typedef enum {…} name` already work). Add `type: ENUM IDENT`
(treat as `INT`, like the existing enum handling). Re-run spike to confirm it's the right next layer.

### Step 3 — stdint header gap (quick, supporting)

Shipped `minic/include/stdint.h` lacks `intptr_t`/`uintptr_t` (the spike used a throwaway
`build/mp-spike/stubinc/stdint.h` shadow to get past `py/mpconfig.h`). Add them — but the width is
**model-dependent** (near data → `int`/`unsigned int`; far data → `long`/`unsigned long`), so gate
on `#ifdef FAR_DATA` (the define the far-data build already passes). Also note `int32_t` is
currently `typedef int int32_t` which is **16-bit** on i8086 — should be `long`; fix while there
(check nothing in-tree depends on the wrong width first).

## Pause point (unchanged)

**Do NOT start the aggregate/designated-initializer emitter (approved plan §1b, the `dataitem()`
design) without re-running the spike first** and confirming it's genuinely the next layer. That is
the substantial piece; everything above is small localized grammar. If the spike after Steps 1–2
shows initializers as the dominant remaining blocker (very likely), **pause and re-scope §1b from
the fresh tally** before writing the emitter.

## Guardrails

- Rebuild with `cd minic && make minic`; the local `yacc` prints conflict counts — no new
  reduce/reduce, and justify any new shift/reduce against the existing same-state token family.
- Flow is system `cpp -P -nostdinc` + `minic/include` → `minic`. Spike harness:
  `build/mp-spike/run-spike.sh ~/projects/micropython/py/*.c` then read `summary.tsv` /
  `err/<file>.minic.err`. Note minic's reported error line often points just *past* the failing
  construct — read a few lines back, and prefer isolating a minimal repro through `minic -m medium`.
- Keep edits localized to declarator / type-specifier / struct-member / param rules.
