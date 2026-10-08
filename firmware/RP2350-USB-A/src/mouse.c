//////////////////////////////////////////////////////////////////////////////
//
// Mouse mode ("v32io:mouse"): mouse buttons and movement are sent through
// the gamepad buttons with the v32mouse protocol (see v32mouse.h), so
// Vircon32 programs can read them with mouse.h
//
//////////////////////////////////////////////////////////////////////////////

#include "v32mouse.h"

//////////////////////////////////////////////////////////////////////////////
//
// Movement. Mice report movement in "counts" (typically 400 to 1600 per
// inch). Every MOUSE_DIVISOR counts make 1 step of a counter, and a step
// is sent at most every MOUSE_STEP_MS (per axis). Programs read the
// counters once per frame (16.7 ms) and can tell apart up to 5 steps per
// frame, so with 5 ms there is room for frames up to 25 ms apart.
//
// That makes a speed limit of 200 steps per second. If the emulator ever
// skips a whole frame (33 ms) while the mouse moves at full speed, the
// program can see 6 or 7 steps and read them as movement backwards. With
// 7 ms that can't happen, at the cost of a lower speed limit (143 steps
// per second). Movement beyond what
// can be sent waits, but only up to MOUSE_BACKLOG steps: the rest is
// dropped, so that the pointer stops soon after the mouse does.
//
// A program multiplies steps by its own scale (2 pixels by default).
//
#ifndef MOUSE_STEP_MS
#define MOUSE_STEP_MS       5     // minimum time between steps
#endif
#ifndef MOUSE_DIVISOR
#define MOUSE_DIVISOR       4     // mouse counts per step
#endif
#ifndef MOUSE_BACKLOG
#define MOUSE_BACKLOG       6     // maximum steps waiting to be sent
#endif

//////////////////////////////////////////////////////////////////////////////
//
// Buttons. A program sees the buttons once per frame, so a very quick
// click could fall between 2 frames and be missed. Every change of a
// button is kept for at least MOUSE_CLICK_MS before that button changes
// again, which is longer than a frame
//
#ifndef MOUSE_CLICK_MS
#define MOUSE_CLICK_MS     25
#endif

//////////////////////////////////////////////////////////////////////////////
//
// Setup mode. Holding the 3 mouse buttons (left, right and middle / wheel
// click) for SETUP_HOLD_MS toggles it. In this mode, each left click
// presses the next gamepad button (1 to 11, held for as long as the left
// button is), and a right click goes back one. This is only meant for
// creating the joystick profile in Vircon32's EditControls, which asks to
// press each control on its own.
//
// After toggling, the mouse buttons are ignored until all are released.
// Entering or leaving setup mode releases all buttons and puts the
// counters back at rest.
//
#ifndef SETUP_HOLD_MS
#define SETUP_HOLD_MS    2000
#endif

#define ALL_BUTTONS  (V32MOUSE_HID_LEFT | V32MOUSE_HID_RIGHT | V32MOUSE_HID_MIDDLE)

static bool     setup_mode   = false;
static int      setup_next   = 0;       // button sent by the next click
static uint8_t  setup_last   = 0;       // mouse buttons, in the last task
static bool     wait_release = false;   // ignore buttons until all released
static bool     chord        = false;   // the 3 buttons are held...
static bool     chord_used   = false;   // ...and they already toggled
static uint32_t chord_since  = 0;

//////////////////////////////////////////////////////////////////////////////
//
// Mouse state
//
static uint8_t  held     = 0;                   // mouse buttons (HID bits)
static uint8_t  shown    = 0;                   // mouse buttons being sent
static uint32_t shown_since[ 3 ];               // when each one last changed
static int32_t  acc_x    = 0;                   // movement waiting to be
static int32_t  acc_y    = 0;                   // sent, in mouse counts
static int      pos_x    = V32MOUSE_REST;       // counter positions
static int      pos_y    = V32MOUSE_REST;
static uint32_t last_step = 0;
static uint16_t buttons  = 0;                   // state we want the PC to see

//////////////////////////////////////////////////////////////////////////////
//
// Buttons for a counter at a given position (see v32mouse.h)
//
uint16_t v32mouse_counter_buttons( int position, uint16_t negative, uint16_t positive,
                                   uint16_t high, uint16_t low )
{
    int group = position / 3;                       // 0 to 3
    int index = position % 3;
    int trit  = (group & 1)? 2 - index : index;     // odd groups go backwards
    int gray  = group ^ (group >> 1);               // 00, 01, 11, 10

    uint16_t result = 0;

    if( trit == 0 ) result |= negative;
    if( trit == 2 ) result |= positive;
    if( gray &  2 ) result |= high;
    if( gray &  1 ) result |= low;

    return result;
}

static uint16_t normal_buttons( void )
{
    uint16_t result = v32mouse_counter_buttons( pos_x, V32BTN_LEFT, V32BTN_RIGHT, V32BTN_X, V32BTN_Y )
                    | v32mouse_counter_buttons( pos_y, V32BTN_UP,   V32BTN_DOWN,  V32BTN_L, V32BTN_R );

    if( shown & V32MOUSE_HID_LEFT   ) result |= V32BTN_A;
    if( shown & V32MOUSE_HID_RIGHT  ) result |= V32BTN_B;
    if( shown & V32MOUSE_HID_MIDDLE ) result |= V32BTN_START;

    return result;
}

