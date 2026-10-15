//////////////////////////////////////////////////////////////////////////////
//
// Keyboard mode ("v32io:kbd"): key events are sent through the gamepad
// buttons with the v32kbd protocol (see v32kbd.h), so Vircon32 programs
// can read them with keyboard.h
//
//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "v32kbd.h"
#include "compose.h"

//////////////////////////////////////////////////////////////////////////////
//
// Timing. The emulator applies gamepad changes whenever they arrive, and a
// program reads them once per frame (16.7 ms), so every event has to stay
// unchanged for longer than a frame or it could be missed.
//
// Each key event is sent in 2 steps: first the key code and action, and
// SETTLE_MS later the strobe. That way, even if a report was ever applied
// across 2 frames, the strobe can't be seen before its key code.
//
#define SETTLE_MS       8    // from key code to strobe
#define HOLD_MS        34    // from strobe to the next event (2 frames)
#define QUEUE_SIZE    128    // pending key events (power of 2)

//////////////////////////////////////////////////////////////////////////////
//
// Setup mode, toggled with the Scroll Lock key. In this mode keys F1 to
// F11 are plain buttons 1 to 11, held for as long as the key is. This is
// only meant for creating the joystick profile in Vircon32's EditControls,
// which asks to press each control on its own.
//
static bool setup_mode = false;

//////////////////////////////////////////////////////////////////////////////
//
// Queue of pending key events. A typed character (extended characters, see
// v32kbd.h) takes 2 events, a codepage prefix and its payload: the payload
// is marked as "continued" so that the pair is never split when old events
// are discarded
//
typedef struct
{
    uint8_t code;
    bool    pressed;
    bool    continued;    // payload of the codepage prefix before it
}
key_event_t;

static key_event_t queue[ QUEUE_SIZE ];
static unsigned    queue_head = 0;      // next to read
static unsigned    queue_tail = 0;      // next to write

// discards the oldest event (with its payload, if it has one)
static void queue_discard_oldest( void )
{
    queue_head = (queue_head + 1) & (QUEUE_SIZE - 1);

    while( queue_head != queue_tail && queue[ queue_head ].continued )
      queue_head = (queue_head + 1) & (QUEUE_SIZE - 1);
}

static void queue_push_raw( uint8_t code, bool pressed, bool continued )
{
    queue[ queue_tail ].code      = code;
    queue[ queue_tail ].pressed   = pressed;
    queue[ queue_tail ].continued = continued;
    queue_tail = (queue_tail + 1) & (QUEUE_SIZE - 1);
}

// The event at the head of the queue may be the payload of a prefix that was
// already sent: it must be sent too, or the program would take the next event
// for its payload. Then the oldest event can't be discarded, and the new one
// is what is lost instead (all this only happens with a full queue)
static bool queue_make_room( unsigned needed )
{
    while( ((queue_tail + needed) & (QUEUE_SIZE - 1)) == queue_head
        || ((queue_tail + 1)      & (QUEUE_SIZE - 1)) == queue_head )
    {
        if( queue_head != queue_tail && queue[ queue_head ].continued )
          return false;

        queue_discard_oldest();
    }

    return true;
}

static void queue_push( uint8_t code, bool pressed )
{
    if( queue_make_room( 1 ) )
      queue_push_raw( code, pressed, false );
}

// a typed character, as a BIOS font code: codepage prefix, then payload. Both
// are sent as key RELEASES so that libraries that predate the extension
// ignore them (see keyboard.h)
static void queue_push_char( uint8_t character )
{
    bool high = (character >= 128);

    // room for both (or none is queued)
    if( !queue_make_room( 2 ) )
      return;

    queue_push_raw( high? V32KEY_PAGE_HIGH : V32KEY_PAGE_LOW, false, false );
    queue_push_raw( (uint8_t)( character & 0x7F ),            false, true  );
}

