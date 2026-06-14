# Next session (§7k — continue Phase 6 / open compiler tracks.  §7j [2026-06-14, this session] closed the carried **bounded array-of-array-typedef gap (§7e)** — the brace-init and multi-declarator aoa forms the §7e single-declarator fix had left open — the user picked it and chose to do BOTH forms.  **Background:** §7e made `jmp_buf bufs[N];` (single-declarator, uninitialised; file-scope / block-local / static-local) work by registering an array-of-array-typedef as `IDIR(elem)` with a `var_aoa_dim`=D flag and desugaring a one-level subscript `bufs[i]` to the bare pointer-add `bufs + i*D` (the row address, no deref) via `mkidx` (minic.y ~5318); but two declarator SHAPES still ignored the typedef's inner dimension `g_td_arraydim` entirely.  **(1) MULTI-DECLARATOR aoa** (`jmp_buf a[2], b[2];`, block-local): each declarator reached the multi-decl sized-array (`'B'`) branch of `emit_local_multi_decl_full` (minic.y ~6646), which sized the slot `count*sizeof(elem)` (4 B) instead of `count*D*sizeof(elem)` (32 B) and never set the aoa flag — so `b[i]` was lowered as a SCALAR `loadw` (value-as-pointer) rather than the row address, and `setjmp(b[i])` ran through a garbage pointer (the unfixed probe `alloc4 4` + `%t = loadw (b + i*2)` confirmed it).  **(2) BRACE-INIT 2-D table** (`row3_t t[2] = {{1,2,3},{4,5,6}};` where `typedef int row3_t[3]`): minic has NO true `int[N][3]` (`int x[2][3]` is a hard parse error), so a typedef element is the ONLY way to write a 2-D constant table — and the four local array brace-init rules (dcls sized/unsized + stmt sized/unsized) sized the element as `sizeof(elem)` and stored each top-level item with a single scalar `expr()`, so a nested `{…}` row aborted the compiler (Abort trap 6).  **The fix (all in minic.y, frontend only):** (a) the `emit_local_multi_decl_full` `'B'` branch now reads `aoa = g_td_arraydim` and, when >0, sizes `count*D*sizeof(elem)` + calls `var_set_aoa_dim` (gated `aoa>0` → non-aoa multi-decls byte-identical); (b) a new `static Node *mk_aoa_array_init(v, initlist, dim, zerofill, rows, *out_rows)` helper flattens each `{…}` row into per-element stores `*(v + (r*dim + c))` — built as RAW `'@'(+ V off)` nodes that BYPASS `mkidx` (so the linear index is NOT re-multiplied by D; the bare `'+'` Scale scales by `sizeof(elem)` since `v` decays to `IDIR(elem)`), with row-aligned braced rows + a linear-fill fallback for brace elision + an optional `N*dim` zero-fill; (c) all four brace-init rules (dcls sized ~8461, dcls unsized ~8547, stmt sized ~8980, stmt unsized ~8999) gained an `aoa>0` branch that sizes `N*D*sizeof(elem)`, registers `IDIR(elem)`, calls `var_set_aoa_dim`, and inits via `mk_aoa_array_init` (dcls context `expr()`s the chain at parse time; stmt context defers it as an `Expr` stmt for control-flow order) — every `aoa==0` path left textually unchanged.  **Scope deliberately bounded:** only the BLOCK-LOCAL multi-decl form (the user's named `jmp_buf a[2], b[2]` target, which reaches `emit_local_multi_decl_full` via the §7c array-first stmt/dcls rule) was fixed AND gated; the FILE-SCOPE / function-local-STATIC multi-decl array-first forms (`static jmp_buf fa[2], fb[2];`) are a SEPARATE, PRE-EXISTING **parse-error** gap (no such grammar production — confirmed `parse error` on the unfixed AND fixed compiler), NOT an aoa sizing bug, so `emit_local_multi_decl`'s `'B'` branch and the file-scope `ext_decllist` `'B'` branch were left untouched (no parseable consumer to gate them bug-loud, per the "only fix what you gate" house rule); the plain `jmp_buf a, b;` (array-typedef instance, not array-OF) multi-decl also stays a bounded gap.  **Gated bug-loud:** new `minic/dos/examples/aoa_extended_probe.c` (block-local multi-decl `jmp_buf a[2],b[2]` cross-frame longjmp through BOTH declarators → `md=10,11,20,21`; dcls-context sized 2-D table `t1`; stmt-context sized `t2` + unsized `t3`; write-back-through-indexed-rows `t1x2sum=42`) + golden `minic/dos/tests/aoa_extended_probe.golden.txt`, wired `:medium :compact :large` (matching `arr_jmpbuf_probe`).  Bug-loud confirmed: the unfixed compiler **aborts** on this probe (multi-decl scalar-load + nested-brace abort); the three model builds produce byte-identical correct output.  **test-dos 317/317 → 320/320** (`320/320 ok`, every prior entry unchanged).  Toolchain checks: `make check` green; grammar conflicts UNCHANGED at **115 shift/reduce, 0 reduce/reduce** (no new productions — the nested `{…}` already parses as an `inititem`; only actions changed); **MP compact body EXACTLY 731,088 bytes, byte-identical** to the documented golden (image 751,664 = header 20,576 + body 731,088) → codegen unchanged → no Victor run; and since this is a `minic.y` FRONTEND change (NOT `i8086/emit.c` or middle-end) the emit-bracket audit was NOT required.  The "bounded aoa init/multi-declarator gap (§7e)" open track is now CLOSED for the parseable forms.  Next: pick a carried track — **Kw spill-slot sharing** (frame-size lever, no consumer pain); the REMAINING aoa sub-gaps (file-scope/static multi-decl array-first — a grammar parse-error gap; plain `jmp_buf a, b;` multi-decl — array-typedef-instance decay) if a consumer appears; OR resume **Phase-6 newlibc gating** — `interrupt_test` stays SKIPPED (§6v), the newlibc-under-far-DATA-models (compact/large) stdio story waits for a far-DATA consumer.  There is NO QBE backend bug currently open.)

