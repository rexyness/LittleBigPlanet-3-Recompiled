r"""Milestone 5: an F10 report (analysis\perf\perfNN.txt) or LBP3_SAMPLE output, by category.

    python -I perf_cats.py <perf.txt | stamped log> [...]

The sampler prints only each thread's top 15 host chains, so "covered" says how many of the
3000 samples the categories account for; game-code samples are counted separately.
"""
import re
import sys
from collections import Counter


def cat(c):
    if 'GuestFaultSignalHandler' in c or 'UpdatePageWatchersForRegion' in c or 'InvalidateMemory' in c:
        return 'write-tracking fault (InvalidateMemory / page protect)'
    if 'WaitGfxIdleOrBlocked' in c: return 'wait: graphics queue (submit / frame pipeline)'
    if 'WaitGpuIdleOrBlocked' in c: return 'wait: GPU idle'
    if 'SemaWait' in c: return 'wait: sceKernelWaitSema'
    if 'CondWait' in c: return 'wait: pthread cond'
    if 'fprintf' in c: return 'log fprintf (stderr)'
    if 'ReadImpl' in c: return 'file read (ReadImpl: ReadFile / copy)'
    if 'OpenImpl' in c or 'sceKernelStat' in c: return 'file open / stat'
    if 'WaitFlipSlot' in c: return 'flip slot wait (pacing)'
    if 'Liverpool::Process' in c and 'CondvarWait' in c: return 'command processor idle'
    if 'ReadbackDisplayBuffer' in c: return 'display readback copy'
    if 'nvoglv64' in c and 'WaitForSynchronizationObject' in c: return 'driver: fence wait (GPU running)'
    if 'DestroyAllocation' in c: return 'driver: destroy allocation'
    if 'CreateAllocation' in c: return 'driver: create allocation'
    if 'nvoglv64' in c: return 'driver: other'
    if 'TextureCache' in c or 'Image::' in c or 'Upload' in c: return 'texture cache'
    if 'BufferCache' in c: return 'buffer cache'
    if 'Rasterizer' in c or 'Liverpool' in c: return 'command processor recording (other)'
    if 'scePthreadYield' in c or 'SwitchToThread' in c: return 'yield'
    if 'Sleep' in c or 'Usleep' in c or 'NtDelayExecution' in c: return 'sleep'
    return 'other: ' + c.split(' < ')[0][:50]


for path in sys.argv[1:]:
    txt = open(path, encoding='utf-8', errors='replace').read()
    txt = '\n'.join(l.split('\t', 1)[-1] for l in txt.splitlines())
    for part in re.split(r"(?=\[sample\] '[^']+': \d+ of)", txt):
        m = re.match(r"\[sample\] '([^']+)': (\d+) of (\d+)", part)
        if not m:
            continue
        c = Counter()
        for h in re.finditer(r"\[sample\] host x(\d+): (.*)", part):
            c[cat(h.group(2))] += int(h.group(1))
        ing, tot = int(m.group(2)), int(m.group(3))
        print(f"{path}: '{m.group(1)}' {tot} samples: {ing} in game code, {sum(c.values())} in its top host chains")
        for k, v in c.most_common(12):
            print(f"   {v:5d}  {k}")