static bool queue_pop( key_event_t* event )
{
    if( queue_head == queue_tail )
      return false;

    *event = queue[ queue_head ];
    queue_head = (queue_head + 1) & (QUEUE_SIZE - 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// Gamepad state
//
enum { IDLE, SETTLING, HOLDING };
static int      protocol_state = IDLE;
static uint16_t buttons        = 0;     // state we want the PC to see

//////////////////////////////////////////////////////////////////////////////
//
// Leaves everything as when just powered: no buttons pressed, no pending
// events, and the strobe restarting from Left
//
static void protocol_reset( void )
{
    buttons        = 0;
    queue_head     = queue_tail;
    protocol_state = IDLE;
}

void kbd_reset( void )
{
    setup_mode = false;
    compose_reset();
    protocol_reset();
}

bool kbd_setup_mode( void )
{
    return setup_mode;
}

//////////////////////////////////////////////////////////////////////////////
//
// Handling of key changes coming from the keyboard
//
static void key_changed( uint8_t usage, bool pressed )
{
    // scroll lock toggles setup mode
    if( usage == HID_KEY_SCROLL_LOCK )
    {
        if( pressed )
        {
            setup_mode = !setup_mode;
            protocol_reset();
        }

        return;
    }

    if( setup_mode )
    {
        if( usage >= HID_KEY_F1 && usage < HID_KEY_F1 + V32BTN_COUNT )
        {
            uint16_t mask = (uint16_t)( 1u << (usage - HID_KEY_F1) );

            if( pressed ) buttons |=  mask;
            else          buttons &= (uint16_t)~mask;
        }

        return;
    }

    // the compose layer decides what this key change is: an ordinary key
    // event, typed characters (AltGr, dead keys, Alt codes), or nothing
    compose_out_t out;
    compose_key( usage, pressed, &out );

    for( int i = 0; i < out.count; i++ )
    {
        if( out.act[i].kind == COMPOSE_CHAR )
          queue_push_char( out.act[i].code );

        else
        {
            uint8_t code = v32kbd_keycode( usage );

            if( code != V32KEY_NONE )
              queue_push( code, pressed );
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Turns pending key events into gamepad states, one at a time
//
uint16_t kbd_task( void )
{
    static uint32_t since = 0;
    uint32_t now = v32io_millis();
    #define state protocol_state

    if( setup_mode )
    {
        state = IDLE;
        return buttons;
    }

    switch( state )
    {
        case IDLE:
        {
            key_event_t event;

            if( !queue_pop( &event ) )
              break;

            // step 1: key code and action, strobe is not changed
            uint16_t strobe = buttons & (V32BTN_LEFT | V32BTN_RIGHT);

            buttons = (uint16_t)( strobe
                    | (event.pressed? V32BTN_UP : V32BTN_DOWN)
                    | ((uint16_t)event.code << V32BTN_CODE_SHIFT) );

            state = SETTLING;
            since = now;
            break;
        }

        case SETTLING:
        {
            // wait until step 1 was actually sent, plus the settle time
            if( !v32io_delivered( buttons ) )
            {
                since = now;
                break;
            }

            if( now - since < SETTLE_MS )
              break;

            // step 2: switch the strobe side (first event is Left)
            if( buttons & V32BTN_LEFT )
              buttons = (uint16_t)( (buttons & ~V32BTN_LEFT) | V32BTN_RIGHT );
            else
              buttons = (uint16_t)( (buttons & ~V32BTN_RIGHT) | V32BTN_LEFT );

            state = HOLDING;
            since = now;
            break;
        }

        case HOLDING:
        {
            if( !v32io_delivered( buttons ) )
            {
                since = now;
                break;
            }

            if( now - since >= HOLD_MS )
              state = IDLE;

            break;
        }
    }

    #undef state
    return buttons;
}

//////////////////////////////////////////////////////////////////////////////
//
// Keyboard reports (boot protocol: modifiers plus up to 6 keys)
//
static bool report_has_key( const hid_keyboard_report_t* report, uint8_t usage )
{
    for( int i = 0; i < 6; i++ )
      if( report->keycode[i] == usage )
        return true;

    return false;
}

void kbd_process_report( hid_keyboard_report_t* last, const hid_keyboard_report_t* report, bool active )
{
    // when too many keys are pressed keyboards report an
    // error in all positions: ignore those reports
    if( report->keycode[0] == 1 )
      return;

    if( active )
    {
        // modifier keys: 8 bits that are usages 0xE0 to 0xE7. Process
        // them first so that shift arrives before the key it applies to
        uint8_t changed = report->modifier ^ last->modifier;

        for( int bit = 0; bit < 8; bit++ )
          if( changed & (1 << bit) )
            key_changed( (uint8_t)( HID_KEY_CONTROL_LEFT + bit ), report->modifier & (1 << bit) );

        // released keys: they were in the last report but not in this one
        for( int i = 0; i < 6; i++ )
        {
            uint8_t usage = last->keycode[i];

            if( usage > 3 && !report_has_key( report, usage ) )
              key_changed( usage, false );
        }

        // pressed keys: they are in this report but not in the last one
        for( int i = 0; i < 6; i++ )
        {
            uint8_t usage = report->keycode[i];

            if( usage > 3 && !report_has_key( last, usage ) )
              key_changed( usage, true );
        }
    }

    *last = *report;
}
