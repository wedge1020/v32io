// Headless end-to-end test: firmware modes (kbd.c / mouse.c, compiled for
// the host) -> simulated USB reports -> Vircon32 console logic (from the
// DesktopEmulator) running the test cartridges built with keyboard.h /
// mouse.h. Checks what the program decodes against what was sent.

#include "V32Console.hpp"
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>

extern "C" {
    #include "v32io.h"
    #include "v32kbd.h"
    #include "v32mouse.h"
    #include "compose.h"
}
#include "compose_table.h"

using namespace V32;
using namespace std;

// ---------------------------------------------------------------- firmware glue
static uint32_t     sim_ms       = 0;
static uint16_t     fw_sent      = 0;
static bool         fw_reported  = false;
static v32io_mode_t fw_mode      = V32IO_MODE_NONE;

extern "C" uint32_t     v32io_millis( void )               { return sim_ms; }
extern "C" bool         v32io_delivered( uint16_t b )      { return fw_reported && fw_sent == b; }
extern "C" v32io_mode_t v32io_mode( void )                 { return fw_mode; }

// ---------------------------------------------------------------- console
static void Nop0() {}
static void NopColor( GPUColor ) {}
static void NopQuad( GPUQuad& ) {}
static void NopInt( int ) {}
static void NopTex( int, void* ) {}
static void Log( const string& ) {}
static void Throw( const string& s ) { fprintf( stderr, "EXCEPTION: %s\n", s.c_str() ); throw runtime_error( s ); }

struct Sim
{
    V32Console* C = nullptr;
    mt19937     rng;
    double      frame_ms   = 1000.0 / 60.0;
    double      next_frame = 0;
    double      jitter     = 0;        // extra random ms on each frame
    double      late_p     = 0;        // probability of a frame being a whole frame late
    double      straddle_p = 0;        // probability that a report splits across a frame
    int         port       = 1;
    uint16_t    pc_state   = 0;        // what the console has been given
    vector<int> pending;               // bits of the last report not yet applied
    int         results    = -1;       // RAM address of results[]
    int         frames     = 0;
    int         straddles  = 0;

    function<uint16_t()> task;         // firmware mode task

    Sim( const char* cart, uint32_t seed ) : rng( seed + (getenv("SEED")? atoi(getenv("SEED")) * 1000 : 0) )
    {
        Callbacks::ClearScreen = NopColor;  Callbacks::DrawQuad = NopQuad;
        Callbacks::SetMultiplyColor = NopColor;  Callbacks::SetBlendingMode = NopInt;
        Callbacks::SelectTexture = NopInt;  Callbacks::LoadTexture = NopTex;
        Callbacks::UnloadCartridgeTextures = Nop0;  Callbacks::UnloadBiosTexture = Nop0;
        Callbacks::LogLine = Log;  Callbacks::ThrowException = Throw;

        C = new V32Console;
        #ifndef BIOS_PATH
        #define BIOS_PATH "StandardBios.v32"
        #endif
        C->LoadBios( BIOS_PATH );
        C->LoadCartridge( cart );
        C->SetGamepadConnection( port, true );
        C->SetPower( true );

        // run until the program has written its magic
        for( int i = 0; i < 2000 && results < 0; i++ )
        {
            C->RunNextFrame();
            auto& M = C->RAM.Memory;
            for( size_t a = 0; a + 1 < M.size(); a++ )
              if( M[a].AsBinary == 0x7E57AB1E && M[a+1].AsBinary == 0x0B5E55ED )
                { results = (int)a; break; }
        }

        if( results < 0 ) { fprintf( stderr, "program did not start\n" ); exit( 1 ); }
    }

    int32_t R( int i ) { return C->RAM.Memory[ results + i ].AsInteger; }

    void apply( int bit )
    {
        bool pressed = !( pc_state & (1u << bit) );
        pc_state ^= (uint16_t)(1u << bit);
        C->SetGamepadControl( port, (GamepadControls) bit, pressed );
    }

    void run_frame()
    {
        C->RunNextFrame();
        frames++;
        double len = frame_ms + uniform_real_distribution<double>( 0, jitter )( rng );
        if( uniform_real_distribution<double>( 0, 1 )( rng ) < late_p ) len += frame_ms;
        next_frame += len;
    }

