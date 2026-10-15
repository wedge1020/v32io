#ifndef _KEYBOARD_H
#define _KEYBOARD_H

//////////////////////////////////////////////////////////////////////////////
//
// v32tools/v32kbd library: "keyboard.h"         File version: 2026/10/14
// ----------------------------------------------------------------------
//
// This file contains all definitions and functions needed by programs to
// access input from a keyboard-attached gamepad, allowing keyboard input
// in Vircon32 programs.
//
// Reading the gamepad itself is done by the v32io core (v32io.h); this
// library interprets the packed controls it provides as key events:
//
// BIT:   0 -> V32IO_LEFT   (INP_GamepadLeft)   \  strobe: each new key event
//        1 -> V32IO_RIGHT  (INP_GamepadRight)  /  switches sides (Left first)
//        2 -> V32IO_UP     (INP_GamepadUp)     -> key was pressed
//        3 -> V32IO_DOWN   (INP_GamepadDown)   -> key was released
//        4 -> V32IO_START  (ButtonStart)       -> key code, bit 0
//        5 -> V32IO_A      (ButtonA)           -> key code, bit 1
//        6 -> V32IO_B      (ButtonB)           -> key code, bit 2
//        7 -> V32IO_X      (ButtonX)           -> key code, bit 3
//        8 -> V32IO_Y      (ButtonY)           -> key code, bit 4
//        9 -> V32IO_L      (ButtonL)           -> key code, bit 5
//       10 -> V32IO_R      (ButtonR)           -> key code, bit 6
//
// The device reports  at most 1 key  event per frame, and  it keeps that
// state until the next  event. So a new event is  recognized by the side
// of the strobe (Left or Right) being different from the last one seen.
//
// Key codes are 7 bits, and identify KEYS (not characters): keys with an
// ASCII character report it  as typed with no shift on  a US layout ('a'
// to 'z', '0' to '9',  space and ` - = [ ] \ ; '  , . /). Other keys use
// the V32KEY_  codes defined below. Shift  and caps lock are  applied by
// this library, to obtain the typed character (symbol) for each key.
//
// EXTENDED CHARACTERS: key codes identify keys, so they can't tell what a
// key typed when that is not an ASCII character (an accented letter from
// a dead key or AltGr, a box drawing character...). For these, the device
// can send a typed CHARACTER instead, in the character set of the BIOS font
// (Windows-1252, with box drawing characters in place of codes 14 to 31).
// It takes 2 consecutive events: a codepage PREFIX, whose key code is one
// of the 2 that were left unused, and then the PAYLOAD, which carries the
// low 7 bits of the character in the key code field:
//
//   V32KEY_PAGE_HIGH (30) + payload  ->  character 128 + payload (128-255)
//   V32KEY_PAGE_LOW  (31) + payload  ->  character payload       (0-127)
//
// Both events are sent as key RELEASES, so a library that predates this
// extension sees two key releases (which its v32kbd_read() skips) instead
// of misreading the payload as a key press. This library accepts either
// action. Payload 0 is valid (it is the
// euro sign on the high page). The prefix is dropped if no payload follows
// it within V32KBD_PAGE_TIMEOUT frames.
//
// v32kbd_read() returns these characters like any other: their BIOS font
// code (128 to 255 for the high page, 32 to 126 for ASCII). For the codes
// below 32, which collide with key codes, it adds V32KBD_GLYPH: use
// v32kbd_glyph() to obtain the code to draw from any v32kbd_read() value.
//
// The design of v32kbd is such that allows for multiple instances, if it
// is desired to have more than one keyboard present.
//
// IMPORTANT: v32kbd_probe() must be called  once on EVERY frame for each
// keyboard. If frames are skipped, key events can be lost.
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//
// include the v32io core (it includes the needed DevTools headers)
//
#include "v32io.h"

//////////////////////////////////////////////////////////////////////////////
//
// key codes for the keys that have no ASCII character of their own
//
#define V32KEY_NONE         0
#define V32KEY_UP           1
#define V32KEY_DOWN         2
#define V32KEY_LEFT         3
#define V32KEY_RIGHT        4
#define V32KEY_CAPSLOCK     5
#define V32KEY_LSHIFT       6
#define V32KEY_RSHIFT       7
#define V32KEY_BACKSPACE    8
#define V32KEY_TAB          9
#define V32KEY_LCTRL       10
#define V32KEY_RCTRL       11
#define V32KEY_LALT        12  // left option on Mac
#define V32KEY_ENTER       13
#define V32KEY_F1          14  // F1 to F12 are consecutive: 14 to 25
#define V32KEY_F12         25
#define V32KEY_RALT        26  // right option on Mac
#define V32KEY_ESCAPE      27
#define V32KEY_LGUI        28  // left command on Mac, Windows key on PC
#define V32KEY_RGUI        29  // right command on Mac
#define V32KEY_PAGE_HIGH   30  // codepage prefix: BIOS characters 128-255
#define V32KEY_PAGE_LOW    31  // codepage prefix: BIOS characters 0-127
#define V32KEY_DELETE     127

