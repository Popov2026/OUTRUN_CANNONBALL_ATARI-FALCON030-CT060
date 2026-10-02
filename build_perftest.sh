#!/bin/bash
export PATH=/opt/cross-mint/bin:$PATH
cd /cygdrive/c/claude/cannonball
CPU=68060 EXTRA="-msoft-float -DPLATFORM_FALCON -DPERF_PRINT -DLOGIC50_FILE" LINKCPU=68000 OUT=CBPERF.TOS bash ./build_atari.sh