    // one millisecond of simulated time
    void tick()
    {
        sim_ms++;

        uint16_t want = task();

        // USB: the gamepad is polled every 1 ms (sometimes a poll is
        // missed, which keeps the timing from being too regular)
        bool polled = (rng() % 4) != 0;
        if( polled && (!fw_reported || want != fw_sent) )
        {
            uint16_t changed = (uint16_t)( want ^ fw_sent );
            if( !fw_reported ) changed = (uint16_t)( want ^ pc_state );
            fw_sent = want;  fw_reported = true;

            for( int b = 0; b < 11; b++ )
              if( changed & (1u << b) ) pending.push_back( b );

            shuffle( pending.begin(), pending.end(), rng );
            on_report( want );
        }

        // events reach the console; a frame may begin among them
        if( sim_ms >= next_frame )
        {
            if( pending.size() > 1 && uniform_real_distribution<double>( 0, 1 )( rng ) < straddle_p )
            {
                size_t cut = 1 + rng() % ( pending.size() - 1 );
                for( size_t i = 0; i < cut; i++ ) apply( pending[i] );
                pending.erase( pending.begin(), pending.begin() + cut );
                straddles++;
            }
            else
            {
                for( int b : pending ) apply( b );
                pending.clear();
            }

            while( sim_ms >= next_frame ) run_frame();
        }

        for( int b : pending ) apply( b );
        pending.clear();
    }

    function<void(uint16_t)> on_report = []( uint16_t ){};
};

// ---------------------------------------------------------------- mouse ground truth
// independent decoding, straight from the table in the documentation
static const char* GRAY = "00 00 00 01 01 01 11 11 11 10 10 10";
static const char* TRIT = "-0++0--0++0-";

static int truth_position( uint16_t b, int neg, int pos, int hi, int lo )
{
    for( int p = 0; p < 12; p++ )
    {
        char g1 = GRAY[ p*3 ], g0 = GRAY[ p*3 + 1 ];
        char t  = TRIT[ p ];
        bool n = (t == '-'), q = (t == '+');
        if( (bool)(b & (1u<<neg)) == n && (bool)(b & (1u<<pos)) == q &&
            (bool)(b & (1u<<hi)) == (g1 == '1') && (bool)(b & (1u<<lo)) == (g0 == '1') )
          return p;
    }
    return -1;
}

static int wrap_delta( int a, int b ) { int d = ((b - a) % 12 + 12) % 12; return d > 6? d - 12 : d; }

