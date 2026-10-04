# OutRun – Cannonball for Atari Falcon 030 and Falcon CT60 / CT63 (68060)

**English** | [Français](README.fr.md)

A port of Chris White's **[Cannonball](https://github.com/djyt/cannonball)** engine (a C++
rewrite of the 68000/Z80 code of SEGA's **OutRun** arcade machine) to the **Atari Falcon**:

| Machine | Executable | CPU | Status |
|---|---|---|---|
| Original Falcon 030 | `CB030.TOS` | 68030 @ 16 MHz | Runs, but too slow to be really playable (~35-55 % of real speed) |
| Falcon + CT60 / CT63 | `CB060.TOS` | 68060 (+ Fast RAM) | **Full speed** (100 % real time, measured in Hatari) |

> **On the original 16 MHz Falcon**, `CB030.TOS` runs the game but it is not really playable
> (about one picture every 1.3 s with the default settings). The lighter settings listed in
> [`outrun.ini`](#options-file-outrunini) help. For full speed, use `CB060.TOS` on a Falcon
> with a CT60 / CT63.

**Current version: v0.30 beta**. The full changelog is in [`VERSION.txt`](VERSION.txt).

> ⚠️ **The OutRun ROMs are not included** (they belong to SEGA). You need your own dump of
> the game, **revision B**: see [Install the ROMs](#2-install-the-roms).

### Download

The ready-to-run package is on the **[Releases](../../releases)** page:
`cannonball_falcon_v0.30-beta.zip` contains `CB030.TOS`, `CB060.TOS`, `PADTEST.TOS`, `ROMNAME.TOS`, `outrun.ini`, the `res/`
folder and empty `roms/` and `Music/` folders with instructions. Unzip it, add your ROMs
(and optionally your `.mod` files) and run. No ROMs or `.mod` music are included.

---

## Contents

1. [Repository layout](#repository-layout)
2. [Quick start](#quick-start)
3. [Controls](#controls)
4. [Options file `outrun.ini`](#options-file-outrunini)
5. [`.mod` music (optional)](#mod-music-optional)
6. [Running in the Hatari emulator](#running-in-the-hatari-emulator)
7. [Building from source](#building-from-source)
8. [How the port works](#how-the-port-works)
9. [Measured performance](#measured-performance)
10. [Known limitations and untested areas](#known-limitations-and-untested-areas)
11. [Further documentation](#further-documentation)
12. [Credits and licence](#credits-and-licence)

---

## Repository layout

```
.
├── dist/                    ← ready-to-run binaries (v0.30 beta)
│   ├── CB030.TOS            original Falcon 030 (68030 @ 16 MHz, slow)
│   ├── CB060.TOS            Falcon + CT60 / CT63 (68060, full speed)
│   ├── outrun.ini           preset options (.mod music played by the DSP)
│   ├── VERSION.txt
│   ├── roms/                ← put YOUR ROMs here (see roms.txt / README.txt)
│   └── Music/               ← put YOUR TRACK1..4.MOD files here (see README.txt)
│
├── src/main/                ← Cannonball engine C++ sources
│   ├── engine/              OutRun game logic (original code + Atari hooks)
│   ├── hwvideo/ hwaudio/    video and sound chip emulation (YM2151, SegaPCM)
│   ├── frontend/            configuration (PLATFORM_ATARI branch without Boost/XML)
│   ├── atari/               ★ Atari platform layer (video, DMA sound, DSP, keyboard, joysticks, 68k assembler)
│   ├── main_atari.cpp       ★ Atari entry point (replaces main.cpp / SDL2)
│   └── sdl2/ directx/       original backends (not used on Atari, except the ffeedback stub)
│
├── res/                     tilemap.bin, tilepatch.bin, config.xml… (needed at run time)
├── cmake/                   original CMake build (Windows / Linux / Pi4, not Atari)
├── docs/license.txt         Cannonball licence
│
├── build_atari.sh           ★ main build script (m68k-atari-mint cross compiler)
├── build_release.sh         builds CB030.TOS + CB060.TOS
├── build_*test.sh           diagnostic builds (keyboard, perf, music, quit…)
├── Makefile.atari           list of sources compiled for the Atari
├── outrun.ini.example       every option, with comments
│
├── README.fr.md             French version of this README
├── README_ATARI.md          technical documentation of the port
├── ATARI_PORT_FILES.md      what each file of the port does
├── DSP_NOTES.md             log of the DSP56001 work
└── README_CANNONBALL.md     original README of the Cannonball project
```

---

## Quick start

### 1. Copy the files

The simplest way is to unzip the package from the [Releases](../../releases) page: it already
has the layout below. Otherwise, from this repository, copy these to the Atari's disk, all in
one folder:

- the contents of `dist/`: `CB030.TOS` and/or `CB060.TOS`, `outrun.ini`, `roms/`, `Music/`;
- the `res/` folder from the root of this repository.

```
C:\OUTRUN\
    CB030.TOS
    CB060.TOS
    outrun.ini
    res\
    roms\
    Music\
```

### 2. Install the ROMs

Put the **31 ROM files of OutRun revision B** in `roms\`. They are listed in
[`dist/roms/roms.txt`](dist/roms/roms.txt) and in `src/main/roms.cpp`:

```
epr-10187.88    epr-10327a.76   epr-10328a.75   epr-10329a.58   epr-10330a.57
epr-10380b.133  epr-10381b.132  epr-10382b.118  epr-10383b.117
mpr-10371.9     mpr-10372.13    mpr-10373.10    mpr-10374.14
mpr-10375.11    mpr-10376.15    mpr-10377.12    mpr-10378.16
opr-10185.11    opr-10186.47    opr-10188.71    opr-10189.70    opr-10190.69
opr-10191.68    opr-10192.67    opr-10193.66    opr-10230.104   opr-10231.103
opr-10232.102   opr-10266.101   opr-10267.100   opr-10268.99
```

**Long file names and FreeMiNT.** The original names (`epr-10380b.133`…) are longer than
the GEMDOS 8.3 limit. You have two choices:

- **`freemint = 1`** (default): keep the original names. On real hardware this needs
  **FreeMiNT** (or another file system that supports long names).
- **`freemint = 0`**: works on plain TOS, but the ROMs must be renamed to 8.3 names:
  `epr-` becomes `E`, `mpr-` becomes `M`, `opr-` becomes `O`. For example, `epr-10380b.133`
  becomes `E10380b.133` and `mpr-10371.9` becomes `M10371.9`. The full table is in
  [`README_ATARI.md`](README_ATARI.md).

**`ROMNAME.TOS`** does the renaming for you: put it next to `CB030.TOS` / `CB060.TOS`, run it and
press **S** for the short 8.3 names (plain TOS) or **L** for the long names (real long names need FreeMiNT; plain TOS keeps them cut to 8.3, e.g. `EPR-1038.133`, which the game finds the same way with `freemint = 1`). It
recognises each ROM by its contents (CRC32), whatever its current name - even the truncated alias
plain TOS shows for a long name copied from a PC - and sets `freemint` in `outrun.ini` to match.

### 3. Run

Double-click **`CB030.TOS`** (original Falcon 030) or **`CB060.TOS`** (Falcon with a CT60 / CT63).
Loading the ROMs takes a few seconds, then the game starts straight into attract mode,
with no menu. Press **Return** to insert a coin and start a race.

---

## Controls

### Keyboard

| Key | Action |
|---|---|
| ← / → | Steer |
| Space | Accelerate |
| Left Ctrl | Brake |
| Left Alt / Left Shift | Change gear (LOW / HIGH) |
| Return | Coin / Start |
| V | Change view |
| P | Pause (shows "PAUSE") |
| F9 | Screenshot (`SHOTnnnn.PNG` in the game's folder) |
| Esc / F10 | Quit (confirm with ↑, cancel with ↓) |

### Standard joystick (DB9 socket, the joystick port, not the mouse port)

Left / right to steer, forward to accelerate, back to brake, fire to change gear. The
keyboard and the joystick work at the same time.

### Enhanced joystick ports (the two 15-pin sockets of the Falcon)

These ports take a Jaguar pad, or a plain joystick on an adapter:

| Pad | Action |
|---|---|
| Left / right | Steer |
| Up or **B** | Accelerate |
| Down or **C** | Brake |
| **A** (the fire button of a plain joystick) | Change gear |
| **Option** | Coin / Start |
| **Pause** | Pause |

> In Hatari, port detection has been checked (the ports read "nothing pressed" when nothing
> is plugged in). **This has not yet been tested with a real pad or joystick plugged in.**

---

## Options file `outrun.ini`

The game reads it at start-up, next to the executable. Every line is optional.
Each option is described in [`outrun.ini.example`](outrun.ini.example).

| Option | Values | Default | Effect |
|---|---|---|---|
| `shadows` | 0 / 1 | 1 | Shadows under the cars and the scenery (0 gives about +4 % frames/s) |
| `shadow_min_z` | 0..0x1ff | 0 | With shadows on, only draw them for nearby objects |
| `scenery` | 0 / 1 | 1 | 0 = no trees, signs or buildings. **By far the biggest speed-up** |
| `vscale` | 50..100 | 100 | % of the 224 lines that are computed (67 gives +17 %, 50 gives +26 %) |
| `road_hres` | 0 / 1 | 0 | Road computed at half horizontal resolution (+5 %) |
| `cadence` | 0..4 | 0 | Game steps between two frames: 0 = auto, 1 = 30 fps, 2 = 15, 3 = 10, 4 = 7.5 |
| `sound` | 0 / 1 / 2 | 2 | 0 = silent, 1 = sound always synthesised, 2 = sound only when CPU time is left |
| `music` | 0 / 1 | 1 | Turns the music off without affecting sound effects |
| `fm_half` | 0 / 1 | 0 | FM chip emulated at half rate: cheaper, but duller sound (with `fm_dsp=0` only) |
| `sound_irq` | 0 / 1 / 2 | 2 | 1 = sound made from a timer interrupt: the music keeps its tempo and has no holes even when the game runs slower than real time; 2 = automatic (on with a 68040/68060, off with a 68030, which has no time to spare for it) |
| `fm_dsp` | 0 / 1 | 1 | The FM chip (YM2151) is computed by the Falcon's **DSP56001**: same sound, bit for bit, for a fraction of the CPU time (see below) |
| `mod` | 0 / 1 | 0 | Replaces the FM music with `.mod` files (see below) |
| `mod_dsp` | 0 / 1 / 2 | 0 | Plays the `.mod` files on the Falcon's **DSP56001** instead of the CPU: 1 = SoundTracker replay (Simplet / ABSTRACT), 2 = DSPMOD 3.4 (bITmASTER of TCE; modules with more than 64 patterns, and every module on a 68040/68060, go to the Simplet replay: DSPMOD hangs on the former and is not 68060-safe) |
| `freemint` | 0 / 1 | 1 | Long ROM names (1) or names renamed to 8.3 (0) |
| `joy_accel` | `up`, `down`, `fire`, `b`, `c`, `none` (combine with `+`) | `up+b` | What accelerates with a joystick / pad |
| `joy_brake` | same | `down+c` | What brakes |
| `joy_gear` | same | `fire` | What changes gear |

Joystick words: `up` / `down` = the stick, `fire` = the button of a DB9 joystick or button A of a
Jaguar pad, `b` / `c` = Jaguar pad buttons B and C. Example, button to accelerate:
`joy_accel = fire`, `joy_brake = down`, `joy_gear = up`. Left/right always steer, and the keyboard
always works too.

The supplied `dist/outrun.ini` sets `sound=1`, `fm_dsp=1`, `mod=1` and `mod_dsp=1`.
**Lighter settings for the original Falcon 030:** `scenery=0`, `vscale=67` (or 50), `mod=1`,
`mod_dsp=1`.

---

## `.mod` music (optional)

With `mod=1`, the original FM music is replaced by **4-channel ProTracker** modules that
you supply yourself, in `Music\`:

| File | Track |
|---|---|
| `TRACK1.MOD` | Magical Sound Shower |
| `TRACK2.MOD` | Passing Breeze |
| `TRACK3.MOD` | Splash Wave |
| `TRACK4.MOD` | Last Wave (high-score screen) |

If a file is missing, that track falls back to the original FM music. The `.mod` files cost
much less CPU time than emulating the YM2151.

- **`mod_dsp=0`**: the 4-channel player written for this port (`atari/modplayer.cpp`) mixes
  the music on the CPU.
- **`mod_dsp=1`**: the music is played by the **DSP56001**, using the SoundTracker replay by
  **Simplet / ABSTRACT** (the `dsptrack` archive on dhs.nu, used unmodified), at 49,170 Hz
  stereo, at the right tempo whatever the frame rate. The game's own FM + PCM mix then goes
  through the DSP on two extra voices. If the DSP does not answer, the game falls back to CPU
  mode by itself.

> The `.mod` files are not included in this repository (covers of copyrighted tracks).

---

## Running in the Hatari emulator

Put `CB030.TOS` (or `CB060.TOS`), `roms/`, `res/` and `outrun.ini` in one folder, then
mount that folder as a GEMDOS drive:

```bash
# Original Falcon 030
hatari --machine falcon --memsize 14 --dsp emu --tos tos.img \
       --harddrive <folder>

# Falcon + 68060 with Fast RAM (CT60-like configuration)
hatari --machine falcon --memsize 14 --ttram 32 --cpulevel 6 --cpuclock 32 \
       --addr24 false --dsp emu --tos tos.img --harddrive <folder>
```

`--dsp emu` is needed for the FM on the DSP (`fm_dsp=1`) and for the `.mod` music played by
the DSP. With `--dsp none` the game falls back to the CPU by itself.

These commands were tested with `tos.img` = EmuTOS 1.4, which comes with Hatari.

**Watch out for the `--auto` trap.** `--auto FILE` does not launch the program: Hatari
*types* the file name at the desktop, after boot. To start the game automatically, put it
in the `AUTO\` folder, renamed to `.PRG`. If you still use `--auto`, give it a file name that
does not exist (`--auto NOFILE.TOS`). Otherwise the game restarts by itself as soon as you
quit it. Details are in `README_ATARI.md`.

For joysticks, map a host controller to **joystick port 1**, and to Hatari joystick ports
**2 / 3** for the enhanced ports.

---

## Building from source

### Requirements

- Vincent Rivière's **`m68k-atari-mint` GCC 4.6.4** cross compiler.
  Development was done under Cygwin, with the compiler installed in `/opt/cross-mint`.
- `bash`, `grep` and `sed`. `make` is not needed: `build_atari.sh` does without it.

### Commands

```bash
# Original Falcon 030 -> CB030.TOS
CPU=68030 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB030.TOS bash ./build_atari.sh

# Falcon + 68060 -> CB060.TOS
CPU=68060 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB060.TOS bash ./build_atari.sh

# Both release binaries at once
# (first adjust the "cd /cygdrive/c/claude/cannonball" path in the script)
bash ./build_release.sh
```

The script prints `BUILD_OK` when linking succeeds.

Important points:

- **`LINKCPU=68000` is required**, even for a 030 or 060 build. The toolchain's 68020-60
  libraries assume an FPU, which the Falcon does not have. With the wrong library, the binary
  crashes right after `Pexec`, before `main()`.
- **`OUT` must stay 8.3**: at most 8 characters, plus 3 for the extension.

### Useful build flags (pass them in `EXTRA`)

| Flag | Effect |
|---|---|
| `-DLOWRES` | Render at 160×112, doubled on screen (Falcon) |
| `-DNO_SOUND` | No sound (speed comparisons) |
| `-DSOUND_RATE=25033` | DMA mixing rate other than the default 12,517 Hz |
| `-DMOVE16` | 68060: each row is copied to the screen in MOVE16 bursts |
| `-DBENCH_N=400` | Times 400 frames and prints one result line |
| `-DPERF_PRINT` | Per-stage timings, written to `PERFLOG.TXT` |
| `-DAUTOPLAY` | Automatic coin, start and accelerator (measurements) |
| `-DKEYTRACE` / `-DPADTRACE_FILE` | Trace of the keyboard / of the enhanced joystick ports |

The full list (about thirty diagnostic aids: `ROWCHECK`, `ROADCHECK`, `PCMCHECK`,
`MUSIC_RENDER`…) is in [`README_ATARI.md`](README_ATARI.md#building).

---

## How the port works

The engine, the chip emulation (`hwvideo/`, `hwaudio/`) and the game logic are Cannonball's
original C++ code. The port adds three things:

- an **Atari platform layer** (`src/main/atari/`, `main_atari.cpp`);
- the **changes needed for the MiNT cross compiler**;
- **68k assembler routines** for the most expensive loops.

In short:

- **Video.** The engine composes a 320×224 picture of palette indices.
  - Each pixel goes through a lookup table to 16-bit RGB565
    (`truecolor_asm.S`). Rows that have not changed are not converted again (row cache). The
    screen is triple-buffered, and the video mode is chosen by the size available: VGA
    320×240 or RGB/TV 384×240 overscan, otherwise interlaced 320×400.
- **Timing.** Game logic always runs at 30 steps per second of real time. When drawing is
  slower, several steps are computed per frame, so the game keeps its speed and only
  smoothness drops.
- **Sound.** The YM2151 (FM) and the SegaPCM are emulated, then mixed and sent to the
  Falcon DMA sound at 12,517 Hz, with four buffers. Optionally, `.mod` files are played
  by the CPU or by the DSP56001 (see above).
- **FM on the DSP** (`fm_dsp=1`). Emulating the YM2151 is the most expensive part of the
  sound: with the FM music playing, a 68030 needs about 0.3 s of CPU time per game step
  (1/30 s), hence the stuttering music. The FM synthesis (envelopes, the 4 operators of the
  8 channels, the 8 algorithms, feedback, stereo) is done by a DSP56001 program instead
  (`tools/fmdsp/fm_dsp.asm`). The 68k only decodes the register writes, sends what changed
  through the DSP host port and reads the samples back: about 15 ms per step on a Falcon
  030 for the whole sound, instead of 300+ ms. The DSP's output is checked to be **identical
  sample for sample** to the CPU emulation (the three music tracks in full on the host model
  and in Hatari's DSP emulation, then the game itself), and it needs on average 11 ms of DSP
  time per step, 26 ms at worst. The FM and PCM sound come out one step (1/30 s) late.
  While a `.mod` is played on the DSP, the FM goes back to the CPU.
- **Input.** An assembler keyboard interrupt handler (`kbd_asm.S`) replaces the ACIA vector
  while the game runs and restores it on exit. It reads the IKBD joystick and the enhanced
  joystick ports.
- **68k assembler** (`src/main/atari/*.S`): sprite scanlines (with a 030/060 fast path),
  road spans, 8×8 tiles, SegaPCM channels, 16-bit conversion, DSP replay.
- **Memory.** `operator new` zeroes memory, because TOS does not and the engine relies on
  it. The screen and sound buffers live in ST-RAM; everything else can go in Fast RAM.

What each file does is detailed in [`ATARI_PORT_FILES.md`](ATARI_PORT_FILES.md).

---

## Measured performance

Measured in Hatari, in emulated time, with the game driving itself in demo mode at full
resolution:

| Machine | Time per frame | Game speed |
|---|---|---|
| Original Falcon 030 @ 16 MHz | ~1.3 s | ~35-55 % of real time |
| 68060 @ 32 MHz + Fast RAM (CT60) | ~0.065 s (15 frames/s) | **100 %** of real time |

Effect of `vscale` on the emulated 68060: 100 gives 18.3 fps, 67 gives 21.5 fps and 50
gives 23.1 fps.

---

## Known limitations and untested areas

- **Most testing was done in Hatari** (Falcon 030, and 68060 with DSP emulation, where the 68060
  is marked "experimental"). On a real CT60 the joystick was reported working; the DSP sound
  (FM on the DSP, `.mod` replays) has only been checked in Hatari so far.
- A Jaguar pad on the enhanced joystick ports has not been tested actually plugged in.
- Cannonball's frontend menus (settings, Time Trial…) are not included: the game starts
  straight away.
- There is no analogue steering wheel or pedal support.
- On real hardware without FreeMiNT, you need `freemint=0` and ROMs renamed to 8.3 (`ROMNAME.TOS` does it).
- Keyboard: the handler disables IKBD mouse and joystick packets at start-up. If a key ever
  seems stuck "down", that is the first place to look (`atari/input.cpp`).

Feedback is welcome, especially from real hardware (original Falcon 030, CT60/CT63, Jaguar pads).

---

## Further documentation

| File | Contents |
|---|---|
| [`README_ATARI.md`](README_ATARI.md) | Full technical documentation of the port: build, build flags, Hatari, pitfalls, measurements |
| [`ATARI_PORT_FILES.md`](ATARI_PORT_FILES.md) | What each added or modified file does, hooks in the engine, bugs fixed |
| [`DSP_NOTES.md`](DSP_NOTES.md) | Log of the DSP56001 work |
| [`VERSION.txt`](VERSION.txt) | Changes in v0.30 beta |
| [`outrun.ini.example`](outrun.ini.example) | Every run-time option |
| [`README_CANNONBALL.md`](README_CANNONBALL.md) | Original Cannonball README (Windows / Linux / Pi build) |

---

## Credits and licence

- **Cannonball** © Chris White and the Cannonball team: engine and OutRun reverse
  engineering ([github.com/djyt/cannonball](https://github.com/djyt/cannonball),
  [Reassembler blog](http://reassembler.blogspot.com/)). This port would not exist without
  their work.
- **SoundTracker DSP replay**: Simplet / ABSTRACT (`dsptrack` archive, dhs.nu).
- **DSPMOD 3.4**: bITmASTER of TCE.
- **YM2151 emulation**: Jarek Burczynski (MAME), as used by Cannonball; the DSP56001 FM
  program follows it exactly.
- **Atari Falcon port** (Falcon 030 / CT60 / CT63): Popov2026.

This repository is distributed under the **Cannonball licence** ([`docs/license.txt`](docs/license.txt)):

- **non-commercial** redistribution only;
- any modified version must come with its **complete source code**, which this repository
  provides;
- the copyright notice must be kept.

*OutRun is a trademark of SEGA Corporation. This project is not affiliated with SEGA. No
ROMs or copyrighted music are included.*
