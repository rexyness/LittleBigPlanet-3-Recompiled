"""Which of LBP3's shaders use the GCN instructions behind four recompiler fixes we lack
(perf/seth_ports.md, Batch 2 item 4; research/sethlikepie_gpu.md section 5):

  exec_and   s_and_b64 exec, sA, sB with neither source exec and no s_wqm_b64 exec, exec right
             after (sethlikepie's F91: upstream opens no divergence scope for it, so the VALU code
             after it runs unmasked in ours)
  writelane v_writelane_b32 (VOP2 2 / VOP3 258: ours writes zero to the whole register, sgdc3 eb51f01)
  rcp_clamp v_rcp_clamp_f32, rsq_clamp v_rsq_clamp_f32 (eb51f01: +-FLT_MAX instead of infinity)
  rcp_legacy, rsq_legacy, log_clamp, log_legacy (upstream #5346: 0 -> 0 and the clamp)

over the shader binaries res1440_fullscan.py extracted (the clone's analysis/res1440_scan/bin), and
which of the hits the game actually ran in our test routes (a .meta/.spv of that hash in a shader
cache folder, e.g. analysis/seth_cache/CUSA00473).

  python opcode_scan.py <bin dir> <shaders.json> <cache dir> [out.json]
"""
import json
import os
import struct
import sys

EXEC = 126
VOP1 = {38: 'log_clamp', 40: 'rcp_clamp', 41: 'rcp_legacy', 44: 'rsq_clamp', 45: 'rsq_legacy', 69: 'log_legacy'}
TYPES = {0: 'cs', 1: 'ps', 2: 'vs', 3: 'gs', 4: 'es', 5: 'hs', 6: 'ls'}


def instructions(code):
    """(dword index, first dword, second dword or None, encoding) for each instruction, sized as
    res1440_fullscan.py's literals() walks them (Sea Islands)."""
    w = struct.unpack_from('<%dI' % (len(code) // 4), code)
    i, n = 0, len(w)
    while i < n:
        x = w[i]
        size, lit, enc = 1, False, '?'
        top9 = x >> 23
        if top9 == 0x17D:
            enc, lit = 'sop1', (x & 0xFF) == 0xFF
        elif top9 == 0x17E:
            enc, lit = 'sopc', (x & 0xFF) == 0xFF or ((x >> 8) & 0xFF) == 0xFF
        elif top9 == 0x17F:
            enc = 'sopp'
            if ((x >> 16) & 0x7F) == 1:        # s_endpgm: what follows is the binary's metadata
                yield i, x, None, enc
                return
        elif (x >> 28) == 0xB:
            enc = 'sopk'
        elif (x >> 30) == 0x2:
            enc, lit = 'sop2', (x & 0xFF) == 0xFF or ((x >> 8) & 0xFF) == 0xFF
        elif (x >> 27) == 0x18:
            enc, lit = 'smrd', ((x >> 8) & 1) == 0 and (x & 0xFF) == 0xFF
        elif (x >> 31) == 0:
            op2 = (x >> 25) & 0x3F
            enc = 'vop1' if (x >> 25) == 0x3F else 'vopc' if (x >> 25) == 0x3E else 'vop2'
            lit = (x & 0x1FF) == 0xFF or (op2 in (0x20, 0x21) and (x >> 25) != 0x3E and (x >> 25) != 0x3F)
        elif (x >> 26) == 0x32:
            enc = 'vintrp'
        elif (x >> 26) in (0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E):
            enc = {0x34: 'vop3', 0x36: 'ds', 0x38: 'mubuf', 0x3A: 'mtbuf', 0x3C: 'mimg', 0x3E: 'exp'}[x >> 26]
            size = 2
        if lit:
            size += 1
        yield i, x, (w[i + 1] if i + 1 < n else None), enc
        i += size


def scan(code):
    found = {}
    prev = None
    pending_exec = None
    for i, x, y, enc in instructions(code):
        if pending_exec is not None:
            # s_wqm_b64 exec, exec right after: the discard sequence, left out (as sethlikepie)
            wqm = enc == 'sop1' and ((x >> 8) & 0xFF) == 10 and ((x >> 16) & 0x7F) == EXEC and (x & 0xFF) == EXEC
            if not wqm:
                found['exec_and'] = found.get('exec_and', 0) + 1
            pending_exec = None
        if enc == 'sop2':
            op, sdst, s1, s0 = (x >> 23) & 0x7F, (x >> 16) & 0x7F, (x >> 8) & 0xFF, x & 0xFF
            if op == 15 and sdst == EXEC and s0 != EXEC and s1 != EXEC:
                pending_exec = i
        elif enc == 'vop2':
            if ((x >> 25) & 0x3F) == 2:
                found['writelane'] = found.get('writelane', 0) + 1
        elif enc == 'vop1':
            name = VOP1.get((x >> 9) & 0xFF)
            if name:
                found[name] = found.get(name, 0) + 1
        elif enc == 'vop3':
            op = (x >> 17) & 0x1FF
            if op == 258:
                found['writelane'] = found.get('writelane', 0) + 1
            elif op >= 384 and VOP1.get(op - 384):
                name = VOP1[op - 384]
                found[name] = found.get(name, 0) + 1
        prev = enc
    if pending_exec is not None:
        found['exec_and'] = found.get('exec_and', 0) + 1
    return found


def main():
    bindir, shaders_json, cache = sys.argv[1:4]
    out = sys.argv[4] if len(sys.argv) > 4 else None
    info = json.load(open(shaders_json))
    seen = set()
    for name in os.listdir(cache):
        if name.startswith('0x'):
            seen.add(name[2:18])
    totals, ran, hits = {}, {}, {}
    for name in sorted(os.listdir(bindir)):
        if not name.endswith('.bin'):
            continue
        key = name[:-4]
        found = scan(open(os.path.join(bindir, name), 'rb').read())
        if not found:
            continue
        hits[key] = dict(found, type=TYPES.get(info.get(key, {}).get('type'), '?'),
                         source=info.get(key, {}).get('source', '?'), ran=key in seen)
        for k in found:
            totals[k] = totals.get(k, 0) + 1
            if key in seen:
                ran.setdefault(k, []).append(key)
    print('%d shaders scanned, %d with at least one; %d hashes in the cache folder' %
          (len(info), len(hits), len(seen)))
    for k in sorted(totals):
        r = ran.get(k, [])
        print('  %-10s %6d shaders; %4d of them ran in the cached routes: %s' %
              (k, totals[k], len(r), ' '.join(sorted(r)[:12]) + (' ...' if len(r) > 12 else '')))
    if out:
        json.dump(hits, open(out, 'w'), indent=1)


if __name__ == '__main__':
    main()
