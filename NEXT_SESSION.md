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

# Next session (§7h — continue Phase 6 / open compiler tracks.  §7g [2026-06-14, this session] closed the carried **huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i)** — the user picked it.  **The surprise: the libstub helper `_qbe_huge_add` was CORRECT all along; the bug was the CALLER.**  minic's Scale path (`prom()`, minic.y ~2209, the shared pointer-arithmetic index-scaling code) UNCONDITIONALLY `sext`s a sub-`long` index before the `=l mul <sz>` that scales it by the element size — `sext()` always emits `extsw` regardless of source signedness (its signedness-aware sibling is `widen_int_to_long()`, which picks `extuw`/`extsw`).  So an UNSIGNED `size_t` byte offset whose 16-bit value is ≥0x8000 (the canonical case: MicroPython `gc_alloc`'s `pool_start + start_block*BYTES_PER_BLOCK` on a heap >32 KB, where `start_block≥2048` → offset ≥32768) was sign-extended to a NEGATIVE 32-bit value, then handed to `_qbe_huge_add(ptr, offset)` which faithfully added it to the 20-bit linear address — landing BELOW the object.  **Why compact/large never saw this (and why §4i's fix didn't reach it):** under compact/large `far_ptr ± idx` routes through the dedicated offset-only `addfo`/`subfo` ops, which read ONLY arg1's low 16 bits, where `extsw` and `extuw` agree bit-for-bit — so the sign-extension is harmless there and §4i deliberately left the Scale path's `sext` untouched to keep MP byte-identical.  Under HUGE, objects can exceed 64 KB so a genuine segment carry is required: minic routes the SAME indexing through `huge_ptr_binop` → `_qbe_huge_add`, which uses the FULL 32-bit scaled value — so the sign now matters, and the gap that §4i flagged as "pre-existing, in the helper" was actually in the index typing one level up.  **The fix (one site, minic.y Scale path):** under `memmodel == MHuge` the non-`Con` index is widened with `widen_int_to_long(r)` (source-signedness: `extuw` for unsigned, `extsw` for signed) instead of the unconditional `sext(r)`; compact/large/near keep the uniform `sext` (the `else` branch), so the change is gated to huge and the MP-compact corpus is byte-identical BY CONSTRUCTION.  **Verified:** the unfixed huge build printed `direct=0` for b≥2048 + `FAIL`; the SSA showed `%t157 =l extsw %t156` (off = an unsigned `size_t` `loadw`) feeding `=l mul 1, …` then `$qbe_huge_add`.  Post-fix that line is `%t157 =l extuw %t156` (the signed `int i` blocks-index correctly STAYS `extsw`), and the huge build prints `direct=0x41+i` for every block + `ALL OK`, byte-exact vs the existing `gc_bigheap_probe.golden.txt` (the probe output is model-independent, `sizeof(int)==2` everywhere).  **Gated bug-loud** by adding the `:huge` model to the existing `gc_bigheap_probe` entry in `tools/test-dos.sh` (it was compact+large only; the probe header + the test-dos comment block were updated to record the huge gate and that the helper was correct).  **test-dos 313/313 → 314/314** (the new `huge runtime (gc_bigheap_probe)` entry `[ok]`, every prior entry unchanged; compact+large re-verified byte-exact vs golden).  Toolchain checks: `make check` green; grammar conflicts UNCHANGED (pure C inside `prom()`, no productions); **MP compact rebuilt to a body of EXACTLY 731,088 bytes — byte-identical to the documented golden** (image 751,664, header 20,576 + body 731,088), confirming compact codegen did not shift → no Victor run; and since this is a `minic.y` frontend change (NOT i8086/emit.c) the emit-bracket audit was NOT required.  The "huge `_qbe_huge_add` ≥0x8000 variable-index gap (§4i)" open track is now CLOSED.  Next: pick a carried track — far static-DATA-ptr reloc (§1g); Kw spill-slot sharing (frame-size lever, no consumer pain); the bounded aoa init/multi-declarator gap (§7e — brace-init `jmp_buf x[2]={…}` / multi-decl `jmp_buf a[2], b[2]` still ignore `g_td_arraydim`, no realistic consumer) — OR resume Phase-6 newlibc gating: `serial_loopback_test` remains the only tractable bm_testhost candidate but needs real new harness plumbing (channel-A polled RX + an rs232a TXD→RXD MAME loopback colliding with the rs232a `null_modem` capture → move the gate's serial capture to channel B, plus RX-timing determinism on the 5 MHz 8088); `interrupt_test` stays SKIPPED per §6v.  There is NO QBE backend bug currently open — the carried tracks are all minic/backend feature gaps or Phase-6 harness work.)

