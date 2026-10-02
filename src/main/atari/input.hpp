#pragma once

/***************************************************************************
    Atari Keyboard / Joystick Input.

    Public surface matches the subset of src/main/sdl2/input.hpp that the
    engine (engine/oinputs.cpp) and main_atari.cpp actually touch: the
    `keys[]`/`keys_old[]` logical button state, is_pressed/is_pressed_clear/
    has_pressed/frame_done, and the analog fields oinputs.cpp reads
    (analog, gamepad, a_wheel, a_accel, a_brake). Gamepad/analog support (SDL2
    controller API-style rumble, analog wheel/pedals) is NOT implemented -
    gamepad is always false, analog always 0. The digital DB9 joystick below
    is a separate, simpler thing: on/off directions and a fire button, read
    via the IKBD and folded into the same keys[] array as the keyboard.

    Implementation: an interrupt handler (kbd_asm.S) replaces the keyboard ACIA
    vector for the duration of the game and keeps a table of key-down flags,
    so the state can be polled every frame without blocking (Bconin()/Cnecin
    block and give no key-up events).  init() installs it, shutdown() restores
    the previous vector.

    Joystick: the same interrupt handler also latches IKBD joystick-1 event
    packets (the dedicated joystick-only DB9 port, not the one shared with the
    mouse) into atari_joy1 - see kbd_asm.S. poll() ORs its bits into keys[] on
    top of the keyboard scancodes, so keyboard and joystick both work at once:
    left/right = steer, up = accelerate, down = brake, fire = shift gear
    (GEAR1, the same single "shift" action as the LALT key).

    Enhanced joystick ports (the two 15-pin sockets on the left side of the
    STE and the Falcon): read directly from their registers by poll(), both
    ports, on machines that have them (see read_ext_ports() in input.cpp).
    A Jaguar pad or a plain joystick on an adapter works there: left/right =
    steer, up or B = accelerate, down or C = brake, A (the fire button of a
    plain joystick) = shift gear, Option = coin/start, Pause = pause.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"

class Input
{
public:
    enum presses
    {
        LEFT  = 0,  RIGHT = 1,  UP   = 2,  DOWN  = 3,
        ACCEL = 4,  BRAKE = 5,  GEAR1 = 6, GEAR2 = 7,
        START = 8,  COIN  = 9,  VIEWPOINT = 10,
        PAUSE = 11, STEP  = 12, TIMER = 13, MENU = 14,
        SCREENSHOT = 15,
    };

    bool keys[16];
    bool keys_old[16];

    enum limits { SW_LEFT = 0, SW_CENTRE = 1, SW_RIGHT = 2 };
    bool motor_limits[3];

    bool gamepad;          // always false - see the block comment above (digital DB9 joystick
                           // is folded into keys[] directly, not exposed through this SDL2-style flag)
    int  rumble_supported;  // always 0 (no force-feedback hardware on STE)
    int  analog;            // always 0 (keyboard is digital-only in this pass)
    int  key_press;
    int16_t joy_button;

    int wheel, a_wheel, a_accel, a_brake, a_motor;

    Input();
    ~Input();

    void init(int, int*, int*, const int, int*, bool*, int*);
    void shutdown();       // restores the keyboard interrupt vector
    void open_joy() {}
    void close_joy() {}

    void frame_done();
    bool is_pressed(presses p);
    bool is_pressed_clear(presses p);
    bool has_pressed(presses p);
    void set_rumble(bool, float strength = 1.0f) {}

    // Raw scancode, read directly from the interrupt-maintained table - not routed through
    // keys[]/keys_old[]/has_pressed() at all. escape_down() could not be confirmed to fire at all
    // in testing at any layer (this direct one, has_pressed(MENU), or even the standard TOS BIOS
    // console keyboard buffer - see main_atari.cpp's Bconstat/Bconin check), which points to Escape
    // being intercepted before it ever reaches the guest at all (e.g. by remote-access/viewer
    // software reserving it to leave fullscreen) rather than a bug in this port. f10_down() (Atari
    // scancode 0x44) is a second quit key that is far less likely to be reserved by anything.
    bool escape_down() const { return scan_state[0x01]; }
    bool f10_down() const { return scan_state[0x44]; }
    bool f9_down() const { return scan_state[0x43]; }   // diagnostic: direct read, bypasses keys[]/has_pressed()
    bool y_down() const { return scan_state[0x15]; }
    bool n_down() const { return scan_state[0x31]; }
    // True if ANY key at all is currently down - a much more basic test than any single key: if
    // this is never true either, no keyboard input is reaching the game at all (a focus/host-level
    // issue), regardless of which key is pressed.
    bool any_key_down() const { for (int i = 0; i < NUM_SCANCODES; i++) if (scan_state[i]) return true; return false; }

private:
    static const int NUM_SCANCODES = 128;
    bool scan_state[NUM_SCANCODES];

    // Default scancode bindings (ST/STE IKBD scancode set, NOT PC scancodes
    // - close to but not identical to the PC set for a few keys). Believed
    // correct for the main alpha/cursor block; verify against a real IKBD
    // scancode table during bring-up.
    static const uint8_t SC_UP    = 0x48;
    static const uint8_t SC_DOWN  = 0x50;
    static const uint8_t SC_LEFT  = 0x4B;
    static const uint8_t SC_RIGHT = 0x4D;
    static const uint8_t SC_SPACE = 0x39; // Accel
    static const uint8_t SC_LCTRL = 0x1D; // Brake
    static const uint8_t SC_LALT  = 0x38; // Gear1
    static const uint8_t SC_LSHIFT= 0x2A; // Gear2
    static const uint8_t SC_ENTER = 0x1C; // Start / Coin
    static const uint8_t SC_ESC   = 0x01; // Menu
    static const uint8_t SC_P     = 0x19; // Pause
    static const uint8_t SC_TAB   = 0x0F; // Step
    static const uint8_t SC_T     = 0x14; // Timer freeze
    static const uint8_t SC_V     = 0x2F; // Viewpoint
    static const uint8_t SC_F9    = 0x43; // Screenshot (not "C": suspected host-level key
                                           // interception, same class of issue as Escape earlier
                                           // this session - a function key is a safer bet)

    // atari_joy1 bit layout (IKBD protocol section 5.1, joystick-1 event report byte):
    // confirmed against Hatari's own joy.c/ikbd.c (which builds/reads this exact byte),
    // since no single spec page states the bit order and polarity outright.
    static const uint8_t JOY1_UP    = 0x01;
    static const uint8_t JOY1_DOWN  = 0x02;
    static const uint8_t JOY1_LEFT  = 0x04;
    static const uint8_t JOY1_RIGHT = 0x08;
    static const uint8_t JOY1_FIRE  = 0x80;
    uint8_t joy1_state;

    // Enhanced joystick ports: what read_ext_ports() returns, ports A and B ORed together.
    static const uint16_t PAD_UP = 0x01, PAD_DOWN = 0x02, PAD_LEFT = 0x04, PAD_RIGHT = 0x08,
                          PAD_A = 0x10, PAD_B = 0x20, PAD_C = 0x40, PAD_OPTION = 0x80, PAD_PAUSE = 0x100;
    bool ext_ports;          // the machine has the enhanced ports (STE, Mega STE, Falcon)
    uint16_t read_ext_ports();

    void poll();
};

extern Input input;
