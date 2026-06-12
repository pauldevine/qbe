#!/bin/bash
# test_omf_link.sh — validation tests for tools/omf_link.py
#
# Tests:
#   1. Two-file far-call test: assemble test_a.asm + test_b.asm and link.
#      Verify MZ header decodes correctly and the relocation table looks
#      sensible.
#   2. Stevie smoke test: link the 24 build/stevie-orig/*.obj files
#      together with stub publics for runtime symbols. Should not crash;
#      we don't try to run the result.
#
# Usage: tools/test_omf_link.sh

set -e
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/build/omf_link_test"
mkdir -p "$TMP"

LINK="$ROOT/tools/omf_link.py"
NASM="${NASM:-nasm}"

# ---------------- Test 1: two-file far-call test ----------------

cat > "$TMP/test_a.asm" <<'EOF'
        bits 16
        cpu 8086
        group DGROUP _DATA _BSS
        extern _foo
        global _start

        segment TEST_A_TEXT class=CODE align=2 use16
_start:
        call far _foo
        mov  ah, 0x4C
        int  0x21

        segment _DATA class=DATA align=2 use16
        segment _BSS  class=BSS  align=2 use16
EOF

cat > "$TMP/test_b.asm" <<'EOF'
        bits 16
        cpu 8086
        group DGROUP _DATA _BSS
        global _foo

        segment TEST_B_TEXT class=CODE align=2 use16
_foo:
        mov ax, 0x1234
        retf

        segment _DATA class=DATA align=2 use16
        segment _BSS  class=BSS  align=2 use16
EOF

echo "[test1] assembling..."
"$NASM" -f obj -o "$TMP/test_a.obj" "$TMP/test_a.asm"
"$NASM" -f obj -o "$TMP/test_b.obj" "$TMP/test_b.asm"

echo "[test1] linking..."
python3 "$LINK" -o "$TMP/test.exe" --map "$TMP/test.map" --entry _start \
                 "$TMP/test_a.obj" "$TMP/test_b.obj"

echo "[test1] decoding MZ header..."
python3 - "$TMP/test.exe" <<'PYEOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
sig, blast, pages, nreloc, hdrpara, minalloc, maxalloc, ss, sp, csum, ip, cs, reloc_off, ovl = \
    struct.unpack_from('<2sHHHHHHHHHHHHH', data, 0)
print('  sig=', sig)
print('  bytes_in_last_page=', blast, ' pages=', pages,
      '  total_image=', hdrpara*16 + (pages*512 - (512-blast if blast else 0) - hdrpara*16))
print('  nreloc=', nreloc, ' hdr_paragraphs=', hdrpara, ' (header=', hdrpara*16, 'bytes)')
print('  SS:SP = %04X:%04X   CS:IP = %04X:%04X' % (ss, sp, cs, ip))
print('  reloc_table_offset=', reloc_off)
print('  relocations:')
for i in range(nreloc):
    o, s = struct.unpack_from('<HH', data, reloc_off + 4*i)
    print('    %d) ofs=%04X seg=%04X (paragraph in image)' % (i, o, s))
assert sig == b'MZ', "bad signature"
assert nreloc >= 1, "expected at least one relocation for far-ptr"
print('[test1] OK')
PYEOF

# ---------------- Test 2: stevie smoke link ----------------

echo
echo "[test2] generating runtime stubs..."
STUBS="$TMP/runtime_stubs.asm"

# Collect unresolved externs from the 24 obj files
python3 - "$ROOT/build/stevie-orig" "$STUBS" <<'PYEOF'
import struct, sys, glob, os
objdir, out = sys.argv[1], sys.argv[2]
externs, publics = set(), set()
for fn in sorted(glob.glob(os.path.join(objdir, '*.obj'))):
    with open(fn, 'rb') as f:
        data = f.read()
    p = 0
    while p < len(data):
        rec = data[p]
        ln = struct.unpack_from('<H', data, p+1)[0]
        body = data[p+3:p+3+ln-1]
        if rec == 0x8C:
            i = 0
            while i < len(body):
                nl = body[i]; i += 1
                name = body[i:i+nl].decode('latin1'); i += nl
                ti = body[i]
                i += 2 if (ti & 0x80) else 1
                externs.add(name)
        elif rec in (0x90, 0x91):
            i = 0
            g = body[i]; i += 2 if (g & 0x80) else 1
            s = body[i]; i += 2 if (s & 0x80) else 1
            if g == 0 and s == 0:
                i += 2
            while i < len(body):
                nl = body[i]; i += 1
                name = body[i:i+nl].decode('latin1'); i += nl
                i += 2  # offset
                ti = body[i]
                i += 2 if (ti & 0x80) else 1
                publics.add(name)
        p += 3 + ln

unresolved = sorted(externs - publics)
print('[test2] %d unresolved externs to stub' % len(unresolved))

# Heuristic: names ending in lowercase are functions (far retf stubs);
# names starting with uppercase letter or that are clearly variables in
# stevie are data stubs.
data_syms = {
    '_Botchar','_Changed','_Columns','_Curschar','_Curscol','_Cursrow',
    '_Cursvcol','_Curswant','_Fileend','_Filemem','_Filename','_Filetop',
    '_Insbuff','_Insptr','_Insstart','_Nextscreen','_Ninsert','_Prenum',
    '_Realscreen','_Redobuff','_Rows','_State','_Topchar','_Version',
    '_chars','_curfile','_did_ai','_files','_got_int','_interactive',
    '_mincl','_mtype','_need_redraw','_numfiles','_operator','_params',
    '_reg_ic','_startop','_stderr',
}

