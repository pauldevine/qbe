# C-Kermit triage tools

C-Kermit (`~/projects/ckermit`, the Victor 9000 port, 24 modules) is built
with THIS toolchain as a large consumer.  The authoritative build stays the
Open Watcom one in the ckermit repo; nothing here modifies that tree.

Work files (inputs + outputs) live under `build/ckermit-triage/` (override
with `CKW=`); these tools are tracked.  Paths/env come from `common.sh`:
`CK` (C-Kermit tree), `MODEL` (default large), `MINICFLAGS` (e.g. `-G`),
`OUTSUFFIX` (output dir `out-$MODEL$OUTSUFFIX`).

| tool | what |
|---|---|
| `pp.sh [mod..]` | regenerate `pp/*.i` with Watcom `wcc -p` + the exact victorow.mak flags, in the `ia16-ubuntu-2` container (starts it if needed) |
| `sweep.sh [model]` | pp → strip Watcom-isms → `fixups.pl` → `splitdecl.pl` → minic → qbe → asm_to_omf → nasm; one PASS/stage-error line per module |
| `fixups.pl` | text workarounds, one rule per minic gap (G#) or Watcom-ism (W#).  **Progress = deleting rules** (sweep must stay 24/24) |
| `splitdecl.pl` | G8 workaround: split file-scope `T a = 1, b = 2;` |
| `one.sh mod` / `it.sh mod` | show a module's minic error in context / re-sweep one module then show it |
| `m.sh 'c line'...` | does this snippet parse? |
| `locate.py mod` | bisect which statement triggers an end-of-function (emit-time) error; pairs with minic's "(in statement ending near line N)" suffix |
| `omfsize.py watcomdir mod..` | code/data bytes, ours vs Watcom objects |

Reference: `FINDINGS.md` (the §9f gap list), `undefined-watcom.txt` (the 84
external symbols of the Watcom-header build — input to the libc fill).

Typical loop: `tools/ckermit/pp.sh` (once), then
`MINICFLAGS=-G tools/ckermit/sweep.sh large`.

Note: QBE's i8086 emitter names some local labels after heap addresses
(`.L_ceql_ne_0x…`), so `.asm`/`.obj` differ run-to-run under ASLR; compare
with `sed -E 's/_0x[0-9a-f]+/_ADDR/g'` on the `.omf.asm`.
