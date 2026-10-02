| void atari_tile8(uint16_t* buf, const uint32_t* tile, uint32_t palette, uint32_t stride_bytes)
| Draws one 8x8 tile (8 rows of 8 packed 4-bit pixels, leftmost pixel in the top
| nibble) into a 16-bit index buffer: pixel value 0 is transparent, anything
| else is stored as palette + value.  Equivalent to
| hwtiles::render8x8_tile_mask_lores with a zero mask colour.

	.macro	TPIX	off
	rol.l	#4,%d0
	moveq	#15,%d1
	and.l	%d0,%d1
	beq.s	1f
	add.w	%d3,%d1
	move.w	%d1,\off(%a0)
1:
	.endm

	.text
	.even
	.globl	_atari_tile8_mask
| void atari_tile8_mask(uint16_t* buf, const uint32_t* tile, uint32_t palette, uint32_t stride_bytes, uint32_t rows)
| Like atari_tile8, but only the rows whose bit is set in `rows` (bit 0 = the top row) are drawn.
_atari_tile8_mask:
	movem.l	%d2-%d5/%a2,-(%sp)		| 5 regs = 20 bytes
	move.l	24(%sp),%a0
	move.l	28(%sp),%a1
	move.l	32(%sp),%d3
	move.l	36(%sp),%a2
	move.l	40(%sp),%d5
	moveq	#7,%d4
tm_row:
	lsr.w	#1,%d5
	bcc.s	tm_next				| this row is not drawn
	move.l	(%a1),%d0
	beq.s	tm_next
	TPIX	0
	TPIX	2
	TPIX	4
	TPIX	6
	TPIX	8
	TPIX	10
	TPIX	12
	TPIX	14
tm_next:
	addq.l	#4,%a1
	adda.l	%a2,%a0
	dbra	%d4,tm_row
	movem.l	(%sp)+,%d2-%d5/%a2
	rts

	.globl	_atari_tile8
_atari_tile8:
	movem.l	%d2-%d4/%a2,-(%sp)		| 16 bytes
	move.l	20(%sp),%a0
	move.l	24(%sp),%a1
	move.l	28(%sp),%d3
	move.l	32(%sp),%a2
	moveq	#7,%d4
t8_row:
	move.l	(%a1)+,%d0
	beq.s	t8_next
	TPIX	0
	TPIX	2
	TPIX	4
	TPIX	6
	TPIX	8
	TPIX	10
	TPIX	12
	TPIX	14
t8_next:
	adda.l	%a2,%a0
	dbra	%d4,t8_row
	movem.l	(%sp)+,%d2-%d4/%a2
	rts

#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
| ---------------------------------------------------------------------------
| void atari_tile8_clip(uint16_t* buf, const uint32_t* tile, uint32_t palette, uint32_t stride_bytes,
|                       int32_t x, int32_t y, int32_t width, int32_t height)
| Like atari_tile8, for tiles that may hang over the edge of the picture: only the rows with
| 0 <= y+r < height and the pixels with 0 <= x+i < width are drawn.  buf is the top-left of the
| picture.  Same result as hwtiles::render8x8_tile_mask_clip_lores with a zero mask colour.
| 68020+ (scaled index).
| ---------------------------------------------------------------------------
	.globl	_atari_tile8_clip
_atari_tile8_clip:
	movem.l	%d2-%d7/%a2-%a3,-(%sp)		| 8 regs = 32 bytes
	move.l	36(%sp),%a0			| buf
	move.l	40(%sp),%a1			| tile
	move.l	44(%sp),%d3			| palette
	move.l	52(%sp),%d4			| x
	move.l	56(%sp),%d5			| y
	move.l	64(%sp),%a3			| height
	moveq	#0,%d2				| first pixel column to draw
	tst.l	%d4
	bpl.s	tc_lo
	move.l	%d4,%d2
	neg.l	%d2
tc_lo:
	move.l	60(%sp),%d6
	sub.l	%d4,%d6				| width - x
	moveq	#8,%d0
	cmp.l	%d0,%d6
	ble.s	tc_hi
	move.l	%d0,%d6				| one past the last pixel column to draw
tc_hi:
	cmp.l	%d6,%d2
	bge.s	tc_done				| nothing visible horizontally
	move.l	%d5,%d0
	muls.w	50(%sp),%d0			| y * stride
	add.l	%d0,%a0
	move.l	%d4,%d0
	add.l	%d0,%d0
	add.l	%d0,%a0				| + 2*x
	move.l	%d2,-(%sp)			| first column (args are now 4 bytes further)
	move.l	_g_clip_rows,%a2			| row table: which rows may be drawn
	moveq	#7,%d7
tc_row:
	tst.l	%d5
	bmi.s	tc_next
	tst.b	(%a2,%d5.l)			| is this row drawn in the picture?
	beq.s	tc_next
	cmp.l	%a3,%d5
	bge.s	tc_next
	move.l	(%a1),%d1
	beq.s	tc_next
	move.l	(%sp),%d2
	move.l	%d2,%d0
	lsl.l	#2,%d0
	rol.l	%d0,%d1				| pixel d2 will be in the low nibble after the first rotate below
tc_pix:
	rol.l	#4,%d1
	moveq	#15,%d0
	and.l	%d1,%d0
	beq.s	tc_skip
	add.w	%d3,%d0
	move.w	%d0,(%a0,%d2.l*2)
tc_skip:
	addq.l	#1,%d2
	cmp.l	%d6,%d2
	blt.s	tc_pix
tc_next:
	adda.l	52(%sp),%a0			| next row (stride)
	addq.l	#4,%a1
	addq.l	#1,%d5
	dbra	%d7,tc_row
	addq.l	#4,%sp
tc_done:
	movem.l	(%sp)+,%d2-%d7/%a2-%a3
	rts

#endif

| uint32_t atari_tile_decode(uint32_t mx, int32_t base_l, int32_t base_r, const uint8_t* tile_ram,
|                             uint32_t priority_draw, const uint8_t* tile_banks, uint32_t tiles_mask)
| The per-column lookup of hwtiles::render_tile_layer(): finds this column's tile word, checks its
| priority, resolves the bank and applies tiles_mask.  Returns 0 if the tile is not drawn (wrong
| priority, or the resolved code is 0); otherwise (Colour<<16)|Code, Colour 0..0x7f.
	.text
	.even
	.globl	_atari_tile_decode
_atari_tile_decode:
	movem.l	%d2-%d3,-(%sp)			| d2/d3 are callee-saved in this codebase's convention
	move.l	12(%sp),%d0			| mx
	move.l	24(%sp),%a0			| tile_ram
	add.l	%d0,%d0
	and.l	#0x7f,%d0			| (2*mx)&0x7f
	cmp.l	#64,12(%sp)
	bcc.s	td_right
	add.l	16(%sp),%d0			| + base_l
	bra.s	td_idx
td_right:
	add.l	20(%sp),%d0			| + base_r
td_idx:
	moveq	#0,%d1
	move.w	(%a0,%d0.l),%d1			| d1 = Data, upper word clean
	moveq	#0,%d0
	move.w	%d1,%d0
	lsr.w	#8,%d0
	lsr.w	#7,%d0				| d0 = (uint8_t)(Data>>15), 0 or 1
	cmp.l	28(%sp),%d0			| priority_draw
	bne	td_skip
	moveq	#0,%d0
	move.w	%d1,%d0
	and.w	#0x1fff,%d0			| d0 = Code (13 bits), clean 32-bit value
	move.l	%d0,%d2
	lsr.l	#8,%d2
	lsr.l	#4,%d2				| bank = Code>>12 (0 or 1)
	move.l	32(%sp),%a0			| tile_banks
	moveq	#0,%d3
	move.b	(%a0,%d2.l),%d3			| bank byte, 0..255
	lsl.l	#8,%d3
	lsl.l	#4,%d3				| bank byte << 12 (up to 20 bits: needs the full long)
	and.w	#0xfff,%d0			| Code & 0xfff (d0 still <= 0xfff, fits in the word)
	or.l	%d3,%d0				| combine as a full 32-bit value, like the C++ does
	and.l	36(%sp),%d0			| tiles_mask
	beq	td_skip
	move.l	%d0,%d2				| Code
	moveq	#0,%d0
	move.w	%d1,%d0
	lsr.w	#6,%d0
	and.w	#0x7f,%d0			| Colour (0..0x7f, fits in a byte)
	swap	%d0				| d0 = Colour<<16 (upper word was already clean)
	or.l	%d2,%d0
	movem.l	(%sp)+,%d2-%d3
	rts
td_skip:
	moveq	#0,%d0
	movem.l	(%sp)+,%d2-%d3
	rts
