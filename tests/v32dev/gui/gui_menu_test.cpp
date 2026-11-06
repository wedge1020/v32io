// Drives the v32dev server menu of the DesktopEmulator with synthetic mouse clicks.
//
// The menu code is NOT copied here: build-gui-test.sh cuts it out of the patched
// DesktopEmulator/Emulator/GUI.cpp (the pieces between the markers below) and
// includes it. It runs on the real Dear ImGui sources (assertions on, so a menu
// that is not balanced or reuses an id aborts) and on the real console. What is
// replaced: the window (ImGui needs none to lay out and hit-test), the file
// dialogs (they return what the test says) and the message box (it records).
#include "V32Console.hpp"
#include "ExternalInterfaces.hpp"
#include <imgui/imgui.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unistd.h>

using namespace std;
using namespace V32;

// ---- what GUI.cpp has around the code under test -----------------------------
#define SDL_MESSAGEBOX_ERROR 0x10
V32Console Console;
string EmulatorFolder = "/tmp/";
static string LastMessage;
static string NextLoadPath, NextSavePath;
void DelayedMessageBox( uint32_t, const char*, const char* Message ) { LastMessage = Message; }
string GetLoadFilePath( const char*, const string& = EmulatorFolder ) { return NextLoadPath; }
string GetSaveFilePath( const char*, const string& = EmulatorFolder ) { return NextSavePath; }
string GetPathFileName( const string& P ) { size_t s = P.find_last_of( '/' ); return s == string::npos? P : P.substr( s + 1 ); }
string GetPathDirectory( const string& P ) { size_t s = P.find_last_of( '/' ); return s == string::npos? "" : P.substr( 0, s + 1 ); }

// ---- records where every menu entry was drawn, to click on it ---------------
static map< string, ImVec4 > Rects;
namespace Rec
{
    static void Note( const char* Label ) { ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); Rects[ Label ] = ImVec4( a.x, a.y, b.x, b.y ); }
    static bool BeginMenu( const char* Label, bool Enabled = true ) { bool r = ImGui::BeginMenu( Label, Enabled ); Note( Label ); return r; }
    static bool MenuItem( const char* Label, const char* Shortcut = nullptr, bool Selected = false, bool Enabled = true )
        { bool r = ImGui::MenuItem( Label, Shortcut, Selected, Enabled ); Note( Label ); return r; }
    static bool MenuItem( const char* Label, const char* Shortcut, bool* Selected, bool Enabled = true )
        { bool r = ImGui::MenuItem( Label, Shortcut, Selected, Enabled ); Note( Label ); return r; }
}

// ---- the code under test, cut out of the patched GUI.cpp ---------------------
#include "gui_menu_extract.inc"

// ---- the driver ----------------------------------------------------------------
static int Checks = 0, Failures = 0;
#define CHECK( c ) do { Checks++; if( !(c) ) { Failures++; printf( "  FAIL line %d: %s\n", __LINE__, #c ); } } while( 0 )

// what the menu asked the main loop to do (RenderGUI clears it every frame, as here)
static DelayedFileActions SeenAction = DelayedFileActions::None;
static int SeenChannel = -1;

