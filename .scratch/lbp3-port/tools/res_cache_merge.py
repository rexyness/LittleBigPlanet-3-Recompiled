"""Merge the old separate 1440p shader cache into the shared one (perf/shared_cache.md).

Until fix/shared-cache, LBP3_RES=1440 + LBP3_RES_SHADERS=1 kept the whole shadPS4 cache in
user\\shader_cache\\CUSA00473_res1440. With the shared cache, 1440p uses CUSA00473 and only the rescaled
shaders get their own (salted) names. This tool carries the 1440p-only work over, so the
pipelines first built at 1440p don't compile once more:

  res_cache_merge.py <res1440 folder> <main folder> --out <new folder> [--apply]
                     [--xxh3tool <xxh3tool.exe>]

It never writes to either input folder: --out gets a copy of <main folder> plus what is safe to
add. Without --apply it only reports. Swap the result in by hand (rename, keeping the old folders)
once it has been checked in game.

shadPS4's cache, per folder:
- <pgm>_<idx>.spv: SPIR-V of permutation idx of program pgm (the GCN shader hash);
- <perm>.meta, perm = HashCombine(pgm, idx): u32 meta version, u32 binary version, u64 perm,
  u64 idx, then the specialization and info (with session-dependent addresses);
- g_<XXH3_64(key)>.key: u32 version, u32 0, the 344-byte GraphicsPipelineKey (its first 6 u64 are
  the stage perm hashes, by SwStage), then vertex data; c_<perm>.key: u32 version, u32 1, u64 perm.
Permutation indices are given in discovery order, so the same program can hold the same
translation at different indices in the two folders. Programs are therefore matched by content:
- one of the rescaled shaders (the list in shadPS4's common/lbp3_res.h, 44 since the full scan): skipped (here they are the rescaled translations under their
  original names, which the shared cache keeps for 1080p); keys using them are skipped and
  compile once more at 1440p;
- absent from <main>: copied as is;
- present in both: each res1440 permutation whose SPIR-V equals one of <main>'s maps to that
  index; any other is appended after <main>'s (spv copied, meta renamed with perm/idx rewritten).
Keys get their stage perm hashes rewritten through that map and are renamed by XXH3 of the new
key (computed with xxh3tool.exe; checked first against every existing key name). Nothing in
<main> is ever overwritten.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

# The rescaled shaders: by default read from shadPS4's common/lbp3_res.h (the one list for the
# rescale and the cache salt; --list), so this tool can't drift from the build. The default is the
# clone's submodule, which has the header once fix/shared-cache is merged; before that, pass the
# worktree's (D:\Recomps\lbp3-wt\scache-shadps4\src\common\lbp3_res.h).
DEFAULT_LIST = os.path.join('D:' + os.sep, 'Recomps', 'lbp3-recompiled', 'third_party', 'shadPS4',
                            'src', 'common', 'lbp3_res.h')


def read_list(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    body = text.split('kShaders', 1)[1].split('};', 1)[0]
    found = {int(h, 16) for h in re.findall(r'0x([0-9a-f]{16})ull', body)}
    m = re.search(r'std::array<u64,\s*(\d+)>\s*kShaders', text)
    if not found or (m and int(m.group(1)) != len(found)):
        sys.exit(f'could not read the shader list from {path}')
    return found


M64 = (1 << 64) - 1
KEY_SIZE = 344      # sizeof(GraphicsPipelineKey), checked against the key names
STAGES = 6          # MaxShaderStages (SwStage::NumLogicalStages)
SPV = re.compile(r'^(0x[0-9a-f]{16})_(\d+)\.spv$')
META = re.compile(r'^(0x[0-9a-f]{16})\.meta$')
KEY = re.compile(r'^([gc])_(0x[0-9a-f]{16})\.key$')


def hash_combine(seed, h):
    """shadPS4 common/hash.h HashCombine(u64, u64)."""
    return (seed ^ ((h + 0x9e3779b9 + ((seed << 12) & M64) + (seed >> 4)) & M64)) & M64


def scan(folder):
    progs, metas, keys, other = {}, {}, set(), []
    for name in os.listdir(folder):
        m = SPV.match(name)
        if m:
            progs.setdefault(int(m.group(1), 16), {})[int(m.group(2))] = name
            continue
        m = META.match(name)
        if m:
            metas[int(m.group(1), 16)] = name
            continue
        if KEY.match(name):
            keys.add(name)
            continue
        other.append(name)
    return progs, metas, keys, other


def read(folder, name):
    with open(os.path.join(folder, name), 'rb') as f:
        return f.read()


def xxh3_many(tool, blobs):
    with tempfile.TemporaryDirectory() as td:
        rec = os.path.join(td, 'rec.bin')
        with open(rec, 'wb') as f:
            for b in blobs:
                f.write(struct.pack('<I', len(b)))
                f.write(b)
        out = subprocess.run([tool, rec], capture_output=True, text=True, check=True).stdout.split()
    if len(out) != len(blobs):
        sys.exit('xxh3tool returned the wrong number of hashes')
    return [int(h, 16) for h in out]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('res1440')
    ap.add_argument('main')
    ap.add_argument('--out', required=True)
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--list', default=DEFAULT_LIST,
                    help="shadPS4's common/lbp3_res.h (the rescaled shader list)")
    ap.add_argument('--xxh3tool', default=os.path.join(os.path.dirname(__file__), 'xxh3tool.exe'))
    a = ap.parse_args()
    src, dst, out = a.res1440, a.main, a.out
    for p in (src, dst):
        if not os.path.isdir(p):
            sys.exit(f'not a folder: {p}')
    if os.path.abspath(out) in (os.path.abspath(src), os.path.abspath(dst)):
        sys.exit('--out must be a new folder, not one of the inputs')
    a.xxh3tool = os.path.abspath(a.xxh3tool)
    if not os.path.exists(a.xxh3tool):
        sys.exit(f'xxh3tool not found: {a.xxh3tool} (build tools/xxh3tool.cpp)')

    listed = read_list(a.list)
    print(f'rescaled shaders: {len(listed)} (from {a.list})')
    s_progs, s_metas, s_keys, s_other = scan(src)
    d_progs, d_metas, d_keys, d_other = scan(dst)
    prof_s = read(src, 'profile.bin') if 'profile.bin' in s_other else None
    prof_d = read(dst, 'profile.bin') if 'profile.bin' in d_other else None
    print(f'profile.bin identical: {prof_s == prof_d}')
    if prof_s != prof_d:
        print('  the profiles differ (another device or settings); stopping')
        return 1

    # The key-name hash, checked on every graphics key of both folders first.
    for folder, keys in ((src, s_keys), (dst, d_keys)):
        g = sorted(k for k in keys if k.startswith('g_'))
        hs = xxh3_many(a.xxh3tool, [read(folder, k)[8:8 + KEY_SIZE] for k in g])
        bad = [k for k, h in zip(g, hs) if int(k[2:-4], 16) != h]
        print(f'{os.path.basename(folder)}: {len(g) - len(bad)}/{len(g)} key names = XXH3(key)')
        if bad:
            sys.exit('key layout not as expected; stopping')

    remap = {}       # res1440 perm hash -> perm hash in the merged cache (None = unusable)
    add_files = {}   # merged-cache name -> bytes
    stats = dict(listed=0, new_prog=0, shared_prog=0, same_perm=0, appended_perm=0)
    for pgm, perms in s_progs.items():
        if pgm in listed:
            stats['listed'] += 1
            for idx in perms:
                remap[hash_combine(pgm, idx)] = None
            continue
        if pgm not in d_progs:
            stats['new_prog'] += 1
            for idx, spv in perms.items():
                ph = hash_combine(pgm, idx)
                remap[ph] = ph
                add_files[spv] = read(src, spv)
                if ph in s_metas:
                    add_files[s_metas[ph]] = read(src, s_metas[ph])
            continue
        stats['shared_prog'] += 1
        dperms = d_progs[pgm]
        by_content = {read(dst, f): i for i, f in dperms.items()}
        next_idx = max(dperms) + 1
        for idx in sorted(perms):
            ph = hash_combine(pgm, idx)
            spv = read(src, perms[idx])
            if spv in by_content:
                stats['same_perm'] += 1
                remap[ph] = hash_combine(pgm, by_content[spv])
                continue
            if ph not in s_metas:
                remap[ph] = None
                continue
            new = next_idx
            next_idx += 1
            nph = hash_combine(pgm, new)
            meta = bytearray(read(src, s_metas[ph]))
            struct.pack_into('<QQ', meta, 8, nph, new)
            add_files[f'{pgm:#018x}_{new}.spv'] = spv
            add_files[f'{nph:#018x}.meta'] = bytes(meta)
            by_content[spv] = new
            remap[ph] = nph
            stats['appended_perm'] += 1

    # Keys.
    key_stats = dict(kept_main=0, added=0, unusable=0, already_in_main=0)
    g_new = []
    for name in sorted(s_keys):
        kind = name[0]
        data = bytearray(read(src, name))
        if kind == 'c':
            ph = struct.unpack_from('<Q', data, 8)[0]
            nph = remap.get(ph)
            if nph is None:
                key_stats['unusable'] += 1
                continue
            struct.pack_into('<Q', data, 8, nph)
            new_name = f'c_{nph:#018x}.key'
            if new_name in d_keys:
                key_stats['already_in_main'] += 1
            else:
                add_files[new_name] = bytes(data)
                key_stats['added'] += 1
            continue
        ok = True
        for s in range(STAGES):
            ph = struct.unpack_from('<Q', data, 8 + 8 * s)[0]
            if not ph:
                continue
            nph = remap.get(ph)
            if nph is None:
                ok = False
                break
            struct.pack_into('<Q', data, 8 + 8 * s, nph)
        if not ok:
            key_stats['unusable'] += 1
            continue
        g_new.append(bytes(data))
    for data, h in zip(g_new, xxh3_many(a.xxh3tool, [d[8:8 + KEY_SIZE] for d in g_new])):
        new_name = f'g_{h:#018x}.key'
        if new_name in d_keys:
            key_stats['already_in_main'] += 1
        else:
            add_files[new_name] = data
            key_stats['added'] += 1

    clobber = [n for n in add_files if os.path.exists(os.path.join(dst, n))]
    print(f'res1440: {len(s_progs)} programs, {len(s_metas)} metas, {len(s_keys)} keys')
    print(f'main:    {len(d_progs)} programs, {len(d_metas)} metas, {len(d_keys)} keys')
    print('programs:', stats)
    print('keys:', key_stats, '(unusable = using a rescaled shader: compiles once more at 1440p)')
    print(f'files to add: {len(add_files)}; clashing with an existing name in main: {len(clobber)}')
    if clobber:
        print('  clashes found, stopping:', clobber[:5])
        return 1
    if not a.apply:
        print('(report only; pass --apply to write --out)')
        return 0
    if os.path.exists(out):
        sys.exit(f'--out exists already: {out}')
    shutil.copytree(dst, out)
    for n, b in add_files.items():
        with open(os.path.join(out, n), 'xb') as f:
            f.write(b)
    print(f'wrote {out}: main plus {len(add_files)} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
