#ifndef V32MOUSE_H_
#define V32MOUSE_H_

#include <stdint.h>
#include <stdbool.h>
#include "v32io.h"

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse protocol (mouse mode, "v32io:mouse")
//
//   button  0: Left   \  X counter: trit (left, none or right)
//   button  1: Right  /
//   button  2: Up     \  Y counter: trit (up, none or down)
//   button  3: Down   /
//   button  4: Start  -> middle mouse button
//   button  5: A      -> left mouse button
//   button  6: B      -> right mouse button
//   button  7: X      \  X counter: Gray code (high, low)
//   button  8: Y      /
//   button  9: L      \  Y counter: Gray code (high, low)
//   button 10: R      /
//
// Mouse buttons are sent as they are. Movement is sent as the position of
// 2 counters (one per axis) that go around a cycle of 12 positions; each
// step changes exactly one button, so the PC can never see a state that
// is halfway between 2 positions:
//
//   position:  0  1  2 | 3  4  5 | 6  7  8 | 9 10 11
//   gray:        00    |   01    |   11    |   10
//   trit:      -  0  + | +  0  - | -  0  + | +  0  -
//
// Moving right / down steps forward, left / up steps backward. A program
// compares positions between frames, so a counter must never move 6 or
// more positions within a frame: steps are spaced at least MOUSE_STEP_MS
// apart (see mouse.c). Both counters start at position 1, which has no
// buttons pressed at all.
//
#define V32MOUSE_POSITIONS   12
#define V32MOUSE_REST         1

#define V32MOUSE_HID_LEFT    0x01   // buttons in USB boot mouse reports
#define V32MOUSE_HID_RIGHT   0x02
#define V32MOUSE_HID_MIDDLE  0x04

// buttons for a counter at a given position
uint16_t v32mouse_counter_buttons( int position, uint16_t negative, uint16_t positive,
                                   uint16_t high, uint16_t low );

//////////////////////////////////////////////////////////////////////////////
//
// Mouse mode (mouse.c)
//

// back to power on state: no buttons, counters at rest, no setup mode
void mouse_reset( void );

// mouse input: buttons held (HID boot bits, all mice together) and the
// movement in a report. Only call it while in mouse mode
void mouse_input( uint8_t buttons, int dx, int dy );

// call continuously in mouse mode: returns the buttons the PC should see
uint16_t mouse_task( void );

// true while in setup mode (hold the 3 mouse buttons to toggle it)
bool mouse_setup_mode( void );

#endif
