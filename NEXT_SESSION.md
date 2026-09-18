# Next session — C-KERMIT LINK CAMPAIGN (set 2026-09-18 after §9f; supersedes the §9e "consumer-driven" menu below)

**Goal:** build C-Kermit (`~/projects/ckermit`, the Victor 9000 port, 24
modules) with THIS toolchain, link it, and run it — DOSBox first, then MAME
victor9k.  §9f proved all 24 modules compile end-to-end (large, `-G`), but
only through the text rewrites in `build/ckermit-triage/fixups.pl`, against
Open Watcom's headers, with no libc wired up.  The items below are the work
between "compiles" and "runs", in order.  Take them one (or a few) per
session; each compiler fix follows the house rules (bug-loud probe in
test-dos, `make check`, MP compact byte-compare, conflicts stay 117, emit
audit only if `i8086/emit.c` changes).

**Ground rules for this campaign**
- The Watcom build is authoritative in the ckermit repo, and its CLAUDE.md
  says "there is one build ... do not reintroduce" a second one.  So our
  build lives in THIS repo (a `tools/build-ckermit.sh` reading sources from
  `~/projects/ckermit`), and the ckermit repo is not modified unless the user
  asks.
- Measure progress by deleting rules from `fixups.pl`: when a gap is fixed
  in minic, remove its rewrite and re-run `sweep.sh` (must stay 24/24).
- `ia16-ubuntu-2` container (Watcom `wcc -p`) needs `container system start`
  then `container start ia16-ubuntu-2` first.

### Item 0 — preserve the triage tooling — DONE (2026-09-18)
- Tracked in `tools/ckermit/` (README.md there).  Paths via `common.sh`
  (`CK`, `CKW`, `MODEL`, `MINICFLAGS`, `OUTSUFFIX`); outputs stay in
  `build/ckermit-triage/`.  New `pp.sh` regenerates the Watcom `.i` inputs in
  the container (byte-identical to §9f's).  `MINICFLAGS=-G sweep.sh large`
  reproduces §9f's out-large-G exactly (24/24; `.omf.asm` identical modulo
  QBE's heap-address local labels `_0x…`, which vary under ASLR).

### Item 1 — minic gaps whose rewrites CHANGE MEANING — DONE (2026-09-18)
fixups.pl rules G4, G4b-e, G11, G11b, G13, G16 DELETED; sweep still 24/24
(large, -G).  Gated: `ptrdecl_probe` + `stmtdecl_probe` × 5 models
(test-dos 432 → 442); conflicts 117; MP TU SSA byte-identical (all 118
`build/mp-link/*.pp.c` diffed old-vs-new minic) → MP body 689,760 unchanged;
frontend-only → no emit audit.
- **1a** `ext_decl: '*' ext_decl` — any number of `*` on any declarator form
  (`**p`, `*a[]`, `*a[N]`, `**f()`); a Node `xptr` field counts stars beyond
  the one the P/G/H tag encodes.  The first declarator's `*`s are absorbed
  into `type` by the grammar, so later items must start from the
  declaration's SPECIFIER: `starchain_note()` records each `type '*'`
  reduction (TNAME pushes a 0-star entry), `decl_base0(type)` looks up the
  latest chain ending at the rule's type (a ring, since casts/params inside
  the declaration reduce more types), `ed_elem()` applies an item's own
  stars.  All six consumers now share that rule: `extern_decl_list`
  (file/dcls/stmt extern), `emit_global_rest_item` (file-scope `,` /
  `[N],` / fn-first lists), `emit_local_decl_item` (block multi-decl +
  `_full`, which gained a `first_in_list` flag), and the static rest item.
  Known limit: a pointer-TYPEDEF base plus a same-type `*` cast/param inside
  one declaration can pick the cast's chain (= the old always-peel
  behavior, never worse).
- Found + fixed on the way (all silent miscompiles): file-scope rest items
  ignored their own `*` (`int a, *b;` → b int; `char *p, c;` → c pointer)
  and dropped `= init` (`int a, b = 5;` → b 0), and `*f()`/`*f(int)` items
  became variables; block `char *a[3], b;` made a char[3] + b char*, and
  multi-decl local arrays never recorded their sizeof; `extern void *p;`
  died; `sizeof(charvar)` was 2 (typeof_expr sees the promoted load).
