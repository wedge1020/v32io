#ifndef _MOUSE_H
#define _MOUSE_H

//////////////////////////////////////////////////////////////////////////////
//
// v32tools/v32mouse library: "mouse.h"          File version: 2026/10/08
// ----------------------------------------------------------------------
//
// This file contains all definitions and functions needed by programs to
// access input from a mouse-attached gamepad, allowing mouse input in
// Vircon32 programs.
//
// Reading the gamepad itself is done by the v32io core (v32io.h); this
// library interprets the packed controls it provides as a mouse:
//
// BIT:   0 -> V32IO_LEFT   (INP_GamepadLeft)   \  X counter: trit
//        1 -> V32IO_RIGHT  (INP_GamepadRight)  /  (left, none or right)
//        2 -> V32IO_UP     (INP_GamepadUp)     \  Y counter: trit
//        3 -> V32IO_DOWN   (INP_GamepadDown)   /  (up, none or down)
//        4 -> V32IO_START  (ButtonStart)       -> middle mouse button
//        5 -> V32IO_A      (ButtonA)           -> left mouse button
//        6 -> V32IO_B      (ButtonB)           -> right mouse button
//        7 -> V32IO_X      (ButtonX)           \  X counter: Gray code
//        8 -> V32IO_Y      (ButtonY)           /  (high, low)
//        9 -> V32IO_L      (ButtonL)           \  Y counter: Gray code
//       10 -> V32IO_R      (ButtonR)           /  (high, low)
//
// Mouse buttons are reported as they are, held for as long as they are.
//
// Movement is not reported as deltas, but as the position of 2 counters
// (one per axis) that go around a cycle of 12 positions. Each step moves
// one position forward (right / down) or backward (left / up), and every
// step changes exactly ONE control, so a gamepad state is never seen
// halfway between positions. Each frame, this library compares the new
// counter positions to the previous ones to obtain the movement.
//
// Each counter is made of a 2-bit Gray code (4 groups) and a "trit" on
// a pair of opposite directions (which the console never shows pressed
// at once). Within each group the trit goes negative / none / positive,
// and in odd groups it is walked backwards (positive / none / negative):
//
//   position:  0  1  2 | 3  4  5 | 6  7  8 | 9 10 11
//   gray:        00    |   01    |   11    |   10
//   trit:      -  0  + | +  0  - | -  0  + | +  0  -
//
// At rest after power on, the device shows position 1 on both counters
// (no controls pressed at all).
//
// Since a cycle has 12 positions, the device moves each counter by up to
// 5 positions per frame (a movement of 6 could not be told apart from -6).
// One position is one "step" of movement; the library multiplies steps by
// a scale (pixels per step) to move its pointer.
//
// IMPORTANT: v32mouse_probe() must be called once on EVERY frame for each
// mouse. If frames are skipped, movement can be lost or misread.
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//
// include the v32io core (it includes the needed DevTools headers)
//
#include "v32io.h"

//////////////////////////////////////////////////////////////////////////////
//
// mouse buttons (as used in this library's functions)
//
#define V32MOUSE_LEFT           1  // left mouse button
#define V32MOUSE_RIGHT          2  // right mouse button
#define V32MOUSE_MIDDLE         4  // middle mouse button (wheel click)
#define V32MOUSE_ANY            7  // any of them

//////////////////////////////////////////////////////////////////////////////
//
// meaning of the packed controls for a v32mouse device
//
#define V32MOUSE_X_NEGATIVE     1  // V32IO_LEFT:  X trit, negative
#define V32MOUSE_X_POSITIVE     2  // V32IO_RIGHT: X trit, positive
#define V32MOUSE_Y_NEGATIVE     4  // V32IO_UP:    Y trit, negative
#define V32MOUSE_Y_POSITIVE     8  // V32IO_DOWN:  Y trit, positive
#define V32MOUSE_BTN_MIDDLE    16  // V32IO_START: middle button
#define V32MOUSE_BTN_LEFT      32  // V32IO_A:     left button
#define V32MOUSE_BTN_RIGHT     64  // V32IO_B:     right button
#define V32MOUSE_X_HIGH       128  // V32IO_X:     X Gray code, high bit
#define V32MOUSE_X_LOW        256  // V32IO_Y:     X Gray code, low bit
#define V32MOUSE_Y_HIGH       512  // V32IO_L:     Y Gray code, high bit
#define V32MOUSE_Y_LOW       1024  // V32IO_R:     Y Gray code, low bit

