| Inner loop of SegaPCM::stream_update() for one channel (segapcm.cpp, PLATFORM_ATARI builds).
|
|   uint32_t atari_pcm_channel(struct PcmCh* c);   (extern "C")
|
| struct PcmCh (all 32-bit):
|    0  rom      sample data of this channel's bank (indexed by addr >> 8, 0..0xFFFF)
|    4  buf      int16 stereo output (L,R,L,R...), summed into
|    8  count    number of frames to produce (1..65536)
|   12  addr     24-bit sample address
|   16  loop     address to jump to when the end is reached (looping sample)
|   20  end      high byte of addr that ends the sample (0..255)
|   24  inc      address increment per frame
|   28  lvol     left volume  (0..255)
|   32  rvol     right volume (0..255)
|   36  oneshot  non-zero: stop at the end instead of looping
|   40  tab      scratch, 2*256 int16
|   44  stopped  (out) 1 if the end was reached in one-shot mode
| Returns the final address.
|
| Same result as the original loop
|     if ((addr >> 16) == end) { if (!oneshot) addr = loop; else { stopped; break; } }
|     v = rom[addr >> 8] - 0x80;  buf[2i] += v * lvol;  buf[2i+1] += v * rvol;
|     addr = (addr + inc) & 0xffffff;
| but the products are looked up in two 256-entry tables built once per call
| (v * volume for every possible sample byte) instead of two multiplications per frame.
|
| Registers: d1 addr, d3 end, d4 inc, d6 stopped, d7 frames left, a0 rom, a1 buf,
|            a2 left table, a3 right table, a4 loop.

	.text
	.even

#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
	.macro	LOADT	tab,idx,dst
	move.w	(\tab,\idx\().l*2),\dst
	.endm
#else
	.macro	LOADT	tab,idx,dst
	add.w	\idx,\idx
	move.w	0(\tab,\idx\().w),\dst
	.endm
#endif

	.globl	_atari_pcm_channel
_atari_pcm_channel:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)		| 11 regs = 44 bytes
	move.l	48(%sp),%a6

	| tables: tab[u] = (u - 128) * vol as int16, built by running sums
	move.l	40(%a6),%a2
	move.l	28(%a6),%d5
	move.l	%d5,%d0
	lsl.l	#7,%d0
	neg.l	%d0
	move.w	#255,%d2
pt_l:	move.w	%d0,(%a2)+
	add.w	%d5,%d0
	dbra	%d2,pt_l
	move.l	32(%a6),%d5
	move.l	%d5,%d0
	lsl.l	#7,%d0
	neg.l	%d0
	move.w	#255,%d2
pt_r:	move.w	%d0,(%a2)+
	add.w	%d5,%d0
	dbra	%d2,pt_r
	move.l	40(%a6),%a2
	lea	512(%a2),%a3

	move.l	0(%a6),%a0
	move.l	4(%a6),%a1
	move.l	8(%a6),%d7
	subq.l	#1,%d7
	move.l	12(%a6),%d1
	move.l	16(%a6),%a4
	move.l	20(%a6),%d3
	move.l	24(%a6),%d4
	moveq	#0,%d6

pc_loop:
	move.l	%d1,%d0
	swap	%d0
	cmp.b	%d3,%d0
	beq.s	pc_hit
pc_fetch:
	move.l	%d1,%d0
	lsr.l	#8,%d0
	moveq	#0,%d2
	move.b	(%a0,%d0.l),%d2
	LOADT	%a2,%d2,%d0
	add.w	%d0,(%a1)+
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
	move.w	(%a3,%d2.l*2),%d0
#else
	move.w	0(%a3,%d2.w),%d0		| d2 already doubled by LOADT
#endif
	add.w	%d0,(%a1)+
	add.l	%d4,%d1
	and.l	#0xffffff,%d1
	dbra	%d7,pc_loop
	bra.s	pc_done
pc_hit:
	tst.l	36(%a6)
	bne.s	pc_stop
	move.l	%a4,%d1
	bra.s	pc_fetch
pc_stop:
	moveq	#1,%d6
pc_done:
	move.l	%d6,44(%a6)
	move.l	%d1,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts
