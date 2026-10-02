# DSP 56001 notes (Falcon)

> **Superseded.** The home-made DSP mixer these notes build up to (`dsp_mixer.*`) has been
> removed: it uploaded every sample to the DSP and could not hold a real module. `.mod`
> playback on the DSP now uses Simplet / ABSTRACT's streaming replay from dhs.nu, see
> `src/main/atari/dsp_replay.hpp`. What follows is kept as a record of what was measured.

Status: **toolchain proven end-to-end, both directions (host-port round-trip AND continuous
DSP-to-CODEC audio streaming); YM2151 core not started.** These notes record what was measured,
what was verified, and the plan for what's left.

## Where the DSP could help

Measured on Hatari (Falcon 030 at 16 MHz, one game step = 1/30 s of sound, 417 stereo frames):

| Part | Cost per step |
|---|---|
| YM2151 (FM synthesis) | 321 ms (the budget for a whole step is 33 ms) |
| SegaPCM (after the assembler channel loop) | 12 ms |
| mixing to 8 bit | 1.6 ms |

The FM chip is the only DSP candidate: it is a fixed-rate stream of multiply-accumulate work with a
small state (32 operators), which is what the 56001 does well. On a 68060 the same code takes 12 ms,
so a DSP version mainly matters for the 030, where sound is currently muted whenever the machine
is behind (which is always).

## Where it does not

* **Video (route, sprites, tiles, RGB conversion).** The CPU would have to send the whole index
  buffer (143 KB per picture) through the host port and read the result back; the port has no
  DMA to ST-RAM. That costs about as much as the conversion it replaces (about 80 ms on the 030).
  Confirmed independently by the F030TREX project (github.com/AnimaInCorpore/F030TREX): even
  compact 3D vertex data sent back through the host port cost 84.5 ms/frame on real hardware.
* **Game logic.** 16/32 bit integer code full of branches, on a 24 bit machine with no direct
  access to ST-RAM: it would be a rewrite, not a port.

The 68030 with assembler loops stays the video path.

## Toolchain: no installed DSP assembler, so one was hand-written

No native/host C compiler, bison, flex or m4 exists on this machine (only the m68k-atari-mint
*cross* compiler), and no precompiled DSP56001 assembler binary is available for Windows. Rather
than install anything, a minimal Python encoder was hand-written for exactly the instructions
needed (`C:\claude\dsptest\dspasm.py`).

**Every instruction encoding was derived from Hatari's own DSP56001 core source**
(github.com/hatari/hatari, `src/falcon/dsp_cpu.c`), not from memory — the opcode format is dense
and easy to get subtly wrong by hand. Two independent checks were used before ever touching real
hardware/emulation: (1) a from-scratch re-implementation of the same dispatch formula in the
*decode* direction (`dspdecode_check.py`), confirming the encoder's output maps back to the
intended mnemonics; (2) actually running the result.

**Loading verified working**: `Dsp_ExecBoot()` (not `Dsp_ExecProg()`) is the right XBIOS call for a
fully self-contained program — it drives the DSP56001's own hardware bootstrap mode (fixed chip
behaviour: reset + 512 words in over the host port lands at P:0 and runs), so there is no need to
reverse-engineer EmuTOS's `dspstart` software relay protocol that `Dsp_ExecProg()` goes through
first. (EmuTOS bios source confirms the wire format either way: `Dsp_DoBlock`/`Dsp_BlkHandShake`
send/receive 24-bit words packed as 3 bytes, MSB first, no padding — `codesiz` is a word count.)

**Round-trip verified in Hatari (`--dsp emu`, EmuTOS 1.4.0, Falcon 030)**: a 7-word test program
(`JCLR` poll on HRDF → `MOVEP` read of X:$FFEB into A1 → `JCLR` poll on HTDE → `MOVEP` write of A1
back to X:$FFEB → `JMP`) was loaded via `Dsp_ExecBoot`, and the 68030 sent two different 24-bit
values through the host port and read back exact matches:
```
sent 000042 -> got back 00 00 42
sent ABCDEF -> got back AB CD EF
```
This confirms the JCLR/MOVEP/JMP encodings, the host-port peripheral addresses (X:$FFE9 = HSR,
X:$FFE8 = HCR, X:$FFEB = HRX/HTX; HSR bit0 = HRDF, bit1 = HTDE), and the CPU-side register block
(`$FFFFA200`: ICR/CVR/ISR/IVR bytes then a 4-byte data union, high/mid/low used) are all correct —
not just internally consistent with each other, but correct against Hatari's real DSP56001
emulation. Test driver: `C:\claude\dsptest\drv.cpp`.

## The algorithm to port

`src/main/hwaudio/ym2151.cpp` is the MAME OPM core (Jarek Burczynski). Confirmed by reading it:

* 8 channels x 4 operators (M1/M2/C1/C2) = 32 operators, one of 8 algorithms wiring operator
  outputs to each other's phase-modulation input or to the channel output, feedback only on M1.
* Per-operator output is **one add + one table lookup**, not a multiply: `sin_tab[]` (1024
  entries) holds a log-domain value; `env` (envelope attenuation) is added to it as a table
  *index* into `tl_tab[]` (13*2*256 = 6656 entries) which converts back to linear. This is the
  classic log-domain FM trick and it is exactly what the 56001's single-cycle MAC/ALU + table
  addressing is good at — reinforces that this is a good DSP fit, not just "small state".
* Envelope generator is a 4-state machine (attack/decay/sustain/release) per operator, driven by
  precomputed rate tables (`eg_inc[]`); the C core has a "don't re-visit an operator until
  `eg_wake`" speed-up cache to save CPU branches. **Plan: drop that cache on the DSP** — the 56001
  has no branch-prediction penalty to dodge, so evaluating all 32 operators' EG unconditionally
  every sample is simpler code and, per the cycle budget below, easily affordable. This changes
  nothing about the output (same rate tables, same math), only the control-flow strategy, so it
  does not need the mockup-style approval that a lossy change would.
* Rough cycle budget at 32 MHz: ~20 cycles/operator/sample (envelope step + phase increment + one
  table lookup) x 32 operators = ~640 cycles/sample, leaving room for even the highest DMA sound
  rate the Falcon's CODEC supports. Sine/attenuation tables (1024 + 6656 entries) do not fit in
  the 56001's on-chip X/Y RAM and will live in external DSP memory, built once at init time from
  the CPU side and sent over via block transfer (same mechanism already verified above) rather
  than computed on the DSP.

## Audio out: DSP -> CODEC without going through the host port