static int test_mouse( const char* name, uint32_t seed, double jitter, double late_p, double straddle_p, int seconds, int speed )
{
    Sim S( "t_mouse.v32", seed );
    S.jitter = jitter;  S.late_p = late_p;  S.straddle_p = straddle_p;
    S.next_frame = sim_ms + 1;

    fw_mode = V32IO_MODE_MOUSE;  fw_reported = false;
    mouse_reset();
    S.task = []{ return mouse_task(); };

    long truth_x = 0, truth_y = 0;
    int  last_px = 1, last_py = 1, bad_encoding = 0, multi_step = 0;
    int  truth_left_presses = 0, truth_right_presses = 0, truth_middle_presses = 0;
    uint16_t prev = 0;
    long input_x = 0, input_y = 0;

    S.on_report = [&]( uint16_t b )
    {
        int px = truth_position( b, 0, 1, 7, 8 );
        int py = truth_position( b, 2, 3, 9, 10 );
        if( px < 0 || py < 0 ) { bad_encoding++; return; }
        int dx = wrap_delta( last_px, px ), dy = wrap_delta( last_py, py );
        if( abs(dx) > 1 || abs(dy) > 1 ) multi_step++;
        truth_x += dx;  truth_y += dy;  last_px = px;  last_py = py;

        // exactly one control per axis may change
        uint16_t ch = b ^ prev;
        if( __builtin_popcount( ch & 0x183 ) > 1 || __builtin_popcount( ch & 0x60C ) > 1 ) multi_step++;

        if( (b & V32BTN_A) && !(prev & V32BTN_A) ) truth_left_presses++;
        if( (b & V32BTN_B) && !(prev & V32BTN_B) ) truth_right_presses++;
        if( (b & V32BTN_START) && !(prev & V32BTN_START) ) truth_middle_presses++;
        prev = b;
    };

    // the simulated mouse: a report every 10 ms, smoothly changing velocity
    double vx = 0, vy = 0;
    uint8_t mb = 0;
    normal_distribution<double> accel( 0, speed / 6.0 );
    int end = sim_ms + seconds * 1000;

    while( (int)sim_ms < end )
    {
        if( sim_ms % 10 == 0 )
        {
            vx = vx * 0.9 + accel( S.rng );  vy = vy * 0.9 + accel( S.rng );
            vx = max( -127.0, min( 127.0, vx ) );  vy = max( -127.0, min( 127.0, vy ) );
            int dx = (int) vx, dy = (int) vy;

            // occasional button changes, never all 3 together (setup chord)
            if( S.rng() % 40 == 0 ) mb ^= 1;
            if( S.rng() % 90 == 0 ) mb ^= 2;
            if( S.rng() % 150 == 0 ) mb ^= 4;
            if( mb == 7 ) mb = 3;

            input_x += dx;  input_y += dy;
            mouse_input( mb, dx, dy );
        }
        S.tick();
    }

    // stop moving, release buttons, and let everything settle
    mouse_input( 0, 0, 0 );
    for( int i = 0; i < 1000; i++ ) S.tick();

    long prog_x = S.R(3), prog_y = S.R(4);
    bool ok = prog_x == truth_x && prog_y == truth_y && bad_encoding == 0 && multi_step == 0 && S.R(11) == 0
           && S.R(6) == truth_left_presses && S.R(8) == truth_right_presses && S.R(9) == truth_middle_presses;

    printf( "%-34s %s  frames=%d sent=(%ld,%ld) read=(%ld,%ld) maxstep/frame=%d "
            "clicks L/R/M sent=%d/%d/%d read=%d/%d/%d straddles=%d enc_err=%d input/4=(%ld,%ld)\n",
            name, ok? "PASS" : (strstr( name, "known" )? "OFF " : "FAIL"), S.R(2), truth_x, truth_y, prog_x, prog_y, S.R(10),
            truth_left_presses, truth_right_presses, truth_middle_presses, S.R(6), S.R(8), S.R(9),
            S.straddles, bad_encoding + multi_step, input_x / 4, input_y / 4 );
    return ok? 0 : 1;
}

