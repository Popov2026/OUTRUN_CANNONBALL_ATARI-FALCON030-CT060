/***************************************************************************
    Asks the AES to redraw the whole screen.

    The game takes the screen over without telling the AES (it switches the
    video mode and writes to it directly). Started from the single-tasking
    desktop this does not matter: that desktop redraws itself when a program
    returns. Under a multitasking AES (XaAES, N.AES, MultiTOS) nothing does,
    and the desktop and the windows of the other applications stayed wiped
    until the user forced a redraw. This is the standard way to ask for one:
    register with the AES, report the whole screen as an area that was drawn
    over (form_dial FMD_FINISH), unregister.

    Must be called in user mode, with the original video mode already back.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <gem.h>

void atari_redraw_desktop()
{
    if (appl_init() < 0)
        return;                 // no AES to talk to
    short x, y, w, h;
    wind_get(0, WF_CURRXYWH, &x, &y, &w, &h);   // window 0 is the desktop: the whole screen
    form_dial(FMD_FINISH, 0, 0, 0, 0, x, y, w, h);
    appl_exit();
}
