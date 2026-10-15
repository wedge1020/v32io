#ifndef COMPOSE_H_
#define COMPOSE_H_

#include <stdint.h>
#include <stdbool.h>

//////////////////////////////////////////////////////////////////////////////
//
// Compose layer (keyboard mode only): types the characters of the Vircon32
// BIOS font that a US keyboard doesn't have. The keyboard only tells which
// KEY was pressed, never what the computer's layout would make of it, so
// the adapter has its own ways to type them (all described in docs/v32io/COMPOSE.md):
//
//   AltGr (right Alt) + key       direct characters: ä é ß € ¿ ...
//   AltGr + dead key, then letter accented letters: á à â ä ã
//   left Alt + keypad digits      Alt codes: any character, by its code
//
// What is typed is sent to the program as a typed CHARACTER (see the
// extended characters in v32kbd.h), and the keys that typed it are not
// reported as keys. Everything else, including the Alt keys themselves,
// is reported as keys as always.
//
//////////////////////////////////////////////////////////////////////////////

// what to do with a key change: a list of actions, in order
enum
{
    COMPOSE_KEY  = 0,   // report the key change as a key event, as it came
    COMPOSE_CHAR = 1    // type character `code` (a BIOS font code)
};

typedef struct
{
    uint8_t kind;
    uint8_t code;
}
compose_act_t;

typedef struct
{
    compose_act_t act[3];
    uint8_t       count;    // 0: nothing at all (the key is consumed)
}
compose_out_t;

// forget everything: held modifiers, pending dead key, Alt code
void compose_reset( void );

// processes one key change (HID keyboard usage). Call it for EVERY key and
// modifier change, in the order they happen (modifiers keep it up to date)
void compose_key( uint8_t usage, bool pressed, compose_out_t* out );

// can this BIOS font code be typed (does the font have a glyph for it)?
bool compose_valid_char( uint8_t code );

#endif
