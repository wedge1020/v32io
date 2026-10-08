#ifndef V32IO_H_
#define V32IO_H_

#include <stdint.h>
#include <stdbool.h>

//////////////////////////////////////////////////////////////////////////////
//
// v32io: input devices seen by Vircon32 as a gamepad.
//
// Whatever the device (keyboard, mouse...), the PC sees an ordinary USB
// gamepad with 11 buttons, in the same order as the console's INP ports
// (0x402 to 0x40C), so the buttons word is what a Vircon32 program reads
// from them with v32io.h:
//
//   button  0: Left     button  4: Start    button  8: Y
//   button  1: Right    button  5: A        button  9: L
//   button  2: Up       button  6: B        button 10: R
//   button  3: Down     button  7: X
//
// What each button means depends on the device mode (see v32kbd.h and
// v32mouse.h). In every mode, opposite directions (Left + Right, Up +
// Down) are never pressed at once: the console would release one of them.
//
#define V32BTN_LEFT       (1u <<  0)
#define V32BTN_RIGHT      (1u <<  1)
#define V32BTN_UP         (1u <<  2)
#define V32BTN_DOWN       (1u <<  3)
#define V32BTN_START      (1u <<  4)
#define V32BTN_A          (1u <<  5)
#define V32BTN_B          (1u <<  6)
#define V32BTN_X          (1u <<  7)
#define V32BTN_Y          (1u <<  8)
#define V32BTN_L          (1u <<  9)
#define V32BTN_R          (1u << 10)
#define V32BTN_COUNT      11

//////////////////////////////////////////////////////////////////////////////
//
// Device modes. The mode decides how the adapter presents itself to the
// PC (USB product name and ID) and how the buttons are used
//
typedef enum
{
    V32IO_MODE_NONE = 0,    // no input device: the gamepad is unplugged
    V32IO_MODE_KBD,         // "v32io:kbd"   (keyboard)
    V32IO_MODE_MOUSE        // "v32io:mouse" (mouse)
}
v32io_mode_t;

// report sent to the PC: 11 buttons, plus 2 axes that never move
// (they are only there so that every OS takes this for a gamepad)
typedef struct __attribute__((packed))
{
    uint16_t buttons;
    int8_t   x, y;
}
v32io_report_t;

//////////////////////////////////////////////////////////////////////////////
//
// Provided by main.c to the device modes
//

// mode the PC currently sees (used by the USB descriptors)
v32io_mode_t v32io_mode( void );

// true when exactly this buttons state is what the PC was last sent
bool v32io_delivered( uint16_t buttons );

// milliseconds since boot
uint32_t v32io_millis( void );

#endif
