| Inner loops for the road renderers (hwroad.cpp, PLATFORM_ATARI builds).
|   void atari_fill16(uint16_t* dst, uint32_t color, uint32_t count);
|   void atari_road_copy1(uint16_t* dst, const uint8_t* src, const uint16_t* tab, uint32_t n);
|   void atari_road_copy2(uint16_t* dst, const uint8_t* s0, const uint8_t* s1,
|                         const uint16_t* lut64, uint32_t n);
| Counts must be at least 1 (callers skip empty spans).
| Assembled for every target; 68020+ scaled indexing is used only where it exists.

#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
	.macro	LOADW	base,idx,dst
	move.w	(\base,\idx\().w*2),\dst
	.endm
#else
	.macro	LOADW	base,idx,dst
	add.w	\idx,\idx
	move.w	0(\base,\idx\().w),\dst
	.endm
#endif

	.text
	.even

| ---------------------------------------------------------------------------
| dst[0..count) = (uint16_t)color, two pixels per long write.
| ---------------------------------------------------------------------------
	.globl	_atari_fill16
_atari_fill16:
	move.l	%d2,-(%sp)
	move.l	8(%sp),%a0
	move.l	12(%sp),%d0
	move.l	16(%sp),%d1
	move.l	%d0,%d2
	swap	%d0
	move.w	%d2,%d0			| d0 = colour:colour
	move.l	%d1,%d2			| keep the count for the odd tail
	lsr.l	#1,%d1
	beq.s	fl_tail
	subq.l	#1,%d1
fl_loop:
	move.l	%d0,(%a0)+
	dbra	%d1,fl_loop
fl_tail:
	btst	#0,%d2
	beq.s	fl_done
	move.w	%d0,(%a0)+
fl_done:
	move.l	(%sp)+,%d2
	rts

| ---------------------------------------------------------------------------
| dst[i] = tab[src[i]]   (src values 0..3 or any byte the table covers)
| ---------------------------------------------------------------------------
	.globl	_atari_road_copy1
_atari_road_copy1:
	move.l	%a2,-(%sp)
	move.l	8(%sp),%a0
	move.l	12(%sp),%a1
	move.l	16(%sp),%a2
	move.l	20(%sp),%d1
	moveq	#0,%d0
	subq.l	#1,%d1
c1_loop:
	moveq	#0,%d0
	move.b	(%a1)+,%d0
	LOADW	%a2,%d0,(%a0)+
	dbra	%d1,c1_loop
	move.l	(%sp)+,%a2
	rts

| ---------------------------------------------------------------------------
| dst[i] = lut64[s0[i]*8 + s1[i]]   (s0, s1 in 0..7)
| ---------------------------------------------------------------------------
	.globl	_atari_road_copy2
_atari_road_copy2:
	movem.l	%a2-%a3,-(%sp)
	move.l	12(%sp),%a0
	move.l	16(%sp),%a1
	move.l	20(%sp),%a2
	move.l	24(%sp),%a3
	move.l	28(%sp),%d1
	moveq	#0,%d0
	subq.l	#1,%d1
c2_loop:
	moveq	#0,%d0
	move.b	(%a1)+,%d0
	lsl.w	#3,%d0
	add.b	(%a2)+,%d0
	LOADW	%a3,%d0,(%a0)+
	dbra	%d1,c2_loop
	movem.l	(%sp)+,%a2-%a3
	rts

| ---------------------------------------------------------------------------
| void atari_road_offset(int16_t* dst, const int16_t* src, int32_t inc, uint32_t invert)
| The 512-entry general loop of ORoad::do_road_offset():
|     h = (int16)(acc >> 16); if (src[i] == 0x3210) h = 0;
|     x = src[i] >> 6;  dst[i] = invert ? h - x : h + x;  acc += inc;      (acc starts at 0)
| ---------------------------------------------------------------------------
	.text
	.even
	.globl	_atari_road_offset
