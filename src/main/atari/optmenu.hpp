#pragma once

/***************************************************************************
    Options menu of the Atari port: shown at start-up (and with F1 during the
    game), over the OutRun logo and the scrolling road of Cannonball's own
    menu (frontend/menu.cpp, not built here). It changes the outrun.ini
    options that take effect at once (picture rate, lines drawn, road
    detail, scenery, shadows, music, joystick buttons), can save them into
    outrun.ini, and starts the game.

    The first page has PLAY (selected) and OPTIONS, which opens the options
    page. There up / down (keys or joystick) pick a line, left / right change
    it, Enter or fire (pad A) steps it to the next value; SAVE SETTINGS
    writes outrun.ini, BACK goes back to PLAY / OPTIONS.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

class OptMenu
{
public:
    OptMenu() : page(0), main_cursor(0), cursor(0), frame(0), message(0), last_dirs(0), saved_ok(false) {}
    void init();     // sets the screen up (logo, road) and the sound chips (for the beeps)
    bool tick();     // one game step; true when PLAY was chosen

private:
    int page;          // 0: PLAY / OPTIONS, 1: the options
    int main_cursor;   // page 0: 0 = PLAY, 1 = OPTIONS
    int cursor;        // page 1: the line
    int frame;
    int message;     // steps the "SAVED" line stays up
    int last_dirs;   // up, down, left, right, select last step (edges only)
    bool saved_ok;

    void draw();
    void change(int line, int dir);
};

extern OptMenu optmenu;
