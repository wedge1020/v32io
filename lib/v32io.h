#ifndef _V32IO_H
#define _V32IO_H

//////////////////////////////////////////////////////////////////////////////
//
// v32tools/v32io library: "v32io.h"             File version: 2026/10/08
// ----------------------------------------------------------------------
//
// This file is the core of the v32io device family (v32kbd, v32mouse...)
// All of these devices look like a regular gamepad to the console, and
// use its 11 controls to carry data instead of game presses.
//
// v32io itself knows nothing of what the data means: it only reads the
// 11 controls of a gamepad and packs them into a single value (the
// "packed controls"), in the order of the console's INP ports:
//
// BIT:   0 -> 0x402 INP_GamepadLeft         V32IO_LEFT
//        1 -> 0x403 INP_GamepadRight        V32IO_RIGHT
//        2 -> 0x404 INP_GamepadUp           V32IO_UP
//        3 -> 0x405 INP_GamepadDown         V32IO_DOWN
//        4 -> 0x406 INP_GamepadButtonStart  V32IO_START
//        5 -> 0x407 INP_GamepadButtonA      V32IO_A
//        6 -> 0x408 INP_GamepadButtonB      V32IO_B
//        7 -> 0x409 INP_GamepadButtonX      V32IO_X
//        8 -> 0x40A INP_GamepadButtonY      V32IO_Y
//        9 -> 0x40B INP_GamepadButtonL      V32IO_L
//       10 -> 0x40C INP_GamepadButtonR      V32IO_R
//
// So the bit number of each control is its port number minus 0x402.
//
// Device drivers (keyboard.h, mouse.h) own a v32io instance, call
// v32io_probe() once per frame and then interpret v32io_read().
//
// NOTE: the console never shows opposite directions (Left + Right, or
// Up + Down) pressed at once: pressing one releases the other. Device
// protocols must be designed around this.
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//
// include needed Vircon32 DevTools headers
//
#include "input.h"
#include "misc.h"
#include "time.h"

//////////////////////////////////////////////////////////////////////////////
//
// game port identifiers
//
#define FIRST_GAMEPAD_PORT  0  // first gamepad port (id 0)
#define SECOND_GAMEPAD_PORT 1  // second gamepad port (id 1)
#define THIRD_GAMEPAD_PORT  2  // third gamepad port (id 2)
#define FOURTH_GAMEPAD_PORT 3  // fourth gamepad port (id 3)

//////////////////////////////////////////////////////////////////////////////
//
// masks of each control within the packed controls
//
#define V32IO_LEFT          1  // bit  0: 0x402
#define V32IO_RIGHT         2  // bit  1: 0x403
#define V32IO_UP            4  // bit  2: 0x404
#define V32IO_DOWN          8  // bit  3: 0x405
#define V32IO_START        16  // bit  4: 0x406
#define V32IO_A            32  // bit  5: 0x407
#define V32IO_B            64  // bit  6: 0x408
#define V32IO_X           128  // bit  7: 0x409
#define V32IO_Y           256  // bit  8: 0x40A
#define V32IO_L           512  // bit  9: 0x40B
#define V32IO_R          1024  // bit 10: 0x40C
#define V32IO_ALL        2047  // all 11 controls

//////////////////////////////////////////////////////////////////////////////
//
// library parameters
//
#define V32IO_CONTROLS     11  // number of gamepad controls read
#define V32IO_ROUTINE      68  // size (in words) of the in-RAM routine

//////////////////////////////////////////////////////////////////////////////
//
// v32io is a gamepad being read as a v32io device
//
struct v32io
{
    int     gamepad;    // id of the gamepad being read
    int     raw;        // the in-RAM routine stores its result here
    int     data;       // packed controls, from the latest probe
    int     previous;   // packed controls, from the probe before that
    int     frame;      // frame counter at the latest probe (-1: none yet)
    bool    connected;  // gamepad connected, at the latest probe
    bool    plugged;    // gamepad became connected at the latest probe
    int  [V32IO_ROUTINE] routine;  // in-RAM custom machine code routine
};

//////////////////////////////////////////////////////////////////////////////
//
// API: function prototypes for the functions in this library
//
v32io  *v32io_init      (int     gamepad);           // initialize instance
void    v32io_free      (v32io **io);                // release an instance
int     v32io_scan      (v32io  *io);                // read controls now
bool    v32io_probe     (v32io **io);                // once per frame
int     v32io_read      (v32io **io);                // latest packed data
int     v32io_changed   (v32io **io);                // bits that changed
bool    v32io_connected (v32io **io);                // gamepad connected?
bool    v32io_plugged   (v32io **io);                // just connected?
bool    v32io_isdown    (v32io **io, int mask);      // any of mask held?

