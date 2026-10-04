| Timer B interrupt for the sound (see audio.cpp, "Sound from an interrupt").
|
| About 60 times a second; calls sound_isr_step(), which makes a step of sound when the DMA
| needs one. The interrupt level is lowered to 3 meanwhile, so the keyboard, the 200 Hz system
| timer and the DSP .mod replay's Timer A are not held up; a Timer B interrupt arriving while
| a step is still being made is skipped.

	.text
	.even

	.globl	_sound_isr
_sound_isr:
	bclr	#0,0xfffffa0f.w			| MFP: end of interrupt for Timer B
	tas	snd_isr_busy			| previous step still running?
	bne	snd_out
	move.w	#0x2300,%sr
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	jsr	_sound_isr_step
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	clr.b	snd_isr_busy
snd_out:
	rte

	.data
snd_isr_busy:	.byte	0
	.even
