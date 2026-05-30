#!/usr/bin/env python3
"""omf_link.py — Microsoft OMF linker producing DOS MZ .EXE files.

Implements the medium memory model: multi-CS (each input module's code
segment kept distinct), single-DS via DGROUP (all _DATA/_BSS coalesced).

Reads .obj files produced by `nasm -f obj`, resolves cross-module
references, and writes a DOS MZ executable.

Usage:
    omf_link.py [-o OUT.exe] [--map MAP.txt] [--stack-size N]
                [--entry SYMBOL] OBJ1.obj OBJ2.obj ...

Defaults: -o a.out, --stack-size 4096, --entry _start.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# OMF record types
# ---------------------------------------------------------------------------

R_THEADR  = 0x80
R_LHEADR  = 0x82
R_COMENT  = 0x88
R_MODEND  = 0x8A
R_MODEND2 = 0x8B
R_EXTDEF  = 0x8C
R_PUBDEF  = 0x90
R_PUBDEF2 = 0x91
R_LINNUM  = 0x94
R_LINNUM2 = 0x95
R_LNAMES  = 0x96
R_SEGDEF  = 0x98
R_SEGDEF2 = 0x99
R_GRPDEF  = 0x9A
R_FIXUPP  = 0x9C
R_FIXUPP2 = 0x9D
R_LEDATA  = 0xA0
R_LEDATA2 = 0xA1
R_LIDATA  = 0xA2
R_LIDATA2 = 0xA3
R_COMDEF  = 0xB0
R_BAKPAT  = 0xB2
R_LEXTDEF = 0xB4
R_LPUBDEF = 0xB6
R_LCOMDEF = 0xB8
R_CEXTDEF = 0xBC
R_COMDAT  = 0xC2
R_COMDAT2 = 0xC3
R_LINSYM  = 0xC4
R_ALIAS   = 0xC6
R_NBKPAT  = 0xC8
R_LLNAMES = 0xCA

# 32-bit forms — refused
R_LEDAT32   = 0xB6  # collides with LPUBDEF; differentiated by context
R_LIDAT32   = 0xB7
R_FIXUPP32  = 0xC4  # collides with LINSYM; we don't see these from nasm -f obj


def die(msg: str) -> None:
    print('omf_link: error: ' + msg, file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Low-level OMF parsing helpers
# ---------------------------------------------------------------------------


def read_index(buf: bytes, p: int) -> Tuple[int, int]:
    """OMF variable-length index: 1 byte if <= 0x7F, else 2 bytes high bit set."""
    b = buf[p]
    if b & 0x80:
        return ((b & 0x7F) << 8) | buf[p + 1], p + 2
    return b, p + 1


def read_str(buf: bytes, p: int) -> Tuple[str, int]:
    n = buf[p]
    p += 1
    s = buf[p:p + n].decode('latin1')
    return s, p + n


# ---------------------------------------------------------------------------
# Per-module data structures
# ---------------------------------------------------------------------------


@dataclass
class Segment:
    name: str
    cls: str
    length: int            # actual length in bytes (16-bit; 64KB if Big)
    combine: int           # 0=private, 2=public, 5=stack, 6=common
    big: bool              # 64KB segment
    align: int             # 1,2,4,16,4096 — paragraph default
    data: bytearray = field(default_factory=bytearray)
    fixups: List['Fixup'] = field(default_factory=list)
    # Output mapping (filled in by linker layout pass):
    out_seg_idx: int = -1  # index into linker.out_segs
    out_offset: int = 0    # byte offset of this module's content within
                           # the (possibly combined) output segment


@dataclass
class Group:
    name: str
    seg_indices: List[int]  # 1-based segment indices within this module


@dataclass
class Public:
    name: str
    group_idx: int          # 1-based, 0 = none
    seg_idx: int            # 1-based, 0 = absolute (we don't support absolute)
    offset: int


@dataclass
class Fixup:
    """Resolved-form fixup awaiting linker's address resolution.

    `seg_idx` (1-based) is the segment this fixup is patched into;
    `where` is the absolute byte offset within that segment's data buffer
    (i.e., LEDATA segment-offset + the FIXUP's data-offset field).
    """
    seg_idx: int
    where: int

    self_relative: bool          # M=0 means self-relative
    location: int                # 0=8-bit, 1=16-bit ofs, 2=16-bit seg, 3=32-bit far ptr

    frame_method: int            # 0=segment 1=group 2=external 4=preceding-frame 5=target-frame
    frame_index: int             # interpretation depends on frame_method (1-based)

    target_method: int           # 0..2 with disp; 4..6 without disp
    target_index: int            # 1-based seg/group/extern idx
    target_displacement: int     # 0 if "no disp" methods

    @property
    def has_disp(self) -> bool:
        return self.target_method < 4


@dataclass
class Module:
    path: str
    name: str = ''
    lnames: List[str] = field(default_factory=lambda: [''])  # 1-indexed
    segments: List[Optional[Segment]] = field(default_factory=lambda: [None])
    groups: List[Optional[Group]] = field(default_factory=lambda: [None])
    externs: List[str] = field(default_factory=lambda: [''])  # 1-indexed
    publics: List[Public] = field(default_factory=list)
    entry_seg_idx: int = 0
    entry_offset: int = 0
    has_entry: bool = False


# ---------------------------------------------------------------------------
# Module parser
# ---------------------------------------------------------------------------


class ModuleParser:
    def __init__(self, path: str, data: bytes):
        self.path = path
        self.data = data
        self.mod = Module(path=path)

        # State for FIXUPP threading
        self.frame_threads: List[Optional[Tuple[int, int]]] = [None] * 4
        self.target_threads: List[Optional[Tuple[int, int]]] = [None] * 4

        # The most recent LEDATA's segment + offset, so FIXUPP can resolve
        # "data offset within LEDATA".
        self.last_ledata_seg: int = 0
        self.last_ledata_offset: int = 0

    def parse(self) -> Module:
        d = self.data
        p = 0
        while p < len(d):
            if p + 3 > len(d):
                die('%s: truncated record header at offset %d' % (self.path, p))
            rec_type = d[p]
            length = struct.unpack_from('<H', d, p + 1)[0]
            if p + 3 + length > len(d):
                die('%s: record at offset %d claims length %d but file ends earlier'
                    % (self.path, p, length))
            body = d[p + 3:p + 3 + length - 1]   # strip trailing checksum byte
            self._handle(rec_type, body, p)
            p += 3 + length
        return self.mod

    # -------------------- record dispatch --------------------

    def _handle(self, rec: int, body: bytes, off: int) -> None:
        try:
            if rec == R_THEADR or rec == R_LHEADR:
                name, _ = read_str(body, 0)
                if not self.mod.name:
                    self.mod.name = name
            elif rec == R_COMENT:
                pass  # ignore all comments
            elif rec in (R_MODEND, R_MODEND2):
                self._handle_modend(rec, body)
            elif rec == R_EXTDEF:
                self._handle_extdef(body)
            elif rec in (R_PUBDEF, R_PUBDEF2):
                self._handle_pubdef(body)
            elif rec == R_LNAMES:
                self._handle_lnames(body)
            elif rec in (R_SEGDEF, R_SEGDEF2):
                self._handle_segdef(body, big=(rec == R_SEGDEF2))
            elif rec == R_GRPDEF:
                self._handle_grpdef(body)
            elif rec in (R_FIXUPP, R_FIXUPP2):
                self._handle_fixupp(body)
            elif rec in (R_LEDATA, R_LEDATA2):
                self._handle_ledata(body, big=(rec == R_LEDATA2))
            elif rec in (R_LIDATA, R_LIDATA2):
                self._handle_lidata(body, big=(rec == R_LIDATA2))
            elif rec in (R_LINNUM, R_LINNUM2,
                         R_BAKPAT, R_NBKPAT,
                         R_LEXTDEF, R_LPUBDEF, R_LCOMDEF, R_CEXTDEF,
                         R_COMDEF, R_COMDAT, R_COMDAT2, R_LINSYM, R_ALIAS,
                         R_LLNAMES):
                # Parse-and-discard. We don't generate anything here; if the
                # module relies on these records, the linker will fail at
                # symbol-resolution time with a clear error.
                pass
            elif rec in (0xB7,):
                die('%s: 32-bit OMF (LIDAT32) not supported' % self.path)
            else:
                die('%s: unknown OMF record type 0x%02X at offset %d'
                    % (self.path, rec, off))
        except IndexError as e:
            die('%s: malformed record 0x%02X at offset %d: %s'
                % (self.path, rec, off, e))

    # -------------------- record handlers --------------------

    def _handle_lnames(self, body: bytes) -> None:
        i = 0
        while i < len(body):
            s, i = read_str(body, i)
            self.mod.lnames.append(s)

    def _handle_segdef(self, body: bytes, big: bool) -> None:
        # Segment attributes byte: AAA C P B
        # AAA (3 bits): alignment
        # CCC (3 bits): combine
        # P (1 bit):    "Big" (length = 64KB)
        # B (1 bit):    use32 — must be 0 for our purposes
        attr = body[0]
        align_field = (attr >> 5) & 0x7
        combine = (attr >> 2) & 0x7
        big_bit = (attr >> 1) & 0x1
        use32   = attr & 0x1
        i = 1

        if use32:
            die('%s: 32-bit USE32 segment not supported' % self.path)

        # If alignment field is 0 ("absolute"), three more frame/offset
        # bytes follow. We don't support absolute segments.
        if align_field == 0:
            die('%s: absolute segments not supported' % self.path)

        align_map = {1: 1, 2: 2, 3: 16, 4: 256, 5: 4, 6: 4096}
        align = align_map.get(align_field, 16)

        length = struct.unpack_from('<H', body, i)[0]
        i += 2
        # 64KB segment: explicit "big" record (0x99) OR big_bit in attr.
        # A length of 0 with big-flag set means 64KB; without big-flag it
        # means a genuinely empty segment.
        is_big = big or big_bit
        if is_big and length == 0:
            length = 0x10000

        name_idx, i = read_index(body, i)
        class_idx, i = read_index(body, i)
        overlay_idx, i = read_index(body, i)

        name = self._lname(name_idx)
        cls  = self._lname(class_idx)

        seg = Segment(
            name=name, cls=cls, length=length,
            combine=combine, big=is_big, align=align,
        )
        # Pre-allocate the data buffer so LEDATA writes don't need to grow.
        seg.data = bytearray(length)
        self.mod.segments.append(seg)

    def _handle_grpdef(self, body: bytes) -> None:
        i = 0
        name_idx, i = read_index(body, i)
        members: List[int] = []
        while i < len(body):
            tag = body[i]; i += 1
            if tag != 0xFF:
                die('%s: unexpected GRPDEF component tag 0x%02X' % (self.path, tag))
            seg_idx, i = read_index(body, i)
            members.append(seg_idx)
        self.mod.groups.append(Group(name=self._lname(name_idx), seg_indices=members))

    def _handle_extdef(self, body: bytes) -> None:
        i = 0
        while i < len(body):
            name, i = read_str(body, i)
            # type index (variable)
            _, i = read_index(body, i)
            self.mod.externs.append(name)

    def _handle_pubdef(self, body: bytes) -> None:
        i = 0
        group_idx, i = read_index(body, i)
        seg_idx,   i = read_index(body, i)
        if group_idx == 0 and seg_idx == 0:
            i += 2  # base frame (we don't support absolute symbols beyond skipping)
            die('%s: absolute PUBDEF not supported' % self.path)
        while i < len(body):
            name, i = read_str(body, i)
            offset = struct.unpack_from('<H', body, i)[0]
            i += 2
            _, i = read_index(body, i)  # type index
            self.mod.publics.append(Public(name=name, group_idx=group_idx,
                                           seg_idx=seg_idx, offset=offset))

    def _handle_modend(self, rec: int, body: bytes) -> None:
        if not body:
            return
        mod_type = body[0]
        # bit 6 = main module, bit 7 = has start address
        if mod_type & 0x40:
            self.mod.has_entry = True
        if mod_type & 0x40 and len(body) >= 2:
            # parse logical start address (FIXUPP-style fix-data + indices + disp)
            i = 1
            try:
                end_data = body[i]; i += 1
                # Frame
                fr_method = (end_data >> 4) & 0x7
                tg_method = end_data & 0x7
                if (end_data & 0x80) == 0:
                    # frame index follows
                    _, i = read_index(body, i)
                if (end_data & 0x08) == 0:
                    target_idx, i = read_index(body, i)
                else:
                    target_idx = 0  # threaded — ignore
                if (end_data & 0x04) == 0:
                    # displacement follows (16-bit; or 32-bit if Big — we use 16)
                    if i + 2 <= len(body):
                        disp = struct.unpack_from('<H', body, i)[0]
                        i += 2
                    else:
                        disp = 0
                else:
                    disp = 0
                if tg_method in (0, 4):  # segment-relative
                    self.mod.entry_seg_idx = target_idx
                    self.mod.entry_offset = disp
            except Exception:
                pass

    def _handle_ledata(self, body: bytes, big: bool) -> None:
        i = 0
        seg_idx, i = read_index(body, i)
        seg = self._seg(seg_idx)
        if big or seg.big:
            offset = struct.unpack_from('<I', body, i)[0]
            i += 4
        else:
            offset = struct.unpack_from('<H', body, i)[0]
            i += 2
        chunk = body[i:]
        end = offset + len(chunk)
        if end > len(seg.data):
            # Allow growing for "Big" 64KB segments where LENGTH was 0xFFFF
            if end <= 0x10000:
                seg.data.extend(b'\x00' * (end - len(seg.data)))
            else:
                die('%s: LEDATA writes past end of segment %s (%d > %d)'
                    % (self.path, seg.name, end, len(seg.data)))
        seg.data[offset:end] = chunk
        self.last_ledata_seg = seg_idx
        self.last_ledata_offset = offset

    def _handle_lidata(self, body: bytes, big: bool) -> None:
        i = 0
        seg_idx, i = read_index(body, i)
        seg = self._seg(seg_idx)
        if big or seg.big:
            offset = struct.unpack_from('<I', body, i)[0]
            i += 4
        else:
            offset = struct.unpack_from('<H', body, i)[0]
            i += 2
        # Recursively expand the iterated data block(s) until body is consumed.
        out = bytearray()
        while i < len(body):
            i = self._expand_iblock(body, i, out)
        end = offset + len(out)
        if end > len(seg.data):
            seg.data.extend(b'\x00' * (end - len(seg.data)))
        seg.data[offset:end] = out
        self.last_ledata_seg = seg_idx
        self.last_ledata_offset = offset

    def _expand_iblock(self, body: bytes, i: int, out: bytearray) -> int:
        repeat = struct.unpack_from('<H', body, i)[0]; i += 2
        block_count = struct.unpack_from('<H', body, i)[0]; i += 2
        if block_count == 0:
            # Raw content: 1 byte length, then that many bytes
            n = body[i]; i += 1
            chunk = body[i:i + n]; i += n
            for _ in range(repeat):
                out.extend(chunk)
            return i
        # Nested iterated blocks
        nested = bytearray()
        for _ in range(block_count):
            i = self._expand_iblock_to(body, i, nested)
        for _ in range(repeat):
            out.extend(nested)
        return i

    def _expand_iblock_to(self, body: bytes, i: int, out: bytearray) -> int:
        return self._expand_iblock(body, i, out)

    def _handle_fixupp(self, body: bytes) -> None:
        i = 0
        while i < len(body):
            b = body[i]
            if b & 0x80:
                i = self._parse_fixup(body, i)
            else:
                i = self._parse_thread(body, i)

    def _parse_thread(self, body: bytes, i: int) -> int:
        b = body[i]; i += 1
        # bit 7 = 0 (THREAD)
        # bit 6 = D (1=frame thread, 0=target thread)
        # bits 4-5 = method (0..3 stored; for target add P implicitly)
        # bits 0-1 = thread number (0..3); bit 2/3 reserved
        is_frame = (b & 0x40) != 0
        method = (b & 0x1C) >> 2
        thread_no = b & 0x03
        # For most methods an index follows
        idx = 0
        if is_frame:
            if method <= 2:
                idx, i = read_index(body, i)
        else:
            if method <= 2:
                idx, i = read_index(body, i)
        if is_frame:
            self.frame_threads[thread_no] = (method, idx)
        else:
            self.target_threads[thread_no] = (method, idx)
        return i

    def _parse_fixup(self, body: bytes, i: int) -> int:
        # locat: 2 bytes BIG-ENDIAN
        if i + 2 > len(body):
            die('%s: truncated FIXUP locat' % self.path)
        locat = (body[i] << 8) | body[i + 1]
        i += 2
        # locat bits (interpreted big-endian as a single 16-bit value):
        # bit 15: must be 1 (we already filtered on this)
        # bit 14: M (mode)
        # bits 10-13: location (4 bits)
        # bits 0-9: data record offset
        m = (locat >> 14) & 0x1
        location = (locat >> 10) & 0xF
        data_offset = locat & 0x3FF

        # fix data byte
        fixdata = body[i]; i += 1
        F = (fixdata >> 7) & 1   # 1: frame from thread
        frame_field = (fixdata >> 4) & 0x7
        T = (fixdata >> 3) & 1   # 1: target from thread (low 2 bits of target field)
        P = (fixdata >> 2) & 1   # 1: no displacement
        target_field = fixdata & 0x3 if T else fixdata & 0x7

        # Resolve frame
        if F:
            t = self.frame_threads[frame_field & 0x3]
            if t is None:
                die('%s: FIXUP references unset frame thread %d'
                    % (self.path, frame_field & 0x3))
            frame_method, frame_index = t
        else:
            frame_method = frame_field
            if frame_method <= 2:
                frame_index, i = read_index(body, i)
            else:
                frame_index = 0  # 4=preceding-frame, 5=target-frame: no index

        # Resolve target
        if T:
            t = self.target_threads[target_field & 0x3]
            if t is None:
                die('%s: FIXUP references unset target thread %d'
                    % (self.path, target_field & 0x3))
            target_method_base, target_index = t
            target_method = target_method_base | (4 if P else 0)
        else:
            target_method = target_field
            if (target_method & 0x3) <= 2:
                target_index, i = read_index(body, i)
            else:
                target_index = 0

        # Displacement
        if not P:
            if i + 2 > len(body):
                die('%s: truncated FIXUP displacement' % self.path)
            disp = struct.unpack_from('<H', body, i)[0]
            i += 2
        else:
            disp = 0

        seg_idx = self.last_ledata_seg
        if seg_idx == 0:
            die('%s: FIXUP without preceding LEDATA/LIDATA' % self.path)

        fix = Fixup(
            seg_idx=seg_idx,
            where=self.last_ledata_offset + data_offset,
            self_relative=(m == 0),
            location=location,
            frame_method=frame_method,
            frame_index=frame_index,
            target_method=target_method & 0x7,
            target_index=target_index,
            target_displacement=disp,
        )
        self.mod.segments[seg_idx].fixups.append(fix)
        return i

    # -------------------- helpers --------------------

    def _lname(self, idx: int) -> str:
        if idx <= 0 or idx >= len(self.mod.lnames):
            die('%s: bad LNAMES index %d' % (self.path, idx))
        return self.mod.lnames[idx]

    def _seg(self, idx: int) -> Segment:
        if idx <= 0 or idx >= len(self.mod.segments) or self.mod.segments[idx] is None:
            die('%s: bad segment index %d' % (self.path, idx))
        return self.mod.segments[idx]


# ---------------------------------------------------------------------------
# Linker
# ---------------------------------------------------------------------------


@dataclass
class OutSeg:
    name: str
    cls: str
    align: int
    para_base: int = 0           # paragraph offset relative to image base
    byte_base: int = 0           # byte offset of first usable content
    length: int = 0              # cumulative payload length
    data: bytearray = field(default_factory=bytearray)


@dataclass
class OutGroup:
    """Coalesced group across all input modules. Key by group name (DGROUP).

    Members are output segment indices (into Linker.out_segs) in the order
    we placed them. The group's frame address is the lowest member's
    paragraph base.
    """
    name: str
    member_out_segs: List[int] = field(default_factory=list)


@dataclass
class GlobalSymbol:
    module_idx: int
    seg_idx: int      # 1-based index within that module
    offset: int


class Linker:
    def __init__(self, modules: List[Module],
                 stack_size: int, entry_symbol: str):
        self.modules = modules
        self.stack_size = stack_size
        self.entry_symbol = entry_symbol

        self.out_segs: List[OutSeg] = []
        # Map (module_idx, mod_seg_idx) → (out_seg_idx, byte_offset_within_out_seg)
        self.seg_map: Dict[Tuple[int, int], Tuple[int, int]] = {}
        # Map "DGROUP" → OutGroup; same name is shared across all modules.
        self.out_groups: Dict[str, OutGroup] = {}
        # Global symbol table
        self.symbols: Dict[str, GlobalSymbol] = {}
        # Stack segment + entry mapping
        self.stack_seg_idx: int = -1
        self.entry_cs: int = 0
        self.entry_ip: int = 0
        # Relocations to write into MZ header (offset, segment) — segment
        # is paragraph offset relative to image base.
        self.relocs: List[Tuple[int, int]] = []

    # -------------------- top-level --------------------

    def link(self, out_path: str, map_path: Optional[str]) -> None:
        self._build_symbol_table()
        self._layout_segments()
        self._coalesce_groups()
        self._resolve_entry()
        self._apply_fixups()
        image, hdr = self._build_image()
        Path(out_path).write_bytes(hdr + image)
        if map_path:
            self._write_map(map_path, out_path, len(image), len(hdr))
        self._print_summary(out_path, len(image), len(hdr))

    # -------------------- symbol table --------------------

    def _build_symbol_table(self) -> None:
        for mi, m in enumerate(self.modules):
            for pub in m.publics:
                if pub.name in self.symbols:
                    prev = self.symbols[pub.name]
                    die('duplicate public symbol %r: defined in %s and %s'
                        % (pub.name, self.modules[prev.module_idx].path, m.path))
                self.symbols[pub.name] = GlobalSymbol(
                    module_idx=mi, seg_idx=pub.seg_idx, offset=pub.offset)

        # Verify externs resolve
        unresolved: List[Tuple[str, str]] = []
        for m in self.modules:
            for ext in m.externs[1:]:
                if ext not in self.symbols:
                    unresolved.append((m.path, ext))
        if unresolved:
            msgs = ['undefined symbols:']
            for path, name in unresolved:
                msgs.append('  %s: %s' % (path, name))
            die('\n'.join(msgs))

    # -------------------- layout --------------------

    def _layout_segments(self) -> None:
        """Lay out output segments: CODE first (per-module, no coalescing),
        then DATA (coalesced by name), then BSS (coalesced by name), then a
        STACK segment of --stack-size, then any HUGE-class segments (each
        kept distinct so the linker can place a > 64K array as a series
        of paragraph-adjacent chunks).

        HUGE segments are deliberately laid out AFTER the stack and AFTER
        DGROUP coalescing so they cannot inflate the DGROUP/SP overflow
        check at _build_image.  They get their own paragraph bases and
        the existing segment-relocation machinery generates the right
        MZ-header fixups for far-pointer references."""
        # CODE: each module's CODE-class segments stay distinct.
        # Within a module, segments appear in SEGDEF declaration order.
        for mi, m in enumerate(self.modules):
            for si, seg in enumerate(m.segments):
                if seg is None:
                    continue
                if seg.cls.upper() == 'CODE':
                    self._place_distinct(mi, si, seg)

        # DATA: coalesced by segment NAME across modules.
        coalesced_data: Dict[str, int] = {}
        for mi, m in enumerate(self.modules):
            for si, seg in enumerate(m.segments):
                if seg is None:
                    continue
                if seg.cls.upper() == 'DATA':
                    self._place_coalesced(mi, si, seg, coalesced_data)

        # BSS: same coalescing rule.
        coalesced_bss: Dict[str, int] = {}
        for mi, m in enumerate(self.modules):
            for si, seg in enumerate(m.segments):
                if seg is None:
                    continue
                if seg.cls.upper() == 'BSS':
                    self._place_coalesced(mi, si, seg, coalesced_bss)

        # Stack segment.
        stack = OutSeg(name='STACK', cls='STACK', align=2)
        stack.data = bytearray(self.stack_size)
        stack.length = self.stack_size
        self.stack_seg_idx = len(self.out_segs)
        self.out_segs.append(stack)

        # HUGE: each segment stays distinct (no name-coalescing).  Chunks
        # of the same logical array carry the `_HUGE_<sym>_N` convention
        # from asm_to_omf.py and rely on sorted N to be paragraph-
        # adjacent at layout time.  We sort by (name) so `_0`, `_1`, `_2`
        # land in lexical order; for arrays whose chunk count fits in a
        # single decimal digit this is also numeric order.
        huge_segs: List[Tuple[int, int]] = []
        for mi, m in enumerate(self.modules):
            for si, seg in enumerate(m.segments):
                if seg is None:
                    continue
                if seg.cls.upper() == 'HUGE':
                    huge_segs.append((mi, si))
        huge_segs.sort(key=lambda p: self.modules[p[0]].segments[p[1]].name)
        for mi, si in huge_segs:
            seg = self.modules[mi].segments[si]
            self._place_distinct(mi, si, seg)

        # FAR_DATA / FAR_BSS: far-data-model module statics.  Each module's
        # `<BASE>_DATA` / `<BASE>_BSS` segment is placed DISTINCTLY (its own
        # paragraph base, addressed by its own `seg _sym` selector), OUTSIDE
        # DGROUP — so the aggregate static data can exceed 64KB.  Laid out
        # after the stack so they never inflate the DGROUP/SP overflow check.
        for cls in ('FAR_DATA', 'FAR_BSS'):
            for mi, m in enumerate(self.modules):
                for si, seg in enumerate(m.segments):
                    if seg is None:
                        continue
                    if seg.cls.upper() == cls:
                        self._place_distinct(mi, si, seg)

        # Compute paragraph bases.  HUGE chunks already arrive in
        # `_HUGE_<sym>_N` order so the `_0`, `_1`, ... chunks of the
        # same array land at consecutive paragraph bases — provided
        # _0's length is a paragraph multiple (HUGE_CHUNK_BYTES in
        # asm_to_omf.py is 65520, == 4095 paragraphs).
        cur_byte = 0
        for os_ in self.out_segs:
            # Round up to alignment (paragraph at minimum)
            align = max(os_.align, 16)
            cur_byte = (cur_byte + align - 1) & ~(align - 1)
            if cur_byte % 16 != 0:
                cur_byte = (cur_byte + 15) & ~15
            os_.byte_base = cur_byte
            os_.para_base = cur_byte // 16
            cur_byte += os_.length

    def _place_distinct(self, mi: int, si: int, seg: Segment) -> None:
        idx = len(self.out_segs)
        out = OutSeg(name=seg.name, cls=seg.cls, align=seg.align)
        out.data = bytearray(seg.data)
        out.length = len(seg.data)
        self.out_segs.append(out)
        self.seg_map[(mi, si)] = (idx, 0)

    def _place_coalesced(self, mi: int, si: int, seg: Segment,
                         table: Dict[str, int]) -> None:
        if seg.name in table:
            idx = table[seg.name]
            out = self.out_segs[idx]
            # Pad to alignment within combined segment
            align = max(seg.align, 1)
            pad = (-len(out.data)) & (align - 1)
            if pad:
                out.data.extend(b'\x00' * pad)
            offset = len(out.data)
            out.data.extend(seg.data)
            out.length = len(out.data)
            # Take the loosest alignment requirement from any contributor
            if seg.align > out.align:
                out.align = seg.align
            self.seg_map[(mi, si)] = (idx, offset)
        else:
            idx = len(self.out_segs)
            out = OutSeg(name=seg.name, cls=seg.cls, align=seg.align)
            out.data = bytearray(seg.data)
            out.length = len(seg.data)
            self.out_segs.append(out)
            table[seg.name] = idx
            self.seg_map[(mi, si)] = (idx, 0)

    def _coalesce_groups(self) -> None:
        for m in self.modules:
            for g in m.groups[1:]:
                if g is None:
                    continue
                og = self.out_groups.setdefault(g.name, OutGroup(name=g.name))
                # Translate segment indices from this module to output segs
                mi = self.modules.index(m)
                for si in g.seg_indices:
                    if (mi, si) not in self.seg_map:
                        continue
                    out_idx, _ = self.seg_map[(mi, si)]
                    if out_idx not in og.member_out_segs:
                        og.member_out_segs.append(out_idx)

    # -------------------- entry --------------------

    def _resolve_entry(self) -> None:
        sym = self.symbols.get(self.entry_symbol)
        if sym is None:
            die('entry symbol %r not found' % self.entry_symbol)
        out_idx, base = self.seg_map[(sym.module_idx, sym.seg_idx)]
        out = self.out_segs[out_idx]
        # Frame for code is normally the segment itself. But the offset must
        # be relative to that frame.
        self.entry_cs = out.para_base
        self.entry_ip = base + sym.offset

    # -------------------- fixups --------------------

    def _frame_para(self, m: Module, mi: int, fix: Fixup,
                    target_out_idx: int, target_offset_in_out: int) -> int:
        """Return paragraph base of the frame for this fixup."""
        method = fix.frame_method
        if method == 0:  # segment
            mod_seg_idx = fix.frame_index
            out_idx, _ = self.seg_map[(mi, mod_seg_idx)]
            return self.out_segs[out_idx].para_base
        if method == 1:  # group
            g = m.groups[fix.frame_index]
            og = self.out_groups[g.name]
            # Frame = lowest paragraph base among members
            return min(self.out_segs[i].para_base for i in og.member_out_segs)
        if method == 2:  # external — frame is the extern's segment
            ext_name = m.externs[fix.frame_index]
            sym = self.symbols[ext_name]
            out_idx, _ = self.seg_map[(sym.module_idx, sym.seg_idx)]
            return self.out_segs[out_idx].para_base
        if method in (4, 5):  # preceding-frame / target-frame
            return self.out_segs[target_out_idx].para_base
        die('unsupported frame method %d' % method)
        return 0

    def _resolve_target(self, m: Module, mi: int, fix: Fixup) -> Tuple[int, int]:
        """Return (out_seg_idx, byte_offset_within_that_segment) for target."""
        tm = fix.target_method & 0x3   # disp-or-not collapses to base method
        if tm == 0:  # segment + disp
            out_idx, base = self.seg_map[(mi, fix.target_index)]
            return out_idx, base + fix.target_displacement
        if tm == 1:  # group + disp
            g = m.groups[fix.target_index]
            og = self.out_groups[g.name]
            # Group's "base" is the lowest member's byte base.
            base_para = min(self.out_segs[i].para_base for i in og.member_out_segs)
            base_byte = base_para * 16
            # Use the first member as "target seg" — only its byte_base
            # matters for offset/byte computations below.
            out_idx = og.member_out_segs[0]
            for i in og.member_out_segs:
                if self.out_segs[i].para_base == base_para:
                    out_idx = i
                    break
            return out_idx, (base_byte - self.out_segs[out_idx].byte_base
                             + fix.target_displacement)
        if tm == 2:  # external + disp
            ext_name = m.externs[fix.target_index]
            sym = self.symbols[ext_name]
            out_idx, base = self.seg_map[(sym.module_idx, sym.seg_idx)]
            return out_idx, base + sym.offset + fix.target_displacement
        die('unsupported target method %d' % fix.target_method)
        return -1, 0

    def _apply_fixups(self) -> None:
        for mi, m in enumerate(self.modules):
            for si, seg in enumerate(m.segments):
                if seg is None:
                    continue
                out_idx, mod_base_in_out = self.seg_map.get((mi, si), (-1, 0))
                if out_idx < 0:
                    continue
                out = self.out_segs[out_idx]
                for fix in seg.fixups:
                    self._apply_one_fixup(m, mi, fix, out_idx,
                                          mod_base_in_out, out)

    def _apply_one_fixup(self, m: Module, mi: int, fix: Fixup,
                         site_out_idx: int, mod_base_in_out: int,
                         site_out: OutSeg) -> None:
        # Resolve target
        tgt_out_idx, tgt_byte_in_out = self._resolve_target(m, mi, fix)
        tgt_out = self.out_segs[tgt_out_idx]
        # Absolute byte address of target within the image (relative to image base)
        tgt_abs_byte = tgt_out.byte_base + tgt_byte_in_out

        # Resolve frame
        frame_para = self._frame_para(m, mi, fix, tgt_out_idx, tgt_byte_in_out)
        frame_byte = frame_para * 16

        # Site absolute byte address (where the fixup field lives)
        site_abs_byte = site_out.byte_base + mod_base_in_out + fix.where

        # Patch
        loc = fix.location
        if fix.self_relative:
            # All self-relative fixups are computed as: target - (site + size)
            # The displacement currently in the field (often 0, sometimes
            # nasm pre-computes -3 etc.) is *added* to the result per OMF.
            if loc == 0:   # 8-bit
                size = 1
            elif loc == 1: # 16-bit offset
                size = 2
            else:
                die('self-relative fixup with unsupported location %d' % loc)
            cur = self._read_field(site_out.data,
                                   mod_base_in_out + fix.where, size)
            disp = (tgt_abs_byte - (site_abs_byte + size) + cur) & ((1 << (8 * size)) - 1)
            self._write_field(site_out.data,
                              mod_base_in_out + fix.where, size, disp)
            return

        # Segment-relative (the common case for our linker)
        if loc == 0:  # low 8 bits of offset within frame
            cur = site_out.data[mod_base_in_out + fix.where]
            val = (tgt_abs_byte - frame_byte + cur) & 0xFF
            site_out.data[mod_base_in_out + fix.where] = val
            return

        if loc == 1:  # 16-bit offset within frame
            cur = struct.unpack_from('<H', site_out.data,
                                     mod_base_in_out + fix.where)[0]
            val = (tgt_abs_byte - frame_byte + cur) & 0xFFFF
            struct.pack_into('<H', site_out.data,
                             mod_base_in_out + fix.where, val)
            return

        if loc == 2:  # 16-bit segment selector
            # Selector = frame's paragraph base. Needs a runtime relocation
            # because the actual segment depends on the program's load
            # paragraph.
            struct.pack_into('<H', site_out.data,
                             mod_base_in_out + fix.where, frame_para & 0xFFFF)
            self._add_reloc(site_out, mod_base_in_out + fix.where)
            return

        if loc == 3:  # 32-bit far ptr: low word = offset within target segment, high word = selector
            cur_off = struct.unpack_from('<H', site_out.data,
                                         mod_base_in_out + fix.where)[0]
            off = (tgt_byte_in_out + cur_off) & 0xFFFF
            struct.pack_into('<H', site_out.data,
                             mod_base_in_out + fix.where, off)
            struct.pack_into('<H', site_out.data,
                             mod_base_in_out + fix.where + 2,
                             frame_para & 0xFFFF)
            self._add_reloc(site_out, mod_base_in_out + fix.where + 2)
            return

        if loc == 5:  # loader-resolved 16-bit offset (treat same as 1)
            cur = struct.unpack_from('<H', site_out.data,
                                     mod_base_in_out + fix.where)[0]
            val = (tgt_abs_byte - frame_byte + cur) & 0xFFFF
            struct.pack_into('<H', site_out.data,
                             mod_base_in_out + fix.where, val)
            return

        if loc == 9 or loc == 13:  # 32-bit offset within frame
            cur = struct.unpack_from('<I', site_out.data,
                                     mod_base_in_out + fix.where)[0]
            val = (tgt_abs_byte - frame_byte + cur) & 0xFFFFFFFF
            struct.pack_into('<I', site_out.data,
                             mod_base_in_out + fix.where, val)
            return

        die('unsupported fixup location %d' % loc)

    @staticmethod
    def _read_field(buf: bytearray, off: int, size: int) -> int:
        if size == 1:
            v = buf[off]
            return v - 0x100 if v & 0x80 else v
        if size == 2:
            v = struct.unpack_from('<H', buf, off)[0]
            return v - 0x10000 if v & 0x8000 else v
        die('bad field size %d' % size)
        return 0

    @staticmethod
    def _write_field(buf: bytearray, off: int, size: int, val: int) -> None:
        if size == 1:
            buf[off] = val & 0xFF
        elif size == 2:
            struct.pack_into('<H', buf, off, val & 0xFFFF)
        else:
            die('bad field size %d' % size)

    def _add_reloc(self, seg: OutSeg, byte_within_seg: int) -> None:
        """Record a runtime relocation. The MZ format wants a (segment,
        offset) pair where segment is paragraphs from image base and
        offset is bytes within that segment (0..15 for the bottom of the
        paragraph)."""
        abs_byte = seg.byte_base + byte_within_seg
        para = abs_byte // 16
        ofs = abs_byte - (para * 16)
        # MZ's segment is taken modulo 0x10000; we keep para small for now.
        self.relocs.append((ofs, para))

    # -------------------- emit --------------------

    def _build_image(self) -> Tuple[bytes, bytes]:
        # Concatenate output segments into a single image, padded.
        cur = 0
        image = bytearray()
        for seg in self.out_segs:
            # Pad to byte_base
            if seg.byte_base > cur:
                image.extend(b'\x00' * (seg.byte_base - cur))
                cur = seg.byte_base
            image.extend(seg.data)
            cur = seg.byte_base + seg.length
            if seg.length > len(seg.data):
                # BSS-only contributors might have logical length > data
                image.extend(b'\x00' * (seg.length - len(seg.data)))
                cur = seg.byte_base + seg.length

        # MZ header (28 bytes) + reloc table; pad to paragraph multiple.
        n_relocs = len(self.relocs)
        hdr_min = 28 + 4 * n_relocs
        hdr_size_bytes = (hdr_min + 15) & ~15
        hdr_paragraphs = hdr_size_bytes // 16
        total_image = hdr_size_bytes + len(image)

        bytes_in_last = total_image % 512
        total_pages = (total_image + 511) // 512

        # We want SS == DS == DGROUP so that "near char *" works
        # consistently for both stack-local buffers and DGROUP globals.
        # The C runtime (strcpy's stosb, etc.) treats near pointers as
        # DS-relative; if SS != DS, a stack pointer passed as `char *`
        # writes to the wrong segment.  Lay the STACK segment immediately
        # after DGROUP's BSS so it's reachable as a 16-bit offset from
        # DGROUP's frame, then set SS=DGROUP and SP=stack-top-within-DGROUP.
        dgroup = self.out_groups.get('DGROUP')
        stack_seg = self.out_segs[self.stack_seg_idx]
        if dgroup and dgroup.member_out_segs:
            dgroup_para = min(self.out_segs[i].para_base
                              for i in dgroup.member_out_segs)
            stack_off_in_dgroup = (stack_seg.para_base - dgroup_para) * 16
            sp_full = stack_off_in_dgroup + self.stack_size
            if sp_full > 0xFFFF:
                die('DGROUP + stack overflows 64KB (sp=%d). '
                    'Shrink data or stack.' % sp_full)
            ss_para = dgroup_para
            sp = sp_full & 0xFFFF
        else:
            ss_para = stack_seg.para_base
            sp = self.stack_size & 0xFFFF

        hdr = bytearray(hdr_size_bytes)
        struct.pack_into('<2sHHHHHHHHHHHHH', hdr, 0,
                         b'MZ',
                         bytes_in_last,
                         total_pages,
                         n_relocs,
                         hdr_paragraphs,
                         0,                    # min alloc
                         0xFFFF,               # max alloc
                         ss_para,              # SS
                         sp,                   # SP
                         0,                    # checksum
                         self.entry_ip,        # IP
                         self.entry_cs,        # CS
                         28,                   # reloc table offset
                         0)                    # overlay
        # Write reloc table
        off = 28
        for ofs, seg in self.relocs:
            struct.pack_into('<HH', hdr, off, ofs, seg)
            off += 4
        return bytes(image), bytes(hdr)

    # -------------------- map / summary --------------------

    def _write_map(self, map_path: str, out_path: str,
                   image_size: int, hdr_size: int) -> None:
        lines: List[str] = []
        lines.append('Output: %s' % out_path)
        lines.append('Header: %d bytes  Image: %d bytes  Total: %d bytes'
                     % (hdr_size, image_size, hdr_size + image_size))
        lines.append('Entry: CS=0x%04X IP=0x%04X' % (self.entry_cs, self.entry_ip))
        lines.append('Stack: SS=0x%04X SP=0x%04X' %
                     (self.out_segs[self.stack_seg_idx].para_base,
                      self.stack_size))
        lines.append('Relocations: %d' % len(self.relocs))
        lines.append('')
        lines.append('Output segments:')
        lines.append('  %-20s %-8s %-8s %-8s %s'
                     % ('NAME', 'CLASS', 'PARA', 'SIZE', 'BYTES'))
        for seg in self.out_segs:
            lines.append('  %-20s %-8s 0x%04X   0x%04X   %d'
                         % (seg.name, seg.cls, seg.para_base,
                            seg.length, seg.length))
        lines.append('')
        lines.append('Symbols:')
        for name in sorted(self.symbols):
            sym = self.symbols[name]
            out_idx, base = self.seg_map[(sym.module_idx, sym.seg_idx)]
            seg = self.out_segs[out_idx]
            lines.append('  %-32s seg=%-12s para=0x%04X off=0x%04X (mod=%s)'
                         % (name, seg.name, seg.para_base,
                            base + sym.offset,
                            Path(self.modules[sym.module_idx].path).name))
        Path(map_path).write_text('\n'.join(lines) + '\n')

    def _print_summary(self, out_path: str, image_size: int,
                       hdr_size: int) -> None:
        n_mods = len(self.modules)
        code_bytes = sum(s.length for s in self.out_segs if s.cls.upper() == 'CODE')
        data_bytes = sum(s.length for s in self.out_segs
                         if s.cls.upper() in ('DATA', 'BSS'))
        fardata_bytes = sum(s.length for s in self.out_segs
                            if s.cls.upper() in ('FAR_DATA', 'FAR_BSS', 'HUGE'))
        n_relocs = len(self.relocs)
        print('omf_link: linked %d modules' % n_mods)
        print('  code: %d bytes' % code_bytes)
        if fardata_bytes:
            print('  far data: %d bytes' % fardata_bytes)
        print('  data+bss: %d bytes' % data_bytes)
        print('  relocations: %d' % n_relocs)
        print('  image: %d bytes (header %d + body %d)'
              % (hdr_size + image_size, hdr_size, image_size))
        print('  output: %s' % out_path)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--output', default='a.out', help='output EXE path')
    ap.add_argument('--map', dest='map_path', default=None, help='write linker map')
    ap.add_argument('--stack-size', type=int, default=4096, help='stack size in bytes')
    ap.add_argument('--entry', default='_start', help='entry point symbol')
    ap.add_argument('--memory-model', default='medium',
                    help='only "medium" is currently supported')
    ap.add_argument('objs', nargs='+', help='input .obj files')
    args = ap.parse_args()

    if args.memory_model not in ('tiny', 'small', 'medium', 'compact',
                                 'large', 'huge'):
        raise NotImplementedError(
            'unknown memory model %r' % args.memory_model)

    if args.stack_size <= 0 or args.stack_size > 0xFFFF:
        die('--stack-size must be in 1..65535')

    modules: List[Module] = []
    for path in args.objs:
        try:
            data = Path(path).read_bytes()
        except OSError as e:
            die('cannot read %s: %s' % (path, e))
        m = ModuleParser(path, data).parse()
        modules.append(m)

    linker = Linker(modules, args.stack_size, args.entry)
    linker.link(args.output, args.map_path)


if __name__ == '__main__':
    main()
