| DSPMOD 3.4 glue (see dspmod.hpp).
|
| dspmod_call(entry, regs): calls a DSPMOD entry point with d0-d3/a0-a3 loaded from
| regs[0..7] (that order) and returns its d0. DSPMOD changes any register it likes,
| so everything the C caller expects preserved is saved here.
|
| dspmod_isr: Timer A interrupt, 200 Hz; every fourth one calls dspmod_frame(), i.e.
| 50 times a second (DSPMOD's PlayMusic wants 50..70 Hz). The interrupt level is lowered
| to 3 meanwhile, so the keyboard and the 200 Hz system timer are not held up.

	.text
	.even

	.globl	_dspmod_call
_dspmod_call:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)		| 11 registers = 44 bytes
	move.l	48(%sp),%a5			| entry
	move.l	52(%sp),%a6			| regs
	move.l	%a6,-(%sp)
	movem.l	(%a6),%d0-%d3/%a0-%a3
	jsr	(%a5)
	move.l	(%sp)+,%a6
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl	_dspmod_isr
_dspmod_isr:
	bclr	#5,0xfffffa0f.w			| MFP: end of interrupt for Timer A
	subq.b	#1,dm_isr_div
	bne	dm_out
	move.b	#4,dm_isr_div
	tas	dm_isr_busy			| previous frame still running?
	bne	dm_out
	move.w	#0x2300,%sr
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	jsr	_dspmod_frame
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	clr.b	dm_isr_busy
dm_out:
	rte

	.data
dm_isr_div:	.byte	4
dm_isr_busy:	.byte	0
	.even
