# Next session (§6e — continue Phase 6.  §6d [2026-06-11, this session] completed **step 4a: a real `__attribute__((interrupt))` ABI, end-to-end to a live timer interrupt on the Victor 9000.**  The old ISR story (an `asm "iret"` appended to the body — frame still up, no register save, block left unterminated) is GONE; the attribute now travels as QBE **`interrupt` function linkage** (`Lnk.isr`, parse.c keyword, lexh K regenerated — tools/lexh.c's stale token list now mirrors the runtime kwmap exactly) and the i8086 backend emits the **ES-safe ISR prologue/epilogue itself**: two CS-local words ahead of the entry label (`_qbe_isr_es_<fn>: dw 0`, `_qbe_isr_dg_<fn>: dw DGROUP` — the proven libstub `_dgroup_para` pattern, correct under both MZ relocs and raw-binary absolute patching), ES saved to static memory FIRST (newlibc's hardware-validated Victor rule — never on the stack), all registers saved, DS=ES=DGROUP established without trusting the interrupted DS, standard frame inside, and every `ret`/`retf` block replaced by the full restore + `iret`.  `libstub _qbe_get_cs` + the model-agnostic install idiom (`seg = (uint32_t)fnptr >> 16, 0 ⇒ qbe_get_cs()`) cover IVT installs in near- AND far-code models.  Probe-gated bug-loud (`isr_probe.c`, small+medium, 1006 software-INT fires; pre-fix toolchain dies at build: "last block misses jump").  Drivers ported to minic dialect (`bm_pic.c`, `bm_interrupts.c`, `bm_timer.c` — the 8253 ch2/IR2/vector-0x42 facts preserved, ALL the original's inline asm gone), and **`timer_bm` PASSES on MAME victor9k**: live IR2 ticks through the compiler-emitted ISR, ~200 ISR entries, delay-in-range, cli-freeze — now a `tools/test-victor.sh` golden entry.  KEY bare-metal lesson: a bare `sti` without a full PIC re-init (ICW1=0x17 Victor value, base 0x40, clear stale in-service bits, mask 0xFF) wild-jumps within milliseconds — the boot ROM leaves IRQs unmasked on stale vectors.  Emit-audit taught the ISR epilogue (`CHKT … live=isr` skip tag): baseline now **369 files / 117,002 regions / 0 violations**.  Gate 287→**289/289**; grammar 115 s/r unchanged.  MP compact: compiler-neutrality PROVEN byte-identical (old toolchain vs new-compiler+old-libstub, whole-image cmp); the only delta is the +5-byte `_qbe_get_cs` libstub insertion, and the shifted image is Victor-validated (math PROG.PY byte-exact vs host python3 via run-victor-sasi.sh); victor pipeline **4/4**.  Next: **step 4b** — extended-asm output constraints + Intel template translation (still unneeded by the timer path — decide whether keyboard/display force it); keyboard ISR (IR6) + display driver ports; serial RX ISR; grow the bare-metal battery → `tools/test-newlibc.sh`.)

## §6d session notes (2026-06-11)

### The ISR ABI (QBE `interrupt` linkage → i8086 backend)
- minic: `__attribute__((interrupt))` → `interrupt function` via fn_export_kw();
  the three asm-"iret" sites now emit a plain `ret` (the backend owns the
  epilogue).  Flag hygiene audited: every definition path resets
  cur_fn_interrupt (attrreset / type_and_ident) before optionally setting it.
- parse.c: `interrupt` linkage keyword (data rejects it); all.h Lnk.isr.
  **lexh trap**: adding ANY keyword needs a new perfect-hash K; tools/lexh.c's
  token list was stale (missing asm/loadfs/storefs/addfo/subfo/vargp) — it now
  mirrors the runtime kwmap exactly.  New K=520135915.  make check green.
- i8086_emitfn (Lnk.isr): CS-local `dw 0` (ES save) + `dw DGROUP` words emitted
  AFTER `.text` and BEFORE the entry label, so asm_to_omf.py's
  function-boundary splitter keeps them glued to the function.  Prologue:
  `mov [cs:es],es` → push ax/cx/dx/ds → `mov ds,[cs:dg]` → ES=DS → standard
  frame (bx/si/di layout unchanged at [bp-2/-4/-6]).  Epilogue (ALL Jret*
  forms): standard unwind → pop ds/dx/cx/ax → `mov es,[cs:es]` LAST → iret.
