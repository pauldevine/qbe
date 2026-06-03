# Claude Session Status: QBE C11/C17 8086 Compiler

**Project:** C11/C17 + GNU-extensions C Compiler for 8086 DOS using QBE Backend
**Standard:** C11 feature set (`_Static_assert`, `_Generic`, `_Alignof`/`_Alignas`, compound literals, designated initializers, anonymous struct/union) + GNU extensions (`__attribute__`, inline `__asm__`, `__far` pointers).  Equivalently **C17-level** since C17 added no new language features over C11.  **No C23 language features** (`nullptr`/`constexpr`/`typeof`/`_BitInt`/`[[…]]` attributes/binary literals are not implemented).  C only — no C++.
**Last Updated:** 2026-05-30 (MicroPython port §1q — **build bring-up step 3: FIRST REAL LINK of the curated core subset**.  New harness `tools/build-micropython.sh` (committed) compiles the curated subset — all py/*.c minus other-arch native-emitters/inline-asms/NLR-backends (kept nlr.c + nlrsetjmp.c) — plus `ports/dos8086/main.c` (`do_str("print(1+2)")` milestone entry, py-core-only) + `mphalport.c` (INT 21h console): **106/106 TUs → OMF objects, 0 fail**, then crt0_exe + libstub_exe → omf_link.  **Links cleanly except ONE symbol: `setjmp`/`longjmp`** (the NLR keystone; needs a far-form medium-model impl in `libstub_to_exe.py`'s EPILOGUE + a real nlr round-trip probe — its own session, NOT mechanically convertible from a near stub because the far frame has a 4-byte CS:IP ret and longjmp must `jmp far`).  `make check` green, 111 s/r 0 r/r (no grammar change), gate **128→130**.  Link wall hit and cleared: (1) **`static` functions were exported as duplicate public symbols** (real bug; `duplicate public '_utf8_get_char'` from the `static inline` in py/misc.h, defined-and-exported by every TU).  C `static` = internal linkage.  TWO-part fix: **minic** (`minic.y`) emits module-local QBE `function` (not `export function`) for a `static` function — new `pending_static` flag set/cleared in the `yylex()` wrapper (top-level `STATIC` token sets it; function-body-closing `}` and top-level `;` clear it), read at all 8 function-header emit sites via new `fn_export_kw()` (lexer-level → 0 grammar conflicts); **`tools/asm_to_omf.py`** stops auto-promoting `.text` (code) labels to publics (tracks `defined_text`), trusting minic's `.globl`; data labels still auto-promote (minic doesn't `export data` yet — separate non-blocking gap).  Probe `static_linkage_probe.c` (medium+large): static fns reachable via far-call (direct/nested/recursive/fn-ptr) + a non-static stays exported.  (2) **Undefined runtime symbols resolved**: `___builtin_clz` (16-bit CLZ loop — no 8086 BSR), `_memmove` (overlap-safe), `___builtin_expect` (returns arg0), `___builtin_unreachable` (ret) — NEW additive `libstub.asm` helpers (near form; libstub_to_exe shifts `[bp+N]+2`/`ret→retf`); `gc_collect` — bring-up STUB in main.c (no root scan; OK for `print(1+2)`, replace before non-trivial programs); `alloca` — eliminated via `MICROPY_NO_ALLOCA=(1)` (→`m_malloc`).  See `NEXT_SESSION.md` / `MICROPYTHON_PORT.md` / [[minic-first-link-spike]].  Prior: §1p — **build bring-up step 2: all 132 py/*.c now survive asm→obj** (each per-TU i8086 `.asm` → `asm_to_omf.py` + `nasm -f obj` → OMF object).  New harness `build/mp-spike/run-asmobj.sh` (committed).  First run 13 OK / 119 NASM_FAIL but only **3 root causes**, all fixed → **132/132 OK**.  `make check` green, 111 s/r 0 r/r, gate **125→128**.  (1) **`asm_to_omf.py` missed multi-`_` externs** — `__builtin_clz`→`___builtin_clz` (`call far ___builtin_clz`); `collect_referenced_syms` regex `\b(_[A-Za-z]…)` can't match (word boundary before the 1st `_`, which is followed by `_` not a letter), so it was never declared `extern` → nasm "symbol not defined" in 118/132 files.  Fix: `\b(_+[A-Za-z][\w]*)`.  (`___builtin_clz` still has no runtime impl — link-layer gap for later.)  (2) **C labels collided across functions** (py/runtime.c's two `too_short:`) — minic emitted flat `@user_too_short` → one asm symbol defined twice → nasm "inconsistently redefined".  C labels are function-scoped; fix in `minic/minic.y`: per-function `cur_fn_labelid` (bumped at all 4 function-body emit starts) suffixes user labels `@user_<name>_F<id>` at the Goto/Label sites.  Not exported, so only intra-module collisions needed fixing.  Probe `dup_label_probe.c` (medium+large).  (3) **16-bit Ocopy of a relocatable addr into a slot dropped `word`** (py/mpprint.c `_pad_common+17`, py/objstr.c `__str_uni_strip_whitespace`) — `=w add $sym,off` folds to a copy; rega→slot used the generic `{Ocopy,Ki,"mov %=, %0"}` → `mov [bp-N], _sym+off` (no size) → nasm OBJ "can only handle 16- or 32-bit relocations".  Fix in `i8086/emit.c`: early special-case `Ocopy Kw && to=RSlot && arg[0]=RCon` emits `mov word [bp-N], <imm/addr>` (no scratch reg; Kl Ocopy path already sized it).  Probe `caddr_slot_probe.c` (medium-only).  See `NEXT_SESSION.md` / [[minic-asmobj-spike]].  Prior: §1m — **four grammar/codegen wins; py/*.c spike effectively COMPLETE** (spike **126→131/132**, gate **115→121**, still 111 s/r 0 r/r, `make check` green; all in `minic/minic.y` + gate wiring, no i8086/QBE backend changes).  **Flipped binary, objlist, modbuiltins, objtype, parse.**  The lone remaining fail (`stream`) is **NOT a minic bug** — `SEEK_SET` is never `#define`d in the preprocessed TU (spike `cpp` include-path gap; real MicroPython gets it from `<stdio.h>`); minic correctly reports undefined-variable.  (1) **Anonymous struct/union as a type** — `struct{…}`/`union{…}` usable directly as a `type` (cast `(struct{…}*)0`, local decl `struct{…} v;`, typedef `typedef struct{…} T;`, member `struct{…} name;`).  §1k's reuse-`typedefstructstart` attempt gave **76 r/r** because `STRUCT '{'` then had TWO empty markers reachable in a struct body (`typedefstructstart` anon + `nested_s_begin`).  **Fix = UNIFY to ONE marker:** `nested_s_begin`/`nested_u_begin` (always pushes enclosing `curstruct`/-1 onto `structstk`); `type: nested_s_begin smembers '}'` pops + returns `(idx<<3)+STRUCT_T`.  Removed the dedicated *named*-nested member rules (`struct{…} name;` now flows through `smembers type IDENT ';'`, popping structstk to parent in the `type` reduce); `typedef struct{…} T;` flows through `TYPEDEF type IDENT ';'`.  `typedefstructstart`/`typedefunionstart` now **tagged-only** (`STRUCT IDENT '{'`).  (2) **Function-local + inner-block anon enum** `enum{A,B,C};` — new `dcls: dcls enumstart enums '}' ';'` AND `stmt: enumstart enums '}' ';'`, mirroring file-scope `edcl`.  (1)+(2) flip binary/objlist/modbuiltins; probe `anon_aggr_probe.c`.  (3) **Compound literal with NESTED brace through a deref** `*o=(T){{a},b,c};` (objtype's `mp_obj_super_t`) — `inititem` accepts `'{' initlist '}'` + `.field='{' … '}'`; the DUPLICATE inline member-fill loops in expr()+lval() now both call one shared recursive `emit_clit_aggr()` that descends into a sub-struct/union member on a nested-brace item (lval() path matters: `*p=(T){…}` re-materialises the literal via lval() for the struct-copy address).  Probe `nested_clit_probe.c`.  (4) **Cast to a function-pointer type** `(RET (*)(PARAMS)) expr` (parse's `(void (*)(void *))(mp_lexer_free)`) — new `pref: '(' type '(' '*' ')' '(' fptpar0 ')' ')' pref` → `IDIR(FUNC($2))`, distinguished from plain cast / compound literal by the token after `type` (`(` vs `)`).  Probe `fnptr_cast_probe.c`.  miniyacc gotcha bit twice again: NO `/* … */` standalone comment between a `;` and the next production head — put it inside the action body.  See `NEXT_SESSION.md` / [[minic-anon-aggregate-type]].  Prior: §1l — **for-init inner-block scope** (spike **125→126/132**, gate **113→115**, still 111 s/r 0 r/r, `make check` green; all in `minic/minic.y` + gate wiring, no i8086/QBE backend changes).  Closed py/compile.c's sibling for-loop `double definition` (`for (int i …)` then `for (size_t i …)` in `c_del_stmt`).  §1k gave block-scoped *statement* decls alpha-renaming, but a for-init declarator's uses (test/incr/body) are all lexed inside the single FOR production *before* it reduces, so the reduce-time rename was too late.  The three C99 for-init rules are now refactored to share a new `forinit_var: type IDENT '='` nonterminal; because they all share that prefix, the state after `type IDENT =` is single-action and miniyacc **default-reduces it without lexing lookahead**, so `forinit_var`'s action (rename via `block_scope_decl` + varadd + alloc; base type stashed in new global `forinit_basetyp`) runs before the test/incr/body are lexed, letting the existing lexer-stamp rewrite them to the renamed slot.  Probe `for_init_scope_probe.c` (medium + large; 5 cases incl. two-pointer/multi-scalar regression guards).  **miniyacc gotcha reconfirmed:** an apostrophe in an action-body comment opens a char-literal scan in `cpycode` and eats past the closing brace — keep action comments free of `'` (and `/` `[` `]`).  Remaining 6 fails: binary/objlist/modbuiltins (anon struct/union as a type — 76 r/r attempt, reverted), objtype (nested compound-literal-through-deref), parse (downstream `mp_parse` blocker), stream (SEEK_SET = harness include artifact, not a minic bug).  See `NEXT_SESSION.md` / [[minic-inner-block-scope]].  Prior: §1k — **inner-block scope + four more grammar wins** (spike **120→125/132**, gate **106→113**, still 111 s/r 0 r/r, `make check` green; all in `minic/minic.y` + gate wiring, no i8086/QBE backend changes).  Commit `f51894e`: **inner-block lexical scope via alpha-renaming** — a local name reused across distinct blocks with *different* types used to die "double definition" (minic has one flat local symtab and emits bodies lazily, resolving a USE by name at emit time).  Now `block_scope_decl()` (called from the stmt-level `type IDENT ;` / `type IDENT = expr ;` rules) renames the colliding declarator to `name$N`, pushes a rename binding, and the lexer stamps that mangled name into every subsequent *use* of the source name; the binding pops at block exit (`rename_pop_closed()` at top of `yylex`, keyed on `brace_depth`).  **Renaming fires ONLY on a different-type collision**, so same-type re-decl still folds in `varadd` (stevie for-bodies; stevie size unchanged) and already-passing files are byte-identical.  Verified miniyacc default-reduces those decl rules *before* lexing the next token (rename active when following uses are lexed).  Flips mpprint/runtime/sequence/vm; advances parse past its `pn` collision.  Probe `block_scope_probe.c` (medium+large).  See [[minic-inner-block-scope]].  Commit `6dc14b2`: **multi-scalar C99 for-init** (`for (size_t a=0,b=0,c=0; …)`, new rule distinguished from the two-pointer for by IDENT-vs-`*` after the comma; flips gc; probe `for_multiscalar_probe.c`) + **function-local fnptr typedef** (new `dcls` production; advances stream; probe `local_typedef_probe.c`).  Commit `dcd6742`: **out-of-order designated array init** (`agg_emit_array` buffers into index-addressed slots, positional→cursor, `[k]=v` sets cursor per C99, gaps coalesce so in-order arrays are byte-identical) + **trailing comma in brace-init lists** (advances objtype past two blockers; probe `array_designate_probe.c`, medium).  Remaining 7 fails: binary/objlist/modbuiltins (anonymous struct/union as a type — a `typedefstructstart smembers '}'` attempt gave **76 r/r**, reverted), compile (for-init scope — needs the rename fired before the for-body is lexed, a marker after the init-decl), objtype (nested compound-literal-through-deref), parse (downstream `mp_parse` blocker), stream (SEEK_SET undefined = spike-harness include artifact, not a minic bug).  miniyacc gotcha reconfirmed: action-body comments must avoid `/` `[` `]`.  See `NEXT_SESSION.md`.  Prior: §1j — **three grammar wins** (spike **118→120/132**, gate **100→106**, 111 s/r 0 r/r, `make check` green; all in `minic/minic.y` + a one-line `minic/yacc.c`, no i8086/QBE backend changes).  Commit `53b3942`: **tentative-definition reuse** — `static const T x;` then `static const T x = {…};` no longer dies; new varh `istentative` + `mark_tentative()`/`glo_redef_index()` let the initialized def overwrite the buffered `ini[]` slot instead of double-emitting `data $x` (flips py/objdict.c; probe `tentative_def_probe.c`).  Commit `e890cc9`: **const-expr file-scope scalar initializers** — replaced the `= NUM`/`= -NUM`/`= (NUM)`/`= (-NUM)` rules with one `= expr ;` folded via `const_eval` (byte-identical for old cases; `= STR`/`= gaggr` keep own rules); unblocks py/parse.c's ~160-level nested `?:` enum-const initializer.  That deep right-assoc ternary also overflowed the generated parser's `StackSize=500` at ~120 levels → raised to **4000** in `yacc.c` (host-side, cheap).  +2 s/r (NUM/STR-vs-expr, shift wins), 0 r/r.  Probe `const_init_probe.c`.  Commit `0e2d9db`: **comma expr in C99 for-init increment** — both C99 for rules now use `comma_exp0` (not `exp0`) for test/increment, so `for (size_t i=n; i>0; i--, ptrs++)` parses (flips py/bc.c, advances py/gc.c; probe `for_comma_inc_probe.c`).  **Inner-block scope NOT attempted** — established it needs alpha-renaming, not just a scoped symtab, because minic emits the body lazily and `varget` resolves use-type at emit time; full design (rename map + deferred rename-pop for the shadow case + parser-depth tagging) is written up in `NEXT_SESSION.md` for the 6 `double definition` files.  Incidental limit found: `{ int a=0, b=0; }` (multi-decl, init on first declarator) parse-errors inside an inner block.  Prior: §1h — **ten aggregate/array grammar wins**.  All in `minic/minic.y`, no i8086/QBE backend changes, `make check` green, spike **100→111/132**, gate **89/89**, 109 s/r 0 r/r.  Commits `8c6bbf4`/`cc46af4`/`444cd24`/`c858bd2`: const-expr local array dims (NUM→expr across all local + bare-global array rules); `sizeof(arrayvar)` via new varh `arraybytes` field (was a silent miscompile returning pointer size); adjacent string-literal concat in the lexer; `static __attribute__((..)) T f()`; `_Static_assert(expr,…)` (folded, non-foldable offsetof skipped); local aggregate init `struct P p={…}` (compound-literal desugar); file-scope designated array init `[IDX]=v` (`sai_designate`); sized file-scope array init `T t[N]={…}` (`sai_pad_to_count`); **general `sizeof(expr)`** via `typeof_expr` (emit-and-discard with `of`→/dev/null + counters restored — unblocks the `sizeof(arr)/sizeof(arr[0])` count idiom); local unsized array init + block-scoped array init (`mk_local_array_init` comma-chain) + arithmetic init items (`inititem` `pref`→`expr`).  Pinned by `mp_grammar_probe.c` (medium + large).  Found-not-fixed backend bug: two `/`|`%` divisions feeding one call corrupt the first result (i8086 div AX/DX clobber), see `[[i8086-two-div-one-call-clobber]]`.  miniyacc gotcha: action-body comments must avoid `/` `[` `]`.  Next: struct-array per-element `.field=` designators (`{ {.a=1},… }`, 5+ files) + real inner-block scope for the 4 `double definition` files (NOT the reverted varh-overwrite).  See `NEXT_SESSION.md`.  Prior: §1g — **bitfield static init + static address-of + `sizeof(T[expr])` + `alloca` builtin + `long<-char` widening**.  Five independent minic frontend wins, no i8086/QBE backend changes, `make check` green, spike **97→100/132** (objfun/objboundmeth + one obj* flipped): (a) `agg_emit_struct` packs a run of bitfield members sharing one storage unit into one scalar data item (sequential + `.field=` designated) — was `die("bitfield initializer unsupported")`; (b) undeclared-call path special-cases `alloca`/`__builtin_alloca` → `void*` (was defaulting `int`, breaking `T *p = alloca(...)`) — **parse/typecheck only, no runtime alloca yet**; (c) assignment converter extends `INT`*or*`CHR` RHS to `LNG` via `sext` (`uint32_t = uint8_t`); (d) new grammar `SIZEOF '(' type '[' expr ']' ')'` folds to `SIZE(T)*dim`, dummy=1 when `dim` non-foldable (MP_STATIC_ASSERT idiom), via new non-dying `constfoldable(Node*)`; (e) `cival_lval` resolves `&global`/`&agg.member`(offset chains)/`&arr[i]` to `$sym+off` and the `sai_*` machinery gains an `'A'` address-symbol item kind + `sai_emit_sym` — unblocks `mp_rom_map_elem_t table[]={{k,&g},…}`.  0 new conflicts (108 s/r, 0 r/r).  Pinned by `mp_aggregate_probe.c` (medium).  Gate **87/87**.  REVERTED mid-session: different-typed sibling-block local re-declaration (minic has no inner-block scope → silent miscompile of the earlier block); the 3 `double definition` files need real block scoping.  Orthogonal gap surfaced: static address-of emits canonical `data $P={l $sym,…}` — correct under near-data, but far-data (large/huge) needs `$sym`→far seg:off data relocation in `asm_to_omf`/`omf_link` (so `mp_aggregate_probe` is medium-only).  See `MICROPYTHON_PORT.md` / `NEXT_SESSION.md`.  Prior: §1f — **scalar array initializers + offsetof + member-attr + const-expr folding**.  Five independent minic frontend wins, no i8086 backend changes, `make check` green: (a) `emit_scalar_array_data` — `T NAME[] = {…}` with a scalar element type emits a real data block (was `die("array initializer requires struct or pointer type")`); (b) `sai_item: expr` + new `sai_add_expr` fold each brace item through `const_eval` (string literals still route to `sai_add_str`), so OR-of-flags / shift / arithmetic items like `((0x02)|(0x01))` work; (c) `offsetof` added to `minic/include/stddef.h` as `((size_t)&((type*)0)->member)`, unblocking the flexible-array `m_new_obj_var` idiom; (d) `__attribute__((…))` as a struct-member prefix via new `smembers: smembers attrspec` no-op alternative, + member cap 64→`NMember`(256) (`members[NMember]` + three `>= NMember` checks) — together flipped 5 of the 6-file `mp_fun_table` cluster (emitnx86/emitcommon/emitnative/nativeglue/persistentcode); (e) `const_eval` gains `'K'` (cast=identity fold), `!`/`==`/`!=`/`<`/`<=`/`&&`/`||`, and `?:` ternary — unblocking `builtinimport`.  0 new grammar conflicts (108 s/r, 0 r/r).  Pinned by `scalar_array_probe.c` (medium + large; cast/ternary/comparison folding + two offsetof offsets).  MicroPython spike **87→97/132**.  Gate **86/86**.  See `MICROPYTHON_PORT.md` / `NEXT_SESSION.md`.  Prior: §1e — **typedef-name shadowing** + **capacity bumps**.  (a) Lexer disambiguation in the `yylex()`/`yylex_inner()` wrapper: a declarator/parameter/member/member-access identifier that collides with a typedef name now lexes as `IDENT` not `TNAME` (the `py/scope.h` shape `scope_find_or_add_id(scope_t *scope, qstr qstr, …)`).  Three rules — after a complete type-specifier token → `IDENT`; in-scope local/param (`var_islocal()`) → `IDENT`, with the previous function's locals dropped on its body's closing `}` deferred one token (`pending_varclr`/`brace_depth`) so the body's last statement still resolves; after `.`/`->` → `IDENT`.  0 new conflicts; pinned by `typedef_shadow_probe.c` (medium + large).  (b) `NStruct` 64→256, `NTyp` 128→512 — preprocessed MicroPython TUs exceed both; this (not the shadow fix) flipped the 8 `scope_kind_t` files whose real `die()` was *"too many struct/union definitions"* (the `} scope_kind_t;` source line was lookahead-lag noise).  MicroPython spike **82→87/132**.  Gate **84/84**.  See `MICROPYTHON_PORT.md` / `NEXT_SESSION.md`.  Prior: §1d — **struct/union return-by-value** landed in the minic frontend (hidden caller-allocated result pointer, System-V style; no i8086/QBE backend changes).  Function header + `ret` + call site + `lval(call)` for `f().field`; new `emit_struct_copy`/`alloc_sret_slot` helpers; `cur_fn_sret` state.  Closes the `return source_line;` cluster — MicroPython spike **77→82/132**.  Pinned by `sret_probe.c` (medium + large).  Orthogonal limitation noted: `long` struct members read from an opaque call-result source lose their high word (pre-existing `[[qbe-loadc-wordsize-i8086]]`; MicroPython structs are word/`size_t`-sized so it doesn't gate).  See `MICROPYTHON_PORT.md` / `NEXT_SESSION.md`.  Gate **82/82**.  Prior: 2026-05-28 (qq — libstub 4-byte-return DX-leak audit (follow-on to oo).  Swept every libstub.asm stub returning a 4-byte type.  **`_ftell` fixed** — declared `long ftell()`, and `long` is 4 bytes (DX:AX) in EVERY model, so `mov ax,0; ret` leaked a garbage high word in ALL models (masked when DX chanced to be 0; bug-loud under huge where `_qbe_huge_add` dirties DX).  Now `xor ax,ax; xor dx,dx; ret`.  **`_signal` cleared DX defensively** — in-tree `<signal.h>` declares `int signal()` (2-byte, AX-only) so its dirty DX is currently harmless, but the standard prototype is a 4-byte fn-ptr.  **`_mktemp` deferred** — it *echoes its arg pointer* (not a NULL return), so clearing DX is wrong; proper fix is a `_far_mktemp` far_stdlib entry, and stevie ignores its return so it doesn't bite today.  All other pointer-returning stubs are in `far_stdlib` (reached only as near `_X` under near-data, FILE*/char*=2B) or already clear DX (`_malloc`).  New probe `ftell_null_probe.c` (compact/large/huge) verified bug-loud against the reverted stub.  No qbe/minic changes.  Gate **71/71**.  See [[libstub-null-ptr-dx]].  Prior: (pp — Phase B'' (track g) closed as **PROVEN UNREACHABLE**, no code shipped.  Investigated the symmetric Phase B' gap for narrow stores (`Ostorew`/`Ostoreh`/`Ostoreb` whose dest address is a spilled pointer-value slot).  Wrote a mirror-of-Phase-B' fix in i8086/emit.c (SSA `make check` green, verified it does NOT fire on alloca or call-arg slot stores), then proved no C source or authorable SSA can produce the shape: `spill.c::force_kl_slot` is **Kl-only** so near (Kw) pointers are never slot-resident — rega always materialises a narrow store's address into a register (no 8086 memory-indirect store); far-data narrow stores route through `storef*`/`storel`; isel's Oaddr rewrite turns address-taken locals into `lea reg; mov [reg]`.  **Reverted** the fix rather than ship untested-because-unreachable code; upgraded the docs from "latent, defer" to "proven unreachable, revisit only if Kw temps ever become slot-resident".  emit.c byte-identical to (oo).  Gate stays **68/68**.  Prior: (oo — closed huge boot hang.  Root cause: `_getenv` stub in libstub.asm only set AX=0, left DX undefined; under huge, Phase B's `_qbe_huge_add` dirtied DX with a real segment, so unset DX leaked as a fake non-NULL pointer; stevie chased an unterminated EXINIT "string" through DGROUP until DOSBox wedged.  Fix: `xor ax,ax; xor dx,dx; ret` in `_getenv`+`_fgets`.  Probe getenv_null_probe.c.  See [[libstub-null-ptr-dx]] + [[huge-stub-null-fix]].))))
**Status:** ~99% Complete (medium + compact + large + huge all boot cleanly; huge stevie **interactively verified 2026-05-28 (rr)** — opens a file, renders it, responds to keys, and saves edited files cleanly in DOSBox.  Earlier "huge crashes/hangs" reports were stale (predated the oo boot-hang fix + the 3577d89 render fixes).  No code change this session: a headless marker/render-capture harness confirmed the launch+render path matches the large model byte-for-byte, then the user confirmed the full interactive edit/save cycle.)

---

## 📍 Current Project Status

**For up-to-date project status, progress tracking, and implementation details, see:**

### **→ [ROADMAP.md](./ROADMAP.md) ←**

The ROADMAP.md file contains:
- **Accurate current status** of all components (updated 2026-05-23)
- **Phase completion tracking** (Phases 0, 1, 2, 4 complete; Phase 3 ~90%)
- **Component status table** with evidence and file references
- **What's actually missing** vs what's been completed
- **Original planned roadmap** for reference

---

## Quick Status Summary

**Completed ✅:**
- MiniC Compiler (C89/C99/C11)
- i8086 Backend (all integer + FPU ops + 32-bit div/rem via libstub helpers)
- 8087 FPU Support (PR #11)
- Inline Assembly (commits d44ea80, c0ddbff)
- C11 Features: _Static_assert, _Generic, _Alignof/_Alignas, compound literals, designated initializers (PR #12)
- Far Pointers (PR #13)
- 32-bit long support (DX:AX pairs + libstub div/rem)
- Function pointers, struct bitfields
- ANSI C function definitions (PR #15)
- **DOS Runtime Library** — real printf/sprintf, freelist malloc/free, file I/O (commits 775fd38, fc8d2bc, 76c213e, 19f6029)
- **DOS API** — int86/int86x/intdos/intdosx/segread + video/keyboard/mouse wrappers (commits 28941ae, d36f103)
- **OMF link toolchain** — tools/omf_link.py, asm_to_omf.py, libstub_to_exe.py
- **Stevie editor** — full .EXE port (148 KB medium-model), `:w` round-trips, `/search` works
- **Examples** — 16 legacy + 3 modern `<dos.h>` demos (mouse_demo, vga_pixels, kbtest)

**In Progress ⚠️:**
- Memory Models — runtime gate covers tiny/medium/compact/large/huge (**59/59 ok** in `tools/test-dos.sh`).  Huge Phase C landed 2026-05-24 (r): per-symbol `_HUGE_<sym>` segments let huge mode hold arrays > 64K.  Far DOS-API + puts landed 2026-05-24 (s).  Far stdio FILE* landed 2026-05-24 (t/u): full `_far_f{open,close,puts,putc,gets,read,write,flush}` in FAR_STDIO_EXE.  Session (v): `_malloc` now returns DX=SS so mediumprobe.c passes verbatim under large/huge.  Session (w): mathprobe (Kl mul/div/rem + sext + `%ld`/`%lu`/`%lx`) and dosapi_probe (segread + intdos + bdos) also pass verbatim under large/huge — no source or backend changes.  Session (x): stdio_far_probe (full 22-assertion far-stdio surface) now also passes under compact, closing the last carve-out in the far-stdio coverage matrix.  Session (gg): Phase B Var-operand carveout removed; stack ptr arith under huge now normalises like global arith.
- Tiny memory model (.COM) — pipeline gated by com_smoke/long_math/fileio/tinyprobe.  Stevie shrink to .COM size is **PARKED** ([[minic-pointer-bloat]]): stevie ships as .EXE; the medium model is the design target.
- Small .EXE — architecturally broken: libstub_to_exe.py rewrites every `ret` to `retf`, mismatches small's near-call ABI → DOSBox hangs.  Needs near+far libstub variants or model-conditional ret rewrite.  See [[per-model-gate]].
- Latent Kl-CAddr arith — **CLOSED 2026-05-25 (aa/bb/cc/dd/ee)**: 141f2e8 fixed Oloadf*/Ostoref*/Oadd/Osub; (bb) fixed Oand/Oor/Oxor; (cc) fixed cmp32_high/cmp32_low (all 10 Kl Oc*l handlers); (dd) fixed emit_push_long (Odiv/Orem Kl signed + unsigned); (ee) Omul Kl + CAddr now die()s defensively — pointer multiplication is C-illegal, unreachable from realistic frontend output but bug-loud if ever hit.  Ocopy was already CAddr-safe.  Matrix closed.
- Kl shift AX clobber ([[i8086-kl-shift-clobbers-ax]]) — Oshl/Oshr/Osar Kl handlers clobber AX/DX without telling rega; latent.  Fix mirror of [[i8086-kl-add-sub-mul-r1-alias]].
- Phase B' storel-via-Kl-slot gap ([[huge-phase-b-storel-gap]]) — **CLOSED 2026-05-25 (ff)**: `fn->arg_slot_top` threshold lets emit distinguish ABI direct-slot writes (call args / params) from spilled-Kl-ptr slots; latter now derefs through ES:BX (far-data) or [BX] (near-data).  Probe `phase_bprime_probe.c` pins all 3 far-data models.
- Phase B Var-operand carveout ([[huge-stack-arith]]) — **REMOVED 2026-05-25 (gg)**: stack pointer arith under huge now routes through `_qbe_huge_add` identically to global arith.  Pinned by `huge_stack_arith_probe.c` (compact/large/huge).  Lifted carveout surfaced two latent BX-clobber bugs in i8086/emit.c ([[i8086-kl-mul-bx-clobber]] Omul Kl r1=RCon, [[i8086-farptr-bx-clobber]] Ostoref/Oloadf{b,h,w}) — both fixed in the same session.  Gate **53/53**.
- `storefl`/`loadfl` for `long` through far pointer ([[storefl-portable]]) — **CLOSED 2026-05-25 (hh)**: new backend ops `Oloadfl`/`Ostorefl` (ops.h + all.h `isstore`/`isloadfar` + load.c `storesz`); i8086/emit.c handlers parallel to the Kl Oload/Ostorel slot-resident invariant + load_farptr_con value-park dance; minic.y `loadfar`/`storefar` + 3 inline `storef*` sites gain `'l'` branch; lexer perfect-hash K regenerated via tools/lexh.c (K=362902335).  Probe `storefl_probe.c` (5 asserts × compact/large/huge).  Closes [[storefar-lacks-storefl]].  Gate **56/56**.
- Narrow far-load AX clobber ([[loadfb-alias-portable]]) — **CLOSED 2026-05-25 (jj)**: `Oloadfb`/`Oloadfh`/`Oloadfw` in `i8086/emit.c` now wrap with `kl_save_axdx`/`kl_restore_axdx`, same bracket `Oloadfl` already used.  rega-placed live tmps in AX survive back-to-back narrow far-loads.  Probe `loadfb_alias_probe.c` (6 asserts × compact/large/huge).  Closes [[i8086-compact-loadfb-aliases-ax]].  Gate **59/59**.

---

## Key Documentation Files

- **[ROADMAP.md](./ROADMAP.md)** - Current status and implementation plan (UPDATED 2026-05-23)
- **[C11_8086_ARCHITECTURE.md](./C11_8086_ARCHITECTURE.md)** - Architectural analysis
- **[NEW_FEATURES_DOCUMENTATION.md](./NEW_FEATURES_DOCUMENTATION.md)** - MiniC feature reference
- **[I8086_TARGET.md](./I8086_TARGET.md)** - i8086 backend reference
- **[i8086/README.md](./i8086/README.md)** - i8086 backend documentation
- **[NEXT_SESSION_PROMPT.md](./NEXT_SESSION_PROMPT.md)** - Resume prompt for the next session

---

## Recent Major Accomplishments

### Stevie compact 5-fix sprint + remaining QBE rega bug (2026-05-26/27, session ll)

User reported stevie under `--model=compact` showing screen corruption with `HELLO.TXT` present in the DOSBox mount. The `(kk)` track-(l) close had verified compact with the file **missing** from the mount, which exercises the `[New File]` path and renders status-line garbage that hid the underlying breakage. With a file present, compact/large fail to render file content; huge hangs DOSBox.

Investigation surfaced **5 real QBE/minic bugs**, all fixed:

  1. **`crt0_exe.asm` argv ABI** — under medium, argv slots are 2-byte near ptrs.  Under compact/large/huge, `main(int argc, char *argv[])` expects 4-byte far ptrs.  Fix: gated on `-DFAR_DATA` define passed from `tools/build-stevie.sh` / `tools/build-example.sh` for far-data models; argv_arr emits 4-byte slots (offset + segment) and `main` is called with argv as 4-byte far ptr.  MAX_ARGV capped at 8 under FAR_DATA to keep DGROUP footprint identical (DGROUP+stack at 64KB ceiling for stevie).  Pinned by `argv_probe.c` + `argv_fopen_probe.c`.

  2. **`minic/minic.y` postinc/preinc strips FAR off PTR** — `case 'p'/'P'/'m'/'M'` did `s0.ctyp = sl.ctyp & ~FAR` unconditionally.  For `*bptr++` where `bptr` is `char *` (far), the postinc result type lost FAR; the outer `case '@'` saw ISFAR(sr.ctyp)=false and emitted `loadsb` (near) instead of `loadfb` (far) — reading the OFFSET byte of the far ptr instead of derefing.  Surfaced as stevie's `flushbuf` writing the wrong byte to BIOS AH=0E.  Fix: keep FAR on PTR/FUN value types (it's the "this value IS a 4-byte far pointer" bit), strip only on non-pointer scalars.

  3. **`minic/minic.y` struct-copy `=w add` truncates Kl ptrs** — the `*X = *Y` struct-copy block used hardcoded `=w add` + `loadw`/`storew` for word-by-word copy.  Under far-data, when either side's address was Kl (far pointer through a deref), the `=w add` truncated to 16 bits.  Fix: detect ISFAR(src_addr.ctyp) and ISFAR(s1.ctyp) **independently**; route to `=l add` + `loadfw`/`storefw` per side.  Same fix path for the byte tail of odd-sized struct copies.  Pinned by `stevie_lines_probe.c` exercising `*Topchar = *Filemem` under compact.

  4. **`minic/minic.y::branch()` emits `jnz` on Kl without truncation** — QBE's `jnz` is typed `w` and the i8086 backend emits `test reg16, reg16`, testing only 16 bits.  For a far pointer with offset=0 but valid segment (e.g. `0x1234:0x0000`), the test reads as NULL and the `if (lp && lp->linep)` test in stevie's `inc()` returned -1 on the first byte read of every line.  Fix: `branch()` (and the ternary-operator path in `case '?'`) emits `=w cnel s, 0` first when `irtyp(s.ctyp) == 'l'`, then `jnz` on the resulting Kw.

  5. **`emit.c` data emit `l` → `.quad` (8B) on i8086** — `dtoa[DL] = "\t.quad"` emits 8 bytes per `l` data item.  On i8086, Kl is 4 bytes (32-bit long / far pointer), so global struct/array initializers had each `char *` field consume 8 bytes while `sizeof()` reported 4.  Stevie's `chars[]` table accessed by `chars[c].ch_size` returned the WRONG byte (often non-zero, often "looks like a ch_size > 1") for any `c`, triggering the chars-expansion loop with bogus `n` and writing garbage to `extra[]`.  Fix: override DL directive to `.long` when `T.wordsz == 2` (the i8086 marker, no other target sets word size to 2 bytes).  Pinned by `chars_layout_probe.c` (sizeof=5, indexed reads of ch_size).

Gate stays **59/59** across all fixes; medium baseline byte-identical for fixes 1–4 (medium doesn't exercise FAR_DATA / postinc on far / struct copy on far / Kl jnz), shifts +16B per fix 5 (medium's long globals now correctly emit 4B).

**One QBE bug remains.** With all 5 fixes applied, stevie compact still mis-renders the file: `R0:L`, `R1:`, `R2:`, `R3:L` pattern in Nextscreen — one `L` per "line" then blank rows.  Instrumentation pins the failure at filetonext's `if (nextra > 0)` check: `nextra` lives in a stack slot (bp-26 in the inspected build) but rega coalesces its value through phi-slot chains on loop back-edges, and one of the chains propagates a stale value (most likely from a sibling local with overlapping live range — `n` from `(n = chars[c].ch_size) > 1` or similar).  The chars[c].ch_size load returns 1 correctly per `chars_layout_probe.c`, the inc/gchar walk works correctly per `inc_gchar_probe.c`, the struct copy works correctly per `stevie_lines_probe.c` — only stevie's actual filetonext loop fails.  See `[[qbe-rega-phi-slot-leak]]` for the repro and the QBE pipeline phases to investigate (load.c phi insertion, spill.c slot reuse, rega.c phi resolution at loop back-edges).

### Phase B Var-operand carveout removal + BX-clobber fixes (2026-05-25, session gg)
- ✅ `minic/minic.y::huge_ptr_binop` — the `if (lhs.t == Var || rhs.t == Var) return 0;` carveout (plus the surrounding paragraph of comments referencing [[huge-phase-b-storel-gap]]) is removed.  Stack pointer arith under `--model=huge` now routes through `_qbe_huge_add` / `_qbe_huge_sub` identically to global pointer arith.
- ✅ `i8086/emit.c::Omul Kl` r1=RCon branch — wrapped `mov bx, val; imul bx` with `push bx ... pop bx` to mirror the Kw `Omul` const path at line 3488.  Closes [[i8086-kl-mul-bx-clobber]] surfaced by the new probe's `*(stk+i) = i*7+3` stride mul (`mul 2, %t_i` Kl, with `i` live in BX).
- ✅ `i8086/emit.c::Ostoref{b,h,w}` and `Oloadf{b,h,w}` — wrapped each handler body with `push bx ... pop bx`, parallel to the existing `push es ... pop es` from [[i8086-farptr-es-clobber]].  Closes [[i8086-farptr-bx-clobber]].
- ✅ `minic/dos/examples/huge_stack_arith_probe.c` — 5 `ok` reductions (stk_direct / stk_opaque / stk_opaque_read / stk_eq_heap / heap_opaque) + 6 boundary-sample prints + golden.  3 new RUNTIME_TESTS entries (`...:compact`, `:large`, `:huge`).  Gate **53/53 ok**.  SSA `make check` green.
- Notes: probe element type is `int` not `long` because `minic.y::storefar()` only handles b/h/w widths — `long` through a far pointer truncates ([[storefar-lacks-storefl]]).  Orthogonal gap; doesn't affect this track.

### Phase B' — storel/loadl Kl with spilled-ptr slot (2026-05-25, session ff)
- ✅ `all.h` — added `int arg_slot_top` to `struct Fn` (call-arg slot count, set by i8086 ABI).
- ✅ `i8086/abi.c` — `i8086_abi()` sets `fn->arg_slot_top = max_arg_words` after reserving call-arg slots at the bottom of the locals frame.
- ✅ `i8086/emit.c` — `Ostorel` and `Oload Kl` handlers branch on `slot_idx >= fn->arg_slot_top`: ABI direct-slot writes (call args at low indices, selpar params at negative indices) keep the old direct-bytes semantics; spilled Kl tmp slots (high indices) now load the pointer from the slot to ES:BX (far-data) or BX (near-data) and dereference.  Closes the long-latent gap that masked any `*p = q` where `p` is a Kl ptr opaque to QBE folding.
- ✅ `minic/dos/examples/phase_bprime_probe.c` — 4 assertions of `*pp = q` where `pp` is a `long **` opaqued through identity-call (defeats constant folding).  Cross-checks via direct global load.
- ✅ `tools/test-dos.sh` — 3 new RUNTIME_TESTS entries (compact + large + huge).  Gate **50/50 ok**.  SSA `make check` green.

### Far DOS-API + puts under large/huge (2026-05-24, session s)
- ✅ `tools/libstub_to_exe.py` — new `FAR_DOSIO_EXE` constant in EPILOGUE with `_far_intdos`, `_far_int86`, `_far_segread`, `_far_puts`.  Each reads/writes its struct arg(s) via ES:BX (loaded from the 4-byte far ptr), swaps ES between in/out structs when needed, restores caller's ES on exit.  `_far_segread` stashes caller ES in SI before overwriting it.  `_far_puts` writes via `mov ds, es` temporarily then restores DS=DGROUP via `push ss; pop ds`.
- ✅ `minic/minic.y` — `far_stdlib[]` extended with `intdos`, `int86`, `segread` so calls under MCompact/MLarge/MHuge mangle to `_far_X` (puts/fputs/fputc/fgets were already listed but most had no near-helper backing).
- ✅ `minic/dos/examples/dos_far_probe.c` + golden — 8 assertions under `--model=large`: segread invariants, intdos AH=0x30/0x3300, int86 AH=0x30, `puts()` writes a string.
- ✅ `tools/test-dos.sh` — `dos_far_probe` wired under large + huge.  Gate now **25/25 ok**.
- Carry-over: `_far_fputs`/`_far_fputc`/`_far_fgets` still need FILE* sizing under far-data (4-byte representation, model-aware EPILOGUE for stdin/stdout/stderr sentinels, `_far_fopen`/`_far_fclose`).  See [[large-huge-bringup]] / NEXT_SESSION_PROMPT.md.

### Huge Phase C — per-symbol _HUGE_<sym> segments (2026-05-24, session r)
- ✅ `tools/asm_to_omf.py` — recognises `.section "_HUGE_<sym>"` markers, splits `times N db 0` bodies across paragraph-aligned chunks (HUGE_CHUNK_BYTES = 65520 = 4095 paragraphs each), emits each as `segment _HUGE_<sym>_<N> class=HUGE align=16 use16`.
- ✅ `tools/omf_link.py` — 4th layout phase after STACK places HUGE-class segments distinctly, sorted by name so consecutive `_0/_1/_2` chunks land at adjacent paragraph bases (no inter-chunk padding).  Existing fixup machinery handles MZ segment relocations unchanged.
- ✅ `minic/minic.y` — new `glosec[NGlo][NString]` + `maybe_mark_huge_global(idx, sym, total)`.  Under `MHuge` with `total > 65536`, sets `glosec[i] = "_HUGE_<sym>"`; global-emit loop prepends `section "..."` to the data decl.  Wired into file-scope-array and static-local-array rules.
- ✅ `minic/dos/examples/hugeprobe.c` + golden — `static char arr[80000]` exercises arr[0]/arr[65535]/arr[65536]/arr[79999] via both subscript and pointer arith.
- Gate now **23/23 ok**.  `large` and `huge` are finally genuinely distinct.

### Per-model runtime gate (2026-05-24, session o)
- ✅ `minic/dos/examples/tinyprobe.c` — first real tiny .COM runtime probe.  Uses inline-asm INT 21h AH=40h for output (libstub `_printf` is a stub for .COM; `_sprintf` IS implemented in libstub.asm so we sprintf-then-write).  17 verified lines: arithmetic, near-ptr pass, fn-ptr table, struct global, static local, 32-bit divmod, sprintf widths, near-pointer walk, local-array deref.
- ✅ `tools/test-dos.sh` extended with `COM_RUNTIME_TESTS` block + `run_com_runtime_probe` helper.  Gate now **21/21 ok**.
- Documented 3 architectural gaps in `[[per-model-gate]]`: small .EXE (libstub_to_exe ret→retf rewrite breaks small near-call ABI), large/huge DOS-API + stdio (libstub helpers consume near pointers), huge >64K data (no normalization + 64K/segment linker).

### Large + huge memory models bring-up (2026-05-24, session n)
- ✅ All 4 compact-mode runtime probes (cstrprobe / compactprobe_extra / fnptrprobe / farretprobe) pass verbatim under `--model=large` and `--model=huge` — same goldens.
- ✅ `tools/test-dos.sh` gate extended to 20/20 with 8 new entries (4 probes × {large, huge}).
- ✅ No qbe / minic / libstub changes were required — the existing `_far_X` helper family + `uses_far_code()` / `NEAR_CODE()` model gating already covered the surface.
- Carry-over: large/huge **DOS-API** (`intdos`/`int86`/`segread`) and **stdio** (`fputs`/`fputc`/`fgets`/`puts`) still read near pointers off the stack, so they corrupt under large/huge.  Needs `_far_intdos`/`_far_int86`/`_far_segread`/`_far_fputs`/... + adding those names to `far_stdlib[]` in `minic.y:1252`.  See [[large-huge-bringup]].

### Compact runtime test wired into test-dos.sh (2026-05-23)
- ✅ `tools/run-dos-exe.sh` — generic runner: copies .EXE to 8.3 short name, generates a DOSBox autoexec.bat-equivalent conf, captures `OUT.TXT`, strips CRLF.  Handles `$DOSBOX` env override, `dosbox` on PATH, and the macOS .app path; exit 77 = skip-not-fail when DOSBox is unavailable.
- ✅ `tools/test-dos.sh` adds a `compact runtime (cstrprobe)` step that builds via `tools/build-example.sh --model=compact` and diffs against `minic/dos/tests/cstrprobe.golden.txt`.  Now reports 5/5 ok.
- ✅ `cstrprobe.c` extended to cover all 13 `_far_X` helpers + `%p`.  Validation uses `strcmp`/`strlen`/`memcmp` returns (single int per printf) instead of multi-byte loadfb varargs, side-stepping the pre-existing `[[i8086-compact-loadfb-aliases-ax]]` register-allocation bug.

### Compact far-helpers + `_far_sprintf` (2026-05-23)
- ✅ 13 new `_far_X` helpers in `minic/dos/libstub.asm`: strlen, strcpy, strcmp, strncmp, strncpy, strchr, strrchr, strcat, strcspn, strstr, memcpy, memcmp, memset — each takes 4-byte far pointer args; pointer returns via DX:AX (seg:off).  Functions that need two distinct source segments swap DS/ES with save/restore.
- ✅ `_far_sprintf` added to `tools/libstub_to_exe.py` EPILOGUE: clones the `_sprintf` format engine but writes to a far dest (ES:DI), copies the far fmt into a DGROUP scratch up front, consumes %s/%p as 4-byte far ptrs (with DS-swap for the source-string copy), and forces %p to the 32-bit hex path.  `_far_printf` / `_far_fprintf` now delegate to it.
- ✅ Runtime-verified: `tools/build-example.sh --model=compact minic/dos/examples/cstrprobe.c` prints all 16 expected lines including `%s` over DGROUP literals + stack-local buffers, width/precision/left-align padding, and mixed `%s`/`%d` varargs.

### Compact memory model end-to-end (2026-05-23, commit 493b84b)
- ✅ `uses_far_code()` now includes Mcompact; selret emits `Jretf*`, selcall emits `Ocallfar`, crt0's `call far _main` lines up with main's `retf`
- ✅ minic mangles known stdlib calls to `far_X` (asm `_far_X`) in compact/large/huge
- ✅ `_far_printf` / `_far_fprintf` injected by `libstub_to_exe.py`; copy 4-byte far fmt into local DGROUP scratch, then call `_sprintf`
- ✅ `--model=<m>` plumbed through `asm_to_omf.py`, `libstub_to_exe.py`, `omf_link.py` (reserved for future near-code coalescing)
- ✅ Runtime-verified: `tools/build-example.sh --model=compact minic/dos/examples/cprobe.c` prints `x=42 / x=99` in DOSBox

### Tiny / cheap DOS API (2026-05-22, commits 28941ae + d36f103)
- ✅ int86 / int86x / intdos / intdosx / segread (full `union REGS` / `struct SREGS`)
- ✅ set_video_mode, putpixel (VGA mode 13h), kbhit, getche, bdos
- ✅ INT 33h mouse: mouse_reset / mouse_show / mouse_hide / mouse_get_pos
- ✅ Three new `#include <dos.h>` demos (mouse_demo, vga_pixels, kbtest)
- ✅ `tools/build-example.sh` parameterized build for any `<dos.h>` demo

### Real DOS Runtime (2026-05-20…22)
- ✅ Full sprintf/printf with width/precision/flags + `l` 32-bit modifier (commit 775fd38)
- ✅ File I/O: fopen mode-aware, fread/fwrite/fputc/fputs/fprintf, getc/fclose (commit fc8d2bc)
- ✅ Freelist malloc/free, ~39 KB heap (commits 76c213e, 19f6029)
- ✅ 32-bit div/rem via libstub helpers (commit c53ce0a)

### Stevie editor .EXE port (2026-05-15…21)
- ✅ Full medium-model .EXE build via `tools/build-stevie.sh --exe`
- ✅ File load/edit/`:w` round-trips real DOS files
- ✅ `/search` and regex work; render loop fixed
- ✅ Multiple i8086 codegen bugs flushed out and fixed along the way

### PR #11 - 8087 FPU & Long Support (2025-11-26)
- ✅ Full hardware float/double operations
- ✅ Comparisons with FPU status word
- ✅ Type conversions (int ↔ float/double)
- ✅ 32-bit long support with DX:AX pairs

### PR #12 - C11 Features (2025-11-26)
- ✅ _Static_assert, _Generic, _Alignof/_Alignas
- ✅ Compound literals, designated initializers
- ✅ Anonymous struct/union

### Inline Assembly Support (commits d44ea80, c0ddbff)
- ✅ GCC-style extended inline assembly with output/input operands and clobber lists

### PR #13 - Far Pointers (commit 6492370)
- ✅ Far pointer support for small memory model

### PR #15 - ANSI Functions (commit 03d0b81)
- ✅ ANSI C-style function definitions

---

## Next Priorities

No specific high-priority follow-on.  Kl-CAddr matrix sealed
(aa-ee); Phase B' (ff) and Phase B carveout lift (gg) make huge-mode
ptr arith first-class on global + stack operands; gate **53/53**;
stevie ships as .EXE.  Remaining tracks are independent and
lower-priority — pick whichever a real consumer needs.

1. **`storefar` lacks 32-bit width** — `minic.y::storefar()`/`loadfar()`
   handle b/h/w only; `long` through a far pointer truncates.  Not
   exercised by any in-tree consumer.  See `[[storefar-lacks-storefl]]`.

2. **Phase B'' — Ostorew/Ostoreh/Ostoreb same-shape gap** — symmetric
   to Phase B' but for narrower stores.  **PROVEN UNREACHABLE
   2026-05-28** under the current rega/isel — the gap (a narrow store
   whose dest address is a spilled pointer-value slot) cannot be
   produced: (a) `spill.c`'s `force_kl_slot` is **Kl-only**, so a near
   (Kw) pointer is never forced slot-resident — rega always materialises
   a narrow store's address into a register (8086 has no memory-indirect
   store), giving an RTmp/RMem dest, never RSlot; (b) under far-data,
   far scalars go through `storef*` and far pointers/fn-ptrs through
   `storel` (Phase B'), so plain `Ostorew` never carries a far address —
   minic even types a near-code fn-ptr as `l` under compact; (c)
   address-taken locals are rewritten by isel's Oaddr pass to
   `lea reg; mov [reg]`, never a direct RSlot dest.  A symmetric fix was
   written + verified SSA-green but **reverted** (2026-05-28) rather than
   ship untested-because-unreachable code.  Revisit only if a future
   rega/isel change ever makes Kw pointer temps slot-resident.  See
   `[[huge-phase-b-storel-gap]]`.

3. **Small .EXE architectural break** — `tools/libstub_to_exe.py`
   unconditionally rewrites `ret`→`retf`, mismatching small's near-call
   ABI → DOSBox hangs.  Needs near+far libstub variants or
   model-conditional ret rewriting.  See `[[per-model-gate]]`.

4. **211-commit upstream-qbe rebase** — pure plumbing; deferred until
   i8086 backend stabilises.

Parked: tiny .COM stevie shrink.  Stevie is a medium-model program by
design and ships as .EXE; further shrink isn't worth chasing.  See
`[[minic-pointer-bloat]]` for history.

---

## Repository Information

**Repository:** https://github.com/pauldevine/qbe
**Current Branch:** master
**Main Branch:** master

**Recent Key Commits:**
- `493b84b` - i8086+minic: compact uses far-code ABI; libstub _far_printf landed
- `1f197a0` - i8086+minic: compact far-data deref + Kl CAddr seg/off + Kl call return
- `e70a5dc` - Roadmap: reflect DOS API + runtime close-out (~90%)
- `d36f103` - libstub: INT 33h mouse wrappers + parameterized example build
- `28941ae` - libstub: complete int86x/intdosx/segread plus DOS API wrappers
- `c53ce0a` - i8086: 32-bit div/rem via libstub helpers
- `fc8d2bc` - libstub: real file I/O for .EXE so stevie's :w persists edits
- `775fd38` - libstub: full sprintf with width/precision/hex/octal/long
- `76c213e` - libstub: real freelist malloc/free + bump heap to ~34KB

---

## Project Contact

This project is developed by Paul Devine with assistance from Claude (Anthropic).

For detailed status, progress tracking, and implementation plans, always refer to **[ROADMAP.md](./ROADMAP.md)**.

---

*Last updated: 2026-05-23*
*See ROADMAP.md for current status*