// ---------------------------------------------------------------- keyboard
static int test_kbd( const char* name, uint32_t seed, double jitter, double straddle_p, int events_wanted, int max_gap )
{
    Sim S( "t_kbd.v32", seed );
    S.jitter = jitter;  S.straddle_p = straddle_p;
    S.next_frame = sim_ms + 1;

    fw_mode = V32IO_MODE_KBD;  fw_reported = false;
    kbd_reset();
    S.task = []{ return kbd_task(); };

    hid_keyboard_report_t last;  memset( &last, 0, sizeof(last) );
    hid_keyboard_report_t now;   memset( &now, 0, sizeof(now) );
    vector<int> expected;

    // usable keys: letters, digits, some punctuation, arrows, enter
    vector<uint8_t> keys;
    for( uint8_t u = HID_KEY_A; u <= HID_KEY_Z; u++ ) keys.push_back( u );
    for( uint8_t u = HID_KEY_1; u <= HID_KEY_0; u++ ) keys.push_back( u );
    uint8_t extra[] = { HID_KEY_SPACE, HID_KEY_ENTER, HID_KEY_BACKSPACE, HID_KEY_ARROW_UP, HID_KEY_ARROW_LEFT,
                        HID_KEY_COMMA, HID_KEY_PERIOD, HID_KEY_SLASH, HID_KEY_DELETE, HID_KEY_F5, HID_KEY_TAB };
    for( uint8_t u : extra ) keys.push_back( u );

    auto send = [&]( const hid_keyboard_report_t& r )
    {
        // expected events, in the same order the firmware produces them
        uint8_t changed = r.modifier ^ last.modifier;
        for( int bit = 0; bit < 8; bit++ )
          if( changed & (1 << bit) )
            expected.push_back( v32kbd_keycode( HID_KEY_CONTROL_LEFT + bit ) + ((r.modifier & (1 << bit))? 256 : 0) );
        for( int i = 0; i < 6; i++ )
        {
            uint8_t u = last.keycode[i]; bool still = false;
            for( int j = 0; j < 6; j++ ) if( r.keycode[j] == u ) still = true;
            if( u > 3 && !still && v32kbd_keycode( u ) ) expected.push_back( v32kbd_keycode( u ) );
        }
        for( int i = 0; i < 6; i++ )
        {
            uint8_t u = r.keycode[i]; bool was = false;
            for( int j = 0; j < 6; j++ ) if( last.keycode[j] == u ) was = true;
            if( u > 3 && !was && v32kbd_keycode( u ) ) expected.push_back( v32kbd_keycode( u ) + 256 );
        }
        kbd_process_report( &last, &r, true );
    };

    int sent_events = 0;
    uint32_t next_change = sim_ms + 50;

    while( sent_events < events_wanted )
    {
        if( sim_ms >= next_change )
        {
            // change one thing: press or release a key, or shift
            int held = 0;
            for( int i = 0; i < 6; i++ ) if( now.keycode[i] ) held++;

            int what = S.rng() % 10;
            if( what == 0 )
              now.modifier ^= KEYBOARD_MODIFIER_LEFTSHIFT;
            else if( (what < 5 && held < 3) || held == 0 )
            {
                uint8_t u = keys[ S.rng() % keys.size() ];
                bool dup = false;
                for( int i = 0; i < 6; i++ ) if( now.keycode[i] == u ) dup = true;
                if( !dup ) for( int i = 0; i < 6; i++ ) if( !now.keycode[i] ) { now.keycode[i] = u; break; }
            }
            else
            {
                for( int i = 0; i < 6; i++ ) if( now.keycode[i] ) { now.keycode[i] = 0; break; }
            }

            size_t before = expected.size();
            send( now );
            sent_events += (int)( expected.size() - before );
            next_change = sim_ms + 1 + S.rng() % max_gap;
        }
        S.tick();
    }

    // release everything and let the queue drain
    memset( &now, 0, sizeof(now) );
    send( now );
    for( int i = 0; i < 60000 && S.R(3) < (int) expected.size(); i++ ) S.tick();
    for( int i = 0; i < 500; i++ ) S.tick();

    int got = S.R(3), mismatch = -1;
    for( int i = 0; i < (int) expected.size() && i < got; i++ )
      if( S.R( 4 + i ) != expected[i] ) { mismatch = i; break; }

    bool ok = got == (int) expected.size() && mismatch < 0;
    if( mismatch >= 0 && getenv("DBG") )
      for( int i = max(0, mismatch-5); i < mismatch + 8 && i < got; i++ )
        printf( "  #%d expected %d got %d\n", i, expected[i], S.R(4+i) );
    printf( "%-34s %s  frames=%d events sent=%zu read=%d first_mismatch=%d straddles=%d\n",
            name, ok? "PASS" : "FAIL", S.R(2), expected.size(), got, mismatch, S.straddles );
    return ok? 0 : 1;
}

// ---------------------------------------------------------------- keyboard: typed characters
// Extended characters: the firmware's compose layer turns AltGr, dead keys and
// Alt codes into codepage prefix + payload events, and the library must give
// the program exactly the characters that were typed. The expected results are
// written down here from the documentation (docs/v32io/COMPOSE.md), not computed by
// the firmware code.
struct Step { uint8_t usage; bool down; };
struct Ev   { int kind; int code; };            // 0: key down, 1: key up, 2: typed character

static const int KD = 0, KU = 1, TX = 2;
static const int RALT = V32KEY_RALT, LALT = V32KEY_LALT, LSHIFT = V32KEY_LSHIFT, RSHIFT = V32KEY_RSHIFT;

static void P( vector<Step>& v, uint8_t u ) { v.push_back( { u, true } ); }
static void R( vector<Step>& v, uint8_t u ) { v.push_back( { u, false } ); }

// Alt + keypad digits, as typed: "0233"
static void altcode( vector<Step>& st, vector<Ev>& ev, const char* digits, int expected_char )
{
    P( st, HID_KEY_ALT_LEFT );  ev.push_back( { KD, LALT } );
    for( const char* d = digits; *d; d++ )
    {
        uint8_t u = (*d == '0')? HID_KEY_KEYPAD_0 : (uint8_t)( HID_KEY_KEYPAD_1 + (*d - '1') );
        P( st, u );  R( st, u );
    }
    R( st, HID_KEY_ALT_LEFT );  ev.push_back( { KU, LALT } );
    if( expected_char >= 0 ) ev.push_back( { TX, expected_char } );
}