## §7j session notes (2026-06-14)

### The gap (carried open track — §7e bounded aoa forms)
- §7e closed single-declarator uninitialised aoa (`jmp_buf bufs[N];`) via
  `var_aoa_dim` + `mkidx` (row-address desugar).  Two declarator SHAPES still
  ignored the typedef inner dim `g_td_arraydim`:
  - **multi-declarator** `jmp_buf a[2], b[2];` — each `'B'` declarator sized
    `count*sizeof(elem)` (4 B) not `count*D*sizeof(elem)` (32 B), no aoa flag →
    `b[i]` was a scalar `loadw` (value-as-pointer); `setjmp(b[i])` ran through
    garbage.
  - **brace-init 2-D table** `row3_t t[2] = {{1,2,3},{4,5,6}};` — minic has no
    real `int[N][3]` (hard parse error), so a typedef element is the ONLY 2-D
    table; the four local brace-init rules sized `sizeof(elem)` and stored each
    item with one scalar `expr()` → a nested `{…}` row Abort-trap-6'd the
    compiler.

### The fix (minic.y, frontend only — all `aoa>0`-gated + additive)
- `emit_local_multi_decl_full` `'B'` branch (~6646): `aoa = g_td_arraydim`;
  size `count*SIZE(elem)*(aoa?aoa:1)`; `var_set_aoa_dim(v, aoa)` when aoa>0.
