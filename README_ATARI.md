# Cannonball - Atari Falcon port (CT60 / CT63, 68060)

A port of the Cannonball OutRun engine to the Atari Falcon. **A 68060 accelerator
(CT60 / CT63) is required**: on a stock 16 MHz Falcon 030 the game runs but is not
usable (about one picture every 1.3 s). The engine, the chip
emulation (`hwvideo/`, `hwaudio/`) and the game logic are the original C++;
only the platform layer is new (`src/main/atari/`, `src/main/main_atari.cpp`),
plus the changes needed to compile with the MiNT cross compiler and a set of
speed optimisations.

It needs the original OutRun (revision B) ROM set in `roms/`, which is **not**
included: supply your own dump (the 31 files listed in `src/main/roms.cpp`).
Cannonball's licence (`docs/license.txt`) allows non-commercial redistribution
and requires the complete source to accompany derivative works.

**Long ROM filenames / FreeMiNT.** The stock dump names (e.g.
`epr-10380b.133`) are past the 8.3 GEMDOS limit (9-10 character base). By
default (`freemint=1`, see [Options file](#options-file) below) the ROM set
keeps those original names, which needs **FreeMiNT** (or another
long-filename-capable kernel/filesystem) on real hardware for the files to
exist on disk at all - Hatari's GEMDOS HDD emulation papers over this by
auto-clipping, which is why it isn't visible when testing under emulation.
Set `freemint=0` instead to avoid that dependency: the loader then looks for
an 8.3-safe renamed set (`E10380b.133`, ...) - rename your ROM dump to match
the table below. The options file is named `outrun.ini` (not `cannonball.ini`)
specifically so that it is itself already 8.3-safe (6+3 characters) - the
setting needed to turn long names off must be reachable without needing long
names for anything, including this file.

| Original | Renamed (8.3) | Original | Renamed (8.3) |
|---|---|---|---|
| `epr-10187.88` | `E10187.88` | `opr-10185.11` | `O10185.11` |
| `epr-10327.76` | `E10327.76` | `opr-10186.47` | `O10186.47` |
| `epr-10327a.76` | `E10327a.76` | `opr-10188.71` | `O10188.71` |
| `epr-10328.75` | `E10328.75` | `opr-10188.71f` | `O10188.71f` |
| `epr-10328a.75` | `E10328a.75` | `opr-10189.70` | `O10189.70` |
| `epr-10329.58` | `E10329.58` | `opr-10190.69` | `O10190.69` |
| `epr-10329a.58` | `E10329a.58` | `opr-10191.68` | `O10191.68` |
| `epr-10330.57` | `E10330.57` | `opr-10192.67` | `O10192.67` |
| `epr-10330a.57` | `E10330a.57` | `opr-10193.66` | `O10193.66` |
| `epr-10380.133` | `E10380.133` | `opr-10230.104` | `O10230.104` |
| `epr-10380b.133` | `E10380b.133` | `opr-10231.103` | `O10231.103` |
| `epr-10381.132` | `E10381.132` | `opr-10232.102` | `O10232.102` |
| `epr-10381b.132` | `E10381b.132` | `opr-10266.101` | `O10266.101` |
| `epr-10382.118` | `E10382.118` | `opr-10267.100` | `O10267.100` |
| `epr-10382b.118` | `E10382b.118` | `opr-10268.99` | `O10268.99` |
| `epr-10383.117` | `E10383.117` | `mpr-10371.9` | `M10371.9` |
| `epr-10383b.117` | `E10383b.117` | `mpr-10372.13` | `M10372.13` |
| | | `mpr-10373.10` | `M10373.10` |
| | | `mpr-10374.14` | `M10374.14` |
| | | `mpr-10375.11` | `M10375.11` |
| | | `mpr-10376.15` | `M10376.15` |
| | | `mpr-10377.12` | `M10377.12` |
| | | `mpr-10378.16` | `M10378.16` |

Verified working in Hatari both ways: original names with `freemint=1`
(default), and the renamed set with `freemint=0`.

## Building

Toolchain: the `m68k-atari-mint` GCC 4.6.4 cross compiler (Vincent Riviere's
Cygwin package, installed in `C:\cygwin64\opt\cross-mint`). It has no `make`,
so `build_atari.sh` compiles every source and links:

```bash
# Falcon 030 (not usable on a stock 16 MHz Falcon)
CPU=68030 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB030.TOS bash ./build_atari.sh

# Falcon with a 68060 accelerator
CPU=68060 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB060.TOS bash ./build_atari.sh
```

`LINKCPU=68000` is deliberate: the toolchain's 68020-60 C library assumes an
FPU, which the Falcon does not have. Code is compiled for the 030/060 but linked
against the 68000 libraries. **`OUT` must be 8.3-safe (8-character base, 3-character
extension)** - GEMDOS names are 8.3 on real hardware without FreeMiNT, and this
applies to the executable itself just as much as to the ROM set or `outrun.ini`
(see the "Long ROM filenames / FreeMiNT" note above and the `freemint` option).
**Digits in the name can confuse Hatari's `--auto` flag specifically** -
confirmed in an earlier session that `--auto CB030.TOS` can fail ("cannot find
the folder or file") while the exact same binary renamed to a letters-only
name boots fine; digit-free names work more reliably there. This build now
outputs `CB030.TOS`/`CB060.TOS` anyway (clearer than the old `CBFAL`/`CBFAST`
names), because `--auto <file>` turns out not to actually launch the game -
see the "Hatari `--auto` does not do what it looks like it does" section
below. What launches the game is EmuTOS's own `AUTO\` folder mechanism
executing whatever `.PRG` is in there (`CB030.PRG` in this project,
regardless of which CPU target its content actually is), so the digit-name
quirk in `--auto`'s argument only affects the delayed auto-typed keystroke,
never the actual game launch. `outrun.ini`'s own `fopen()` call (a normal
GEMDOS file open from inside the running program) is not affected either way.

Extra compile flags (put them in `EXTRA`):

| Flag | Effect |
|---|---|
| `-DLOWRES` | Render at half resolution (160x112, doubled on screen). Falcon only. |
| `-DNO_SOUND` | No audio and no sound-command processing (speed comparisons). |
| `-DSOUND_RATE=25033` | Mix at another DMA rate than 12517 Hz. 6258 Hz gives no sound at all on real hardware. |
| `-DFORCE_SOUND` | Synthesise sound on every logic step even when the machine is behind (measurements). |
| `-DNO_SKY` | Do not draw the sky layer (test only, leaves trails). |
| `-DHALF_SCENERY` | Hide every second scenery sprite (test only). |
| `-DAUTOPLAY` | Insert a coin, press start and hold the accelerator (measurements). |
| `-DAUTOSHOT` | With `-DAUTOPLAY`: save pictures 170 and 260 of the drive as `SHOTnnnn.PNG`. |
| `-DAUDIO_DUMP` | Append what each step hands to the DMA to `AUDIO.RAW` (signed 8-bit stereo, 12517 Hz). |
| `-DDSPFX_LOG` | Log the state of the DSP replay's effects queue to `DSPFX.TXT` every 50 steps. |
| `-DPERF_PRINT` | Append per-stage timings and `MARK` lines to `PERFLOG.TXT`. |
| `-DDUMP_FRAME` | Write frames 30 and 300 to `frame30.bin` / `frame300.bin` (RGB565). |
| `-DROWCHECK` | Check that the Falcon row cache (unchanged rows are not converted again) matches a full conversion. |
| `-DNO_ROWCACHE` | Convert every row every frame (for speed comparisons). |
| `-DROADCOPYCHECK` | Check the fast road-RAM block copies against the original word-by-word ones. |
| `-DSPRCOPYCHECK` | Same for the sprite-RAM copy. |
| `-DPCMCHECK` | Compare the assembler SegaPCM channel loop with the original C loop on live data. |
| `-DKEYTRACE` | Print every key press/release (checks the keyboard handler). |
| `-DMUSIC_RENDER` | Offline tool: with `-DMUSIC_ID=0x85 -DMUSIC_SECONDS=100`, plays one music track through the sound driver and writes `music_ym.raw` / `music_pcm.raw` (mono, signed 8 bit). Shows a black screen. With `-DMUSIC_LOGONLY` nothing is synthesised and the FM register writes go to `music_log.bin` (fast). |
| `-DCOVERCHECK` | Check that every pixel of the index buffer is written each picture (what makes the two-buffer scheme equivalent to a single buffer). |
| `-DCOVERSKIPCHECK` | Check that skipping the rows the road foreground overwrites (background fill and tile layers) gives the same picture. |
| `-DBENCH_N=400` | Time 400 pictures after the first 100 and print one line (pictures/s, steps per picture); for measuring on real hardware. |
| `-DMOVE16` | 68060 only (build with `CPU=68060`): each converted row goes through a fast-RAM buffer and is pushed to the ST-RAM screen with MOVE16 bursts. Identical picture; meant to be timed on real hardware (Hatari does not model the bus: 4348 vs 4294 units there). |
| `-DTILEDECCHECK` | Compare the assembler tile-column decode with the original C++ formula on live data. |
| `-DTILEDEC_OLD` | Use the original C++ per-column tile decode instead of the assembler one (comparison build). |
| `-DROADCHECK` | Self-check the fast road renderer against the original per-pixel walk. |

## Options file

`outrun.ini` (optional, next to `roms\`; see `outrun.ini.example`, which describes every key) sets: `shadows`,
`shadow_min_z`, `scenery`, `vscale`, `road_hres`, `cadence`, `sound`, `music`, `fm_half`, `mod`, `mod_dsp` and
`freemint` (see just above for the last one - default
`freemint=1` keeps the ROM set's original filenames and needs FreeMiNT on real hardware; `freemint=0` looks
for an 8.3-safe renamed set instead). `vscale` (50..100, default 100) is the share of the 224 lines that is computed: at 67 two lines out of three
are drawn and the third repeats the one above it; at 50 every other line. `road_hres` halves the sampling of the road surface only (dashed lines and
edges get a little chunkier; sprites, tiles and text are unaffected); measured on the emulated 030
@32MHz: +5 % pictures/s.

`mod=1` replaces the game's 3 selectable FM music tracks, plus the Last Wave
ending/high-score tune, with Amiga ProTracker `.mod` files you supply
yourself, in a `Music\` folder next to this `.ini`:
`Music\TRACK1.MOD` = Magical Sound Shower, `TRACK2.MOD` = Passing Breeze,
`TRACK3.MOD` = Splash Wave (order matches `frontend/config.cpp`'s track list),
`TRACK4.MOD` = Last Wave (plays when you actually earn a place on the high
score table - see `ohiscore.cpp`, not selectable on the music screen). A
missing file for a given track falls back to that track's normal FM music -
no files at all behaves exactly like `mod=0`. Needs `music=1` too. Last Wave
also silences whatever FM music was still playing in-game before switching to
the `.mod` track (`sound::FM_RESET`), since it interrupts mid-game rather
than only changing on the static selection screen like the other 3.
Played by a from-scratch 4-channel replay engine (`atari/modplayer.cpp`).
By default it mixes on the main CPU; set `mod_dsp=1` too (needs `mod=1`) to play
the module on the DSP56001 instead. The DSP side is the SoundTracker DSP replay
by Simplet / ABSTRACT (`dsptrack` archive on dhs.nu), used unmodified
(`src/main/atari/dsp_tracker_p56.h`); the 68k side is `dsp_replay.cpp`, fed
from a Timer A interrupt 50 times a second. It streams the sample data each
frame, so the size of the module does not matter. The music is mixed at
49170 Hz in stereo and keeps its tempo whatever the picture rate. While it
runs the DAC listens to the DSP alone, so the game's own FM + PCM mix is sent
to the DSP as an extra pair of voices rather than to the DMA. Checked in
Hatari's DSP emulation only (the samples handed to the DAC were captured and
listened to); `mod_dsp=0` (default) keeps CPU mixing.

Measured on the emulated 68060 at 32 MHz (400 pictures, old pacing): 100 = 18.3
pictures/s, 67 = 21.5 (+17 %), 50 = 23.1 (+26 %). The gain is smaller than the share of lines removed because
the conversion to 16 bits, the row comparison and the game logic do not shrink.

## Running (Hatari)

Put `CB030.TOS` (or `CB060.TOS`), `roms/` (your ROMs) and `res/` in one folder
and mount it as a GEMDOS drive:

```
hatari --machine falcon --memsize 14 --dsp none --tos tos.img \
       --harddrive <folder> --auto CB030.TOS
# 68060 with fast RAM (fastest configuration measured):
hatari --machine falcon --memsize 14 --ttram 32 --cpulevel 6 --cpuclock 32 --addr24 false ...
```

`tos.img` is EmuTOS 1.4 (bundled with Hatari). ROM loading takes a while of
emulated time (Hatari's GEMDOS drive is slow). Keys: cursors, space = accelerate,
left ctrl = brake, left alt/shift = gears, Enter = coin/start, V = viewpoint,
P = pause (shows a "PAUSE" caption), F9 = screenshot
(`SHOTnnnn.PNG` in the program's folder), Esc or F10 = quit to the desktop
(shows a confirmation: up = yes, down = cancel).

A digital joystick works too, plugged into the joystick-only DB9 port (the one
NOT shared with the mouse): left/right = steer, forward = accelerate, back =
brake, fire = shift gear (same single action as the left alt key). Read via
IKBD joystick-1 event reports (`atari_joy1` in `atari/kbd_asm.S`/`input.cpp`),
folded into the same `keys[]` array as the keyboard - both work at the same
time, nothing is disabled by having a joystick plugged in. In Hatari, map a
host joystick/gamepad or the numeric keypad to ST joystick port 1 (the
`--joystick` option or the GUI's Joysticks panel; port 1, not port 0/mouse).
The enhanced joystick ports (the two 15-pin sockets on the left side of the Falcon) are read
too, both of them, with a Jaguar pad or a plain joystick on an adapter: left/right = steer, up or B =
accelerate, down or C = brake, A (the fire button of a plain joystick) = shift gear, Option = coin/start,
Pause = pause. In Hatari these are joystick ports 2 and 3.

The keyboard is read through an interrupt handler (`kbd_asm.S`) that replaces the ACIA vector while the
game runs and restores it on exit; `-DKEYTRACE` prints every key change.

**Hatari `--auto` does not do what it looks like it does.** `--auto <file>`
does *not* launch `<file>` - it simulates typing `<file>` + Return at the
desktop **after boot**, on a timer, regardless of what is actually running at
that moment. What launches the game is EmuTOS's own, completely separate
`AUTO\` folder mechanism, which runs every `.PRG` there automatically before
the desktop even starts (this port ships one, named `CB030.PRG` for historical
reasons - its actual content is always kept in sync with the `.TOS` build).
Consequences, both confirmed this session:
* The filename passed to `--auto` is close to irrelevant *while the game is
  running* - it can be a file that does not exist at all (`--auto NOFILE.TOS`
  works fine) since nothing ever reads it until the desktop is reached.
* **The moment the game actually quits back to the desktop, those queued
  keystrokes finally fire.** If `--auto` named a file that exists (e.g. the
  same `.TOS` the game was built as), the desktop obediently reopens it -
  the game appears to "never quit" or "freeze right back up", when what
  really happened is a clean quit immediately followed by an automatic
  relaunch. This is almost certainly what made Escape/F10 look broken for
  most of this session: they were not. For a `--auto` argument that will
  actually let you *see* the desktop, use one that does not name a real file
  (`--auto NOFILE.TOS`), or just drop `--auto` in favour of the `AUTO\`
  folder alone. (A *separate*, smaller quirk also seen this session: with
  too few files in `AUTO\`, `--auto` can fail even at boot, "cannot find the
  folder or file" - keep a handful of files there, this port's release
  folder already does.)

**Quitting (Escape/F10) needs a normal `return` from `main()`, not
`Pterm()`/`exit()`.** Every variant of explicit process termination was tried
this session - `exit()`, the raw GEMDOS `Pterm()` trap, with and without
first switching back from supervisor to user mode (`SuperToUser()`), with
and without restoring the video mode, with and without any cleanup at all -
and every one of them froze the machine solid instead of returning to the
desktop, reproduced identically down to a `Pterm()` called with *nothing*
else run first. The fix: `state = STATE_QUIT` instead of calling the
termination function directly, letting `main_loop()` notice it, do its own
cleanup, and return normally out of `main()` - i.e. behave like a
well-behaved program launched from the `AUTO\` folder, before the
desktop/AES exists yet, is expected to. The working theory is that GEMDOS's
own process termination (`Pterm()`, which the C runtime's `exit()` and the
normal end of `main()` both eventually call) assumes a parent/AES context
that is not there yet at this point in boot, and only the C runtime's own
"just fall off the end of `main()`" path handles that correctly - but this
was not root-caused any further than "one of these works and the other
never does, confirmed repeatedly". `quit_func()`/`Pterm()` is kept only for
the ROM-load/video-init failure paths in `main()`, before the game loop -
i.e. before there is anything to quit *out of* yet, which apparently makes
a difference.

**IKBD hijack note**: this port's own keyboard interrupt handler
(`atari/kbd_asm.S`, installed by `Input::init()`) takes over the ACIA vector
without first telling the real IKBD to stop sending mouse/joystick packets;
`Input::init()` now sends the DISABLE MOUSE / DISABLE JOYSTICKS commands right
after installing the vector as a precaution (see `atari/input.cpp`), since a
stray packet byte in flight at that exact instant could otherwise be
misread as a raw key-down that never gets a matching key-up. Not fully
proven under real hardware timing - if a key ever seems to get stuck "down"
on its own, this is the first place to look.

## How the port works

* **Video**: the engine composes a 320x224 buffer of palette indices, then
  one table lookup per pixel converts it into 16-bit RGB565.
* **Timing (Falcon)**: game logic is decoupled from drawing. It always runs
  30 steps per second of real time (`main_atari.cpp`, fixed timestep); when
  drawing is slower, several steps run per picture. Screen memory is triple
  buffered and the frame rate is capped at 60 without waiting for the retrace.
  When more than one step runs per picture, audio synthesis is skipped (the
  YM2151 timers still advance so the game's sound logic keeps going).
* **Screen mode (Falcon)**: the mode is chosen by size, not assumed: `VgetSize()` is asked for each
  candidate and one with at least 224 lines is required (VGA: 320x240; RGB/TV: 384x240 overscan,
  else interlaced 320x400). The 224 lines are centred; if no mode is big enough the program says so
  instead of cropping the picture.
* **Sound DMA**: four buffers; the DMA registers are only rewritten once the DMA has finished the
  buffer it is playing (`Audio::service()`, also called while the main loop waits). At most two
  buffers wait; beyond that the oldest is dropped. Not listened to.
* **Index buffers (Falcon)**: `Video` keeps two index buffers and swaps them after each picture; the row cache compares the new picture with the other buffer, so no copy of the previous picture is needed.
* **Rows under the road**: the road foreground overwrites every pixel of the rows it draws, so the background fill and the two tile layers skip those rows (about 95 of 224 on average). The picture is identical; the gain measured on the emulated 060 is small (1.7 %) because most tiles sit above the horizon.
* **Memory**: `new` returns zeroed memory (TOS does not zero it; the engine
  relies on it). DMA sound and screen buffers are allocated explicitly in ST-RAM;
  everything else can live in fast RAM.
* **Assembler** (`src/main/atari/*.S`): sprite scanlines (`sprite_asm030.S` for
  030/060 with a clip-free fast path), road spans, 8x8 tiles, 16-bit conversion.

## Measured speed (Hatari, emulated time; 68060 emulation is "experimental")

Full resolution, driving (the demo driving itself), seconds per picture:

| Machine | Picture | Game speed |
|---|---|---|
| 68060 @ 32 MHz, fast RAM (CT60) | ~0.065 s (15/s) | 100 % of real time |
| Stock Falcon 030 @ 16 MHz | ~1.3 s | not usable |

Nothing here has been checked on real hardware, and sound output has not been
listened to.