`Devconnect(DSPXMIT, DAC, CLK25M, <prescaler>, <protocol>)` (XBIOS, confirmed present in
mint/falcon.h) routes the DSP's own serial port (SSI) output directly to the Falcon's CODEC/DAC —
this is the Falcon's audio crossbar, a fixed hardware routing matrix, not something built by hand.
Register writes (note on/off, frequency, algorithm, etc.) go *in* via the host port (low
bandwidth, matches this project's existing YM2151 `write_reg(r,v)` call pattern exactly); finished
samples go *out* via SSI/CODEC continuously, never touching the 68030 per-sample.

**Verified working in Hatari (`--dsp emu`, EmuTOS 1.4.0, Falcon 030, `--trace crossbar,dsp_host_ssi`)**:
a 78-word DSP program (`C:\claude\dsptest\dspasm.py`'s `assemble_tone_test()`) was booted via
`Dsp_ExecBoot`, configured the SSI (`CRA=$4100` 16-bit word length, `CRB=$1800` network mode +
transmit enable) and Port B/C using the *exact register values from a real 1996 Falcon DSP audio
demo* (`SOURCES/ATARI/SOUND/SOUND.ASM`, Fredrik Noring/NoCrew — downloaded from
mikro.naprvyraz.sk's DSP archive, cross-checked against Hatari's `dsp_core.h` peripheral offsets,
which matched exactly), then polled the SSI transmit-empty flag and wrote an alternating +/-
16-bit square wave to the transmit register in a tight, fully unrolled loop (only JCLR/MOVEP/JMP —
no new instruction types beyond the ones already verified in phase 1, except `MOVEP #imm,pp` which
was independently decode-checked the same way as everything else).

The first attempt produced exactly one transmitted sample and then silence — the DSP's `JCLR`
poll loop was stuck forever waiting for the transmit-empty flag to be set again. Root-caused by
reading Hatari's `crossbar.c`: `Devconnect()` sets the crossbar destination register correctly,
but its source register write **deliberately leaves bit 7 alone** (EmuTOS's own
`devconnect_falcon()` source comment: *"do not touch bit 7 (DSP)"*). Per `crossbar.c`'s register
bit-field documentation, bit 7 of `$ffff8930` is *"0 = Tristate and disconnect DSP / 1 = Connect
DSP to multiplexer (only for external SSI use)"* — precisely this situation — and nothing else
sets it. Fix: the 68030 program itself ORs `0x0080` into `$ffff8930` after calling `Devconnect()`.
With that fix, the crossbar trace showed continuous, correct transfers:
```
Dsp set TX register: 0x400000
Dsp SSI transmit value to crossbar: 0x004000
Crossbar : DSP --> Crossbar transfer	0x004000
... (repeats 8x, matching the square wave's half-period) ...
Dsp set TX register: 0xc00000
Dsp SSI transmit value to crossbar: 0x00c000
Crossbar : DSP --> Crossbar transfer	0x00c000
```
454,000+ transfers logged over the run, alternating in blocks of 8 exactly as programmed, with the
16-bit word correctly recovered from the left-justified 24-bit value written to the SSI TX
register (`0x400000 -> 0x004000`, `0xC00000 -> 0x00C000`) — confirms the CRA word-length setting
and the sample left-justification convention, not just that *something* reached the crossbar.
Driver: `C:\claude\dsptest\drv2.cpp`. Not yet confirmed to reach the actual emulated speaker
output (would need Hatari's WAV recording, gated behind an awkward AltGr+y hotkey not yet
automated) — but the entire chip-level path (DSP -> SSI -> crossbar -> "send to DAC" call) is
now proven, which is what actually mattered at this phase.

## Phase 3, partial: register-controlled branching on the DSP (jclr_reg)

Needed a way for the DSP to branch on a value the CPU sent it (e.g. "which tone to play"),
without yet tackling general-purpose register loads (see the AGU dead-end below). Added
`jclr_reg`/`jset_reg` to the encoder: same instruction family as the already-verified `jclr_pp`
(peripheral-memory bit test), but testing a bit of a *register* instead.

**A real bug, caught by the same mechanical-verification standard used throughout this port**:
the first version copied `jclr_pp`'s `(1<<7)` term across without re-deriving it for the new
dispatch table position. That single stray bit silently redirected the instruction to
`dsp_jmp_ea` (index `0x15C`, unconditional jump via a computed effective address) instead of
`dsp_jclr_reg` (index `0x158`) — an entirely different instruction that jumps to a
garbage/uninitialized address. Symptom: a minimal test program hung forever (crossbar trace
showed the TX register's power-on-zero value transferred forever, never once reaching either of
the two marker values the test was supposed to write) — plausible enough to look like a dozen
other possible bugs, and NOT something the existing decode-check script would have caught, since
that script re-derives dispatch from the same by-hand method as the encoder and so shares its
blind spots.

Fixed by writing `verify_dispatch.py`, which is deliberately independent of dspasm.py's own
derivations: it parses `opcodes8h[512]`/`opcodes_parmove[16]` directly out of `dsp_cpu.c`'s source
text (no manual counting) and re-implements the dispatch formula as a literal transcription of the
C code, then just checks "does this word reach the handler function I think it does?" for every
encoder function. It caught the bug immediately (`dsp_jmp_ea` instead of `dsp_jclr_reg`). This
script should be run against any new instruction added to `dspasm.py` from now on, before ever
loading it into Hatari — it's a five-second check that would have saved a debugging session.

With the fix, `verify_dispatch.py` passes for every instruction in the encoder, and a minimal
isolated test (`isolate_jclr_reg.py` + `drv4.cpp`) confirmed it by real execution too: A1 is `0` at
boot, `JCLR #0,A1,<jumped>` correctly took the jump, and the crossbar trace showed the "jumped"
block's marker value (`0x2222`) being transmitted, not the "fell through" one.

**Resolved in a later, unlogged test this session (`isolate_hostread.py`/`drv5.cpp`,
`trace5.log`)** — found by re-reading the raw trace files rather than trusting this file, which
was last edited before this test was run and never updated. The non-blocking host-port read is
NOT broken: `isolate_hostread` mirrors `assemble_freq_select_test`'s exact `loop_top` structure
(same JCLR HRDF check, same MOVEP read into A1, same JCLR_REG bit0 branch) but collapses the
outcome to one irreversible flip instead of an audible pattern, so there's no ambiguity about
timing windows. Trace5.log shows the crossbar output sitting at `MARK_WAITING` (0x1111) for
~16000 transfers, then flipping to `MARK_GOTIT` (0x5555) exactly once and staying there for
131000+ transfers after the driver sent one byte with bit0 set. The JCLR_REG/MOVEP-based
non-blocking read genuinely works.

So why did `assemble_freq_select_test` look broken? Structural difference, not a mechanism bug:
`isolate_hostread`'s loop returns to `loop_top` (the host-port check) on **every** iteration,
while `freq_select_test`'s loop only returns to `loop_top` once per full square-wave block
(`FAST_REPS`/`SLOW_REPS` x 2 SSI writes later) - i.e. the host-port poll rate is throttled by
how long the current audio block takes to play out, gated by the SSI hardware's real sample
timing. If the CPU's selector byte arrived and was still unread by the time the *next* poll
happened, that's plausibly just a coarser polling cadence than the (short, two-run) test window
allowed for, not a broken read. **Lesson for the real driver: poll the host port every sample (or
every few samples), not once per audio block** - which is what a responsive synth needs anyway,
so this isn't a workaround, it's the right design.

## AGU register immediate load (R0/N0/M0) - SOLVED

Needed for table-indexed sine lookup. Two dead ends confirmed first (both hit the same
underlying collision): `dsp_movec_ea`'s immediate form and `dsp_movec_imm` (`dsp_cpu.c`
~line 3079) both dispatch only when **bit 5 of cur_inst is forced to 1**, which is also the
MSB of the 6-bit `numreg` field they use - so both can only ever address M0-M7 (bit5 set),
never R0-R7/N0-N7 (bit5 clear). Re-derived mechanically (`find_movec_imm.py`, same method as
`verify_dispatch.py` - parse `opcodes8h[512]` out of the C source, don't hand-count) rather
than trust the by-hand bit math that caused the `jclr_reg`/`jmp_ea` mixup earlier this project.

**The real answer: `dsp_pm_5`** (`dsp_cpu.c` ~line 3989), a completely different opcode
family (dispatched through `opcodes_parmove[16]`, the `cur_inst >= 0x100000` space, not
`opcodes8h`). Found by scanning a real, working, period-correct DSP56001 program
(`dspsrc/SOURCES/ATARI/SOUND/SOUND.LOD`, the same Fredrik Noring/NoCrew Falcon audio demo
already used for the SSI/CODEC register values) - `scan_sound_lod.py` parses its P-memory
words and runs each through the same dispatch formula, and `dsp_pm_5` alone accounts for 36
of the ~238 real instructions in that program, far more than `movec_ea`(4)/`movec_imm`(1).
`verify_pm5_imm.py` decoded 5 real immediate-register-writes in that binary (`#imm,LA` /
`#imm,$0f`, using `ea_mode=0x34`) using `dsp_pm_5`'s own field-extraction code, confirming
the field layout against ground truth before trusting it for anything new.

Field layout: the top-level parallel-move dispatch index itself (`cur_inst[23:20]`, one of
16 slots) supplies `numreg`'s bits[4:3] - which of 3 index values (5/6/7 = binary 01/10/11;
"00" is structurally unreachable, matching the "bits[4:3] != 00" limitation noted in an
earlier pass) depends on the register range: idx 5 -> bits[4:3]=01 (A0/B0/A2/B2/A1/B1/A/B,
0x08-0x0F), idx 6 -> bits[4:3]=10 (**R0-R7**), idx 7 -> bits[4:3]=11 (**N0-N7**). M0-M7 need
bits[4:3]=00, unreachable here (not needed for this project - only R0/N0 are). `numreg`'s low
3 bits sit at `cur_inst[18:16]`. bit15=1 selects "Write D" (load the register). bit14=1 picks
the 6-bit `ea_mode` field over a short absolute address. `ea_mode=0x31` (`dsp_calc_ea`'s case
6, "aa", with any nonzero low-3-bits sub-field) is the immediate-value addressing mode - it
consumes one 24-bit extension word as the literal, exactly like `movep_imm_pp` already used
for the same trick in peripheral space. `cur_inst[7:0]=0` selects `opcodes_alu[0]`, which is
`dsp_move()` - read directly in `dsp_cpu.c`: a literally empty function body, a true no-op,
so only the register load happens.

Implemented as `move_imm_reg(numreg, value)` in `dspasm.py`, added to `verify_dispatch.py`'s
standard checks (both R0 and N0 forms pass), and confirmed by **real execution**, not just
dispatch math: `isolate_move_imm_reg.py`/`drv6.cpp` loads R0 with an immediate whose bit 0 is
set, then re-uses the already-proven `JCLR_REG` to test that exact bit on R0 directly - no
other new instruction needed, so this isolates `move_imm_reg` alone, same "one irreversible
marker" pattern as `isolate_hostread.py`. Crossbar trace: 465,612 transfers of `MARK_PASS`
(0x6666), **zero** `MARK_FAIL` (0x2222) - R0 held the right value from the very first check,
every single time. `dsptest6.tos`/`drv6.cpp`/`isolate_move_imm_reg.py` kept as the reference.

This was the last structural blocker for phase 3's stated remaining scope ("an actual sine
table" + "a real sample-rate-matched phase increment", see the Plan below) - table-indexed
reads/writes (`X:(R0)`, `move X:(R0),X0`, etc., needed to actually fetch table entries) are a
separate, not-yet-implemented encoding, but loading the *index* register itself, the part that
was blocked, now works.

## Indexed table read/write via R0 (`move X:(R0),X0` etc.) - SOLVED, plus a real pipeline hazard found

Needed to actually fetch table entries once R0 holds an index. Two more instructions, both
ground-truthed the same way as `move_imm_reg`:

**`dsp_pm_1`** (`dsp_cpu.c` ~line 3659, `opcodes_parmove` index 1) is `MOVE <X|Y>:ea,D1` /
`MOVE S1,<X|Y>:ea` - reads or writes X/Y memory through any of `dsp_calc_ea`'s real addressing
modes (`(Rx)`, `(Rx)+`, `(Rx)+Nx`, etc.), with D1 restricted to X0/X1/A/B (memspace=X) or
Y0/Y1/A/B (memspace=Y) - a fixed 2-bit selector, not the general `numreg` field, so no
dispatch collision to work around this time. `SOUND.LOD` alone had **zero** standalone
`dsp_pm_1` words - a real assembler folds a move immediately followed by a compatible ALU op
into one instruction, so the source's plain-looking `move X:(r0),y0` doesn't survive as a
separate word. Ground truth instead came from scanning **every `.LOD` file in the archive**
(`scan_all_lod.py`): `DGE.LOD`/`STEREO.LOD`/`FFTSSI.LOD` had 11 real examples between them,
e.g. `STEREO.LOD` P:0090 = `1CDDD8` = real, working `MOVE Y:(R5)+,Y0` - decoded via the exact
field extraction `dsp_pm_1()` itself uses and cross-checked field-by-field.

**`dsp_pm_3`**'s field layout (`dsp_cpu.c` ~line 3796) is `MOVE #imm8,D` for D in
X0/X1/Y0/Y1/A0/B0/A2/B2/A1/B1/A/B (0x04-0x0F) - needed to get a known test value into an
ALU-visible register at all (`move_imm_reg` only reaches R0-R7/N0-N7). Same dispatch-collision
shape as before: `dsp_pm_3`'s own direct slot (`opcodes_parmove` index 3) forces cur_inst
bit20=1, which is also `dstreg`'s bit4 - forcing `dstreg>=16` and excluding every one of
X0-B. The real path is index **2** (`dsp_pm_2`), whose own three special-case checks (nop /
R-update / `pm_2_2`) are skipped for the right bit pattern, falling through to a plain C
function call `dsp_pm_3()` - same field layout, no collision, since idx=2 leaves bit20=0.
Ground truth: `SOUND.LOD` P:00B4 = `244000` decodes to dstreg=0x04 (**X0**), srcvalue=0x40 -
which is exactly `move #0.5,x0` (SOUND.ASM line 203): `dsp_pm_3` left-shifts the 8-bit
immediate by 16 for these registers, and `0x40<<16 = 0x400000 = 0.5` in the DSP's 24-bit 1.23
fixed-point format. `move_imm_alu(REG_X0, 0x40)` in `dspasm.py` produces `244000` byte-for-byte.

Both added to `dspasm.py` (`move_ea_reg`, `move_imm_alu`) and `verify_dispatch.py`.

**A real, previously-undocumented correctness bug found while building the isolated test**:
the first version of `isolate_table_lookup.py` (load R0, load an ALU register, write via R0,
load R1, immediately read via R1) failed **100% of the time** - not flaky, not timing-window
dependent, every single sample was `MARK_FAIL`. Root cause, found by reading
`dsp_cpu.c`'s post-execute phase (~line 815) rather than guessing: the 56001 has a genuine
**1-instruction AGU pipeline hazard**. `dsp_calc_ea()` checks `agu_pipeline_reg[0]` and, if it
matches the register being used for addressing, uses the *pre-write* value
(`agu_pipeline_val[0]`) instead of the just-written one - modeling a real hardware pipeline
delay: writing an address register and using it as a pointer in the *very next* instruction
sees the old value, not the new one. `move_imm_reg`'s write sets `agu_move_indirect_instr=1`,
which queues the register into the pipeline for exactly one instruction. My R1 load was
immediately followed by its use as an address (zero instructions between), which hit this
squarely; R0 happened to have one unrelated instruction in between and was fine by luck, not
design. **Fix**: load both R0 and R1 up front, with at least one unrelated instruction after
each before it's used as an address (a natural fit here, since the ALU-register immediate
load needed anyway serves as that spacer for both). After reordering, the exact same test
passed cleanly: 56,252 transfers of `MARK_PASS` (0x7777), zero `MARK_FAIL` (0x3333).
**This applies to every future instruction that writes R0-R7/N0-N7/M0-M7 and then uses that
register for addressing** - always leave at least one other instruction in between.
`dsptest7.tos`/`drv7.cpp`/`isolate_table_lookup.py` kept as the reference.

With this, the full chain needed for a table-driven oscillator (load an index register, read
a table entry through it into an ALU-visible register, and - by symmetry, already covered by
the same `move_ea_reg` with `EA_CASE_INC`/`EA_CASE_RN_PLUS` - auto-increment that index each
sample) is proven.

## Wavetable oscillator - first one running end to end, real execution confirmed

Combined everything above into an actual circular-buffer oscillator (`assemble_wavetable_osc.py`
/ `drv8.cpp` / `dsptest8.tos`), the concrete target this pass of work was aiming at:

