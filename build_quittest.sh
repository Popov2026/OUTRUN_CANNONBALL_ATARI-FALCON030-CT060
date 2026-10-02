#!/bin/bash
export PATH=/opt/cross-mint/bin:$PATH
cd /cygdrive/c/claude/cannonball
CPU=68030 EXTRA="-msoft-float -DPLATFORM_FALCON -DQUIT_DEBUG" LINKCPU=68000 OUT=CBQUIT.TOS bash ./build_atari.sh