static void scenarios( vector<Step>& st, vector<Ev>& ev )
{
    // AltGr + e
    P( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KD, RALT } );
    P( st, HID_KEY_E );  R( st, HID_KEY_E );  ev.push_back( { TX, 0xE9 } );
    R( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KU, RALT } );

    // AltGr + 5: the euro sign, payload 0 on the high page
    P( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KD, RALT } );
    P( st, HID_KEY_5 );  R( st, HID_KEY_5 );  ev.push_back( { TX, 0x80 } );
    R( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KU, RALT } );

    // AltGr + ', then e: acute accent on the e
    P( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KD, RALT } );
    P( st, HID_KEY_APOSTROPHE );  R( st, HID_KEY_APOSTROPHE );
    R( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KU, RALT } );
    P( st, HID_KEY_E );  R( st, HID_KEY_E );  ev.push_back( { TX, 0xE9 } );

    // AltGr + Shift + 6, then shift + o: O circumflex
    P( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KD, RALT } );
    P( st, HID_KEY_SHIFT_RIGHT );  ev.push_back( { KD, RSHIFT } );
    P( st, HID_KEY_6 );  R( st, HID_KEY_6 );
    R( st, HID_KEY_SHIFT_RIGHT );  ev.push_back( { KU, RSHIFT } );
    R( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KU, RALT } );
    P( st, HID_KEY_SHIFT_LEFT );  ev.push_back( { KD, LSHIFT } );
    P( st, HID_KEY_O );  R( st, HID_KEY_O );  ev.push_back( { TX, 0xD4 } );
    R( st, HID_KEY_SHIFT_LEFT );  ev.push_back( { KU, LSHIFT } );

    // grave accent then x (no such letter): the accent, then the key
    P( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KD, RALT } );
    P( st, HID_KEY_GRAVE );  R( st, HID_KEY_GRAVE );
    R( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KU, RALT } );
    P( st, HID_KEY_X );  ev.push_back( { TX, '`' } );  ev.push_back( { KD, 'x' } );
    R( st, HID_KEY_X );  ev.push_back( { KU, 'x' } );

    // Alt codes: an accented letter, box drawing, and the characters whose
    // payload is a codepage prefix code (30 and 31) on both pages
    altcode( st, ev, "0233", 0xE9 );
    altcode( st, ev, "21",   0x15 );
    altcode( st, ev, "14",   0x0E );
    altcode( st, ev, "31",   0x1F );
    altcode( st, ev, "30",   0x1E );
    altcode( st, ev, "159",  0x9F );
    altcode( st, ev, "158",  0x9E );
    altcode( st, ev, "255",  0xFF );
    altcode( st, ev, "65",   'A' );

    // codes with no glyph, or too big, type nothing
    altcode( st, ev, "0",    -1 );
    altcode( st, ev, "16",   -1 );
    altcode( st, ev, "160",  -1 );
    altcode( st, ev, "256",  -1 );

    // ordinary typing is untouched
    P( st, HID_KEY_A );  ev.push_back( { KD, 'a' } );
    R( st, HID_KEY_A );  ev.push_back( { KU, 'a' } );
    P( st, HID_KEY_ENTER );  ev.push_back( { KD, V32KEY_ENTER } );
    R( st, HID_KEY_ENTER );  ev.push_back( { KU, V32KEY_ENTER } );
}

