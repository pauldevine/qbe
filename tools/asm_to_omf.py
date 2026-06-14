#!/usr/bin/env python3
"""asm_to_omf.py — convert qbe i8086 .asm output to OMF-friendly NASM source.

Reads a .asm file produced by `qbe -t i8086 -m medium`, splits sections
(.text/.data/.bss), applies the syntax transforms previously handled by
the sed/perl pipeline in build-stevie.sh, and emits a single NASM source
ready for `nasm -f obj -o <base>.obj`.

The output is a single file with proper segment directives, a DGROUP
declaration, and auto-generated `extern`/`global` lines.

Usage:
    asm_to_omf.py [--model=<m>] <basename> <input.asm> <output.nasm.asm>

Near-code models (tiny/small/compact) emit all code into a shared `_TEXT`
segment so the linker can coalesce multiple modules into a single CS frame.
Far-code models (medium/large/huge) keep the per-module `<BASE>_TEXT`
name so each module lands in its own physical 64KB code segment.
"""
import os
import re
import sys


# Local-label prefix scheme (mirrors the perl pass in build-stevie.sh):
# `lN` / `lN_lM` jump labels and `_glo<N>` data labels are renamed to
# `<base>_lN`, `<base>_lN_lM`, `<base>_glo<N>` so that they are unique
# across translation units.  Function names and runtime symbols are left
# alone.
def apply_local_prefix(line, prefix):
    line = re.sub(r'^(l\d+(?:_l\d+)?):', prefix + r'\1:', line)
    line = re.sub(r'^(\s*j[a-z]+\s+)(l\d+(?:_l\d+)?)\b',
                  lambda m: m.group(1) + prefix + m.group(2), line)
    line = re.sub(
        r'^(\s*jnz\s+[^,]+,\s*)(l\d+(?:_l\d+)?)(\s*,\s*)(l\d+(?:_l\d+)?)\b',
        lambda m: m.group(1) + prefix + m.group(2) + m.group(3) + prefix + m.group(4),
        line,
    )
    line = re.sub(r'^_?glo(\d+):', prefix + r'glo\1:', line)
    line = re.sub(r'\b_?glo(\d+)\b', prefix + r'glo\1', line)
    return line