- Audit: ISR ret regions are tagged `; CHKT n live=isr`; check_emit_brackets.py
  skips them (the epilogue restores the INTERRUPTED context — every register
  including ES/DS legitimately differs from region entry; one fixed template).
  Stale-corpus trap: run-emit-audit.sh CACHES probe asm — rm the probe's
  build/chk-corpus entries after changing its codegen.

### isr_probe (the reduced gate)
- Software INT 0xF1 (user range): near-data store + far MMIO write (40:F0 ICA
  scratch) + callee with 32-bit divide inside the handler; live locals across
  triggers; 1006 fires (stack-balance hammer); vector saved/restored.
- Model-agnostic install: `lin=(uint32_t)fnptr; seg=lin>>16; if(!seg)
  seg=qbe_get_cs();` — far-code models carry seg:off in the pointer (cast
  preserves raw bits), near-code models call the new libstub helper (C name
  `qbe_get_cs`, asm `_qbe_get_cs`, caller's CS — near-code only by design).
- Bug-loud verified: stashed toolchain fails at BUILD ("last block misses
  jump") — the old asm-"iret" left the ret block unterminated.

### Bare-metal timer (minic/dos/newlibc/)
- bm_pic.c: **the load-bearing lesson** — full 8259A re-init before any sti
  (ICW1 0x17 = Victor ROM value NOT IBM 0x11; ICW2 0x40; ICW4 0x01; 8 specific
  EOIs to clear the ROM's in-service bits; mask 0xFF).  Without it the boot
  ROM's unmasked IRQs (IR7 vsync fires every frame) hit stale vectors whose
  RAM workspace the image overwrote — symptom: output RESTARTS from the banner
  (wild jump re-enters the 0x3000 loader stub).  pic_delay = two
  `jmp short $+2` (NASM-safe, no labels).
- bm_timer.c: 8253 ch2 mode 2 divisor 1000 via plain volatile-far byte stores
  (the original's intel_dev_write_byte asm was ia16-gcc store-merging damage
  control); double-read tick getter; delay_ms.  bm_interrupts.c: timer_isr
  (interrupt attr) = tick handler + specific EOI 0x62; IVT install via
  bm_set_vector (far write to 0:int*4).
- timer_bm.c: 7 printed phases (5 MHz rule), deterministic booleans/ranges
  only — MAME clocks ch2 at 125 kHz vs the documented 100 kHz, so tick-vs-wall
  numbers vary but tick accounting is exact.  PASS on MAME; golden +
  test-victor.sh entry "victor bare-metal (timer_bm, live ISR)".
- build-newlibc-baremetal.sh links bm_timer.c/bm_interrupts.c/bm_pic.c only
  when the program #includes their headers — hello_bm image stays stable.

### Open tracks (new + carried)
- newlibc step 4b: keyboard ISR (IR6) + display driver port; serial RX ISR;
  extended-asm output constraints + Intel template translation (NOTHING in
  the timer path needed it — decide whether keyboard/display do);
  tools/test-newlibc.sh once the bare-metal battery has a few more entries.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp; multi-decl items after the first skip block_scope_decl;
  Kw spill-slot sharing.

---

# Next session (§6d — continue Phase 6.  §6c [2026-06-11, this session] completed **step 3: the toolchain's first BARE-METAL program runs on the Victor 9000.**  `omf_link.py --raw-binary --load-addr 0x3000` emits a flat binary (selectors resolved at link time against the load paragraph, no MZ header, a 32-byte synthesized register-setup stub at the image head — the MAME Lua loader enters at 0:0x3000 with CS=DS=SS=0 — and BSS rides as zeros, no clear loop).  A **minic-built crt0** (`bm_crt0.c` `start()` → board init → `main()` → hlt loop) plus a minic-dialect **polled NEC 7201 serial console** (`bm_console.c`, newlibc's validated VIA2/8253-counter-0/WR-register sequence, pure volatile-far MMIO — the original's inline asm was all ia16-gcc workarounds) carry `hello_bm.c` to a PASS over serial under MAME: `tools/build-newlibc-baremetal.sh` + `tools/run-victor-baremetal.sh` (Lua autoboot loader, null_modem bitbanger capture).  Gated: `test_omf_link.sh` test 3 (deterministic raw-image structure asserts) + a `tools/test-victor.sh` golden-diff entry (victor pipeline 3/3).  DOSBox gate stays **287/287**; MP compact **byte-identical** (MZ-path refactor also proven by relink `cmp`).  Next: **step 4** — drivers/ISRs: ISR definition strategy, extended-asm output constraints + Intel template translation; port timer/display; more newlibc tests bare-metal; `tools/test-newlibc.sh` once a battery exists.)

## §6c session notes (2026-06-11)

### omf_link.py raw-binary mode (the step-3 enabler)
- `--raw-binary --load-addr 0x3000` (default 0x3000, paragraph-aligned): all
  loc==2/loc==3 selector fixups get `frame_para + base_para` patched in at
  link time (base_para = load_addr>>4) instead of an MZ reloc record; layout
  starts at byte 32 (`RAW_STUB_SIZE`) so segment paragraph alignment holds.
- The synthesized head stub: `cli; mov ss/sp; mov ds/es=DGROUP; jmp far
  entry` — all constants known at link time (same `_compute_ss_sp()` as the
  MZ header, SS=DGROUP + SP=stack-top-in-DGROUP for the small model).  The
  hlt-padded 32-byte head means entry lands at image para 2.
- Program-RAM ceiling check: image end past 0x9F000 (video RAM at 0xA0000)
  is a link error.
- MZ path refactor (shared `_concat_segments`/`_compute_ss_sp`) verified
  byte-identical: snprintf_test relink `cmp` + MP compact whole-image `cmp`.
- `test_omf_link.sh` test 3 asserts the raw structure (no MZ sig, stub
  opcodes at fixed offsets, entry 0302:0000, far-call selector 0303
  absolute); also fixed test 2's stale-crt0 collision (stevie-orig now
  carries crt0_exe.obj — excluded from the smoke link).

### Bare-metal runtime story (minic/dos/newlibc/)
- `bm_crt0.c`: C `start()` (OMF `_start`) → `bm_board_init()` → `main(0,0)`
  → `while(1) hlt`.  No DOS crt0, no PSP, no HALT2DOS rewrite — bare metal
  WANTS the hlt idle loop.  BSS zero-fill not needed (in-image zeros).
- `bm_console.c` + `.h`: polled 7201 channel-A TX at 9600.  Mirrors
  newlibc drivers/console.c exactly (VIA2 port A bit0 internal clock; 8253
  counter 0 — NOT counter 1 — mode 2 LSB+MSB divisor 8; channel reset +
  WR0/WR4=0x44/WR3=0xC1/WR5=0xEA/WR1=0); all access via v9k_hw.h
  HW_READ/WRITE_BYTE volatile-far MMIO.  The original's inline asm
  (SAVE_ES/RESTORE_ES, forced byte stores) is ia16-gcc damage control this
  backend doesn't need.  Plus bm_puts/bm_putu (32-bit udiv)/bm_puthex.
- `hello_bm.c`: 48 KB raw image; checks volatile 16-bit mul, 32-bit
  unsigned divide, strlen, hex print; __V9BEGIN__/__V9END__ sentinels +
  PASS:/FAIL: verdict (newlibc run_test.sh regex convention).
- libstub on bare metal: `--no-stdio` libstub links fine (its INT 21h
  sites — exit/putc/dos_*/int86 — are functions, nothing runs at startup;
  `_dgroup_para: dw DGROUP` resolves via the raw selector patch).  They are
  LANDMINES if called; the real fix is newlibc replacing libstub (the
  Phase-6 end state).

### Harness
- `tools/build-newlibc-baremetal.sh [--load-addr=] <name|path.c>`: test TU +
  bm_crt0 + bm_console, small model, `--no-stdio` libstub, raw link.  Bare
  name resolves minic/dos/newlibc/ first, then newlibc tests/.
- `tools/run-victor-baremetal.sh <bin> [secs]`: MAME victor9k + Lua
  autoboot loader (newlibc phase-3 pattern: write_u8 loop at 0x3000, zero
  segment regs, IP=0x3000), serial via `-rs232a null_modem -bitbanger`,
  sentinel-trimmed stdout, exit 77 skips, same orphan-killer watchdog as
  run-victor-sasi.sh.
- `tools/test-victor.sh` new entry "victor bare-metal (hello_bm)" diffs
  against `minic/dos/tests/hello_bm.golden.txt`; skips when newlibc tree or
  MAME absent.  Victor pipeline 3/3.

### Open tracks (new + carried)
- newlibc step 4: ISR definition strategy; extended-asm output-constraint
  store marking + Intel-syntax template translation; port timer/display
  drivers to minic dialect (the bm_console port shows most driver asm is
  removable); grow the bare-metal test battery → tools/test-newlibc.sh.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp; multi-decl items after the first skip block_scope_decl;
  Kw spill-slot sharing.

---

Older session headers (§6a and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
