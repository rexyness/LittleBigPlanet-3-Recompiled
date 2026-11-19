"""Rebuild a plain ELF from a fake-signed PS4 SELF (same logic as UnwrapSelf in the
fork's runtime/src/core/module.cpp). Reads the SELF, writes the ELF elsewhere; never
writes into the game dump.

  python self_unwrap.py <in.self> <out.elf>
"""
import struct
import sys

ENCRYPTED, COMPRESSED, BLOCKED = 0x2, 0x8, 0x800


def unwrap(s):
    if s[:4] != b"\x4f\x15\x3d\x1d":
        raise ValueError("not a SELF")
    count, = struct.unpack_from("<H", s, 24)
    elf = 32 + count * 32
    if s[elf:elf + 4] != b"\x7fELF":
        raise ValueError("SELF holds no ELF header")
    phoff, = struct.unpack_from("<Q", s, elf + 32)
    phnum, = struct.unpack_from("<H", s, elf + 56)
    headers = phoff + phnum * 56
    phdrs = [struct.unpack_from("<IIQQQQQQ", s, elf + phoff + i * 56) for i in range(phnum)]
    end = max([headers] + [p[2] + p[5] for p in phdrs])
    out = bytearray(end)
    out[:headers] = s[elf:elf + headers]
    for i in range(count):
        flags, off, size, _ = struct.unpack_from("<QQQQ", s, 32 + i * 32)
        if not flags & BLOCKED:
            continue
        if flags & (ENCRYPTED | COMPRESSED):
            raise ValueError("encrypted or compressed SELF")
        index = (flags >> 20) & 0xFFF
        if index >= phnum or size != phdrs[index][5]:
            raise ValueError(f"malformed SELF segment {i}")
        out[phdrs[index][2]:phdrs[index][2] + size] = s[off:off + size]
    return bytes(out)


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    open(sys.argv[2], "wb").write(unwrap(data))
    print("wrote", sys.argv[2])
