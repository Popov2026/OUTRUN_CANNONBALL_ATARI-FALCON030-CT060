| 68000 version of the inner loop of hwsprites::render(): one scanline of one
| sprite (source words -> horizontally zoomed pixels).  Matches the C loops it
| replaces (PIXEL_ACCURACY 0 path) exactly.
|
|   uint32_t* atari_sprite_line(struct SprLine* s);   (extern "C")
|
| struct SprLine (all 32-bit):
|    0  p        uint16_t* first destination pixel (may be outside the row)
|    4  spr      uint32_t* word to start from, already stepped back one word
|    8  sprstep  bytes to add to spr before each fetch (+4 normal, -4 flipped)
|   12  pstep    bytes to add to p per output pixel (+2 / -2)
|   16  hzoom    horizontal zoom (0x200 == 1:1)
|   20  p1       clip start pointer (&row[x1])
|   24  p2       clip end pointer   (&row[x2])
|   28  pbound   row end (&row[width]) if pstep>0, row start (&row[0]) if <0
|   32  color    sprite colour base (pix is OR-ed into its low nibble)
|   36  shadow   non-zero: pix 0xA darkens instead of drawing
|   40  flip     0: pixels MSB-first, 1: LSB-first
| Returns the final source pointer (last word fetched).
|
| Registers: d0 scratch, d1 xacc, d2 word, d3 hzoom, d4 pix, d5 colour,
|            d6 shadow, d7 pstep, a0 sprstep, a1 spr, a2 p, a3 pbound,
|            a4 p1, a5 p2.  d2-d7/a2-a5 are saved (callee-saved in the C ABI).

#if !defined(__mc68030__) && !defined(__mc68060__)   /* 030/060 builds use sprite_asm030.S instead */
	.text
	.even

| Emit one output pixel for the value in d4 (0..15) at (a2), then advance.
	.macro	EMIT	n
	tst.b	%d4
	beq	.Lskip\n
	cmp.b	#15,%d4
	beq	.Lskip\n
	cmpa.l	%a4,%a2
	bcs	.Lskip\n
	cmpa.l	%a5,%a2
	bcc	.Lskip\n
	tst.b	%d6
	beq	.Lnorm\n
	cmp.b	#10,%d4
	bne	.Lnorm\n
	move.w	(%a2),%d0
	and.w	#0x0fff,%d0
	add.w	#0x1000,%d0
	move.w	%d0,(%a2)
	bra	.Lskip\n
.Lnorm\n:
	move.w	%d5,%d0
	or.b	%d4,%d0
	move.w	%d0,(%a2)
.Lskip\n:
	adda.l	%d7,%a2
	add.w	%d3,%d1
	.endm

| One source nibble -> zero or more output pixels (xacc handling).
| kind 0: MSB first (rol then read), kind 1: LSB first (read then ror).
	.macro	NIB	n,kind
	.if \kind == 0
	rol.l	#4,%d2
	move.b	%d2,%d4
	and.b	#15,%d4
	.else
	move.b	%d2,%d4
	and.b	#15,%d4
	ror.l	#4,%d2
	.endif
	cmp.w	#0x200,%d1
	bge	.Lnone\n
.Lmore\n:
	EMIT	\n\()a
	cmp.w	#0x200,%d1
	blt	.Lmore\n
.Lnone\n:
	sub.w	#0x200,%d1
	.endm

	.globl	_atari_sprite_line
_atari_sprite_line:
	movem.l	%d2-%d7/%a2-%a5,-(%sp)		| 10 regs = 40 bytes
	move.l	44(%sp),%a5			| struct
	move.l	0(%a5),%a2
	move.l	4(%a5),%a1
	move.l	8(%a5),%a0
	move.l	12(%a5),%d7
	move.l	16(%a5),%d3
	move.l	20(%a5),%a4
	move.l	28(%a5),%a3
	move.l	32(%a5),%d5
	move.l	36(%a5),%d6
	move.l	40(%a5),%d0			| flip
	move.l	24(%a5),%a5			| p2 (struct pointer no longer needed)
	moveq	#0,%d1				| xacc
	tst.l	%d0
	bne	flipped

| ------------------------------ normal ---------------------------------
nwordloop:
	tst.l	%d7
	bmi.s	nneg
	cmpa.l	%a3,%a2
	bcc	ndone
	bra.s	ngo
nneg:
	cmpa.l	%a3,%a2
	bcs	ndone
ngo:
	adda.l	%a0,%a1
	move.l	(%a1),%d2
	NIB	1,0
	NIB	2,0
	NIB	3,0
	NIB	4,0
	NIB	5,0
	NIB	6,0
	NIB	7,0
	NIB	8,0
	move.l	%d2,%d0
	and.l	#0x000000f0,%d0
	cmp.l	#0x000000f0,%d0
	bne	nwordloop
ndone:
	move.l	%a1,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts

| ------------------------------ flipped --------------------------------
flipped:
fwordloop:
	tst.l	%d7
	bmi.s	fneg
	cmpa.l	%a3,%a2
	bcc	fdone
	bra.s	fgo
fneg:
	cmpa.l	%a3,%a2
	bcs	fdone
fgo:
	adda.l	%a0,%a1
	move.l	(%a1),%d2
	NIB	11,1
	NIB	12,1
	NIB	13,1
	NIB	14,1
	NIB	15,1
	NIB	16,1
	NIB	17,1
	NIB	18,1
	move.l	%d2,%d0
	and.l	#0x0f000000,%d0
	cmp.l	#0x0f000000,%d0
	bne	fwordloop
fdone:
	move.l	%a1,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts
#endif
