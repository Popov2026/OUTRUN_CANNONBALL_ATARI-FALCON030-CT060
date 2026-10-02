| 68030/68060 version of atari_sprite_line (same contract as sprite_asm.S, plus a
| fast path).  Assembled only when targeting the 68030/68060; the 68000 build
| uses the unrolled version in sprite_asm.S.
|
|   void atari_sprite_rows(void);   (extern "C") - see the entry point below. The field list
|   that follows describes the values the per-row code works with (they were the fields of
|   the per-row struct of the earlier one-call-per-row version).
|
| struct SprLine (all 32-bit):
|    0  p        uint16_t* first destination pixel (may be outside the row)
|    4  spr      uint32_t* word to start from, already stepped back one word
|    8  sprstep  bytes added to spr before each fetch (+4 normal, -4 flipped)
|   12  pstep    bytes added to p per output pixel (+2 / -2)
|   16  hzoom    horizontal zoom (0x200 == 1:1)
|   20  p1       clip start pointer (&row[x1])
|   24  p2       clip end pointer   (&row[x2])
|   28  pbound   row end if pstep>0 (&row[x2]), row start if <0 (&row[x1])
|   32  color    sprite colour base (pix is OR-ed into its low nibble)
|   36  shadow   non-zero: pix 0xA darkens instead of drawing
|   40  flip     0: pixels MSB-first, 1: LSB-first
|   44  lo       fast path window start (see below)
|   48  range    fast path window size in bytes (0 disables the fast path)
|   52  ttab     uint16_t[16]: colour|pix for pix 1..14, 0 for 0 and 15;
|                  entry 10 is 0x8000 for a shadow sprite (pixel 0xA darkens instead)
| Returns the final source pointer (last word fetched).
|
| Fast path: when the whole word being drawn (at most 8 nibbles * ceil(0x200/hzoom)
| pixels) is certain to stay inside the clip window, the nibbles are drawn with
| no clip tests and a single table lookup per pixel (shadow pixels are flagged
| by the table).  The caller sets lo/range so that (p - lo) < range guarantees that.
|
| Two 68030 specifics: compact dbra loops that stay in the 256-byte instruction
| cache, and scaled-index addressing for the table lookup.
|
| Registers: d0 scratch, d1 xacc, d2 word, d3 hzoom, d4 pix-1 (general) or pix
|            (fast), d5 colour+1, d6 shadow, d7 nibble counter, a0 sprstep,
|            a1 spr, a2 p, a3 ttab, a4 p1, a5 p2, a6 pstep.
|            Stack: 0(sp) pbound, 4(sp) lo, 8(sp) range.

#if defined(__mc68030__) || defined(__mc68060__)

	.text
	.even

| Field offsets of struct SprRows (hwvideo/hwsprites.cpp) - one global instance, atari_sr.
	.equ	R_Y,0			| current row
	.equ	R_YTARGET,4		| row to stop at (not drawn)
	.equ	R_YDELTA,8		| +1 / -1
	.equ	R_YACC,12		| vertical zoom accumulator
	.equ	R_VZOOM,16
	.equ	R_ADDR,20		| source word index of the current row
	.equ	R_PITCH,24		| source words per source row
	.equ	R_ROW,28		| &pixels[y * width]
	.equ	R_ROWSTEP,32		| bytes from one row to the next (signed)
	.equ	R_XOFF,36		| bytes from the row start to the first pixel
	.equ	R_X1OFF,40		| bytes from the row start to the clip start
	.equ	R_X2OFF,44		| bytes from the row start to the clip end
	.equ	R_LOOFF,48		| bytes from the row start to the fast path window
	.equ	R_RANGE,52		| fast path window size (0 disables the fast path)
	.equ	R_SPRDATA,56		| base of the sprite bank
	.equ	R_FLIP,60
	.equ	R_HEIGHT,64		| rows in the picture
	.equ	R_ROWDRAW,68		| g_row_draw
	.equ	R_ENDSLOT,72		| uint16_t*: receives the last source word index used
	.equ	R_SPRSTEP,76
	.equ	R_PSTEP,80
	.equ	R_HZOOM,84
	.equ	R_COLOR,88
	.equ	R_SHADOW,92
	.equ	R_TTAB,96

| void atari_sprite_rows(void);   (extern "C")
|
| Draws every row of one sprite, described by the global struct atari_sr. The row loop used to
| be in C, with one call per row: each call saved and restored 11 registers and reloaded the
| 14 fields that do not change from one row to the next. Here they are loaded once; only the
| row-dependent values (pointers into the row, source address) are recomputed per row.
| The per-row drawing code below is unchanged; it ends at `done`, which comes back to the
| row loop.
	.globl	_atari_sprite_rows
_atari_sprite_rows:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	lea	-12(%sp),%sp
	move.l	_atari_sr+R_RANGE,8(%sp)
	move.l	_atari_sr+R_HZOOM,%d3
	move.l	_atari_sr+R_SPRSTEP,%a0
	move.l	_atari_sr+R_TTAB,%a3
	move.l	_atari_sr+R_COLOR,%d5
	addq.w	#1,%d5				| colour+1 (pix is stored as pix-1)
	move.l	_atari_sr+R_SHADOW,%d6
	move.l	_atari_sr+R_PSTEP,%a6