//////////////////////////////////////////////////////////////////////////////
//
// v32io_scan(): select the instance's gamepad and run its in-RAM routine,
// which reads the 11 gamepad control ports and packs them as bits (see
// above). It also updates the connection state. The previously selected
// gamepad is restored afterwards.
//
// This reads the controls right away and does not update data/previous:
// drivers normally use v32io_probe() instead.
//
int  v32io_scan (v32io *io)
{
    int  previous         = 0;
    int  offset           = 0;

    previous              = get_selected_gamepad ();
    select_gamepad (io -> gamepad);

    io -> connected       = gamepad_is_connected ();

    //////////////////////////////////////////////////////////////////////////
    //
    // Call the routine
    //
    offset                = (int) &(io -> routine[0]);
    asm
    {
        "PUSH  R0"
        "MOV   R0, {offset}"
        "CALL  R0"
        "POP   R0"
    }

    select_gamepad (previous);

    return (io -> raw);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_init(): initialize new v32io instance (associate with gamepad)
//
v32io  *v32io_init (int  gamepad)
{
    v32io  *io                = NULL;
    int    *routine           = NULL;
    int     index             = 0;
    int     offset            = 0;
    int     port              = 0x000;

    io                        = (v32io *) malloc (sizeof (v32io) * 1);
    if (io                   != NULL)
    {
        io -> gamepad         = gamepad;
        io -> raw             = 0;
        io -> data            = 0;
        io -> previous        = 0;
        io -> frame           = -1;
        io -> connected       = false;
        io -> plugged         = false;

        //////////////////////////////////////////////////////////////////////
        //
        // Cycle through the gamepad controls via ports in assembly;
        // due to how the compiler processes inline assembly, we're
        // generating an in-RAM custom routine with exactly what is
        // needed to pack all of them as bits of a single value.
        //
        // The routine is generated only once and it is kept within
        // the instance, so it is released along with it.
        //
        routine               = &(io -> routine[0]);
        routine[0]            = 0x54000000;        // PUSH R0
        routine[1]            = 0x54200000;        // PUSH R1
        routine[2]            = 0x54400000;        // PUSH R2
        routine[3]            = 0x4E200000;        // MOV R1, 0
        routine[4]            = 0x00000000;        // immediate
        routine[5]            = 0x4C424000;        // MOV R2, R1
        for (index            = 0;
             index           <  V32IO_CONTROLS;
             index            = index + 1)
        {
            offset            = ((index * 5) + 6);
            port              = index + 2;         // 0x402 to 0x40C

            routine[offset]   = 0x5C000400 | port; // IN  R0, port
            routine[offset+1] = 0x24040000;        // IGT R0, R2
            routine[offset+2] = 0x96000000;        // SHL R0, index
            routine[offset+3] = index;             // immediate value
            routine[offset+4] = 0x88200000;        // OR  R1, R0
        }

        offset                = (index * 5) + 6;
        routine[offset]       = 0x4E034000;        // MOV [raw], R1
        routine[offset+1]     = (int) &(io -> raw);
        routine[offset+2]     = 0x58400000;        // POP R2
        routine[offset+3]     = 0x58200000;        // POP R1
        routine[offset+4]     = 0x58000000;        // POP R0
        routine[offset+5]     = 0x10000000;        // RET
        routine[offset+6]     = 0x00000000;        // HLT (for safety)

        //////////////////////////////////////////////////////////////////////
        //
        // Take the current state as the starting point, so that both
        // data and previous hold what the device is showing right now
        //
        io -> data            = v32io_scan (io);
        io -> previous        = io -> data;
    }

    return (io);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_free(): release a v32io instance
//
void    v32io_free (v32io **io)
{
    if (*io                  != NULL)
    {
        free (*io);
        *io                   = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_probe(): read the controls for this frame. The packed controls
// from the previous probe are kept in previous. Call it once per frame
// (further calls within the same frame do nothing, since the console
// only updates gamepads between frames). Returns true when the packed
// controls are different from those of the previous probe.
//
bool    v32io_probe (v32io **io)
{
    bool    result            = false;
    bool    before            = false;
    int     frame             = 0;
    v32io  *dev               = NULL;

    if (*io                  != NULL)
    {
        dev                   = *io;
        frame                 = get_frame_counter ();

        if (frame            != dev -> frame)
        {
            before            = dev -> connected;
            dev -> frame      = frame;
            dev -> previous   = dev -> data;
            dev -> data       = v32io_scan (dev);
            dev -> plugged    = (dev -> connected && !before);
            result            = (dev -> data != dev -> previous);
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_read(): obtain the packed controls from the latest probe
//
int     v32io_read (v32io **io)
{
    int     result            = 0;

    if (*io                  != NULL)
    {
        result                = (*io) -> data;
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_changed(): obtain which controls changed in the latest probe (as
// a mask of V32IO_ bits)
//
int     v32io_changed (v32io **io)
{
    int     result            = 0;

    if (*io                  != NULL)
    {
        result                = (*io) -> data ^ (*io) -> previous;
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_connected(): is the gamepad connected? (as of the latest probe)
//
bool    v32io_connected (v32io **io)
{
    bool    result            = false;

    if (*io                  != NULL)
    {
        result                = (*io) -> connected;
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_plugged(): did the gamepad become connected in the latest probe?
// Drivers whose protocol depends on the history of the controls use it to
// start over (a newly connected gamepad shows all controls unpressed).
//
bool    v32io_plugged (v32io **io)
{
    bool    result            = false;

    if (*io                  != NULL)
    {
        result                = (*io) -> plugged;
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32io_isdown(): is any of the controls in mask held? (latest probe)
//
bool    v32io_isdown (v32io **io, int mask)
{
    bool    result            = false;

    if (*io                  != NULL)
    {
        result                = (((*io) -> data & mask) != 0);
    }

    return (result);
}

#endif
