# Resume prompt — Stevie/QBE feature-driven port

## Context

Modern C toolchain (MiniC frontend → QBE → NASM) targeting the 1978
Intel 8086 in DOS real mode.  The premise: **Stevie 3.69b (a 1986 vi
clone) is the workload.  Its needs drive what features get added.**
Every parse error, missing feature, or codegen miscompile we hit
while compiling the *unmodified* sources is a lead — fix it in the
toolchain, not in Stevie.

Original sources are in `~/projects/stevie/`; lowercased copies are
in `stevie-orig/`.  `stevie-dos/` is the previous gutted port — leave
it as a reference for what NOT to do.  Build target is `stevie-orig/`.

## Where we are right now

`tools/build-stevie.sh --keep-going` runs minic → qbe → nasm against
`stevie-orig/` and reports per-file status.

**19 of 24 sources compile end-to-end** (alloc, cmdline, edit,
enveval, fileio, help, hexchars, linefunc, main, mark, misccmds,
normal, param, ptrfunc, regexp, regsub, regsub-via-search, sentence,
undo, version + more).  The 5 holdouts are listed below in the
suggested attack order.

## What's been added to MiniC since the last session

- Struct-array initializers at top level — both flat and brace-per-row
  forms: `struct charinfo chars[] = { 1, 0, 2, "^A", ... }` and
  `struct param params[] = { { ... }, { ... } }`.  Member types are
  walked round-robin and emitted as typed QBE data items.
- `struct TAG { ... } NAME[N];` — define a struct and a sized array
  variable in one decl (used by tagcmd.c's `tagstack`).
- Minimal `<dos.h>` and `<signal.h>` stubs under `minic/include/`.
  REGS / SREGS / FP_SEG / FP_OFF / SIGINT / signal() pre-declared.
- `cpp -D__TURBOC__` on the build pipeline; libstub adds
  `_delay`/`_disable`/`_enable` stubs so the Turbo C branch builds.
- `.fill N,1,0` → `times N db 0` translation in build-stevie.sh.
- Block-scope re-declaration of a same-typed local is now a no-op
  (previously `die("double definition")`).  Stevie's `for ... { LPTR
  *pos; ... }` blocks in distinct loop bodies now compile.
- `ext_decl` accepts `IDENT '=' expr` and `'*' IDENT '=' expr`, so
  multi-decls like `LINE *l, *nc = 0;` work — initializer is
  evaluated after the alloc.
- "Uniform-* peeling" is now applied to plain IDENT items in
  multi-decls (`char *p, c;` makes `c` a char per standard C).
  Previously it was a `char*`, which mis-sized stores several
  statements later.
- `'G'` (`*ident()`) in `emit_local_multi_decl_full` no longer
  re-pointers the base.  `LPTR *bcksearch(), *fwdsearch();` proto
  multi-decls now register both as `FUNC(LPTR*)` rather than one as
  `FUNC(LPTR**)`.
- `varclr()` repairs linear-probe chains after clearing locals.  This
  was the root cause of "undefined variable" on a file-scope decl
  whose hash slot collided with a just-cleared local from the
  previous function.
- enum constants now walk the varh chain to set the `enumconst` flag
  on the correct slot (not the raw `hash(name)` slot).  Mis-typed
  case labels were the symptom.

## What's still failing (and why)

```
dos.c    : parse error 822  — `static char far *disclaimer2[] = {strs}`
                              array-of-(far)pointer global initializer
search.c : parse error 1056 — `int (*move)(), inc(), dec();`
                              fnptr + K&R protos in the same multi-decl