//////////////////////////////////////////////////////////////////////////////
//
// library parameters
//
#define V32MOUSE_POSITIONS     12  // positions in each counter cycle
#define V32MOUSE_SCALE          2  // default pixels per step
#define V32MOUSE_SCREEN_W     640  // default pointer bounds: the screen
#define V32MOUSE_SCREEN_H     360

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse is the mouse instance, bound to a gameport
//
struct v32mouse
{
    v32io  *io;        // v32io core instance reading the gamepad
    int     gamepad;   // id of gamepad being used as a v32 mouse
    int     countx;    // last X counter position (-1: unknown)
    int     county;    // last Y counter position (-1: unknown)
    int     stepx;     // X movement in the latest probe, in steps
    int     stepy;     // Y movement in the latest probe, in steps
    int     dx;        // X movement in the latest probe, in pixels
    int     dy;        // Y movement in the latest probe, in pixels
    int     x;         // pointer position
    int     y;
    int     minx;      // pointer bounds (inclusive)
    int     miny;
    int     maxx;
    int     maxy;
    int     scale;     // pixels per step
    int     buttons;   // buttons held (V32MOUSE_ masks)
    int     down;      // buttons that went down in the latest probe
    int     up;        // buttons that went up in the latest probe
    int     lastread;  // frame of the latest movement or button change
};

//////////////////////////////////////////////////////////////////////////////
//
// API: function prototypes for the functions in this library
//
v32mouse *v32mouse_init        (int        gamepad);          // initialize mouse
void      v32mouse_free        (v32mouse **mouse);            // release a mouse
int       v32mouse_counter     (int raw, int negative, int positive,
                                int high, int low);           // decode a counter
int       v32mouse_steps       (int before, int after);       // counter movement
bool      v32mouse_probe       (v32mouse **mouse);            // check for input
void      v32mouse_delta       (v32mouse **mouse, int *dx, int *dy);  // movement
void      v32mouse_position    (v32mouse **mouse, int *x, int *y);    // pointer
void      v32mouse_setposition (v32mouse **mouse, int x, int y);      // move pointer
void      v32mouse_setbounds   (v32mouse **mouse, int minx, int miny,
                                int maxx, int maxy);          // pointer limits
