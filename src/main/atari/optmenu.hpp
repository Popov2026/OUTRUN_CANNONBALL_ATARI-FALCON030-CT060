#pragma once

/***************************************************************************
    Options menu of the Atari port: shown at start-up (and with F1 during the
    game), over the OutRun logo and the scrolling road of Cannonball's own
    menu (frontend/menu.cpp, not built here). It changes the outrun.ini
    options that take effect at once (picture rate, lines drawn, road
    detail, scenery, shadows, music, joystick buttons), can save them into
    outrun.ini, and starts the game.

    Up / down (keys or joystick) pick a line, left / right change it,
    Enter or fire (pad A) on PLAY starts the game, on SAVE writes outrun.ini; on
    another line it steps to the next value.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

class OptMenu
{
public:
    OptMenu() : cursor(0), frame(0), message(0), last_dirs(0), saved_ok(false) {}
    void init();     // sets the screen up (logo, road) and the sound chips (for the beeps)
    bool tick();     // one game step; true when PLAY was chosen

private:
    int cursor;
    int frame;
    int message;     // steps the "SAVED" line stays up
    int last_dirs;   // up, down, left, right, select last step (edges only)
    bool saved_ok;

    void draw();
    void change(int line, int dir);
};

extern OptMenu optmenu;