with open(out, 'w') as f:
    f.write('; auto-generated runtime stubs for omf_link smoke test\n')
    f.write('bits 16\ncpu 8086\n')
    f.write('group DGROUP _DATA _BSS\n')
    for n in unresolved:
        f.write('global %s\n' % n)
    f.write('\nsegment STUBS_TEXT class=CODE align=2 use16\n')
    for n in unresolved:
        if n not in data_syms:
            f.write('%s:\n  retf\n' % n)
    f.write('\nsegment _DATA class=DATA align=2 use16\n')
    f.write('\nsegment _BSS class=BSS align=2 use16\n')
    for n in unresolved:
        if n in data_syms:
            f.write('%s: resb 64\n' % n)
PYEOF

# stevie wants a _start; provide one that calls _main and exits.
ENTRY="$TMP/stevie_entry.asm"
cat > "$ENTRY" <<'EOF'
        bits 16
        cpu 8086
        group DGROUP _DATA _BSS
        extern _main
        global _start

        segment ENTRY_TEXT class=CODE align=2 use16
_start:
        xor   ax, ax
        push  ax           ; argv[0] = NULL
        xor   ax, ax
        push  ax           ; argc = 0
        call  far _main
        add   sp, 4
        mov   ah, 0x4C
        int   0x21

        segment _DATA class=DATA align=2 use16
        segment _BSS  class=BSS  align=2 use16
EOF

echo "[test2] assembling stubs and entry..."
"$NASM" -f obj -o "$TMP/runtime_stubs.obj" "$STUBS"
"$NASM" -f obj -o "$TMP/stevie_entry.obj" "$ENTRY"

echo "[test2] linking 24 stevie objs + stubs + entry..."
# build/stevie-orig may carry its own crt0_exe.obj (a real _start) from the
# stevie build; exclude it — this smoke test supplies a synthetic entry.
STEVIE_OBJS=()
for o in "$ROOT"/build/stevie-orig/*.obj; do
        case "$o" in */crt0_exe.obj) continue ;; esac
        STEVIE_OBJS+=("$o")
done
python3 "$LINK" -o "$TMP/stevie.exe" --map "$TMP/stevie.map" \
        --entry _start --stack-size 4096 \
        "$TMP/stevie_entry.obj" \
        "${STEVIE_OBJS[@]}" \
        "$TMP/runtime_stubs.obj"

echo "[test2] decoding MZ header..."
python3 - "$TMP/stevie.exe" <<'PYEOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
sig = data[0:2]
nreloc = struct.unpack_from('<H', data, 6)[0]
hdrpara = struct.unpack_from('<H', data, 8)[0]
ss, sp = struct.unpack_from('<HH', data, 0x0E)
ip, cs = struct.unpack_from('<HH', data, 0x14)
print('  sig=', sig, ' nreloc=', nreloc, ' hdr=', hdrpara*16, ' bytes')
print('  CS:IP=%04X:%04X  SS:SP=%04X:%04X  total_file=%d' %
      (cs, ip, ss, sp, len(data)))
assert sig == b'MZ'
print('[test2] OK')
PYEOF

# ---------------- Test 3: raw-binary output (--raw-binary) ----------------
# Re-link test 1's objects as a flat bare-metal binary at 0x3000 and verify
# the structure: no MZ header, the synthesized register-setup stub at the
# image head, link-time-absolute selectors (no relocation table exists in
# this format), and the far call's selector resolving to the load-paragraph-
# based frame of TEST_B_TEXT.

echo
echo "[test3] linking raw binary @ 0x3000..."
python3 "$LINK" -o "$TMP/test.bin" --raw-binary --load-addr 0x3000 \
                 --entry _start "$TMP/test_a.obj" "$TMP/test_b.obj"

python3 - "$TMP/test.bin" <<'PYEOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
assert data[0:2] != b'MZ', "raw binary must not carry an MZ header"
# Stub: cli; mov ax,SS; mov ss,ax; mov sp,SP; mov ax,DS; mov ds,ax;
#       mov es,ax; jmp far CS:IP  — fixed offsets within the 32-byte head.
assert data[0] == 0xFA, "stub must start with cli"
assert data[1] == 0xB8 and data[4:6] == b'\x8e\xd0', "mov ax,SS / mov ss,ax"
assert data[6] == 0xBC, "mov sp,imm16"
assert data[9] == 0xB8 and data[12:14] == b'\x8e\xd8', "mov ax,DS / mov ds,ax"
assert data[14:16] == b'\x8e\xc0', "mov es,ax"
assert data[16] == 0xEA, "far jmp to entry"
ip, cs = struct.unpack_from('<HH', data, 17)
# Stub head is 32 bytes -> TEST_A_TEXT at image para 2 -> 0x300 + 2.
assert (cs, ip) == (0x302, 0), "entry must be 0302:0000, got %04X:%04X" % (cs, ip)
# TEST_A_TEXT content: call far _foo = 9A <off16> <seg16>.
assert data[32] == 0x9A, "call far at TEST_A_TEXT start"
foo_off, foo_seg = struct.unpack_from('<HH', data, 33)
# TEST_A_TEXT is 9 bytes (32..41); TEST_B_TEXT paragraph-aligns to byte 48
# -> image para 3 -> absolute selector 0x303, offset 0.
assert (foo_seg, foo_off) == (0x303, 0), \
    "far target must be 0303:0000, got %04X:%04X" % (foo_seg, foo_off)
print('  stub OK, entry %04X:%04X, far call -> %04X:%04X (absolute)'
      % (cs, ip, foo_seg, foo_off))
print('[test3] OK')
PYEOF

echo
echo "All tests passed."
echo "Output files in $TMP:"
ls -la "$TMP" | awk '{print "  " $0}'