//////////////////////////////////////////////////////////////////////////////
//
// meaning of the packed controls for a v32kbd device
//
#define V32KBD_STROBE       3  // V32IO_LEFT | V32IO_RIGHT
#define V32KBD_PRESSED      4  // V32IO_UP
#define V32KBD_RELEASED     8  // V32IO_DOWN
#define V32KBD_CODE_SHIFT   4  // key code starts at V32IO_START (bit 4)
#define V32KBD_CODE_MASK  127  // and it is 7 bits long

//////////////////////////////////////////////////////////////////////////////
//
// library parameters
//
#define V32KBD_KEYS       128  // number of possible key codes
#define V32KBD_MAXQUEUE    64  // max. events waiting to be read
#define V32KBD_GLYPH      256  // added by v32kbd_read() to typed characters
                               // below 32 (box drawing...): see v32kbd_glyph()
#define V32KBD_PAGE_TIMEOUT 30 // frames a codepage prefix waits for its payload

//////////////////////////////////////////////////////////////////////////////
//
// the v32key struct stores a single key event,  in the v32kbd linked list
// of input values
//
struct v32key
{
    int     value;    // key code of the key (see V32KEY_ definitions); for
                      // a typed character (text), its BIOS font code
    int     symbol;   // typed character (shift applied); same as value
                      // for keys that have no character
    bool    pressed;  // true: key was pressed, false: it was released
                      // (always true for a typed character)
    bool    text;     // true: this is a typed character sent by the device
                      // (extended characters), not a key
    v32key *next;
};

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd is the keyboard instances, bound to a gameport
//
struct v32kbd
{
    v32io  *io;        // v32io core instance reading the gamepad
    v32key *input;     // start of list (next available key input)
    v32key *data;      // end of the list (new keys appended)
    int     count;     // number of key events in the list
    int     gamepad;   // id of gamepad being used as a v32 keyboard
    int     lastread;  // contains frame of last received key event
    int     strobe;    // last strobe side seen (0: none, 1: left, 2: right)
    int     page;      // codepage prefix waiting for its payload (0: none,
                       // else V32KEY_PAGE_HIGH or V32KEY_PAGE_LOW)
    int     pageframe; // frame at which that prefix was received
    bool    capslock;  // caps lock state (toggles when it is pressed)
    bool [V32KBD_KEYS]    held;     // which keys are currently pressed
};

//////////////////////////////////////////////////////////////////////////////
//
// API: function prototypes for the functions in this library
//
v32key *v32key_newkey    (int      keyval);                // allocate new v32key
v32kbd *v32kbd_init      (int      gamepad);               // initialize keyboard
void    v32kbd_free      (v32kbd **keyboard);              // release a keyboard
int     v32kbd_addkey    (v32kbd **keyboard, v32key *key); // add new key to list
v32key *v32kbd_getkey    (v32kbd **keyboard);              // obtain next key
int     v32kbd_scan      (v32kbd  *keyboard);              // read controls now
int     v32kbd_symbol    (v32kbd  *keyboard, int keyval);  // apply shift to key
bool    v32kbd_probe     (v32kbd **keyboard);              // check for input
int     v32kbd_read      (v32kbd **keyboard);              // read next key press
int     v32kbd_readevent (v32kbd **keyboard, bool *pressed); // read next event
bool    v32kbd_isdown    (v32kbd **keyboard, int keyval);  // is key held now?
int     v32kbd_glyph     (int      value);                 // character to draw