- **1b/1c** statement scope (a nested block's leading decls parse as stmts):
  `EXTERN type ext_decllist ';'` replaces the single-name rule (arrays,
  multi-name, pointer arrays), plus `EXTERN type IDENT '(' par1 ')' ';'`,
  `type IDENT '(' ')' ';'`, `type IDENT '(' par1 ')' ';'`; and function-top
  `dcls EXTERN type IDENT '(' par1 ')' ';'`.  (A dedicated stmt
  `EXTERN type IDENT '(' ')' ';'` added a conflict against ext_decl's
  `IDENT '(' ')'` — dropped, the list path handles it.)
- New gaps noticed (item 2 material): `sizeof x` without parens;
  `init_decllist` items take no `*` (G15: `char *p = 0, *q = f;`,
  `register CHAR c = 0, *p;`); ext_decl dims must be a bare NUM
  (`char f1[64+1], f2[64+1];` in ckcpro/ckcfns).

### Item 2 — remaining syntax-only minic gaps — DONE (2026-09-18)
Every G rule is gone from fixups.pl (only W1–W5 remain) and `splitdecl.pl`
is deleted; sweep still 24/24 (large, -G).  Gated: `initdecl_probe`,
`structdecl_probe`, `paramdecl_probe`, `twod_probe` × 5 models (test-dos
442 → 462); conflicts **117 → 115** (the removed `EXTERN STRUCT IDENT ...`
rules were the source of 2 s/r); MP TU SSA byte-identical (118/118, old-vs-new
minic) → body 689,760 unchanged; all other example probes SSA-identical;
frontend-only → no emit audit.
- **G8** file scope `T a = e, b, *c = &a;` (+ `= {…}` first item):
  `'=' expr|gaggr ',' ext_decllist ';'`; cival_eval already handles string
  literals, so no separate STR rule (it would add a conflict).
- **G15** `T a = e, <any declarator>...` at block/function-top scope: the two
  rules take `ext_decllist` (was `init_decllist`), items via
  `local_init_rest_item` (plain items keep the old volatile-aware alloc path;
  decorated ones go through emit_local_decl_item, starting from decl_base0).
  Also fixed: `char *p = 0, c;` made c a `char *`.  `ext_decl` dims are now
  constant expressions (`name[128+1]`).
- **Tag definition as a type**: one `tagged_s_begin`/`tagged_u_begin` marker
  (pushes curstruct like nested_s_begin) serves `struct T {..};`, `struct T
  {..} v, *p, a[3];`, `typedef struct T {..} T_t;`, members, locals.  The
  old `typedefstruct*` rules and the sdcl `structstart ... IDENT '[' NUM ']'`
  rules were subsumed (identical emitters).
- **Struct member lists**: later declarators start from the specifier and
  honor their own `*` (was: `int a, *b;` → b int, `char *c, d;` → d char*).
  **G19** unnamed bitfields `T :N;` (pads, no member — invisible to
  positional init) and `T :0;` (closes the unit), also in lists.
- **G3** removed the redundant `EXTERN STRUCT IDENT ...` rules (they stole the
  `*` so `extern struct T *f(int);` dead-ended).  **G7** `long double`
  (== double).  **G23** east const for void/char/int.
- **G5/G6/G20/G21** params: `int (*)(char)` (also inside fn-ptr param lists),
  `char *[]`, `struct K[]`, `T [N]`, `T (name)`, `int (__far *fn)(..)`;
  `void (__far *m)(void);` members, file-scope vars and casts (`__far`
  dropped: code pointers are already far in medium/large/huge).
- **G1** `typedef void fn_t(int, int);`.  **G2** function returning a fn
  pointer, `void (*signal(int, void (*)(int)))(int);`, plain/static/extern
  and `(__far *f(..))()` — prototype only (tagged 'q' in gfnptr_decl; the
  extern fn-ptr rule now takes a gfnptr_decllist).  No DEFINITION form yet.
- **G24** aggregate `?:` as an assignment/return source: `lval('?')` =
  `*(c ? &a : &b)`; `expr('?')` of struct arms (`agg_arm`: a struct var or
  `*p`, a static no-side-effect check — NOT typeof_expr, which registers
  string globals and would change MP output) yields the address so the
  struct-assign path reuses it (condition evaluated once).
- **G12** true 2-D arrays at file scope: `T a[N][M];`, `= {{..},{..}}`,
  `T a[][M] = {..}` via the aoa machinery; `aoa_flatten` turns nested /
  elided / designated rows into a designated flat list (agg_emit_array
  zero-fills and coalesces).  **Found + fixed a silent miscompile**: a
  file-scope array of array-typedef rows with a brace initializer
  (`row_t t[2] = {{1,2,3},{4}}`) read each braced row as ONE element — i.e.
  the §9f G12 rewrite made C-Kermit's `txtp`/`binp` [11][64] pattern tables
  wrong.  The declaration's array-typedef state is now captured at
  type_and_ident (`parsed_arraydim`/`parsed_arrayelem`) because the lexer
  clears g_td_arraydim on any type keyword, e.g. a `(void *)0` in the
  initializer.  `sizeof(a[i])` of such an array now gives the row size.
- Not done (no C-Kermit need): `sizeof x` without parens; block-scope and
  static-local 2-D declarators (static-local array-typedef-row brace init
  likely has the same flatten bug — `emit_static_array` does not use
  aoa_flatten yet); a definition form for functions returning fn pointers;
  string rows in a 2-D char array (`char a[2][4] = {"ab","cd"}` dies).

### Item 3 — switch from Watcom's headers to ours — DONE (2026-09-18)
`tools/build-ckermit.sh` builds all **24/24** modules (large, `-G`) against
our own header set, `minic/dos/ckermit/include/` (21 headers, self-contained;
README there), with no Watcom headers.  No compiler/qbe/emit change → no
make check / MP byte-compare / emit audit needed; test-dos untouched.
- Pipeline: wart (host, from ckwart.c; output byte-identical to the
  committed ckcpro.c) → `clang -E -undef -nostdinc` with victorow → victor →
  our headers → CK, `-include ckvictor.h` → `fixups.pl` → minic -G → qbe →
  asm_to_omf → nasm → `build/ckermit/<model>/*.obj`.  Flags live in
  `tools/ckermit/cppflags.sh` (shared).  `-undef` matters: clang's own
  predefines (`__APPLE__`, `__GNUC__`) would steer C-Kermit's platform
  ifdefs; wcc's (`M_I86`, `__DOS__`, `MSDOS`, `__LARGE__`, `_M_IX86=0`...)
  are defined instead, minus `__WATCOMC__`.
- **Equivalence with the Watcom build is checked, not assumed**:
  `tools/ckermit/hdrcheck.sh` (needs the container) compares (1) definedness
  of all ~3,300 identifiers C-Kermit tests in `#if`s, after all system
  headers, Watcom vs ours, plus the values of shared ones used in `#if`
  expressions; (2) per module, the C-Kermit statements both ways as
  multisets — value-only differences pair up, a code path in one build only
  unbalances the counts.  PASS: every module balanced.  Bug-loud: putting
  `F_SETFL`/`O_NDELAY` back flags both checks (ckutio 36/48, ckufio 40/45).
- What that check caught in the first draft of the headers (all fixed):
  `fcntl()`/`F_*`/`O_NDELAY`/`O_NONBLOCK` and `EWOULDBLOCK` — Watcom's DOS
  headers have none, and they switched on ckutio/ckufio non-blocking paths
  (`#ifdef F_SETFL`, `DONDELAY`) absent from the reference build;
  `PATH_MAX` is 143 in Watcom (144 changed buffer sizes).  Watcom-only macros
  left are all foreign-platform/dead/cosmetic (listed in hdrcheck's ACCEPTED).
- `fixups.pl` is now only source-level Watcom-isms: W3 `__interrupt`, W5 XI
  records, W6 (new) object `__near` — items 5a/5c/5b.  W1/W2/W4 matched only
  Watcom-header text and moved into `sweep.sh` (still 24/24 on the Watcom
  path).
- Sizes: code 421,495 B vs 426,922 with Watcom headers (ctype calls instead
  of inline table macros), data 83,095 vs 83,127.
- External symbols: 97 (`tools/ckermit/undefined-ours.txt`).  Watcom
  internals gone (`___iob`, `__IsTable`, `___get_errno_ptr`, `__setjmp`);
  new are ordinary libc (`feof`/`ferror`/`fileno`/`clearerr`/`getc`/`putc`/
  `getchar`/`putchar`/`isalnum`/..., `__errno`, `stdin`/`stdout`/`stderr`,
  `_far_setjmp`/`_far_longjmp`, `_far_intdos`, `_intdosx`).
- **Item 4 ABI hazards** (README table): `struct stat` and `off_t` (long)
  differ from shiminc — dos_vfs's `vfs_stat` writes nothing and `vfs_lseek`
  returns 16 bits, so stat/fstat/lseek need implementations compiled against
  these headers; `signal` must return a function pointer (dos_libc's is an
  `int` stub); `struct dirent`/`DIR` is Watcom's layout, not
  libgloss/dirent.c's; `_fmode` and all of time.h have no provider.  `FILE`,
  `jmp_buf`, `errno` match the runtime.
- Noticed, not fixed: the i8086 emitter prints a `w` compare against -1 as
  `cmp bx, 4294967295` (nasm warns "word exceeds bounds", truncates to
  0xFFFF — correct, 422 warnings in C-Kermit, same under Watcom headers).
  Cosmetic; a fix is an emit.c change (audit).

### Item 4 — libc fill (84 undefined → 0)
From `build/ckermit-triage/undefined.txt` (84 symbols, Watcom headers).
Already provided by the libstub-free runtime: printf family + far bridges,
str/mem, malloc/free, fopen/fread/fputs/fseek/fgetc, open/read/write/close,
unlink/rename/stat, isatty, setjmp, _qbe_div32*, tolower/toupper.
- **4a. Replace stubs with real code** — `dos_libc.c` copies libstub's
  STUBS: `atoi` returns 0, `getenv` returns NULL, `signal` NULL.  C-Kermit
  parses numbers and reads `TERM`.  Real versions change the
  `dos_libc_probe` libstub-equivalence story — decide whether real
  impls replace the stubs or live in a C-Kermit-specific TU.
- **4b. Missing:** directory (`opendir`/`readdir`/`closedir` over INT 21h
  4Eh/4Fh, `getcwd`, `chdir`, `mkdir`, `rmdir`); time (`time`, `localtime`,
  `gmtime`, `ctime`, `asctime`, `tzset`, `utime` over INT 21h 2Ah/2Ch/57h);
  misc (`umask`, `dup2`, `perror`, `putenv`, `setbuf`, `fdopen`, `memchr`,
  `atexit`, `_exit`, `atol`); `execl`/`execvp` may stub.
- Each new function gets a probe (DOS-hosted, golden) before C-Kermit relies
  on it.  Far-data: remember the far_stdlib bridge names (`_far_X`) for any
  function minic mangles.

### Item 5 — the Victor-specific, Watcom-specific parts
- **5a. `ckvisr.asm`** (the µPD7201 receive ISR, §16t in ckermit's
  PORTING.md) is Watcom `wasm` syntax, declares
  `DGROUP GROUP CONST,CONST2,_DATA,_BSS`, reaches C variables via DS.  Port to
  nasm with our segment/group names (`_DATA`/`_BSS` only).  Alternatively
  build with `XFLAGS=-dV9K_CISR` semantics (the C handler) first, using
  minic's `__attribute__((interrupt))` ABI — simpler, slower (§16t measured
  38400 baud needs the asm).
- **5b. Object-level `__near` in minic.**  `ckvictor.c:2697`
  `volatile unsigned char __near v9k_rxbuf[4096]` must stay in DGROUP for the
  ISR, but under `-G` a 4096-byte array goes to `_FARDATA`.  minic ignores
  `__near` on objects today.  Add it: forces DGROUP placement (and may allow
  near access).  Gate with a probe that an asm TU reads through DS.
- **5c. `_fmode` binary initializer.**  C-Kermit sets `_fmode = O_BINARY`
  before main() via Watcom's XI init table (`__based(__segname("XI"))`,
  rewrite W5 strips it).  Without it binary transfers corrupt (ckermit
  PORTING.md §16h).  Needs a crt0 init hook or an explicit call, and our
  runtime's equivalent of text/binary mode (dos_vfs is raw — check whether
  `_fmode` even matters on our path).
