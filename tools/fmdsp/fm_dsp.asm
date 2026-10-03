; ---------------------------------------------------------------------------
; FM synthesis (YM2151 operators) on the Falcon's DSP56001.
;
; Computes exactly what tools/fmdsp/fm_engine.h computes - the part of the
; game's YM2151 emulation (src/main/hwaudio/ym2151.cpp) that makes the sound:
; envelope generator, operator phases, the four operators of each channel,
; the eight algorithms, feedback and the stereo mix. The 68k keeps decoding the
; registers and sends this program events through the host port (24-bit words):
;
;   $01000i  operator i parameters, then 8 words:
;            freq>>16, freq&$ffff, tl, d1l, then for AR, D1R, D2R, RR: sh<<8|sel
;   $02000c  channel c settings, then 2 words: con<<8|fb_shift, pan (bit0 L, bit1 R)
;   $0300vv  key on/off (register 8 value)
;   $0400nn  render one game step of nn frames: the program first sends back the
;            previous step (2*frames words, L then R, 16-bit in the low bits),
;            then computes this one
;   $050000  set up: then sin_tab (1024 words), tl_tab (6656), eg_inc (152),
;            eg_timer_add, eg_timer_overflow; answers $FA5E00 when done
;   $060000  answers $AC0000 (test aid: tells when the last render is finished)
;
; Assembled with a56 (tools/fmdsp/build_dsp.sh). Memory is kept below $1000 in P
; and between $1000 and $3DFF in X and Y, so it is safe whichever way the
; Falcon's 32K words are shared between the P, X and Y spaces.
;
; Copyright (c) port authors. See license.txt for more details.
; ---------------------------------------------------------------------------

HSR	equ	$ffe9		; host status: bit 0 receive full, bit 1 transmit empty
HRX	equ	$ffeb		; host receive / transmit data
HTX	equ	$ffeb

; --- internal X memory -------------------------------------------------------
S_TAB	equ	$00		; S[sh] = (eg_cnt >> sh) & 7, for sh up to the trailing zeros
V_M2	equ	$10		; operator connection variables of the channel being computed
V_C1	equ	$11
V_C2	equ	$12
V_MEM	equ	$13
V_OUT	equ	$14
EGT	equ	$20		; eg_timer
EGCNT	equ	$21		; eg_cnt
EGADD	equ	$22		; eg_timer_add
EGOVF	equ	$23		; eg_timer_overflow
TZEND	equ	$24		; S_TAB + number of valid S entries
OUTP	equ	$25		; where the next output frame goes
PREVN	equ	$27		; frames in the previous buffer
BUFCUR	equ	$28		; Y address of the buffer being filled
BUFPRV	equ	$29		; Y address of the previous buffer
NACT	equ	$2a		; active channels in this step
CHNUM	equ	$2b
TMP	equ	$2c
KEYV	equ	$2d
RATEW	equ	$30		; the four rate words of an operator event
DIRTY	equ	$34		; the rate lists must be rebuilt
NEMPTY	equ	$37		; bit sh set: rate list sh is not empty
KHALF	equ	$36		; $400000: pm scale of M2, C1, C2 (index += pm / 2)
B_NONE	equ	$38		; pan buckets: channel outputs summed by pan (B_NONE + pan)
B_L	equ	$39
B_R	equ	$3a
B_LR	equ	$3b
ACTL	equ	$40		; channels that can sound in this step: M1 address, C_REST address
SCST	equ	$50		; set_cur's state, limit, base
SCLIM	equ	$51
SCBASE	equ	$52
LSAVE	equ	$54		; eg_lim: 4 words

; --- external memory ----------------------------------------------------------
SINT	equ	$1000		; X: sin_tab, 1024 words
EGINC	equ	$1400		; X: eg_inc, 152 words
EGINC8	equ	$1c00		; X: eg_inc * 8
OPS	equ	$1500		; X: 32 operators x 32 words
CHS	equ	$1900		; X: 8 channels x 16 words
ROUTE	equ	$1980		; X: 8 algorithms x 4 words (restore, M1, M2, C1 destinations)
FBKT	equ	$19a0		; X: feedback scale for fb_shift 0..15
LISTS	equ	$1a00		; X: 12 rate lists x 33 words
TL	equ	$1000		; Y: tl_tab, 6656 words, then zeros up to TL+TLEXT
TLEXT	equ	11008		; ENV8 (at most 6656) + the largest sin_tab value (4277) fits
KCONST	equ	$3c00		; Y: $3ff (index mask)
BUF0	equ	$2000		; X: output buffers, 2 x 1024 words
BUF1	equ	$2400