rows_loop:
	move.l	_atari_sr+R_Y,%d0
	bmi	rows_skip			| above the picture
	cmp.l	_atari_sr+R_HEIGHT,%d0
	bge	rows_skip			| below the picture
	move.l	_atari_sr+R_ROWDRAW,%a1
	tst.b	(%a1,%d0.l)
	beq	rows_skip			| row not drawn in this picture (vscale option)
	addq.l	#1,_g_spr_lines			| statistics
	move.l	_atari_sr+R_ROW,%a2
	move.l	%a2,%a4
	adda.l	_atari_sr+R_X1OFF,%a4		| clip start
	move.l	%a2,%a5
	adda.l	_atari_sr+R_X2OFF,%a5		| clip end
	move.l	%a2,%d0
	add.l	_atari_sr+R_LOOFF,%d0
	move.l	%d0,4(%sp)			| fast path window start
	move.l	%a6,%d0
	bmi.s	1f
	move.l	%a5,0(%sp)			| pbound: row end when drawing rightwards
	bra.s	2f
1:	move.l	%a4,0(%sp)			| row start when drawing leftwards
2:	adda.l	_atari_sr+R_XOFF,%a2		| first destination pixel
	move.l	_atari_sr+R_ADDR,%d0
	asl.l	#2,%d0
	move.l	_atari_sr+R_SPRDATA,%a1
	adda.l	%d0,%a1
	suba.l	%a0,%a1				| one word before the first: the code below steps first
	moveq	#0,%d1				| xacc
	tst.l	_atari_sr+R_FLIP
	bne	flipped

| ------------------------------ normal ---------------------------------
nword:
	move.l	%a2,%d0
	sub.l	4(%sp),%d0
	cmp.l	8(%sp),%d0
	bcs	nfast
	move.l	%a6,%d0
	bmi.s	nneg
	cmpa.l	0(%sp),%a2
	bcc	done
	bra.s	ngo
nneg:
	cmpa.l	0(%sp),%a2
	bcs	done
ngo:
	adda.l	%a0,%a1
	move.l	(%a1),%d2
	moveq	#7,%d7
nnib:
	rol.l	#4,%d2
	move.b	%d2,%d4
	and.b	#15,%d4
	subq.b	#1,%d4				| pix 0 -> 255, pix 15 -> 14
	cmp.w	#0x200,%d1
	bge.s	nnone
nmore:
	cmp.b	#13,%d4				| transparent (0) or end marker (15)
	bhi.s	nskip
	cmpa.l	%a4,%a2
	bcs.s	nskip
	cmpa.l	%a5,%a2
	bcc.s	nskip
	tst.b	%d6
	beq.s	nnorm
	cmp.b	#9,%d4				| pix 0xA
	bne.s	nnorm
	move.w	(%a2),%d0
	and.w	#0x0fff,%d0
	add.w	#0x1000,%d0
	move.w	%d0,(%a2)
	bra.s	nskip
nnorm:
	move.w	%d5,%d0
	add.b	%d4,%d0
	move.w	%d0,(%a2)
nskip:
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	nmore
nnone:
	sub.w	#0x200,%d1
	dbra	%d7,nnib
	move.l	%d2,%d0
	and.l	#0x000000f0,%d0
	cmp.l	#0x000000f0,%d0
	bne	nword
	bra	done

| fast word: window guaranteed inside the clip range, no shadow
nfast:
	adda.l	%a0,%a1
	move.l	(%a1),%d2
#ifndef SPR_NOFAST1
	cmp.w	#0x200,%d3
	beq	nf1
#endif
	moveq	#7,%d7
nfnib:
	rol.l	#4,%d2
	moveq	#15,%d4
	and.l	%d2,%d4
	cmp.w	#0x200,%d1
	bge.s	nfnone
	move.w	(%a3,%d4.l*2),%d0		| colour|pix, 0 if transparent / marker, 0x8000 if shadow pixel
	beq.s	nftrans
	bmi.s	nfshad
nfopq:
	move.w	%d0,(%a2)
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	nfopq
	bra.s	nfnone
nfshad:
	move.w	(%a2),%d0
	and.w	#0x0fff,%d0
	add.w	#0x1000,%d0
	move.w	%d0,(%a2)
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	nfshad
	bra.s	nfnone
nftrans:
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	nftrans
nfnone:
	sub.w	#0x200,%d1
	dbra	%d7,nfnib
	move.l	%d2,%d0
	and.l	#0x000000f0,%d0
	cmp.l	#0x000000f0,%d0
	bne	nword
	bra	done

#ifndef SPR_NOFAST1
| fast word at 1:1 zoom: exactly one pixel per nibble, no accumulator
nf1:
	.rept	8
	rol.l	#4,%d2
	moveq	#15,%d4
	and.l	%d2,%d4
	move.w	(%a3,%d4.l*2),%d0
	beq.s	1f
	bpl.s	2f
	move.w	(%a2),%d0
	and.w	#0x0fff,%d0
	add.w	#0x1000,%d0
