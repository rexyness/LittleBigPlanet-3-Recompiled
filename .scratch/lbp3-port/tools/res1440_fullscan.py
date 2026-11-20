"""Find every GCN shader in LBP3's game data and the eboot, and list those whose float
literals equal a 1080p render size (or its reciprocal), the same test as the 1440p shader
swap in shadPS4's recompiler (Lbp3ResFactor; perf/res1440_shader_scan.md).

shadPS4 keys a shader by the hash in its "OrbShdr" footer (regs_shader.h BinaryInfo), which
Sony's compiler writes into the binary, so the hashes found here are the ones the
recompiler sees. Game shaders sit inside LBP resources (GMTb materials etc.), which are
zlib-compressed in chunks, inside the .farc archives (read-only game dump).

  python res1440_fullscan.py <out dir>      writes shaders.json, hits.json and the binaries
"""
import glob
import json
import os
import struct
import sys
import zlib

GAME = r'D:\Recomps\ROMs\PS4\CUSA00473'
EBOOT = r'D:\Recomps\lbp3-recompiled\analysis\eboot.elf'
ENTRY = struct.Struct('>20sII')
SIG = b'OrbShdr'
SIZES = [1920, 1080, 960, 540, 480, 270, 3840, 2160, 7680, 4320]


def farc_index(path):
    """FARC (the big archives): table, count, magic. FAR4 (level archives): table, a 20-byte
    hash, count, magic. Both use (sha1[20], offset u32, size u32) big-endian entries."""
    with open(path, 'rb') as f:
        f.seek(-8, 2)
        n, magic = struct.unpack('>I4s', f.read(8))
        extra = {b'FARC': 0, b'FAR4': 20}[magic]
        f.seek(-8 - extra - n * ENTRY.size, 2)
        t = f.read(n * ENTRY.size)
    return [ENTRY.unpack_from(t, i * ENTRY.size) for i in range(n)]


def payload(d):
    """The decompressed body of an LBP resource, or d itself if it isn't one. Two layouts:
    binary resources (magic ending in 'b': header, then the chunk table), and the
    compiled-program blobs that start 00 01 (u16 1, then the chunk table straight away),
    which hold most of the engine's own shaders (post-processing, SMAA, the composite)."""
    if len(d) >= 8 and d[:2] == b'\x00\x01':
        end, offsets = len(d), (2,)
    elif len(d) >= 24 and d[3:4] == b'b':
        dep = struct.unpack('>I', d[8:12])[0]
        end, offsets = (dep if 0 < dep <= len(d) else len(d)), range(12, 28)
    else:
        return d
    for o in offsets:
        if o + 2 > end:
            break
        n = struct.unpack('>H', d[o:o + 2])[0]
        if not 0 < n < 4096:
            continue
        s = o + 2 + 4 * n
        if s > end:
            continue
        pairs = [struct.unpack('>HH', d[o + 2 + 4 * i:o + 6 + 4 * i]) for i in range(n)]
        comp = sum(c for c, _ in pairs)
        if s + comp != end:
            continue
        out, pos = [], s
        try:
            for c, u in pairs:
                chunk = d[pos:pos + c]
                pos += c
                if c == u:
                    out.append(chunk)
                else:
                    z = zlib.decompress(chunk)
                    if len(z) != (u or 0x10000):
                        raise ValueError
                    out.append(z)
        except Exception:
            continue
        return b''.join(out)
    return d


def shaders_in(buf):
    """(start, length, hash, type) for each OrbShdr footer with its code located."""
    res = []
    i = buf.find(SIG)
    while i >= 0:
        if i + 28 <= len(buf):
            word = struct.unpack_from('<I', buf, i + 8)[0]
            length = word >> 8
            stype = (word >> 2) & 0xF
            h = struct.unpack_from('<Q', buf, i + 16)[0]
            start = None
            # Sony's shaders open with s_mov_b32 vcc_hi, <dwords to the footer / 2 - 1>
            # (regs_shader.h SearchBinaryInfo): find the start that points at this footer.
            lo = max(0, i - max(length, 4) - 0x10000)
            j = buf.rfind(b'\xff\x03\xeb\xbe', lo, i)
            while j >= 0:
                if j % 4 == i % 4 or True:
                    k = struct.unpack_from('<I', buf, j + 4)[0]
                    if j + (k + 1) * 8 == i:
                        start = j
                        break
                j = buf.rfind(b'\xff\x03\xeb\xbe', lo, j)
            if start is None and length and i - length >= 0:
                start = i - length
            if start is not None and length:
                res.append((start, length, h, stype))
        i = buf.find(SIG, i + 1)
    return res


