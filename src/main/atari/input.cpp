/***************************************************************************
    Atari Keyboard Input - Implementation.
    See input.hpp for design notes and status.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include <cstring>
#include <cstdio>
#include "atari/input.hpp"
#include "main.hpp"
#include "atari/options.hpp"

#ifdef __MINT__
#include <mint/osbind.h>
#include <mint/cookie.h>
#endif

Input input;

// Single global instance backs the static vector callback below since the
// IKBD vector API takes a plain C function pointer with no user-data slot.
static Input* g_input_instance = &input;

#ifdef __MINT__
// kbd_asm.S: interrupt handler and the key-down table it maintains.
extern "C" void atari_ikbd_isr(void);
extern "C" volatile uint8_t atari_scan[128];
extern "C" volatile uint8_t atari_joy1;   // last joystick-1 event report byte (see kbd_asm.S)
extern "C" volatile uint8_t atari_joy0;   // same for joystick 0, the port shared with the mouse

static void (*old_acia_vector)(void) = 0;
static bool acia_installed = false;
static const uint32_t ACIA_VECTOR = 0x118;   // MFP interrupt 6: keyboard / MIDI ACIAs
#endif

Input::Input()
{
    gamepad = false;
    rumble_supported = 0;
    analog = 0;
    key_press = -1;
    joy_button = -1;
    ext_ports = false;
    wheel = a_wheel = a_accel = a_brake = a_motor = 0x80;
    std::memset(keys, 0, sizeof(keys));
    std::memset(keys_old, 0, sizeof(keys_old));
    std::memset(motor_limits, 0, sizeof(motor_limits));
    std::memset(scan_state, 0, sizeof(scan_state));
}

// Nothing to free. The keyboard vector is given back by shutdown(), not here.
Input::~Input()
{
}

#ifdef __MINT__
// Root cause of a bug found this session: our ISR (kbd_asm.S) treats every byte below 0xf6 as a
// raw keyboard scancode. The real IKBD also sends mouse-motion and joystick packets (multi-byte,
// header >=0xf6) whenever those are enabled - which they are by default, since TOS uses the mouse
// for the desktop. If one of those packets is already in flight at the exact instant we swap the
// ACIA vector, our ISR has no way to know it is looking at a packet's payload byte rather than a
// packet header, and misreads it as a raw scancode - e.g. a small rightward mouse-delta byte of
// 0x01 looks exactly like "scancode 0x01 (Escape) key down". Since no matching break code (0x81)
// ever follows for a key that was never really pressed, that scancode then reads as permanently
// held down - which, combined with this session's new immediate `escape_down()` check in
// main_atari.cpp's tick(), quits the game before the first picture is even drawn (seen as a solid
// black screen). Sending these two IKBD commands right after installing the vector tells the IKBD
// itself to stop sending mouse packets - the only thing left on the wire afterwards is genuine
// keyboard scancodes (and, once enabled below, joystick-1 event packets, which the ISR now parses
// on purpose - see kbd_asm.S). Cannonball never reads the mouse, so disabling it is free.
static void ikbd_send(uint8_t b)
{
    volatile uint8_t* stat = (volatile uint8_t*)0xfffffc00;
    volatile uint8_t* data = (volatile uint8_t*)0xfffffc02;
    // Bounded wait for TDRE (transmit data register empty): this hung indefinitely in testing
    // (100% CPU, no progress) on the very first byte sent this way, cause not yet identified -
    // a capped retry count turns that into "skip this command" instead of a permanent freeze,
    // while still sending the byte in the (expected) common case where TDRE comes up quickly.
    for (int i = 0; i < 100000 && !(*stat & 0x02); i++) {}
    if (*stat & 0x02) *data = b;
}
#endif

void Input::init(int, int*, int*, const int, int*, bool*, int*)
{
    // Config-driven key remapping (as sdl2/input.cpp supports via
    // key_config[]/pad_config[]) is not implemented; the fixed scancode
    // bindings in input.hpp are used unconditionally.
#ifdef __MINT__
    // Enhanced joystick ports exist on the STE, the Mega STE and the Falcon only (machine cookie
    // 1 and 3 in the high word); their registers must not be touched on anything else.
    {
        long mch = 0;
        Getcookie(C__MCH, &mch);
        ext_ports = (mch >> 16) == 1 || (mch >> 16) == 3;
    }
    // The program runs in supervisor mode (main_atari.cpp), so the vector table is writable.
    if (!acia_installed)
    {
        // Tell the IKBD to stop sending mouse packets and to report both joystick ports as events
        // BEFORE taking over the vector: Ikbdws() is TOS's own (waiting, reliable) way to send
        // IKBD commands, and any packet still in flight is then eaten by the system handler, not
        // misread by ours. The direct ikbd_send() writes below stay as a second attempt.
        {
            static char cmd[2] = { 0x12, 0x14 };   // DISABLE MOUSE, SET JOYSTICK EVENT REPORTING
            Ikbdws(1, cmd);                        // count is "bytes - 1"
            volatile uint32_t* hz200 = (volatile uint32_t*)0x4BA;
            const uint32_t t0 = *hz200;
            while (*hz200 - t0 < 4) {}             // ~20 ms: let the IKBD act on them
        }
        std::memset((void*)atari_scan, 0, sizeof(atari_scan));
        atari_joy1 = 0;
        atari_joy0 = 0;
        volatile uint32_t* vec = (volatile uint32_t*)ACIA_VECTOR;
        uint16_t sr;
        __asm__ volatile ("move.w %%sr,%0" : "=d"(sr));
        __asm__ volatile ("or.w #0x0700,%%sr" : : : "cc");   // no interrupts while swapping
        old_acia_vector = (void (*)(void))*vec;
        *vec = (uint32_t)atari_ikbd_isr;
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "cc");
        acia_installed = true;
        ikbd_send(0x12);   // DISABLE MOUSE
        ikbd_send(0x14);   // SET JOYSTICK EVENT REPORTING (joystick 1, the dedicated port - see kbd_asm.S)
        // A packet could have been mid-flight and partly misread as a raw scancode in the brief
        // window before the two commands above took effect (see block comment). Wipe the table
        // clean now that no further stray packets can arrive, so any single misparsed byte does
        // not linger as a permanently "stuck" key for the rest of the run.
        std::memset((void*)atari_scan, 0, sizeof(atari_scan));
        atari_joy1 = 0;
        atari_joy0 = 0;
    }
#endif
}

// Gives the keyboard back to TOS: re-enables mouse reporting and restores the ACIA interrupt
// vector that init() replaced. Must be called before the program ends.
void Input::shutdown()
{
#ifdef __MINT__
    if (acia_installed)
    {
        // Undo init()'s DISABLE MOUSE (0x12): without this, the IKBD stays silent on
        // mouse motion/buttons forever, even after we hand the ACIA vector back to TOS, leaving
        // the desktop's cursor dead until a cold reset. 0x08 = SET RELATIVE MOUSE POSITION
        // REPORTING, the state TOS itself sets up at boot.
        volatile uint32_t* vec = (volatile uint32_t*)ACIA_VECTOR;
        uint16_t sr;
        __asm__ volatile ("move.w %%sr,%0" : "=d"(sr));
        __asm__ volatile ("or.w #0x0700,%%sr" : : : "cc");
        *vec = (uint32_t)old_acia_vector;
        __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "cc");
        acia_installed = false;
        // Vector given back first, so the mouse packets that follow go to the system handler.
        static char cmd[1] = { 0x08 };
        Ikbdws(0, cmd);
    }
#endif
}

// Reads the two enhanced joystick ports (the 15-pin sockets on the left side of the STE and the
// Falcon) and returns the PAD_* bits of what is pressed, ports A and B together.
//
// These ports are a matrix: writing a word to $FF9202 selects one of four groups of lines per
// port (a 0 bit selects: bits 0-3 for port A, bits 4-7 for port B), then $FF9202 gives the
// directions (bits 8-11 port A, 12-15 port B: up, down, left, right) and $FF9200 the buttons
// (port A: bit 0 pause, bit 1 fire; port B: bits 2 and 3). Everything reads 0 when pressed.
// On a Jaguar pad the first group carries the directions, pause and button A, and the fire line
// of the next three groups carries B, C and Option (their direction lines are the keypad).
// A plain joystick on an adapter ignores the selection and answers the same in all four groups:
// when all four "buttons" read pressed at once it is taken as that single fire button.
uint16_t Input::read_ext_ports()
{
#ifdef __MINT__
    if (!ext_ports) return 0;
    volatile uint16_t* const buttons = (volatile uint16_t*)0xFFFF9200L;
    volatile uint16_t* const matrix  = (volatile uint16_t*)0xFFFF9202L;
    static const uint16_t SELECT[4] = { 0xFFEE, 0xFFDD, 0xFFBB, 0xFF77 };   // group 1..4, both ports
    static const uint16_t BUTTON[4] = { PAD_A, PAD_B, PAD_C, PAD_OPTION };
    uint16_t pad = 0;
    for (int g = 0; g < 4; g++)
    {
        *matrix = SELECT[g];
        // A 68060 reads back long before the selected lines have settled (a 68000 is slow
        // enough not to notice): NOP waits for the write to have left the CPU, then a few
        // reads through the slow bus give the lines a couple of microseconds.
        __asm__ volatile ("nop" ::: "memory");
        for (int w = 0; w < 8; w++) (void)*buttons;
        const uint16_t b = (uint16_t)~*buttons;
        if (b & 0x0A) pad |= BUTTON[g];            // fire line of port A or B
        if (g == 0)
        {
            const uint16_t d = (uint16_t)~*matrix;
            if (b & 0x05) pad |= PAD_PAUSE;
            if (d & 0x1100) pad |= PAD_UP;
            if (d & 0x2200) pad |= PAD_DOWN;
            if (d & 0x4400) pad |= PAD_LEFT;
            if (d & 0x8800) pad |= PAD_RIGHT;
        }
    }
    *matrix = 0xFFFF;                               // nothing selected
    const uint16_t all = PAD_A | PAD_B | PAD_C | PAD_OPTION;
    if ((pad & all) == all) pad &= (uint16_t)~(PAD_B | PAD_C | PAD_OPTION);   // plain joystick: one fire button
    return pad;
#else
    return 0;
#endif
}

// Copies the state of the keyboard and joystick, kept up to date by the interrupt handler
// (kbd_asm.S), into the keys[] array the game reads.
void Input::poll()
{
#ifdef __MINT__
    for (int i = 0; i < NUM_SCANCODES; i++)
        scan_state[i] = atari_scan[i] != 0;
    // Both DB9 ports count: joystick 1 (the joystick-only port) and joystick 0 (the mouse port,
    // which reports as a joystick since init() disabled the mouse). Snapshot once per poll, same
    // reason as the scan_state[] copy above.
    joy1_state = atari_joy1 | atari_joy0;
#else
    joy1_state = 0;
#endif
    const uint16_t pad = read_ext_ports();   // enhanced ports A and B (0 on machines without them)
#ifdef PADTRACE_FILE
    // Test aid: the first 5 values read from the enhanced ports, and whether the machine has them.
    {
        static int n = 0;
        if (n < 5) { n++; FILE* lf = fopen("PADS.TXT", "a"); if (lf) { fprintf(lf, "ext_ports=%d pad=%04x\r\n", (int)ext_ports, (unsigned)pad); fclose(lf); } }
    }
#endif
    // The joysticks are ORed on top of the keyboard so both work at once. Left/right always
    // steer; what accelerates, brakes and changes gear (GEAR1 - the same single "shift" action
    // as the LALT key) comes from outrun.ini's joy_accel / joy_brake / joy_gear (default: stick
    // forward = accelerate, back = brake, fire / pad A = gear, Jaguar B / C = accelerate / brake).
    int src = 0;
    if ((joy1_state & JOY1_UP)   || (pad & PAD_UP))   src |= JOYSRC_UP;
    if ((joy1_state & JOY1_DOWN) || (pad & PAD_DOWN)) src |= JOYSRC_DOWN;
    if ((joy1_state & JOY1_FIRE) || (pad & PAD_A))    src |= JOYSRC_FIRE;
    if (pad & PAD_B)                                  src |= JOYSRC_B;
    if (pad & PAD_C)                                  src |= JOYSRC_C;
    keys[UP]        = scan_state[SC_UP];
    keys[DOWN]      = scan_state[SC_DOWN];
    keys[LEFT]       = scan_state[SC_LEFT]  || (joy1_state & JOY1_LEFT)  != 0 || (pad & PAD_LEFT)  != 0;
    keys[RIGHT]      = scan_state[SC_RIGHT] || (joy1_state & JOY1_RIGHT) != 0 || (pad & PAD_RIGHT) != 0;
    keys[ACCEL]      = scan_state[SC_SPACE] || (src & atari_opt.joy_accel) != 0;
    keys[BRAKE]      = scan_state[SC_LCTRL] || (src & atari_opt.joy_brake) != 0;
    keys[GEAR1]      = scan_state[SC_LALT]  || (src & atari_opt.joy_gear)  != 0;
    keys[GEAR2]      = scan_state[SC_LSHIFT];
    keys[START]      = scan_state[SC_ENTER] || (pad & PAD_OPTION) != 0;
    keys[COIN]       = scan_state[SC_ENTER] || (pad & PAD_OPTION) != 0;
    keys[MENU]       = scan_state[SC_ESC];
    keys[VIEWPOINT]  = scan_state[SC_V];
    keys[PAUSE]      = scan_state[SC_P] || (pad & PAD_PAUSE) != 0;
    keys[STEP]       = scan_state[SC_TAB];
    keys[TIMER]      = scan_state[SC_T];
    keys[SCREENSHOT] = scan_state[SC_F9];
#ifdef KEYTRACE
    {   // Test aid (-DKEYTRACE): print every key state change.
        static bool prev[NUM_SCANCODES];
        for (int c = 0; c < NUM_SCANCODES; c++)
            if (scan_state[c] != prev[c])
            {
                prev[c] = scan_state[c];
                printf("KEY %02x %s%c%c", c, scan_state[c] ? "down" : "up", 13, 10);
            }
    }
#endif
#ifdef KEYTRACE_FILE
    // Test aid: checks a single scancode (0x01, Escape) and writes ONE line to a plain file the
    // first time it is ever seen down (see hwaudio/segapcm.cpp's PCM_MEASURE_FILE comment - console
    // output redirected from Hatari was not reliably captured this session while the game kept
    // running). Deliberately as light as possible: an earlier version of this that fopen/fclose'd
    // on every changed scancode, in a loop over all 128, made the game appear to freeze - opening
    // a file is slow under GEMDOS HDD emulation and this runs inside the per-tick poll().
    {
        static bool told = false;
        if (!told && scan_state[0x01])
        {
            told = true;
            FILE* lf = fopen("KEYS.TXT", "a");
            if (lf) { fprintf(lf, "ESC seen down, cannonball::state=%d\r\n", (int)cannonball::state); fclose(lf); }
        }
    }
#endif
#ifdef AUTOPLAY
    // Test aid (-DAUTOPLAY): insert a coin, press start repeatedly through the
    // menus and hold the accelerator, so steady-state speed can be measured.
    {
        int f = cannonball::frame;
        bool coin_pulse  = (f % 40) < 3 && f < 200;
        bool start_pulse = (f % 40) >= 20 && (f % 40) < 23;
        keys[COIN]  = coin_pulse;
        keys[START] = start_pulse;
        keys[ACCEL] = f > 10;
    }
#endif
}

// End of a game step: remembers this step's keys (for has_pressed()) and reads the next state.
void Input::frame_done()
{
    std::memcpy(&keys_old, &keys, sizeof(keys));
    poll();
}

bool Input::is_pressed(presses p) { return keys[p]; }

// Like is_pressed(), but the key then reads as released until it is pressed again.
bool Input::is_pressed_clear(presses p)
{
    bool pressed = keys[p];
    keys[p] = false;
    return pressed;
}

bool Input::has_pressed(presses p) { return keys[p] && !keys_old[p]; }
