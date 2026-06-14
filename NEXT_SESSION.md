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

# Next session (§7i — continue Phase 6 / open compiler tracks.  §7h [2026-06-14, this session] closed the carried **far static-DATA-ptr relocation gap (§1g)** — the user picked it.  **The gap:** under a far-data model (compact/large/huge) a static/file-scope data initializer holding a symbol address — `int *pcell = &cell;`, `int *mid = &arr[2];`, `char **env_like = words;` (the §6a `cival_eval`/`emit_global_sym_init` scalar-symbol-address path) — is a **4-byte far pointer** (seg:off), but `tools/asm_to_omf.py` emitted it as `dd _sym` (a single 32-bit OMF loc-9 OFFSET fixup) so the SEGMENT word was left 0 → a wrong-segment far deref at runtime.  The `.long _sym` → `dw _sym / dw seg _sym` split that fixes this (FIX 3, far-pointer DATA reloc) was gated behind `split_sym_long = far_data or model == 'medium'`, and `far_data` itself is `far_static_data and model in (compact/large/huge)` — i.e. it only fired when the build opted into `--far-static-data` (MicroPython's `MP_SPLIT_STACK` layout, which routes statics into their own far `<BASE>_DATA` segment).  A **default** compact/large/huge build (statics in DGROUP, `build-example.sh` without `QBE_FAR_STATIC_DATA=1` — which is how the gate builds every probe) got `far_data=False` → `split_sym_long=False` → the buggy offset-only `dd _sym`.  **Root insight:** the `.long _sym` split is about whether DATA POINTERS ARE FAR, which is true for every far-data model regardless of where the statics physically live — it is INDEPENDENT of the `--far-static-data` section/class ROUTING that `far_data` controls.  With `--far-static-data` the far pointer's `seg _sym` resolves into `<BASE>_DATA`; without it, into DGROUP; either way the segment word must be emitted and relocated.  **The fix (one line, `tools/asm_to_omf.py`):** `split_sym_long = model in ('compact', 'large', 'huge', 'medium')` — fires for ALL far-data models plus medium (medium stays for its far-CODE function-pointer initializers, the §6k case).  `far_data` (still `far_static_data and …`) is left untouched, since it ALSO drives the `data_seg`/`bss_seg`/`*_cls` section routing further down.  **Byte-identical for the MP corpus BY CONSTRUCTION:** MP compact uses `--far-static-data` → `far_data=True` → `split_sym_long` was ALREADY `True` there, so the widening only newly affects DEFAULT (non-`--far-static-data`) compact/large/huge builds — confirmed by **MP compact rebuilding to a body of EXACTLY 731,088 bytes, byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088).  **Verified bug-loud:** the `static_sym_init_probe` under default compact PRE-fix printed raw offsets `4194 / 4192` plus an `Illegal byte sequence` from the `%s` deref of the wrong-segment `env_like[0]` (a near-perfect "loud" failure — corrupted output AND a garbled string read); POST-fix the `.omf.asm` emits `dw _words+0 / dw seg _words`, `dw _cell+0 / dw seg _cell`, `dw _arr+4 / dw seg _arr`, and the probe prints byte-exact `7 / 9 / w0` on compact, large, AND huge in DOSBox, identical to the existing model-independent golden (`sizeof(int)==2` everywhere).  **Gated** by adding `:compact`, `:large`, `:huge` to the existing `static_sym_init_probe` entry in `tools/test-dos.sh` (it was small+medium only — the near-data models, which need no segment word and are unaffected); the gate comment was rewritten from "the §1g gap is REAL" to record the fix.  **test-dos 314/314 → 317/317** (the three new far-data entries pass; the batched DOS pipeline reports `317/317 ok`, zero FAIL, every prior entry unchanged; small+medium re-verified `[ok]`).  Toolchain checks: `make check` green; **MP compact body 731,088 bytes byte-identical** → codegen unchanged → no Victor run; and since `asm_to_omf.py` is a TOOLCHAIN script (NOT `i8086/emit.c` or middle-end), the emit-bracket audit was NOT required.  The "far static-DATA-ptr reloc (§1g)" open track is now CLOSED — and it removes the last "medium-only" caveat from probes that take a static address: scalar symbol-address initializers now relocate correctly in every model, which newlibc-style tables-of-pointers will want under far-data.  Next: pick a carried track — **Kw spill-slot sharing** (frame-size lever, no consumer pain); the **bounded aoa init/multi-declarator gap (§7e)** — brace-init `jmp_buf x[2]={…}` / multi-decl `jmp_buf a[2], b[2]` still ignore `g_td_arraydim`, no realistic consumer; OR resume **Phase-6 newlibc gating**: `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX + an rs232a TXD→RXD MAME loopback colliding with the rs232a `null_modem` capture → move the gate's serial capture to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.  There is NO QBE backend bug currently open — the carried tracks are all minic/backend feature gaps or Phase-6 harness work.)

