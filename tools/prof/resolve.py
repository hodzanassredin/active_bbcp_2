#!/usr/bin/env python3
"""Resolve bbprof samples against the PROFMAP lines printed by Dev_devProfMap.Map.

usage: resolve.py samples.bin console-output.txt [top]
"""
import bisect
import collections
import struct
import sys

samples_path, out_path = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 30

mods = []  # (code, size, name, [(end, proc)])
for line in open(out_path, encoding="utf-8", errors="replace"):
    p = line.split()
    if len(p) >= 5 and p[0] == "PROFMAP" and p[1] == "M":
        mods.append((int(p[3]), int(p[4]), p[2], []))
    elif len(p) >= 4 and p[0] == "PROFMAP" and p[1] == "P" and mods:
        mods[-1][3].append((int(p[2]), p[3]))
mods.sort()
starts = [m[0] for m in mods]

data = open(samples_path, "rb").read()
pcs = struct.unpack("<%dQ" % (len(data) // 8), data[: len(data) // 8 * 8])
by_proc = collections.Counter()
by_mod = collections.Counter()
for pc in pcs:
    i = bisect.bisect_right(starts, pc) - 1
    if i >= 0 and pc < mods[i][0] + mods[i][1]:
        code, _size, name, procs = mods[i]
        off = pc - code
        ends = sorted(procs)
        j = bisect.bisect_right([e for e, _ in ends], off)
        proc = ends[j][1] if j < len(ends) else "?"
        by_proc[name + "." + proc] += 1
        by_mod[name] += 1
    else:
        key = "(вне модулей: libc, ядро ОС)"
        by_proc[key] += 1
        by_mod[key] += 1

n = len(pcs) or 1
print("выборок: %d" % len(pcs))
print("\nпо модулям:")
for k, v in by_mod.most_common(12):
    print("  %5.1f%%  %6d  %s" % (100.0 * v / n, v, k))
print("\nпо процедурам:")
for k, v in by_proc.most_common(top):
    print("  %5.1f%%  %6d  %s" % (100.0 * v / n, v, k))