- **5d. Watcom runtime APIs** used by ckvictor.c: `_dos_getvect`/
  `_dos_setvect` (a `__MINIC__` fill exists from §8z in newlibc
  `v9k_hardware.h`), `_dos_gettime`, `_dos_getfileattr`, `_LpCmdLine`,
  `_fmode`, the `seg :> off` base operator (rewrite W4; `MK_FP`).

### Item 6 — link and run
- Link with omf_link (large, `-G`, libstub-free runtime).  Budget: with -G,
  code ≈ 427 KB + far data ≈ 64 KB + DGROUP ≈ 14 KB + libc.  Victor MS-DOS
  3.1 gives 824,784 B at 896 KB — fits; DOSBox's ~600 KB free may be tight.
- DOSBox: the serial path won't work, but the `C-Kermit>` prompt, SHOW,
  local file commands and the command parser can be exercised (script via a
  take file).  Compare behavior against the Watcom `ckermitw.exe`.
- MAME victor9k (`tools/run-victor-sasi.sh`): a real file transfer round
  trip, md5-compared, as the ckermit repo does.
- Code-size follow-up (separate backend work, only if needed to fit or for
  speed): the Kl slot-resident design (53k push/pop in C-Kermit), boolean
  materialization before branches, linear `switch` chains — §9f measured
  2.7× Watcom `-os` with -G.