static void counters_reset( void )
{
    acc_x = acc_y = 0;
    pos_x = pos_y = V32MOUSE_REST;
}

void mouse_reset( void )
{
    setup_mode   = false;
    setup_next   = 0;
    setup_last   = 0;
    wait_release = false;
    chord        = false;
    chord_used   = false;
    held         = 0;
    shown        = 0;
    counters_reset();

    for( int i = 0; i < 3; i++ )
      shown_since[i] = 0;

    buttons      = normal_buttons();
}

bool mouse_setup_mode( void )
{
    return setup_mode;
}

//////////////////////////////////////////////////////////////////////////////
//
// Mouse input
//
static int32_t limit_backlog( int32_t counts )
{
    const int32_t limit = MOUSE_DIVISOR * MOUSE_BACKLOG;

    if( counts >  limit ) return  limit;
    if( counts < -limit ) return -limit;
    return counts;
}

void mouse_input( uint8_t new_buttons, int dx, int dy )
{
    held = new_buttons & ALL_BUTTONS;

    if( !setup_mode )
    {
        acc_x = limit_backlog( acc_x + dx );
        acc_y = limit_backlog( acc_y + dy );
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Turns the mouse state into gamepad states
//
uint16_t mouse_task( void )
{
    uint32_t now = v32io_millis();

    //////////////////////////////////////////////////////////////////////////
    //
    // The 3 buttons held for a while toggle setup mode
    //
    if( (held & ALL_BUTTONS) == ALL_BUTTONS )
    {
        if( !chord )
        {
            chord       = true;
            chord_used  = false;
            chord_since = now;
        }

        else if( !chord_used && now - chord_since >= SETUP_HOLD_MS )
        {
            chord_used   = true;
            setup_mode   = !setup_mode;
            setup_next   = 0;
            wait_release = true;
            shown        = 0;
            counters_reset();
        }
    }

    else chord = false;

    if( wait_release && held == 0 )
      wait_release = false;

    //////////////////////////////////////////////////////////////////////////
    //
    // Setup mode: left click presses the next button, right click goes back
    //
    if( setup_mode )
    {
        uint8_t now_held = wait_release? 0 : held;
        uint8_t went_down = now_held & (uint8_t)~setup_last;
        uint8_t went_up   = setup_last & (uint8_t)~now_held;
        setup_last = now_held;

        if( went_up & V32MOUSE_HID_LEFT )
          setup_next = (setup_next + 1) % V32BTN_COUNT;

        if( (went_down & V32MOUSE_HID_RIGHT) && !(now_held & V32MOUSE_HID_LEFT) )
          setup_next = (setup_next + V32BTN_COUNT - 1) % V32BTN_COUNT;

        buttons = (now_held & V32MOUSE_HID_LEFT)? (uint16_t)( 1u << setup_next ) : 0;
        return buttons;
    }

    setup_last = 0;

    //////////////////////////////////////////////////////////////////////////
    //
    // Normal mode: buttons follow the mouse, but each change lasts at
    // least MOUSE_CLICK_MS
    //
    uint8_t target = wait_release? 0 : held;

    for( int i = 0; i < 3; i++ )
    {
        uint8_t mask = (uint8_t)( 1u << i );

        if( ((target ^ shown) & mask) && now - shown_since[i] >= MOUSE_CLICK_MS )
        {
            shown ^= mask;
            shown_since[i] = now;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Movement: 1 step per axis at most, only once the previous state
    // has reached the PC, and at least MOUSE_STEP_MS after it did
    //
    if( !v32io_delivered( buttons ) )
      last_step = now;

    else if( now - last_step >= MOUSE_STEP_MS )
    {
        bool moved = false;

        if( acc_x >= MOUSE_DIVISOR )
        {
            pos_x  = (pos_x + 1) % V32MOUSE_POSITIONS;
            acc_x -= MOUSE_DIVISOR;
            moved  = true;
        }

        else if( acc_x <= -MOUSE_DIVISOR )
        {
            pos_x  = (pos_x + V32MOUSE_POSITIONS - 1) % V32MOUSE_POSITIONS;
            acc_x += MOUSE_DIVISOR;
            moved  = true;
        }

        if( acc_y >= MOUSE_DIVISOR )
        {
            pos_y  = (pos_y + 1) % V32MOUSE_POSITIONS;
            acc_y -= MOUSE_DIVISOR;
            moved  = true;
        }

        else if( acc_y <= -MOUSE_DIVISOR )
        {
            pos_y  = (pos_y + V32MOUSE_POSITIONS - 1) % V32MOUSE_POSITIONS;
            acc_y += MOUSE_DIVISOR;
            moved  = true;
        }

        if( moved )
          last_step = now;
    }

    buttons = normal_buttons();
    return buttons;
}