## §7g session notes (2026-06-14)

### The bug (carried open track 1, §4i — the huge half of the far-ptr unsigned-index family)
- Under `--model=huge`, `gc_bigheap_probe` printed `direct=0` for every block
  with byte offset ≥0x8000 (b≥2048) + `FAIL`, while `rt` (ptr−ptr DIFFERENCE,
  via `_qbe_huge_cmp`) and `vp` (ptr COMPARE) round-tripped correctly.  So the
  failure was isolated to the `pool[off]` indexing path → `huge_ptr_binop` →
  `_qbe_huge_add`.
- **The helper was NOT the bug.**  `_qbe_huge_add` (libstub.asm) correctly
  computes `linear = seg<<4 + off + offset`, renormalises, returns seg:off.
  The bug was the OFFSET it was handed.
- **Root cause (minic.y Scale path, `prom()` ~2209):** scaling a non-`Con`
  index for a far (`l`) pointer called `sext(r)` UNCONDITIONALLY before
  `=l mul <sz>`.  `sext()` always emits `extsw` (its doc even contrasts the
  signedness-aware `widen_int_to_long()`).  An unsigned `size_t` offset ≥0x8000
  → sign-extended NEGATIVE 32-bit → `_qbe_huge_add(ptr, <negative>)` →
  addresses below the object.  Smoking-gun SSA: `%t157 =l extsw %t156` where
  `%t156 =w loadw %off` and `off` is `size_t`.

### Why compact/large were immune (and §4i never reached this)
- compact/large lower `far_ptr ± idx` to the offset-only `addfo`/`subfo` ops,
  which read ONLY arg1's low 16 bits — and `extsw`/`extuw` agree on the low 16
  bits.  So §4i deliberately left the Scale `sext` alone to keep MP
  byte-identical; the sign only matters under huge, where the FULL 32-bit
  scaled value is added to the 20-bit linear address via `_qbe_huge_add`.

### The fix (one site, gated to huge)
- In the Scale path, when `memmodel == MHuge`, widen the index with
  `widen_int_to_long(r)` (source-signedness: `extuw` unsigned, `extsw` signed)
  instead of `sext(r)`.  compact/large/near keep the `else sext(r)` branch.
- Gated to huge ⇒ compact (MP's model) is the unchanged branch ⇒ MP-compact
  byte-identical by construction.

### Gate (bug-loud) + toolchain checks
- Added `:huge` to the existing `gc_bigheap_probe` entry in `tools/test-dos.sh`
  (was compact+large).  Bug-loud verified: pre-fix huge → `direct=0`/`FAIL`;
  post-fix → `direct=0x41+i` + `ALL OK`, byte-exact vs the (model-independent)
  golden on huge, AND compact+large re-verified byte-exact.
- **test-dos 313 → 314.**  `make check` green.  Grammar conflicts UNCHANGED
  (pure C in `prom()`, no productions).
- minic.y frontend (NOT emit.c) → NO emit audit.  MP compact body EXACTLY
  **731,088 bytes**, byte-identical → codegen unchanged, NO Victor run.

### ⇒ Next session (§7h): carried tracks (no QBE bug currently open)
- far static-DATA-ptr reloc (§1g).
- Kw spill-slot sharing (frame-size lever, no consumer pain).
- Bounded aoa gap (§7e): brace-init / multi-declarator array-of-array-typedef
  still ignore `g_td_arraydim`; no realistic consumer.
- Phase-6 newlibc `serial_loopback_test` (needs NEW harness plumbing —
  channel-A polled RX + rs232a TXD→RXD loopback, move gate capture to channel
  B, RX-timing determinism); `interrupt_test` stays SKIPPED (§6v).

---

Older session headers (§7g and everything before) are archived verbatim in [SESSION_LOG.md](./SESSION_LOG.md).