// the values each test cartridge reports for the expected events
// mode 0: v32kbd_getkey (everything), 1: v32kbd_read (+ its glyph), 2: v32kbd_readevent (keys only)
static void expected_values( const vector<Ev>& ev, int mode, vector<int>& values, vector<int>& glyphs )
{
    for( const Ev& e : ev )
    {
        if( mode == 0 )
          values.push_back( e.kind == TX? e.code + 512 : (e.kind == KD? e.code + 256 : e.code) );

        else if( mode == 2 && e.kind != TX )
          values.push_back( e.kind == KD? e.code + 256 : e.code );

        // old library, v32kbd_read(): key presses only; typed characters are
        // sent as releases, which it skips
        else if( mode == 3 && e.kind == KD )
          values.push_back( e.code );

        // old library, v32kbd_readevent(): everything looks like a key. A typed
        // character is 2 releases: the codepage prefix, and the payload (which it
        // ignores when it is 0)
        else if( mode == 4 )
        {
            if( e.kind != TX )
              values.push_back( e.kind == KD? e.code + 256 : e.code );
            else
            {
                values.push_back( e.code >= 128? V32KEY_PAGE_HIGH : V32KEY_PAGE_LOW );
                if( (e.code & 0x7F) != 0 ) values.push_back( e.code & 0x7F );
            }
        }

        else if( mode == 1 && e.kind != KU )
        {
            if( e.kind == TX )
            {
                values.push_back( e.code < 32? e.code + 256 : e.code );
                glyphs.push_back( e.code );
            }
            else
            {
                values.push_back( e.code );
                glyphs.push_back( (e.code >= 32 && e.code < 127)? e.code : 0 );
            }
        }
    }
}

// 3 and 4: programs built with the library from before extended characters
static const char* CARTS[5] = { "t_kbd.v32", "t_kbdread.v32", "t_kbdevent.v32", "t_oldread.v32", "t_oldevent.v32" };

static void send_step( hid_keyboard_report_t& last, hid_keyboard_report_t& now, const Step& st )
{
    if( st.usage >= HID_KEY_CONTROL_LEFT )
      now.modifier ^= (uint8_t)( 1 << (st.usage - HID_KEY_CONTROL_LEFT) );

    else if( st.down )
    {
        for( int i = 0; i < 6; i++ ) if( !now.keycode[i] ) { now.keycode[i] = st.usage; break; }
    }

    else
      for( int i = 0; i < 6; i++ ) if( now.keycode[i] == st.usage ) now.keycode[i] = 0;

    kbd_process_report( &last, &now, true );
}

// runs a script of key changes through the firmware and the cartridge. With
// exact == false, the results only have to be an in-order subset of what was
// sent (events dropped because the queue overflowed, but never garbled)
static int test_kbd_script( const char* name, int mode, const vector<Step>& steps, const vector<Ev>& events,
                            uint32_t seed, double jitter, double straddle_p, int min_gap, int max_gap, bool exact = true )
{
    Sim S( CARTS[mode], seed );
    S.jitter = jitter;  S.straddle_p = straddle_p;
    S.next_frame = sim_ms + 1;

    fw_mode = V32IO_MODE_KBD;  fw_reported = false;
    kbd_reset();
    S.task = []{ return kbd_task(); };

    hid_keyboard_report_t last, now;
    memset( &last, 0, sizeof(last) );  memset( &now, 0, sizeof(now) );

    vector<int> expected, expected_glyphs;
    expected_values( events, mode, expected, expected_glyphs );

    size_t next_step = 0;
    uint32_t next_change = sim_ms + 50;

    while( next_step < steps.size() )
    {
        if( sim_ms >= next_change )
        {
            send_step( last, now, steps[ next_step++ ] );
            next_change = sim_ms + min_gap + (max_gap > min_gap? S.rng() % (max_gap - min_gap + 1) : 0);
        }
        S.tick();
    }

    for( int i = 0; i < 120000 && exact && S.R(3) < (int) expected.size(); i++ ) S.tick();
    for( int i = 0; i < 20000; i++ ) S.tick();

    int got = S.R(3), mismatch = -1;
    bool ok = true;
    vector<int> received;
    for( int i = 0; i < got; i++ ) received.push_back( S.R( 4 + i ) );

    if( exact )
    {
        ok = got == (int) expected.size();
        for( int i = 0; i < got && i < (int) expected.size(); i++ )
          if( received[i] != expected[i] ) { mismatch = i; ok = false; break; }

        if( mode == 1 )
          for( int i = 0; i < got && i < (int) expected_glyphs.size(); i++ )
            if( S.R( 2052 + i ) != expected_glyphs[i] ) { mismatch = i; ok = false; break; }
    }
    else
    {
        size_t at = 0;
        for( int i = 0; i < got && ok; i++ )
        {
            while( at < expected.size() && expected[at] != received[i] ) at++;
            if( at == expected.size() ) { ok = false; mismatch = i; }
            else at++;
        }
        ok = ok && got > 0;
    }

    if( !ok && getenv("DBG") )
      for( int i = 0; i < (int) max( expected.size(), received.size() ); i++ )
        printf( "  #%d expected %d got %d\n", i, i < (int) expected.size()? expected[i] : -1, i < got? received[i] : -1 );

    printf( "%-44s %s  frames=%d expected=%zu read=%d first_mismatch=%d straddles=%d\n",
            name, ok? "PASS" : "FAIL", S.R(2), expected.size(), got, mismatch, S.straddles );
    return ok? 0 : 1;
}