//////////////////////////////////////////////////////////////////////////////
//
// v32key_newkey(): allocate a new v32key for the v32kbd input list
//
v32key *v32key_newkey (int  keyval)
{
    v32key *newkey        = NULL;

    newkey                = (v32key *) malloc (sizeof (v32key) * 1);
    if (newkey           != NULL)
    {
        newkey -> value   = keyval;
        newkey -> symbol  = keyval;
        newkey -> pressed = true;
        newkey -> text    = false;
        newkey -> next    = NULL;
    }

    return (newkey);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_scan(): read the keyboard's gamepad controls right away, packed
// as bits (see v32io.h):
//
//   bit 0: left    bit 1: right    bit 2: up    bit 3: down
//   bits 4 to 10:  key code (start, a, b, x, y, l, r)
//
// This is kept for compatibility: v32kbd_probe() reads through v32io
//
int  v32kbd_scan (v32kbd *keyboard)
{
    return (v32io_scan (keyboard -> io));
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_init(): initialize new v32kbd instance (associate with gamepad)
//
v32kbd *v32kbd_init (int  gamepad)
{
    v32kbd *keyboard          = NULL;
    int     index             = 0;

    keyboard                  = (v32kbd *) malloc (sizeof (v32kbd) * 1);
    if (keyboard             != NULL)
    {
        keyboard -> input     = NULL;
        keyboard -> data      = NULL;
        keyboard -> count     = 0;
        keyboard -> gamepad   = gamepad;
        keyboard -> lastread  = -1;
        keyboard -> strobe    = 0;
        keyboard -> page      = 0;
        keyboard -> pageframe = 0;
        keyboard -> capslock  = false;

        for (index            = 0;
             index           <  V32KBD_KEYS;
             index            = index + 1)
        {
            keyboard -> held[index]  = false;
        }

        //////////////////////////////////////////////////////////////////////
        //
        // The v32io core does the reading of the gamepad
        //
        keyboard -> io        = v32io_init (gamepad);
        if (keyboard -> io   == NULL)
        {
            free (keyboard);
            keyboard          = NULL;
        }

        //////////////////////////////////////////////////////////////////////
        //
        // The device keeps the state of its last event (even if the
        // console is reset), so take the current strobe side as our
        // starting point: this way an old event is not read as new
        //
        else
        {
            keyboard -> strobe  = v32io_read (&(keyboard -> io)) & V32KBD_STROBE;
        }
    }

    return (keyboard);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_free(): release a keyboard instance and any pending key events
//
void    v32kbd_free (v32kbd **keyboard)
{
    v32key *key               = NULL;

    if (*keyboard            != NULL)
    {
        key                   = v32kbd_getkey (keyboard);
        while (key           != NULL)
        {
            free (key);
            key               = v32kbd_getkey (keyboard);
        }

        v32io_free (&((*keyboard) -> io));
        free (*keyboard);
        *keyboard             = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_addkey(): add new key input to keyboard list
//
int     v32kbd_addkey (v32kbd **keyboard, v32key *key)
{
    v32key *tmp                   = NULL;
    int     result                = 0;

    if ((*keyboard               != NULL) &&
        (key                     != NULL))
    {
        key             -> next   = NULL;
        if ((*keyboard) -> input == NULL)
        {
            (*keyboard) -> input  = key;
            (*keyboard) -> data   = key;
        }
        else
        {
            tmp                   = (*keyboard) -> data;
            tmp         -> next   = key;
            (*keyboard) -> data   = key;
        }
        (*keyboard)     -> count  = (*keyboard) -> count + 1;
        result                    = 1;
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_getkey(): obtain next key of input from the keyboard list
// (the caller becomes responsible of calling free() on the key)
//
v32key *v32kbd_getkey (v32kbd **keyboard)
{
    v32key *nextkey                  = NULL;

    if (*keyboard                   != NULL)
    {
        if ((*keyboard) -> input    != NULL)
        {
            nextkey                  = (*keyboard) -> input;
            (*keyboard) -> input     = nextkey     -> next;
            (*keyboard) -> count     = (*keyboard) -> count - 1;
            nextkey  -> next         = NULL;
            if ((*keyboard) -> input == NULL)
            {
                (*keyboard) -> data  = NULL;
            }
        }
    }

    return (nextkey);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_symbol(): obtain the typed character for a key,  applying  the
// current state of shift and caps lock (US layout).  Keys that have  no
// character return their same key code
//
int  v32kbd_symbol (v32kbd *keyboard, int  keyval)
{
    bool  shift          = false;
    int   symbol         = keyval;

    shift                = keyboard -> held[V32KEY_LSHIFT] ||
                           keyboard -> held[V32KEY_RSHIFT];

    //////////////////////////////////////////////////////////////////////////
    //
    // Letters: caps lock inverts the effect of shift
    //
    if ((keyval         >= 'a') &&
        (keyval         <= 'z'))
    {
        if (shift       != keyboard -> capslock)
        {
            symbol       = keyval - 32;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Numbers and punctuation: only shift is applied
    //
    else if (shift      == true)
    {
        switch (keyval)
        {
            case '1':  symbol = '!';  break;
            case '2':  symbol = '@';  break;
            case '3':  symbol = '#';  break;
            case '4':  symbol = '$';  break;
            case '5':  symbol = '%';  break;
            case '6':  symbol = '^';  break;
            case '7':  symbol = '&';  break;
            case '8':  symbol = '*';  break;
            case '9':  symbol = '(';  break;
            case '0':  symbol = ')';  break;
            case '-':  symbol = '_';  break;
            case '=':  symbol = '+';  break;
            case '[':  symbol = '{';  break;
            case ']':  symbol = '}';  break;
            case '\\': symbol = '|';  break;
            case ';':  symbol = ':';  break;
            case '\'': symbol = '"';  break;
            case ',':  symbol = '<';  break;
            case '.':  symbol = '>';  break;
            case '/':  symbol = '?';  break;
            case '`':  symbol = '~';  break;
        }
    }

    return (symbol);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_probe(): check for new input, updating keyboard input list.
// Call it once every frame; it returns true if a key event was received
//
bool v32kbd_probe (v32kbd **keyboard)
{
    //////////////////////////////////////////////////////////////////////////
    //
    // Declare and initialize local variables
    //
    bool     result                = false;
    bool     handled               = false;
    int      keyval                = 0;
    int      character             = 0;
    int      frame                 = 0;
    int      raw                   = 0;
    int      side                  = 0;
    v32kbd  *kbd                   = NULL;
    v32key  *key                   = NULL;

    //////////////////////////////////////////////////////////////////////////
    //
    // Only process for an established keyboard instance
    //
    if (*keyboard                  != NULL)
    {
        kbd                         = *keyboard;

        //////////////////////////////////////////////////////////////////////
        //
        // Have the v32io core read the gamepad, and get its packed
        // controls
        //
        v32io_probe (&(kbd -> io));
        raw                         = v32io_read (&(kbd -> io));

        //////////////////////////////////////////////////////////////////////
        //
        // A gamepad that has just been connected shows every control
        // released, so the history of the device is lost: the strobe
        // starts over (the first event will be a new one, whatever side it
        // is) and a codepage prefix that was waiting won't get its payload
        //
        if (v32io_plugged (&(kbd -> io)))
        {
            kbd -> strobe           = 0;
            kbd -> page             = 0;
        }

        //////////////////////////////////////////////////////////////////////
        //
        // There is a new key event when the strobe side has changed
        // (side is 1 for left,  2 for right,  0 before any events)
        //
        side                        = raw & V32KBD_STROBE;
        if ((side                  != 0) &&
            (side                  != kbd -> strobe))
        {
            kbd -> strobe           = side;
            frame                   = get_frame_counter ();
            kbd -> lastread         = frame;
            keyval                  = (raw >> V32KBD_CODE_SHIFT) & V32KBD_CODE_MASK;

            //////////////////////////////////////////////////////////////////
            //
            // A codepage prefix is waiting: this event is its payload (any
            // key code, including 0 and the prefix codes themselves, and
            // whatever the action). A prefix that has waited too long was
            // lost with its payload: forget it and read this event as a
            // regular one
            //
            if (kbd -> page        != 0)
            {
                if ((frame - kbd -> pageframe) <= V32KBD_PAGE_TIMEOUT)
                {
                    handled         = true;
                    result          = true;
                    character       = keyval;
                    if (kbd -> page == V32KEY_PAGE_HIGH)
                    {
                        character   = keyval + 128;
                    }
                    kbd -> page     = 0;

                    //////////////////////////////////////////////////////////
                    //
                    // Queue the character (character 0 is nothing), unless
                    // nobody is reading them
                    //
                    if ((character >  0) &&
                        (kbd -> count < V32KBD_MAXQUEUE))
                    {
                        key         = v32key_newkey (character);
                        if (key    != NULL)
                        {
                            key -> text  = true;
                            v32kbd_addkey (keyboard, key);
                        }
                    }
                }
                else
                {
                    kbd -> page     = 0;
                }
            }

            //////////////////////////////////////////////////////////////////
            //
            // Codepage prefixes carry no key: they announce that the next
            // event is a character
            //
            if ((handled          == false) &&
                ((keyval          == V32KEY_PAGE_HIGH) ||
                 (keyval          == V32KEY_PAGE_LOW)))
            {
                handled             = true;
                kbd -> page         = keyval;
                kbd -> pageframe    = frame;
            }

            if ((handled          == false) &&
                (keyval           >  0))
            {
                result              = true;

                //////////////////////////////////////////////////////////////
                //
                // Keep track of which keys are held (up: key pressed)
                //
                kbd -> held[keyval] = ((raw & V32KBD_PRESSED) != 0);

                if ((keyval        == V32KEY_CAPSLOCK) &&
                    (kbd -> held[keyval]))
                {
                    kbd -> capslock = !(kbd -> capslock);
                }

                //////////////////////////////////////////////////////////////
                //
                // Queue the event, unless nobody is reading them
                //
                if (kbd -> count   <  V32KBD_MAXQUEUE)
                {
                    key             = v32key_newkey (keyval);
                    if (key        != NULL)
                    {
                        key -> pressed  = kbd -> held[keyval];
                        key -> symbol   = v32kbd_symbol (kbd, keyval);
                        v32kbd_addkey (keyboard, key);
                    }
                }
            }
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_readevent(): obtain next key event from keyboard input list (if
// available). Returns the key code (not the symbol), or 0 if there is no
// event. If pressed is not NULL, it is set to true (press) or false
// (release). Typed characters (extended characters) are not keys, so they
// are skipped; read them with v32kbd_read() or v32kbd_getkey()
//
int  v32kbd_readevent (v32kbd **keyboard, bool *pressed)
{
    int     keyval                = 0;
    v32key *oldkey                = NULL;

    if (*keyboard                != NULL)
    {
        oldkey                    = v32kbd_getkey (keyboard);
        while ((oldkey           != NULL) &&
               (oldkey -> text))
        {
            free (oldkey);
            oldkey                = v32kbd_getkey (keyboard);
        }

        if (oldkey               != NULL)
        {
            keyval                = oldkey   -> value;
            if (pressed          != NULL)
            {
                *pressed          = oldkey   -> pressed;
            }
            free (oldkey);
            oldkey                = NULL;
        }
    }

    return (keyval);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_read(): obtain next key PRESS from keyboard input list, skipping
// any key releases. Returns the typed character (compatible with the BIOS
// font regions) or, for keys with no character, their key code. Typed
// characters sent by the device (extended characters) are returned too:
// as their BIOS font code, or for codes below 32 (which are also key codes)
// that code plus V32KBD_GLYPH. Use v32kbd_glyph() to turn any returned
// value into the code to draw. Returns 0 when there is nothing left to read
//
int  v32kbd_read  (v32kbd **keyboard)
{
    int     keyval                = 0;
    v32key *oldkey                = NULL;

    if (*keyboard                != NULL)
    {
        oldkey                    = v32kbd_getkey (keyboard);
        while ((oldkey           != NULL) &&
               (keyval           == 0))
        {
            if (oldkey -> pressed)
            {
                keyval            = oldkey   -> symbol;
                if ((oldkey -> text) &&
                    (keyval      <  32))
                {
                    keyval        = keyval + V32KBD_GLYPH;
                }
            }
            free (oldkey);
            oldkey                = NULL;

            if (keyval           == 0)
            {
                oldkey            = v32kbd_getkey (keyboard);
            }
        }
    }

    return (keyval);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_isdown(): check if a key is currently held (use key codes here,
// like 'a' or V32KEY_LEFT). Useful for modifiers and for game controls
//
bool v32kbd_isdown (v32kbd **keyboard, int  keyval)
{
    bool    result                = false;

    if ((*keyboard               != NULL) &&
        (keyval                  >  0)    &&
        (keyval                  <  V32KBD_KEYS))
    {
        result                    = (*keyboard) -> held[keyval];
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32kbd_glyph(): from a value returned by v32kbd_read(), obtain the BIOS
// font character to draw (the region number print_at() uses), or 0 if the
// value is not a typed character: a key with no character (arrows, F keys,
// escape, delete...) or no key at all. This is what tells apart a key code
// below 32 from a box drawing character with the same code
//
int  v32kbd_glyph (int  value)
{
    int     glyph                 = 0;

    if (((value                  >= 32)  &&
         (value                  <  127)) ||
        ((value                  >= 128) &&
         (value                  <  256)))
    {
        glyph                     = value;
    }

    else if ((value              >  V32KBD_GLYPH) &&
             (value              <  (V32KBD_GLYPH + 32)))
    {
        glyph                     = value - V32KBD_GLYPH;
    }

    return (glyph);
}

#endif
