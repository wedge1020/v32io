// Unit test of the firmware's compose layer (compose.c): every entry of its
// tables, dead key behaviors and Alt codes, against what they must type.
// No console needed; the end-to-end test (harness) then sends the same
// kind of key sequences through kbd.c to a program running on the console.

#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "compose.h"
#include "compose_table.h"

static int fails = 0;
static int checks = 0;

#define CHECK( cond, ... ) do { checks++; if( !(cond) ) { fails++; printf( "FAIL: " __VA_ARGS__ ); printf( "\n" ); } } while(0)

static compose_out_t press( uint8_t usage )   { compose_out_t o; compose_key( usage, true,  &o ); return o; }
static compose_out_t release( uint8_t usage ) { compose_out_t o; compose_key( usage, false, &o ); return o; }

static bool is_char( compose_out_t o, int index, uint8_t code )
{
    return o.count > index && o.act[index].kind == COMPOSE_CHAR && o.act[index].code == code;
}

static bool is_key( compose_out_t o, int index )
{
    return o.count > index && o.act[index].kind == COMPOSE_KEY;
}

static void clear( void )
{
    compose_reset();
}

int main( void )
{
    compose_out_t o;

    // -- AltGr + key: every table entry, plain and shifted
    for( size_t i = 0; i < sizeof(compose_altgr) / sizeof(compose_altgr[0]); i++ )
    {
        const compose_altgr_t* e = &compose_altgr[i];

        for( int shifted = 0; shifted < 2; shifted++ )
        {
            // AltGr + Shift + 6 is the circumflex dead key
            if( e->usage == HID_KEY_6 && shifted ) continue;

            clear();
            o = press( HID_KEY_ALT_RIGHT );          CHECK( o.count == 1 && is_key( o, 0 ), "altgr press is a key" );
            if( shifted ) press( HID_KEY_SHIFT_LEFT );
            uint8_t want = shifted? e->code_shifted : e->code;
            o = press( e->usage );
            CHECK( o.count == 1 && is_char( o, 0, want ), "AltGr%s+usage 0x%02X types 0x%02X (got count=%d)", shifted? "+Shift" : "", e->usage, want, o.count );
            CHECK( compose_valid_char( want ), "0x%02X is in the BIOS font", want );
            o = release( e->usage );                 CHECK( o.count == 0, "release of a typed key is consumed" );
            if( shifted ) release( HID_KEY_SHIFT_LEFT );
            o = release( HID_KEY_ALT_RIGHT );        CHECK( o.count == 1 && is_key( o, 0 ), "altgr release is a key" );
        }
    }

    // -- AltGr + a key with no entry is an ordinary key; no AltGr is ordinary too
    clear();
    press( HID_KEY_ALT_RIGHT );
    o = press( HID_KEY_J );     CHECK( o.count == 1 && is_key( o, 0 ), "AltGr+j is just a key" );
    o = release( HID_KEY_J );   CHECK( o.count == 1 && is_key( o, 0 ), "its release too" );
    release( HID_KEY_ALT_RIGHT );
    o = press( HID_KEY_E );     CHECK( o.count == 1 && is_key( o, 0 ), "e alone is a key" );
    o = release( HID_KEY_E );   CHECK( o.count == 1 && is_key( o, 0 ), "e release" );

    // -- consumed key released after AltGr: still consumed
    clear();
    press( HID_KEY_ALT_RIGHT );
    o = press( HID_KEY_E );     CHECK( is_char( o, 0, 0xE9 ), "AltGr+e" );
    release( HID_KEY_ALT_RIGHT );
    o = release( HID_KEY_E );   CHECK( o.count == 0, "e released after AltGr is still consumed" );
    o = press( HID_KEY_E );     CHECK( o.count == 1 && is_key( o, 0 ), "later e is a key again" );
    release( HID_KEY_E );

    // -- dead keys: every pair, with each way of typing the dead key
    struct { uint8_t usage; bool shift; uint8_t dead; } deadkeys[] =
    {
        { HID_KEY_APOSTROPHE, false, DEAD_ACUTE },      { HID_KEY_APOSTROPHE, true, DEAD_DIAERESIS },
        { HID_KEY_GRAVE,      false, DEAD_GRAVE },      { HID_KEY_GRAVE,      true, DEAD_TILDE },
        { HID_KEY_6,          true,  DEAD_CIRCUMFLEX },
    };

    for( size_t k = 0; k < sizeof(deadkeys) / sizeof(deadkeys[0]); k++ )
    {
        for( size_t i = 0; i < sizeof(compose_pairs) / sizeof(compose_pairs[0]); i++ )
        {
            if( compose_pairs[i].dead != deadkeys[k].dead ) continue;
            char base = compose_pairs[i].base;
            bool upper = (base >= 'A' && base <= 'Z');
            uint8_t letter = (uint8_t)( HID_KEY_A + ((upper? base + 32 : base) - 'a') );

            for( int caps = 0; caps < 2; caps++ )
            {
                clear();
                press( HID_KEY_ALT_RIGHT );
                if( deadkeys[k].shift ) press( HID_KEY_SHIFT_RIGHT );
                o = press( deadkeys[k].usage );
                CHECK( o.count == 0, "dead key press is consumed" );
                if( deadkeys[k].shift ) release( HID_KEY_SHIFT_RIGHT );
                o = release( deadkeys[k].usage );
                CHECK( o.count == 0, "dead key release is consumed" );
                release( HID_KEY_ALT_RIGHT );

                // capital: shift held, or caps lock on (and not both)
                if( caps ) press( HID_KEY_CAPS_LOCK );
                bool want_shift = upper != (bool) caps;
                if( want_shift ) press( HID_KEY_SHIFT_LEFT );
                o = press( letter );
                CHECK( o.count == 1 && is_char( o, 0, compose_pairs[i].code ),
                       "dead %d + '%c' (caps %d) types 0x%02X", deadkeys[k].dead, base, caps, compose_pairs[i].code );
                CHECK( compose_valid_char( compose_pairs[i].code ), "pair code in font" );
                o = release( letter );  CHECK( o.count == 0, "letter release consumed" );
                if( want_shift ) release( HID_KEY_SHIFT_LEFT );

                // the dead key was used up: the next letter is just a key
                o = press( letter );    CHECK( o.count == 1 && is_key( o, 0 ), "dead key is used up" );
            }
        }
    }

    // -- dead key + space, same dead key twice, dead + a letter without that accent, dead + other key
    clear();
    press( HID_KEY_ALT_RIGHT ); press( HID_KEY_APOSTROPHE ); release( HID_KEY_APOSTROPHE ); release( HID_KEY_ALT_RIGHT );
    o = press( HID_KEY_SPACE );
    CHECK( o.count == 1 && is_char( o, 0, 0xB4 ), "acute + space types the accent" );
    o = release( HID_KEY_SPACE );  CHECK( o.count == 0, "and eats the space" );

    clear();
    press( HID_KEY_ALT_RIGHT ); press( HID_KEY_APOSTROPHE ); release( HID_KEY_APOSTROPHE );
    o = press( HID_KEY_APOSTROPHE );
    CHECK( o.count == 1 && is_char( o, 0, 0xB4 ), "acute twice types the accent" );
    release( HID_KEY_APOSTROPHE ); release( HID_KEY_ALT_RIGHT );
    o = press( HID_KEY_A );  CHECK( is_key( o, 0 ), "nothing pending after that" );
    release( HID_KEY_A );

    clear();
    press( HID_KEY_ALT_RIGHT ); press( HID_KEY_GRAVE ); release( HID_KEY_GRAVE ); release( HID_KEY_ALT_RIGHT );
    o = press( HID_KEY_X );
    CHECK( o.count == 2 && is_char( o, 0, '`' ) && is_key( o, 1 ), "grave + x: the accent, then the key" );
    o = release( HID_KEY_X );  CHECK( o.count == 1 && is_key( o, 0 ), "the key's release is reported" );

    clear();
    press( HID_KEY_ALT_RIGHT ); press( HID_KEY_APOSTROPHE ); release( HID_KEY_APOSTROPHE );
    o = press( HID_KEY_5 );    // AltGr+5 is the euro: acute, then the euro
    CHECK( o.count == 2 && is_char( o, 0, 0xB4 ) && is_char( o, 1, 0x80 ), "acute + AltGr+5: accent then euro" );
    release( HID_KEY_5 ); release( HID_KEY_ALT_RIGHT );

    clear();
    press( HID_KEY_ALT_RIGHT ); press( HID_KEY_6 );  // no shift: AltGr+6 is a quarter, not a dead key
    o = release( HID_KEY_6 );
    CHECK( o.count == 0, "AltGr+6 types 1/4" );
    release( HID_KEY_ALT_RIGHT );

    // modifiers between the dead key and its letter don't cancel it
    clear();
    press( HID_KEY_ALT_RIGHT ); press( HID_KEY_APOSTROPHE ); release( HID_KEY_APOSTROPHE ); release( HID_KEY_ALT_RIGHT );
    o = press( HID_KEY_SHIFT_LEFT );  CHECK( is_key( o, 0 ), "shift is a key" );
    o = press( HID_KEY_E );           CHECK( o.count == 1 && is_char( o, 0, 0xC9 ), "acute, shift, e -> E acute" );

    // -- Alt codes: every code from 0 to 999, with and without leading zeros
    for( int value = 0; value < 1000; value++ )
    {
        for( int zeros = 0; zeros < 2; zeros++ )
        {
            char digits[8];
            snprintf( digits, sizeof(digits), zeros? "0%d" : "%d", value );
            if( strlen( digits ) > 4 ) continue;

            clear();
            o = press( HID_KEY_ALT_LEFT );  CHECK( o.count == 1 && is_key( o, 0 ), "left alt is a key" );
            for( const char* d = digits; *d; d++ )
            {
                uint8_t usage = (*d == '0')? HID_KEY_KEYPAD_0 : (uint8_t)( HID_KEY_KEYPAD_1 + (*d - '1') );
                o = press( usage );    CHECK( o.count == 0, "keypad digit under Alt is consumed" );
                o = release( usage );  CHECK( o.count == 0, "and its release" );
            }
            o = release( HID_KEY_ALT_LEFT );

            bool typed = value <= 255 && compose_valid_char( (uint8_t) value );
            CHECK( o.count == (typed? 2 : 1) && is_key( o, 0 ), "alt release reports the key (value %d)", value );
            if( typed )
              CHECK( is_char( o, 1, (uint8_t) value ), "Alt+%s types %d", digits, value );
        }
    }

    // BIOS font facts that matter: box drawing and a few spots
    CHECK( compose_valid_char( 0x15 ) && compose_valid_char( 0x1F ) && compose_valid_char( 0x0E ), "box drawing" );
    CHECK( !compose_valid_char( 0x10 ) && !compose_valid_char( 0x7F ) && !compose_valid_char( 0xA0 ) &&
           !compose_valid_char( 0x81 ) && !compose_valid_char( 0x9D ) && !compose_valid_char( 0x00 ), "not used" );
    CHECK( compose_valid_char( 0x80 ) && compose_valid_char( 0xFF ) && compose_valid_char( 'A' ), "valid" );
    int total = 0;
    for( int c = 0; c < 256; c++ ) total += compose_valid_char( (uint8_t) c );
    CHECK( total == 95 + 17 + 128 - 6 + 0, "number of typable characters is %d", total );

    // Alt code digit that is not under Alt is a normal key; 5 digits ignore the 5th
    clear();
    o = press( HID_KEY_KEYPAD_5 );  CHECK( o.count == 1 && is_key( o, 0 ), "keypad 5 alone is a key" );
    release( HID_KEY_KEYPAD_5 );
    clear();
    press( HID_KEY_ALT_LEFT );
    uint8_t seq[] = { HID_KEY_KEYPAD_0, HID_KEY_KEYPAD_2, HID_KEY_KEYPAD_3, HID_KEY_KEYPAD_3, HID_KEY_KEYPAD_9 };
    for( int i = 0; i < 5; i++ ) { press( seq[i] ); release( seq[i] ); }
    o = release( HID_KEY_ALT_LEFT );
    CHECK( o.count == 2 && is_char( o, 1, 233 ), "a 5th digit is ignored" );

    printf( "compose: %d checks, %d failed -> %s\n", checks, fails, fails? "FAIL" : "PASS" );
    return fails != 0;
}
