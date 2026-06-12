# Next session (§6f — continue Phase 6.  §6e [2026-06-12, this session] completed **step 4b: the display, keyboard, and serial-RX drivers run bare-metal on the Victor 9000 through compiler-emitted ISRs, and the newlibc bare-metal battery is a standing gate (`tools/test-newlibc.sh`, 5/5).**  Three new minic-dialect driver ports (`bm_display.c`+`bm_font_data.c`, `bm_keyboard.c`, `bm_serial.c`) and three new MAME-verified tests, each PASS on the FIRST run — the §6d ISR ABI carried them with zero compiler changes.  `display_bm`: font→0000:0C00, 6845 bring-up, glyph-pointer VRAM writes, scroll/tab/backspace — all self-verified from the machine (VRAM/font readback over serial; the 6845 only reads back R14/R15, so cursor is the only CRTC readback).  `keyboard_bm`: MAME natural-keyboard types "v9k" (harness `V9K_KEYPOST`), every keystroke travels VIA-CS2 shift register → KBINT/IR6 → compiler-emitted ISR → ring buffer → cooked ASCII — the port is interrupt-driven ONLY (MAME wires IR6 to the VIA2 IRQ, so each state-machine step re-edges IR6), with the timer ISR live concurrently.  `serial_bm`: a REAL 7201 channel-B RX ISR (newlibc's own was a stub) — IR1, WR1=0x18 int-on-all-RX, and a drain-until-RR0-empty loop because the edge-triggered 8259A never re-edges while the 7201 holds INT for an unread byte; harness `V9K_SERIAL_IN` attaches a byte file to a second null_modem on rs232b MID-RUN from Lua (attach-at-boot would stream the bytes before the program initializes; two bitbangers renumber the capture option to `-bitbanger1`).  **KEY step-4 question ANSWERED: extended-asm is NOT needed for the driver suite** — newlibc's display/keyboard inline asm was entirely ia16-gcc ES-workarounds (the interrupt ABI owns ES now) + a pushf/cli flags-save the single-producer/single-consumer ring design makes unnecessary; the CRTC asm is just volatile-far MMIO.  `bm_install_isr()` exported from bm_interrupts.c for driver TUs.  Gates: test-dos 289/289, test_omf_link all pass, victor pipeline 4/4, test-newlibc 5/5; NO toolchain change (new sources + harness scripts only) so no emit audit / MP byte-compare triggered.  Next: **step 4c** — sweep more newlibc phase-3 tests onto the bare-metal battery (interrupt_test/pic_test/crtc_test/memory_test should be near-free now); a cooked console (display output + keyboard input) toward retiring libstub's DOS-only stdio; block/SASI driver port for bare-metal FAT; serial TX ISR if a consumer appears.)

## §6e session notes (2026-06-12)

### Driver ports (minic/dos/newlibc/)
- bm_display.c + bm_font_data.c (8 KB font table, verbatim from newlibc):
  font MUST load to 0000:0C00 before the CRTC shows anything (no char ROM);
  VRAM words at F000:0000 are (attr<<8)|glyph_ptr with glyph_ptr=char+0x60;
  VIA brightness (E800:40=0x54, E800:42=0xFF) or the screen stays dark;
  the original's CRTC push-es/mov-es asm is exactly a minic volatile-far
  store.  Self-check hooks (bm_display_read_cell/read_crtc/screen_word)
  let display_bm verify everything over serial — no host screen dump.
- bm_keyboard.c: BIOS state machine (SHIFTING→STOP_LOW→STOP_HIGH),
  MAME-validated ASCII map, S88-Return compat, Shift/RPT/Alt — all kept.
  Interrupt-driven ONLY: the ISR is the sole ring-buffer producer and the
  consumer pops via one-byte indexes, so the original's pushf/cli
  flags-save (the last inline-asm holdout) is structurally unnecessary.
- bm_serial.c: 7201 channel B mirrors the channel-A console bring-up
  (VIA2 PA bit1 internal clock, 8253 counter 1 — counter 1 is Serial B —
  mode 2 divisor 8, reset+WR4/WR3/WR5) plus WR1=0x18.  ISR drains all
  pending bytes before the specific EOI (0x61) — edge-triggered PIC +
  level-holding 7201 INT means a left-behind byte kills all future IRQs.
- bm_interrupts.c: install() → exported bm_install_isr(int_num, fn)
  (model-agnostic qbe_get_cs idiom) so driver TUs install their own ISRs.

### Harness (tools/)
- run-victor-baremetal.sh: V9K_KEYPOST + V9K_KEYPOST_DELAY (MAME
  natkeyboard:post — the newlibc-validated pattern; works headless),
  V9K_SERIAL_IN + V9K_SERIAL_IN_DELAY (second null_modem on rs232b,
  byte file attached mid-run via Lua manager.machine.images img:load —
  null_modem streams an attached file as RX data, so attaching at boot
  would lose everything during program init).  With rs232b present the
  bitbanger media options renumber: capture binds to -bitbanger1.
- build-newlibc-baremetal.sh: per-header driver TU selection
  (bm_display/bm_keyboard/bm_serial) + dedup (keyboard and serial both
  pull bm_interrupts+bm_pic).
- tools/test-newlibc.sh: the standing battery — hello_bm, timer_bm,
  display_bm, keyboard_bm, serial_bm, each golden-diffed
  (minic/dos/tests/<name>.golden.txt), skip-77 when MAME/newlibc absent.

### Victor/MAME facts worth keeping
- 6845 CRTC registers are write-only except R14/R15 (cursor) — readback
  tests must not assert config registers.
- MAME victor9k drives IR6 from the VIA2 IRQ line (victor9k.cpp:561), so
  the keyboard handshake is fully interrupt-driven — every SR/CB1 step
  re-raises KBINT.
- natkeyboard:post() is deterministic under -nothrottle and types
  shifted chars itself; keyboard_bm's "v9k" arrives byte-exact.
- 7201 INT stays asserted while an RX byte is pending; with the 8259A in
  edge mode the ISR MUST drain to RR0-empty or IR1 never fires again.

### Open tracks (new + carried)
- newlibc step 4c: port more phase-3 tests bare-metal (interrupt_test,
  pic_test, crtc_test, memory_test look near-free); cooked console
  (bm_display + bm_keyboard behind a getc/putc pair) → stdio toward
  retiring libstub; block/SASI driver port (MAME -scsi:0 harddisk) for
  bare-metal FAT; serial TX ISR when a consumer appears.
- run-dos-exe.sh stdin redirect (unlocks 3 more DOS-hosted newlibc tests).
- newlibc-under-far-models stdio story — when a far consumer appears.
- Carried: far static-DATA-ptr reloc (§1g); param/static-local shadowing a
  global; huge `_qbe_huge_add` ≥0x8000 (§4i); `jmp_buf bufs[6]` (§4v,
  unreduced); minic static-init FLOAT const-expr folding; small
  setjmp/longjmp (newlibc may want it); multi-decl items after the first
  skip block_scope_decl; Kw spill-slot sharing.

---

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

Older session headers (§6c and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