; operator fields (FH .. TLB: read in this order by the channel code; TLB .. CBASE8:
; by the EG list loop, from CBASE8 down)
O_FH	equ	0
O_FL	equ	1
O_PH	equ	2
O_PL	equ	3
O_TLB	equ	4		; TL + ENV8, ENV8 = (tl + volume) * 8 at most TLLEN
O_VOL8	equ	5		; volume * 8
O_LIM8	equ	6		; current state's limit * 8: D1L (decay), 1023 (sustain, release), -1 (attack)
O_TL8B	equ	7		; tl * 8 + TL
O_CBASE8	equ	8		; current state's EGINC8 + sel (EGINC + sel in attack)
O_STATE	equ	9
O_KEY	equ	10
O_D1L8	equ	11		; d1l * 8
O_CSHA	equ	12		; current state's rate: S_TAB+sh, or NEVER
O_SHAR	equ	13		; AR shift as is (key on)
O_RATES	equ	14		; 4 x (SHA, sel) for AR, D1R, D2R, RR
OPSIZE	equ	32

; channel fields (C_REST .. C_BUCK in the order the channel code reads them)
C_ON	equ	0		; (unused)
C_REST	equ	1		; address of the variable MEM is restored into
C_FBP	equ	2
C_FBC	equ	3
C_MEMV	equ	4
C_M1D	equ	5		; address M1's output goes to (V_C1 for algorithm 5, see C_ALG5)
C_ALG5	equ	6
C_FBK	equ	7
C_M2D	equ	8
C_C1D	equ	9
C_BUCK	equ	10		; pan bucket
CHSIZE	equ	16

NEVER	equ	$7fffff
EG_OFF	equ	0
EG_REL	equ	1
EG_SUS	equ	2
EG_DEC	equ	3
EG_ATT	equ	4
TLLEN	equ	6656

	org	p:$0
	jmp	start

	org	p:$40
; ---------------------------------------------------------------------------
; What runs for every sample comes first, in the DSP's internal program RAM
; (P:$40-$1FF): no wait state when an instruction also reads external data.
; ---------------------------------------------------------------------------
; render, continued from ev_render
rn_frames
	jsr	build_lists
	move	x:BUFCUR,x0
	move	x0,x:OUTP
	move	x:TMP,x0
	do	x0,rn_frame
	; envelope generator: eg_timer += eg_timer_add, one EG step per overflow
	move	x:EGT,a
	move	x:EGADD,x0
	add	x0,a
	move	x:EGOVF,x0
	cmp	x0,a
	jlt	rn_noeg
rn_eg	sub	x0,a
	move	a1,x:EGT
	jsr	eg_step
	move	x:EGT,a
	move	x:EGOVF,x0
	cmp	x0,a
	jge	rn_eg
rn_noeg	move	a1,x:EGT
	clr	a	#ACTL,r7
	move	a,x:B_L
	move	a,x:B_R
	move	a,x:B_LR
	move	#OPSIZE-4,n0
	move	#KCONST,r6
	do	x:NACT,rn_chans
	move	x:(r7)+,r0		; M1
	move	x:(r7)+,r2		; C_REST
; ---------------------------------------------------------------------------
; One channel (ym2151.cpp chan_calc): r0 = its M1, r2 -> its C_REST,
; n0 = OPSIZE-4, r4 = SINT, r6 -> Y:KCONST. Each operator: phase += freq,
; index = (P16 + pm * scale) & $3ff, output = tl_tab[ENV8 + sin_tab[index]]
; (TL + ENV8 is kept as a Y address; past TLLEN, tl_tab is zeros). Its output is
; added to its pan bucket (B_NONE + pan).
	clr	a	#V_M2,r3
	rep	#5
	move	a,x:(r3)+		; V_M2 .. V_OUT = 0
	move	x:(r2)+,r3		; C_REST           r2 -> C_FBP
	move	r2,r1
	move	x:(r2)+,b		; FB prev          r2 -> C_FBC
	move	x:(r2)+,a		; FB curr          r2 -> C_MEMV
	add	a,b	a,x:(r1)+	; fb = prev + curr; prev = curr   r1 -> C_FBC
	move	x:(r2)+,x0		; MEMV             r2 -> C_M1D
	move	x0,x:(r3)		; MEM restore
	move	x:(r2)+,r3		; M1 destination   r2 -> C_ALG5
	move	b,y0			; pm = fb
	move	x:(r2)+,b		; algorithm 5?     r2 -> C_FBK
	move	a,x:(r3)		; M1's previous output to its destination
	tst	b	x:(r2)+,y1	; feedback scale   r2 -> C_M2D
	jeq	cc_n5
	move	a,x:V_MEM
	move	a,x:V_C2
