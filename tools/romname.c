/***************************************************************************
    ROMNAME.TOS - renames the OutRun ROM files in roms\ to the long names
    (epr-10380b.133, for FreeMiNT: outrun.ini freemint = 1) or to the short
    8.3 names (E10380b.133, for plain TOS: freemint = 0), and sets the
    freemint line of outrun.ini to match.

    Put it next to CB030.TOS / CB060.TOS and run it from there. Each ROM is
    recognised by its contents (CRC32), whatever its name is now - even the
    truncated alias plain TOS shows for a long name copied from a PC
    (EPR-10~1.133). Long names can only be created under FreeMiNT.

    Build: m68k-atari-mint-gcc -O2 -mcpu=68000 tools/romname.c -o ROMNAME.TOS

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

struct Rom { const char* name; unsigned long crc; int revb; };

/* The names and CRC32 of src/main/roms.cpp: revision B, then the Japanese version, then the
   fixed sound ROM some sets have. */
static const struct Rom ROMS[] = {
    { "epr-10380b.133", 0x1f6cadadUL, 1 }, { "epr-10382b.118", 0xc4c3fa1aUL, 1 },
    { "epr-10381b.132", 0xbe8c412bUL, 1 }, { "epr-10383b.117", 0x10a2014aUL, 1 },
    { "epr-10327a.76",  0xe28a5bafUL, 1 }, { "epr-10329a.58",  0xda131c81UL, 1 },
    { "epr-10328a.75",  0xd5ec5e5dUL, 1 }, { "epr-10330a.57",  0xba9ec82aUL, 1 },
    { "opr-10268.99",   0x95344b04UL, 1 }, { "opr-10232.102",  0x776ba1ebUL, 1 },
    { "opr-10267.100",  0xa85bb823UL, 1 }, { "opr-10231.103",  0x8908bcbfUL, 1 },
    { "opr-10266.101",  0x9f6f1a74UL, 1 }, { "opr-10230.104",  0x686f5e50UL, 1 },
    { "opr-10185.11",   0x22794426UL, 1 }, { "opr-10186.47",   0x22794426UL, 1 },
    { "mpr-10371.9",    0x7cc86208UL, 1 }, { "mpr-10373.10",   0xb0d26ac9UL, 1 },
    { "mpr-10375.11",   0x59b60bd7UL, 1 }, { "mpr-10377.12",   0x17a1b04aUL, 1 },
    { "mpr-10372.13",   0xb557078cUL, 1 }, { "mpr-10374.14",   0x8051e517UL, 1 },
    { "mpr-10376.15",   0xf3b8f318UL, 1 }, { "mpr-10378.16",   0xa1062984UL, 1 },
    { "epr-10187.88",   0xa10abaa9UL, 1 }, { "opr-10193.66",   0xbcd10ddeUL, 1 },
    { "opr-10192.67",   0x770f1270UL, 1 }, { "opr-10191.68",   0x20a284abUL, 1 },
    { "opr-10190.69",   0x7cab70e2UL, 1 }, { "opr-10189.70",   0x01366b54UL, 1 },
    { "opr-10188.71",   0xbad30ad9UL, 1 },
    { "epr-10380.133",  0xe339e87aUL, 0 }, { "epr-10382.118",  0x65248dd5UL, 0 },
    { "epr-10381.132",  0xbe8c412bUL, 0 }, { "epr-10383.117",  0xdcc586e7UL, 0 },
    { "epr-10327.76",   0xda99d855UL, 0 }, { "epr-10329.58",   0xfe0fa5e2UL, 0 },
    { "epr-10328.75",   0x3c0e9a7fUL, 0 }, { "epr-10330.57",   0x59786e99UL, 0 },
    { "opr-10188.71f",  0x37598616UL, 0 }, { "opr-10188.71f",  0xC2DE09B2UL, 0 },
};
#define NROMS (int)(sizeof(ROMS) / sizeof(ROMS[0]))
#define REVB 31

static unsigned long crc_tab[256];
static unsigned char buf[16384];

static void crc_init(void)
{
    for (unsigned long n = 0; n < 256; n++)
    {
        unsigned long c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}

static int file_crc(const char* path, unsigned long* crc, long* size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    unsigned long c = 0xffffffffUL;
    long total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ buf[i]) & 0xff] ^ (c >> 8);
        total += (long)n;
    }
    fclose(f);
    *crc = c ^ 0xffffffffUL;
    *size = total;
    return 1;
}

/* epr-10380b.133 -> E10380b.133 (same rule as src/main/romloader.cpp, atari_short_name) */
static void short_name(const char* lng, char* out)
{
    out[0] = (char)toupper((unsigned char)lng[0]);
    strcpy(out + 1, lng + 4);
}