// many random characters, typed in any of the ways: the expected character of
// each comes from the documented tables, not from compose.c
static void random_typing( mt19937& rng, int count, vector<Step>& st, vector<Ev>& ev )
{
    for( int n = 0; n < count; n++ )
    {
        int how = rng() % 3;

        if( how == 0 )   // AltGr + key
        {
            const compose_altgr_t& e = compose_altgr[ rng() % (sizeof(compose_altgr) / sizeof(compose_altgr[0])) ];
            bool shifted = (rng() % 2) && e.usage != HID_KEY_6;
            P( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KD, RALT } );
            if( shifted ) { P( st, HID_KEY_SHIFT_LEFT );  ev.push_back( { KD, LSHIFT } ); }
            P( st, e.usage );  R( st, e.usage );
            ev.push_back( { TX, shifted? e.code_shifted : e.code } );
            if( shifted ) { R( st, HID_KEY_SHIFT_LEFT );  ev.push_back( { KU, LSHIFT } ); }
            R( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KU, RALT } );
        }
        else if( how == 1 )  // dead key + letter
        {
            const compose_pair_t& e = compose_pairs[ rng() % (sizeof(compose_pairs) / sizeof(compose_pairs[0])) ];
            bool upper = (e.base >= 'A' && e.base <= 'Z');
            uint8_t letter = (uint8_t)( HID_KEY_A + ((upper? e.base + 32 : e.base) - 'a') );
            uint8_t dead_usage = HID_KEY_APOSTROPHE;  bool dead_shift = false;
            switch( e.dead )
            {
                case DEAD_ACUTE:      dead_usage = HID_KEY_APOSTROPHE;  dead_shift = false;  break;
                case DEAD_DIAERESIS:  dead_usage = HID_KEY_APOSTROPHE;  dead_shift = true;   break;
                case DEAD_GRAVE:      dead_usage = HID_KEY_GRAVE;       dead_shift = false;  break;
                case DEAD_TILDE:      dead_usage = HID_KEY_GRAVE;       dead_shift = true;   break;
                case DEAD_CIRCUMFLEX: dead_usage = HID_KEY_6;           dead_shift = true;   break;
            }
            P( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KD, RALT } );
            if( dead_shift ) { P( st, HID_KEY_SHIFT_RIGHT );  ev.push_back( { KD, RSHIFT } ); }
            P( st, dead_usage );  R( st, dead_usage );
            if( dead_shift ) { R( st, HID_KEY_SHIFT_RIGHT );  ev.push_back( { KU, RSHIFT } ); }
            R( st, HID_KEY_ALT_RIGHT );  ev.push_back( { KU, RALT } );
            if( upper ) { P( st, HID_KEY_SHIFT_LEFT );  ev.push_back( { KD, LSHIFT } ); }
            P( st, letter );  R( st, letter );
            ev.push_back( { TX, e.code } );
            if( upper ) { R( st, HID_KEY_SHIFT_LEFT );  ev.push_back( { KU, LSHIFT } ); }
        }
        else            // Alt code, any typable character
        {
            int code;
            do { code = rng() % 256; } while( !compose_valid_char( (uint8_t) code ) );
            char digits[8];
            snprintf( digits, sizeof(digits), (rng() % 2)? "0%d" : "%d", code );
            if( strlen( digits ) > 4 ) snprintf( digits, sizeof(digits), "%d", code );
            altcode( st, ev, digits, code );
        }
    }
}

