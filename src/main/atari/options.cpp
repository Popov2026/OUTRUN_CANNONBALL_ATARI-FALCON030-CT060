/***************************************************************************
    Run-time options of the Atari port (see options.hpp).

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "atari/options.hpp"

AtariOptions atari_opt = { 1, 0, 1, 100, 0, 0, 2, 1, 0, 1, 0, 0, 1,
                           JOYSRC_UP | JOYSRC_B, JOYSRC_DOWN | JOYSRC_C, JOYSRC_FIRE };

unsigned char g_row_draw[232];
unsigned char g_row_mask8[232];
unsigned char g_row_src[224];
int g_row_reduced = 0;
unsigned char g_row_cover[232];
unsigned char g_tile_draw[232];
unsigned char g_tile_mask8[232];
const unsigned char* g_clip_rows = g_row_draw;

// Rebuilds the row tables used by the layers drawn under the road (see options.hpp): a row is
// drawn there only if the vscale option keeps it and the road will not cover it entirely.
void atari_frame_rows(void)
{
    // g_tile_mask8[y] has bit r set when row y+r is drawn: built from the bottom with a sliding window.
    unsigned char m = 0;
    for (int y = 231; y >= 0; y--)
    {
        const unsigned char d = (y < 224 && g_row_draw[y] && !g_row_cover[y]) ? 1 : 0;
        g_tile_draw[y] = d;
        m = (unsigned char)((m << 1) | d);
        g_tile_mask8[y] = m;
    }
}

// Row r is drawn when the source line floor(r * vscale / 100) differs from the one of row r-1
// (row 0 always); the other rows show the last drawn row above them.
static void build_rows(int vscale)
{
    memset(g_row_draw, 0, sizeof(g_row_draw));
    g_row_reduced = 0;
    int last = 0;
    for (int r = 0; r < 224; r++)
    {
        const bool draw = (r == 0) || vscale >= 100 || (r * vscale / 100) != ((r - 1) * vscale / 100);
        g_row_draw[r] = draw ? 1 : 0;
        if (draw) last = r; else g_row_reduced = 1;
        g_row_src[r] = (unsigned char)last;
    }
    for (int y = 0; y < 232; y++)
    {
        unsigned char m = 0;
        for (int r = 0; r < 8; r++)
            if (y + r < 224 && g_row_draw[y + r]) m |= (unsigned char)(1 << r);
        g_row_mask8[y] = m;
    }
    memset(g_row_cover, 0, sizeof(g_row_cover));
    atari_frame_rows();
}

// Stores the number in `text` into *dst if it is readable and within lo..hi; otherwise the
// default stays.
static void set_int(int* dst, const char* text, int lo, int hi)
{
    char* end = 0;
    long v = strtol(text, &end, 0);          // decimal, 0x... hex
    if (end == text || v < lo || v > hi)
        return;                              // unreadable or out of range: keep the default
    *dst = (int)v;
}

// Stores the joystick sources named in `text` ("up+b", "fire", "none"...) into *dst. Unknown
// words are ignored; if nothing at all was recognised the default stays.
static void set_joy(int* dst, const char* text)
{
    static const struct { const char* name; int bits; } NAMES[] = {
        { "up", JOYSRC_UP }, { "down", JOYSRC_DOWN }, { "fire", JOYSRC_FIRE },
        { "b", JOYSRC_B }, { "c", JOYSRC_C }, { "none", 0 } };
    int bits = 0;
    bool any = false;
    char word[16];
    while (*text)
    {
        int n = 0;
        while (*text && (*text == ' ' || *text == '\t' || *text == '+' || *text == ',' || *text == '\r' || *text == '\n')) text++;
        while (*text && *text != ' ' && *text != '\t' && *text != '+' && *text != ',' && *text != '\r' && *text != '\n')
        {
            if (n < 15) word[n++] = (char)((*text >= 'A' && *text <= 'Z') ? *text + 32 : *text);
            text++;
        }
        word[n] = 0;
        if (!n) continue;
        for (unsigned i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
            if (!strcmp(word, NAMES[i].name)) { bits |= NAMES[i].bits; any = true; }
    }
    if (any) *dst = bits;
}

// Reads outrun.ini (see options.hpp for the keys). Unknown keys are ignored; a missing file
// leaves every default in place.
void atari_load_options(const char* filename)
{
    // filename is "outrun.ini" (see main_atari.cpp) - already 8.3-safe (6+3 characters), so
    // freemint=0 (see below) is reachable without FreeMiNT even for this file, no fallback name
    // needed.
    FILE* f = fopen(filename, "r");
    if (!f)
    {
        build_rows(atari_opt.vscale);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f))
    {
        char* c = line + strcspn(line, "#;");
        *c = 0;
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;

        // trim the key
        char* key = line;
        while (*key == ' ' || *key == '\t') key++;
        char* ke = key + strlen(key);
        while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;

        const char* val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        if      (!strcmp(key, "shadows"))      set_int(&atari_opt.shadows,      val, 0, 1);
        else if (!strcmp(key, "shadow_min_z")) set_int(&atari_opt.shadow_min_z, val, 0, 0x1ff);
        else if (!strcmp(key, "scenery"))      set_int(&atari_opt.scenery,      val, 0, 1);
        else if (!strcmp(key, "vscale"))       set_int(&atari_opt.vscale,       val, 50, 100);
        else if (!strcmp(key, "road_hres"))      set_int(&atari_opt.road_hres,    val, 0, 1);
        else if (!strcmp(key, "cadence"))      set_int(&atari_opt.cadence,      val, 0, 4);
        else if (!strcmp(key, "sound"))        set_int(&atari_opt.sound,        val, 0, 2);
        else if (!strcmp(key, "music"))        set_int(&atari_opt.music,        val, 0, 1);
        else if (!strcmp(key, "fm_half"))      set_int(&atari_opt.fm_half,      val, 0, 1);        else if (!strcmp(key, "mod"))          set_int(&atari_opt.mod,          val, 0, 1);
        else if (!strcmp(key, "fm_dsp"))       set_int(&atari_opt.fm_dsp,       val, 0, 1);
        else if (!strcmp(key, "mod_dsp"))      set_int(&atari_opt.mod_dsp,      val, 0, 2);
        else if (!strcmp(key, "freemint"))     set_int(&atari_opt.freemint,     val, 0, 1);
        else if (!strcmp(key, "joy_accel"))    set_joy(&atari_opt.joy_accel,    val);
        else if (!strcmp(key, "joy_brake"))    set_joy(&atari_opt.joy_brake,    val);
        else if (!strcmp(key, "joy_gear"))     set_joy(&atari_opt.joy_gear,     val);
    }
    fclose(f);
    build_rows(atari_opt.vscale);
}
