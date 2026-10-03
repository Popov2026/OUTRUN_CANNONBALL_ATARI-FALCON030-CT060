#pragma once

/***************************************************************************
    Run-time options of the Atari port, read from "outrun.ini" in the
    program's folder (same folder as roms\).  A missing file, a missing key or
    an unreadable value leaves the default.  Lines look like  key=value ;
    everything after '#' or ';' on a line is a comment.

      shadows       1   draw the shadows under cars and scenery (0 = no shadows)
      shadow_min_z  0   with shadows on: only objects whose distance value is at least
                        this get a shadow (0 = all, 0x100 = only the nearest ones)
      scenery       1   0 = no roadside scenery (trees, signs, buildings, rocks...): only the road,
                        the traffic and the player's car are left, with the sky and the HUD. For
                        slower machines. The objects are not just hidden: the engine does not
                        create them (engine/osprites.cpp), so there is nothing to crash into
                        beside the road either. The start line (banner, lights) is kept.
      vscale      100 lines drawn, in percent of the 224 (100 = all; 67 = two lines out of three are
                        computed and the third repeats the one above; 50 = every other line)
      road_hres     0   0 = full-detail road texture (dashes, edges); 1 = half the columns are
                        sampled and duplicated in pairs (the road surface only; sprites, tiles, text
                        are unaffected)
      cadence       0   pictures: 0 = automatic, 1..4 = one picture every K game steps
      sound         2   0 = no sound synthesis, 1 = always, 2 = when the machine has time
      music         1   1 = play the FM music tracks, 0 = skip them (sound effects unaffected)
      fm_half       0   1 = the FM chip (YM2151) is synthesised at half the mixing rate (6258 Hz
                        instead of 12517 Hz) and the missing samples are interpolated. Measured
                        on the emulated 68060 with the FM music playing: 13.5 ms per game step
                        instead of 18.6 (not half: the envelopes cost the same either way, only the
                        channel synthesis is halved). The price is the top octave of the FM sound (nothing above
                        ~3 kHz), so the music is duller. Sound effects on the PCM chip and .mod
                        music are not affected. Only read at start-up.
      mod           0   1 = replace the 3 selectable music tracks, plus the Last Wave
                        ending/high-score tune, with .mod files loaded from a "Music" folder next
                        to this .ini (Music\TRACK1.MOD = Magical Sound Shower, Music\TRACK2.MOD =
                        Passing Breeze, Music\TRACK3.MOD = Splash Wave, Music\TRACK4.MOD = Last
                        Wave - the user supplies these files; none are included). A missing file
                        falls back to the normal FM music for that track. 0 = never touch the .mod
                        player (default; needs the "music" key above set to 1 too).
      mod_dsp        0   2 = play the .mod files with DSPMOD 3.4 (bITmASTER of TCE, atari/dspmod.hpp):
                        its own 68k + DSP replay; the game's FM + PCM sound goes through two of
                        its fx voices. 4-voice modules ("M.K.", "M!K!", "FLT4"); others fall back
                        to CPU mixing.
                         1 = play the .mod files (see "mod" above) on the DSP56001 instead of mixing
                        them on the CPU: the DSP side is the SoundTracker replay by Simplet /
                        ABSTRACT (from dhs.nu), fed from a Timer A interrupt - see
                        atari/dsp_replay.hpp. The music is mixed at 49170 Hz in stereo and keeps
                        its tempo whatever the picture rate; the game's own FM and sound effects
                        go through the DSP too (the DAC listens to it alone while this is on).
                        Needs "mod=1"; falls back to CPU mixing if the DSP does not answer or the
                        module has more than 4 channels. Checked in Hatari's DSP emulation only.
      freemint      1   1 = ROM set keeps its original filenames (e.g. "epr-10380b.133") - needs
                        FreeMiNT or another long-filename-capable kernel/filesystem on real hardware.
                        0 = look for the 8.3-safe renamed set instead (see README_ATARI.md for the
                        full old->new table); no long-filename support needed at all ("outrun.ini"
                        itself is already 8.3-safe, 6+3 characters).
      joy_accel  up+b   what accelerates, joy_brake  down+c  what brakes, joy_gear  fire  what
                        changes gear. Each takes one or more of these, joined with '+':
                          up, down  the stick (DB9 joysticks and the 15-pin ports)
                          fire      the fire button of a DB9 joystick, button A of a 15-pin pad
                          b, c      buttons B and C of a Jaguar pad (15-pin ports)
                          none      nothing
                        e.g. joy_accel = fire, joy_brake = down, joy_gear = up. The keyboard
                        keys (space, left ctrl, left alt/shift) always work as well.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

struct AtariOptions
{
    int shadows;
    int shadow_min_z;
    int scenery;
    int vscale;
    int road_hres;
    int cadence;
    int sound;
    int music;
    int fm_half;
    int mod;
    int mod_dsp;
    int freemint;
    int joy_accel, joy_brake, joy_gear;   // JOYSRC_* bits
};

// Joystick sources the joy_* options can name.
enum { JOYSRC_UP = 1, JOYSRC_DOWN = 2, JOYSRC_FIRE = 4, JOYSRC_B = 8, JOYSRC_C = 16 };

extern AtariOptions atari_opt;

void atari_load_options(const char* filename);

// Rows drawn per picture (see vscale).  Filled by atari_load_options().
extern unsigned char g_row_draw[232];   // 1: this row is drawn (padded past the end with 0)
extern unsigned char g_row_mask8[232];  // bit r set: row y+r is drawn (the 8 rows of a tile starting at y)
extern unsigned char g_row_src[224];    // the drawn row whose content the picture shows at this row
extern int g_row_reduced;               // 0 when every row is drawn

// Per picture: the rows that the road foreground overwrites completely.  Everything drawn under the
// road on such a row (background fill, both tile layers) is invisible, so those rows are skipped there.
extern unsigned char g_row_cover[232];  // 1: the road foreground will overwrite this whole row
extern unsigned char g_tile_draw[232];  // g_row_draw and not covered: rows the layers under the road draw
extern unsigned char g_tile_mask8[232]; // the same, as 8-row masks like g_row_mask8
extern const unsigned char* g_clip_rows; // row table used by atari_tile8_clip (g_tile_draw or g_row_draw)
void atari_frame_rows(void);            // rebuilds g_tile_draw / g_tile_mask8 from g_row_draw and g_row_cover
