#!/bin/bash
export PATH=/opt/cross-mint/bin:$PATH
cd /cygdrive/c/claude/cannonball
CPU=68030 EXTRA="-msoft-float -DPLATFORM_FALCON -DPERF_PRINT -DMARK_FILE" LINKCPU=68000 OUT=CBMARK.TOS bash ./build_atari.sh
