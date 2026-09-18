# C-Kermit triage tools

C-Kermit (`~/projects/ckermit`, the Victor 9000 port, 24 modules) is built
with THIS toolchain as a large consumer.  The authoritative build stays the
Open Watcom one in the ckermit repo; nothing here modifies that tree.

**The build is `tools/build-ckermit.sh`** (item 3): our headers
(`minic/dos/ckermit/include`), clang -E, the W rules of `fixups.pl`, minic -G,
qbe, asm_to_omf, nasm; objects in `build/ckermit/<model>/`.  The tools below
from `pp.sh` down are the §9f triage path through Watcom's headers, kept as
the reference that `hdrcheck.sh` compares against.

Triage work files (inputs + outputs) live under `build/ckermit-triage/`
(override with `CKW=`); these tools are tracked.  Paths/env come from `common.sh`:
`CK` (C-Kermit tree), `MODEL` (default large), `MINICFLAGS` (e.g. `-G`),
`OUTSUFFIX` (output dir `out-$MODEL$OUTSUFFIX`).

| tool | what |
|---|---|
| `../build-ckermit.sh [--model=] [-k] [mod..]` | THE build: one PASS/stage-error line per module |
| `cppflags.sh` | (sourced) the preprocessor flags for our headers; shared by build-ckermit.sh and hdrcheck.sh |
| `hdrcheck.sh` | do our headers select the same C-Kermit code as Watcom's?  macro definedness + `#if` values + per-module statement balance, vs wcc in the container; run after any header change |
| `undefined-ours.txt` | external symbols of the 24 objects built with our headers (97) -- input to item 4 |
| `pp.sh [mod..]` | regenerate `pp/*.i` with Watcom `wcc -p` + the exact victorow.mak flags, in the `ia16-ubuntu-2` container (starts it if needed) |
| `sweep.sh [model]` | Watcom `.i` → strip Watcom-isms → `fixups.pl` + the Watcom-header rewrites (W1/W2/W4) → minic → qbe → asm_to_omf → nasm |
| `fixups.pl` | text workarounds for Watcom language extensions in the C-Kermit sources (W3 `__interrupt`, W5 XI records, W6 object `__near`) -- items 5a/5c/5b remove them |
| `one.sh mod` / `it.sh mod` | show a module's minic error in context / re-sweep one module then show it |
| `m.sh 'c line'...` | does this snippet parse? |
| `locate.py mod` | bisect which statement triggers an end-of-function (emit-time) error; pairs with minic's "(in statement ending near line N)" suffix |
| `omfsize.py watcomdir mod..` | code/data bytes, ours vs Watcom objects |

Reference: `FINDINGS.md` (the §9f gap list), `undefined-watcom.txt` (the 84
external symbols of the Watcom-header build — input to the libc fill).

Typical loop: `tools/build-ckermit.sh`.  Reference (Watcom headers):
`tools/ckermit/pp.sh` (once), then `MINICFLAGS=-G tools/ckermit/sweep.sh large`.

Note: QBE's i8086 emitter names some local labels after heap addresses
(`.L_ceql_ne_0x…`), so `.asm`/`.obj` differ run-to-run under ASLR; compare
with `sed -E 's/_0x[0-9a-f]+/_ADDR/g'` on the `.omf.asm`.