### Current state (end of item 3)
- Items 0–3 done; test-dos 462/462; MP compact body 689,760; grammar
  conflicts 115 s/r, 0 r/r.
- `tools/build-ckermit.sh` = the 24/24 compile (large, -G, our headers) into
  `build/ckermit/large/`; `tools/ckermit/undefined-ours.txt` (97 symbols) and
  `minic/dos/ckermit/include/README.md` (ABI table) are the inputs to item 4.
- **Next: item 4** (libc fill).  Build its TUs against
  `minic/dos/ckermit/include`, not shiminc; re-run `hdrcheck.sh` whenever a
  header there changes.

---

# Next session (the §9d handoff completed the file-scope function-pointer grammar family and left only consumer-driven options; the user (AskUserQuestion) chose **HUNT A COMPILER TRACK**.  §9e [2026-06-19, this session] **CLOSED the MULTI-DECLARATOR bitfield list — `unsigned a:3, b:5, c:4;` (the common hardware-register form) plus the mixed `unsigned a:3, b;` / `unsigned a, b:5;` and arbitrary interleavings — all of which were hard parse errors; AND fixed a latent pre-existing bug in the same `sm_more_names` code path that silently dropped middle members from any struct declaration with 4+ comma-separated members.  The fix is a frontend `minic.y` change → no emit audit; test-dos 417 → 422; conflicts UNCHANGED at the §9a/§9b/§9d baseline 117 s/r, 0 r/r; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  EMPIRICAL SCOPING FIRST (house rule): batch-probed ~30 C11/GNU constructs through `minic -m small < x.c` to find a REAL gap rather than assume one.  Genuine gaps surfaced — multi-declarator bitfields; block-scope/static-local/`__far`/array function-pointer VARIABLES (`int (*p)(int)` block-scope works, but `static int (*p)(int);`, `int __far (*p)(int);`, `int (*tab[3])(int);` all parse-error); pointer-to-array (`int (*p)[3];`); array-of-fn-ptr file-scope + typedef; unnamed fn-ptr parameter (`int f(int (*)(int))`); compound-literal-array (`(int[]){1,2,3}`); nested designated initializer (`{[0].x=1}`).  Picked **multi-declarator bitfields**: bounded, codegen already works (a single bitfield packs correctly with `and`/`shl`/`or` mask-shift), directly relevant to Victor 9000 hardware-register structs, and no consumer needed.  **THE GAP:** `smembers` had only a single-bitfield production (`type IDENT ':' expr ';'`) and a plain multi-NAME production (`type IDENT ',' sm_more_names ';'`, for `struct L *prev, *next;`) — there was no C11 struct-declarator-list (6.7.2.1) in which each comma-separated item can independently carry a `: width`.  **THE FIX (frontend `minic.y`, additive):** (1) a `sm_more_names` list node now carries an optional bitfield width-expr in `n->l` (NIL = a plain member); added a start item `IDENT ':' expr` and a chain item `sm_more_names ',' IDENT ':' expr`.  (2) a NEW production `smembers type IDENT ':' expr ',' sm_more_names ';'` handles a list whose FIRST declarator is a bitfield.  (3) the existing plain multi-name action was generalized to emit a bitfield (`structaddbitfield`) when a node's `n->l` is set, else `structaddmember` — so the plain-only path is byte-identical (every node keeps `n->l == NIL`, exactly the prior behavior) and an all-bitfield list emits SSA byte-identical to the equivalent separate-declaration form (verified by `diff`).  Lookahead distinguishes the single-bitfield rule (`;` after `expr`) from the new multi (`,` after `expr`), so it is conflict-free.  **THE LATENT BUG (found + fixed in the SAME code):** `sm_more_names` chained new items with `$1->r = n` — writing the HEAD node's link, not the tail's — so a list of 3+ TRAILING declarators (4+ comma-separated members total in one declaration) silently DROPPED its middle members: `struct L { int a, b, c, d; }` registered only 3 members (`alloc 6`, should be 8).  Latent because real `T a, b;` / `struct L *prev, *next;` lists rarely exceed ONE trailing name (the overwrite only bites the 2nd-and-later append); the new multi-declarator bitfield probe's 3-item tail (`n, o:4, r`) was the first construct to hit it.  Fixed all three `sm_more_names ','` chain productions to APPEND at the tail (`Node *tl = $1; while (tl->r) tl = tl->r; tl->r = n;`).  The MP body byte-compare being IDENTICAL proves MP contains no 4+-comma struct member lists (so the correctness fix changes no existing gated output).  **GATED bug-loud** by `minic/dos/examples/bitfield_multidecl_probe.c` (small+medium+compact+large+huge): on the pre-fix compiler the first multi-declarator bitfield is `error:46: parse error` so the program does not build (confirmed by `git stash`-ing the §9e `minic.y` change and recompiling); the probe covers form A all-bitfield / B bitfield+plain / C plain+bitfield / D mixed (bitfield,plain,bitfield,plain), field-independence (assigning `a = 13` to a 3-bit field wraps to `5` AND leaves `b`/`c` untouched — proving the per-field read-modify-write masking), and the four struct `sizeof`s (A=2 B=4 C=4 D=8).  All values are field contents and sizes (not addresses), so the golden is model-independent and byte-identical across all five models (golden ends `bitfield_multidecl_probe done`, no §8y trailing-blank trap).  **VALIDATION:** `make check` green; full gate **422/422 ok** (417 → 422, the 5 new probe entries, no regressions); MP compact body **689,760 BYTE-IDENTICAL** (the chaining fix + bitfield productions never alter MP's codegen → no Victor run); frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master (`minic.y` = the `smembers` bitfield-list productions + the generalized actions + the `sm_more_names` tail-append chaining fix; new `minic/dos/examples/bitfield_multidecl_probe.c` + `minic/dos/tests/bitfield_multidecl_probe.golden.txt`; 5 `tools/test-dos.sh` entries — NO compiler-backend/qbe/emit/build-script change, NO newlibc-tree change).  **NOTE on conflict figures:** they come from the SYSTEM yacc (`/usr/bin/yacc` = bison): `yacc -v minic/minic.y`, then sum the per-state `N shift/reduce` lines in `y.output` → 117 s/r, 0 r/r.  The vendored `minic/yacc` can no longer parse the current `minic.y` (the §9c finding, [[minic miniyacc and lexer quirks]]).  Rebuild minic staleness-safe: `rm -f minic/minic && touch minic/minic.y && make minic/minic`.  **⇒ Next session — still consumer-driven (pick with the user):** (1) hunt another bounded no-consumer compiler track — the scoping sweep above is a ready menu (block-scope/static-local/`__far`/array fn-ptr VARIABLES; ptr-to-array; unnamed fn-ptr param; compound-literal-array; nested designated init); (2) merge newlibc **PR #24** is ALREADY DONE (`victor9K_newlibc` `46eb8a7`, confirmed §9c/§9e); (3) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (4) Victor-native INT 1Ah/16h-free timer/keyboard DOS probes if wanted (the §8l/§8n bare-metal pattern already covers that ground).  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §9f session notes (2026-09-18) — C-Kermit triage + minic -G near globals

