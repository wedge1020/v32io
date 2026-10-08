#ifndef LED_H_
#define LED_H_

#include <stdbool.h>

//////////////////////////////////////////////////////////////////////////////
//
// Status light, using the board's RGB LED:
//
//   solid red         powered, no keyboard or mouse
//   blinking yellow   a device was plugged in and is being set up
//                     (at least 3 blinks are always shown)
//   3 green blinks    the keyboard is ready...
//   solid green       ...and in normal operation (keyboard mode)
//   3 cyan blinks     the mouse is ready...
//   solid cyan        ...and in normal operation (mouse mode)
//   2 quick blue blinks, then steady blue blinking
//                     setup mode
//   3 blue blinks, then solid green / cyan
//                     setup mode was left
//
typedef enum
{
    LED_LINK_NONE,          // no keyboard or mouse
    LED_LINK_CONNECTING,    // a device is being set up
    LED_LINK_KEYBOARD,      // keyboard mode
    LED_LINK_MOUSE          // mouse mode
}
led_link_t;

void led_init( void );

// call continuously from the main loop
void led_task( led_link_t link, bool setup_mode );

#endif