void      v32mouse_setscale    (v32mouse **mouse, int scale); // pixels per step
bool      v32mouse_isdown      (v32mouse **mouse, int button);  // button held?
bool      v32mouse_pressed     (v32mouse **mouse, int button);  // just went down?
bool      v32mouse_released    (v32mouse **mouse, int button);  // just went up?

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_counter(): obtain the position (0 to 11) of a movement counter,
// from the packed controls and the masks of the 4 controls it uses.
// Returns -1 if the controls don't form a valid position
//
int  v32mouse_counter (int raw, int negative, int positive, int high, int low)
{
    int  trit             = 1;   // 0: negative, 1: none, 2: positive
    int  group            = 0;   // 0 to 3, from the Gray code
    int  position         = -1;

    //////////////////////////////////////////////////////////////////////////
    //
    // Gray code to group: 00 -> 0, 01 -> 1, 11 -> 2, 10 -> 3
    //
    if ((raw & high)     != 0)
    {
        group             = 3;
        if ((raw & low)  != 0)
        {
            group         = 2;
        }
    }
    else if ((raw & low) != 0)
    {
        group             = 1;
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // The trit (both of its controls can never be seen pressed)
    //
    if ((raw & negative) != 0)
    {
        trit              = 0;
        if ((raw & positive) != 0)
        {
            trit          = -1;
        }
    }
    else if ((raw & positive) != 0)
    {
        trit              = 2;
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Odd groups walk the trit backwards
    //
    if (trit             >= 0)
    {
        if ((group & 1)  == 0)
        {
            position      = (group * 3) + trit;
        }
        else
        {
            position      = (group * 3) + 2 - trit;
        }
    }

    return (position);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_steps(): obtain the movement (-5 to +5 steps) from counter
// position "before" to "after". A difference of 6 can't be told apart from
// -6, and it is taken as no movement
//
int  v32mouse_steps (int before, int after)
{
    int  steps            = 0;

    if ((before          >= 0) &&
        (after           >= 0))
    {
        steps             = (after - before + V32MOUSE_POSITIONS) % V32MOUSE_POSITIONS;

        if (steps        == 6)
        {
            steps         = 0;
        }
        else if (steps   >  6)
        {
            steps         = steps - V32MOUSE_POSITIONS;
        }
    }

    return (steps);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_init(): initialize new v32mouse instance (associate with gamepad)
// The pointer starts at the center of the screen
//
v32mouse *v32mouse_init (int  gamepad)
{
    v32mouse *mouse           = NULL;
    int       raw             = 0;

    mouse                     = (v32mouse *) malloc (sizeof (v32mouse) * 1);
    if (mouse                != NULL)
    {
        mouse -> gamepad      = gamepad;
        mouse -> stepx        = 0;
        mouse -> stepy        = 0;
        mouse -> dx           = 0;
        mouse -> dy           = 0;
        mouse -> minx         = 0;
        mouse -> miny         = 0;
        mouse -> maxx         = V32MOUSE_SCREEN_W - 1;
        mouse -> maxy         = V32MOUSE_SCREEN_H - 1;
        mouse -> x            = V32MOUSE_SCREEN_W / 2;
        mouse -> y            = V32MOUSE_SCREEN_H / 2;
        mouse -> scale        = V32MOUSE_SCALE;
        mouse -> down         = 0;
        mouse -> up           = 0;
        mouse -> lastread     = -1;

        //////////////////////////////////////////////////////////////////////
        //
        // The v32io core does the reading of the gamepad
        //
        mouse -> io           = v32io_init (gamepad);
        if (mouse -> io      == NULL)
        {
            free (mouse);
            mouse             = NULL;
        }

        //////////////////////////////////////////////////////////////////////
        //
        // Take the current counter positions and buttons as the starting
        // point: movement is measured from here on
        //
        else
        {
            raw               = v32io_read (&(mouse -> io));
            mouse -> countx   = v32mouse_counter (raw, V32MOUSE_X_NEGATIVE, V32MOUSE_X_POSITIVE,
                                                       V32MOUSE_X_HIGH,     V32MOUSE_X_LOW);
            mouse -> county   = v32mouse_counter (raw, V32MOUSE_Y_NEGATIVE, V32MOUSE_Y_POSITIVE,
                                                       V32MOUSE_Y_HIGH,     V32MOUSE_Y_LOW);
            mouse -> buttons  = 0;
            if ((raw & V32MOUSE_BTN_LEFT)   != 0) mouse -> buttons = mouse -> buttons | V32MOUSE_LEFT;
            if ((raw & V32MOUSE_BTN_RIGHT)  != 0) mouse -> buttons = mouse -> buttons | V32MOUSE_RIGHT;
            if ((raw & V32MOUSE_BTN_MIDDLE) != 0) mouse -> buttons = mouse -> buttons | V32MOUSE_MIDDLE;
        }
    }

    return (mouse);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_free(): release a mouse instance
//
void    v32mouse_free (v32mouse **mouse)
{
    if (*mouse               != NULL)
    {
        v32io_free (&((*mouse) -> io));
        free (*mouse);
        *mouse                = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_probe(): check for new input, updating movement, pointer and
// buttons. Call it once every frame; it returns true if the mouse moved
// or any button changed
//
bool v32mouse_probe (v32mouse **mouse)
{
    //////////////////////////////////////////////////////////////////////////
    //
    // Declare and initialize local variables
    //
    bool      result               = false;
    int       raw                  = 0;
    int       buttons              = 0;
    int       countx               = 0;
    int       county               = 0;
    v32mouse *m                    = NULL;

    //////////////////////////////////////////////////////////////////////////
    //
    // Only process for an established mouse instance
    //
    if (*mouse                     != NULL)
    {
        m                           = *mouse;
        m -> stepx                  = 0;
        m -> stepy                  = 0;
        m -> dx                     = 0;
        m -> dy                     = 0;

        //////////////////////////////////////////////////////////////////////
        //
        // Have the v32io core read the gamepad, and get its packed
        // controls
        //
        v32io_probe (&(m -> io));
        raw                         = v32io_read (&(m -> io));

        //////////////////////////////////////////////////////////////////////
        //
        // Buttons
        //
        if ((raw & V32MOUSE_BTN_LEFT)   != 0) buttons = buttons | V32MOUSE_LEFT;
        if ((raw & V32MOUSE_BTN_RIGHT)  != 0) buttons = buttons | V32MOUSE_RIGHT;
        if ((raw & V32MOUSE_BTN_MIDDLE) != 0) buttons = buttons | V32MOUSE_MIDDLE;

        m -> down                   = buttons      & (V32MOUSE_ANY - m -> buttons);
        m -> up                     = m -> buttons & (V32MOUSE_ANY - buttons);
        m -> buttons                = buttons;

        //////////////////////////////////////////////////////////////////////
        //
        // Movement: compare counter positions with the previous ones.
        // A newly connected gamepad shows all controls released, which
        // says nothing of where the counters really are: start over
        //
        countx                      = v32mouse_counter (raw, V32MOUSE_X_NEGATIVE, V32MOUSE_X_POSITIVE,
                                                             V32MOUSE_X_HIGH,     V32MOUSE_X_LOW);
        county                      = v32mouse_counter (raw, V32MOUSE_Y_NEGATIVE, V32MOUSE_Y_POSITIVE,
                                                             V32MOUSE_Y_HIGH,     V32MOUSE_Y_LOW);

        if (v32io_connected (&(m -> io)) &&
            !v32io_plugged  (&(m -> io)))
        {
            m -> stepx              = v32mouse_steps (m -> countx, countx);
            m -> stepy              = v32mouse_steps (m -> county, county);
        }

        m -> countx                 = countx;
        m -> county                 = county;

        //////////////////////////////////////////////////////////////////////
        //
        // Move the pointer, keeping it within its bounds
        //
        m -> dx                     = m -> stepx * m -> scale;
        m -> dy                     = m -> stepy * m -> scale;
        m -> x                      = m -> x + m -> dx;
        m -> y                      = m -> y + m -> dy;

        if (m -> x                 <  m -> minx)  m -> x = m -> minx;
        if (m -> x                 >  m -> maxx)  m -> x = m -> maxx;
        if (m -> y                 <  m -> miny)  m -> y = m -> miny;
        if (m -> y                 >  m -> maxy)  m -> y = m -> maxy;

        if ((m -> stepx            != 0) ||
            (m -> stepy            != 0) ||
            (m -> down             != 0) ||
            (m -> up               != 0))
        {
            result                  = true;
            m -> lastread           = get_frame_counter ();
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_delta(): obtain the movement of the latest probe, in pixels
// (steps multiplied by the scale). Either pointer can be NULL
//
void v32mouse_delta (v32mouse **mouse, int *dx, int *dy)
{
    if (*mouse                     != NULL)
    {
        if (dx                     != NULL)  *dx = (*mouse) -> dx;
        if (dy                     != NULL)  *dy = (*mouse) -> dy;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_position(): obtain the pointer position. Either pointer can be
// NULL
//
void v32mouse_position (v32mouse **mouse, int *x, int *y)
{
    if (*mouse                     != NULL)
    {
        if (x                      != NULL)  *x = (*mouse) -> x;
        if (y                      != NULL)  *y = (*mouse) -> y;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_setposition(): place the pointer (it is kept within bounds)
//
void v32mouse_setposition (v32mouse **mouse, int x, int y)
{
    v32mouse *m                     = NULL;

    if (*mouse                     != NULL)
    {
        m                           = *mouse;
        m -> x                      = x;
        m -> y                      = y;

        if (m -> x                 <  m -> minx)  m -> x = m -> minx;
        if (m -> x                 >  m -> maxx)  m -> x = m -> maxx;
        if (m -> y                 <  m -> miny)  m -> y = m -> miny;
        if (m -> y                 >  m -> maxy)  m -> y = m -> maxy;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_setbounds(): set the area the pointer can move in (inclusive)
//
void v32mouse_setbounds (v32mouse **mouse, int minx, int miny, int maxx, int maxy)
{
    if (*mouse                     != NULL)
    {
        (*mouse) -> minx            = minx;
        (*mouse) -> miny            = miny;
        (*mouse) -> maxx            = maxx;
        (*mouse) -> maxy            = maxy;
        v32mouse_setposition (mouse, (*mouse) -> x, (*mouse) -> y);
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_setscale(): set how many pixels the pointer moves per step
//
void v32mouse_setscale (v32mouse **mouse, int scale)
{
    if ((*mouse                    != NULL) &&
        (scale                     >  0))
    {
        (*mouse) -> scale           = scale;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_isdown(): check if a button is held (V32MOUSE_LEFT, ...). With
// several buttons in the mask, true if any of them is held
//
bool v32mouse_isdown (v32mouse **mouse, int button)
{
    bool    result                  = false;

    if (*mouse                     != NULL)
    {
        result                      = (((*mouse) -> buttons & button) != 0);
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_pressed(): check if a button went down in the latest probe
//
bool v32mouse_pressed (v32mouse **mouse, int button)
{
    bool    result                  = false;

    if (*mouse                     != NULL)
    {
        result                      = (((*mouse) -> down & button) != 0);
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse_released(): check if a button went up in the latest probe
//
bool v32mouse_released (v32mouse **mouse, int button)
{
    bool    result                  = false;

    if (*mouse                     != NULL)
    {
        result                      = (((*mouse) -> up & button) != 0);
    }

    return (result);
}

#endif
