| Timer A interrupt for the DSP .mod replay (see dsp_replay.hpp).
|
| The timer runs at 200 Hz (MFP divider 64, count 192); every fourth interrupt calls
| dsp_replay_frame(), i.e. 50 times a second, the same arrangement as Simplet's own 68030
| code. The interrupt level is lowered to 3 for the duration of the frame so the keyboard
| ACIA and the 200 Hz system timer are not held up while the samples are sent to the DSP.

	.text
	.even

	.globl	_dsp_replay_isr
_dsp_replay_isr:
	bclr	#5,0xfffffa0f.w			| MFP: end of interrupt for Timer A
	subq.b	#1,dsp_isr_div
	bne.s	dr_out
	move.b	#4,dsp_isr_div
	tas	dsp_isr_busy			| previous frame still being sent?
	bne.s	dr_out
	move.w	#0x2300,%sr
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	jsr	_dsp_replay_frame
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	clr.b	dsp_isr_busy
dr_out:
	rte

	.data
dsp_isr_div:	.byte	4
dsp_isr_busy:	.byte	0
	.even
