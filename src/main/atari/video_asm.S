| 68000 hot loops for the Atari video backend (see video.cpp).
| C prototypes (extern "C"):
|   void atari_hist(const uint16_t* px, uint16_t* hist, uint32_t count);
|   void atari_c2p (const uint16_t* px, const uint8_t* slot, uint16_t* dst, uint32_t groups);
| Plain 68000 only: no 32-bit multiplies, no 020+ instructions.

	.text
	.even

| ---------------------------------------------------------------------------
| atari_hist: hist[px[i]]++ for i in [0,count).  count must be 1..65536.
| hist is an array of uint16_t, px values must be < 0x4000 (word index * 2).
| ---------------------------------------------------------------------------
	.globl	_atari_hist
_atari_hist:
	move.l	%d2,-(%sp)		| d2 is callee-saved in the C ABI
	move.l	8(%sp),%a0		| px
	move.l	12(%sp),%a1		| hist
	move.l	16(%sp),%d1		| count
	subq.l	#1,%d1
	move.l	%d1,%d2
	lsr.l	#2,%d2			| number of 4-pixel blocks
	and.w	#3,%d1			| remainder pixels
	tst.l	%d2
	beq.s	hist_rem
	subq.w	#1,%d2
hist_blk:
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	addq.w	#1,0(%a1,%d0.w)
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	addq.w	#1,0(%a1,%d0.w)
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	addq.w	#1,0(%a1,%d0.w)
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	addq.w	#1,0(%a1,%d0.w)
	dbra	%d2,hist_blk
hist_rem:
	| d1 = remaining count - 1 (0..3) after the -1 above; handle count%4 pixels
	| Total pixels = 4*blocks + (d1+1)  since count-1 = 4*blocks + d1
hist_tail:
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	addq.w	#1,0(%a1,%d0.w)
	dbra	%d1,hist_tail
	move.l	(%sp)+,%d2
	rts

| ---------------------------------------------------------------------------
| atari_c2p: for each group of 16 pixels, look each pixel's palette slot up
| in slot[] (byte table indexed by engine colour index) and emit 4 interleaved
| bitplane words (plane0..plane3) to dst.  groups must be 1..65536.
| ---------------------------------------------------------------------------
	.macro	PIX
	move.w	(%a0)+,%d0
	move.b	0(%a1,%d0.w),%d1
	lsr.b	#1,%d1
	addx.w	%d3,%d3
	lsr.b	#1,%d1
	addx.w	%d4,%d4
	lsr.b	#1,%d1
	addx.w	%d5,%d5
	lsr.b	#1,%d1
	addx.w	%d6,%d6
	.endm

	.globl	_atari_c2p
_atari_c2p:
	movem.l	%d2-%d7/%a2,-(%sp)
	move.l	32(%sp),%a0		| px  (7 saved regs = 28 bytes + return address 4)
	move.l	36(%sp),%a1		| slot
	move.l	40(%sp),%a2		| dst
	move.l	44(%sp),%d7		| groups
	subq.l	#1,%d7
c2p_group:
	moveq	#0,%d3
	moveq	#0,%d4
	moveq	#0,%d5
	moveq	#0,%d6
	moveq	#0,%d0
	moveq	#0,%d1
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	PIX
	move.w	%d3,(%a2)+
	move.w	%d4,(%a2)+
	move.w	%d5,(%a2)+
	move.w	%d6,(%a2)+
	dbra	%d7,c2p_group
	movem.l	(%sp)+,%d2-%d7/%a2
	rts