def transform_line(line, prefix, far_data=False, split_sym_long=False):
    """Apply the same syntactic rewrites the sed/perl pipeline did,
    but leave section markers in place — the caller routes lines into
    per-section buckets.  May return a multi-LINE string (joined with
    '\\n'); the caller splits before bucketing.

    split_sym_long: a relocatable `.long _sym[+N]` initializer is a 4-byte
    far pointer (seg:off) and must carry BOTH offset and segment.  Emitting
    it as `dd _sym` makes nasm produce a 32-bit *offset* fixup (OMF loc 9) —
    the segment word is left 0, so the pointer is wrong, and an indirect far
    CALL through it (e.g. a function-pointer field in a static device-ops
    table) wild-jumps.  Split into `dw _sym+N` (loc-1 offset) + `dw seg _sym`
    (loc-2 segment + runtime reloc), both of which omf_link already resolves.
    True under far-DATA models (every data pointer is far) AND under medium
    (near-DATA but FAR-CODE: minic emits `.long _sym` only for 4-byte far
    CODE pointers — function pointers in static initializers — or explicit
    __far data pointers; both are seg:off)."""
    # 32-bit op stub note
    line = re.sub(r'; TODO: 32-bit op \d+',
                  '; XXX 32-bit op stub - codegen incomplete', line)
    # `.ascii "..."` → NASM backtick string
    m = re.match(r'^\s*\.ascii\s+"(.*)"\s*$', line)
    if m:
        s = m.group(1).replace('`', r'\`')
        return 'db `' + s + '`'

    line = apply_local_prefix(line, prefix)

    # MASM operand qualifiers → NASM
    line = re.sub(r'word ptr (\w\w:)\[', r'word \1[', line)
    line = re.sub(r'byte ptr (\w\w:)\[', r'byte \1[', line)
    line = re.sub(r'dword ptr (\w\w:)\[', r'dword \1[', line)
    line = line.replace('word ptr [', 'word [')
    line = line.replace('byte ptr [', 'byte [')
    line = line.replace('dword ptr [', 'dword [')

    # /* comment */ → ; comment
    line = re.sub(r'/\* (.*) \*/', r'; \1', line)

    # `name proc near|far` → bare label (qbe should no longer emit these,
    # but tolerate stale output).
    line = re.sub(r'^([A-Za-z_][\w]*) proc (near|far)$', r'\1:', line)
    line = re.sub(r'^([A-Za-z_][\w]*) endp$', r'; \1 endp', line)

    # GAS data directives → NASM
    line = re.sub(r'^\s*\.byte (.*)$',  r'db \1', line)
    line = re.sub(r'^\s*\.short (.*)$', r'dw \1', line)
    # A relocatable `.long _sym[+N]` is a 4-byte far pointer — split into
    # offset + segment words (see docstring).  Numeric `.long` (a real
    # 32-bit constant) falls through to `dd`.
    if split_sym_long:
        m = re.match(r'^\s*\.long\s+(_?[A-Za-z][\w]*)\s*(\+\s*\d+)?\s*$', line)
        if m:
            sym = m.group(1)
            off = (m.group(2) or '').replace(' ', '')
            line = '\tdw %s%s\n\tdw seg %s' % (sym, off, sym)
            return line
    line = re.sub(r'^\s*\.long (.*)$',  r'dd \1', line)
    line = re.sub(r'^\s*\.int (.*)$',   r'dw \1', line)
    line = re.sub(r'^\s*\.word (.*)$',  r'dw \1', line)
    line = re.sub(r'^\s*\.quad (.*)$',  r'dq \1', line)
    line = re.sub(r'^\s*\.zero (.*)$',  r'times \1 db 0', line)
    line = re.sub(r'^\s*\.fill (\d+),1,0$', r'times \1 db 0', line)
    line = re.sub(r'^\s*\.fill (\d+),2,0$', r'times \1 dw 0', line)
    line = re.sub(r'^\s*\.fill (\d+),4,0$', r'times \1 dd 0', line)
    line = re.sub(r'^\s*\.fill (\d+)$',     r'times \1 db 0', line)

    # `test es,es` → `test ax,ax` (codegen artifact from segment regs)
    line = re.sub(r'^(\s*test\s+)(es|ds|cs|ss),\s*\2\b',
                  r'\1ax, ax ; XXX was test \2,\2', line)

    # `mov [bp+N], <symbol>` (no size) — qbe's Ocopy/spill emits a slot
    # store without a size qualifier, which `nasm -f bin` accepts but
    # `nasm -f obj` rejects (relocation size ambiguous).  Promote to
    # `mov word [bp+N], <symbol>`.  Same fix for `mov [reg], symbol`.
    line = re.sub(r'^(\s*)mov (\[bp[+\-]\d+\]),\s*(_?[A-Za-z][\w]*)$',
                  r'\1mov word \2, \3', line)
    line = re.sub(r'^(\s*)mov (\[bp[+\-]\d+\]),\s*(\d+)$',
                  r'\1mov word \2, \3', line)

    return line


# Symbols emitted as data labels by qbe that are local to the file but
# don't follow the `glo<N>` naming convention.  We treat any `_<name>:`
# that isn't preceded by `.globl _<name>` as a file-local definition,
# so this list is informational only.
RUNTIME_SYMS = set()  # populated below if/when needed


# Skip-list of GAS directives we drop entirely.  Section markers
# (.text/.data/.bss) and `.section "_HUGE_..."` overrides are handled
# separately to switch buckets — they are NOT in this list.
# NOTE: `.balign`/`.p2align` are NOT dropped here — they are translated to
# NASM `align` in the data/bss buckets (see the line loop).  Dropping them
# would only word(2)-align data, but minic aligns aggregates to >=4 because
# MicroPython (and any tagged-pointer scheme) stores tag bits in the low 2
# bits of an object pointer and requires every aggregate object to be at
# least 4-byte aligned.  Honoring the directive keeps those objects aligned
# in the final OMF segment.
DROP_PREFIXES = (
    '.local', '.type', '.size', '.file',
    '.ident', '.string', '.model', '.code',
)