* **`move_imm_m(mreg, value)`** added to `dspasm.py` - `MOVE #value,Mn` via `dsp_movec_imm`,
  the *same* instruction family that was a dead end for R0/N0 (see `move_imm_reg`'s docstring)
  is the *right* tool for M0-M7: the dispatch condition forces cur_inst bit5=1, and every
  M-register number (0x20-0x27) already has bit5=1, so there's no collision here. Sets up
  hardware circular addressing (`dsp_update_rn_modulo`, `dsp_cpu.c` ~line 1701): `Mn = (buffer
  size) - 1`, e.g. 7 for an 8-entry table.
* R0 = table base, M0 = 7 (8-entry circular), each given a spacer instruction first (the AGU
  pipeline hazard from `isolate_table_lookup.py` above).
* Table filled with 8 known markers (`move_imm_alu` + `move_ea_reg` write, `EA_CASE_INC`) -
  8 back-to-back write instructions, no spacers needed between them: confirmed by reading
  `dsp_update_rn` that auto-increment writes `dsp_core.registers[...]` directly and does
  **not** go through `dsp_write_reg`/set the pipeline flag, unlike explicit register loads -
  only explicit loads (`move_imm_reg`, `move_imm_m`, `pm_5`, `movec_reg`) need a spacer before
  their *own* value is used for addressing; auto-increment side effects don't re-trigger it.
  After 8 writes R0 has wrapped exactly back to the table base, ready for playback.
* Playback loop: wait for SSI transmit-empty, read `X:(R0)+` into X1 (circular), `MOVEP
  X1,SSI_TX` to transmit, jump back - forever.

**Crossbar trace, real execution**: `0x110000, 0x220000, 0x330000, 0x440000, 0x550000,
0x660000, 0x770000, 0x880000`, repeating in exactly that order indefinitely - no skipped
entries, no repeats, no wandering into other memory. The circular wraparound, auto-increment
addressing, and immediate loads all work correctly together, not just individually.

**Deliberately not included in this first version** (kept out to verify the core circular
playback mechanism in isolation first): receiving the table from the host, and live frequency
control via `N0`. Both done as a second pass, below.

## Both follow-ups done: host-supplied table + live frequency control - real execution confirmed

`assemble_wavetable_osc2.py` / `drv9.cpp` / `dsptest9.tos` extends the first oscillator with
both pieces that were deliberately deferred:

**`move_reg_reg(srcreg, dstreg)`** added to `dspasm.py` - plain register-to-register `MOVE`
via `dsp_pm_2_2` (a sub-case of `dsp_pm_2`, reached only when cur_inst matches neither of
`dsp_pm_2`'s own "nop"/"R-update" special cases first). Its 5-bit src/dst fields sit at
cur_inst[17:13]/[12:8] - completely outside any dispatch bit, so unlike `movec_imm`/
`movec_ea`/`dsp_pm_3`'s own slot, there's no bit5 collision here; any register 0-31 works on
either side. Ground truth (`verify_pm22.py`, scanned the whole archive since `SOUND.LOD` alone
had none - same situation as `dsp_pm_1`): `SOUND.LOD` P:00ED = `210400` is real, working
`MOVE A0,X0`; `SNDTMP.LOD` P:0081 = `219000` is `MOVE A1,R0` - between them, both this
project's actual source register (A1, to move a just-received host-port byte) and destination
range (X0/N0, ALU-visible or AGU registers) are independently confirmed against real code.

The oscillator boot sequence now receives its table over the host port instead of using
baked-in constants: 8x (blocking wait for a host word, `MOVEP` into A1, `move_reg_reg(A1,X0)`,
write `X:(R0)+N0`). The playback loop now non-blockingly checks the host port every sample
(the proven pattern from `isolate_hostread`) and, if a byte arrived, `move_reg_reg`s it into
N0 before the next table read - changing the per-sample step, i.e. the pitch, live, with
`EA_CASE_RN_PLUS` addressing (`X:(R0)+N0`) actually using N0 for the step instead of the fixed
+1 the first oscillator used.

**Real execution, `--trace crossbar,dsp_host_ssi`, driven by `drv9.cpp`** (sends the 8 table
markers, waits, then sends one new N0 value of 2): the trace's early transfers show
`0x110000, 0x220000, 0x330000, 0x440000, 0x550000, 0x660000, 0x770000, 0x880000` repeating -
proving the table really was received over the wire and stored correctly (not hardcoded, and
matching the exact 8 values `drv9.cpp` sent). After the frequency-change byte, later transfers
show `0x110000, 0x330000, 0x550000, 0x770000` repeating - every other entry, exactly the
expected result of a step of 2 through the same 8-entry circular table, confirming live `N0`
control works mid-playback without corrupting or restarting the buffer.

This is now a genuinely complete, host-controllable, table-driven oscillator core: everything
phase 3 needed (register loads, indexed/circular addressing, host-port I/O, live parameter
changes) is proven working together, by real DSP execution, not just dispatch math. What
remains before this becomes a real synth voice is phases 4-8's own scope (envelope generator,
all 32 operators/8 algorithms, LFO/noise, wiring into `atari/audio.cpp`) - a substantially
larger undertaking than this pass, not a new class of unknown.

## Plan (each phase measured/verified before moving to the next, per this project's standing rule)

1. ~~Toolchain proof of concept: hand-written encoder, verified round-trip~~ **done, this session.**
2. ~~Audio-out proof of concept: a square wave generated on the DSP, routed to the CODEC via
   `Devconnect`~~ **done, this session** — see above; found and fixed a real bug (crossbar source
   register tristate bit) along the way.
3. ~~Single-operator sine test driven by real register writes from the 68030~~ **done, this
   session** — see "Wavetable oscillator" and "Both follow-ups done" above: a circular table
   (M0 modulo addressing) received live from the 68030 over the host port, played back through
   R0 with auto-increment, transmitted continuously to the CODEC via SSI, with the per-sample
   step (N0) changeable live mid-playback - all proven together by real crossbar traces, not
   just individually. Uses fixed markers rather than a real sine table (that's what phase 4's
   sine/envelope math is for), but the mechanism - the actual point of phase 3 - is done.
4. Full envelope generator for one operator, checked against `ym2151.cpp`'s `advance_eg()` sample
   by sample for a fixed register script (same "-DxxxCHECK against the C version" methodology used
   for the assembler routines elsewhere in this port). **Started this session**: the ALU-op field
   present in every `pm_1`/`pm_2`/`pm_2_2`/`pm_3`/`pm_5` instruction (`cur_inst[7:0]`, indexing
   `opcodes_alu[256]`, `dsp_cpu.c` ~line 595) is a flat 0-255 lookup with none of the dispatch
   bit-collisions the register/addressing fields had - real arithmetic (ADD/SUB/MPY/shifts/etc,
   not just data movement) is available by giving any existing instruction a nonzero `alu_op`
   instead of 0. Confirmed by real execution: `isolate_alu_not.py`/`drv10.cpp` loads A=0, applies
   `dsp_not_a` (`ALU_NOT_A=0x17`) via a harmless `X1,X1` self-move that carries the real op as a
   side effect (per `dsp_pm_2_2`'s own code: it captures the S,D move's source value *before*
   running the ALU op, so a same-register "self move" is a clean no-op vehicle for the ALU op
   alone), then reads A back out via `move_reg_reg` and checks bit16 with the already-proven
   `JCLR_REG`. 54,933 transfers of `MARK_PASS`, zero `MARK_FAIL`. This is genuinely the first
   arithmetic (not data-movement) instruction verified in this whole DSP port, and the pattern
   generalizes immediately: the exact same "harmless self-move + real `alu_op`" technique reaches
   any of the ~200 real operations in that table (`ALU_NOT_A`/`ALU_NOT_B`/`ALU_ASR_A`/
   `ALU_ASR_B`/`ALU_ADD_X_A`/`ALU_ADD_X_B` already named as constants in `dspasm.py`; MPY/MAC
   variants and the rest are at other indices in the same flat table, not yet named/tested).
   **MPY verified too, and two real hand-counting errors caught by execution, not review** -
   `isolate_alu_mpy.py`/`drv11.cpp`: X0=Y0=0.5 (0x400000, exact 1.23 fixed point), multiply via
   `dsp_mpy_p_y0_x0_a` (`A = Y0*X0`), read A1 back, expect bit21 set (0.5*0.5=0.25=0x200000
   exactly - no rounding ambiguity, chosen on purpose). First attempt used a hand-counted ALU
   index of 0xC8 and failed 100% of the time (`MARK_FAIL` from the very first sample); checking
   `opcodes_alu[0xC8]` with a script (never hand-count into these tables - see below) showed it
   is actually `dsp_mpy_p_x0_y1_b`, a different instruction; the real index for
   `dsp_mpy_p_y0_x0_a` is **0xD0**. With that fix, `isolate_alu_mpy.py` passed cleanly. Second
   miscount this session in the exact same table (`ALU_ADD_X_A`/`ALU_ADD_X_B` were first written
   as 0x10/0x18, the real indices are 0x20/0x28 - caught by a fresh row-by-row script recount
   before they were ever used in a real test, so no failed run needed that time). **Standing
   rule from here on: every `ALU_*` constant in `dspasm.py` is looked up by parsing
   `opcodes_alu[256]` and searching for the exact function name (`find_mpy_index.py`'s
   one-off pattern, worth keeping as a template) - counting rows/columns by eye in a 256-entry
   table is exactly the kind of mechanical task this project already learned (from the earlier
   `jclr_reg`/`jmp_ea` mixup) not to trust without a script.**

   **Also found while chasing what looked like a DSP bug and wasn't one**: `dsptest10.tos`/
   `dsptest11.tos` (this session's `.tos` output names) are 9-character basenames - one over
   the 8.3 limit this whole project has had to respect since the Atari port began. GEMDOS
   silently truncates/collides them, so `--auto dt11.tos`-style launches of double-digit-numbered
   test binaries were actually still running the *previous* test's content, with no error of any
   kind - just a wrong-looking result. Symptom looked exactly like a real DSP correctness bug
   (consistent, reproducible wrong marker) until checked against the binary's own build
   timestamp/checksum. **Fix**: renamed to `dt11.tos` (short); numbered DSP test drivers from
   here on should stay at or under 8 characters (`dtNN.tos`, not `dsptestNN.tos`) once they hit
   double digits.

   **The real attack-phase formula now runs correctly on the DSP, verified against a known
   answer** (`isolate_envelope_att.py`/`drv12.cpp`/`dt12.tos`): `volume=100, eg_inc=1 ->
   new_volume=93`, exactly matching `advance_eg()`'s `EG_ATT` case
   (`op->volume += (~op->volume * eg_inc[i]) >> 4`). The expected answer was computed
   independently first, in Python, by transliterating `dsp_mul56`/`dsp_asr56` from `dsp_cpu.c`
   line-for-line (`mul56_ref.py`) rather than reasoning about fixed-point bit positions by
   hand - given how many subtle position/scaling issues this step surfaced (below), that
   discipline is what made the bugs findable at all instead of just "looks wrong somehow".

   Three real, non-obvious things had to be worked out, in order:

   1. **`op->volume`/`eg_inc[]` are plain integers** (`int32_t`/`uint8_t` in `ym2151.hpp`), not
      the 1.23 fixed-point fractions `move_imm_alu`'s left-shift-by-16 convention assumes (that
      convention is for audio-sample-style registers like X0/Y0, not attenuation indices). Load
      them unscaled instead: `move_imm_reg` into a scratch N-register (N1; no scaling applied
      there) then `move_reg_reg` to copy into X0/Y0/A/B/etc (also no scaling - only `pm_3`'s own
      dedicated immediate form shifts).
   2. **MPY doubles its true product** (`dsp_asl56`, "get rid of extra sign bit" - a standard
      DSP56k convention already known from the earlier fixed-point MPY test) - so the C code's
      `>>4` becomes 5x `ALU_ASR_A` on the DSP (one extra shift to undo the doubling), not 4. Also,
      for *plain small integers* (as opposed to near-unity fixed-point fractions), the product's
      significant bits land in **A0**, not A1 where the earlier 0.5*0.5 test found them - this
      followed directly from running the real `dsp_mul56` algorithm against these specific
      operands in `mul56_ref.py`, not from reasoning about "high bits vs low bits" in the
      abstract (which is exactly the kind of reasoning that produced the ALU-index miscounts
      earlier this session).
   3. **Register-write/read positional mismatches - two of them, both real, both caught by a
      failed test run, not by review:**
      - `dsp_write_reg`'s special case for `DSP_REG_B` (used whenever `move_reg_reg` or any
        other instruction writes to the *named* "B" register, 0x0F) always puts the value in
        **B1**, zeroing B0 - never B0 directly (confirmed by reading `dsp_cpu.c`'s
        `dsp_write_reg` switch statement). The increment from the MPY/ASR chain naturally sits
        in **A0** (the low slot, per point 2). Adding these via any accumulator-format ADD
        (`dsp_add_b_a`, which genuinely does operate on the full A0/A1/A2 += B0/B1/B2 triple -
        verified by reading its code too, ruling that out as the bug) silently mixed up slots
        and produced garbage. **Fix**: write the volume directly to **B0** (register 0x09, its
        own addressable register number - bypasses the special "write B" repositioning
        entirely, confirmed via `dsp_write_reg`'s generic `default:` case), leaving B1/B2 at
        their natural post-boot value of 0.
      - **`dsp_pm_read_accu24`** (what "read A" or "read B" as a plain register - i.e.
        `move_reg_reg(REG_A, dst)` - actually calls) **silently saturates/clamps its result**
        when the accumulator's A2 sign-extension byte doesn't match A1's own sign bit. `NOT`
        (`dsp_not_a`) only ever touches **A1**, confirmed by reading its code - it never updates
        A2. So `A=100` (A1=100, A2=0, consistent) followed by `NOT` (A1 becomes ~100, negative -
        but A2 is still 0, now *inconsistent*) leaves the accumulator in exactly the
        mismatched state `dsp_pm_read_accu24` treats as overflow, and it clamps to the nearest
        representable value (`0x7FFFFF` here) instead of returning the true bit pattern. Traced
        by reading `dsp_pm_read_accu24`'s full body line by line after the formula kept coming
        out wrong with every other step already individually verified correct. **Fix**: read
        **A1** directly (register 0x0C) instead of the named "A" (0x0E) whenever the true raw
        value is needed right after an operation (like NOT) that only touches A1 - this
        bypasses `dsp_pm_read_accu24` and its saturation entirely, the same "address a
        sub-register directly" trick as the B0 fix above.

      **This retroactively puts a question mark over `isolate_alu_not.py`'s earlier "PASS"**:
      that test also read the result via `move_reg_reg(REG_A, REG_X0)` after a NOT, which (per
      the above) would have been silently saturated to `0x7FFFFF` instead of the true
      `0xFFFFFF` - and by coincidence, *both* values have bit16 set, the one bit that test
      checked, so it passed either way without actually distinguishing correct from saturated.
      The instruction itself (`ALU_NOT_A=0x17`) is still correct - only that one test's read-back
      step, and thus its specific pass/fail signal, is unreliable. Not yet re-run with the fix;
      worth doing before leaning on it again, though `isolate_envelope_att.py`'s success (which
      exercises NOT plus everything downstream, now with the A1-direct read) is itself strong
      evidence the underlying NOT operation was always fine.

      **General lesson for everything still to come**: any register read immediately following
      an operation that only touches *part* of an accumulator (A1 alone, not A0/A2 too) needs
      the same "read the sub-register directly, not through the named A/B accumulator" treatment
      - `dsp_pm_read_accu24`'s saturation is a real hardware behavior (it's what real 56k
      overflow limiting does), not a Hatari emulation quirk, so this will keep coming up as more
      of the envelope/operator math gets built, not just here.

   **Decay/sustain/release's plain-addition formula also verified, first try, no new bugs**
   (`isolate_envelope_dec.py`/`drv13.cpp`/`dt13.tos`): `volume=930, eg_inc=8 -> new_volume=938`
   (checked against 5 bits of 938's real binary pattern). Simpler than attack (no NOT/MPY, so
   none of that step's positional traps apply) - both operands written via the *normal* "write
   A"/"write B" path (`move_reg_reg` into the named `REG_A`/`REG_B`, not the B0-direct trick the
   attack step needed), since a fresh write via `dsp_write_reg`'s A/B case correctly sets
   A2/B2's sign-extension to match A1/B1 (confirmed by reading it: `A2 = value&(1<<23)?0xff:0`)
   - no mismatch the way a partial op like NOT leaves one. `dsp_add_b_a`'s `dsp_add56` call also
   correctly propagates carries into A2, so no saturation risk reading the sum back either;
   read via A1 directly anyway, to stay consistent with the established discipline.

   **State-transition comparison also verified, first try** (`isolate_envelope_cmp.py`/
   `drv14.cpp`/`dt14.tos`): `volume=938, threshold=900 -> volume>=threshold` (needed for e.g.
   `if (op->volume >= (int32_t)op->d1l) op->state = EG_SUS;`). Computed via `dsp_sub_b_a`
   (`A -= B`, both operands written the clean "normal A/B write" way like the decay test) then
   tested bit23 (the sign bit) of the raw difference directly - clear means non-negative, i.e.
   `volume >= threshold`. 38 = 938-900, positive, bit23 correctly clear; also checked 2 of the
   diff's own low bits (1 and 5, both set in 38's real binary pattern) for confidence beyond
   just the sign.

   Three envelope-generator building blocks now verified in a row with no new bugs (attack,
   decay/sustain/release's addition, and the comparison) - the register-slot lessons from the
   attack step's two bugs generalized cleanly to both of these.

   **`eg_inc[]` as a real table in DSP memory, indexed at a computed (non-zero) offset, also
   verified** (`isolate_envelope_table.py`/`drv15.cpp`/`dt15.tos`): the attack formula unchanged,
   but `eg_inc` is now `move_ea_reg`-fetched from X-memory at `TABLE_BASE+index` instead of a
   hardcoded immediate. Sent real `eg_inc[]` data (row 12, `ym2151.cpp` ~line 174, "rate 14 0" -
   `4,4,4,4,4,4,4,4`) from the host, fetched at index 5 (not 0, to prove the index arithmetic
   isn't accidentally reading the first element), got `eg_inc=4`, and the full formula gave
   `new_volume=74` - matching `~100*4>>4` computed independently beforehand, same as every
   other step.

   **Found a real hardware constraint doing this, not a bug**: `Dsp_ExecBoot`'s hardware
   bootstrap mode is capped at **512 words** (the DSP56001 boot ROM's fixed behavior - see the
   Toolchain section above). The first version of this test tried to receive and store the
   *entire* real `eg_inc[]` table (19*8=152 entries) via an unrolled loop (4 instructions x 152
   = 608 words just for that part) and came out to 814 words total, over the limit. Caught by
   counting the assembler's own word output before ever loading it, not by a failed boot.
   **Fix for this test**: send only the one real table row actually needed (8 words) instead of
   the full table - proves the lookup mechanism with real data, just not the complete table.
   **Real implication for the eventual full system**: loading a genuinely large table (the real
   152-entry `eg_inc[]`, or a 1024-entry sine table, or `tl_tab[]`'s 6656 entries) at boot time
   needs an actual DSP-side loop (`DO`/`REP`-style repeat, or a manual counter+branch) rather
   than unrolled receive code, or loading it in chunks across several `Dsp_ExecBoot` calls, or
   finding a way to seed X/Y memory directly in the boot image instead of receiving it at
   runtime at all. Not yet designed - a real open question for wiring the full envelope/operator
   system together, not just this test.

   Four envelope-generator building blocks now verified (attack, decay/sustain/release's
   addition, the comparison, and table-driven lookup) - the register-slot lessons from the
   attack step's two bugs generalized cleanly to all three that followed, and the 512-word
   limit is now a known constraint to design around rather than a surprise waiting to happen
   mid-integration.

   **State dispatch - branching to the right formula based on a state flag, the last piece
   needed for a real `advance_eg()`-style state machine - also verified**
   (`isolate_envelope_dispatch.py`/`drv16.cpp`/`dt16.tos`): with a state flag set to "attack",
   `JCLR_REG` branches around the decay code entirely and runs the attack formula, landing on
   93 (the attack answer) - not 101 (what the decay path would give for the same inputs, a
   deliberately different and easily distinguishable wrong answer if the branch fell through
   incorrectly). Proves the branch genuinely *skips* the other path's code, not just that both
   paths happen to agree.

   **Five building blocks now verified, covering every mechanism `advance_eg()`'s real
   `switch(state)` structure needs**: the two arithmetic formulas (attack's NOT+MPY+ASR,
   decay/sustain/release's plain addition), the threshold comparison for state transitions, the
   table lookup with a real (non-hardcoded) index, and the branch that picks between them. What
   remains is integration, not new unknowns: combining these into one continuously-running
   per-operator loop with real timing.

   **Multi-tick progression with the real `eg_cnt` timing gate - a genuine, if small, running
   envelope - also verified** (`isolate_envelope_multitick.py`/`drv17.cpp`/`dt17.tos`): 4 ticks
   (`eg_cnt`=0,1,2,3), `mask=1` (`eg_sh_ar`=1, so only even `eg_cnt` triggers an update, odd
   ticks are skipped via `!(eg_cnt & mask)` - `advance_eg()`'s own gating condition, not a
   simplification), volume held in one persistent register (Y1) across ticks rather than
   reloaded each time. Tick 0 (even) updates 100->93; tick 1 (odd) is skipped, volume stays 93;
   tick 2 (even) updates 93->87 (the *same* attack formula applied to a *different* input,
   proving it's not hardcoded to 100); tick 3 (odd) skipped. Final volume 87, computed
   independently first the same way as every step before it, confirmed bit-for-bit by real
   execution (4 of 87's bits checked). 123 words - comfortably under the 512-word boot limit
   even fully unrolled at this scale.

   **This is the point where the individual pieces stop being separate primitives and become an
   actual running envelope**: real timing gate, real persistence across ticks, real formula
   applied to whatever the current state is rather than fixed test constants. Six building
   blocks verified overall now (the five above plus this integration).

   **The integration gap identified right after this test - state that actually changes
   mid-run - closed immediately after, same session**
   (`isolate_envelope_lifecycle.py`/`drv18.cpp`/`dt18.tos`): a real mini-lifecycle, starting in
   ATTACK with volume=10, `eg_inc_attack=8`. Every tick: dispatch to the current state's formula
   (attack or decay), then - only while still in ATTACK - check `volume<=0` (via `MIN_ATT_INDEX`
   comparison, tested as "are bits 0-6 of volume all clear", volume never goes negative here by
   construction) and if true, clamp volume to 0 and flip the state register to DECAY, exactly
   matching `advance_eg()`'s own `if (op->volume <= MIN_ATT_INDEX) { op->volume = MIN_ATT_INDEX;
   op->state = EG_DEC; }`. Independently computed trace: tick0 10->4, tick1 4->1, tick2 1->0
   (hits zero, state flips to DEC), tick3 - now dispatching to DECAY - 0+2=2 (`eg_inc_decay=2`).
   Final state=DEC, volume=2, confirmed exactly by real DSP execution (checked both the state
   flag and 3 bits of the volume). 222 words.

   This proves the transition isn't just a passive comparison (already shown separately) but
   that flipping the state register mid-run genuinely redirects *later* ticks to different code
   - tick 3 took the decay path only because tick 2's transition really took effect, not because
   the test was pre-wired to expect it.

   **Seven building blocks now verified overall**, covering the complete mechanism set a real,
   continuously-running, state-changing envelope generator needs for one operator.

   **The real (non-unrolled) loop construct - immediately built and verified, same session**
   (`isolate_real_loop.py`/`drv19.cpp`/`dt19.tos`): a manual counter loop, not `DO`/`ENDDO`
   (which needs a hardware loop stack and `dsp_postexecute_update_pc` interaction not yet
   traced - more new unverified mechanics than reusing already-proven ones). Counter kept in
   Y1 (not A1, which the per-iteration host-receive step already needs - an early draft
   collided the two, caught before assembling by re-reading the register usage, not by a
   failed run), decremented via the already-proven `ALU_ADD_B_A` with B1=-1, zero-tested via
   the same "check low bits all clear" pattern the lifecycle test used for `volume<=0`.

   Loop body (receive one host word, store it via `X:(R0)+`) written **once**, executed 8
   times via a backward `JMP`, receiving 8 markers (`0x11`..`0x88`) from the host. Real
   execution: crossbar trace shows the stored sequence played back in exact order,
   `0x110000, 0x220000, ..., 0x880000` - proving the loop ran exactly 8 times (not 7, not 9,
   not forever) and each pass stored to the correctly-advancing address. 48 words total for
   the *entire* program including the loop - compare to the unrolled table-fill tests, where
   receiving N words cost 4N words of code; this one is O(1) in N, which is the whole point
   and what makes the real ~152-2000+-entry tables and thousands-of-ticks note durations
   actually loadable within the 512-word boot budget.

   **Eight building blocks verified. Then, immediately after, the complete lifecycle - all
   three real transitions (ATT->DEC->SUS->OFF), running via the real loop construct - verified
   too, same session** (`isolate_envelope_full_cycle.py`/`drv20.cpp`/`dt20.tos`): uses the
   *real* `EG_*` state values from `ym2151.hpp` (`EG_ATT=4, EG_DEC=3, EG_SUS=2, EG_OFF=0`,
   dispatched via a 3-level bit-test decision tree: bit2 set->ATT, else bit1 set->(bit0:DEC or
   SUS), else->OFF/no-op - matching `advance_eg()`'s own `default:` case exactly). 8 ticks,
   independently computed first: volume 10->4->1->0 (ATT, 3 ticks, hits 0, transitions to DEC)
   ->2->4 (DEC, 2 ticks, hits D1L=4, transitions to SUS) ->7->clamped 8 (SUS, 2 ticks, hits
   MAX_ATT_INDEX=8, transitions to OFF) -> stays 8 (OFF, 1 tick, no-op). Final state=OFF,
   volume=8 (`MAX_ATT_INDEX`, correctly clamped), confirmed exactly by real execution.

   First version of this test unrolled all 8 ticks by hand and came out to **742 words** -
   comfortably over the 512-word boot limit, caught by the assembler's own word count before
   ever loading it (the same check that caught the `eg_inc[]` full-table overrun earlier).
   Rewritten around the just-proven real loop construct instead (tick logic written once,
   looped 8 times via a decrementing counter in its own register, N4) - **156 words**, well
   within budget, and a concrete demonstration that the loop construct actually solves the
   problem it was built for, not just in its own isolated test.

   **A second real bug found rewriting it, caught by re-reading the code before assembling,
   not by a failed run**: the loop's decrement counter reused B1 for its "-1" constant, but
   the DEC/SUS transition checks earlier in the same tick body *also* use B1 (for `D1L` and
   `MAX_ATT_INDEX`) - by the time the tick body reached the counter decrement, B1 no longer
   held -1. Fixed by reloading B1=-1 fresh right before the decrement every iteration, instead
   of once before the loop.

   **This is a complete, working, single-operator envelope generator.** Every mechanism
   `advance_eg()` needs - both arithmetic formulas, threshold comparisons, table lookup with a
   real index, state dispatch, timing-gated multi-tick progression, state that actually changes
   mid-run, and a real loop construct - is proven, and this test demonstrates all of them
   working together for a full attack-to-silence lifecycle, not just in isolation.

   **Pitch and amplitude - the two previously-separate halves of this whole DSP effort -
   combined for the first time, same session** (`isolate_pitch_amp_combo.py`/`drv21.cpp`/
   `dt21.tos`): the circular wavetable oscillator (pitch/waveform, from the earlier
   `assemble_wavetable_osc*.py` work) now has its output amplitude scaled by a shift amount
   standing in for an envelope-derived attenuation (`volume>>7`, a crude but genuine
   approximation - not the real `tl_tab[]` log-domain scheme, which needs its own 6656-entry
   table load, future work). 3 phases through the same 8-entry table, shifts 0/2/4 (standing
   in for envelope volumes 0/256/512): real DSP execution reproduced every expected value
   **exactly**, including the trickiest case - `0x880000`'s top bit makes it "look negative"
   in 24-bit two's complement, and its shifted values (`0xE20000` at >>2, `0xF88000` at >>4)
   only come out right with correct *arithmetic* (sign-preserving) shifting, not a naive
   logical shift. Caught one design bug before assembling (not by a failed run): `ALU_ASR_A`
   shifts the accumulator A, not the register named in the parallel-move part of the
   instruction - the harmless-self-move trick that worked for `NOT`/`MPY` doesn't apply here
   unchanged, since those needed A only as a scratch computation surface, while this needed
   the shift to actually affect the *sample value* itself. Fixed by explicitly moving the
   sample into A first, shifting there, then reading A1 back out.

   **Ten building blocks verified overall**, now spanning both halves of a working FM voice:
   phase-accumulated waveform generation (proven earlier this session) and envelope-driven
   amplitude control (proven this pass), demonstrated working together, not just side by side.

   **The core missing mechanism for `eg_sh_*`/`eg_sel_*` - a runtime-variable-count shift -
   also verified, same session** (`isolate_variable_shift.py`/`drv23.cpp`/`dt23.tos`): the real
   formula is `v = kc >> op->ks` (`ym2151.cpp`'s `refresh_EG`), and `op->ks` is set from
   `5-(v>>6)` on an 8-bit register write - checked by reading the actual assignment rather than
   assumed, since a first guess of `ks` ranging 0-3 was wrong; it's really **{2,3,4,5}**.
   DSP56k has no "shift by register N" instruction (`ASR` only shifts 1 bit per instruction),
   so this needed something genuinely new: a 4-way branch on `ks` (tested as `ks-2`, a 2-bit
   selector), each arm applying a different *fixed* count of `ALU_ASR_A` (2, 3, 4, or 5) -
   built entirely from already-proven primitives (`JCLR`/`JSET` dispatch, `ASR_A` chains)
   rather than a new "counted shift" instruction. Test: `kc=40`, `ks=3` loaded into a register
   at *runtime* (not baked into which code path gets assembled), correctly computed `v=5`
   (`40>>3`) - confirming the branch genuinely reads `ks` and picks the matching arm, not that
   the right answer happened to be hardcoded.

   **Twelve building blocks verified overall.** With this, deriving the real `eg_sh_ar`/
   `eg_sel_ar` (etc.) from an operator's actual rate register and key-code - the one piece of
   `advance_eg()`'s machinery not yet touched - is down to wiring (fetch `ar`, compute `v` via
   this shift, look up `eg_rate_shift[ar+v]`/`eg_rate_select[ar+v]`, both already-proven table
   reads), not a new unknown.

   **Immediately closed, same session**: the one remaining unverified piece - reading a table
   at a *dynamically computed* address (`TABLE_BASE + runtime_index`, as opposed to every
   earlier table read in this whole session, which used a compile-time-known address) -
   confirmed by `isolate_dynamic_addr.py`/`drv24.cpp`/`dt24.tos`. Full chain, all at runtime:
   `ar=67, kc=40, ks=3` -> `v=kc>>ks=5` (the proven variable-shift mechanism) -> `index=ar+v=72`
   -> `R0 = TABLE_BASE + index`, computed and loaded into R0 live, then read - fetching
   `eg_rate_shift[72]` and `eg_rate_select[72]` from real table data (extracted from
   `ym2151.cpp` by script - `extract_rate_tables.py` - not hand-transcribed, the same
   discipline used for the DSP opcode tables). First run failed (`MARK_FAIL`): the sent table
   slice started at real index 68, but the read address used `TABLE_BASE + 72` directly
   instead of accounting for the slice's own offset - landed 68 words past where the data
   actually was. Fixed by giving the fill address and the read-computation's base two
   separate constants (the fill uses the real DSP address; the read uses a *virtual* base
   offset by the slice's starting index, so `virtual_base + real_index` lands correctly).
   With that fix: both fetched values matched exactly.

   **This closes the loop completely**: an operator's real rate/key-code/key-scale registers
   now genuinely drive which envelope-rate table row gets used, computed and fetched entirely
   at runtime, not hardcoded per test. **Thirteen building blocks verified.** Every mechanism
   `advance_eg()` needs, for every register value combination, not just the ones baked into a
   test at assembly time, is now proven.

   **Done immediately after, same session**: exactly this wiring, in one program
   (`isolate_full_integration.py`/`drv25.cpp`/`dt25.tos`). Same `ar=67, kc=40, ks=3` as
   `isolate_dynamic_addr.py` (chosen because that test already proved it derives
   `eg_sh_ar=1`), but this time the derived value is **read and checked** (not just assumed)
   before being used to gate a real 4-tick envelope run identical in structure to
   `isolate_envelope_multitick.py` - the two previously-separate integration tests, chained.
   Passed first try: derivation correct, gating correct, final volume exactly 87. This is the
   first program in the whole session where "read an operator's actual rate/key-code/key-scale
   registers" and "run the envelope" are the same running program, not two things proven
   separately and taken on faith to compose.

   **Fourteen building blocks verified.** The full path from real operator register values to
   a correctly-gated, multi-tick envelope is proven end to end, not just piece by piece.

   **The scaling question, tackled next, same session** (`isolate_multi_operator.py`/
   `drv26.cpp`/`dt26.tos`): every test up to this point kept its "operator" state (volume,
   etc.) resident in a DSP register for the whole run. That does not scale to 32 real
   operators - the DSP56001 has only 8 R/N/M registers each and a handful of ALU-visible
   registers (X0/X1/Y0/Y1/A/B), nowhere near enough to hold 32 operators' state at once. A
   real implementation has to mirror the C code's `oper[32]` array: one small record per
   operator in X/Y memory, loaded into registers, processed, and stored back - once per
   operator, once per tick. This is the first test where operator state lives in **memory**
   between operations rather than staying parked in a register throughout.

   Minimal proof: two operators, each with an independent volume at its own fixed memory
   slot (`X:$0700`, `X:$0701`), each ticked once with the plain-addition decay formula but a
   *different* `eg_inc` (op0: 100+5=105, op1: 200+9=209) - checking that op0's result reflects
   only op0's inputs, with zero leakage from op1's slot or vice versa. First run failed
   (`MARK_FAIL`): the memory-init step used `move_ea_reg(..., d1_sel=0)` to store the loaded
   volume, but `d1_sel=0` selects **X0**, not the accumulator **A** the volume had actually
   been loaded into (per `move_ea_reg`'s docstring, memspace=X's d1_sel mapping is
   `0=X0,1=X1,2=A,3=B`) - both operators' memory slots silently got initialized with stale X0
   garbage instead of 100/200. Fixed by using `d1_sel=2` (selects A) for that store. Second
   run: `MARK_PASS` (0xD7D700), both operators' final memory-resident volumes exactly correct
   and mutually independent. **Fifteen building blocks verified** - the register-file scaling
   problem for 32 operators is solved, not just assumed solvable.

   **Immediately generalized, same session** (`isolate_operator_loop.py`/`drv27.cpp`/
   `dt27.tos`): `isolate_multi_operator.py` proved 2 operators can hold independent
   memory-resident state, but got there by hand-duplicating straight-line code per operator -
   fine for 2, unworkable for 32 (32 copies of the same code would blow well past the
   512-word boot image, aside from being unmaintainable). This test proves the mechanism a
   real 32-operator implementation actually needs: **one loop body**, driven by a runtime
   index register, dynamically computing each operator's memory address in turn - combining
   the real loop construct (`isolate_real_loop.py`) with dynamic addressing
   (`isolate_dynamic_addr.py`) and memory-resident per-operator state
   (`isolate_multi_operator.py`) for the first time, all three at once.

   4 operators, one loop, 4 iterations: `volume[i]` at `X:$0710+i`, `eg_inc[i]` at
   `X:$0720+i`, each iteration computes `volume[i] += eg_inc[i]` via a dynamically-addressed
   read/read/add/write, then increments the index and loops. Loop-exit test exploits
   `OPERATOR_COUNT=4=0b100`: indices 0-3 all have bit2 clear, and incrementing to 4 sets bit2
   for the first time, so a single `JSET` on bit2 after the increment is an exact "done"
   test with no separate counter register needed. Expected
   `[10,20,30,40]+[1,2,3,4]=[11,22,33,44]`. **Passed first try** - all four results correct,
   confirming the loop correctly re-addresses a fresh operator each pass rather than
   accidentally reusing one slot's address (the exact bug class the memory-init mistake in
   `isolate_multi_operator.py` belonged to). **Sixteen building blocks verified.** Scaling
   from "2 operators, hand-duplicated" to "N operators, one real loop" is now proven, not
   just planned - going from 4 to 32 in the real implementation is a constant-count change,
   not a new mechanism.

   **Still not done**: the real `eg_inc[]`/`sin_tab[]`/`tl_tab[]` tables' full contents in DSP
   memory
   (only representative slices sent so far - the real loop construct makes this a work item,
   not an open question). **Transfer time measured, same session**
   (`isolate_transfer_timing.py`/`drv22.cpp`/`dt22.tos`): 1024 words (a realistic `sin_tab[]`
   size) sent from the 68030 and received via the real loop construct took **3 ticks of the
   classic 200Hz `_hz_200` system timer (~15ms)**, read back via the DSP reporting the
   68030-measured elapsed count over SSI/crossbar (the same "transmit and observe" technique
   used throughout this session, applied here to get a timing number out reliably - `printf`
   from a running DSP test was never reliably captured earlier this session). Extrapolating
   linearly, the real `tl_tab[]` (6656 entries) would take roughly 100ms at boot - not a
   practical bottleneck for a one-time load, though `_hz_200`'s ~5ms granularity makes this a
   coarse estimate, not a precise one. This settles what was previously an open question:
   loading the real, large tables at boot time is practical, not just theoretically possible.

   Still needed: replacing the crude `>>7` attenuation approximation with the real log-domain
   `tl_tab[]` scheme for bit-exact fidelity to the CPU reference; and all of the above for 32
   operators at once, each
   independent, combined through the 8 FM algorithms' operator-to-operator connections.
   Substantial remaining work, but no more open questions about whether the core mechanisms
   work - only about assembling and scaling them to a real, multi-operator, multi-voice synth.
5. All 32 operators + all 8 algorithms + feedback, checked the same way.
6. LFO (AM/PM) and noise channel.
7. Wire into `osoundint`/`atari/audio.cpp` as an opt-in `cannonball.ini` backend (`sound=2` or a new
   key), falling back to the existing CPU-side YM2151 when the DSP path isn't selected or isn't
   present — same "measured, opt-in, never a silent default" pattern as `road_hres`/`vscale`.
8. **Untested on real hardware** (as before) — everything here is Hatari `--dsp emu` only.

## Real hardware constraint found: R/N/M (AGU) registers are 16-bit, not 24-bit

Discovered while starting the separate `.mod`-on-DSP effort (see next section) - a genuine
DSP56001 hardware fact, not a bug, that had been silently invisible for the whole YM2151 effort
above. `dsp_write_reg()` in `dsp_cpu.c` (~line 1514):
```c
case DSP_REG_R0: ... case DSP_REG_M7:
    dsp_core.registers[numreg] = value & BITMASK(16);
```
**R0-R7, N0-N7, M0-M7 (the Address Generation Unit's registers) are masked to 16 bits on real
hardware.** Only the data-ALU registers (X0/X1/Y0/Y1/A/B and their A0/A1/A2/B0/B1/B2 sub-slots)
are the full 24 bits. Every use of `move_imm_reg()` (which only ever targets R0-R7/N0-N7) in the
entire YM2151 effort above loaded either a small address (`TABLE_BASE`-style, always < 0x1000)
or a small step count (1-9) - values that fit in 16 bits anyway, so this ceiling was never hit
and never visible. It surfaced the moment a real 24-bit AUDIO SAMPLE value (`0x16A100`, top byte
significant) was loaded into N0 as scratch and silently came back as `0x00A100` - found via three
bisecting isolated tests (`isolate_store_a_width.py`, `isolate_store_x0_width.py`,
`isolate_imm_width.py`, each stripping away one more layer - store-path, then plain-X0-path,
then the immediate load itself in complete isolation) before reading `dsp_write_reg` directly
confirmed the root cause. **Rule for all future DSP work**: never route a value that needs full
24-bit (or even full 16-bit-signed) precision through `move_imm_reg`/R/N/M - load it into a data
register instead, e.g. via the host-port MOVEP-into-A1 pattern (`assemble_wavetable_osc2.py`,
proven correct at full width) or `move_imm_alu` (8-bit precision only, `value8<<16`). R/N/M stay
safe for what they're actually for: addresses and small step/index values.

## `.mod` music on the DSP - started, first audible real-time mix produced

Separate effort from the YM2151 work above (see `README_ATARI.md` for the CPU-side `.mod`
player these DSP tests will eventually offload). Goal: move the actual per-sample
resample+volume+sum arithmetic (`ModPlayer::mix()`, `atari/modplayer.cpp` - currently 100% CPU)
onto the DSP, keeping tracker/pattern/effect logic on the CPU (cheap, ~50 Hz, not worth
reimplementing in DSP assembly).

**Open question resolved first**: can the DSP address more than its tiny 512-word x3 internal
SRAM, enough to hold real sample data? **Yes** - confirmed independently by two sources: Hatari's
own DSP core (`dsp_cpu.c`'s `read_memory`/`read_memory_p`, explicit `/* Falcon: External RAM */`
comments, a 32768-word external pool that X/Y alias into the upper/lower half of, P addressing it
directly) and EmuTOS's real shipped XBIOS driver (`emutos_dsp.c`: `YSIZE=0x4000`, `PSIZE=0x7ea9`,
matching numbers). The internal-512-word ceiling this project hit repeatedly is specifically
`Dsp_ExecBoot`'s *boot-image* limit, not a total-addressable-memory limit - a small (<512-word)
bootstrap/mixer-loop program loaded via `Dsp_ExecBoot` can then address ~32K words of real
sample/table data at runtime via addresses ≥0x100 (X/Y) / ≥0x200 (P), sent with the same
`Dsp_BlkHandShake`-style transfer already used throughout this project. Caveat carried over as
always: verified against Hatari's (Falcon-accurate) DSP core and EmuTOS's real driver source, not
against real hardware.

**First real-execution test** (`isolate_stereo_mix.py`/`drv28.cpp`/`dt28.tos`): two independent
wavetable oscillators (the proven phase-accumulator mechanism from `assemble_wavetable_osc2.py` -
one per DSP56001 Rn/Mn/Nn register triple, R0/M0/N0 for channel A and R1/M1/N1 for channel B,
both walking the SAME shared 32-entry sine table at different step rates: step=2 and step=3, a
3:2 ratio / musical fifth) summed sample-by-sample via the plain-ALU-add idiom
(`isolate_multi_operator.py`'s `ALU_ADD_B_A`, now exercised on real signed audio-sample words for
the first time) and streamed continuously to the SSI/CODEC.

First run produced audio with the correct *shape* but each channel's amplitude topped out at a
few hundred instead of the expected ~0x2000 (8192) - traced to exactly the R/N/M 16-bit ceiling
above: the table-fill loop was loading each 24-bit sine value into N0 as scratch before copying
it into the accumulator, silently truncating every entry's top byte. Fixed by refilling the table
the same way `assemble_wavetable_osc2.py` already does it correctly - MOVEP straight from the
host port into A1 (full 24-bit, never touching N0/R0 for the value itself). Second run: sums
matched hand-computed expected values exactly (e.g. `table[1]+table[8] = 0x16A100+0x200000 =
0x36A100`, found byte-for-byte in the crossbar trace), amplitude range -15603..+15603 (correctly
within the ±0x4000 half-scale headroom each channel was pre-scaled to, confirming no clipping),
smooth 32-sample periodic waveform (not the degenerate 8-sample buzz an earlier 8-entry-table
version produced).

**Turned into something actually audible, not just a trace-verified number**: parsed every `Dsp
set TX register` line from the real Hatari `--dsp emu` execution trace (`make_wav.py`) and wrote
them out as a real 16-bit PCM `.wav` file (`stereo_mix.wav`, 6 seconds, declared at 16000 Hz - an
estimate bracketed by wall-clock sample-count measurements during capture, not the DSP's true
hardware rate, so the *absolute* pitch may be a little off; the 3:2 frequency ratio between the
two channels is exact regardless, since it comes only from the integer step values) - sent
directly to the user, letting them hear the DSP's own real computed output without needing to
install or run an emulator themselves.

**Done immediately after, same session** (`isolate_real_sample.py`/`drv31.cpp`/`dt31.tos`): real
8-bit PCM sample data, not a synthetic sine table, played through the same two-channel mixer -
the "hallbrass" instrument (9400 bytes) from `res/mod_placeholder/AXELF.MOD`, an established
placeholder test asset for this project's `.mod` work (not real OutRun content). Two new real
problems found and solved to get here, both genuine DSP56001 hardware facts rather than bugs in
this project's own logic:

1. **9400 table entries is far too many to fill via an unrolled per-entry loop** (the 32-entry
   sine table's fill code, at ~5 words/entry, would balloon to ~47000 words - 90x the 512-word
   `Dsp_ExecBoot` cap). Fixed by using the real (non-unrolled) loop construct
   (`isolate_real_loop.py`'s template - Y1-resident counter, B1 reloaded to -1 every iteration,
   zero-tested via a bit chain, widened from 4 bits (COUNT=8) to 14 bits since 2^14=16384 is the
   smallest power of 2 exceeding 9400) - the fill loop's CODE SIZE is now independent of the
   sample length, only its *run time* scales with it.
2. **A circular buffer's modulo alignment depends on the table's base address, not just its
   size** - found by reading `dsp_update_rn_modulo()` (`dsp_cpu.c` ~line 1701) directly: the
   circular window's lower bound is computed as `r_reg - (r_reg & bufmask)` where
   `bufmask = (next power of 2 >= modulus) - 1` - i.e. the window is anchored to a
   `bufmask`-aligned boundary of whatever address R0 already holds, not to whatever address the
   fill loop happened to start writing at. For a 9400-word sample, `bufsize=16384`, so
   `TABLE_BASE` must itself be a multiple of 16384 for the circular window to actually line up
   with the uploaded data - satisfied by `TABLE_BASE=0`. (`isolate_stereo_mix.py`'s
   `TABLE_BASE=0x0100` only worked correctly because 256 happens to be a multiple of its own
   32-word modulus's `bufsize=32` too - this constraint was real there as well, just invisible
   since it happened to already hold.)
3. **`move_imm_m` (the existing M-register immediate load) only carries an 8-bit value**
   (`cur_inst[15:8]` in `dsp_movec_imm`) - fine for the 32-entry table's modulus (31), nowhere
   near enough for 9399. Needed a genuinely new encoding: `move_imm_m_long()` added to
   `dspasm.py`, using `dsp_movec_ea` (dispatch index `0xB9` in `opcodes8h[512]`, mechanically
   confirmed by parsing the table directly, not hand-counted) with `ea_mode=0x31` (the same
   "immediate value, consume an extension word" trick used everywhere else in this file) -
   `dsp_movec_ea`'s own 6-bit `numreg` field isn't restricted to the R/N-only ranges that block
   M-registers from `move_imm_reg`, giving a full 24-bit-capable path to M0-M7.

Sample bytes pre-converted on the host side (`signed_byte << 14`, landing in the same `v16<<8`
24-bit convention every earlier table used) and uploaded via the real loop's host-port receive,
exactly like `isolate_real_loop.py`'s pattern - the DSP-side fill/playback code is textually
identical to `isolate_stereo_mix.py`'s, only the data and table size differ. Two channels play
the SAME real sample at step=1 (native pitch) and step=2 (one octave up) - genuinely proving
"real instrument sample, pitch-shifted, mixed with itself" rather than an abstract tone. Result:
-11200..+10240 amplitude range (no clipping), 96% nonzero samples, a smooth non-degenerate
waveform starting near zero (consistent with a real instrument's attack envelope) - turned into
`real_sample_mix.wav` via `make_wav.py` and sent directly to the user, same as the sine-table
milestone. **Seventeenth and eighteenth building blocks verified**: a real (non-unrolled,
constant-code-size) large-table fill, and real 8-bit PCM sample playback through the DSP mixer.

**Done immediately after, same session** (`isolate_live_note.py`/`drv32.cpp`/`dt32.tos`): live
CPU control over a real-sample channel WHILE the mixer keeps running - the actual mechanism a
`.mod` tracker needs every tick. Builds on `isolate_real_sample.py` unchanged except for one
addition: a non-blocking host-port check inside the playback loop (the exact idiom
`assemble_wavetable_osc2.py` already proved for a synthetic oscillator's frequency), but this
time also **retriggering** the sample - resetting R0 back to the sample start - on every new
note, not just changing the step. This matters: a real tracker "note on" always restarts the
instrument from its first sample, it doesn't bend the pitch of whatever position the phase
accumulator happens to be sitting at, and only the retrigger makes each note sound like a clean
attack rather than a pitch glide.

Channel B stays fixed at step=2 (a steady octave-up drone, unchanged), channel A starts at
step=1 and the host sends a 7-note "melody" (steps 1,2,3,4,3,2,1 - unison up to two octaves and
back down) at ~1.5-second intervals via `drv32.cpp`'s busy-wait delay loop, each retriggering the
same real "hallbrass" sample at a new pitch. Captured via the same trace-to-`.wav` technique
(`make_wav.py`, now taking an optional third argument for clip length since this melody needed
more than the default 6 seconds) - the resulting `live_note_melody.wav`'s per-second RMS/min/max
statistics show clearly distinct plateaus lining up with the note boundaries (e.g. peak amplitude
step-changing every ~5 seconds), confirming the retrigger-and-repitch mechanism is really firing
on each host message, not just holding one fixed mix. **Nineteenth building block verified**: the
CPU can drive a DSP-resident sample channel's note/pitch live, in real time, while the mixer
keeps outputting continuously - the last missing mechanism before wiring the real `.mod` tracker
sequencer in `atari/modplayer.cpp` to this mixer instead of its own CPU-side `mix()` loop.

**Done immediately after, same session** (`isolate_sw_wrap.py` + `isolate_4ch_mixer.py` /
`drv34.cpp`/`dt34.tos`): scaled from 2 hardcoded oscillator/sample channels to 4 REAL channels
with fully memory-resident state (step, length, position - no fixed R/M/N register pair, no
hardware M-register at all), processed by one real loop per output sample - the actual
architecture a `.mod` player needs (4 is ProTracker's real channel count).

Two new mechanisms needed, each isolated and verified separately before combining, per this
project's standing discipline:

1. **Software length-wraparound** (`isolate_sw_wrap.py`/`drv33.cpp`/`dt33.tos`, `MARK_PASS`
   first try): a channel looped over in memory can't grab a free hardware M-register per
   iteration the way `isolate_real_sample.py`'s fixed R0/M0 and R1/M1 pairs could - so looping
   is done in software instead: `diff = new_position - length` via `ALU_SUB_B_A`, then testing
   `diff`'s sign bit (bit23 of A1, read directly - not through the named "A" accumulator, which
   would hit `dsp_pm_read_accu24`'s saturation quirk documented earlier this session) - negative
   means still within bounds, non-negative means wrap (reset to 0; a simplification that drops
   the remainder past `length` each wrap rather than computing exact modulo, an accepted
   trade-off for now). Verified against an 8-tick, `LENGTH=10`/`STEP=3` Python reference before
   ever assembling the DSP version, matching this project's standing rule.
2. **A real hazard found and fixed while wiring the 4-channel loop itself, this time BEFORE ever
   running it**: `move_ea_reg`'s underlying handler (`dsp_pm_1`, `dsp_cpu.c` ~line 3710-3739)
   ALWAYS performs a second, forced parallel move baked into the instruction format - it captures
   accumulator A's value before the instruction runs and writes it into **Y0** afterward, for
   ANY memspace=X call (`numreg2 = DSP_REG_Y0 + ((cur_inst>>16)&1)`, and this project's
   `move_ea_reg` wrapper never sets bit16, so it's always Y0). An early draft of the 4-channel
   loop tried to hold each channel's OLD position in Y0 across several more
   `move_ea_reg(X, ...)` calls (the position store, the sample read) - every one of those would
   have silently overwritten Y0 with a stale copy of A, corrupting the saved position. Caught by
   re-reading `dsp_pm_1` directly after noticing the risk during design, not by a failed run this
   time - fixed by holding the value in N3 instead (immune - the side effect only ever targets
   Y0) and re-deriving the sample-read address from N3 only after the new_position/wraparound
   math (which needs its own A/B/X0/X1 churn) fully completes. Y1 (the persistent 4-channel mix
   sum) was never actually at risk, confirmed by the same source read - and retroactively
   consistent with the sample-fill loop elsewhere in this same file, which already relies on Y1
   surviving `move_ea_reg(X, EA_CASE_INC, ...)` calls without issue.

Combined result: all 4 channels play the same real "hallbrass" sample (`AXELF.MOD`) at once, at
4 different pitches (step=1,2,3,4 - unison through two octaves, a spread chord rather than a
sequence), each channel's table amplitude pre-scaled to `signed_byte<<13` (half of the
2-channel tests' `<<14`) for 4-way headroom. **Passed first try**: -10048..+10080 amplitude
range (nowhere near clipping), 98% nonzero samples, genuinely complex/non-degenerate waveform -
turned into `mixer4ch.wav` and sent to the user, same technique as the earlier milestones.
**Twentieth and twenty-first building blocks verified.**

**Done immediately after, same session** (`isolate_volume_mpy.py`/`drv35.cpp`/`dt35.tos`, then
`isolate_4ch_mixer_vol.py`/`drv40.cpp`/`dt40.tos`): real per-channel volume, the last item on the
prior "still not done" list. `scaled_sample = sample * volume / 64` (ProTracker convention,
volume 0-64, 64=unity) via a real MPY (`ALU_MPY_Y0_X0_A`) plus 7 `ASR_A` steps (`2*64=128=2^7`) -
bit placement derived from `mul56_ref.py` and cross-checked against random signed samples in
Python before ever assembling anything, per standing rule.

Two real bugs found while building the isolated test, both root-caused with dedicated
diagnostics rather than guessed at:
1. **The exact 16-bit R/N/M-register truncation bug class documented earlier this session,
   recurred in a NEW spot**: the test's own constant `SAMPLE=0x100000` was loaded via
   `move_imm_reg` (R/N-range) as a convenience, silently zeroing it (0x100000 exceeds 16 bits).
   Test-harness-only - the real mixer already loads sample data through the safe host-port path.
   Fixed with `move_imm_alu` instead.
2. **A genuinely new finding**: even after fixing (1), the result still came back 0. The MPY
   itself was verified correct in isolation first (`A1=0x000004`, exactly matching
   `mul56_ref.py`) - the bug was downstream, in reading the WRONG accumulator slot after the
   shift chain. Every earlier envelope-generator MPY use in this project (the YM2151 work)
   always read A1 after its shift chain, because those specific products/shift-counts happened
   to keep the meaningful bits within A1's range (bits 47:24 of the 56-bit accumulator) the
   whole time. This test's product is large enough that after 7 right-shifts, the significant
   bits migrate DOWN into A0's range (bits 23:0) - confirmed by a diagnostic transmitting both
   A1 and A0 after each of the 7 shifts, watching the value visibly move from A1 into A0 at step
   3 of 7. Fixed by reading A0, not A1, after the shift chain. **Rule for future MPY+shift use**:
   check where the answer actually lands (A1 or A0) for the SPECIFIC magnitudes involved, rather
   than assuming A1 by precedent.

Wired into the 4-channel mixer: records grow from 3 fields (step, length, position) to 4 (+
volume), each channel's raw sample is now scaled by its own volume before summing. Per-channel
volumes 64/48/32/16 (descending as pitch rises, channels step=1/2/3/4). **Passed first try**:
amplitude range -6272..+6128, versus -10048..+10080 for the otherwise-identical no-volume test -
a ~62% ratio, matching the average applied volume ((64+48+32+16)/4=40, 40/64=62.5%) almost
exactly, strong quantitative confirmation the scaling is real and correctly proportioned, not
just "some smaller number." **Twenty-second and twenty-third building blocks verified.**

**Done immediately after, same session** (`isolate_4ch_multisample.py`/`drv41.cpp`/`dt41.tos`):
different instruments per channel, not just different pitches of one shared sample - the last
"still not done" item from the 4-channel work. Channel records grow from 4 fields to 5 (step,
length, **sample_base**, position, volume) - `sample_base` is now per-channel data read from the
record, not a compile-time constant, so each channel can point at a completely different region
of DSP memory.

Four real, different AXELF.MOD instruments uploaded to non-overlapping regions and played
together: `hallbrass` (9400 bytes, channel 0, native pitch), `bassdrum2` (3000 bytes, channel 1,
native pitch), `hihat2` (2000 bytes, channel 2, one octave up), `digdug` (3100 bytes, channel 3,
native pitch) - a melodic note, a kick, a pitched-up hihat and a fourth texture, all mixed live -
much closer to real `.mod` music than four pitches of one sound. One real address-planning bug
caught before assembling, not after a failed run: the first memory layout put
`CHANNEL_STATE_BASE` at `0x4500`, inside the 4th sample's own range (`digdug` at `0x3A00` runs
through `0x461B`) - caught by writing a small Python range-overlap check before ever running
anything, moved `CHANNEL_STATE_BASE` to `0x4700` with margin. No hardware-modulo alignment
constraint applied here (unlike `isolate_real_sample.py`'s fixed M-register channels) - the
software wraparound this design already uses works at any base address, so the four samples only
needed to not overlap each other, not satisfy any power-of-2 alignment.

**Passed first try**: -7104..+5744 amplitude range, no clipping, 99.7% nonzero, a visibly
richer/more varied waveform than any single-instrument test (multiple distinct real textures
combining, not four transpositions of one sound) - turned into `mixer4ch_multisample.wav` and
sent to the user. **Twenty-fourth building block verified.**

**Still not done**: wiring the CPU-side tracker's ACTUAL per-tick note/volume/pitch/instrument
values (not hardcoded test values) into this mixer - `ModPlayer`'s own `process_tick()` already
computes exactly these values (`Channel::period`/`step`/`volume`/`sample`) every tick, this is
now "send what it already computes over the host port" rather than a new mechanism; respecting
each instrument's own loop-start/loop-length metadata instead of looping the whole sample;
volume envelopes / note-off fades (only a static per-note volume proven, not a changing one);
replacing `ModPlayer::mix()`'s CPU call with this per-tick host-port push; measuring whether
host-port throughput can keep up with 4 real channels' worth of per-tick updates without
stalling the mixer; total external-memory budget for a real song's FULL sample set (this test's
4 samples used ~18K of the ~32K-word external space - a real multi-sample song could exceed it,
not yet measured against a real `.mod` file's total sample data size). Untested on real
hardware, as always. Every mechanism the DSP side of a real 4-channel `.mod` mixer needs is now
individually proven - what remains is CPU-side integration work in `atari/modplayer.cpp`, not
further DSP unknowns.

**Done immediately after, same session** (`isolate_generic_loader.py`/`drv42.cpp`/`dt42.tos`):
the precondition for embedding a FIXED compiled DSP boot image in the actual game, rather than
regenerating one per test. Every multi-sample test up to this point (`isolate_4ch_multisample.py`
included) had its sample COUNT and each LENGTH baked into the DSP program at Python-assembly
time - fine for a fixed test, useless for a real `.mod` file, whose sample count (up to 31) and
lengths are only known at runtime, read from the file. This test proves a genuinely generic
loader: the host sends `NUM_SAMPLES` first, then for each sample its `LENGTH` followed by that
many PCM words - a **nested real loop** (outer: per-sample, inner: per-word), the first time
this project has needed two real loops at once. A single persistent write pointer (R0) keeps
advancing across ALL samples without ever resetting mid-load, reset only once before the whole
multi-sample transfer begins.

Base-address bookkeeping is deliberately NOT reported back by the DSP - both sides derive each
sample's base address the same deterministic way (`base[0]=TABLE_BASE`,
`base[i]=base[i-1]+length[i-1]`), so the host, which already knows the lengths it's sending, can
compute matching addresses independently and use them directly in channel records later, with no
round-trip needed.

Test: 3 samples of different lengths (5, 3, 4 words) with distinct per-sample marker values,
verifying both that boundaries land at the right addresses AND that values inside each sample
are exactly the ones sent for that sample. **Passed first try**: the readback sequence matched
the expected 12-word sequence exactly, byte for byte, in order. **Twenty-fifth building block
verified.** The DSP boot program is now completely song-independent - the actual next step is
wiring `atari/modplayer.cpp` to this stable protocol (a fixed compiled boot image, checked into
the source tree, driven by real `.mod` file data read at runtime) rather than any further
isolated DSP verification.

## The combined, final DSP mixer program - every mechanism in one boot image

`dsp_mixer_final.py`/`drv43.cpp`/`dt43.tos` - everything proven separately this session, combined
into ONE ~295-word boot image: the generic nested-loop multi-sample loader, generic 4-channel
record init, the real 4-channel software-wraparound mixer with per-channel volume (MPY-scaled),
and a NEW per-tick **live update protocol** - the direct DSP-side counterpart of what a real
`.mod` tracker's `process_tick()` needs to do every tick.

**Protocol** (all values 24-bit words over the host port, same `send24()`/MOVEP pattern used
throughout this whole project):
- Boot-time: `NUM_SAMPLES`, then per sample `LENGTH` + that many pre-shifted (`signed_byte<<13`)
  PCM words (the generic loader); then per channel (4x) `step, length, sample_base, position(0),
  volume` (the generic channel-record fill).
- Per-tick, only for channels that actually changed (not required every tick for every
  channel): `channel_index (0-3), step, length, sample_base, volume` - always a full note-on
  (position is reset to 0 by the DSP itself, not sent). Checked ONCE per output sample,
  non-blocking - if no host byte is waiting, the mixer proceeds straight to mixing with zero
  added latency; if one is waiting, the DSP blocks only for the remaining 4 words of that one
  message (already guaranteed in flight) before applying it and continuing.

A first design for this protocol tried to make per-tick updates skip the `length` field (since a
channel's current instrument's length rarely changes tick-to-tick) by reading it back and
writing it unchanged to "skip over" it - this was wrong (each `EA_CASE_INC` access advances its
own address independently, so the "read then write back" pair actually read `length` and then
wrote it into the WRONG field, corrupting `sample_base`) and was caught during design, before
ever assembling it, by re-tracing the address arithmetic by hand. Simplified instead to always
sending all 4 fields (`step, length, sample_base, volume`) - marginally more per-message data,
but correct and much simpler; a lighter "same note, only volume/pitch changed" update (needed
for tremolo/portamento effects, which must NOT reset position) is deferred, noted as a real gap
for later rather than worked around.

**Test**: boot with all 4 channels silent (volume=0), then drive a tiny 8-event "song" purely
via live per-tick updates - a 2-note-alternating melody on channel 0 (`hallbrass`, real AXELF.MOD
sample) at varying pitch/volume, a repeating kick on channel 1 (`bassdrum2`), channels 2-3 left
silent throughout - `drv43.cpp` sending each update after a fixed delay, simulating a tracker's
tick timer. **Passed first try, and cleanly demonstrates the exact behavior a real player
needs**: per-second RMS/amplitude analysis of the captured `.wav` shows the first ~3 seconds are
EXACT digital silence (`rms=0, max=0, min=0` - the boot-time silent init holding correctly with
zero spurious noise), then amplitude steps up and varies as each of the 8 live update events
arrives over the following ~7.5 seconds - direct, quantifiable proof that "no update pending"
truly produces nothing and "update arrives" truly and immediately changes what's audible, with
the mixer never glitching or stalling in between. Turned into `dsp_mixer_final_demo.wav` and
sent to the user.

**This is the last DSP-side milestone of this session.** Every mechanism a real 4-channel `.mod`
mixer's DSP half needs - sample loading (any count, any lengths, runtime-driven), multi-instrument
per-channel playback, software wraparound looping, per-channel volume, and now live per-tick
control matching a tracker's own update cadence - is proven, combined into one stable,
song-independent protocol, and verified by real Hatari `--dsp emu` execution. What remains is
CPU-side work: embedding `dsp_mixer_final.bin`'s compiled bytes into `atari/`, writing a
`DspMixer` class that speaks this exact protocol over the real host port (`0xffffa200`
registers, not Hatari's trace log), and modifying `atari/modplayer.cpp`'s `process_tick()` to
call it instead of running `ModPlayer::mix()` on the CPU.

## Fractional (Q8.8) step - found necessary while starting the C++ integration, fixed before writing any game code

Real gap, not a DSP unknown: every DSP mixer test up to this point used small PLAIN INTEGER
steps (1,2,3,4) - fine for a demo, but `modplayer.cpp`'s actual `Channel::step` is a **Q16.16
fractional** value (`update_step()`: `step = (src_hz/out_rate)*65536`), needed to hit real
musical pitches. Plain integer steps would have played real `.mod` music audibly out of tune.
Q16.16 itself doesn't fit this DSP's 24-bit ALU for realistic sample lengths (9400 in Q16.16 is
~615 million, needs ~30 bits) - chose **Q8.8** instead (9400 in Q8.8 is ~2.4 million, needs ~22
bits, safely inside 24). Found and fixed *before* writing any C++ integration code, at the
user's explicit choice between "fix precision first" and "ship integer steps now, fix later" -
verified in isolation first, per standing discipline, rather than discovered as a bug after
building on top of it.

`isolate_fractional_step.py`/`drv44.cpp`/`dt44.tos`: position and step become Q8.8
(`index<<8 | fraction`) instead of plain integers. The wraparound check's STRUCTURE is
unchanged (ADD, SUB against a pre-shifted length, test the sign bit) - only the VALUES are now
Q8.8, with `length` pre-shifted to `length*256` by the host once per channel record, no
DSP-side shift needed there. The one genuinely new step: extracting the actual table READ INDEX
from a Q8.8 position needs an 8-bit right shift (8x `ASR_A`) - checked, not assumed, to land
entirely within A1 with no A0 spillover (the opposite of the earlier volume-MPY surprise,
where the answer DID spill into A0 - here the position magnitudes involved are nowhere near
large enough to spill after only an 8-bit shift, confirmed by rerunning the same "check where
it lands" diagnostic technique).

Test: `LENGTH=10, STEP_Q8=320` (1.25 in Q8.8 - a real fractional step), 12 ticks, transmitting
the extracted index after each tick. Python reference computed independently first:
`[1,2,3,5,6,7,8,0,1,2,3,5]` (the 3→5 skip and the wrap to 0 after tick 7 are both genuine signs
of real fractional accumulation, not silent rounding to integer steps). **Passed first try**,
exact match. Folded into `dsp_mixer_final.py` (8 extra `ASR_A` instructions in the per-channel
mix path, `length`/`step` now documented as Q8.8 everywhere in that file's protocol comments)
and re-verified with the same live 8-event mini-song test as before, this time using real
fractional steps (1.125x, 1.5x, 1.0x, 1.875x) instead of small integers -
`dsp_mixer_q88_demo.wav`, same clean silence-then-music structure, no clipping. **Twenty-sixth
building block verified.**

## DMA+DSP audio coexistence - a real architectural blocker found and solved while starting the C++ integration

While starting to design the actual `atari/` wiring, re-reading Hatari's `crossbar.c` directly
(the same source this session's earlier "Audio out" phase already used) surfaced a real
constraint that would have made the C++ integration silently break existing game audio:

> "one receiving device can be connected to only one source device"

The DAC is one receiving device. Every earlier test this session called
`Devconnect(DSPXMIT, DAC, ...)`, which - in the real game, not an isolated dsptest driver -
would **disconnect the existing DMA-sound-based FM/PCM audio from the DAC**, not add to it:
enabling DSP-mixed `.mod` playback would have silenced the game's existing music and sound
effects while active. Found and raised to the user before writing any integration code, with
two options offered (accept the exclusivity now vs. build real coexistence); the user chose
real coexistence.

The crossbar diagram also shows DMA PLAYBACK is routable to **DSP RECEIVE**, not just DAC - so
the fix is to make the DSP itself the single combined DAC source: reroute the existing DMA
audio into the DSP's SSI receive, have the DSP sum it with its own 4-channel `.mod` mix each
sample, and send only the combined total onward to DAC via SSI transmit (unchanged). Verified
in three steps, each isolated first per standing discipline - this is the first work all
session using DSP SSI **receive**; everything before this was DSP-to-DAC transmit only:

1. **`isolate_ssi_relay.py`/`drv45.cpp`/`dt45.tos`** - pure passthrough (receive, transmit back
   unchanged), to prove data really arrives before trusting anything about its format. Played a
   known repeating 8-bit DMA pattern (100,50,0,-50) through real DMA sound hardware (the same
   register sequence `audio.cpp`'s `start_dma()` already uses), routed `Devconnect(DMAPLAY,
   DSPRECV,...)`. **First run relayed nothing but zeros** - root-caused by reading
   `crossbar.c`'s `Crossbar_DstControler_WriteWord()` directly: `$FF8932` (the destination
   control register) has its OWN bit7, tied to `dspReceive.isTristated` - exactly symmetric to
   the ALREADY-KNOWN `$FF8930`/DSPXMIT bit7 issue from this project's very first "Audio out"
   phase (EmuTOS's `Devconnect()` "deliberately leaves bit 7 alone" on both registers, not just
   the source one). Fixed by ORing `0x0080` into `$FF8932` too, mirroring the established
   `$FF8930` fix. Second run: real DMA sample values (50, 100, -50 in Q8.8-adjacent raw form)
   correctly relayed through DSP receive and back out to DAC.
2. **`isolate_ssi_receive_sum.py`/`drv46.cpp`/`dt46.tos`** - receive PLUS a fixed own-generated
   value, summed (not just relayed), the actual mechanism needed. **First run's sums were wrong
   by a factor matching an extra `<<8`** - root cause: the received value from
   `dsp_core_ssi_readRX()` is ALREADY in this project's established v16-in-bits[23:8] SSI
   convention (symmetric with transmit), not a plain small integer at the LSB the way an
   earlier test's result had been (mis-)read to suggest - that earlier reading came from
   Hatari's own already-divided-by-256 *display* trace line, not the raw register. Bisected
   with a 3-stage diagnostic (`isolate_ssi_recv_stages.py`, transmitting the value at each of
   3 points: right after receive, after the (wrongly added) shift, after the sum) that showed
   the value was already correctly positioned before any shift of this test's own ran. Fixed by
   removing the shift - the received value adds directly. Second run: exact expected sums
   (0x007400, 0x004200, 0x001000, 0xFFDE00) for all 4 known DMA samples.
3. **Folded into `dsp_mixer_final.py`** (SSI receive enabled via CRB bit13/RE; a non-blocking
   RDF check right before the SSI TX write, adding whatever DMA-sourced sample is waiting into
   the 4-channel `.mod` mix sum, or silence if none - a rate mismatch between DMA's own
   hardware rate and this DSP's output rate is expected, not treated as an error) and verified
   with a new combined test (`drv49.cpp`/`dt49.tos`): a continuous, independent DMA "drone"
   (standing in for the game's existing FM/PCM audio) playing alongside the same live 8-event
   `.mod` mini-song. **Passed first try, and gives quantitative proof of real coexistence, not
   just "no crash"**: the first 3 seconds' RMS (23.7) matches the drone's OWN theoretical RMS
   exactly (`sqrt((30²+15²+15²+30²)/4) ≈ 23.7`) - proving the DMA content plays correctly and
   continuously from the very start, unlike every earlier pure-DSP test (which was exact
   digital silence until the first update arrived) - then amplitude rises further, and stays
   elevated, as the `.mod` notes layer on top from second 3 onward. `dsp_dma_coexist_demo.wav`
   sent to the user. **Twenty-seventh, twenty-eighth and twenty-ninth building blocks
   verified.**

No further DSP unknowns block the C++ integration work - this was the last one, and a
significant one (without it, the integration would have shipped a real regression: existing
game audio silenced whenever `.mod`-on-DSP is enabled). Untested on real hardware, as always -
everything in this document is Hatari `--dsp emu` only.

## The real C++ integration - built, compiled, and confirmed working in the actual game

`.mod`-on-DSP is now wired into the real port, not just `dsptest` isolated drivers:

- **`atari/dsp_mixer_bin.h`** - `dsp_mixer_final.py`'s compiled boot image, copied in verbatim
  (do not hand-edit; regenerate from that script and re-verify in `dsptest` if the DSP program
  ever needs to change).
- **`atari/dsp_mixer.hpp`/`.cpp`** - the host-side `DspMixer` class: `init()` (boots the DSP,
  wires both `Devconnect()` legs and both bit7 fixes from the "DMA+DSP audio coexistence"
  section above), `begin_song_load()`/`upload_sample()`/`end_song_load()` (the generic loader
  protocol, driven by a real `.mod` file's actual sample count/lengths read at runtime - not
  hardcoded per test the way every `dsptest` driver was), `update_channel()` (the per-tick live
  update protocol), `shutdown()` (restores the crossbar to its pre-game state - DMA routed
  straight to DAC again, both DSP tristate bits cleared - same "leave hardware the way you
  found it" reasoning as `atari/input.cpp`'s IKBD mouse fix earlier this session).
- **`atari/modplayer.cpp`/`.hpp`** - `ModPlayer::dsp_mode`, set in `load()` when
  `atari_opt.mod_dsp` is on, the DSP is available, and the module has ≤4 channels (falls back to
  CPU mixing silently otherwise); `trigger_note()` forwards every genuine retrigger to
  `dsp_mixer.update_channel()` (step converted from the CPU's own Q16.16 to the DSP's Q8.8 by a
  plain `>>8`); `mix()` skips its own per-channel CPU accumulation entirely when `dsp_mode` is
  set (the DSP is already streaming those samples straight to the CODEC).
- **`atari/options.hpp`/`.cpp`** - new `mod_dsp` ini key (0 default, needs `mod=1` too - same
  "measured, opt-in, never a silent default" convention as `road_hres`/`vscale`).
- **`main_atari.cpp`** - `dsp_mixer.shutdown()` added to `quit_cleanup()`, alongside the
  existing `audio.stop_audio()`/`input.shutdown()`.

**Verified in the actual compiled game, not just `dsptest`**: `CB030.TOS`/`CB060.TOS` (built via
the normal `build_release.sh`) launched in Hatari (`--machine falcon --memsize 14 --dsp emu
--trace crossbar,dsp_host_ssi`, `mod=1`/`mod_dsp=1` in `outrun.ini`, `Music\TRACK1-3.MOD` all
present) with real keyboard/mouse input driven via Windows `SendKeys`/`mouse_event` automation
(no GUI automation tool was available for this - PowerShell's `user32.dll` P/Invoke was used
directly) to insert a coin and reach the real music-selection screen. Confirmed by:
- The crossbar trace showing the EXACT same `$ff8930`/`$ff8932` write sequence as the isolated
  `drv49.cpp` coexistence test (source=0x0000 then 0x0080, destination=0x0000 then 0x2080 then
  0x0080) - the real game's `DspMixer::init()` doing exactly what the isolated test proved.
- 85,000+ real `Dsp set TX register` events once the music-select screen (with its own live
  preview-on-selection feature) appeared on screen, showing "PASSING BREEZE" selected and
  `CREDIT 1`.
- The extracted audio (`real_game_dsp.wav`, sent to the user) - a genuine, unedited recording of
  the actual game's own DSP output during this session, not a synthetic test signal: -4992..+4640
  amplitude, no clipping, 99.7% nonzero.
- No crash, hang, or corruption at any point across the whole boot -> attract -> coin-insert ->
  music-select sequence.

**Real, current limitations** (all previously documented as deferred, restated here as the
actual state of the shipped integration, not just plans): only full note-on retriggers reach the
DSP (effects that change step/volume without retriggering - arpeggio, vibrato, portamento,
volume slide - are computed by `do_effect_tick()` as always, but not yet forwarded, so they have
no audible effect while `dsp_mode` is active); sample loop points (`loop_start`/`loop_length`)
are not respected by the DSP mixer, which always loops the whole sample; ≤4-channel modules
only; and everything above is Hatari `--dsp emu` only - **real hardware testing remains the one
genuinely open item**, for this feature and every other DSP mechanism this session built.

## Real bug found in the actual game, right after the section above was written: a second song load corrupted the mix

Caught by the user directly watching/listening to the real game (not by any automated
trace-based check this session had been relying on) - a sharp, immediate correction that the
DSP mix was producing garbage, not music. Root cause, confirmed by re-examining the actual
compiled game's trace: `DspMixer::begin_song_load()` only ever sent `NUM_SAMPLES` and the
sample data over the host port - it never re-executed `dsp_mixer_final_dsp`'s boot image. That
loader protocol only exists at the very start of the DSP's boot execution, before it falls
through into the permanent mixing loop; it is not a state the DSP can re-enter on its own. The
real game's music-selection screen has a "preview on selection" feature
(`omusic.cpp`/`config.sound.preview`) that calls `ModPlayer::load()` - and therefore
`dsp_mixer.begin_song_load()` - more than once per session as the player (or attract-mode AI)
cycles through tracks. The FIRST load landed correctly, since the DSP was freshly booted; the
SECOND load sent a fresh `NUM_SAMPLES`/`LENGTH`/PCM-data stream into a DSP that was already
running its mixing loop, which misinterpreted those words as a stream of 5-word per-tick
channel updates - permanently desynchronizing the channel state. Confirmed in the real captured
trace: clean, correctly-scaled audio for the first ~2.5-2.7 seconds (the first song, playing
normally), then a sudden, PERMANENT jump to sustained full-24-bit-range output, hard-clipping at
the int16 boundary for the rest of the session - not a transient glitch, a stuck failure state.

Fixed by moving `Dsp_ExecBoot()` out of the one-time `init()` (which now only does the
one-time `Dsp_Lock()` + crossbar/`Devconnect()` setup) and into `begin_song_load()` itself,
called unconditionally on every song load: this always returns the DSP to a known, correct
loader entry point before sending new sample data, regardless of what it was doing before.

**Verified with a targeted isolated test built specifically to reproduce this exact
sequence** (`drv50.cpp`/`dt50.tos`, in `dsptest`): boot once, load song A (`hallbrass`), let it
mix and play for several seconds, then load song B (`bassdrum2`) - via the SAME fixed
`Dsp_ExecBoot()`-per-load pattern `dsp_mixer.cpp` now uses. Captured trace shows song A playing
cleanly (RMS ~800, stable amplitude bounds, no clipping) for the first ~7.5 seconds, a brief
pause during the reload, then song B playing cleanly (RMS ~1430, different but equally stable
amplitude bounds, no clipping) from ~8 seconds onward - a clean transition between two loads
with the DSP mid-mixing-loop at reload time, the exact scenario that broke before the fix.
`reload_test.wav` sent to the user. **Thirtieth building block verified** - and a reminder that
trace-log analysis alone had missed this for a full session's worth of isolated tests, because
none of them ever called the loader protocol a second time; only testing the real integration
end-to-end in the actual game surfaced it.