static int same_name(const char* a, const char* b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static const char* ext_of(const char* name)
{
    const char* d = strrchr(name, '.');
    return d ? d + 1 : "";
}

/* Which ROM a file is: by CRC; when two names share contents, the one the current name (long,
   short or truncated alias: the extension survives) points to, else revision B's. */
static int identify(const char* fname, unsigned long crc)
{
    int first = -1, by_ext = -1;
    for (int i = 0; i < NROMS; i++)
    {
        if (ROMS[i].crc != crc) continue;
        char sn[20];
        short_name(ROMS[i].name, sn);
        if (same_name(fname, ROMS[i].name) || same_name(fname, sn)) return i;
        if (first < 0) first = i;
        if (by_ext < 0 && same_name(ext_of(fname), ext_of(ROMS[i].name))) by_ext = i;
    }
    return by_ext >= 0 ? by_ext : first;
}

static int wait_key(void)
{
    return (int)(Cconin() & 0xff);
}

/* Sets "freemint = v" in outrun.ini (keeps the rest of the line). */
static int set_freemint(int v)
{
    static char text[32768];
    FILE* f = fopen("outrun.ini", "rb");
    if (!f) return 0;
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = 0;
    char* p = text;
    while ((p = strstr(p, "freemint")) != 0)
    {
        if (p == text || p[-1] == '\n')
        {
            char* q = p + 8;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '=')
            {
                q++;
                while (*q == ' ' || *q == '\t') q++;
                if (*q == '0' || *q == '1')
                {
                    *q = (char)('0' + v);
                    f = fopen("outrun.ini", "wb");
                    if (!f) return 0;
                    fwrite(text, 1, n, f);
                    fclose(f);
                    return 1;
                }
            }
        }
        p += 8;
    }
    return 0;
}

int main(void)
{
    long mint = 0;
    const int have_mint = Getcookie(C_MiNT, &mint) == C_FOUND;

    printf("\033E OutRun ROM names / Noms des ROMs OutRun\r\n\r\n");
    printf(" L = long names  / noms longs  (epr-10380b.133, FreeMiNT, freemint = 1)\r\n");
    printf(" S = short names / noms courts (E10380b.133, TOS, freemint = 0)\r\n");
    printf(" Other key / autre touche = quit / quitter\r\n\r\n");
#ifdef TEST_MODE
    int k = TEST_MODE;
#else
    int k = toupper(wait_key());
#endif
    if (k != 'L' && k != 'S') return 0;
    const int to_long = k == 'L';
    if (to_long && !have_mint)
    {
        printf("\r\nLong names need FreeMiNT. / Les noms longs demandent FreeMiNT.\r\n");
        printf("\r\nPress a key / Appuie sur une touche\r\n");
        wait_key();
        return 1;
    }

    crc_init();
    DIR* d = opendir("roms");
    if (!d)
    {
        printf("\r\nNo roms folder here. / Pas de dossier roms ici.\r\n");
        printf("\r\nPress a key / Appuie sur une touche\r\n");
        wait_key();
        return 1;
    }
    /* Read the folder first: renaming while listing it can skip or repeat entries. */
    static char names[128][64];
    int nf = 0;
    struct dirent* e;
    while ((e = readdir(d)) != 0 && nf < 128)
    {
        if (e->d_name[0] == '.') continue;
        strncpy(names[nf], e->d_name, 63);
        names[nf][63] = 0;
        nf++;
    }
    closedir(d);

    int found[NROMS];
    memset(found, 0, sizeof(found));
    int renamed = 0, ok = 0, failed = 0, unknown = 0;
    for (int i = 0; i < nf; i++)
    {
        char path[96], target[96];
        sprintf(path, "roms\\%s", names[i]);
        unsigned long crc;
        long size;
        if (!file_crc(path, &crc, &size)) continue;
        const int r = identify(names[i], crc);
        if (r < 0)
        {
            if (!same_name(names[i], "README.TXT") && !same_name(names[i], "ROMS.TXT")
                && !same_name(names[i], "LISEZMOI.TXT")) unknown++;
            continue;
        }
        found[r] = 1;
        char want[20];
        if (to_long) strcpy(want, ROMS[r].name); else short_name(ROMS[r].name, want);
        if (strcmp(names[i], want) == 0 || (!have_mint && same_name(names[i], want))) { ok++; continue; }
        sprintf(target, "roms\\%s", want);
        if (same_name(names[i], want))
        {
            /* only the case differs: go through a temporary name */
            char tmp[96];
            sprintf(tmp, "roms\\ROMNAME.TMP");
            if (rename(path, tmp) == 0 && rename(tmp, target) == 0) { renamed++; printf(" %s -> %s\r\n", names[i], want); }
            else { failed++; printf(" ! %s: not renamed / pas renomme\r\n", names[i]); }
            continue;
        }
        FILE* t = fopen(target, "rb");
        if (t)
        {
            fclose(t);
            failed++;
            printf(" ! %s: %s already exists / existe deja\r\n", names[i], want);
            continue;
        }
        if (rename(path, target) == 0) { renamed++; printf(" %s -> %s\r\n", names[i], want); }
        else { failed++; printf(" ! %s: not renamed / pas renomme\r\n", names[i]); }
    }

    printf("\r\n%d renamed / renommes, %d already right / deja bons", renamed, ok);
    if (failed) printf(", %d failed / echecs", failed);
    if (unknown) printf(", %d other files / autres fichiers", unknown);
    printf("\r\n");
    int missing = 0;
    for (int i = 0; i < REVB; i++)
        if (!found[i])
        {
            char sn[20];
            if (to_long) strcpy(sn, ROMS[i].name); else short_name(ROMS[i].name, sn);
            if (!missing) printf("\r\nMissing (revision B) / Manquants (revision B):\r\n");
            printf(" %s", sn);
            missing++;
        }
    if (missing) printf("\r\n");

    if (set_freemint(to_long ? 1 : 0))
        printf("\r\noutrun.ini: freemint = %d\r\n", to_long ? 1 : 0);
    else
        printf("\r\nSet freemint = %d in outrun.ini / Mets freemint = %d dans outrun.ini\r\n", to_long ? 1 : 0, to_long ? 1 : 0);

    printf("\r\nPress a key / Appuie sur une touche\r\n");
#ifndef TEST_MODE
    wait_key();
#endif
    return 0;
}
