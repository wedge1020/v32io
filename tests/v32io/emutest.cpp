// Emulator device test: the patched DesktopEmulator's own v32kbd code
// (GamepadsInput.cpp, with v32io.patch applied)
// is driven with synthetic SDL events, and the real console logic runs the
// t_kbd test cartridge (built with keyboard.h) to read what comes out.
// Everything else of the emulator (window, audio, menus) is stubbed.

#include "ConsoleLogic/V32Console.hpp"
#include "Emulator/GamepadsInput.hpp"
#include "Emulator/Globals.hpp"
#include "DesktopInfrastructure/Logger.hpp"
#include <imgui/imgui.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace V32;
using namespace std;

// ---------------------------------------------------------------- stubs
bool GlobalLoopActive = true;
bool MouseIsOnWindow  = false;
// (Emulator and EmulatorControl::IsPowerOn() are in emustubs.cpp)
void LOG( const string& ) {}
void GUI_LoadState() {}
void GUI_SaveState() {}
void CancelDelayedMessageBox() {}
namespace ImGui { ImGuiIO& GetIO() { static char b[ 1 << 14 ]; return *(ImGuiIO*) b; } }

V32Console Console;
GamepadsInput Gamepads;

static void Nop0() {}
static void NopColor( GPUColor ) {}
static void NopQuad( GPUQuad& ) {}
static void NopInt( int ) {}
static void NopTex( int, void* ) {}
static void Log( const string& ) {}
static void Throw( const string& s ) { fprintf( stderr, "EXCEPTION: %s\n", s.c_str() ); throw runtime_error( s ); }

// ---------------------------------------------------------------- helpers
static int results = -1;
static int failed  = 0, checks = 0;

static int R( int i ) { return Console.RAM.Memory[ results + i ].AsInteger; }

static void frame()
{
    Gamepads.UpdateV32Kbd();        // as in the emulator's main loop
    Console.RunNextFrame();
}

static void key( SDL_Scancode sc, bool down, bool repeat = false )
{
    SDL_Event E;
    memset( &E, 0, sizeof E );
    E.type = down? SDL_KEYDOWN : SDL_KEYUP;
    E.key.keysym.scancode = sc;
    E.key.repeat = repeat;
    Gamepads.ProcessEvent( E );
}

static void text( const char* utf8 )
{
    SDL_Event E;
    memset( &E, 0, sizeof E );
    E.type = SDL_TEXTINPUT;
    strncpy( E.text.text, utf8, sizeof( E.text.text ) - 1 );
    Gamepads.ProcessEvent( E );
}

// lets the program read everything that is queued
static void drain( int frames = 40 ) { for( int i = 0; i < frames; i++ ) frame(); }

static const int PRESS = 256, TEXT = 512;
static size_t consumed = 0;

// what the program read since the last check
static void expect( const char* name, vector<int> want )
{
    drain();
    vector<int> got;
    for( ; consumed < (size_t) R( 3 ); consumed++ )
      got.push_back( R( 4 + (int) consumed ) );

    checks++;
    if( got != want )
    {
        failed++;
        printf( "FAIL %s\n  want:", name );
        for( int v : want ) printf( " %d", v );
        printf( "\n  got: " );
        for( int v : got ) printf( " %d", v );
        printf( "\n" );
    }
}

