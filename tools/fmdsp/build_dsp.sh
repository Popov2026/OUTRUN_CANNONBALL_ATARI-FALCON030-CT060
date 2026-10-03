#!/bin/bash
# Assembles fm_dsp.asm with a56 (Debian/Ubuntu package "a56") and regenerates
# src/main/atari/fm_dsp_p56.h. Stops on any a56 error or warning (pipeline hazards included).
set -e
cd "$(dirname "$0")"
OUT=$(a56 -o /tmp/fm_dsp.lod fm_dsp.asm 2>&1)
echo "$OUT" | grep -E "^errors=|^warnings="
if ! echo "$OUT" | grep -q "^errors=0" || ! echo "$OUT" | grep -q "^warnings=0"; then
  echo "$OUT" | grep -B1 -A1 -E "line [0-9]+:"; exit 1
fi
python3 lod2h.py /tmp/fm_dsp.lod ../../src/main/atari/fm_dsp_p56.h fm_dsp_p56
