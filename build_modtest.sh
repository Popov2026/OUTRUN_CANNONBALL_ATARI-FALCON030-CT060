#!/bin/bash
export PATH=/opt/cross-mint/bin:$PATH
cd /cygdrive/c/claude/cannonball
CPU=68060 EXTRA="-msoft-float -DPLATFORM_FALCON -DMOD_MUSIC -DMOD_MUSIC_LOG -DPERF_PRINT -DFORCE_CREDIT_TEST" LINKCPU=68000 OUT=CBMOD.TOS bash ./build_atari.sh
