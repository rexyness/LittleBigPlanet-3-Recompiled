#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copied from LBPPC (https://github.com/emurphy04/LBPPC, by emurphy04) at commit aad6a91,
# scripts/quit_menu.py, GPL-2.0-or-later, unchanged apart from these four comment lines. It writes
# user\file_edits.bin for our runtime's file layer (runtime/src/hle/file_edits.h); the runtime's
# answer to the two keys is our own (runtime/src/hle/quit.cpp). Run by tools/quit_menu_setup.py.
"""Quit Game in LBP3's own pause menu (CUSA00473 1.28), in place of Good Grief!.

The pause menu is a script, ModalOverlayStateMainMenu (patch_001.farc). Its Good Grief! button
(DoGriefReportButton) takes its label from LAMS.Translate and, pressed, opens the grief report
warning. Here the button asks Translate for two keys of LBPPC's own instead, which the runtime
answers (probe.c, quit_menu_install): QUIT_LABEL is the button's text ("Quit Game", or "Press again
to quit" after a first press), QUIT_ACTION a press. The button is also enabled offline, where the
game greys Good Grief! out.

The game finds a script by its GUID in output/orbisguids.map, which names a hash, and the hash in a
.farc's table. The edited script gets its own hash: out/file_edits.bin tells the runtime's file
layer (runtime_file.c) to show patch_001.farc with the script added after its data and a table that
includes it, and the map with that entry's size and hash changed. The game files are not changed.

Script format: ennuo's cwlib (RScript), revision 0x3f9 with compressed integers.
"""
import argparse
import bisect
import hashlib
import struct
import zlib
from pathlib import Path

SCRIPT_PATH = b'gamedata/scripts/modaloverlaystatemainmenu.ff'
SCRIPT_SHA1 = bytes.fromhex('50500623d5a50337d172c5c8f21f15407b894e1c')  # the 1.28 patch's
FARC = 'patch_001.farc'
MAP = 'output/orbisguids.map'
FUNCTION = 'DoGriefReportButton__Q5Gooeyi'
GRIEF_LABEL = -912847811                # Good Grief!'s LAMS key
QUIT_LABEL, QUIT_ACTION = 0x4C425051, 0x4C425058  # 'LBPQ', 'LBPX': probe.c's
LCi, CALL, B, NEW_OBJECT, RETURN = 0x03, 0xa4, 0xa6, 0xb5, 0xa5


class Reader:
    """Compressed-integer reads (uleb128; strings' lengths zigzag), big-endian fixed sizes."""
    def __init__(self, data):
        self.data, self.at = data, 0

    def u8(self):
        self.at += 1
        return self.data[self.at - 1]

    def i16(self):
        self.at += 2

    def uleb(self):
        value = shift = 0
        while True:
            byte = self.u8()
            value |= (byte & 0x7f) << shift
            if not byte & 0x80:
                return value
            shift += 7

    def string(self):
        n = self.uleb()
        n = (n >> 1) ^ -(n & 1)
        self.at += n
        return self.data[self.at - n:self.at]

    def resource(self):
        self.uleb()
        kind = self.u8()
        if kind & 2:
            self.uleb()
        if kind & 1:
            self.at += 20

    def rows(self, read):
        return [read() for _ in range(self.uleb())]

    def vector(self):
        n = self.uleb()
        if not n:
            return []
        width, values = self.u8(), [0] * n
        for byte in range(width):
            for i in range(n):
                values[i] |= self.u8() << (8 * byte)
        return values


def uleb(value):
    out = bytearray()
    while True:
        byte = value & 0x7f
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def code_of(body):
    """The bytecode (offset of its count, its end, the instructions) and each function's name and range."""
    r = Reader(body)
    r.string(); r.resource(); r.i16()
    r.rows(lambda: (r.u8(), r.u8(), r.u8(), r.u8(), r.resource(), r.uleb()))  # type references
    r.rows(lambda: (r.uleb(), r.uleb()))                                      # field references
    r.rows(lambda: (r.uleb(), r.uleb()))                                      # function references
    r.rows(lambda: (r.i16(), r.uleb(), r.uleb()))                             # fields
    r.rows(lambda: (r.i16(), r.uleb(), r.uleb(), r.uleb(), r.uleb()))         # properties

    def function():
        r.i16(); r.uleb(); name = r.uleb()
        ranges = [struct.unpack_from('>HH', body, r.at + 4 * i) for i in range(4)]
        r.at += 16
        r.uleb()
        return name, ranges[1][0], ranges[1][0] + ranges[1][1]
    functions = r.rows(function)
    r.rows(lambda: (r.uleb(), r.uleb()))                                      # arguments
    start = r.at
    code = r.rows(r.uleb)
    end = r.at
    r.rows(lambda: (r.u8(), r.u8()))                                          # line numbers (i16)
    r.rows(lambda: (r.i16(), r.uleb(), r.uleb(), r.uleb()))                   # locals
    offsets = r.vector()
    size = r.uleb()
    pool = body[r.at:r.at + size]
    names = [pool[a:pool.index(b'\0', a)] for a in offsets]
    return start, end, code, [(names[n].decode(), b, e) for n, b, e in functions]


def high(bits, value):
    return (bits & 0xffffffff) | ((value & 0xffffffff) << 32)