## §7h session notes (2026-06-14)

### The bug (carried open track — §1g far static-DATA-ptr relocation)
- Under a far-data model (compact/large/huge) a static/file-scope data
  initializer holding a symbol address (`int *pcell = &cell;`,
  `int *mid = &arr[2];`, `char **env_like = words;`) is a 4-byte FAR pointer
  (seg:off), but `tools/asm_to_omf.py` emitted it as `dd _sym` — a single
  32-bit OMF loc-9 OFFSET fixup — so the SEGMENT word was left 0 → a
  wrong-segment far deref at runtime.
- `static_sym_init_probe` (the §6a `cival_eval`/`emit_global_sym_init`
  scalar-symbol-address probe) under DEFAULT compact printed raw offsets
  `4194 / 4192` + an `Illegal byte sequence` `%s` deref of `env_like[0]`.

### Root cause (`tools/asm_to_omf.py`)
- The `.long _sym` → `dw _sym / dw seg _sym` split (FIX 3) was gated by
  `split_sym_long = far_data or model == 'medium'`, and `far_data` is
  `far_static_data and model in (compact/large/huge)` — so the split only
  fired when the build passed `--far-static-data` (MicroPython's
  `MP_SPLIT_STACK` layout).  A default compact/large/huge build (statics in
  DGROUP — how `build-example.sh` builds every gate probe, no
  `QBE_FAR_STATIC_DATA=1`) got `far_data=False` → `split_sym_long=False` →
  the offset-only `dd _sym`.
- The split is about whether DATA POINTERS ARE FAR (true for every far-data
  model, independent of where statics physically live), NOT about the
  `--far-static-data` section/class routing `far_data` controls.

### The fix (one line, gated to far-data + medium)
- `split_sym_long = model in ('compact', 'large', 'huge', 'medium')`.
  Fires for ALL far-data models (data pointers are far) plus medium (its
  far-CODE function-pointer initializers, the §6k case).  `far_data` left
  untouched — it still drives `data_seg`/`bss_seg`/`*_cls` routing below.
- MP compact uses `--far-static-data` → `far_data` was already True →
  `split_sym_long` was already True there → MP byte-identical BY
  CONSTRUCTION (the widening only newly affects default, non-far-static-data
  compact/large/huge builds).

### Gate (bug-loud) + toolchain checks
- Added `:compact`, `:large`, `:huge` to the existing `static_sym_init_probe`
  entry in `tools/test-dos.sh` (was small+medium — near-data, unaffected).
- Bug-loud verified: PRE-fix default compact → `4194 / 4192` + Illegal byte
  sequence; POST-fix `.omf.asm` emits `dw _sym+N / dw seg _sym` and the
  probe prints byte-exact `7 / 9 / w0` on compact, large, AND huge.
- **test-dos 314 → 317** (`317/317 ok`, zero FAIL).  `make check` green.
- `asm_to_omf.py` is a TOOLCHAIN script (NOT emit.c / middle-end) → NO emit
  audit.  MP compact body EXACTLY **731,088 bytes**, byte-identical → no
  Victor run.

### ⇒ Next session (§7i): carried tracks (no QBE bug currently open)
- Kw spill-slot sharing (frame-size lever, no consumer pain).
- Bounded aoa gap (§7e): brace-init `jmp_buf x[2]={…}` / multi-declarator
  `jmp_buf a[2], b[2]` still ignore `g_td_arraydim`; no realistic consumer.
- Phase-6 newlibc `serial_loopback_test` (needs NEW harness plumbing —
  channel-A polled RX + rs232a TXD→RXD loopback, move gate capture to
  channel B, RX-timing determinism); `interrupt_test` stays SKIPPED (§6v).

---

Older session headers (§7h and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