cc_n5
	; M1 (feedback)
	move	x:(r0)+,x1		; FH
	move	x:(r0)+,x0		; FL
	move	x:(r0)+,a		; P16
	move	x:(r0)-,a0		; PL
	add	x,a	a1,x1		; phase += freq; x1 = P16 before
	mpy	y1,y0,b	a1,x:(r0)+	; b1 = pm * scale
	add	x1,b	a0,x:(r0)+	; + P16
	move	y:(r6),x0
	and	x0,b	x:(r0)+n0,r5	; b1 = sin_tab index; r5 = TL + ENV8; r0 -> next operator
	move	b1,n4
	move	x:(r0)+,x1		; (M2's FH)
	move	x:(r4+n4),n5		; sin_tab[index]
	move	x:(r0)+,x0		; (M2's FL)
	move	y:(r5+n5),b		; tl_tab[ENV8 + sin]: M1's output
	move	b,x:(r1)+		; FB curr          r1 -> C_MEMV
	; M2 (input m2)
	move	x:V_M2,y0
	move	x:KHALF,y1		; pm / 2 from here on
	move	x:(r2)+,r3		; M2 destination   r2 -> C_C1D
	move	x:(r0)+,a		; P16
	move	x:(r0)-,a0		; PL
	add	x,a	a1,x1		; phase += freq; x1 = P16 before
	mpy	y1,y0,b	a1,x:(r0)+	; b1 = pm * scale
	add	x1,b	a0,x:(r0)+	; + P16
	move	y:(r6),x0
	and	x0,b	x:(r0)+n0,r5	; b1 = sin_tab index; r5 = TL + ENV8; r0 -> next operator
	move	b1,n4
	move	x:(r0)+,x1		; (C1's FH)
	move	x:(r4+n4),n5
	move	x:(r0)+,x0		; (C1's FL)
	move	x:(r3),a
	move	y:(r5+n5),b		; M2's output
	add	b,a	x:V_C1,y0	; to its destination; C1's input
	move	a,x:(r3)
	; C1 (input c1)
	move	x:(r2)+,r3		; C1 destination   r2 -> C_BUCK
	move	x:(r0)+,a		; P16
	move	x:(r0)-,a0		; PL
	add	x,a	a1,x1		; phase += freq; x1 = P16 before
	mpy	y1,y0,b	a1,x:(r0)+	; b1 = pm * scale
	add	x1,b	a0,x:(r0)+	; + P16
	move	y:(r6),x0
	and	x0,b	x:(r0)+n0,r5	; b1 = sin_tab index; r5 = TL + ENV8; r0 -> next operator
	move	b1,n4
	move	x:(r0)+,x1		; (C2's FH)
	move	x:(r4+n4),n5
	move	x:(r0)+,x0		; (C2's FL)
	move	x:(r3),a
	move	y:(r5+n5),b		; C1's output
	add	b,a	x:V_C2,y0	; to its destination; C2's input
	move	a,x:(r3)
	; C2 (input c2)
	move	x:(r2)+,r3		; pan bucket
	move	x:(r0)+,a		; P16
	move	x:(r0)-,a0		; PL
	add	x,a	a1,x1		; phase += freq; x1 = P16 before
	mpy	y1,y0,b	a1,x:(r0)+	; b1 = pm * scale
	add	x1,b	a0,x:(r0)+	; + P16
	move	y:(r6),x0
	and	x0,b	x:(r0)+n0,r5	; b1 = sin_tab index; r5 = TL + ENV8; r0 -> next operator
	move	b1,n4
	move	x:V_OUT,a
	move	x:(r4+n4),n5
	nop
	move	y:(r5+n5),b		; C2's output
	add	b,a	x:V_MEM,x0	; a = channel output
	move	x:(r3),b
	add	a,b	x0,x:(r1)	; bucket += output; MEM kept for the next sample
	move	b,x:(r3)
rn_chans
	; L = both + left only, R = both + right only; clip, * 128 >> 8
	move	x:OUTP,r7
	move	#>32767,x0
	move	#>$ff8000,x1
	move	x:B_LR,a
	move	x:B_L,y0
	add	y0,a
	cmp	x0,a
	tgt	x0,a
	cmp	x1,a
	tlt	x1,a
	asr	a	x:B_LR,b
	move	a1,x:(r7)+
	move	x:B_R,y0
	add	y0,b
	cmp	x0,b
	tgt	x0,b
	cmp	x1,b
	tlt	x1,b
	asr	b
	move	b1,x:(r7)+
	move	r7,x:OUTP
rn_frame
	jmp	main

eg_step
	move	x:DIRTY,a
	tst	a
	jeq	es_clean
	jsr	build_lists
es_clean
	move	x:EGCNT,a
	move	#>1,x0
	add	x0,a
	move	a1,x:EGCNT
	; eg_cnt ^ (eg_cnt - 1): bits 0 .. trailing zeros, the shifts that move now
	move	a1,x1
	sub	x0,a
	eor	x1,a
	move	x:NEMPTY,x0
	and	x0,a
	jne	es_work
	rts				; none of their lists has an operator
es_work	move	x1,a
	; S[sh] = (eg_cnt >> sh) & 7 while the bits below sh are all zero (at most 12)
	move	#S_TAB,r6
	move	#>7,y0
	do	#12,es_tz
	tfr	a,b
	and	y0,b
	move	b1,x:(r6)+
	jclr	#0,a1,es_more
	enddo
	jmp	es_tz
es_more	lsr	a
	nop
es_tz
	; the lists of shifts 0 .. trailing zeros
	move	r6,x0			; number of lists (S_TAB = 0)
	move	#LISTS,r3
	move	#S_TAB,r6
	move	#>TL+TLLEN,x1
	do	x0,es_lists
	move	x:(r3)+,b		; count
	move	r3,r2
	tst	b	x:(r6)+,x0	; x0 = S[sh]
	jeq	es_empty
	move	b1,n1
	nop
	do	n1,es_ops
	; one operator (decay, sustain, release: volume += inc, up to the state's limit;
	; attack and the limits go the slow way, eg_lim)
	move	x:(r3)+,r0		; its CBASE8
	nop
	move	x:(r0)-,a		; CBASE8
	add	x0,a	x:(r0)-,y1	; + S[sh]; y1 = TL8B
	move	a1,r1
	move	x:(r0)-,b		; LIM8
	move	x:(r0),a		; VOL8
	move	x:(r1),y0		; inc * 8 (inc in attack)
	add	y0,a
	cmp	b,a
	jsge	eg_lim
	move	a1,x:(r0)-		; VOL8
	add	y1,a
	cmp	x1,a
	tgt	x1,a
	move	a1,x:(r0)		; TLB
es_ops
es_empty
	; next list: 33 words after this one's first slot - 1
	move	#32,n3
	move	r2,r3
	nop
	move	(r3)+n3
es_lists
	rts

; ---------------------------------------------------------------------------
start
	movep	#0,x:$fffe		; no wait states for external memory
	movec	#$ffff,m0
	movec	#$ffff,m1
	movec	#$ffff,m2
	movec	#$ffff,m3
	movec	#$ffff,m4
	movec	#$ffff,m5
	movec	#$ffff,m6
	movec	#$ffff,m7
	move	#SINT,r4
	move	#TL,r5
	clr	a
	move	a,x:PREVN
	move	#BUF0,x0
	move	x0,x:BUFCUR
	move	#BUF1,x0
	move	x0,x:BUFPRV

; ---------------------------------------------------------------------------
main
	jsr	get
	move	a1,x:KEYV
	move	#>$ffff,x0
	and	x0,a		; a1 = payload
	move	x:KEYV,b
	rep	#16
	lsr	b		; b1 = event type
	move	#>1,x0
	cmp	x0,b
	jeq	ev_op
	move	#>2,x0
	cmp	x0,b
	jeq	ev_ch
	move	#>3,x0
	cmp	x0,b
	jeq	ev_key
	move	#>4,x0
	cmp	x0,b
	jeq	ev_render
	move	#>5,x0
	cmp	x0,b
	jeq	ev_init
	move	#>6,x0
	cmp	x0,b
	jeq	ev_ping
	jmp	main

; $06: answers $AC0000 (once the previous render is finished: tells its duration)
ev_ping	move	#>$ac0000,a
	jsr	put
	jmp	main

; --- host port -----------------------------------------------------------------
get	jclr	#0,x:HSR,get
	movep	x:HRX,a
	rts

put	jclr	#1,x:HSR,put
	movep	a,x:HTX
	rts

; r1 = OPS + a1 * 32
op_addr
	rep	#5
	asl	a
	move	#OPS,x0
	add	x0,a
	move	a1,r1
	rts

; ---------------------------------------------------------------------------
; $05: tables, then reset
ev_init
	move	#SINT,r0
	do	#1024,ini1
	jsr	get
	move	a1,x:(r0)+
ini1
	move	#TL,r0
	move	#>6656,x0
	do	x0,ini2
	jsr	get
	move	a1,y:(r0)+
ini2
	clr	a
	move	#>TLEXT-6656,x0
	do	x0,ini2z
	move	a,y:(r0)+
ini2z
	move	#>$3ff,x0
	move	x0,y:KCONST
	move	#>$400000,x0
	move	x0,x:KHALF
	move	#>1,x0
	move	x0,x:DIRTY
	move	#EGINC,r0
	move	#EGINC8,r1
	do	#152,ini3
	jsr	get
	move	a1,x:(r0)+
	rep	#3
	asl	a
	move	a1,x:(r1)+
ini3
	jsr	get
	move	a1,x:EGADD
	jsr	get
	move	a1,x:EGOVF
	; operators
	move	#OPS,r0
	do	#32,ini4
	move	r0,r1
	clr	a
	rep	#OPSIZE
	move	a,x:(r1)+
	move	#O_VOL8,n0
	move	#>8184,x0
	move	x0,x:(r0+n0)
	move	#O_TL8B,n0
	move	#TL,x0
	move	x0,x:(r0+n0)
	move	#O_TLB,n0
	move	#>TL+TLLEN,x0
	move	x0,x:(r0+n0)
	move	#O_CSHA,n0
	move	#>NEVER,x0
	move	x0,x:(r0+n0)
	move	#OPSIZE,n0
	nop
	move	(r0)+n0
ini4
	; channels
	move	#CHS,r0
	clr	a
	rep	#128
	move	a,x:(r0)+
	; routing per algorithm: restore, M1, M2, C1 destinations
	move	#ROUTE,r0
	move	#rtab,r1
	do	#32,ini5
	movem	p:(r1)+,x0
	move	x0,x:(r0)+
ini5
	; feedback scales: 0 for fb_shift 0, else 2^(fb_shift+7)
	move	#FBKT,r0
	move	#fbktab,r1
	do	#16,ini6
	movem	p:(r1)+,x0
	move	x0,x:(r0)+
ini6
	clr	a
	move	a,x:EGT
	move	a,x:EGCNT
	move	a,x:PREVN
	move	#>$fa5e00,a
	jsr	put
	jmp	main

rtab	dc	V_M2,V_C1,V_C2,V_MEM	; 0: M1-C1-MEM-M2-C2
	dc	V_M2,V_MEM,V_C2,V_MEM	; 1
	dc	V_M2,V_C2,V_C2,V_MEM	; 2
	dc	V_C2,V_C1,V_C2,V_MEM	; 3
	dc	V_MEM,V_C1,V_C2,V_OUT	; 4
	dc	V_M2,V_C1,V_OUT,V_OUT	; 5 (M1 also to MEM and C2, see C_ALG5)
	dc	V_MEM,V_C1,V_OUT,V_OUT	; 6
	dc	V_MEM,V_OUT,V_OUT,V_OUT	; 7

fbktab	dc	0,0,0,0,0,0,0,$4000,$8000,$10000,$20000,$40000,$80000,$100000,0,0

; ---------------------------------------------------------------------------
; $01: operator parameters
ev_op
	jsr	op_addr			; r1 = operator
	move	r1,r0
	jsr	get			; freq >> 16
	move	a1,x:(r1)+		; FH
	jsr	get			; freq & $ffff
	rep	#8
	asl	a
	move	a1,x:(r1)+		; FL
	move	#O_TL8B,n0
	jsr	get
	rep	#3
	asl	a
	move	#TL,x0
	add	x0,a
	move	a1,x:(r0+n0)		; TL8B
	move	#O_D1L8,n0
	jsr	get
	rep	#3
	asl	a
	move	a1,x:(r0+n0)		; D1L8
	; the four rates: read them all first
	move	#RATEW,r2
	do	#4,op_rd
	jsr	get
	move	a1,x:(r2)+
op_rd
	move	#O_RATES,n0
	move	#RATEW,r2
	lua	(r0)+n0,r1
	do	#4,op_rt
	move	x:(r2)+,a
	move	a1,b
	move	#>$ff,x0
	and	x0,a			; a1 = sel
	rep	#8
	lsr	b			; b1 = sh
	move	#S_TAB,x0
	add	x0,b
	move	b1,x:(r1)+		; SHA
	move	a1,x:(r1)+		; sel
op_rt
	; AR shift as is, for key on
	move	x:RATEW,a
	rep	#8
	lsr	a
	move	#O_SHAR,n0
	nop
	move	a1,x:(r0+n0)
	jsr	set_cur
	jsr	set_env
	jmp	main

; ---------------------------------------------------------------------------
; $02: channel settings
ev_ch
	move	a1,x:CHNUM
	rep	#4
	asl	a			; * 16
	move	#CHS,x0
	add	x0,a
	move	a1,r0
	jsr	get			; con << 8 | fb_shift
	move	a1,x:TMP
	move	#>$f,x0
	and	x0,a
	move	a1,n1
	move	#FBKT,r1
	nop
	move	x:(r1+n1),x0
	move	#C_FBK,n0
	nop
	move	x0,x:(r0+n0)
	move	x:TMP,a
	rep	#8
	lsr	a			; con
	move	#>7,x0
	and	x0,a
	move	#>5,x0
	cmp	x0,a
	move	#0,b
	jne	chn5
	move	#>1,b
chn5	move	#C_ALG5,n0
	nop
	move	b1,x:(r0+n0)
	asl	a
	asl	a			; con * 4
	move	#ROUTE,x0
	add	x0,a
	move	a1,r1
	move	#C_REST,n0
	nop
	move	x:(r1)+,x0
	move	x0,x:(r0+n0)		; restore
	move	#C_M1D,n0
	nop
	move	x:(r1)+,x0
	move	x0,x:(r0+n0)		; M1
	move	#C_M2D,n0
	nop
	move	x:(r1)+,x0
	move	x0,x:(r0+n0)		; M2
	move	#C_C1D,n0
	nop
	move	x:(r1)+,x0
	move	x0,x:(r0+n0)		; C1
	jsr	get			; pan: bucket B_NONE + (bit 0 L, bit 1 R)
	move	#>3,x0
	and	x0,a
	move	#B_NONE,x0
	add	x0,a
	move	#C_BUCK,n0
	nop
	move	a1,x:(r0+n0)
	jmp	main

; ---------------------------------------------------------------------------
; $03: key on/off (register 8): channel v & 7, bits 3 M1, 5 M2, 4 C1, 6 C2
ev_key
	move	a1,x:KEYV
	move	#>7,x0
	and	x0,a
	rep	#2
	asl	a			; channel * 4 = first operator
	jsr	op_addr
	move	r1,r0
	move	#OPSIZE,n0
	move	x:KEYV,a
	jsr	key1			; M1: bit 3
	move	x:KEYV,a
	lsr	a
	lsr	a			; M2: bit 5 -> bit 3
	jsr	key1
	move	x:KEYV,a
	lsr	a			; C1: bit 4 -> bit 3
	jsr	key1
	move	x:KEYV,a
	rep	#3
	lsr	a			; C2: bit 6 -> bit 3
	jsr	key1
	jmp	main

; r0 = operator, a1 bit 3 = key; leaves r0 on the next operator
key1
	move	#O_KEY,n1
	move	r0,r1
	jclr	#3,a1,kyoff
	move	x:(r1+n1),b
	tst	b
	jne	kyset
	; KEY_ON: phase 0, attack, volume += (~volume * inc) >> 4
	clr	b
	move	#O_PH,n1
	nop
	move	b,x:(r1+n1)
	move	#O_PL,n1
	nop
	move	b,x:(r1+n1)
	move	#O_SHAR,n1
	nop
	move	x:(r1+n1),x0		; sh_ar
	move	x:EGCNT,a
	move	x0,b
	tst	b
	jeq	kysh0
	rep	x0
	lsr	a
kysh0	move	#>7,x0
	and	x0,a
	move	#O_RATES+1,n1		; sel_ar
	nop
	move	x:(r1+n1),x0
	add	x0,a
	move	#EGINC,x0
	add	x0,a
	move	a1,r2
	move	#O_VOL8,n1
	move	x:(r2),y0		; inc
	move	x:(r1+n1),a
	jsr	att_step
	move	#>EG_ATT,x0
	jgt	kyatt
	clr	b
	move	#>EG_DEC,x0
kyatt	rep	#3
	asl	b
	move	b1,x:(r1+n1)		; VOL8
	move	#O_STATE,n1
	nop
	move	x0,x:(r1+n1)
	jsr	set_cur
	jsr	set_env
kyset	move	#O_KEY,n1
	move	#>1,x0
	move	r0,r1
	nop
	move	x0,x:(r1+n1)
	jmp	kynext
kyoff	move	x:(r1+n1),b		; KEY_OFF: only if the key was on
	tst	b
	jeq	kynext
	clr	b
	move	b,x:(r1+n1)
	move	#O_STATE,n1
	nop
	move	x:(r1+n1),b
	move	#>EG_REL,x0
	cmp	x0,b
	jle	kynext
	move	x0,x:(r1+n1)
	jsr	set_cur
kynext	move	#OPSIZE,n0
	nop
	move	(r0)+n0
	rts

; ---------------------------------------------------------------------------
; $04: render nn frames
ev_render
	move	a1,x:TMP		; frames of this step
	; send back the step computed at the last render
	move	x:PREVN,a
	tst	a
	jeq	rn_nosend
	asl	a
	move	a1,x0
	move	x:BUFCUR,r0
	do	x0,rn_send
rn_tx	jclr	#1,x:HSR,rn_tx
	movep	x:(r0)+,x:HTX
rn_send
rn_nosend
	; channels that can sound in this step (ym2151.cpp stream_update's rule)
	clr	a
	move	a,x:NACT
	move	#OPS,r0
	move	#CHS,r2
	move	#ACTL,r7
	move	#OPSIZE,n0
	do	#8,rn_chon
	move	r0,y1			; the channel's M1
	clr	b			; on?
	do	#4,rn_opon
	move	#O_STATE,n1
	move	r0,r1
	nop
	move	x:(r1+n1),a
	tst	a
	jeq	rn_skip			; off
	move	#>EG_REL,x0
	cmp	x0,a
	jne	rn_on
	move	#O_TLB,n1
	nop
	move	x:(r1+n1),a
	move	#>TL+TLLEN,x0
	cmp	x0,a
	jge	rn_skip			; released and below the threshold
rn_on	move	#>1,b
rn_skip	move	(r0)+n0
rn_opon
	tst	b
	jne	rn_act
	; silent: what two more samples would leave behind
	move	#C_FBP,n2
	nop
	move	b,x:(r2+n2)
	move	#C_FBC,n2
	nop
	move	b,x:(r2+n2)
	move	#C_MEMV,n2
	nop
	move	b,x:(r2+n2)
	jmp	rn_nx
rn_act	move	x:NACT,a
	move	#>1,x0
	add	x0,a
	move	a1,x:NACT
	move	y1,x:(r7)+		; ACTL: its M1, its C_REST
	move	r2,a
	move	#>C_REST,x0
	add	x0,a
	move	a1,x:(r7)+
rn_nx	move	#CHSIZE,n2
	nop
	move	(r2)+n2
rn_chon
	; swap the buffers: the next step goes into the other one
	move	x:BUFCUR,x0
	move	x:BUFPRV,x1
	move	x1,x:BUFCUR
	move	x0,x:BUFPRV
	move	x:TMP,x0
	move	x0,x:PREVN
	move	x:BUFCUR,r7
	move	x:NACT,a
	tst	a
	jne	rn_frames
	; nothing can sound: silence, and nothing else moves (ym2151.cpp skip_frame)
	clr	a
	move	x:TMP,b
	asl	b
	move	b1,x1
	do	x1,rn_sil
	move	a,x:(r7)+
rn_sil
	jmp	main


; ---------------------------------------------------------------------------
; EG slow path, from the list loop: an operator in attack, or reaching its state's
; limit. r0 -> its VOL8, a = VOL8 + inc, y0 = inc. Returns a1 = its new VOL8, with
; its state and rate updated. Keeps x0, x1, y1, r2, r3, r6 (the list loop's).
eg_lim
	move	x0,x:LSAVE
	move	r3,x:LSAVE+1
	move	r0,x:LSAVE+2
	move	#O_STATE-O_VOL8,n0
	nop
	move	x:(r0+n0),b
	move	#>EG_ATT,x0
	cmp	x0,b
	jne	el_lin
	move	x:(r0),a		; attack: volume += (~volume * inc) >> 4
	jsr	att_step
	tfr	b,a
	jgt	el_ret8
	clr	a			; at 0: decay
	move	#>EG_DEC,x0
	jmp	el_chg
el_lin	move	#>EG_DEC,x0
	cmp	x0,b
	move	#>EG_SUS,x0
	jeq	el_chg			; decay at D1L: sustain
	move	#>8184,a		; sustain, release at 1023: off
	move	#>EG_OFF,x0
el_chg	move	a1,x:(r0)		; VOL8 (set_cur looks at it)
	move	x0,x:(r0+n0)		; state
	move	#-O_VOL8,n0
	nop
	lua	(r0)+n0,r0		; the operator
	move	a1,x:LSAVE+3
	jsr	set_cur
	move	x:LSAVE+3,a
	jmp	el_rest
el_ret8	rep	#3
	asl	a
el_rest	move	x:LSAVE,x0
	move	x:LSAVE+1,r3
	move	x:LSAVE+2,r0
	move	#>TL+TLLEN,x1
	rts

; a = VOL8 in attack, y0 = inc: b = volume + ((~volume * inc) >> 4), flags set on b
att_step
	rep	#3
	asr	a			; a1 = volume
	move	a1,b
	not	a			; ~volume
	move	a1,x0
	mpy	x0,y0,a			; a1:a0 = 2 * ~volume * inc
	rep	#5
	asr	a			; a0 = (~volume * inc) >> 4
	move	a0,x0
	add	x0,b
	rts

; r0 = operator: TLB = min(VOL8 + TL8B, TL + TLLEN) (tl_tab is zero past TLLEN)
set_env
	move	#O_VOL8,n0
	nop
	move	x:(r0+n0),a
	move	#O_TL8B,n0
	nop
	move	x:(r0+n0),x0
	add	x0,a
	move	#>TL+TLLEN,x0
	cmp	x0,a
	tgt	x0,a
	move	#O_TLB,n0
	nop
	move	a1,x:(r0+n0)
	rts

; r0 = operator: from the rate of its current state, CSHA (the rate list it goes
; in), CBASE8 and LIM8; the lists are rebuilt at the next EG step. A zero rate
; (eg_inc row 18) never moves the volume, so such an operator is left out of the
; lists (NEVER) unless its state changes at the next step anyway (attack at
; volume 0, decay at D1L, sustain or release at 1023) - as ym2151.cpp, which
; looks at every operator at every step.
set_cur
	move	#O_STATE,n0
	move	#>1,x0
	move	x0,x:DIRTY
	move	x:(r0+n0),b
	tst	b
	jeq	sc_off
	move	b1,x:SCST
	; state 4 ATT -> rate 0, 3 DEC -> 1, 2 SUS -> 2, 1 REL -> 3: offset = O_RATES + 2*(4-state)
	move	#>4,a
	sub	b,a
	asl	a
	move	#O_RATES,x0
	add	x0,a
	move	a1,n0
	nop
	lua	(r0)+n0,r3
	nop
	move	x:(r3)+,x0		; SHA
	move	x:(r3),x1		; sel
	move	#>EG_ATT,a
	cmp	a,b
	jne	sc_lin
	move	#>-1,a			; attack: every step goes the slow way
	move	a1,x:SCLIM
	move	#EGINC,a
	jmp	sc_base
sc_lin	move	#>EG_DEC,a
	cmp	a,b
	move	#>8184,a		; sustain, release: 1023 * 8
	jne	sc_lim
	move	#O_D1L8,n0
	nop
	move	x:(r0+n0),a		; decay: D1L * 8
sc_lim	move	a1,x:SCLIM
	move	#EGINC8,a
sc_base	add	x1,a
	move	a1,x:SCBASE
	move	#>$90,a			; zero rate?
	cmp	x1,a
	jne	sc_set
	move	#O_VOL8,n0
	nop
	move	x:(r0+n0),a
	move	x:SCST,b
	move	#>EG_ATT,x1
	cmp	x1,b
	jne	sc_zl
	tst	a
	jle	sc_set
	jmp	sc_nev
sc_zl	move	x:SCLIM,b
	cmp	b,a
	jge	sc_set
sc_nev	move	#>NEVER,x0
sc_set	move	#O_CSHA,n0
	nop
	move	x0,x:(r0+n0)
	move	#O_CBASE8,n0
	move	x:SCBASE,x0
	move	x0,x:(r0+n0)
	move	#O_LIM8,n0
	move	x:SCLIM,x0
	move	x0,x:(r0+n0)
	rts
sc_off	move	#O_CSHA,n0
	move	#>NEVER,x0
	move	x0,x:(r0+n0)
	rts

; The operators are kept in 12 lists by the shift of their current rate (LISTS:
; for each shift a count, then up to 32 operator addresses), rebuilt whenever a
; current rate changed (DIRTY). At an EG step, eg_cnt's trailing zeros say which
; shifts move: the operators of those lists, and only those, are looked at.
build_lists
	move	#LISTS,r3
	move	#33,n3
	clr	a
	do	#12,bl_clr
	move	a,x:(r3)+n3
bl_clr
	move	#OPS+O_CSHA,r1
	move	#OPSIZE,n1
	move	#OPS,x1
	do	#32,bl_ops
	move	x:(r1),a		; S_TAB + sh, or NEVER
	move	#>NEVER,x0
	cmp	x0,a
	jeq	bl_next
	; list = LISTS + sh * 33
	move	#S_TAB,x0
	sub	x0,a
	move	a1,y0
	move	#>33,x0
	mpy	x0,y0,a
	asr	a
	move	a0,a
	move	#LISTS,x0
	add	x0,a
	move	a1,r3
	nop
	move	x:(r3),b		; count
	move	b1,n3
	move	#>1,x0
	add	x0,b
	move	b1,x:(r3)+		; count + 1, r3 -> first slot
	nop
	lua	(r3)+n3,r3
	move	r1,a			; the operator's CBASE8 = r1 - O_CSHA + O_CBASE8
	move	#>O_CSHA-O_CBASE8,x0
	sub	x0,a
	move	a1,x:(r3)
bl_next	move	(r1)+n1
bl_ops
	; NEMPTY
	clr	a	#LISTS,r3
	move	#>1,x1
	move	#33,n3
	do	#12,bl_ne
	move	x:(r3)+n3,b
	tst	b
	jeq	bl_e
	or	x1,a
bl_e	move	x1,b
	asl	b
	move	b1,x1
bl_ne
	move	a1,x:NEMPTY
	clr	a
	move	a,x:DIRTY
	rts

	end