ops.c    : qbe segfault (i8086 backend)
screen.c : qbe segfault (i8086 backend)
tagcmd.c : qbe segfault (i8086 backend)
```

### Priority 1 — qbe i8086 backend segfault (3 files)

`ops.c`, `screen.c`, `tagcmd.c` all parse cleanly through minic but
then crash *inside* qbe at the entry to `emit()`:

```
EXC_BAD_ACCESS at util.c:307
emit(op=63, k=0, to.val=N, arg0.type=RTmp arg0.val=0, arg1.val=0)
```

op 63 is `Oload`.  The crash is at `*--curi = (Ins){...}` — `curi`
has been corrupted (pointing at `0xfffffffffffffff0`) before this
call.  Bisect within `tagcmd.ssa` shows the crash triggers on a
specific `%t = l loadl ...` immediately after a sequence of empty
labels (`@l37` directly followed by another label).  Likely culprit:
recent i8086 backend change involving register hinting or selcmp.
The git log around `5528c8a` ("drop all 186/286/386 instructions")
and `2e23b5c` ("route 32-bit comparisons through Oc*l") is suspect.

Reproduce:
```
./qbe -t i8086 build/stevie-orig/tagcmd.ssa > /dev/null
```

Test by chopping the .ssa at function boundaries and binary-searching
for the first failing function/instruction sequence.

### Priority 2 — `int (*move)(), inc(), dec();` in search.c

Single multi-decl with mixed function pointer + K&R prototypes.  The
existing dcls grammar has `dcls type IDENT '(' ')' ',' ext_decllist`
for `T name(), other();` — but the `(*move)()` first item isn't
matched.  Add a dcls alt for `dcls type '(' '*' IDENT ')' '(' ')' ','
ext_decllist ';'` that registers move as `IDIR(FUNC(base))` and walks
ext_decllist with the same uniform-* peeling.

### Priority 3 — `static char far *disclaimer2[] = {"a","b","c"};`

Array-of-pointer global initializer.  Different shape from the
struct-array path: type is `char far *` (or just `char*`), array of
N elements each a string-literal pointer.  Plan: extend the existing
typed_decl_rest `'[' ']' '=' '{' ... '}' ';'` rule to accept simple
pointer types in addition to structs.  When parsed_type is a PTR,
emit `data $name = align 8 { l $glo<N>, l $glo<N+1>, ..., l 0 }`.

### Priority 4 — link missing symbols

Many `not defined` link errors are downstream of dos/screen/ops not
compiling.  Once those are resolved, the still-missing symbols are
mostly platform glue: `_islower`, `_sleep`, `_stat`, `_chmod`,
`_fopenb`, `_setcolor`, `_setrows`, `_fixname`, `_doshell`,
`_bios_t_ed`, `_bios_t_el`.  These need libstub stubs.

## Hard-won lessons (all in memory)

See `~/.claude/projects/-Users-pauldevine-projects-qbe/memory/feedback_minic_yacc_quirks.md`.
Highlights from this session:

- **varclr breaks linear-probe chains.**  The fix re-shifts entries
  on each empty slot.  Anywhere else that empties varh slots
  individually, do the same.
- **Never index varh by raw `hash(name)`.**  For per-entry flags,
  walk the chain — same trap as the var_isarray bug.
- **Uniform-* peeling applies to plain IDENT too.**  `T *X, Y;` makes
  Y type T, not T*.

## Useful one-liners

```sh
# Rebuild
make qbe && cd minic && make && cd ..

# Build stevie
rm -rf build/stevie-orig && tools/build-stevie.sh --keep-going

# Pass/fail summary
for src in alloc cmdline dos edit enveval fileio help hexchars linefunc \
           main mark misccmds normal ops param ptrfunc regexp regsub \
           screen search sentence tagcmd undo version; do
    err=$(cat build/stevie-orig/$src.err 2>/dev/null | head -1)
    [ -z "$err" ] && echo "PASS: $src" || echo "FAIL: $src ($err)"
done

# Bisect a qbe segfault inside one .ssa
for n in 100 200 300 400; do
  awk -v N=$n 'NR<=N {print} NR==N {print "}"; exit}' build/stevie-orig/tagcmd.ssa > /tmp/t.ssa
  ./qbe -t i8086 /tmp/t.ssa > /dev/null 2>&1
  echo "$n: $?"
done

# Inspect the pipeline for one file
cat build/stevie-orig/<base>.pp.c    # cpp output (post tr/sed)
cat build/stevie-orig/<base>.ssa     # minic's IR
cat build/stevie-orig/<base>.asm     # qbe i8086 emit
cat build/stevie-orig/<base>.nasm.asm
```

## When the build links

Once all 24 files compile and the link succeeds:

```sh
dosbox-x build/stevie-orig/stevie.com
```

If it crashes, the libstub functions in `minic/dos/libstub.asm`
(malloc, printf, fopen, ...) are the first suspect.