- New `mk_aoa_array_init(v, initlist, dim, zerofill, rows, *out_rows)`
  (~after `mk_local_array_init`): flattens braced rows into RAW `'@'(+ V off)`
  stores (BYPASSES `mkidx` so the linear index is NOT ×D again; the bare `'+'`
  Scale scales by `sizeof(elem)` via `v`'s `IDIR(elem)` decay), row-aligned
  rows + linear-fill fallback + optional `N*dim` zero-fill.
- All four brace-init rules — dcls sized (~8461), dcls unsized (~8547), stmt
  sized (~8980), stmt unsized (~8999) — gained an `aoa>0` branch: size
  `N*D*sizeof(elem)`, register `IDIR(elem)`, `var_set_aoa_dim`, init via
  `mk_aoa_array_init` (dcls `expr()`s at parse time; stmt defers as `Expr`).
  Every `aoa==0` path left textually unchanged → non-aoa byte-identical.

### Scope (deliberately bounded — "only fix what you gate")
- FIXED + GATED: block-local multi-decl `jmp_buf a[2], b[2]` (reaches
  `emit_local_multi_decl_full` via the §7c array-first rule) + block-local
  brace-init 2-D tables (dcls + stmt, sized + unsized).
- LEFT (separate gaps, no parseable/realistic consumer): file-scope /
  static multi-decl array-first (`static jmp_buf fa[2], fb[2];`) is a
  PRE-EXISTING grammar **parse-error** gap (verified `parse error` unfixed AND
  fixed), not aoa sizing — so `emit_local_multi_decl`'s `'B'` branch and the
  file-scope `ext_decllist` `'B'` branch were NOT touched; plain
  `jmp_buf a, b;` (array-typedef instance) multi-decl also stays bounded.

### Gate (bug-loud) + checks
- `minic/dos/examples/aoa_extended_probe.c` + golden
  `minic/dos/tests/aoa_extended_probe.golden.txt`, wired `:medium :compact
  :large` (matching `arr_jmpbuf_probe`).  Output: `md=10,11,20,21` /
  `t1=1,2,3,4,5,6` / `t2=10,20,30,40,50,60` / `t3=7,8,9,11,12,13` /
  `t1x2sum=42` / `done`.
- Bug-loud confirmed: unfixed compiler ABORTS (scalar-load multi-decl +
  nested-brace abort); all three model builds byte-identical.
- **test-dos 317 → 320**.  `make check` green.  Conflicts UNCHANGED
  (115 s/r, 0 r/r — no new productions, only actions).
- `minic.y` FRONTEND change → NO emit audit.  **MP compact body 731,088 B
  byte-identical** → no Victor run.

### ⇒ Next session (§7k): carried tracks (no QBE bug currently open)
- Kw spill-slot sharing (frame-size lever, no consumer pain).
- Remaining aoa sub-gaps IF a consumer appears: file-scope/static multi-decl
  array-first (grammar parse-error gap); plain `jmp_buf a, b;` multi-decl
  (array-typedef-instance decay).
- Phase-6 newlibc: `interrupt_test` stays SKIPPED (§6v); far-DATA-model
  (compact/large) newlibc stdio waits for a far-DATA consumer.

---

# Next session (§7j — continue Phase 6 / open compiler tracks.  §7i [2026-06-14, this session] gated the UNMODIFIED upstream `serial_loopback_test` bare-metal through bm_testhost — the user picked the Phase-6 newlibc track.  **battery 38/38 → 39/39, test-dos UNCHANGED (bare-metal-only gate, like §6q/§6u–§6y), ZERO compiler/qbe/emit/minic changes.**  This is the **first gate of the newlibc raw-serial console API**: Test 1 drives `console_putc`/`console_getc_nonblock`/`console_rx_ready` on 7201 channel A; Test 2 drives `/dev/tty` write/read — both over a hardware TXD→RXD loopback, and both writing their PASS/FAIL to the DISPLAY (VRAM, uncaptured).  **Two NEW capabilities, both newlibc-support-glue only (NOT compiler):** (1) **channel-A polled RX** in `bm_console.c` — `bm_console_putc`/`bm_console_getc`/`_getc_nonblock`/`_rx_ready` (channel-A RX was already enabled by `bm_console_init`'s WR3=0xC1; this completes the polled path the upstream `drivers/console.c` exposes), aliased to the unprefixed `console_*` names in `bm_shim.c`, AND `tty_dev_*` re-routed there to MATCH UPSTREAM — upstream `/dev/tty` IS the raw serial debug console (`tty_dev_read=console_getc`, `tty_dev_write=console_putc`), DISTINCT from `/dev/console`'s cooked keyboard (`console_dev_*`); our default bare-metal port had lazily folded the two onto the cooked `bm_tty` (the bare machine's only interactive console), and serial_loopback_test is the first test to need them separated.  (2) **a MAME loopback harness mode** — channel A becomes `-rs232a loopback` (the test's TXD→RXD data path; the `loopback` device is NOT a bitbanger, so it cannot also capture), so the captured testhost debug console MOVES to channel B (`-rs232b null_modem -bitbanger`), and `bm_console.c` under `-DBM_SERIAL_LOOPBACK` routes `bm_putc` (the harness output: testhost preamble + the `printf` result line via bm_tty) to a polled channel-B TX it programs (counter-1 baud + the WR sequence `bm_serial.c` uses, polled WR1=0).  **All new behavior is `#ifdef BM_SERIAL_LOOPBACK`-gated** (defined ONLY for serial_loopback_test, via a per-test `EXTRA_CFLAGS` applied to every TU in `build-newlibc-baremetal.sh`) **plus additive** (the `bm_console_*` RX fns + a new `display_putc` alias are `--gc-sections`-stripped when unreferenced), so every other bm build is behavior-identical — re-verified `snprintf_test`/`stdin_test`/`stdio_bm`/`serial_bm` → all `[ok]`.  The golden is the testhost result line, so **`test returned 0` proves both subtests round-tripped all 8 bytes (5 in Test 1 + 3 in Test 2) through the channel-A loopback**.  **Bug-loud:** `return 0` requires the real loopback (a timeout → `failures++` → `returned 1`), and any non-loopback channel-A device both breaks the loopback AND — because two `null_modem`s collide on MAME's bitbanger numbering — loses the channel-B capture, so the gate fails LOUDLY (empty/wrong capture, never a spurious `returned 0`); without the channel-A RX code the build won't even link (`console_*` undefined).  **Plumbing:** `run-victor-baremetal.sh` `$V9K_SERIAL_LOOPBACK=1` (rs232a loopback + channel-B capture), `test-newlibc.sh` an optional 8th `:<loopback>` entry field (`lb`), golden `minic/dos/tests/serial_loopback_test.golden.txt`.  **SMALL** (61,171 B `_TEXT`, under the 64 KB ceiling; DGROUP _DATA+_BSS+STACK = 54,810 B); FIRST-RUN PASS on MAME, byte-identical across two runs, 30-s budget.  Newlibc bare-metal support glue (NOT compiler/qbe/emit/minic; MP does not link `bm_console.c`/`bm_shim.c`) → **no emit audit, no MP byte-compare**.  With the keyboard family (§6w/§6x/§6n/§6o/§6t), the PIC mask API (§6y), and now the serial loopback / raw-serial console (§7i) all gated, the only remaining ungated phase-3 test is `interrupt_test` (stays SKIPPED — §6v's `[90,110]` FAIL-window + raw iteration-count brittleness); the display-only/`hlt`-loop tests (memory/segment/simple_screen/font/font_ram/font_layout/minimal_irq) are NOT bm_testhost-shaped and already covered by hand-mirrored `bm_*` ports.  Next: the newlibc-under-far-DATA-models (compact/large) stdio story when a far-DATA consumer appears; OR pick from the carried compiler tracks — **Kw spill-slot sharing** (frame-size lever, no consumer pain); the **bounded aoa init/multi-declarator gap (§7e)** — brace-init `jmp_buf x[2]={…}` / multi-decl `jmp_buf a[2], b[2]` still ignore `g_td_arraydim`, no realistic consumer.  There is NO QBE backend bug currently open.)

## §7i session notes (2026-06-14)

### The test (first newlibc raw-serial console gate)
- UNMODIFIED upstream `phase3_newlib/tests/serial_loopback_test.c`.  Test 1
  (`console_putc`/`console_getc_nonblock`/`console_rx_ready`, pattern "Rx09!")
  and Test 2 (`/dev/tty` `write`/`read`, pattern "vfs"), both over a HARDWARE
  TXD→RXD loopback on 7201 channel A.  Its own PASS/FAIL text goes to the
  DISPLAY (`display_puts`/`display_putc` → VRAM, uncaptured); `main` returns 1
  on any failure, 0 only when both subtests round-trip every byte.

### Why this needed real new plumbing (three obstacles)
- **Channel-A RX did not exist.**  `bm_console.c` was polled-TX-only; there
  were no `console_*` aliases.  Added `bm_console_getc`/`_getc_nonblock`/
  `_rx_ready` + `bm_console_putc` (RX was already enabled by WR3=0xC1).
- **`/dev/tty` was mis-routed.**  Upstream `/dev/tty` (`tty_dev_*`) is the RAW
  serial debug console (`console_getc`/`console_putc`); `/dev/console`
  (`console_dev_*`) is the cooked keyboard.  Our `bm_shim.c` had lazily aliased
  `tty_dev_*` → `console_dev_*` (the cooked `bm_tty` keyboard).  Under
  `-DBM_SERIAL_LOOPBACK`, `tty_dev_*` now routes to the raw channel-A path,
  matching upstream — Test 2's reason for existing.
- **Capture collided with the loopback.**  The harness captures channel A
  (`-rs232a null_modem -bitbanger`).  The loopback needs `-rs232a loopback`
  (TXD→RXD, NOT a bitbanger → cannot capture), so the testhost preamble +
  result line had to MOVE to channel B.  `bm_console.c` under the flag programs
  channel B for polled TX and routes `bm_putc` there; the harness captures
  `-rs232b null_modem -bitbanger`.  Channel A then carries ONLY the test's
  loopback bytes (display output is VRAM, never serial — no pollution).

### The fix (all `#ifdef BM_SERIAL_LOOPBACK`-gated + additive)
- `bm_console.c`: channel-A `console_*` RX/TX (always compiled, gc-stripped
  when unreferenced); under the flag, `bm_putc` → channel B + `bm_console_b_init`.
  Non-loopback `bm_putc` branch left textually identical.
- `bm_shim.c`: under the flag, raw-serial `tty_dev_*` + the `console_*` aliases;
  unconditional `display_putc` alias; `#include "bm_console.h"`.
- `build-newlibc-baremetal.sh`: per-test `EXTRA_CFLAGS` (only
  serial_loopback_test → `-DBM_SERIAL_LOOPBACK`), threaded into every
  `compile_unit` with the script's `set -u`-safe array idiom.
- `run-victor-baremetal.sh`: `$V9K_SERIAL_LOOPBACK=1` → `-rs232a loopback
  -rs232b null_modem -bitbanger CAP` (single bitbanger ⇒ binds to channel B).
- `test-newlibc.sh`: 8th `:<loopback>` field (`lb`) → `V9K_SERIAL_LOOPBACK=1`.

### Gate (bug-loud) + checks
- New entry `serial_loopback_test:30::::::lb`; golden = the 4 testhost lines
  ending `bm_testhost: test returned 0`.  FIRST-RUN PASS, deterministic across
  two runs; `tools/test-newlibc.sh serial_loopback_test` → `[ok]`.
- Bug-loud confirmed: with channel A as a plain `null_modem` (no echo) the test
  cannot reach `returned 0` and the channel-B capture is lost (loud failure).
- SMALL: `_TEXT` 61,171 B (< 64 KB), DGROUP 54,810 B.  Battery **38 → 39**.
- Newlibc support glue (NOT compiler) → NO emit audit, NO MP byte-compare;
  bare-metal-only → test-dos UNCHANGED.  `make check` unaffected (no QBE change).

### ⇒ Next session (§7j): carried tracks (no QBE bug currently open)
- Kw spill-slot sharing (frame-size lever, no consumer pain).
- Bounded aoa gap (§7e): brace-init / multi-declarator array-of-array-typedef
  still ignore `g_td_arraydim`; no realistic consumer.
- newlibc-under-far-DATA-models (compact/large) stdio when a far-DATA consumer
  appears; `interrupt_test` stays SKIPPED (§6v).

---

Older session headers (§7i and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