static void Frame( bool Down )
{
    ImGui::GetIO().MouseDown[ 0 ] = Down;
    ImGui::NewFrame();
    PendingAction = DelayedFileActions::None;
    if( ImGui::BeginMainMenuBar() )
    {
        if( Rec::BeginMenu( "Memory card" ) )
        {
            ProcessMenuDevServer();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::Render();

    if( PendingAction != DelayedFileActions::None )
    {
        SeenAction = PendingAction;
        SeenChannel = PendingDevChannel;
    }
}

static void Idle( int n = 3 ) { for( int i = 0; i < n; i++ ) Frame( false ); }

static bool Click( const string& Label )
{
    if( !Rects.count( Label ) ) { printf( "  (no entry \"%s\" on screen)\n", Label.c_str() ); return false; }
    ImVec4 r = Rects[ Label ];
    ImGui::GetIO().MousePos = ImVec2( (r.x + r.z) / 2, (r.y + r.w) / 2 );
    Frame( false ); Frame( false );
    Frame( true );  Frame( true );
    Frame( false ); Frame( false );
    return true;
}

// the menu closes after an item is chosen: open it again down to a path of labels
static bool Path( std::initializer_list< string > Labels )
{
    Rects.clear();
    SeenAction = DelayedFileActions::None;
    SeenChannel = -1;
    ImGui::GetIO().MousePos = ImVec2( -1000, -1000 );
    Idle();
    bool ok = true;
    for( const string& L : Labels ) { if( !Click( L ) ) { ok = false; break; } }
    return ok;
}

static string Ch( int n, const string& State ) { return "Channel " + to_string( n ) + ": " + State + "###v32devch" + to_string( n ); }

int main()
{
    Callbacks::LogLine = []( const string& ) {};
    Callbacks::ThrowException = []( const string& M ) { throw runtime_error( M ); };

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2( 800, 600 );
    io.DeltaTime = 1.0f / 60;
    unsigned char* Pixels; int W, H;
    io.Fonts->GetTexDataAsRGBA32( &Pixels, &W, &H );

    string Dir = getenv( "TEST_TMPDIR" )? getenv( "TEST_TMPDIR" ) : ".";
    Dir += "/";

    printf( "gui: the menu opens and lists the server and its 4 channels\n" );
    CHECK( Path( { "Memory card", "v32dev server" } ) );
    CHECK( Rects.count( "Enabled" ) && Rects.count( Ch( 0, "off" ) ) && Rects.count( Ch( 3, "off" ) ) );

    printf( "gui: Enabled switches the server on and off\n" );
    CHECK( !Console.IsDevServerEnabled() );
    CHECK( Path( { "Memory card", "v32dev server", "Enabled" } ) );
    CHECK( Console.IsDevServerEnabled() );
    CHECK( Console.GetDevServerStatus().find( "waiting" ) != string::npos );
    CHECK( Path( { "Memory card", "v32dev server", "Enabled" } ) );
    CHECK( !Console.IsDevServerEnabled() );
    CHECK( Path( { "Memory card", "v32dev server", "Enabled" } ) );

    printf( "gui: serial echo, tcp and console entries\n" );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 1, "off" ), "Serial: echo" } ) );
    CHECK( Console.DevServer.GetSpec( 1 ) == "serial:echo" );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 1, "serial echo" ) } ) );             // its label follows what it serves
    CHECK( Path( { "Memory card", "v32dev server", Ch( 1, "serial echo" ), "Serial: TCP 127.0.0.1:3201" } ) );
    CHECK( Console.DevServer.GetSpec( 1 ) == "serial:tcp=3201" );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 2, "off" ), "Serial: console (stdin/stdout)" } ) );
    CHECK( Console.DevServer.GetSpec( 2 ) == "serial:stdio" );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 1, "serial tcp 3201" ), "Off" } ) );
    CHECK( Console.DevServer.GetSpec( 1 ).empty() );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 2, "serial console" ), "Off" } ) );

    printf( "gui: storage files, with the byte mode and read only switches\n" );
    string Img = Dir + "gui_test.img";
    { FILE* f = fopen( Img.c_str(), "wb" ); for( int i = 0; i < 8 * 512; i++ ) fputc( i & 255, f ); fclose( f ); }
    NextLoadPath = Img;
    CHECK( Path( { "Memory card", "v32dev server", Ch( 0, "off" ), "Storage: open file..." } ) );
    CHECK( SeenAction == DelayedFileActions::DevChannelOpenStorage && SeenChannel == 0 );
    // (the main loop does this once the menu is done)
    GUI_DevChannelStorage( 0, false );
    CHECK( Console.DevServer.GetSpec( 0 ) == "storage:" + Img );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 0, "storage gui_test.img" ), "Byte mode (next file)" } ) );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 0, "storage gui_test.img" ), "Read only (next file)" } ) );
    GUI_DevChannelStorage( 0, false );
    CHECK( Console.DevServer.GetSpec( 0 ) == "storage:" + Img + ",bytes,ro" );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 0, "storage gui_test.img" ), "Byte mode (next file)" } ) );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 0, "storage gui_test.img" ), "Read only (next file)" } ) );

    string NewImg = Dir + "gui_new.img";
    unlink( NewImg.c_str() );
    NextSavePath = NewImg;
    CHECK( Path( { "Memory card", "v32dev server", Ch( 3, "off" ), "Storage: new image..." } ) );
    CHECK( SeenAction == DelayedFileActions::DevChannelNewStorage && SeenChannel == 3 );
    GUI_DevChannelStorage( 3, true );
    CHECK( Console.DevServer.GetSpec( 3 ) == "storage:" + NewImg + ",create=16384" );

    printf( "gui: a file that can't be served is reported\n" );
    LastMessage.clear();
    NextLoadPath = "/nonexistent/dir/x.img";
    Console.SetDevServerEnabled( true );
    string Card = Dir + "gui_card.memc";
    Console.CreateMemoryCard( Card );
    Console.LoadMemoryCard( Card );                       // now the channels are really served
    CHECK( Console.IsDevServerActive() );
    GUI_DevChannelStorage( 1, false );
    CHECK( LastMessage.find( "Channel 1" ) != string::npos );
    CHECK( !Console.DevServer.GetChannelInfo( 1 ).Serving );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 1, "error" ) } ) );   // and its menu says so
    CHECK( Path( { "Memory card", "v32dev server", Ch( 1, "error" ), "Off" } ) );

    printf( "gui: a served channel shows its state (storage, byte mode)\n" );
    CHECK( Console.DevServer.GetChannelInfo( 0 ).Serving );
    CHECK( Console.DevServer.GetChannelInfo( 3 ).Serving );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 0, "storage gui_test.img" ) } ) );
    CHECK( Path( { "Memory card", "v32dev server", Ch( 3, "storage gui_new.img" ) } ) );
    CHECK( Console.DevServer.GetChannelInfo( 3 ).Description.find( "16384 sectors" ) != string::npos );

    Console.UnloadMemoryCard();

    printf( "\n%d checks, %d failures\n", Checks, Failures );
    ImGui::DestroyContext();
    return Failures? 1 : 0;
}