int main( int argc, char** argv )
{
    if( argc < 3 ) { fprintf( stderr, "usage: emutest bios.v32 t_kbd.v32\n" ); return 2; }

    Callbacks::ClearScreen = NopColor;  Callbacks::DrawQuad = NopQuad;
    Callbacks::SetMultiplyColor = NopColor;  Callbacks::SetBlendingMode = NopInt;
    Callbacks::SelectTexture = NopInt;  Callbacks::LoadTexture = NopTex;
    Callbacks::UnloadCartridgeTextures = Nop0;  Callbacks::UnloadBiosTexture = Nop0;
    Callbacks::LogLine = Log;  Callbacks::ThrowException = Throw;

    Console.LoadBios( argv[1] );
    Console.LoadCartridge( argv[2] );
    Console.SetPower( true );

    // the v32kbd device on the second gamepad port
    for( int p = 0; p < Constants::GamepadPorts; p++ )
      Gamepads.MappedGamepads[ p ].Type = DeviceTypes::NoDevice;
    Gamepads.MappedGamepads[ 1 ].Type = DeviceTypes::V32Kbd;
    Gamepads.AssignInputDevices();

    for( int i = 0; i < 2000 && results < 0; i++ )
    {
        Console.RunNextFrame();
        auto& M = Console.RAM.Memory;
        for( size_t a = 0; a + 1 < M.size(); a++ )
          if( M[a].AsBinary == 0x7E57AB1E && M[a+1].AsBinary == 0x0B5E55ED )
            { results = (int) a; break; }
    }
    if( results < 0 ) { fprintf( stderr, "program did not start\n" ); return 1; }
    if( Gamepads.GetV32KbdGamepad() != 1 ) { fprintf( stderr, "v32kbd not assigned\n" ); return 1; }

    // ---- the character table
    checks++;
    if( V32Kbd::GetBiosCharacter( 0xE9 ) != 0xE9 || V32Kbd::GetBiosCharacter( 0x20AC ) != 0x80
        || V32Kbd::GetBiosCharacter( 'A' ) != 'A' || V32Kbd::GetBiosCharacter( 0x4E2D ) != 0
        || V32Kbd::GetBiosCharacter( 0x2500 ) < 14 || V32Kbd::GetBiosCharacter( 0x2500 ) > 31 )
      { failed++; printf( "FAIL character table\n" ); }

    // ---- ordinary ASCII key: a key press and a release, like always
    key( SDL_SCANCODE_A, true );  text( "a" );  key( SDL_SCANCODE_A, false );
    expect( "ascii key", { 'a' + PRESS, 'a' } );

    // ---- shift + key: the modifier is not delayed, the text is ASCII
    key( SDL_SCANCODE_LSHIFT, true );  key( SDL_SCANCODE_A, true );  text( "A" );
    key( SDL_SCANCODE_A, false );  key( SDL_SCANCODE_LSHIFT, false );
    expect( "shift a", { 6 + PRESS, 'a' + PRESS, 'a', 6 } );

    // ---- a key that types an accented letter in the host layout
    key( SDL_SCANCODE_SEMICOLON, true );  text( "\xC3\xB6" );   // ö
    key( SDL_SCANCODE_SEMICOLON, false );
    expect( "layout o-diaeresis", { 0xF6 + TEXT } );

    // ---- euro sign (needs the high page with payload 0)
    key( SDL_SCANCODE_5, true );  text( "\xE2\x82\xAC" );
    key( SDL_SCANCODE_5, false );
    expect( "euro", { 0x80 + TEXT } );

    // ---- character below 32 (box drawing), through the low page
    int box = V32Kbd::GetBiosCharacter( 0x2500 );
    text( "\xE2\x94\x80" );
    expect( "box drawing", { box + TEXT } );

    // ---- dead key: no text for the dead key itself, then the combined letter
    key( SDL_SCANCODE_APOSTROPHE, true );  key( SDL_SCANCODE_APOSTROPHE, false );
    key( SDL_SCANCODE_A, true );  text( "\xC3\xA1" );  key( SDL_SCANCODE_A, false );
    expect( "dead key", { '\'' + PRESS, '\'', 0xE1 + TEXT } );

    // ---- text with no key (an input method)
    text( "\xC3\xB1" );
    expect( "standalone text", { 0xF1 + TEXT } );

    // ---- text that is not in the font is dropped; its key still counts
    key( SDL_SCANCODE_Q, true );  text( "\xE4\xB8\xAD" );  key( SDL_SCANCODE_Q, false );
    expect( "not in font", { 'q' + PRESS, 'q' } );

    // ---- retriggers: not reported, nor their text
    key( SDL_SCANCODE_SEMICOLON, true );  text( "\xC3\xB6" );
    key( SDL_SCANCODE_SEMICOLON, true, true );  text( "\xC3\xB6" );
    key( SDL_SCANCODE_SEMICOLON, true, true );  text( "\xC3\xB6" );
    key( SDL_SCANCODE_SEMICOLON, false );
    expect( "no retrigger", { 0xF6 + TEXT } );

    key( SDL_SCANCODE_A, true );  text( "a" );
    key( SDL_SCANCODE_A, true, true );  text( "a" );
    key( SDL_SCANCODE_A, false );
    expect( "no ascii retrigger", { 'a' + PRESS, 'a' } );

    // ---- a key press waiting for text when the frame ends is a plain key
    key( SDL_SCANCODE_B, true );
    drain( 5 );
    key( SDL_SCANCODE_B, false );
    expect( "pending at frame end", { 'b' + PRESS, 'b' } );

    // ---- ctrl + key makes no text
    key( SDL_SCANCODE_LCTRL, true );  key( SDL_SCANCODE_C, true );  key( SDL_SCANCODE_C, false );
    key( SDL_SCANCODE_LCTRL, false );
    expect( "ctrl c", { 10 + PRESS, 'c' + PRESS, 'c', 10 } );

    // ---- order between a typed character and the following key
    key( SDL_SCANCODE_SEMICOLON, true );  text( "\xC3\xB6" );
    key( SDL_SCANCODE_X, true );  text( "x" );
    key( SDL_SCANCODE_SEMICOLON, false );  key( SDL_SCANCODE_X, false );
    expect( "char then key", { 0xF6 + TEXT, 'x' + PRESS, 'x' } );

    // ---- a replaced key's release must not be reported even if its
    //      release was lost once before (no stale state)
    key( SDL_SCANCODE_SEMICOLON, true );  text( "\xC3\xB6" );   // release never comes
    key( SDL_SCANCODE_SEMICOLON, true );  text( ";" );          // a new press of the key
    key( SDL_SCANCODE_SEMICOLON, false );
    expect( "stale replaced key", { 0xF6 + TEXT, ';' + PRESS, ';' } );

    // ---- queue overflow with no program reading: characters stay whole
    for( int i = 0; i < 400; i++ ) { text( "\xC3\xA9" ); if( i % 7 == 0 ) { text( "\xE2\x82\xAC" ); } }
    drain( 700 );
    {
        vector<int> got;
        for( ; consumed < (size_t) R( 3 ); consumed++ ) got.push_back( R( 4 + (int) consumed ) );
        bool ok = !got.empty();
        for( int v : got ) if( v != 0xE9 + TEXT && v != 0x80 + TEXT ) ok = false;
        checks++;
        if( !ok ) { failed++; printf( "FAIL overflow integrity (%zu values)\n", got.size() ); }
    }

    // ---- still in sync afterwards
    key( SDL_SCANCODE_A, true );  text( "a" );  key( SDL_SCANCODE_A, false );
    expect( "after overflow", { 'a' + PRESS, 'a' } );

    printf( "emulator device test: %d checks, %d failed\n", checks, failed );
    return failed? 1 : 0;
}
