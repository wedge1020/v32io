//////////////////////////////////////////////////////////////////////////////
//
// Compose layer: AltGr characters, dead keys and Alt codes. See compose.h
// (what it does) and docs/COMPOSE.md (how to type each character).
//
// This file is only logic, with no hardware: it is also built for the PC
// by v32io-tests, which checks it against the console running a program.
//
//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "tusb.h"
#include "compose.h"
#include "compose_table.h"

#define ARRAY_COUNT( a )  ( sizeof(a) / sizeof((a)[0]) )

//////////////////////////////////////////////////////////////////////////////
//
// State
//
static bool    shift_left   = false;
static bool    shift_right  = false;
static bool    caps_lock    = false;     // toggles with each press, like keyboard.h
static bool    altgr        = false;     // right Alt
static bool    alt_left     = false;     // left Alt (Alt codes)

static uint8_t dead_key     = DEAD_NONE; // dead key waiting for its letter

// key presses that were turned into characters: their release must not be
// reported as a key either, whatever the order the modifiers are let go in
static bool    consumed[256];

// Alt code being typed
#define ALTCODE_MAX_DIGITS   4
#define ALTCODE_OVERFLOW   256           // anything over 255 can't be a code
static int     altcode_digits = 0;
static int     altcode_value  = 0;

//////////////////////////////////////////////////////////////////////////////
//
// Output helpers
//
static void emit_key( compose_out_t* out )
{
    out->act[ out->count ].kind = COMPOSE_KEY;
    out->act[ out->count ].code = 0;
    out->count++;
}

static void emit_char( compose_out_t* out, uint8_t code )
{
    out->act[ out->count ].kind = COMPOSE_CHAR;
    out->act[ out->count ].code = code;
    out->count++;
}

//////////////////////////////////////////////////////////////////////////////
//
// Public
//
void compose_reset( void )
{
    shift_left     = false;
    shift_right    = false;
    caps_lock      = false;
    altgr          = false;
    alt_left       = false;
    dead_key       = DEAD_NONE;
    altcode_digits = 0;
    altcode_value  = 0;
    memset( consumed, 0, sizeof(consumed) );
}

bool compose_valid_char( uint8_t code )
{
    return ( compose_valid[ code / 32 ] >> (code % 32) ) & 1u;
}

//////////////////////////////////////////////////////////////////////////////
//
// Table lookups
//
static bool lookup_pair( uint8_t dead, char base, uint8_t* code )
{
    for( size_t i = 0; i < ARRAY_COUNT( compose_pairs ); i++ )
      if( compose_pairs[i].dead == dead && compose_pairs[i].base == base )
      {
          *code = compose_pairs[i].code;
          return true;
      }

    return false;
}

static bool lookup_altgr( uint8_t usage, bool shift, uint8_t* code )
{
    for( size_t i = 0; i < ARRAY_COUNT( compose_altgr ); i++ )
      if( compose_altgr[i].usage == usage )
      {
          *code = shift? compose_altgr[i].code_shifted : compose_altgr[i].code;
          return true;
      }

    return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// Main entry
//
void compose_key( uint8_t usage, bool pressed, compose_out_t* out )
{
    bool    shift = shift_left || shift_right;
    uint8_t code;

    out->count = 0;

    //////////////////////////////////////////////////////////////////////////
    //
    // Modifiers: always reported, and they keep the state up to date
    //
    switch( usage )
    {
        case HID_KEY_SHIFT_LEFT:   shift_left  = pressed;  emit_key( out );  return;
        case HID_KEY_SHIFT_RIGHT:  shift_right = pressed;  emit_key( out );  return;
        case HID_KEY_ALT_RIGHT:    altgr       = pressed;  emit_key( out );  return;

        case HID_KEY_CAPS_LOCK:
            if( pressed ) caps_lock = !caps_lock;
            emit_key( out );
            return;

        case HID_KEY_ALT_LEFT:
            alt_left = pressed;
            emit_key( out );

            // starting an Alt code, or finishing it: the character is typed
            // when Alt is released, after the key event for that
            if( pressed )
            {
                altcode_digits = 0;
                altcode_value  = 0;
            }

            else
            {
                if( altcode_digits > 0 && altcode_value < ALTCODE_OVERFLOW
                    && compose_valid_char( (uint8_t) altcode_value ) )
                  emit_char( out, (uint8_t) altcode_value );

                altcode_digits = 0;
                altcode_value  = 0;
            }

            return;

        default:
            break;
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Releases: a key that was turned into a character is not reported
    //
    if( !pressed )
    {
        if( consumed[ usage ] )
          consumed[ usage ] = false;
        else
          emit_key( out );

        return;
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Alt code digits: numeric keypad while the left Alt key is held
    //
    if( alt_left && usage >= HID_KEY_KEYPAD_1 && usage <= HID_KEY_KEYPAD_0 )
    {
        int digit = (usage == HID_KEY_KEYPAD_0)? 0 : (usage - HID_KEY_KEYPAD_1 + 1);

        // a 5th digit changes nothing (so the code stays what it was)
        if( altcode_digits < ALTCODE_MAX_DIGITS )
        {
            altcode_digits++;
            altcode_value = altcode_value * 10 + digit;

            if( altcode_value > 255 )
              altcode_value = ALTCODE_OVERFLOW;
        }

        consumed[ usage ] = true;
        return;
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // A dead key is waiting for the key that goes under it
    //
    if( dead_key != DEAD_NONE )
    {
        uint8_t dead = dead_key;
        dead_key = DEAD_NONE;

        // a letter: combine, when the font has that accented letter
        if( usage >= HID_KEY_A && usage <= HID_KEY_Z )
        {
            char base = (char)( 'a' + (usage - HID_KEY_A) );

            if( shift != caps_lock )
              base = (char)( base - 32 );

            if( lookup_pair( dead, base, &code ) )
            {
                emit_char( out, code );
                consumed[ usage ] = true;
                return;
            }
        }

        // space (or the same dead key again): the accent itself, alone
        bool same = altgr && (
              (dead == DEAD_ACUTE      && usage == HID_KEY_APOSTROPHE && !shift)
           || (dead == DEAD_DIAERESIS  && usage == HID_KEY_APOSTROPHE &&  shift)
           || (dead == DEAD_GRAVE      && usage == HID_KEY_GRAVE      && !shift)
           || (dead == DEAD_TILDE      && usage == HID_KEY_GRAVE      &&  shift)
           || (dead == DEAD_CIRCUMFLEX && usage == HID_KEY_6          &&  shift) );

        if( usage == HID_KEY_SPACE || same )
        {
            emit_char( out, compose_dead_char[ dead ] );
            consumed[ usage ] = true;
            return;
        }

        // anything else: the accent, then that key as it would be alone
        emit_char( out, compose_dead_char[ dead ] );
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // AltGr: dead keys and direct characters
    //
    if( altgr )
    {
        uint8_t new_dead = DEAD_NONE;

        if( usage == HID_KEY_APOSTROPHE )
          new_dead = shift? DEAD_DIAERESIS : DEAD_ACUTE;

        else if( usage == HID_KEY_GRAVE )
          new_dead = shift? DEAD_TILDE : DEAD_GRAVE;

        else if( usage == HID_KEY_6 && shift )
          new_dead = DEAD_CIRCUMFLEX;

        if( new_dead != DEAD_NONE )
        {
            dead_key = new_dead;
            consumed[ usage ] = true;
            return;
        }

        if( lookup_altgr( usage, shift, &code ) )
        {
            emit_char( out, code );
            consumed[ usage ] = true;
            return;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Nothing special: an ordinary key
    //
    emit_key( out );
}
