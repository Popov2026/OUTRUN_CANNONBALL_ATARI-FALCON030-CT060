/***************************************************************************
    Run-time options of the Atari port (see options.hpp).

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "atari/options.hpp"

AtariOptions atari_opt = { 1, 0, 1, 100, 0, 0, 0, 2, 1, 0, 1, 2, 0, 0, 1,
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

// vscale changed while running (atari/optmenu.cpp).
void atari_set_vscale(int vscale)
{
    atari_opt.vscale = vscale;
    build_rows(vscale);
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
        else if (!strcmp(key, "fps"))          { set_int(&atari_opt.fps, val, 0, 30); if (atari_opt.fps && atari_opt.fps < 10) atari_opt.fps = 10; }
        else if (!strcmp(key, "sound"))        set_int(&atari_opt.sound,        val, 0, 2);
        else if (!strcmp(key, "music"))        set_int(&atari_opt.music,        val, 0, 1);
        else if (!strcmp(key, "fm_half"))      set_int(&atari_opt.fm_half,      val, 0, 1);        else if (!strcmp(key, "mod"))          set_int(&atari_opt.mod,          val, 0, 1);
        else if (!strcmp(key, "sound_irq"))    set_int(&atari_opt.sound_irq,    val, 0, 2);
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

void atari_joy_name(int bits, char* out)
{
    static const struct { const char* name; int bits; } NAMES[] = {
        { "up", JOYSRC_UP }, { "down", JOYSRC_DOWN }, { "fire", JOYSRC_FIRE }, { "b", JOYSRC_B }, { "c", JOYSRC_C } };
    out[0] = 0;
    for (unsigned i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
        if (bits & NAMES[i].bits)
        {
            if (out[0]) strcat(out, "+");
            strcat(out, NAMES[i].name);
        }
    if (!out[0]) strcpy(out, "none");
}

bool atari_save_options(const char* filename)
{
    // the keys and their values, as they are written
    static const char* KEYS[] = { "shadows", "shadow_min_z", "scenery", "vscale", "road_hres", "cadence", "fps",
                                  "music", "joy_accel", "joy_brake", "joy_gear" };
    const int NK = sizeof(KEYS) / sizeof(KEYS[0]);
    char vals[NK][24];
    sprintf(vals[0], "%d", atari_opt.shadows);
    if (atari_opt.shadow_min_z) sprintf(vals[1], "0x%x", atari_opt.shadow_min_z); else strcpy(vals[1], "0");
    sprintf(vals[2], "%d", atari_opt.scenery);
    sprintf(vals[3], "%d", atari_opt.vscale);
    sprintf(vals[4], "%d", atari_opt.road_hres);
    sprintf(vals[5], "%d", atari_opt.cadence);
    sprintf(vals[6], "%d", atari_opt.fps);
    sprintf(vals[7], "%d", atari_opt.music);
    atari_joy_name(atari_opt.joy_accel, vals[8]);
    atari_joy_name(atari_opt.joy_brake, vals[9]);
    atari_joy_name(atari_opt.joy_gear, vals[10]);
    bool done[NK];
    for (int k = 0; k < NK; k++) done[k] = false;

    // the whole file in memory (a few KB), then written back
    static char in[32768];
    long n = 0;
    FILE* f = fopen(filename, "rb");
    if (f) { n = (long)fread(in, 1, sizeof(in) - 1, f); fclose(f); }
    in[n] = 0;
    static char out[32768 + 1024];
    long o = 0;
    const char* p = in;
    while (*p && o < (long)sizeof(out) - 512)
    {
        const char* e = strchr(p, '\n');
        const long len = e ? (long)(e - p) + 1 : (long)strlen(p);
        // "key = value  # comment": the key is the first word, before any comment
        const char* k = p;
        while (*k == ' ' || *k == '\t') k++;
        const char* ke = k;
        while ((*ke >= 'a' && *ke <= 'z') || (*ke >= 'A' && *ke <= 'Z') || (*ke >= '0' && *ke <= '9') || *ke == '_') ke++;
        const char* eq = ke;
        while (*eq == ' ' || *eq == '\t') eq++;
        int which = -1;
        if (*eq == '=' && ke > k)
            for (int i = 0; i < NK; i++)
                if ((long)strlen(KEYS[i]) == ke - k && !strncmp(k, KEYS[i], ke - k)) which = i;
        if (which < 0 || done[which])
        {
            memcpy(out + o, p, len); o += len;
        }
        else
        {
            done[which] = true;
            const char* v = eq + 1;
            while (*v == ' ' || *v == '\t') v++;
            const char* ve = v;
            while (*ve && *ve != ' ' && *ve != '\t' && *ve != '#' && *ve != ';' && *ve != '\r' && *ve != '\n') ve++;
            const char* after = ve;
            while (*after == ' ' || *after == '\t') after++;
            memcpy(out + o, p, v - p); o += (long)(v - p);
            const long nv = (long)strlen(vals[which]);
            memcpy(out + o, vals[which], nv); o += nv;
            const bool comment = *after == '#' || *after == ';';
            if (comment)
            {
                // keep the comment where it was when the new value fits
                long pad = (long)(after - v) - nv;
                if (pad < 1) pad = 1;
                while (pad--) out[o++] = ' ';
            }
            const long rest = len - (long)(after - p);
            memcpy(out + o, after, rest); o += rest;
        }
        p += len;
    }
    // keys not in the file yet: added at the end, with the file's own line ends
    const char* eol = (!n || strstr(in, "\r\n")) ? "\r\n" : "\n";
    if (o && out[o - 1] != '\n') o += sprintf(out + o, "%s", eol);
    for (int i = 0; i < NK; i++)
        if (!done[i]) o += sprintf(out + o, "%-12s = %s%s", KEYS[i], vals[i], eol);

    f = fopen(filename, "wb");
    if (!f) return false;
    const bool ok = fwrite(out, 1, o, f) == (size_t)o;
    return fclose(f) == 0 && ok;
}