# Largest payload we put inside a single OMF SEGDEF.  NASM emits a valid
# big-form SEGDEF (length=0, big_bit=1, interpreted as 64K) when the
# segment is exactly 65536 bytes, but silently mis-encodes anything
# larger.  Keep huge chunks at a safe paragraph-aligned 65520 bytes so
# we never trip that path and so two consecutive chunks always sit at
# paragraph (P) and (P + chunk_paras) with no inter-chunk padding.
HUGE_CHUNK_BYTES = 65520
assert HUGE_CHUNK_BYTES % 16 == 0


def is_label_def(line):
    m = re.match(r'^([A-Za-z_.][\w.]*):\s*(?:;.*)?$', line)
    return m.group(1) if m else None


def collect_referenced_syms(line):
    """Return set of `_xxx`-style symbol references in the given line.
    Skips operand qualifiers (word/byte/dword), instruction mnemonics,
    and segment registers."""
    syms = set()
    # Strip line comment so we don't pick up `; XXX was test es,es`
    code = re.split(r';', line, maxsplit=1)[0]
    # One-or-more leading underscores: a C `__builtin_clz` reference is
    # mangled by minic to `___builtin_clz`, and the word boundary only
    # sits before the FIRST underscore — a `_[A-Za-z]` pattern there sees
    # `__` and never matches, so such symbols silently miss the extern set.
    for m in re.finditer(r'\b(_+[A-Za-z][\w]*)\b', code):
        syms.add(m.group(1))
    return syms


# Code-segment split budget.  A real-mode USE16 segment caps at 64KB.  We
# split a TU's text at function boundaries so each CODE segment stays under
# this estimated-byte budget.  EST_INSTR_BYTES over-estimates the average
# qbe i8086 instruction (~2.1 bytes measured) by ~2x, so a segment estimated
# at TEXT_SEG_BUDGET is really well under 64KB even for denser-than-average
# code; over-splitting is harmless (just a few more segments).  omf_link
# hard-rejects any USE16 segment that still slips past 64KB.
TEXT_SEG_BUDGET = int(os.environ.get('QBE_TEXT_SEG_BUDGET', '56000'))
EST_INSTR_BYTES = 4


def est_line_bytes(line):
    """Rough upper-ish estimate of a qbe asm line's encoded size."""
    s = line.strip()
    if (not s or s.startswith(';') or s.startswith('/*')
            or s.startswith('.') or s.endswith(':')):
        return 0
    return EST_INSTR_BYTES


def emit_text_segments(out, code_seg, text_lines, func_bounds):
    """Emit the TU's `.text` as one CODE segment, or — if its estimated size
    exceeds TEXT_SEG_BUDGET — several, split at function boundaries.  The
    first segment keeps the canonical `<BASE>_TEXT` name (single-segment TUs
    are byte-identical to before); overflow functions go to `<BASE>_TEXT1`,
    `<BASE>_TEXT2`, ...  Far calls resolve cross-segment via symbol fixups,
    so the split is transparent; each function stays wholly in one segment so
    its intra-function near jumps remain segment-local."""
    n = len(text_lines)
    # Function start indices, normalised: start at 0, strictly increasing,
    # within range.  qbe emits a `.text` before every function.
    starts = sorted(set(b for b in func_bounds if 0 <= b < n))
    if not starts or starts[0] != 0:
        starts = [0] + starts
    starts.append(n)

    # Pack consecutive functions into segments under the byte budget.
    segs = []        # list of (lo, hi) line ranges
    seg_lo = 0
    seg_bytes = 0
    for k in range(len(starts) - 1):
        lo, hi = starts[k], starts[k + 1]
        fb = sum(est_line_bytes(l) for l in text_lines[lo:hi])
        if hi == lo:
            continue
        # Start a new segment if this function would push us over budget
        # (but never emit an empty segment — always keep at least one fn).
        if seg_bytes > 0 and seg_bytes + fb > TEXT_SEG_BUDGET:
            segs.append((seg_lo, lo))
            seg_lo = lo
            seg_bytes = 0
        seg_bytes += fb
    segs.append((seg_lo, n))

    for idx, (lo, hi) in enumerate(segs):
        nm = code_seg if idx == 0 else '%s%d' % (code_seg, idx)
        out.append('segment %s class=CODE align=2 use16' % nm)
        out.extend(text_lines[lo:hi])
        out.append('')


