/***************************************************************************
    PADTEST.TOS - joystick test for the Atari Falcon port of Cannonball.

    Shows, live, what the game sees from every joystick socket:
      - the two DB9 ports (joystick 0 = mouse port, joystick 1), read through the
        IKBD with the game's own interrupt handler (src/main/atari/kbd_asm.S);
      - the two 15-pin enhanced ports on the left side of the Falcon, raw register
        values for each of the four select groups.
    Move the stick / press the buttons and watch which values change.
    Esc quits.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

extern void atari_ikbd_isr(void);
extern volatile unsigned char atari_scan[128];
extern volatile unsigned char atari_joy0, atari_joy1;

#define ACIA_VECTOR 0x118

static void wait_ticks(unsigned long n)
{
    volatile unsigned long* hz200 = (volatile unsigned long*)0x4BA;
    unsigned long t0 = *hz200;
    while (*hz200 - t0 < n) {}
}

static void joy_text(char* out, unsigned char j)
{
    sprintf(out, "%02X %c%c%c%c%c", j,
            (j & 0x01) ? 'U' : '-', (j & 0x02) ? 'D' : '-',
            (j & 0x04) ? 'L' : '-', (j & 0x08) ? 'R' : '-', (j & 0x80) ? 'F' : '-');
}

int main(void)
{
    static char cmd_on[2]  = { 0x12, 0x14 };   /* disable mouse, joystick event reporting */
    static char cmd_off[1] = { 0x08 };         /* relative mouse reporting (TOS default) */
    static const unsigned short SELECT[4] = { 0xFFEE, 0xFFDD, 0xFFBB, 0xFF77 };
    volatile unsigned short* const buttons = (volatile unsigned short*)0xFFFF9200L;
    volatile unsigned short* const matrix  = (volatile unsigned short*)0xFFFF9202L;
    volatile unsigned long* vec = (volatile unsigned long*)ACIA_VECTOR;
    unsigned long old_vec;
    long mch = 0, ssp;
    int ext;
    char line[200], j0[16], j1[16];

    Getcookie(C__MCH, &mch);
    ext = (mch >> 16) == 1 || (mch >> 16) == 3;

    Cconws("\033E PADTEST - Cannonball Falcon joystick test\r\n\r\n");
    sprintf(line, " Machine cookie _MCH = %08lX, enhanced ports %s\r\n\r\n",
            (unsigned long)mch, ext ? "present" : "NOT present");
    Cconws(line);
    Cconws(" DB9 ports through the IKBD (U D L R F = up down left right fire):\r\n");
    Cconws(" 15-pin ports: B = $FF9201 buttons, M = $FF9202 high byte, per group 1..4\r\n");
    Cconws("   (idle value is FF; a pressed line reads 0)\r\n\r\n");
    Cconws(" Press Esc to quit.\r\n\r\n");

    ssp = Super(0L);              /* system variables ($4BA) and hardware need supervisor mode */
    Ikbdws(1, cmd_on);
    wait_ticks(4);
    {
        unsigned short sr;
        __asm__ volatile ("move.w %%sr,%0" : "=d"(sr));
        __asm__ volatile ("or.w #0x0700,%%sr" : : : "cc");
        old_vec = *vec;
        *vec = (unsigned long)atari_ikbd_isr;
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "cc");
    }
    memset((void*)atari_scan, 0, sizeof(atari_scan));
    atari_joy0 = atari_joy1 = 0;

    while (!atari_scan[0x01])
    {
        unsigned int b[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, m[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
        int g, w;
        if (ext)
        {
            for (g = 0; g < 4; g++)
            {
                *matrix = SELECT[g];
                __asm__ volatile ("nop" ::: "memory");
                for (w = 0; w < 8; w++) (void)*buttons;
                b[g] = *buttons & 0xFF;
                m[g] = (*matrix >> 8) & 0xFF;
            }
            *matrix = 0xFFFF;
        }
        joy_text(j0, atari_joy0);
        joy_text(j1, atari_joy1);
        sprintf(line, "\r J0:%s J1:%s | B:%02X %02X %02X %02X M:%02X %02X %02X %02X ",
                j0, j1, b[0], b[1], b[2], b[3], m[0], m[1], m[2], m[3]);
        Cconws(line);
        wait_ticks(10);
    }

    {
        unsigned short sr;
        __asm__ volatile ("move.w %%sr,%0" : "=d"(sr));
        __asm__ volatile ("or.w #0x0700,%%sr" : : : "cc");
        *vec = old_vec;
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "cc");
    }
    SuperToUser((void*)ssp);
    Ikbdws(0, cmd_off);
    Cconws("\r\n\r\n Done.\r\n");
    return 0;
}
