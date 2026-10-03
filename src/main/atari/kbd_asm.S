| IKBD keyboard interrupt handler (installed on the ACIA / MFP level-6 vector by input.cpp).
|
| Runs in the interrupt: reads every byte waiting in the keyboard ACIA and keeps
|   atari_scan[scancode & 0x7f] = 1 (key down) / 0 (key up)
| up to date.  Mouse, status and clock packets from the IKBD are skipped by length
| (atari_ikbd_skip counts the bytes still to ignore).  The two joystick event
| packets are skipped by length the same way, but their one data byte is latched
| first: 0xff (joystick 1, the joystick-only port) into atari_joy1 and 0xfe
| (joystick 0, the port shared with the mouse, which reports as a joystick once
| input.cpp has disabled the mouse) into atari_joy0 - see atari_ikbd_joypending.
|
| Keeping the state in a byte table lets the game poll the keyboard without ever
| blocking (Bconin()/Cnecin() wait for a key press and give no key-up events).

	.text
	.even

	.globl	_atari_ikbd_isr
_atari_ikbd_isr:
	movem.l	%d0-%d1/%a0,-(%sp)
ik_next:
	move.b	0xfffffc00.w,%d0		| ACIA status
	btst	#0,%d0				| receive data register full?
	beq	ik_done
	moveq	#0,%d1
	move.b	0xfffffc02.w,%d1		| data (also clears the error flags)
	tst.b	_atari_ikbd_skip
	beq	ik_head
	subq.b	#1,_atari_ikbd_skip
	move.b	_atari_ikbd_joypending,%d0	| a joystick packet's one data byte? 1 = joystick 1, 2 = joystick 0
	beq	ik_next
	clr.b	_atari_ikbd_joypending
	subq.b	#1,%d0
	beq	ik_joy1_data
	move.b	%d1,_atari_joy0
	bra	ik_next
ik_joy1_data:
	move.b	%d1,_atari_joy1
	bra	ik_next
ik_head:
	cmp.w	#0xf6,%d1
	bcc	ik_packet			| 0xf6..0xff: start of a non-keyboard packet
	lea	_atari_scan,%a0
	move.w	%d1,%d0
	and.w	#0x7f,%d0
	tst.b	%d1
	bmi	ik_up
	move.b	#1,(%a0,%d0.w)
	bra	ik_next
ik_up:
	clr.b	(%a0,%d0.w)
	bra	ik_next
ik_packet:
	cmp.w	#0xff,%d1
	beq	ik_joy1_header
	cmp.w	#0xfe,%d1
	beq	ik_joy0_header
	sub.w	#0xf6,%d1
	lea	ik_skiptab(%pc),%a0
	move.b	(%a0,%d1.w),_atari_ikbd_skip
	bra	ik_next
ik_joy1_header:
	move.b	#1,_atari_ikbd_skip		| joystick-1 event: always exactly 1 data byte
	move.b	#1,_atari_ikbd_joypending
	bra	ik_next
ik_joy0_header:
	move.b	#1,_atari_ikbd_skip		| joystick-0 event: always exactly 1 data byte
	move.b	#2,_atari_ikbd_joypending
	bra	ik_next
ik_done:
	bclr	#6,0xfffffa11.w			| MFP: end of interrupt for the ACIA line
	movem.l	(%sp)+,%d0-%d1/%a0
	rte

ik_skiptab:
	.byte	7,5,2,2,2,2,6,2,1,1		| 0xf6 status, f7 abs mouse, f8-fb mouse, fc clock, fd joysticks, fe/ff joystick
						| (0xfe/0xff never actually reach this table - see ik_joy0_header/ik_joy1_header)

	.even
	.comm	_atari_scan,128
	.comm	_atari_ikbd_skip,2
	.comm	_atari_joy1,2			| last joystick-1 report byte: bit0 up, bit1 down, bit2 left,
						| bit3 right, bit7 fire (1 = active) - IKBD protocol section 5.1
	.comm	_atari_joy0,2			| last joystick-0 report byte, same layout
	.comm	_atari_ikbd_joypending,2