def emit_huge_section(out, sec_name, lines):
    """Emit one `.section "_HUGE_<sym>"` bucket as one or more NASM
    `segment` blocks of at most HUGE_CHUNK_BYTES.  Splits the trailing
    `times N db 0` directive across chunks; everything before the fill
    (label, comments) is kept in the first chunk."""
    # Find the `times N db 0` directive in the bucket.  qbe emits at
    # most one per data global because `{ z N }` collapses to a single
    # .fill which our sed pipeline rewrites to `times N db 0`.
    fill_idx = None
    fill_count = 0
    for i, ln in enumerate(lines):
        m = re.match(r'^\s*times\s+(\d+)\s+db\s+0\s*$', ln)
        if m:
            fill_idx = i
            fill_count = int(m.group(1))
            break

    if fill_idx is None or fill_count <= HUGE_CHUNK_BYTES:
        # Small enough to live in one segment; emit verbatim under the
        # `_HUGE_<sym>_0` name so the linker's huge-segment matcher
        # (any segment whose name starts with `_HUGE_`) catches it
        # uniformly.
        out.append('segment %s_0 class=HUGE align=16 use16' % sec_name)
        out.extend(lines)
        out.append('')
        return

    # Split: first chunk holds the label + first HUGE_CHUNK_BYTES of
    # fill.  Subsequent chunks hold the rest, named `_0`, `_1`, ... so
    # the linker can place them at adjacent paragraphs.
    chunk0 = lines[:fill_idx] + ['\ttimes %d db 0' % HUGE_CHUNK_BYTES]
    out.append('segment %s_0 class=HUGE align=16 use16' % sec_name)
    out.extend(chunk0)
    out.append('')

    remaining = fill_count - HUGE_CHUNK_BYTES
    idx = 1
    while remaining > 0:
        take = min(remaining, HUGE_CHUNK_BYTES)
        out.append('segment %s_%d class=HUGE align=16 use16'
                   % (sec_name, idx))
        out.append('\ttimes %d db 0' % take)
        out.append('')
        remaining -= take
        idx += 1


