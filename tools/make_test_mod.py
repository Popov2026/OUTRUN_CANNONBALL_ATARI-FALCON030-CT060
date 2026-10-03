#!/usr/bin/env python3
"""Writes TEST.MOD: a minimal 4-voice ProTracker module (no copyrighted content) that holds
one 440 Hz sine note on voice 1 forever. Sample = one 32-byte sine cycle, looped; Amiga period
252 -> 3546895 / 252 = 14075 bytes/s -> 14075 / 32 = 440 Hz. Used by the DSPMOD Hatari test."""
import math, struct, sys

out = sys.argv[1] if len(sys.argv) > 1 else "TEST.MOD"
sample = bytes((int(round(100 * math.sin(2 * math.pi * i / 32))) & 0xff) for i in range(32))

hdr = b"CB-TEST".ljust(20, b"\0")
for n in range(31):
    if n == 0:   # name, length in words, finetune, volume, loop start, loop length (words)
        hdr += b"sine440".ljust(22, b"\0") + struct.pack(">HBBHH", len(sample) // 2, 0, 64, 0, len(sample) // 2)
    else:
        hdr += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
hdr += bytes([1, 127]) + bytes(128) + b"M.K."   # song length 1, order table (pattern 0), tag

pattern = bytearray(64 * 4 * 4)
period, smp = 252, 1
pattern[0:4] = bytes([(smp & 0xf0) | (period >> 8), period & 0xff, (smp & 0x0f) << 4, 0])
open(out, "wb").write(hdr + bytes(pattern) + sample)
print("wrote", out, len(hdr) + len(pattern) + len(sample), "bytes")
