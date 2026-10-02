# Atari port files — what each one does

This document only covers the code written or modified for the Atari port
(Mega STE + Falcon 030/060). The rest of the Cannonball engine (`src/main/engine/`,
`src/main/frontend/`, etc., apart from the hooks listed at the bottom of this page) is
the project's original code and is not documented here. All credit for the engine,
the OutRun reverse engineering and Cannonball itself goes to
**Chris White** (project creator, [github.com/djyt/cannonball](https://github.com/djyt/cannonball)),
without whom this Atari port would simply have nothing to start from. Thanks to him.
Non-commercial licence: see `docs/license.txt` — any redistribution of a derivative work
(such as this port) must include the complete source code.

## Build scripts (root)

| File | Role |
|---|---|
| `build_atari.sh` | Main build script. Compiles every `.cpp`/`.S` listed in `Makefile.atari` with `-mcpu=$CPU`, then links with `-mcpu=${LINKCPU:-$CPU}`. **`LINKCPU` must be `68000`** even for a 68030/68060 build: the cross compiler picks a different libc/libstdc++ set (`multilib`) depending on the target CPU at link time, and only the base multilib matches the generic `crt0.o` supplied — a mismatch crashes the binary right after `Pexec`, before `main()` is even reached. See `README_ATARI.md`. |
| `build_release.sh` | Builds the two clean release binaries (no debug instrumentation): `CB030.TOS` and `CB060.TOS` (8.3 names showing the target CPU). Digits in the name can upset Hatari's own `--auto` argument (`hatari --auto CB030.TOS` may fail with "cannot find the folder or file"), but this does not affect how the game is actually launched, which always goes through the native `AUTO\` folder (`CB030.PRG`) and not through `--auto`; see `README_ATARI.md`. |
| `Makefile.atari` | List of source files compiled for the Atari target (read by `build_atari.sh` via `grep`). |

## `src/main/main_atari.cpp`

Program entry point for this target, replaces `main.cpp` (SDL2, not compiled here).
Contains:
- `operator new/delete`: unlike a desktop OS, TOS does not hand out zeroed memory
  pages; zero-fill is forced explicitly.
- `tick()`: one frame of game logic (equivalent to the body of the SDL2 loop).
- Three variants of `main_loop()` depending on the build flags:
  - `PLATFORM_FALCON` without `OLD_PACING` (the one used in practice):
    fixed-rate loop, 30 logic steps/s, one picture drawn every
    K steps (K auto-adjusted to stay within the time budget — see the comment
    at the top of the function).
  - `PLATFORM_FALCON` + `OLD_PACING`: older version of the loop,
    kept for comparison.
  - Without `PLATFORM_FALCON` (Mega STE): simple loop locked to the VBL.
- `main()`: boot sequence (supervisor mode, Mega STE detection via the
  machine cookie, loading config/options/ROMs, video/sound/input init,
  straight into the game — no frontend menu in this port).
- Diagnostic aids enabled by build flags (none is active by default):
  `STARTUP_DEBUG` (checkpoints printed at each boot step), `EARLY_RETURN_TEST`
  (returns from `main()` immediately, to tell a crash in the application code
  from a crash in the C runtime before `main()`), `BENCH_N=<n>` (measures the
  frame rate over n pictures and prints a `BENCH pictures=...` line),
  `KEYTRACE` (see `atari/input.cpp`), `MUSIC_RENDER` (offline tool that replays
  a sound command and writes the result to `.raw` files).

## `src/main/atari/` — platform-specific backends

| File | Role |
|---|---|
| `input.cpp` / `.hpp` | Keyboard, IKBD joystick and enhanced joystick ports (`read_ext_ports()`, v0.27). An interrupt handler (`kbd_asm.S`) replaces the keyboard ACIA vector to keep a key-down state table without blocking. `poll()` copies that state into `keys[]` through a fixed scancode mapping. `frame_done()` saves `keys_old` **before** reading the current state again — the reverse order (fixed) prevented `has_pressed()` from detecting a rising edge. |
| `kbd_asm.S` | Low-level keyboard interrupt: reads bytes from the ACIA, updates `atari_scan[scancode]` (1 = pressed / 0 = released), skips mouse/clock packets by their length and records joystick-1 reports. |
| `options.cpp` / `.hpp` | Reads `outrun.ini` (next to `roms\` — name chosen on purpose to stay 8.3: 6+3 characters, see `freemint` below): shadows, draw distance, `cadence`, `sound`, `music` (0/1, turns off the FM music without touching the sound effects), `mod`/`mod_dsp`, `freemint` (0/1, default 1: keeps the original ROM names, needs FreeMiNT on real hardware; 0 = expects a ROM set renamed to 8.3, see `romloader.cpp` and the table in `README_ATARI.md`), and `road_hres` (see below — **must stay 0 by default**, it is a visually risky optimisation not yet validated). |
| `src/main/romloader.cpp` | `RomLoader::load_rom()`: when `atari_opt.freemint==0`, remaps the ROM names (`epr-10380b.133` → `E10380b.133`) before opening the file — see `atari_short_name()`. By default (`freemint=1`) the original names are used as they are. |
| `timer.cpp` / `.hpp` | Pacing based on the 200 Hz system counter (`_hz_200`, address 0x4BA) rather than on `SDL_GetTicks`. `frame_pace()` caps at 60 Hz without ever catching up on a delay with a burst. |
| `audio.cpp` / `.hpp` | STE DMA sound backend — takes the buffers already synthesised by the engine (YM2151 + SegaPCM, untouched), mixes them with the `.mod` player if active (`-DMOD_MUSIC`), and pushes the result to the STE DMA. |
| `modplayer.cpp` / `.hpp` | `.mod` file player (Amiga ProTracker, 4 channels) that plays the game music instead of the FM chip when `mod=1` (`Music\TRACK1.MOD` to `TRACK4.MOD`, supplied by the user). Detects both header variants (15 and 31 samples), handles the common effects (arpeggio, portamento, vibrato, volume slide, position jump, tempo). Mixed on the CPU by default. With `mod_dsp=1`, the module is played by the DSP (`dsp_replay.*`): the replay interrupt advances the pattern and reads the samples, and `mix()` then does nothing. |
| `dsp_replay.cpp` / `.hpp` / `dsp_replay_asm.S` / `dsp_tracker_p56.h` | `.mod` playback by the DSP56001 (`mod_dsp` option). The DSP side is the SoundTracker replay by Simplet / ABSTRACT (`dsptrack` archive on dhs.nu), unmodified (`dsp_tracker_p56.h`). The 68k side runs from a 50 Hz Timer A interrupt: each frame it sends the DSP the volume and pitch of each voice, then the sample bytes requested (nothing is stored on the DSP, so there is no limit on module size). Six voices: the module's 4, plus a stereo pair carrying the game's FM + PCM mix, since the DAC only listens to the DSP while this mode is active. Checked under Hatari `--dsp emu` (capture of the samples sent to the DAC). |
| `gemredraw.cpp` | Asks the AES to redraw the whole screen when the game exits (only called under MiNT, i.e. under a multitasking AES such as XaAES, which does not redraw the desktop by itself). |
| `screenshot.cpp` / `.hpp` | F9 screenshot, saved as `SHOTnnnn.PNG` in the program's folder. |
| `video.cpp` / `.hpp` | Mega STE video backend (16 colours, histogram reduction + chunky-to-planar conversion). The engine composes a palette-index picture in a shared buffer (`src/main/video.cpp`, unmodified); this file converts it for the real hardware. |
| `video_falcon.cpp` | Falcon video backend: 16-bit true colour (RGB565), a single 65536-colour lookup table, no palette reduction or bitplane packing (unlike the STE). |
| `road_asm.S` | Inner loops of the road renderer (`atari_fill16`, `atari_road_copy1`, `atari_road_copy2` + `_half` variants for `road_hres`). Called from `hwvideo/hwroad.cpp`. |
| `sprite_asm.S` | 68000 version of the sprite rendering inner loop (one line of one sprite, horizontal zoom). |
| `sprite_asm030.S` | Same contract as `sprite_asm.S`, with a fast path for 68030/68060 (chosen automatically according to the target CPU). |
| `tile_asm.S` | Draws an 8x8 tile (4 bits/pixel, 0 = transparent) into the index buffer. |
| `truecolor_asm.S` | Final index → RGB565 conversion (`dst[i] = pal[px[i]]`), Falcon-specific. |
| `pcm_asm.S` | Inner loop of one SegaPCM channel (sample fetch, volume, looping). |
| `video_asm.S` | Palette histogram + chunky-to-planar conversion, pure 68000 (no 32-bit multiply, no 020+ instructions) for the STE. |

## Hooks in the shared engine (`#ifdef PLATFORM_ATARI` / `PLATFORM_FALCON`)

These files belong to the original Cannonball engine; only the sections listed
were touched for the port.

| File | What the Atari section does |
|---|---|
| `src/main/frontend/config.cpp` | `PLATFORM_ATARI` branch of `Config::load()`: no Boost/XML available, applies the default values directly in code (see also the `ostats.cpp`/`save_tiletrial_scores` skip). `data.rom_path="roms/"`, `data.crc32=0` (an `opendir`+CRC32 hangs forever under this toolchain + emulated GEMDOS, replaced by a direct `fopen` by name). |
| `src/main/engine/oinputs.hpp` | Redefines how analogue inputs are read for this target (digital controls only, no physical wheel/pedals). |
| `src/main/engine/oroad.cpp` | Calls the assembler road routines (`atari_road_copy*`) instead of the generic C++ when `PLATFORM_ATARI` is defined. |
| `src/main/engine/osprites.cpp` | Calls `atari_sprite_line` instead of the generic C++ loop. `finalise_sprites()` also has its own fine-grained timing (`dosprite`/`blit`/`trafficlogic`/`trafficsnd`) under `-DPERF_PRINT`; `-DLOGIC50_FILE` also writes it to `SPR.TXT` with the number of active sprites. Finding from this work: `nsprites` jumps from ~4-22 (level 1) to 63-64 (level 2, the area reported as slow) — confirms "too many sprites" without yet pinning down which loop in `sprite_copy()` (beyond what `dosprite`/`blit` already measure) dominates the cost. |
| `src/main/hwvideo/hwroad.cpp` | Switches between `atari_road_copy1/2` and their `_half` variants according to `atari_opt.road_hres` (see table above). |
| `src/main/hwvideo/hwsprites.cpp` | Calls the assembler sprite routines (`sprite_asm.S`/`sprite_asm030.S`). |
| `src/main/hwvideo/hwtiles.cpp` | Calls `atari_tile8` (Falcon only) instead of the generic C++ renderer. |
| `src/main/hwaudio/segapcm.cpp` | Calls `atari_pcm_channel` instead of the generic C++ loop for each PCM channel. Also contains the `-DPERF_PRINT -DPCM_MEASURE_FILE` per-channel cost measurement (written to `PCMLOG.TXT`, not to the console — see the methodology note below). |
| `src/main/engine/omusic.cpp` | `play_music()`: in a `-DMOD_MUSIC` build, starts the `.mod` player instead of the YM2151 command for the 3 selectable tracks (not Last Wave, which is triggered elsewhere). Otherwise unchanged (normal YM2151 command), with the `atari_opt.music` check to turn off the music without touching the sound effects. |
| `src/main/engine/ostats.cpp` | `OStats::init()`: `-DFORCE_CREDIT_TEST` test aid (credit=1 at boot, never in a normal build) — used because synthetic key injection in Hatari never proved reliable for automatically validating a screen that needs a credit. |
| `src/main/main.hpp` | Shared declarations specific to the Atari target (types, macros). |
| `src/main/engine/outrun.cpp` | `jump_table()`: adds `-DLOGIC50_FILE` (with `-DPERF_PRINT`), which writes the cost breakdown (switch/inputs/sprites/objects/traffic/ferrari/crash/copy) to `LOGIC.TXT`, correlated with the stage and the position on the track — used to investigate the slowdown reported in the level 2 tunnel. |
| `src/main/video.cpp` | Hands the index buffer composed by the engine to the `atari/video.cpp` or `atari/video_falcon.cpp` backend depending on the target. |

## Documentation

| File | Role |
|---|---|
| `README.md` | Overview: installation, controls, options, building, performance. |
| `README_ATARI.md` | Exact build commands (including the `LINKCPU` trap), build flags (`-DBENCH_N`, `-DMOVE16`, `-DCOVERSKIPCHECK`, etc.), Hatari launch commands, known measurements. |
| `DSP_NOTES.md` | Notes on the DSP56001 work (home-made assembler encoder, host-port round-trip check, `jclr_reg` bug found and fixed) — unrelated to the video/road rendering above. |
| `ATARI_PORT_FILES.md` | This file. |

## Bugs fixed (already applied in the delivered sources)

1. **`input.cpp` — `frame_done()`**: the order `poll()` then copy into
   `keys_old` made `keys_old` always equal to `keys`, so
   `has_pressed()` (rising edge) could never return true — the
   Return key (credit/start) did nothing. Fixed by saving
   `keys_old` **before** `poll()`, as the SDL2 reference version does
   (`src/main/sdl2/input.cpp`).
2. **`outrun.ini` shipped with `road_hres=1` by default** — this setting
   enables an assembler path (`atari_road_copy*_half`) not yet validated
   visually, which caused a distorted road reported in testing.
   Set back to `road_hres=0` by default; only re-enable it after explicit
   visual validation.
3. **Very poor/choppy sound** — `sound=2` (default) turns off all audio
   synthesis as soon as the CPU is loaded (see `atari/options.hpp`); since the game
   is often CPU-bound, the sound was cut most of the time. `sound=1`
   in `outrun.ini` forces continuous synthesis (trade-off: slightly
   more CPU load in scenes that are already heavy).

## Methodology note: console output capture is unreliable

Several diagnostics showed that the game's console output
(`printf`/`Cconws`) redirected to a host file via `hatari ... > out.log`
**does not arrive reliably while the process is running** — neither
`fflush()` nor even killing the process made it appear in several tests,
whereas the same line does end up appearing after a clean exit in
other cases (mechanism not fully understood). For any future diagnostic
that must read a result while the game is running: **write to a
file on the mounted drive** (`fopen(...,"a")` + `fclose()`, which forces a
real GEMDOS `Fwrite`/`Fclose`) rather than to the console — reliable in every
test, unlike stdout redirection. See
`PCM_MEASURE_FILE` in `hwaudio/segapcm.cpp` for an example.

Another pitfall encountered: some diagnostic builds failed to load
the ROMs (`cannot open rom`) even though the files were present and
identical to a deployment that worked — the exact cause was not
isolated (not a problem with the file contents), but duplicating `roms/`,
`res/` and `outrun.ini` **both at the root of the mounted GEMDOS drive
and in `AUTO\`** solved the problem every time it occurred.