2:	move.w	%d0,(%a2)
1:	adda.l	%a6,%a2
	.endr
	move.l	%d2,%d0
	and.l	#0x000000f0,%d0
	cmp.l	#0x000000f0,%d0
	bne	nword
	bra	done
#endif

| ------------------------------ flipped --------------------------------
flipped:
fword:
	move.l	%a2,%d0
	sub.l	4(%sp),%d0
	cmp.l	8(%sp),%d0
	bcs	ffast
	move.l	%a6,%d0
	bmi.s	fneg
	cmpa.l	0(%sp),%a2
	bcc	done
	bra.s	fgo
fneg:
	cmpa.l	0(%sp),%a2
	bcs	done
fgo:
	adda.l	%a0,%a1
	move.l	(%a1),%d2
	moveq	#7,%d7
fnib:
	move.b	%d2,%d4
	and.b	#15,%d4
	subq.b	#1,%d4
	ror.l	#4,%d2
	cmp.w	#0x200,%d1
	bge.s	fnone
fmore:
	cmp.b	#13,%d4
	bhi.s	fskip
	cmpa.l	%a4,%a2
	bcs.s	fskip
	cmpa.l	%a5,%a2
	bcc.s	fskip
	tst.b	%d6
	beq.s	fnorm
	cmp.b	#9,%d4
	bne.s	fnorm
	move.w	(%a2),%d0
	and.w	#0x0fff,%d0
	add.w	#0x1000,%d0
	move.w	%d0,(%a2)
	bra.s	fskip
fnorm:
	move.w	%d5,%d0
	add.b	%d4,%d0
	move.w	%d0,(%a2)
fskip:
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	fmore
fnone:
	sub.w	#0x200,%d1
	dbra	%d7,fnib
	move.l	%d2,%d0
	and.l	#0x0f000000,%d0
	cmp.l	#0x0f000000,%d0
	bne	fword
	bra	done

| fast word (flipped)
ffast:
	adda.l	%a0,%a1
	move.l	(%a1),%d2
#ifndef SPR_NOFAST1
	cmp.w	#0x200,%d3
	beq	ff1
#endif
	moveq	#7,%d7
ffnib:
	moveq	#15,%d4
	and.l	%d2,%d4
	ror.l	#4,%d2
	cmp.w	#0x200,%d1
	bge.s	ffnone
	move.w	(%a3,%d4.l*2),%d0
	beq.s	fftrans
	bmi.s	ffshad
ffopq:
	move.w	%d0,(%a2)
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	ffopq
	bra.s	ffnone
ffshad:
	move.w	(%a2),%d0
	and.w	#0x0fff,%d0
	add.w	#0x1000,%d0
	move.w	%d0,(%a2)
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	ffshad
	bra.s	ffnone
fftrans:
	adda.l	%a6,%a2
	add.w	%d3,%d1
	cmp.w	#0x200,%d1
	blt.s	fftrans
ffnone:
	sub.w	#0x200,%d1
	dbra	%d7,ffnib
	move.l	%d2,%d0
	and.l	#0x0f000000,%d0
	cmp.l	#0x0f000000,%d0
	bne	fword
	bra	done

#ifndef SPR_NOFAST1
ff1:
	.rept	8
	moveq	#15,%d4
	and.l	%d2,%d4
	ror.l	#4,%d2
	move.w	(%a3,%d4.l*2),%d0
	beq.s	1f
	bpl.s	2f
	move.w	(%a2),%d0
	and.w	#0x0fff,%d0
	add.w	#0x1000,%d0
2:	move.w	%d0,(%a2)
1:	adda.l	%a6,%a2
	.endr
	move.l	%d2,%d0
	and.l	#0x0f000000,%d0
	cmp.l	#0x0f000000,%d0
	bne	fword
	bra	done
#endif

done:
	move.l	%a1,%d0				| last source word used by this row
	sub.l	_atari_sr+R_SPRDATA,%d0
	asr.l	#2,%d0
	move.l	_atari_sr+R_ENDSLOT,%a1
	move.w	%d0,(%a1)
rows_skip:
	| accumulate the vertical zoom: addr += pitch * (yacc >> 9), yacc &= 0x1ff
	move.l	_atari_sr+R_YACC,%d0
	add.l	_atari_sr+R_VZOOM,%d0
	move.l	%d0,%d1
	and.l	#0x1ff,%d0
	move.l	%d0,_atari_sr+R_YACC
	lsr.l	#8,%d1
	lsr.l	#1,%d1
	beq.s	1f
	move.l	_atari_sr+R_PITCH,%d0
	muls.w	%d0,%d1
	add.l	%d1,_atari_sr+R_ADDR
1:	move.l	_atari_sr+R_ROWSTEP,%d0
	add.l	%d0,_atari_sr+R_ROW
	move.l	_atari_sr+R_Y,%d0
	add.l	_atari_sr+R_YDELTA,%d0
	move.l	%d0,_atari_sr+R_Y
	cmp.l	_atari_sr+R_YTARGET,%d0
	bne	rows_loop

	lea	12(%sp),%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

#endif
