#!/bin/bash
export PATH=/opt/cross-mint/bin:$PATH
cd /cygdrive/c/claude/cannonball
CPU=68030 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB030.TOS bash ./build_atari.sh
CPU=68060 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB060.TOS bash ./build_atari.sh