def factor(f):
    import math
    if not math.isfinite(f) or f <= 0:
        return None
    for v in SIZES:
        if abs(f - v) <= v * 2e-6:
            return '%d' % v
        if abs(f - 1.0 / v) <= (1.0 / v) * 2e-6:
            return '1/%d' % v
    return None


def literals(code):
    """Literal constants of a GCN (Sea Islands) program: (dword index, value)."""
    w = struct.unpack_from('<%dI' % (len(code) // 4), code)
    out, i, n = [], 0, len(w)
    while i < n:
        x = w[i]
        size, lit = 1, False
        top9 = x >> 23
        if top9 == 0x17D:                      # SOP1
            lit = (x & 0xFF) == 0xFF
        elif top9 == 0x17E:                    # SOPC
            lit = (x & 0xFF) == 0xFF or ((x >> 8) & 0xFF) == 0xFF
        elif top9 == 0x17F:                    # SOPP
            if ((x >> 16) & 0x7F) == 1:        # s_endpgm
                pass
        elif (x >> 28) == 0xB:                 # SOPK
            pass
        elif (x >> 30) == 0x2:                 # SOP2
            lit = (x & 0xFF) == 0xFF or ((x >> 8) & 0xFF) == 0xFF
        elif (x >> 27) == 0x18:                # SMRD
            lit = ((x >> 8) & 1) == 0 and (x & 0xFF) == 0xFF
        elif (x >> 31) == 0:                   # VOP2 / VOP1 / VOPC
            op2 = (x >> 25) & 0x3F
            lit = (x & 0x1FF) == 0xFF or (op2 in (0x20, 0x21) and (x >> 25) != 0x3E and (x >> 25) != 0x3F)
        elif (x >> 26) == 0x32:                # VINTRP
            pass
        elif (x >> 26) in (0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E):  # VOP3 DS MUBUF MTBUF MIMG EXP
            size = 2
        else:
            size = 1
        if lit and i + 1 < n:
            out.append((i + 1, w[i + 1]))
            size += 1
        i += size
    return out


def main():
    outdir = sys.argv[1]
    os.makedirs(os.path.join(outdir, 'bin'), exist_ok=True)
    found = {}   # hash -> info
    sources = []
    for path in sorted(glob.glob(os.path.join(GAME, '*.farc')) +
                       glob.glob(os.path.join(GAME, 'gamedata', '**', '*.farc'), recursive=True)):
        sources.append(('farc', path))
    for path in glob.glob(os.path.join(GAME, 'gamedata_orbis', '**', '*'), recursive=True):
        if os.path.isfile(path):
            sources.append(('file', path))
    sources.append(('file', EBOOT))
    nres = 0
    for kind, path in sources:
        rel = os.path.relpath(path, GAME) if path.startswith(GAME) else os.path.basename(path)
        def items():
            if kind == 'farc':
                with open(path, 'rb') as f:
                    for sha, off, size in farc_index(path):
                        f.seek(off)
                        yield sha.hex(), f.read(size)
            else:
                with open(path, 'rb') as f:
                    yield '-', f.read()
        for sha, d in items():
            nres += 1
            body = payload(d)
            if SIG not in body:
                continue
            for start, length, h, stype in shaders_in(body):
                code = body[start:start + length]
                key = '%016x' % h
                if key in found:
                    found[key]['copies'] += 1
                    continue
                found[key] = {'hash': key, 'type': stype, 'length': length, 'magic': d[:4].decode('latin1'),
                              'source': rel, 'sha1': sha, 'copies': 1}
                with open(os.path.join(outdir, 'bin', key + '.bin'), 'wb') as g:
                    g.write(code)
        print('%-60s %6d resources so far, %5d unique shaders' % (rel[:60], nres, len(found)), flush=True)
    hits = {}
    for key, info in found.items():
        code = open(os.path.join(outdir, 'bin', key + '.bin'), 'rb').read()
        lits = []
        for idx, v in literals(code):
            f = struct.unpack('<f', struct.pack('<I', v))[0]
            n = factor(f)
            if n:
                lits.append((idx, '%08x' % v, n))
        if lits:
            hits[key] = dict(info, literals=lits)
    json.dump(found, open(os.path.join(outdir, 'shaders.json'), 'w'), indent=1)
    json.dump(hits, open(os.path.join(outdir, 'hits.json'), 'w'), indent=1)
    print('%d unique shaders, %d with 1080p-size float literals' % (len(found), len(hits)))


if __name__ == '__main__':
    main()
