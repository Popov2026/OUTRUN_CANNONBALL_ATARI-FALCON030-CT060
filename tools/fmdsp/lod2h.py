#!/usr/bin/env python3
"""Converts a56 output ("P 0040 0AF080" lines) into a C header holding the program in the
Dsp_ExecProg() binary format: blocks of (memory space 0=P 1=X 2=Y, start address, word count,
words...), every value as 3 bytes, most significant first.
Usage: lod2h.py fm_dsp.lod ../../src/main/atari/fm_dsp_p56.h fm_dsp_p56"""
import sys
src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]
words = {}
for line in open(src):
    p = line.split()
    if len(p) == 3 and p[0] in ("P", "X", "Y"):
        words[(p[0], int(p[1], 16))] = int(p[2], 16)
blocks = []
for (sp, addr) in sorted(words, key=lambda k: ("PXY".index(k[0]), k[1])):
    if blocks and blocks[-1][0] == sp and blocks[-1][1] + len(blocks[-1][2]) == addr:
        blocks[-1][2].append(words[(sp, addr)])
    else:
        blocks.append([sp, addr, [words[(sp, addr)]]])
out = []
for sp, addr, data in blocks:
    out += ["PXY".index(sp), addr, len(data)] + data
    assert sp != "P" or addr + len(data) <= 0x1000, "program must stay below P:$1000"
b = []
for w in out:
    b += [(w >> 16) & 255, (w >> 8) & 255, w & 255]
with open(dst, "w") as f:
    f.write("// DSP56001 program %s (tools/fmdsp/fm_dsp.asm, assembled with a56, converted by lod2h.py).\n" % name)
    f.write("// Dsp_ExecProg() binary format. Generated file, do not edit.\n")
    f.write("static const unsigned char %s[] = {\n" % name)
    for i in range(0, len(b), 12):
        f.write("    " + ",".join("0x%02X" % x for x in b[i:i + 12]) + ",\n")
    f.write("};\n#define %s_WORDS %d\n" % (name.upper(), len(out)))
print(dst, len(blocks), "blocks,", len(out), "words, highest P address $%X" % max(a + len(d) for s, a, d in blocks if s == "P"))