_atari_road_offset:
	movem.l	%d2-%d4,-(%sp)			| 3 regs = 12 bytes
	move.l	16(%sp),%a0			| dst
	move.l	20(%sp),%a1			| src
	move.l	24(%sp),%d3			| inc
	move.l	28(%sp),%d4			| invert
	moveq	#0,%d1				| acc
	move.w	#511,%d0			| dbra counter
	tst.l	%d4
	bne.s	ro_inv
ro_fwd:
	move.l	%d1,%d2
	swap	%d2				| h = acc >> 16
	move.w	(%a1)+,%d4
	cmp.w	#0x3210,%d4
	bne.s	ro_f1
	moveq	#0,%d2				| ignore the car position on this line
ro_f1:
	asr.w	#6,%d4
	add.w	%d4,%d2
	move.w	%d2,(%a0)+
	add.l	%d3,%d1
	dbra	%d0,ro_fwd
	bra.s	ro_end
ro_inv:
	move.l	%d1,%d2
	swap	%d2
	move.w	(%a1)+,%d4
	cmp.w	#0x3210,%d4
	bne.s	ro_i1
	moveq	#0,%d2
ro_i1:
	asr.w	#6,%d4
	sub.w	%d4,%d2
	move.w	%d2,(%a0)+
	add.l	%d3,%d1
	dbra	%d0,ro_inv
ro_end:
	movem.l	(%sp)+,%d2-%d4
	rts

| ---------------------------------------------------------------------------
| void atari_hscroll_out(uint16_t* dst, const int16_t* src)
| 512 words: dst[i] = 0x654 - src[i]   (ORoad::output_hscroll)
| ---------------------------------------------------------------------------
	.globl	_atari_hscroll_out
_atari_hscroll_out:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.w	#63,%d0				| 64 groups of 8
ho_loop:
	.rept	8
	move.w	#0x654,%d1
	sub.w	(%a1)+,%d1
	move.w	%d1,(%a0)+
	.endr
	dbra	%d0,ho_loop
	rts

| ---------------------------------------------------------------------------
| void atari_road_lut(uint16_t* lut, const uint16_t* ct, const uint8_t* pmap)
| The 8x8 priority table of the two-road case (HWRoad::render_foreground_lores):
|     lut[p0*8 + p1] = (pmap[p0] >> p1) & 1 ? ct[0x10 + p1] : ct[p0]
| ---------------------------------------------------------------------------
	.text
	.even
	.globl	_atari_road_lut
_atari_road_lut:
	movem.l	%d2-%d3/%a2,-(%sp)		| 3 regs = 12 bytes
	move.l	16(%sp),%a0			| lut
	move.l	20(%sp),%a1			| ct
	move.l	24(%sp),%a2			| pmap
	moveq	#0,%d0				| 2*p0
rl_row:
	moveq	#0,%d2
	move.b	(%a2)+,%d2			| mask for this p0
	move.w	(%a1,%d0.w),%d3			| ct[p0]
	.irp	k,0,1,2,3,4,5,6,7
	lsr.b	#1,%d2
	bcc.s	1f
	move.w	32+2*\k(%a1),(%a0)+		| road 1 wins: ct[0x10 + k]
	bra.s	2f
1:	move.w	%d3,(%a0)+			| road 0
2:
	.endr
	addq.w	#2,%d0
	cmp.w	#16,%d0
	bne.s	rl_row
	movem.l	(%sp)+,%d2-%d3/%a2
	rts

