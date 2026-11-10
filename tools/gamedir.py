"""Where the user's own game dump lives (same rules as lbp3.exe's GameDir).

LBP3_GAME_DIR, else the first line of game_dir.txt in the project root, else
"LBP3 Game Files" next to the project. The folder holds CUSA00473 (base game)
and, unless the two are merged, CUSA00473-patch.
"""
import os
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent


def game_dir():
    if os.environ.get("LBP3_GAME_DIR"):
        return pathlib.Path(os.environ["LBP3_GAME_DIR"])
    cfg = ROOT / "game_dir.txt"
    if cfg.exists():
        lines = cfg.read_text().splitlines()
        if lines and lines[0].strip():
            return pathlib.Path(lines[0].strip())
    return ROOT.parent / "LBP3 Game Files"


def eboot():
    """The eboot lbp3.exe runs: the patch folder's if there is one, else the base folder's
    (a dump with base and patch merged has only CUSA00473)."""
    for sub in ("CUSA00473-patch", "CUSA00473"):
        path = game_dir() / sub / "eboot.bin"
        if path.exists():
            return path
    return game_dir() / "CUSA00473" / "eboot.bin"


def eboot_elf():
    """The eboot as a plain ELF image. A fake-signed SELF (as in a dump made from a console) is
    rebuilt the way the loader does it (runtime/src/core/module.cpp, UnwrapSelf)."""
    s = eboot().read_bytes()
    if s[:4] != b"\x4f\x15\x3d\x1d":
        return s
    count, = struct.unpack_from("<H", s, 24)
    elf = 32 + count * 32
    if s[elf:elf + 4] != b"\x7fELF":
        raise ValueError("SELF holds no ELF header")
    phoff, = struct.unpack_from("<Q", s, elf + 32)
    phnum, = struct.unpack_from("<H", s, elf + 56)
    headers = phoff + phnum * 56
    phdrs = [struct.unpack_from("<IIQQQQQQ", s, elf + phoff + i * 56) for i in range(phnum)]
    out = bytearray(max([headers] + [p[2] + p[5] for p in phdrs]))
    out[:headers] = s[elf:elf + headers]
    for i in range(count):
        flags, off, size, _ = struct.unpack_from("<QQQQ", s, 32 + i * 32)
        if not flags & 0x800:  # not a blocked segment
            continue
        if flags & (0x2 | 0x8):
            raise ValueError("encrypted or compressed SELF")
        index = (flags >> 20) & 0xFFF
        if index >= phnum or size != phdrs[index][5]:
            raise ValueError(f"malformed SELF segment {i}")
        out[phdrs[index][2]:phdrs[index][2] + size] = s[off:off + size]
    return bytes(out)