def edit(code, begin, end):
    """DoGriefReportButton, checked instruction by instruction against the 1.28 script first (False: not changed)."""
    c = code[begin:end]
    s32 = lambda bits: struct.unpack('<i', struct.pack('<I', bits >> 32))[0]
    expected = [(0, LCi, GRIEF_LABEL), (2, CALL, None), (6, B, 2), (7, LCi, 2), (23, NEW_OBJECT, None), (44, RETURN, None)]
    if len(c) != 45 or any(c[i] & 0xff != op or (value is not None and s32(c[i]) != value) for i, op, value in expected):
        return False
    c[0] = high(c[0], QUIT_LABEL)    # the label: Translate(QUIT_LABEL)
    c[7] = high(c[7], 0)             # enabled offline too (2 greys it out)
    c[23] = high(c[0], QUIT_ACTION)  # pressed: Translate(QUIT_ACTION), with the label's ARG and CALL
    c[24], c[25] = c[1], c[2]
    c[26] = high(c[6], 44 - 26)      # then to the RETURN
    code[begin:end] = c
    return True


def chunks(body):
    """The resource's zlib chunks (32 KiB of script each) and their sizes."""
    parts = [body[i:i + 0x8000] for i in range(0, len(body), 0x8000)]
    packed = [zlib.compress(p, 9) for p in parts]
    sizes = b''.join(struct.pack('>HH', len(z), len(p)) for z, p in zip(packed, parts))
    return struct.pack('>HH', 1, len(parts)) + sizes + b''.join(packed)


def farc_table(path):
    with open(path, 'rb') as f:
        f.seek(0, 2)
        size = f.tell()
        f.seek(size - 8)
        count, tag = struct.unpack('>I4s', f.read(8))
        if tag != b'FARC':
            return size, size, []
        start = size - 8 - count * 28
        f.seek(start)
        table = f.read(count * 28)
    return size, start, [table[i * 28:i * 28 + 28] for i in range(count)]


def prepare(game, out):
    out.mkdir(parents=True, exist_ok=True)
    target = out / 'file_edits.bin'
    target.unlink(missing_ok=True)
    guid_map = (game / MAP).read_bytes()
    at = guid_map.find(struct.pack('>H', len(SCRIPT_PATH)) + SCRIPT_PATH)
    fields = at + 2 + len(SCRIPT_PATH) + 4  # the date, then the size and hash
    if at < 0 or guid_map[fields + 4:fields + 24] != SCRIPT_SHA1:
        print('Quit Game: not added (the pause menu script is not the 1.28 one)')
        return
    farc_size, table_start, entries = farc_table(game / FARC)
    found = [e for e in entries if e[:20] == SCRIPT_SHA1]
    if not found:
        print(f'Quit Game: not added (no pause menu script in {FARC})')
        return
    offset, size = struct.unpack('>II', found[0][20:])
    with open(game / FARC, 'rb') as f:
        f.seek(offset)
        resource = f.read(size)
    if hashlib.sha1(resource).digest() != SCRIPT_SHA1:
        print(f'Quit Game: not added (the pause menu script in {FARC} does not match its hash)')
        return

    # Header: FSHb, revision, dependency table offset, branch (4), compression flags, compressed, chunks.
    dependencies, = struct.unpack_from('>I', resource, 8)
    flags = resource[12:18]
    count, = struct.unpack_from('>H', resource, 20)
    sizes = [struct.unpack_from('>HH', resource, 22 + 4 * i) for i in range(count)]
    data, body = 22 + 4 * count, b''
    for packed, _ in sizes:
        body += zlib.decompress(resource[data:data + packed])
        data += packed
    start, end, code, functions = code_of(body)
    begin, stop = next(((b, e) for name, b, e in functions if name == FUNCTION), (0, 0))
    if not edit(code, begin, stop):
        print(f'Quit Game: not added ({FUNCTION} is not the one this was made for)')
        return
    body = body[:start] + uleb(len(code)) + b''.join(uleb(c) for c in code) + body[end:]
    packed = chunks(body)
    new = resource[:8] + struct.pack('>I', 12 + len(flags) + len(packed)) + flags + packed + resource[dependencies:]
    sha1 = hashlib.sha1(new).digest()

    # The .farc as the game sees it: its data, the new script, the table with it added (in hash order).
    entry = sha1 + struct.pack('>II', table_start, len(new))
    keys = [e[:20] for e in entries]
    entries.insert(bisect.bisect_left(keys, sha1), entry)
    tail = new + b''.join(entries) + struct.pack('>I4s', len(entries), b'FARC')

    with target.open('wb') as f:
        f.write(struct.pack('<8sI', b'LBPEDIT1', 2))
        for path, original, cut, add, patches in (
                (FARC, farc_size, table_start, tail, []),
                (MAP, len(guid_map), len(guid_map), b'', [(fields, struct.pack('>I', len(new)) + sha1)])):
            name = path.encode()
            f.write(struct.pack('<H', len(name)) + name + struct.pack('<QQQ', original, cut, len(add)) + add)
            f.write(struct.pack('<I', len(patches)))
            for where, data in patches:
                f.write(struct.pack('<QI', where, len(data)) + data)
    print(f'Quit Game: added to the pause menu (script {sha1.hex()[:12]}, {len(new):,} bytes)')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('game', type=Path)
    p.add_argument('--out', type=Path, default=Path(__file__).resolve().parent.parent / 'out')
    a = p.parse_args()
    # The game starts without the button rather than not at all.
    try:
        prepare(a.game, a.out)
    except Exception as error:  # noqa: BLE001
        (a.out / 'file_edits.bin').unlink(missing_ok=True)
        print(f'Quit Game: not added ({type(error).__name__}: {error})')