| ---------------------------------------------------------------------------
| Half-horizontal-resolution road copies (run-time option: half the columns
| are sampled from the road texture, each sample written to a pair of screen
| pixels).  Same table-lookup contract as atari_road_copy1/2: the source still
| advances one texture column per output pixel, but only every other column is
| read.  (It used to advance one column per PAIR of pixels: the texture was then
| stretched to twice its width from the left edge of each span, which drew the
| road at the wrong place and bent it as the span moved from row to row.)
|
| Pairs must line up on the same even/odd screen columns on every scanline, or
| the boundary between two neighbouring pixels drifts by one column from one
| row to the next as the clip window (r.ab) moves with the road curve, which
| shears the whole road into a diagonal.  `phase` is the absolute screen x of
| the first output pixel (dst[0]) & 1: 0 starts a pair immediately, 1 means
| dst[0] is the second half of a pair whose first half was already drawn (by
| the caller's clip fill or the previous span), so it is written on its own
| and pairing resumes at dst[1].  n (output pixel count) may be 0.
|
|   void atari_road_copy1_half(uint16_t* dst, const uint8_t* src, const uint16_t* tab, uint32_t n, uint32_t phase);
|   void atari_road_copy2_half(uint16_t* dst, const uint8_t* s0, const uint8_t* s1,
|                              const uint16_t* lut64, uint32_t n, uint32_t phase);
| ---------------------------------------------------------------------------
	.globl	_atari_road_copy1_half
_atari_road_copy1_half:
	movem.l	%d2/%a2,-(%sp)
	move.l	12(%sp),%a0
	move.l	16(%sp),%a1
	move.l	20(%sp),%a2
	move.l	24(%sp),%d1
	tst.l	28(%sp)
	beq.s	rc1h_even
	tst.l	%d1
	beq.s	rc1h_done
	moveq	#0,%d0
	move.b	(%a1)+,%d0
	LOADW	%a2,%d0,%d0
	move.w	%d0,(%a0)+
	subq.l	#1,%d1
rc1h_even:
	moveq	#0,%d0
	move.l	%d1,%d2
	lsr.l	#1,%d1
	beq.s	rc1h_tail
	subq.l	#1,%d1
rc1h_loop:
	moveq	#0,%d0
	move.b	(%a1),%d0
	addq.l	#2,%a1				| one texture column used, the next one skipped
	LOADW	%a2,%d0,%d0
	move.w	%d0,(%a0)+
	move.w	%d0,(%a0)+
	dbra	%d1,rc1h_loop
rc1h_tail:
	btst	#0,%d2
	beq.s	rc1h_done
	moveq	#0,%d0
	move.b	(%a1),%d0
	LOADW	%a2,%d0,%d0
	move.w	%d0,(%a0)
rc1h_done:
	movem.l	(%sp)+,%d2/%a2
	rts

	.globl	_atari_road_copy2_half
_atari_road_copy2_half:
	movem.l	%d2/%a2-%a3,-(%sp)
	move.l	16(%sp),%a0
	move.l	20(%sp),%a1
	move.l	24(%sp),%a2
	move.l	28(%sp),%a3
	move.l	32(%sp),%d1
	tst.l	36(%sp)
	beq.s	rc2h_even
	tst.l	%d1
	beq.s	rc2h_done
	moveq	#0,%d0
	move.b	(%a1)+,%d0
	lsl.w	#3,%d0
	add.b	(%a2)+,%d0
	LOADW	%a3,%d0,%d0
	move.w	%d0,(%a0)+
	subq.l	#1,%d1
rc2h_even:
	moveq	#0,%d0
	move.l	%d1,%d2
	lsr.l	#1,%d1
	beq.s	rc2h_tail
	subq.l	#1,%d1
rc2h_loop:
	moveq	#0,%d0
	move.b	(%a1),%d0
	addq.l	#2,%a1				| one texture column used, the next one skipped
	lsl.w	#3,%d0
	add.b	(%a2),%d0
	addq.l	#2,%a2
	LOADW	%a3,%d0,%d0
	move.w	%d0,(%a0)+
	move.w	%d0,(%a0)+
	dbra	%d1,rc2h_loop
rc2h_tail:
	btst	#0,%d2
	beq.s	rc2h_done
	moveq	#0,%d0
	move.b	(%a1),%d0
	lsl.w	#3,%d0
	add.b	(%a2),%d0
	LOADW	%a3,%d0,%d0
	move.w	%d0,(%a0)
rc2h_done:
	movem.l	(%sp)+,%d2/%a2-%a3
	rts