### C-Kermit triage (~/projects/ckermit, Victor port, 24 modules)
- Method: Open Watcom `wcc -p` (real victorow.mak flags, in the
  `ia16-ubuntu-2` container; `container system start` first) → strip
  Watcom-isms → minic -m large → qbe → asm_to_omf → nasm.  Scripts + full
  gap list: `build/ckermit-triage/` (untracked; FINDINGS.md, sweep.sh,
  fixups.pl, locate.py, omfsize.py).  All 24 modules compile end-to-end
  with ~20 frontend gaps worked around by text rewrites (fixups.pl, G1..G24).
- Fixed (647d4f9): comma-expression statements + comma in while/if
  conditions, block-scope extern of an initialized global, local array
  shadowing a file-scope name, NGlo 512→8192, die() statement line.
- Grammar conflict baseline is **117** (HEAD already had 117 with the system
  yacc; CLAUDE.md said 115).

### minic -G (near globals) — the far-data code-size / DGROUP fix
- Before: `FARSTORAGE(s)` made EVERY global/extern access far under
  compact/large/huge (`loadfw $g` → ES:BX + push/pop brackets, ~14 B).
  C-Kermit large: `wart()` 68,873 B (> 64 KB segment), DGROUP 78 KB.
- `-G`: a named global/extern whose declared type is < NEAR_GLOBAL_MAX (128)
  bytes, not an array, not in a `_HUGE_` section, is DGROUP-resident and
  accessed with plain load/store (DS-relative).  Placement in main()'s data
  loop mirrors the same TYPE rule (so extern access in one TU agrees with the
  definer's placement); string literals + big objects + non-near arrays get
  `section "_FARDATA"` → asm_to_omf emits `<BASE>_FAR` class FAR_DATA
  (omf_link already places FAR_DATA outside DGROUP).  `glonear[]` pins any
  slot this TU accessed near.  Pointer TYPES carry FAR under far-data — that
  is the value's far-ness, NOT storage; do not exclude ISFAR.
- The backend needed NO change: plain `loadw $g`/`storew`/`loadl` already emit
  `mov [_g], r` in large, and §8a's group-framing makes `&g` (seg:off) agree
  with DS:[_g].  DS==DGROUP holds everywhere (crt0, int86x restore, ISR
  prologue).  The planned isel CAddr loadf→load rewrite was measured and
  SKIPPED: only 100 const-address far accesses remain in all of C-Kermit,
  nearly all on big (correctly far) arrays.
- Compatibility: a TU built without -G keeps all data in DGROUP and accesses
  it far → link-compatible both ways (support/runtime TUs stay without -G).
  NOT compatible with asm_to_omf --far-static-data (MP) — build-example.sh
  rejects the combination.
- Gate: `near_globals_probe` (+ `near_globals_probe2.c`, 2 TUs) ×5 models via
  `build-example.sh --near-globals`; bug-loud (sabotaged placement → 5 FAILs).
  test-dos 432/432, make check green, MP compact image BYTE-IDENTICAL (cmp),
  no emit.c change → no emit audit.
- C-Kermit large with -G: DGROUP 78.0 KB → 14.0 KB, wart() 37,611 B, total
  code 488 KB → 427 KB (Watcom -os: 160 KB).  Remaining 2.7× is the Kl
  slot-resident design (32-bit values in stack slots with push/pop dx/ax
  brackets: 53k push/pop in C-Kermit) + boolean materialization
  (mov ax,0/1; test; jnz) + linear switch chains — separate backend work.

## §9e session notes (2026-06-19)

### The pick
- §9d handoff: file-scope fn-ptr grammar family COMPLETE, no open bug / no
  carried track.  PR #24 already merged (`46eb8a7`, verified this session).
  User (AskUserQuestion) chose **Hunt a compiler track**.

### Empirical scoping (do this before assuming the gap)
- Batch-probed ~30 C11/GNU constructs through `minic -m small < x.c`.  Real
  parse gaps: multi-declarator bitfields; block-scope/static-local/`__far`/
  array fn-ptr VARIABLES; ptr-to-array; array-of-fn-ptr file-scope+typedef;
  unnamed fn-ptr param; compound-literal-array; nested designated init.
- Picked **multi-declarator bitfields** — bounded, codegen already works
  (single bitfield packs with `and`/`shl`/`or`), hardware-register relevant,
  no consumer needed.

### The gap + fix (frontend minic.y, additive)
- `smembers` had only single-bitfield (`type IDENT ':' expr ';'`) and plain
  multi-name (`type IDENT ',' sm_more_names ';'`) — no struct-declarator-list
  where each item can carry `: width`.
- `sm_more_names` node now carries an optional width-expr in `n->l` (NIL =
  plain); added start item `IDENT ':' expr` + chain `sm_more_names ',' IDENT
  ':' expr`.
- New production `smembers type IDENT ':' expr ',' sm_more_names ';'`
  (first declarator is a bitfield).
- Existing plain multi-name action generalized: emit `structaddbitfield` when
  `n->l` set, else `structaddmember` — plain-only path BYTE-IDENTICAL (all
  nodes keep `n->l==NIL`).  All-bitfield list emits SSA byte-identical to the
  separate-declaration form (diff-verified).

### The latent bug (found + fixed in the same code)
- `sm_more_names` chained `$1->r = n` (overwrote the HEAD's link), so a list
  with 3+ trailing declarators (4+ comma members total) silently dropped its
  middle members — `struct L{int a,b,c,d;}` = 3 members, `alloc 6` not 8.
  Latent because real lists rarely exceed one trailing name; the probe's
  3-item tail `n,o:4,r` was the first to hit it.
- Fixed all 3 chain productions to APPEND at the tail
  (`while (tl->r) tl=tl->r; tl->r=n`).  MP body byte-identical ⇒ MP has no
  4+-comma struct lists, so the correctness fix changes no gated output.

### Conflicts (no change)
- `yacc -v minic/minic.y` → sum per-state s/r in `y.output` = 117 s/r,
  0 r/r (the §9a/§9b/§9d baseline).  Lookahead `;` vs `,` after the bitfield
  `expr` keeps the new multi production conflict-free.

### Gate + validation
- `bitfield_multidecl_probe.c` (5 models): forms A all-bitfield / B
  bitfield+plain / C plain+bitfield / D mixed, field-independence (a=13→wraps
  3-bit 5, leaves b/c), sizeofs (A=2 B=4 C=4 D=8).  Model-independent golden,
  ends `bitfield_multidecl_probe done`.  Bug-loud: pre-fix `error:46: parse
  error`, build fails (git-stash confirmed).
- `make check` green; test-dos 417 → 422; MP compact body 689,760
  byte-identical; frontend-only → no emit audit.

### git scope
- qbe master: minic.y (smembers bitfield-list productions + generalized
  actions + sm_more_names tail-append chaining fix),
  minic/dos/examples/bitfield_multidecl_probe.c,
  minic/dos/tests/bitfield_multidecl_probe.golden.txt,
  tools/test-dos.sh (+5 entries).  No backend/build-script/newlibc change.

### ⇒ Next session (consumer-driven, with the user)
- (1) hunt another bounded no-consumer track from the scoping menu above
  (block-scope/static-local/`__far`/array fn-ptr vars; ptr-to-array; unnamed
  fn-ptr param; compound-literal-array; nested designated init);
- (2) PR #24 already merged;
- (3) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (4) Victor-native INT 1Ah/16h-free timer/kbd DOS probes if wanted.
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

# Next session (the §9c handoff confirmed the §9b baseline healthy and left only consumer-driven options; the user (AskUserQuestion) chose **HUNT A COMPILER TRACK**.  §9d [2026-06-18, this session] **CLOSED the LAST file-scope function-pointer grammar gap — the `__attribute__`-QUALIFIED pointee `void __attribute__((interrupt)) (*v)(void);` (plus the combined `__far __attribute__((...))` / `__attribute__((...)) __far` forms and the multi-declarator) was still a hard parse error after §9b (which deliberately admitted ONLY `TFAR` at file scope); minic now parses all of them; the fix is a frontend `minic.y` change → no emit audit; test-dos 412 → 417; conflicts UNCHANGED at the §9a/§9b baseline 117 s/r, 0 r/r; MP compact body 689,760 BYTE-IDENTICAL → no Victor run; `make check` green.**  EMPIRICAL SCOPING FIRST: of the §9b handoff's two named candidates, "a multi-declarator mixing `__far`/near pointees" was ALREADY CLOSED — §9b's per-declarator `gfnptr_decl` chains `TFAR`/near independently (verified `int (*nb)(int), __far (*fa)(int);` and `int __far (*fa)(int), (*nb)(int);` both parse on the §9c compiler), so the ONLY genuine gap was the `__attribute__`-qualified pointee.  It is ALSO a real latent consumer: newlibc interrupts.h spells a far ISR `void __far __attribute__((interrupt)) ...`, and §8w had to sidestep the fn-ptr-VARIABLE equivalent in `test_timer_dos` by declaring its handler as a plain `static void __far *`.  **THE §9b TRAP, AND HOW §9d AVOIDS IT:** §9b found that reusing the §8r `fpquals` nonterminal (which contains `fp_attr`) introduced 1 NEW reduce/reduce at yacc state 391 — `fp_attr`'s SEPARATE empty save-marker `fp_attr_save` collided with `attrreset`, both reduced after `ATTRIBUTE '(' '('`.  **THE FIX (frontend `minic.y`, additive):** restructured `gfnptr_decl` around a non-nullable `gfnptr_quals` run that SUBSUMES the §9b `TFAR` forms and adds a `gfnptr_attr` which reuses attropt's OWN `attrreset` empty marker (`gfnptr_attr: ATTRIBUTE '(' '(' attrreset attrlist ')' ')'`, dropping whatever it set + resetting `cur_fn_interrupt`/`cur_fn_weak` defensively) — so the attribute is parsed by the EXACT same item sequence as `attropt` and is distinguished only by the token after the closing `))` (IDENT continues `typed_decl`, `(` continues this fn-ptr declarator), which LALR(1) resolves by lookahead with NO new conflict.  `gfnptr_quals` = `{TFAR, gfnptr_attr, TFAR gfnptr_attr, gfnptr_attr TFAR}` and is deliberately NON-nullable (the bare no-qualifier declarator keeps its own two `gfnptr_decl` productions) so no empty reduction can ever compete with `typed_decl`'s `type TFAR attropt IDENT` on a TFAR lookahead.  The collapse into one `gfnptr_quals` symbol keeps the declarator's `$` indices fixed ($4 name, $7 fptpar) regardless of how many qualifiers were written.  Every qualifier is ACCEPTED and DROPPED — `__far` is a memory-model property and an interrupt/weak attribute on a pointer VARIABLE has no codegen meaning (the ISR ABI lives on a function DEFINITION's linkage, not a pointee type), so the pointer type is `IDIR(FUNC(base))` identical to the unqualified declarator.  Codegen confirmed correct: `$isr = { w $handler }`, `$sw = { w 0 }` (static, no `.globl`), and a real `__attribute__((interrupt))` FUNCTION after an attributed fn-ptr VAR still gets `export interrupt function` linkage while a plain function stays plain (NO attribute leak — the §8r concern).  **GATED bug-loud** by `minic/dos/examples/fnptr_attr_probe.c` (small+medium+compact+large+huge): on the §9b compiler the first attribute declaration is a `parse error` so the program does not build (confirmed by `git stash`-ing the §9d `minic.y` change and recompiling); the probe covers plain attribute / static attribute / `__far __attribute__` / `__attribute__ __far` / attributed multi-declarator / attributed initializer + a following plain function (proving no attribute leak), and its values are dispatch results (not pointer addresses) so the golden is model-independent and all five models are byte-identical (golden ends `fnptr_attr_probe done\n`, no §8y trailing-blank trap).  **VALIDATION:** `make check` green; full gate **417/417 ok** (412 → 417, the 5 new probe entries, no regressions); MP compact body **689,760 BYTE-IDENTICAL** (MP has no file-scope fn-ptr variables → the new branches never fire → no Victor run); frontend-only (`minic.y`) → no emit audit.  **git scope:** qbe master (`minic.y` = the `gfnptr_quals`/`gfnptr_attr` restructure of `gfnptr_decl`, subsuming the §9b `TFAR` forms; new `minic/dos/examples/fnptr_attr_probe.c` + `minic/dos/tests/fnptr_attr_probe.golden.txt`; 5 `tools/test-dos.sh` entries — NO compiler-backend/qbe/emit/build-script change, NO newlibc-tree change).  **⇒ Next session — the file-scope fn-ptr grammar family (§9a single, §9b multi+far, §9d attribute) is now COMPLETE; all remaining follow-ups are consumer-driven (pick with the user):** (1) merge newlibc **PR #24** (`minic-dostest-hw-gate` — the §8z `_dos_getvect`/`_dos_setvect`/`_chain_intr` intrinsics + `clock()` fill) into `victor9K_newlibc` main; (2) deepen the capstone (cooked `/dev/console`; a far-code `interrupts.c` model); (3) IF Victor-native timer/keyboard coverage is wanted, author INT 1Ah/16h-free DOS-hosted timing/keyboard probes — but the §8l/§8n bare-metal pattern already covers that ground; (4) hunt another no-consumer compiler track if one surfaces.  NO QBE/minic codegen bug open; NO carried compiler track remains.)

## §9d session notes (2026-06-18)

### The pick
- §9c handoff: §9b baseline confirmed healthy, no open bug / no carried track.
  User (AskUserQuestion) chose **Hunt a compiler track**.

### Empirical scoping (do this before assuming the gap)
- §9b named two bounded candidates.  Testing on the §9c compiler showed the
  "mixed __far/near multi-declarator" candidate ALREADY parses (§9b's
  per-declarator `gfnptr_decl` chains TFAR/near independently — both
  `int (*nb)(int), __far (*fa)(int);` and `int __far (*fa)(int), (*nb)(int);`
  parse).  The ONLY real gap was the `__attribute__`-qualified pointee, which
  is also a latent consumer (§8w's test_timer_dos handler, sidestepped as a
  plain `void __far *`; interrupts.h far-ISR spelling).

### Root cause + the §9b trap
- After `type`, `void __attribute__((..)) (*v)(..)` had no production: §9b
  admitted only TFAR at file scope because reusing §8r `fpquals` (with
  `fp_attr`, whose SEPARATE empty marker `fp_attr_save` collided reduce/reduce
  with `attrreset` at state 391) was the path it avoided.

### The fix (frontend minic.y, additive)
- Restructured `gfnptr_decl` around a NON-nullable `gfnptr_quals` run that
  subsumes the §9b TFAR forms and adds `gfnptr_attr`.
- `gfnptr_attr: ATTRIBUTE '(' '(' attrreset attrlist ')' ')'` — reuses
  attropt's OWN `attrreset` marker (NOT a new one), so the attribute is parsed
  by the same item sequence as attropt and distinguished only by the token
  after `))` (IDENT → typed_decl, `(` → fn-ptr declarator).  Action drops the
  attribute + resets cur_fn_interrupt/cur_fn_weak (defensive).
- `gfnptr_quals: TFAR | gfnptr_attr | TFAR gfnptr_attr | gfnptr_attr TFAR` —
  non-nullable (bare declarator keeps its own productions) so no empty
  reduction competes with `type TFAR attropt IDENT` on a TFAR lookahead.  The
  one `gfnptr_quals` symbol keeps declarator $ indices fixed ($4 name, $7 par).
- All qualifiers accepted + DROPPED (type = IDIR(FUNC(base))).  Verified
  codegen: `$isr = { w $handler }`, static `$sw = { w 0 }` (no .globl); a real
  __attribute__((interrupt)) FUNCTION after an attributed fn-ptr VAR keeps
  `export interrupt function` linkage, a plain fn stays plain — NO leak (§8r).

### Conflicts (no change)
- `yacc -v minic.y` → 117 shift/reduce, 0 reduce/reduce = the §9a/§9b baseline.
  The shared-`attrreset` design (vs §9b's failed `fp_attr`) is conflict-free,
  including the combined far+attribute forms.  (System yacc = /usr/bin/yacc =
  bison; the vendored minic/yacc still can't parse minic.y, §9c finding.)

### Gate + validation
- `fnptr_attr_probe.c` (5 models): plain / static / __far __attribute__ /
  __attribute__ __far / attributed multi-decl / attributed initializer + a
  following plain fn (no-leak).  Dispatch results → model-independent golden,
  ends `fnptr_attr_probe done\n` (no §8y trailing-blank).  Bug-loud: §9b
  compiler `parse error` at the first attribute decl, build fails (git-stash
  confirmed).
- `make check` green; test-dos 412 → 417; MP compact body 689,760
  byte-identical; frontend-only → no emit audit.

### git scope
- qbe master: minic.y (gfnptr_quals/gfnptr_attr restructure of gfnptr_decl,
  subsuming §9b TFAR), minic/dos/examples/fnptr_attr_probe.c,
  minic/dos/tests/fnptr_attr_probe.golden.txt, tools/test-dos.sh (+5 entries).
  No backend/build-script/newlibc change.

### ⇒ Next session (consumer-driven, with the user)
- The file-scope fn-ptr grammar family (§9a single, §9b multi+far, §9d
  attribute) is COMPLETE.
- (1) merge newlibc PR #24 (minic-dostest-hw-gate);
- (2) deepen the capstone (cooked /dev/console; far-code interrupts.c);
- (3) Victor-native timer/kbd DOS probes if wanted (§8l/§8n already cover it);
- (4) hunt another no-consumer compiler track if one surfaces.
- NO QBE/minic codegen bug open; NO carried compiler track remains.
---

Older session headers (§9c and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
