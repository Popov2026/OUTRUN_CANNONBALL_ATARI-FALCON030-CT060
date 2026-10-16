/***************************************************************************
    Options menu of the Atari port - see optmenu.hpp.

    The background (logo, road) is set up and moved as in Cannonball's
    frontend/menu.cpp (Menu::init(), Menu::tick_ui()).

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstring>
#include "main.hpp"
#include "globals.hpp"
#include "frontend/config.hpp"
#include "engine/outrun.hpp"
#include "engine/ohud.hpp"
#include "engine/oinitengine.hpp"
#include "engine/oaddresses.hpp"
#include "romloader.hpp"
#include "roms.hpp"
#include "engine/ologo.hpp"
#include "engine/opalette.hpp"
#include "engine/oroad.hpp"
#include "engine/osprites.hpp"
#include "engine/otiles.hpp"
#include "engine/audio/osoundint.hpp"
#include "engine/audio/commands.hpp"
#include "atari/optmenu.hpp"
#include "atari/options.hpp"
#include "atari/input.hpp"
#include "atari/modplayer.hpp"

OptMenu optmenu;

static const int16_t  LOGO_Y       = -60;     // as frontend/menu.cpp
static const uint16_t HORIZON_DEST = 0x3A0;
static const int      ROAD_SPEED   = 50;      // config.menu.road_scroll_speed's default

// first page: PLAY, OPTIONS; the options page:
enum { L_PICTURES, L_LINES, L_ROAD, L_SCENERY, L_SHADOWS, L_MUSIC, L_ACCEL, L_BRAKE, L_GEAR, L_SAVE, L_BACK, NLINES };
static const char* LABELS[NLINES] = {
    "PICTURES PER SEC", "LINES DRAWN", "ROAD DETAIL", "SCENERY", "SHADOWS", "MUSIC",
    "JOY ACCELERATE", "JOY BRAKE", "JOY GEAR CHANGE", "SAVE SETTINGS", "BACK" };
static const char* MAIN_LABELS[2] = { "PLAY", "OPTIONS" };

// Picture rate: cadence (game steps per picture) or a steady fps (see options.hpp).
static const struct { const char* name; int cadence, fps; } RATES[] = {
    { "AUTO", 0, 0 }, { "30", 1, 0 }, { "25", 0, 25 }, { "20", 0, 20 }, { "15", 2, 0 }, { "10", 3, 0 }, { "7.5", 4, 0 } };
static const int NRATES = sizeof(RATES) / sizeof(RATES[0]);
static const int VSCALES[] = { 100, 75, 67, 50 };
static const int NVSCALES = sizeof(VSCALES) / sizeof(VSCALES[0]);
// Joystick sources for each action (atari/options.hpp JOYSRC_*), the default first.
static const int ACCELS[] = { JOYSRC_UP | JOYSRC_B, JOYSRC_UP, JOYSRC_B, JOYSRC_FIRE, JOYSRC_UP | JOYSRC_FIRE, JOYSRC_C };
static const int BRAKES[] = { JOYSRC_DOWN | JOYSRC_C, JOYSRC_DOWN, JOYSRC_C, JOYSRC_B, JOYSRC_DOWN | JOYSRC_B };
static const int GEARS[]  = { JOYSRC_FIRE, JOYSRC_B, JOYSRC_C, JOYSRC_UP, JOYSRC_DOWN, 0 };

// Index of v in a list, or -1.
static int find(const int* list, int n, int v)
{
    for (int i = 0; i < n; i++) if (list[i] == v) return i;
    return -1;
}

// Next entry of a list of n, from index i (-1: a value not in the list), in direction dir.
static int step(int i, int n, int dir)
{
    if (i < 0) return 0;
    return (i + dir + n) % n;
}

static int rate_index()
{
    for (int i = 0; i < NRATES; i++)
        if (atari_opt.fps == RATES[i].fps && (atari_opt.fps || atari_opt.cadence == RATES[i].cadence)) return i;
    return -1;
}

void OptMenu::init()
{
    // the game's own sound and a .mod still playing are stopped (F1 during the game)
    modplayer.stop_all();

    outrun.select_course(false, config.engine.prototype != 0);
    video.enabled = true;
    video.sprite_layer->set_x_clip(false);
    video.sprite_layer->reset();
    video.clear_text_ram();
    video.tile_layer->restore_tiles();
    ologo.enable(LOGO_Y);

    // palette, road and colours for the background, as frontend/menu.cpp
    oroad.stage_lookup_off = 9;
    oinitengine.init_road_seg_master();
    opalette.setup_sky_palette();
    opalette.setup_ground_color();
    opalette.setup_road_centre();
    opalette.setup_road_stripes();
    opalette.setup_road_side();
    opalette.setup_road_colour();
    otiles.setup_palette_hud();

    oroad.init();
    oroad.road_ctrl = ORoad::ROAD_R0;
    oroad.horizon_set = 1;
    oroad.horizon_base = HORIZON_DEST + 0x100;
    oinitengine.rd_split_state = OInitEngine::SPLIT_NONE;
    oinitengine.car_increment = 0;
    oinitengine.change_width = 0;
    outrun.game_state = GS_INIT;

    // the sound chips, for the beeps
    osoundint.has_booted = true;
    osoundint.init();

    frame = 0;
    message = 0;
    page = 0;
    main_cursor = 0;   // on PLAY
    last_dirs = ~0;   // keys still down from the game do nothing until released
}

// Changes a line's value (dir -1 / +1); the change takes effect at once.
void OptMenu::change(int line, int dir)
{
    switch (line)
    {
        case L_PICTURES:
        {
            const int i = step(rate_index(), NRATES, dir);
            atari_opt.cadence = RATES[i].cadence;
            atari_opt.fps = RATES[i].fps;
            break;
        }
        case L_LINES:
            atari_set_vscale(VSCALES[step(find(VSCALES, NVSCALES, atari_opt.vscale), NVSCALES, dir)]);
            break;
        case L_ROAD:    atari_opt.road_hres = !atari_opt.road_hres; break;
        case L_SCENERY: atari_opt.scenery = !atari_opt.scenery; break;
        case L_SHADOWS:
        {
            // all, near ones only, none
            int i = !atari_opt.shadows ? 2 : (atari_opt.shadow_min_z ? 1 : 0);
            i = (i + dir + 3) % 3;
            atari_opt.shadows = i != 2;
            atari_opt.shadow_min_z = i == 1 ? 0x100 : 0;
            break;
        }
        case L_MUSIC:   atari_opt.music = !atari_opt.music; break;
        case L_ACCEL:
            atari_opt.joy_accel = ACCELS[step(find(ACCELS, 6, atari_opt.joy_accel), 6, dir)];
            break;
        case L_BRAKE:
            atari_opt.joy_brake = BRAKES[step(find(BRAKES, 5, atari_opt.joy_brake), 5, dir)];
            break;
        case L_GEAR:
            atari_opt.joy_gear = GEARS[step(find(GEARS, 6, atari_opt.joy_gear), 6, dir)];
            break;
    }
}

static void upper(char* s)
{
    for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 0x20;
}

void OptMenu::draw()
{
    video.clear_text_ram();
    if (page == 0)
    {
        for (int i = 0; i < 2; i++)
        {
            const int x = 20 - (int)strlen(MAIN_LABELS[i]) / 2, y = 18 + 2 * i;
            ohud.blit_text_new(x, y, MAIN_LABELS[i], i == main_cursor ? ohud.PINK : ohud.GREEN);
            if (i == main_cursor)
                video.write_text32(ohud.translate(x - 3, y), roms.rom0.read32(TILES_MINICARS1));
        }
        return;
    }
    const int y0 = 14;
    for (int i = 0; i < NLINES; i++)
    {
        char value[24] = "";
        switch (i)
        {
            case L_PICTURES:
            {
                const int r = rate_index();
                if (r >= 0) strcpy(value, RATES[r].name);
                else sprintf(value, "%d", atari_opt.fps ? atari_opt.fps : 30 / (atari_opt.cadence ? atari_opt.cadence : 1));
                break;
            }
            case L_LINES:   sprintf(value, "%d", atari_opt.vscale); strcat(value, " PERCENT"); break;
            case L_ROAD:    strcpy(value, atari_opt.road_hres ? "HALF" : "FULL"); break;
            case L_SCENERY: strcpy(value, atari_opt.scenery ? "ON" : "OFF"); break;
            case L_SHADOWS: strcpy(value, !atari_opt.shadows ? "OFF" : (atari_opt.shadow_min_z ? "NEAR" : "ALL")); break;
            case L_MUSIC:   strcpy(value, atari_opt.music ? "ON" : "OFF"); break;
            case L_ACCEL:   atari_joy_name(atari_opt.joy_accel, value); break;
            case L_BRAKE:   atari_joy_name(atari_opt.joy_brake, value); break;
            case L_GEAR:    atari_joy_name(atari_opt.joy_gear, value); break;
        }
        upper(value);
        const uint16_t col = i == cursor ? ohud.PINK : ohud.GREEN;
        const int y = y0 + i + (i >= L_SAVE ? 1 : 0);   // a blank line before SAVE / BACK
        if (i >= L_SAVE)
            ohud.blit_text_new(20 - (int)strlen(LABELS[i]) / 2, y, LABELS[i], col);
        else
        {
            ohud.blit_text_new(3, y, LABELS[i], col);
            ohud.blit_text_new(22, y, value, col);
        }
        // the cursor: the HUD's mini car, as in frontend/menu.cpp (2 characters wide)
        if (i == cursor)
            video.write_text32(ohud.translate(i >= L_SAVE ? 20 - (int)strlen(LABELS[i]) / 2 - 3 : 0, y), roms.rom0.read32(TILES_MINICARS1));
    }
    if (message)
        ohud.blit_text_new(saved_ok ? 13 : 10, 27, saved_ok ? "SETTINGS SAVED" : "OUTRUN.INI NOT WRITTEN", ohud.GREY);
}

bool OptMenu::tick()
{
    frame++;
    if (message) message--;

    // edges of up, down, left, right, select (keyboard and joystick)
    const int js = input.joy_src;
    const int dirs = (input.is_pressed(Input::UP)    || (js & JOYSRC_UP)   ? 1 : 0)
                   | (input.is_pressed(Input::DOWN)  || (js & JOYSRC_DOWN) ? 2 : 0)
                   | (input.is_pressed(Input::LEFT)  ? 4 : 0)
                   | (input.is_pressed(Input::RIGHT) ? 8 : 0)
                   | (input.is_pressed(Input::START) || (js & JOYSRC_FIRE) ? 16 : 0);   // Enter, fire, pad A
    int scripted = 0;
#ifdef MENU_SCRIPT
    // Test aid: presses keys by itself - OPTIONS, 30 pictures/s, 75 % of the lines, shadows near
    // only, SAVE, BACK, then PLAY.
    {
        static const struct { int frame, bits; } KEYS[] = {
            { 40, 2 }, { 50, 16 },
            { 60, 8 }, { 70, 2 }, { 80, 8 }, { 90, 2 }, { 100, 2 }, { 110, 2 }, { 120, 8 },
            { 130, 2 }, { 140, 2 }, { 150, 2 }, { 160, 2 }, { 170, 2 }, { 180, 16 }, { 230, 2 }, { 240, 16 },
            { 280, 16 } };
        for (unsigned k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++) if (KEYS[k].frame == frame) scripted = KEYS[k].bits;
        extern bool g_take_screenshot;
        if (frame == 30 || frame == 200 || frame == 260) g_take_screenshot = true;
    }
#endif
    const int hit = (dirs | scripted) & ~last_dirs;
    last_dirs = dirs;

    bool play = false;
    if (page == 0)
    {
        // PLAY / OPTIONS
        if (hit & 3) { main_cursor ^= 1; osoundint.queue_sound(sound::BEEP1); }
        if (hit & 16)
        {
            if (main_cursor == 0) play = true;
            else { page = 1; cursor = 0; osoundint.queue_sound(sound::BEEP1); }
        }
    }
    else
    {
    if (hit & 1) { cursor = (cursor + NLINES - 1) % NLINES; osoundint.queue_sound(sound::BEEP1); }
    if (hit & 2) { cursor = (cursor + 1) % NLINES; osoundint.queue_sound(sound::BEEP1); }
    if ((hit & 12) && cursor < L_SAVE) { change(cursor, (hit & 4) ? -1 : 1); osoundint.queue_sound(sound::BEEP1); }
    if (hit & 16)
    {
        if (cursor == L_SAVE)
        {
            saved_ok = atari_save_options("outrun.ini");
            message = 90;
            osoundint.queue_sound(sound::BEEP1);
        }
        else if (cursor == L_BACK)
        {
            page = 0;
            main_cursor = 0;   // back on PLAY
            osoundint.queue_sound(sound::BEEP1);
        }
        else
        {
            change(cursor, 1);
            osoundint.queue_sound(sound::BEEP1);
        }
    }
    }

    draw();

    // move the horizon in, then scroll the road (frontend/menu.cpp, at 30 steps a second)
    if (oroad.horizon_base > HORIZON_DEST)
    {
        oroad.horizon_base -= 2;
        if (oroad.horizon_base < HORIZON_DEST)
            oroad.horizon_base = HORIZON_DEST;
    }
    else
    {
        const uint32_t speed = ROAD_SPEED << 1;
        if (oinitengine.car_increment < speed << 16)
            oinitengine.car_increment += (1 << 14);
        if (oinitengine.car_increment > speed << 16)
            oinitengine.car_increment = speed << 16;
        const uint32_t result = 0x12F * (oinitengine.car_increment >> 16);
        oroad.road_pos_change = result;
        oroad.road_pos += result;
        if (oroad.road_pos >> 16 > ROAD_END)
            oroad.road_pos = 0;
        oinitengine.update_road();
        oinitengine.set_granular_position();
        oroad.road_width_bak = oroad.road_width >> 16;
        oroad.car_x_bak = -oroad.road_width_bak;
        oinitengine.car_x_pos = oroad.car_x_bak;
    }
    ologo.tick();
    osprites.sprite_copy();
    osprites.update_sprites();
    oroad.tick();
    return play;
}
