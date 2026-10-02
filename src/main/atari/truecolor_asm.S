| void atari_truecolor(const uint16_t* px, const uint16_t* pal, uint16_t* dst, uint32_t blocks4)
| dst[i] = pal[px[i]] for 4*blocks4 pixels (blocks4 must be 1..65536).
| px values must be < 0x2000 (word table, byte offset = 2*index).

	.text
	.even
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
| 68020+: two pixels per long write, scaled-index table lookups.
	.globl	_atari_truecolor
_atari_truecolor:
	movem.l	%d2-%d4/%a2,-(%sp)		| a2 is callee-saved (4 regs = 16 bytes)
	move.l	20(%sp),%a0
	move.l	24(%sp),%a1
	move.l	28(%sp),%a2
	move.l	32(%sp),%d1
	subq.l	#1,%d1
	moveq	#-1,%d3				| previous pair of indices (never a valid pair)
	moveq	#0,%d4
tc_loop:
	| Flat areas (sky, road stripes, grass) repeat the same pair of indices: reuse the last result.
	move.l	(%a0)+,%d0
	cmp.l	%d3,%d0
	beq.s	tc_same1
	move.l	%d0,%d3
	move.w	%d0,%d2
	swap	%d0
	move.w	(%a1,%d0.w*2),%d0
	swap	%d0
	move.w	(%a1,%d2.w*2),%d0
	move.l	%d0,%d4
tc_same1:
	move.l	%d4,(%a2)+
	move.l	(%a0)+,%d0
	cmp.l	%d3,%d0
	beq.s	tc_same2
	move.l	%d0,%d3
	move.w	%d0,%d2
	swap	%d0
	move.w	(%a1,%d0.w*2),%d0
	swap	%d0
	move.w	(%a1,%d2.w*2),%d0
	move.l	%d0,%d4
tc_same2:
	move.l	%d4,(%a2)+
	subq.l	#1,%d1
	bpl.s	tc_loop
	movem.l	(%sp)+,%d2-%d4/%a2
	rts
#else
	.globl	_atari_truecolor
_atari_truecolor:
	move.l	%a2,-(%sp)			| a2 is callee-saved
	move.l	8(%sp),%a0
	move.l	12(%sp),%a1
	move.l	16(%sp),%a2
	move.l	20(%sp),%d1
	subq.l	#1,%d1
tc_loop:
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	move.w	0(%a1,%d0.w),(%a2)+
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	move.w	0(%a1,%d0.w),(%a2)+
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	move.w	0(%a1,%d0.w),(%a2)+
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	move.w	0(%a1,%d0.w),(%a2)+
	dbra	%d1,tc_loop
	move.l	(%sp)+,%a2
	rts
#endif

| void atari_truecolor_x2(const uint16_t* px, const uint16_t* pal, uint16_t* dst, uint32_t rows)
| Half-resolution frame -> 16-bit screen: each source row of 160 pixels becomes
| two identical rows of 320 pixels (2x2 pixel doubling).  rows must be 1..65536.
	.globl	_atari_truecolor_x2
_atari_truecolor_x2:
	movem.l	%d2-%d4/%a2-%a3,-(%sp)		| 5 regs = 20 bytes
	move.l	24(%sp),%a0
	move.l	28(%sp),%a1
	move.l	32(%sp),%a2
	move.l	36(%sp),%d3
	subq.l	#1,%d3
x2_row:
	lea	640(%a2),%a3			| second output line
	move.w	#159,%d4
x2_px:
	move.w	(%a0)+,%d0
	add.w	%d0,%d0
	move.w	0(%a1,%d0.w),%d1
	move.w	%d1,%d2
	swap	%d2
	move.w	%d1,%d2
	move.l	%d2,(%a2)+
	move.l	%d2,(%a3)+
	dbra	%d4,x2_px
	lea	640(%a2),%a2			| a2 is at the start of line 2: skip it
	dbra	%d3,x2_row
	movem.l	(%sp)+,%d2-%d4/%a2-%a3
	rts
| uint32_t atari_rowsync(const uint32_t* src, uint32_t* prev, uint32_t longs)
| Compares src with prev (longs must be 1..65536).  Returns 0 if they are identical;
| otherwise copies src over prev (from the first difference on) and returns 1.
	.globl	_atari_rowsync
_atari_rowsync:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.l	12(%sp),%d1
	subq.l	#1,%d1
rs_cmp:
	move.l	(%a0)+,%d0
	cmp.l	(%a1)+,%d0
	dbne	%d1,rs_cmp
	beq.s	rs_same
	subq.l	#4,%a0
	subq.l	#4,%a1
rs_copy:
	move.l	(%a0)+,(%a1)+
	dbra	%d1,rs_copy
	moveq	#1,%d0
	rts
rs_same:
	moveq	#0,%d0
	rts

| uint32_t atari_rowcmp(const uint32_t* a, const uint32_t* b, uint32_t longs)
| Returns 0 if the two blocks are identical, 1 otherwise (longs must be 1..65536).
	.text
	.even
	.globl	_atari_rowcmp
_atari_rowcmp:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.l	12(%sp),%d1
	subq.l	#1,%d1
rc_loop:
	cmpm.l	(%a0)+,(%a1)+
	dbne	%d1,rc_loop
	beq.s	rc_same
	moveq	#1,%d0
	rts
rc_same:
	moveq	#0,%d0
	rts

#if defined(__mc68040__) || defined(__mc68060__)
| void atari_copy16(void* dst, const void* src, uint32_t lines)
| Copies `lines` 16-byte lines with MOVE16 (one burst each); dst and src must be 16-byte aligned.
| 68040/68060 only.  Used to push converted rows to the (slow) ST-RAM screen.
	.text
	.even
	.globl	_atari_copy16
_atari_copy16:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.l	12(%sp),%d0
	subq.l	#1,%d0
c16_loop:
	move16	(%a1)+,(%a0)+
	dbra	%d0,c16_loop
	rts
#endif