static int test_typed_chars()
{
    int fails = 0;
    vector<Step> st;  vector<Ev> ev;
    scenarios( st, ev );

    const char* modes[3] = { "getkey", "read+glyph", "readevent" };
    for( int m = 0; m < 3; m++ )
    {
        char name[96];
        snprintf( name, sizeof(name), "chars: scripted, steady (%s)", modes[m] );
        fails += test_kbd_script( name, m, st, ev, 21 + m, 0.0, 0.0, 60, 120 );
        snprintf( name, sizeof(name), "chars: scripted, jitter 3ms (%s)", modes[m] );
        fails += test_kbd_script( name, m, st, ev, 24 + m, 3.0, 0.0, 60, 120 );
    }
    fails += test_kbd_script( "chars: scripted, reports split 40%", 0, st, ev, 27, 2.0, 0.4, 60, 120 );

    // programs built with the previous library keep working: they never see a
    // typed character as a wrong key press
    fails += test_kbd_script( "compat: old library, v32kbd_read()", 3, st, ev, 41, 0.0, 0.0, 60, 120 );
    fails += test_kbd_script( "compat: old library, v32kbd_readevent()", 4, st, ev, 42, 0.0, 0.0, 60, 120 );

    // a burst: far faster than the protocol, queued without losing any
    {
        mt19937 rng( 77 );
        vector<Step> bst;  vector<Ev> bev;
        random_typing( rng, 14, bst, bev );
        fails += test_kbd_script( "chars: burst of 14 queued (getkey)", 0, bst, bev, 28, 0.0, 0.0, 1, 3 );
        fails += test_kbd_script( "chars: burst of 14 queued, jitter (read)", 1, bst, bev, 29, 4.0, 0.2, 1, 3 );
    }

    // random mixes of every kind of typed character, at a steady pace
    for( int run = 0; run < 3; run++ )
    {
        mt19937 rng( 500 + run );
        vector<Step> rst;  vector<Ev> rev;
        random_typing( rng, 120, rst, rev );
        char name[96];
        snprintf( name, sizeof(name), "chars: 120 random typed, run %d", run + 1 );
        fails += test_kbd_script( name, 0, rst, rev, 31 + run, run * 1.5, run? 0.15 : 0.0, 150, 260 );
    }

    // overflow: more than the queue can hold. Old events are discarded, but
    // a prefix is never separated from its payload, so what arrives is an
    // in-order part of what was sent and never a stray key or character
    {
        mt19937 rng( 99 );
        vector<Step> ost;  vector<Ev> oev;
        random_typing( rng, 100, ost, oev );
        fails += test_kbd_script( "chars: overflow keeps pairs whole", 0, ost, oev, 40, 0.0, 0.0, 1, 2, false );
    }

    return fails;
}

int main()
{
    int fails = 0;

    fails += test_kbd(   "kbd: steady frames",              1, 0.0, 0.0,  300, 120 );
    fails += test_kbd(   "kbd: fast typing (queue)",        2, 0.0, 0.0,  110, 20 );
    fails += test_kbd(   "kbd: frame jitter 3ms",           3, 3.0, 0.0,  300, 80 );

    fails += test_typed_chars();

    fails += test_mouse( "mouse: slow, steady frames",     11, 0.0, 0.0, 0.0, 20, 6 );
    fails += test_mouse( "mouse: fast, steady frames",     12, 0.0, 0.0, 0.0, 20, 60 );
    fails += test_mouse( "mouse: fast, 3ms frame jitter",  13, 3.0, 0.0, 0.0, 20, 60 );
    fails += test_mouse( "mouse: fast, split reports 30%", 14, 0.0, 0.0, 0.3, 20, 60 );
    fails += test_mouse( "mouse: fast, 6ms jitter",        15, 6.0, 0.0, 0.0, 20, 60 );
    #ifdef STEP7
    fails += test_mouse( "mouse(7ms): fast, 2% late frames", 16, 0.0, 0.02, 0.0, 20, 60 );
    fails += test_mouse( "mouse(7ms): fast, 6ms jitter",     15, 6.0, 0.0, 0.0, 20, 60 );
    #else
    test_mouse( "mouse: 2% late frames (known)",       16, 0.0, 0.02, 0.0, 20, 60 );
    #endif

    printf( "%d failed\n", fails );
    return fails;
}