def main():
    args = sys.argv[1:]
    model = 'medium'
    far_static_data = False
    while args and args[0].startswith('--'):
        a = args.pop(0)
        if a.startswith('--model='):
            model = a[len('--model='):]
        elif a == '--far-static-data':
            # Opt-in: route this module's statics into its own far
            # `<BASE>_DATA`/`<BASE>_BSS` segment OUTSIDE DGROUP (so total
            # static data can exceed 64KB).  Requires a far-data model
            # (every global is then addressed via `seg sym`).  OFF by
            # default: direct global access still emits near loads/stores
            # in minic, which only resolve correctly when globals live in
            # DGROUP — making this the default awaits the far-global-access
            # work (see NEXT_SESSION.md).
            far_static_data = True
        else:
            print('asm_to_omf: unknown option: ' + a, file=sys.stderr)
            sys.exit(2)
    if len(args) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    basename, in_path, out_path = args[0], args[1], args[2]
    prefix = basename + '_'
    with open(in_path) as f:
        raw_lines = f.readlines()

    # Far-data models address every global via `seg sym`, so a relocatable
    # data pointer is a 4-byte far pointer (offset + segment).  Needed in the
    # line loop below for transform_line's `.long _sym` → dw/dw-seg split.
    far_data = far_static_data and model in ('compact', 'large', 'huge')
    # Whether a relocatable `.long _sym` initializer must be split into
    # offset+segment words.  True for ALL far-DATA models (compact/large/
    # huge): a static data pointer is a 4-byte far pointer that needs its
    # segment REGARDLESS of where the statics live (§1g) — with
    # --far-static-data the target sits in <BASE>_DATA, without it in
    # DGROUP, but either way `dd _sym` leaves the segment word 0 → a
    # wrong-segment far deref.  (Note `far_data` above is gated on
    # --far-static-data because it ALSO routes the section/class names; the
    # `.long _sym` split is independent of that routing and applies whenever
    # data pointers are far.)  Also true for medium: medium is near-DATA but
    # FAR-CODE (NEAR_CODE() is tiny/small only), so its only `.long _sym`
    # initializers are 4-byte far CODE pointers (function pointers in static
    # tables) — those need seg:off or an indirect far CALL through them
    # jumps to segment 0.
    split_sym_long = model in ('compact', 'large', 'huge', 'medium')

    sections = {'text': [], 'data': [], 'bss': []}
    # `huge_sections` is OrderedDict-like: maps `_HUGE_<sym>` → list of
    # lines emitted by qbe between the `.section "_HUGE_<sym>"` marker
    # and the next section/text/data/bss marker.  Each huge section
    # gets its own NASM `segment` (split into 64KB-1para chunks for
    # arrays > 65520 bytes) and is laid out outside DGROUP by the
    # linker so it can hold > 64K data.
    huge_sections = {}
    # Function-boundary indices into sections['text'].  qbe emits a `.text`
    # directive before EVERY function, so each `.text` marks where the next
    # function's lines begin.  Used to split an oversized TU's code across
    # multiple <=64KB CODE segments at function boundaries (a real-mode
    # segment caps at 64KB; far-data codegen can push one big TU's text past
    # it — MicroPython's py/compile.c is 78KB under compact).
    text_func_bounds = []
    current = 'text'
    current_huge = None   # active `_HUGE_<sym>` key when current == 'huge'
    publics = []          # symbols declared via `.globl`
    defined = set()       # all labels actually defined in this file
    defined_text = set()  # labels defined in a .text (code) section
    referenced = set()    # all `_xxx` references seen in operands

    # Match `.section "_HUGE_foo"` or `.section _HUGE_foo` (qbe quotes
    # the string but accept either to be tolerant of pipeline changes).
    huge_re = re.compile(r'^\.section\s+"?(_HUGE_[A-Za-z_][\w]*)"?\s*$')

    for raw in raw_lines:
        line = raw.rstrip('\n')
        s = line.strip()

        # Section markers
        if s == '.text':
            text_func_bounds.append(len(sections['text']))
            current = 'text'; current_huge = None; continue
        if s == '.data':
            current = 'data'; current_huge = None; continue
        if s == '.bss':
            current = 'bss'; current_huge = None; continue
        m = huge_re.match(s)
        if m:
            current_huge = m.group(1)
            huge_sections.setdefault(current_huge, [])
            current = 'huge'
            continue

        # Drop any other `.section` directive we don't recognize so
        # foreign linkage hints from qbe don't leak into the OMF
        # output.  Same goes for the other GAS-only prefixes.
        if s.startswith('.section'):
            continue

        # Alignment directives.  Honor them in the data/bss/huge buckets so
        # aggregate objects keep the >=4-byte alignment minic gave them
        # (tagged-pointer schemes like MicroPython's mp_obj_t store tag bits
        # in a pointer's low 2 bits, so a const object that lands at a
        # 2-mod-4 offset is misclassified).  Code (.text) alignment is
        # perf-only here, so drop it to avoid padding/segment-align fuss.
        m = re.match(r'\.(?:balign\s+(\d+)|p2align\s+(\d+))', s)
        if m:
            if m.group(1) is not None:
                n = int(m.group(1))
            else:
                n = 1 << int(m.group(2))
            if current in ('data', 'bss'):
                sections[current].append('align %d' % n)
            elif current == 'huge':
                huge_sections[current_huge].append('align %d' % n)
            continue

        # GAS-only directives we drop
        if any(s.startswith(p) for p in DROP_PREFIXES):
            continue

        # `.globl _foo` — record as public, drop the line
        m = re.match(r'\.globl\s+(\S+)', s)
        if m:
            publics.append(m.group(1))
            continue

        # Empty line — skip
        if s == '':
            continue

        # Apply transforms (after section/globl handling).  transform_line
        # may return a multi-line string (far-pointer `.long` split into
        # dw offset + dw seg); bucket each resulting line independently.
        for line in transform_line(line, prefix, far_data,
                                   split_sym_long).split('\n'):
            # Track defined labels & referenced symbols
            lbl = is_label_def(line.strip())
            if lbl:
                defined.add(lbl)
                if current == 'text':
                    defined_text.add(lbl)
            referenced |= collect_referenced_syms(line)

            if current == 'huge':
                huge_sections[current_huge].append(line)
            else:
                sections[current].append(line)

    # Publics come exclusively from `.globl` (§6b).  minic emits
    # `export data` for external-linkage file-scope data and a plain
    # `data` for C `static` data (including mangled function-local
    # statics), exactly mirroring the function story (§1q: `export
    # function` vs `function`), so qbe's `.globl` is authoritative for
    # BOTH code and data labels.  The old behavior — auto-promoting
    # every `_xxx:` data label to a public — exported `static` data and
    # collided as duplicate publics when two TUs reused a static name
    # (newlibc: dirent.c and vfs.c both define `static ... dir_table[]`).
    public_set = set(publics)

    # Compute externs: anything referenced but not defined locally.
    # Filter out segment-name-like tokens and false positives.
    externs = sorted(
        sym for sym in referenced
        if sym not in defined and sym not in public_set
    )

    # Build output
    out = []
    out.append('; Auto-generated from qbe i8086 .asm by tools/asm_to_omf.py')
    out.append('; Module: ' + basename)
    out.append('bits 16')
    out.append('cpu 8086')
    out.append('')
    # Far-data models (compact/large/huge): every global is addressed
    # far (qbe emits `mov ax, seg _sym; mov es, ax; es:[bx]` — it never
    # assumes DGROUP), so a module's static data can live in its OWN far
    # segment OUTSIDE DGROUP.  This is what lets total static data exceed
    # 64KB: each module's `<BASE>_DATA` segment has its own selector and
    # `seg _sym` resolves to wherever the symbol's segment landed (the
    # same mechanism the `_HUGE_<sym>` arrays already rely on).  DGROUP
    # then holds only the hand-asm crt0/libstub near data + the stack.
    # Near-data models (tiny/small/medium) keep the classic single
    # DGROUP-resident `_DATA`/`_BSS`.  (far_data computed above for the
    # line loop's far-pointer `.long` split.)
    data_seg  = (prefix + 'DATA') if far_data else '_DATA'
    bss_seg   = (prefix + 'BSS')  if far_data else '_BSS'
    data_cls  = 'FAR_DATA' if far_data else 'DATA'
    bss_cls   = 'FAR_BSS'  if far_data else 'BSS'
    if not far_data:
        out.append('group DGROUP _DATA _BSS')
        out.append('')
    for e in externs:
        out.append('extern ' + e)
    if externs:
        out.append('')
    for p in publics:
        out.append('global ' + p)
    if publics:
        out.append('')

    # Emit all three segments even if empty so DGROUP's references are
    # always resolved.  NASM/OMF tolerate empty segments.
    # Medium model: give each module its own _TEXT segment so the linker
    # places them in separate physical 64KB code segments.  All _DATA /
    # _BSS go into shared DGROUP segments (so DS addresses everything).
    # Near-CODE models (tiny/small — NOT compact, which uses the far-call
    # ABI and may exceed 64KB total code): every module emits into the
    # shared `_TEXT` segment so omf_link coalesces all code into ONE
    # paragraph frame.  Near calls and 2-byte code pointers only work when
    # caller, callee, and the runtime CS share that single frame.  No
    # budget splitting either — total code must fit 64KB anyway.
    if model in ('tiny', 'small'):
        code_seg = '_TEXT'
        out.append('segment %s class=CODE align=2 use16' % code_seg)
        out.extend(sections['text'])
        out.append('')
    else:
        code_seg = basename.upper() + '_TEXT'
        emit_text_segments(out, code_seg, sections['text'], text_func_bounds)

    # align=16 (paragraph): the linker places each segment at a paragraph
    # base, so a within-segment NASM `align N` (N<=16) yields an N-aligned
    # effective offset.  Declaring align=16 also lets NASM accept the
    # `align 4`/`align 16` directives emitted above without complaint.
    out.append('segment %s class=%s align=16 use16' % (data_seg, data_cls))
    out.extend(sections['data'])
    out.append('')

    out.append('segment %s class=%s align=16 use16' % (bss_seg, bss_cls))
    out.extend(sections['bss'])
    out.append('')

    # Huge data segments: one per `.section "_HUGE_<sym>"` marker.  Each
    # segment is class=HUGE so the linker can recognise it and place it
    # outside DGROUP.  Arrays larger than HUGE_CHUNK_BYTES are split
    # across multiple paragraph-aligned chunks so we never blow OMF's
    # 16-bit segment-length field.  The label (e.g. `_arr:`) lives at
    # offset 0 of the first chunk; subsequent chunks are anonymous BSS
    # tail extending the array.
    for sec_name, lines in huge_sections.items():
        emit_huge_section(out, sec_name, lines)

    with open(out_path, 'w') as f:
        f.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
