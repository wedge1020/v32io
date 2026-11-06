// Unit tests for the v32dev embedded server (emulator/v32dev-server): the server,
// its devices, and how it ties to the real console classes (activation, a
// mailbox that exists only in memory, what is and is not saved, card changes).
//
// The "program" here is a small scripted stand-in that talks to the card ONLY
// through the card's memory interface (like a cartridge does), following the
// open handshake of lib/v32dev/v32dev.h. A frame is: Console.SyncMemoryCardMailbox()
// (what RunNextFrame does first), then the program's turn.
#include "tcommon.hpp"
#include <algorithm>
#include <memory>
#include <chrono>
#include <thread>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

static string TmpFile( const char* Name )
{
    string Dir = getenv( "TEST_TMPDIR" ) ? getenv( "TEST_TMPDIR" ) : ".";
    return Dir + "/" + Name;
}

static const int RING = DEV::RingWords;

// ---------------------------------------------------------------------------
//  the program side
// ---------------------------------------------------------------------------
struct Program
{
    V32Console& Console;
    int Ch;
    int32_t HP, CP;

    Program( V32Console& C, int Channel ): Console( C ), Ch( Channel ),
        HP( DEV::HostAreaStart + Channel * DEV::PartWords ), CP( DEV::CartAreaStart + Channel * DEV::PartWords ) {}

    uint32_t Rd( int32_t Word ) { V32Word r; Console.MemoryCardController.ReadAddress( Word, r ); return r.AsBinary; }
    void Wr( int32_t Word, uint32_t Value ) { V32Word x; x.AsBinary = Value; Console.MemoryCardController.WriteAddress( Word, x ); }

    // what v32dev_probe sees
    bool Live() { return Rd( DEV::RegionStart + DEV::H_Magic ) == (uint32_t)DEV::Magic
                      && Rd( DEV::RegionStart + DEV::H_EmulatorFlags ) == (uint32_t)DEV::EmuFlagLive; }
    bool HostAttached() { return (Rd( HP + DEV::HP_Flags ) & 1) != 0; }
    uint32_t Type() { return Rd( HP + DEV::HP_Type ); }
    uint32_t Desc( int i ) { return Rd( HP + DEV::HP_Descriptor + i ); }
    uint32_t HostStatus() { return Rd( HP + DEV::HP_Status ); }

    // the open handshake: each side moves the ring end it owns
    void Open()
    {
        Wr( CP + DEV::CP_H2CTail, Rd( HP + DEV::HP_H2CHead ) );
        Wr( CP + DEV::CP_C2HHead, Rd( HP + DEV::HP_C2HTail ) );
        Wr( CP + DEV::CP_Flags, 1 );
        Wr( CP + DEV::CP_Session, Rd( CP + DEV::CP_Session ) + 1 );
    }

    int Free()
    {
        int head = Rd( CP + DEV::CP_C2HHead ), tail = Rd( HP + DEV::HP_C2HTail );
        return ((tail - head - 1) % RING + RING) % RING;
    }

    bool Write( const vector< uint32_t >& Words )
    {
        if( (int)Words.size() > Free() ) return false;
        uint32_t head = Rd( CP + DEV::CP_C2HHead );
        for( size_t i = 0; i < Words.size(); i++ ) { Wr( CP + DEV::ControlWords + head, Words[ i ] ); head = (head + 1) % RING; }
        Wr( CP + DEV::CP_C2HHead, head );
        return true;
    }

    bool Send( uint32_t Op, const vector< uint32_t >& Payload )
    {
        vector< uint32_t > m; m.push_back( (uint32_t)Payload.size() ); m.push_back( Op );
        m.insert( m.end(), Payload.begin(), Payload.end() );
        return Write( m );
    }

    // everything the host has sent so far
    vector< uint32_t > ReadAll()
    {
        vector< uint32_t > Out;
        uint32_t head = Rd( HP + DEV::HP_H2CHead ), tail = Rd( CP + DEV::CP_H2CTail );
        while( tail != head ) { Out.push_back( Rd( HP + DEV::ControlWords + tail ) ); tail = (tail + 1) % RING; }
        Wr( CP + DEV::CP_H2CTail, tail );
        return Out;
    }

    // one reply message (opcode, payload); empty payload and opcode 0 if none
    bool Recv( uint32_t& Op, vector< uint32_t >& Payload )
    {
        uint32_t head = Rd( HP + DEV::HP_H2CHead ), tail = Rd( CP + DEV::CP_H2CTail );
        int used = ((int)head - (int)tail + RING) % RING;
        if( used < 2 ) return false;
        uint32_t n = Rd( HP + DEV::ControlWords + tail );
        if( used < (int)n + 2 ) return false;
        Op = Rd( HP + DEV::ControlWords + (tail + 1) % RING );
        Payload.clear();
        for( uint32_t i = 0; i < n; i++ ) Payload.push_back( Rd( HP + DEV::ControlWords + (tail + 2 + i) % RING ) );
        Wr( CP + DEV::CP_H2CTail, (tail + 2 + n) % RING );
        return true;
    }
};

static void Frame( V32Console& Console ) { Console.SyncMemoryCardMailbox(); }

template< typename... T > static vector< uint32_t > V( T... Words ) { return vector< uint32_t >{ (uint32_t)Words... }; }

// writes a file of 'Sectors' sectors of 128 words: word i = Pattern( i )
static uint32_t Pat( uint32_t i ) { return (i * 2654435761u) ^ 0xA5A5A5A5u; }

static void MakeImage( const string& Path, int Sectors )
{
    std::ofstream f( Path, std::ios::binary );
    for( int i = 0; i < Sectors * 128; i++ ) { uint32_t w = Pat( i ); f.write( (char*)&w, 4 ); }
}

static string NewCard( V32Console& Console, const char* Name )
{
    string Path = TmpFile( Name );
    Console.CreateMemoryCard( Path );
    return Path;
}

// ---------------------------------------------------------------------------
static void TestSpecs()
{
    printf( "specs: parsing, canonical form, errors\n" );
    DEV::DeviceSpec S; string E;

    CHECK( DEV::DeviceSpec::Parse( "storage:disk.img", S, E ) && S.Kind == DEV::DeviceSpec::Storage && S.Path == "disk.img" );
    CHECK( S.ToString() == "storage:disk.img" );
    CHECK( DEV::DeviceSpec::Parse( "storage:a.img,bytes,ro,sector=64,create=100", S, E ) );
    CHECK( S.BytesMode && S.ReadOnly && S.SectorWords == 64 && S.CreateSectors == 100 && S.Path == "a.img" );
    CHECK( S.ToString() == "storage:a.img,bytes,ro,sector=64,create=100" );

    // options are recognized from the right: the path can have commas
    CHECK( DEV::DeviceSpec::Parse( "storage:/tmp/my,disk,v2.img,bytes", S, E ) );
    CHECK( S.Path == "/tmp/my,disk,v2.img" && S.BytesMode );
    CHECK( DEV::DeviceSpec::Parse( "storage:/tmp/odd,name", S, E ) && S.Path == "/tmp/odd,name" );

    CHECK( DEV::DeviceSpec::Parse( "serial:echo", S, E ) && S.Kind == DEV::DeviceSpec::SerialEcho && S.HangUpAfter < 0 );
    CHECK( DEV::DeviceSpec::Parse( "serial:echo,hup=5006", S, E ) && S.HangUpAfter == 5006 && S.ToString() == "serial:echo,hup=5006" );
    CHECK( DEV::DeviceSpec::Parse( "serial:stdio", S, E ) && S.Kind == DEV::DeviceSpec::SerialStdio );
    CHECK( DEV::DeviceSpec::Parse( "serial:tcp=3201", S, E ) && S.Kind == DEV::DeviceSpec::SerialTcp && S.Port == 3201 );

    CHECK( !DEV::DeviceSpec::Parse( "storage:", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "storage:x.img,sector=0", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "storage:x.img,sector=99999", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "storage:x.img,create=abc", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "serial:tcp=0", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "serial:tcp=70000", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "serial:tcp=12x", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "serial:echo,bogus", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "serial:modem", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "pipe:x", S, E ) );
    CHECK( !DEV::DeviceSpec::Parse( "", S, E ) );
}

// ---------------------------------------------------------------------------
static void TestEnableOnPlainCard()
{
    printf( "enable: a card with no mailbox gets one in memory only; the file is never touched\n" );
    std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
    string Path = NewCard( Console, "srv_plain.memc" );
    string E;

    // configured before any card exists
    CHECK( Console.SetDevServerChannel( 1, "serial:echo", E ) );
    Console.SetDevServerEnabled( true );
    CHECK( Console.IsDevServerEnabled() && !Console.IsDevServerActive() );
    CHECK( Console.GetDevServerStatus().find( "waiting" ) != string::npos );

    Console.LoadMemoryCard( Path );
    Program P( Console, 1 );
    CHECK( Console.IsDevServerActive() );
    CHECK( Console.HasMemoryCardMailbox() );
    CHECK( P.Live() );
    CHECK( P.HostAttached() && P.Type() == DEV::MakeType( DEV::ClassChar, 1 ) );
    CHECK( P.HostStatus() == DEV::StatusReady );

    // channels with nothing configured are not attached
    Program Q( Console, 0 );
    CHECK( !Q.HostAttached() && Q.Type() == 0 );

    // the header is in memory, not in the file
    CHECK( FileWord( Path, DEV::RegionStart + DEV::H_Magic ) == 0 );

    // the host heartbeat moves on every frame
    uint32_t b0 = P.Rd( DEV::HostAreaStart + 1 * DEV::PartWords + DEV::HP_Beat );
    uint32_t e0 = P.Rd( DEV::RegionStart + DEV::H_EmulatorBeat );
    Frame( Console ); Frame( Console ); Frame( Console );
    CHECK( P.Rd( DEV::HostAreaStart + 1 * DEV::PartWords + DEV::HP_Beat ) == b0 + 3 );
    CHECK( P.Rd( DEV::RegionStart + DEV::H_EmulatorBeat ) == e0 + 3 );

    // program data and mailbox traffic: only the data outside the mailbox is saved
    P.Open();
    CHECK( P.Write( V( 11, 22, 33 ) ) );
    P.Wr( 777, 0xBEEF );
    CHECK( Console.WasMemoryCardModified() );
    vector< DEV::SaveSegment > Segs;
    Console.MemoryCardController.PlanSave( Segs );
    bool TouchesRegion = false;
    for( size_t i = 0; i < Segs.size(); i++ )
      if( Segs[ i ].FirstWord + Segs[ i ].NumberOfWords > DEV::RegionStart ) TouchesRegion = true;
    CHECK( Segs.size() == 1 && Segs[ 0 ].FirstWord == 768 && !TouchesRegion );
    Console.SaveMemoryCard();
    CHECK( FileWord( Path, 777 ) == 0xBEEF );
    for( int32_t w = DEV::RegionStart; w < DEV::CardWords; w += 61 )
      if( FileWord( Path, w ) != 0 ) { CHECK( false ); break; }
    CHECK( FileWord( Path, DEV::CartAreaStart + 1 * DEV::PartWords + DEV::CP_C2HHead ) == 0 );

    // an outside tool writing the host area of the file is ignored (the server owns it)
    SetFileWord( Path, DEV::HostAreaStart + 1 * DEV::PartWords + DEV::HP_Type, 0x77 );
    SetFileWord( Path, DEV::RegionStart + DEV::H_Magic, 0 );
    Frame( Console );
    CHECK( P.Type() == DEV::MakeType( DEV::ClassChar, 1 ) && P.Live() );

    // turning it off: the program sees a plain card again, and nothing of the mailbox is saved
    Console.SetDevServerEnabled( false );
    CHECK( !Console.IsDevServerActive() && !Console.HasMemoryCardMailbox() );
    CHECK( !P.Live() && P.Rd( DEV::RegionStart + DEV::H_Magic ) == 0 );
    CHECK( Console.GetDevServerStatus() == "off" );
    Console.SaveMemoryCard();
    CHECK( FileWord( Path, 777 ) == 0xBEEF );

    // and on again
    Console.SetDevServerEnabled( true );
    CHECK( Console.IsDevServerActive() && P.Live() && P.HostAttached() );
    Console.UnloadMemoryCard();
    CHECK( !Console.IsDevServerActive() );
    CHECK( Console.IsDevServerEnabled() );              // the wish stays
}

// ---------------------------------------------------------------------------
static void TestBlockedAndFormatted()
{
    printf( "enable: a card with data in the mailbox region is left alone; a formatted card is taken over\n" );
    {
        std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
        string Path = NewCard( Console, "srv_busy.memc" );
        SetFileWord( Path, DEV::RegionStart + 500, 0x1234 );            // a game keeps its save here
        string E;
        Console.SetDevServerChannel( 0, "serial:echo", E );
        Console.SetDevServerEnabled( true );
        Console.LoadMemoryCard( Path );
        CHECK( !Console.IsDevServerActive() && !Console.HasMemoryCardMailbox() );
        CHECK( Console.GetDevServerStatus().find( "hold data" ) != string::npos );
        Program P( Console, 0 );
        CHECK( !P.Live() && P.Rd( DEV::RegionStart + 500 ) == 0x1234 );
        // saving writes that data back exactly as before
        P.Wr( DEV::RegionStart + 501, 0x4321 );
        Console.SaveMemoryCard();
        CHECK( FileWord( Path, DEV::RegionStart + 500 ) == 0x1234 && FileWord( Path, DEV::RegionStart + 501 ) == 0x4321 );
        Console.UnloadMemoryCard();
    }
    {
        std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
        string Path = NewCard( Console, "srv_fmt.memc" );
        FormatMailbox( Path );
        SetFileWord( Path, DEV::HostAreaStart + 2 * DEV::PartWords + DEV::HP_Type, 0x55 );   // stale host data
        string E;
        Console.SetDevServerChannel( 2, "serial:echo", E );
        Console.SetDevServerEnabled( true );
        Console.LoadMemoryCard( Path );
        Program P( Console, 2 );
        CHECK( Console.IsDevServerActive() && P.Live() );
        CHECK( P.Type() == DEV::MakeType( DEV::ClassChar, 1 ) );        // the server's, not the file's
        P.Open(); P.Write( V( 5 ) );
        Console.SaveMemoryCard();
        CHECK( FileWord( Path, DEV::CartAreaStart + 2 * DEV::PartWords + DEV::CP_C2HHead ) == 0 );   // not saved
        CHECK( FileWord( Path, DEV::RegionStart + DEV::H_Magic ) == (int32_t)DEV::Magic );           // header untouched

        // taking the server out hands the mailbox back to the file (an external host)
        Console.SetDevServerEnabled( false );
        CHECK( Console.HasMemoryCardMailbox() );
        SetFileWord( Path, DEV::HostAreaStart + 2 * DEV::PartWords + DEV::HP_Type, 0x66 );
        Frame( Console );
        CHECK( P.Type() == 0x66 );
        Console.UnloadMemoryCard();
    }
    {
        std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
        string Path = NewCard( Console, "srv_dis.memc" );
        setenv( "V32DEV_DISABLE", "1", 1 );
        Console.SetDevServerEnabled( true );
        Console.LoadMemoryCard( Path );
        unsetenv( "V32DEV_DISABLE" );
        CHECK( !Console.IsDevServerActive() && Console.GetDevServerStatus().find( "V32DEV_DISABLE" ) != string::npos );
        Console.UnloadMemoryCard();
    }
}

// ---------------------------------------------------------------------------
static void TestStorage()
{
    printf( "storage: reads, writes, flush, errors, byte and read-only modes, descriptor\n" );
    std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
    string Card = NewCard( Console, "srv_st.memc" );
    string Img = TmpFile( "srv_st.img" );
    MakeImage( Img, 64 );
    string E;
    CHECK( Console.SetDevServerChannel( 0, "storage:" + Img, E ) );
    Console.SetDevServerEnabled( true );
    Console.LoadMemoryCard( Card );
    Program P( Console, 0 );

    CHECK( P.HostAttached() && P.Type() == DEV::MakeType( DEV::ClassBlock, 1 ) );
    CHECK( P.Desc( 0 ) == 128 && P.Desc( 1 ) == 64 && P.Desc( 2 ) == 0 && P.Desc( 3 ) == 15 );

    uint32_t Op; vector< uint32_t > R;
    P.Open();

    // read sectors 3 and 4
    CHECK( P.Send( 1, V( 7, 3, 2 ) ) );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && Op == 129 && R.size() == 2 + 256 && R[ 0 ] == 7 && R[ 1 ] == 0 );
    bool Same = R.size() == 258;
    for( int i = 0; Same && i < 256; i++ ) Same = (R[ 2 + i ] == Pat( 3 * 128 + i ));
    CHECK( Same );

    // write 2 sectors at 10, flush, read back
    vector< uint32_t > W( 3 + 256 ); W[ 0 ] = 8; W[ 1 ] = 10; W[ 2 ] = 2;
    for( int i = 0; i < 256; i++ ) W[ 3 + i ] = 0xC0DE0000 + i;
    CHECK( P.Send( 2, W ) );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && Op == 130 && R == V( 8, 0 ) );
    CHECK( P.Send( 3, V( 9 ) ) );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && Op == 131 && R == V( 9, 0 ) );
    std::ifstream f( Img, std::ios::binary ); f.seekg( 10 * 512 );
    uint32_t w0, w255; f.read( (char*)&w0, 4 ); f.seekg( 10 * 512 + 255 * 4 ); f.read( (char*)&w255, 4 );
    CHECK( w0 == 0xC0DE0000 && w255 == 0xC0DE00FF );
    CHECK( P.Send( 1, V( 10, 10, 1 ) ) );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && R.size() == 130 && R[ 2 ] == 0xC0DE0000 );

    // errors: range, wrapping sector numbers, zero and too many sectors, short request, bad opcode
    CHECK( P.Send( 1, V( 20, 63, 2 ) ) );                   Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 20, 2 ) );
    CHECK( P.Send( 1, V( 21, 0xFFFFFFFFu, 2 ) ) );          Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 21, 2 ) );
    CHECK( P.Send( 1, V( 22, 0xFFFFFFFEu, 0xFFFFFFFFu ) ) ); Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 22, 1 ) );
    CHECK( P.Send( 1, V( 23, 0, 0 ) ) );                    Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 23, 1 ) );
    CHECK( P.Send( 1, V( 24, 0, 16 ) ) );                   Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 24, 1 ) );
    CHECK( P.Send( 1, V( 25, 0 ) ) );                       Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 25, 1 ) );
    CHECK( P.Send( 2, V( 26, 0, 1, 1, 2, 3 ) ) );           Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 26, 1 ) );
    CHECK( P.Send( 99, V( 27 ) ) );                         Frame( Console ); CHECK( P.Recv( Op, R ) && Op == 227 && R == V( 27, 1 ) );
    CHECK( P.Send( 1, vector< uint32_t >() ) );             Frame( Console ); CHECK( P.Recv( Op, R ) && R == V( 0, 1 ) );

    // several requests in one frame are all answered, in order
    for( uint32_t t = 0; t < 5; t++ ) CHECK( P.Send( 1, V( 100 + t, t, 1 ) ) );
    Frame( Console );
    for( uint32_t t = 0; t < 5; t++ ) CHECK( P.Recv( Op, R ) && R[ 0 ] == 100 + t && R[ 1 ] == 0 && R.size() == 130 );

    // replies that do not fit in the ring wait for the program to read
    for( uint32_t t = 0; t < 3; t++ ) CHECK( P.Send( 1, V( 200 + t, 0, 15 ) ) );    // 3 replies of 1922 words
    Frame( Console );
    CHECK( P.Recv( Op, R ) && R[ 0 ] == 200 && R.size() == 2 + 15 * 128 );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && R[ 0 ] == 201 );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && R[ 0 ] == 202 );
    CHECK( !P.Recv( Op, R ) );

    // the program restarts (opens again): replies nobody awaits are dropped, and it works again
    P.Send( 1, V( 300, 0, 15 ) ); P.Send( 1, V( 301, 0, 15 ) );
    Frame( Console );                                       // one reply fits, the other waits
    P.Open();
    Frame( Console );
    P.ReadAll();
    CHECK( P.Send( 1, V( 1, 1, 1 ) ) );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && R[ 0 ] == 1 && R[ 1 ] == 0 );
    CHECK( !P.Recv( Op, R ) );

    // a program writing garbage indices cannot hurt the emulator
    P.Wr( P.CP + DEV::CP_C2HHead, 0xFFFFFFF0u );
    Frame( Console ); Frame( Console );
    P.Wr( P.CP + DEV::CP_C2HHead, P.Rd( P.HP + DEV::HP_C2HTail ) );
    P.Wr( P.CP + DEV::CP_H2CTail, 99999 );
    Frame( Console );
    P.Open();                                               // and it recovers by opening again
    CHECK( P.Send( 1, V( 2, 2, 1 ) ) );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && R[ 0 ] == 2 && R[ 1 ] == 0 );

    // a message with an impossible length is dropped (there is no way to find the next one)
    // and the stream is usable again for the one after
    P.Write( V( 5000, 1 ) );
    Frame( Console );
    CHECK( !P.Recv( Op, R ) );
    CHECK( P.Send( 1, V( 3, 3, 1 ) ) );
    Frame( Console );
    CHECK( P.Recv( Op, R ) && R[ 0 ] == 3 && R[ 1 ] == 0 );

    // changing the channel while serving swaps the device at once
    CHECK( Console.SetDevServerChannel( 0, "serial:echo", E ) );
    CHECK( P.Type() == DEV::MakeType( DEV::ClassChar, 1 ) );
    CHECK( Console.SetDevServerChannel( 0, "", E ) );
    CHECK( !P.HostAttached() );

    // read only, byte mode: text file served as it is
    string Txt = TmpFile( "srv_st.txt" );
    { std::ofstream t( Txt, std::ios::binary ); string s = "Hello from the host!\n"; for( int i = 0; i < 100; i++ ) t << s; t << "xx"; }
    CHECK( Console.SetDevServerChannel( 3, "storage:" + Txt + ",bytes,ro", E ) );
    Program B( Console, 3 );
    CHECK( B.Desc( 0 ) == 512 && B.Desc( 1 ) == 4 && B.Desc( 2 ) == 3 && B.Desc( 3 ) == 3 );    // 2102 bytes: 4 sectors, the rest is not accessible
    B.Open();
    CHECK( B.Send( 1, V( 1, 0, 1 ) ) );
    Frame( Console );
    CHECK( B.Recv( Op, R ) && R.size() == 2 + 512 && R[ 1 ] == 0 && R[ 2 ] == 'H' && R[ 3 ] == 'e' && R[ 2 + 20 ] == '\n' );
    vector< uint32_t > BW( 3 + 512, 0x141 ); BW[ 0 ] = 2; BW[ 1 ] = 0; BW[ 2 ] = 1;
    CHECK( B.Send( 2, BW ) );
    Frame( Console );
    CHECK( B.Recv( Op, R ) && R == V( 2, 3 ) );
    { std::ifstream t( Txt, std::ios::binary ); char c = 0; t.read( &c, 1 ); CHECK( c == 'H' ); }

    // byte mode keeps the low 8 bits of what is written
    string Bin = TmpFile( "srv_st.bin" );
    { std::ofstream t( Bin, std::ios::binary ); string z( 1024, 'z' ); t << z; }
    CHECK( Console.SetDevServerChannel( 2, "storage:" + Bin + ",bytes", E ) );
    Program C( Console, 2 ); C.Open();
    vector< uint32_t > CW( 3 + 512 ); CW[ 0 ] = 4; CW[ 1 ] = 1; CW[ 2 ] = 1;
    for( int i = 0; i < 512; i++ ) CW[ 3 + i ] = 0x12300 + (i & 0xFF);
    CHECK( C.Send( 2, CW ) );
    Frame( Console );
    CHECK( C.Recv( Op, R ) && R == V( 4, 0 ) );
    { std::ifstream t( Bin, std::ios::binary ); t.seekg( 512 + 258 ); int c = t.get(); CHECK( c == 2 ); t.seekg( 511 ); CHECK( t.get() == 'z' ); }

    // an image that cannot be opened is reported, not served
    CHECK( !Console.SetDevServerChannel( 1, "storage:/nonexistent/dir/x.img", E ) && !E.empty() );
    CHECK( !Program( Console, 1 ).HostAttached() );
    DEV::ChannelInfo Info = Console.DevServer.GetChannelInfo( 1 );
    CHECK( !Info.Serving && !Info.Error.empty() && Info.Spec == "storage:/nonexistent/dir/x.img" );
    Info = Console.DevServer.GetChannelInfo( 0 );
    CHECK( !Info.Serving && Info.Description.empty() );
    Info = Console.DevServer.GetChannelInfo( 2 );
    CHECK( Info.Serving && Info.Description.find( "bytes" ) != string::npos && Info.ProgramOpen && Info.WordsFromProgram > 0 );

    // create=: makes an image when it does not exist
    string New = TmpFile( "srv_st_new.img" );
    unlink( New.c_str() );
    CHECK( Console.SetDevServerChannel( 1, "storage:" + New + ",create=32", E ) );
    Program D( Console, 1 );
    CHECK( D.Desc( 0 ) == 128 && D.Desc( 1 ) == 32 );
    { std::ifstream t( New, std::ios::binary | std::ios::ate ); CHECK( (long)t.tellg() == 32 * 512 ); }

    Console.UnloadMemoryCard();
}

// ---------------------------------------------------------------------------
static void TestSerialEcho()
{
    printf( "serial echo: words come back in order, hang up after N, new conversation after reopening\n" );
    std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
    string Card = NewCard( Console, "srv_ser.memc" );
    string E;
    Console.SetDevServerChannel( 2, "serial:echo,hup=3500", E );
    Console.SetDevServerEnabled( true );
    Console.LoadMemoryCard( Card );
    Program P( Console, 2 );
    P.Open();

    // 3500 words through a ring that holds 2023
    vector< uint32_t > Got; int Sent = 0;
    for( int frame = 0; frame < 40 && (Sent < 3500 || (int)Got.size() < 3500); frame++ )
    {
        vector< uint32_t > Chunk;
        int n = std::min( 3500 - Sent, std::min( 700, P.Free() ) );
        for( int i = 0; i < n; i++ ) Chunk.push_back( Pat( Sent + i ) );
        if( n > 0 ) { P.Write( Chunk ); Sent += n; }
        Frame( Console );
        vector< uint32_t > In = P.ReadAll();
        Got.insert( Got.end(), In.begin(), In.end() );
    }
    bool Same = Got.size() == 3500;
    for( size_t i = 0; Same && i < Got.size(); i++ ) Same = (Got[ i ] == Pat( (uint32_t)i ));
    CHECK( Same );
    Frame( Console );
    CHECK( P.HostStatus() == (DEV::StatusReady | DEV::StatusHangUp) );

    // reopening starts again
    P.Open();
    Frame( Console );
    CHECK( P.HostStatus() == DEV::StatusReady );
    P.Write( V( 1, 2, 3 ) );
    Frame( Console );
    CHECK( P.ReadAll() == V( 1, 2, 3 ) );
    Console.UnloadMemoryCard();
}

// ---------------------------------------------------------------------------
static int Connect( int Port )
{
    int s = socket( AF_INET, SOCK_STREAM, 0 );
    struct sockaddr_in a; memset( &a, 0, sizeof a );
    a.sin_family = AF_INET; a.sin_port = htons( Port ); a.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    if( connect( s, (struct sockaddr*)&a, sizeof a ) != 0 ) { close( s ); return -1; }
    fcntl( s, F_SETFL, fcntl( s, F_GETFL, 0 ) | O_NONBLOCK );
    return s;
}

// reads what the server sent, over a few frames
static string ClientRead( V32Console& Console, int Sock, size_t Want )
{
    string Out;
    for( int i = 0; i < 50 && Out.size() < Want; i++ )
    {
        Frame( Console );
        char b[ 4096 ];
        ssize_t n = recv( Sock, b, sizeof b, 0 );
        if( n > 0 ) Out.append( b, n );
        else std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }
    return Out;
}

static void TestSerialTcp()
{
    printf( "serial tcp: loopback only, bytes both ways, hang up on disconnect, newest connection wins\n" );
    std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
    string Card = NewCard( Console, "srv_tcp.memc" );
    const int Port = 39000 + (getpid() % 500);
    string E;
    CHECK( Console.SetDevServerChannel( 1, "serial:tcp=" + std::to_string( Port ), E ) );
    Console.SetDevServerEnabled( true );
    Console.LoadMemoryCard( Card );
    Program P( Console, 1 );
    P.Open();

    // nobody connected yet: not a hang up, and output is dropped
    Frame( Console );
    CHECK( P.HostStatus() == DEV::StatusReady );
    P.Write( V( 'l', 'o', 's', 't' ) );
    Frame( Console );
    CHECK( Console.DevServer.GetChannelInfo( 1 ).Description.find( "waiting" ) != string::npos );

    // the port is taken: a second server on it says so
    {
        std::unique_ptr< V32Console > H2( new V32Console ); V32Console& Other = *H2;
        string C2 = NewCard( Other, "srv_tcp2.memc" );
        string E2;
        Other.SetDevServerChannel( 0, "serial:tcp=" + std::to_string( Port ), E2 );
        Other.SetDevServerEnabled( true );
        Other.LoadMemoryCard( C2 );
        DEV::ChannelInfo I = Other.DevServer.GetChannelInfo( 0 );
        CHECK( !I.Serving && I.Error.find( "in use" ) != string::npos );
        Other.UnloadMemoryCard();
    }

    int C = Connect( Port );
    CHECK( C >= 0 );
    Frame( Console ); Frame( Console );
    CHECK( Console.DevServer.GetChannelInfo( 1 ).Description.find( "connected" ) != string::npos );

    // client to program: bytes become words; program to client: low 8 bits
    CHECK( send( C, "hello\r\n", 7, 0 ) == 7 );
    Frame( Console ); Frame( Console );
    CHECK( P.ReadAll() == V( 'h', 'e', 'l', 'l', 'o', '\r', '\n' ) );
    P.Write( V( 'o', 'k', 0x141, '\n' ) );
    string Out = ClientRead( Console, C, 4 );
    CHECK( Out == "okA\n" );

    // a lot of output at once is delivered whole
    vector< uint32_t > Big( 1500 ); for( size_t i = 0; i < Big.size(); i++ ) Big[ i ] = 'a' + (i % 26);
    P.Write( Big );
    Out = ClientRead( Console, C, 1500 );
    CHECK( Out.size() == 1500 && Out[ 0 ] == 'a' && Out[ 1499 ] == (char)('a' + (1499 % 26)) );

    // the newest connection replaces the old one
    int C2 = Connect( Port );
    CHECK( C2 >= 0 );
    Frame( Console ); Frame( Console );
    P.Write( V( 'n', 'e', 'w' ) );
    CHECK( ClientRead( Console, C2, 3 ) == "new" );
    char b[ 16 ];
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    CHECK( recv( C, b, sizeof b, 0 ) == 0 );                   // the old one was closed
    close( C );

    // disconnect: the program sees a hang up, until somebody connects again
    close( C2 );
    for( int i = 0; i < 20 && P.HostStatus() == DEV::StatusReady; i++ ) { Frame( Console ); std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) ); }
    CHECK( P.HostStatus() == (DEV::StatusReady | DEV::StatusHangUp) );
    int C3 = Connect( Port );
    Frame( Console ); Frame( Console );
    CHECK( P.HostStatus() == DEV::StatusReady );
    close( C3 );

    // unloading the card closes the port: another server can take it now
    Console.UnloadMemoryCard();
    int Test = Connect( Port );
    CHECK( Test < 0 );
    if( Test >= 0 ) close( Test );
}

// ---------------------------------------------------------------------------
static void TestEnvironmentAndSettings()
{
    printf( "configuration: environment variables, settings store\n" );
    setenv( "V32DEV_SERVER", "1", 1 );
    setenv( "V32DEV_CH0", "serial:echo", 1 );
    setenv( "V32DEV_CH3", "serial:tcp=0", 1 );                   // wrong: reported, not fatal
    {
        std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
        CHECK( Console.DevServer.IsControlledByEnvironment() && Console.IsDevServerEnabled() );
        CHECK( Console.DevServer.GetSpec( 0 ) == "serial:echo" && Console.DevServer.GetSpec( 3 ).empty() );
        CHECK( !Console.DevServer.GetChannelInfo( 3 ).Error.empty() );

        // the settings file keeps its own copy while the environment is in charge
        DEV::ServerConfig Stored;
        Stored.Enabled = false; Stored.Specs[ 1 ] = "serial:echo,hup=7";
        Console.DevServer.SetStoredConfig( Stored );
        CHECK( Console.IsDevServerEnabled() && Console.DevServer.GetSpec( 1 ).empty() );
        CHECK( !Console.DevServer.GetStoredConfig().Enabled && Console.DevServer.GetStoredConfig().Specs[ 1 ] == "serial:echo,hup=7" );
    }
    setenv( "V32DEV_SERVER", "0", 1 );
    {
        std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
        CHECK( Console.DevServer.IsControlledByEnvironment() && !Console.IsDevServerEnabled() );
    }
    unsetenv( "V32DEV_SERVER" ); unsetenv( "V32DEV_CH0" ); unsetenv( "V32DEV_CH3" );
    {
        std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
        CHECK( !Console.DevServer.IsControlledByEnvironment() && !Console.IsDevServerEnabled() );
        DEV::ServerConfig Stored;
        Stored.Enabled = true; Stored.Specs[ 2 ] = "storage:a,b.img,ro"; Stored.Specs[ 1 ] = "bogus";
        Console.DevServer.SetStoredConfig( Stored );
        DEV::ServerConfig Back = Console.DevServer.GetStoredConfig();
        CHECK( Back.Enabled && Back.Specs[ 2 ] == "storage:a,b.img,ro" && Back.Specs[ 1 ].empty() );
    }
}

// ---------------------------------------------------------------------------
static void TestCardChange()
{
    printf( "card changes: the server follows the card; devices are reopened\n" );
    std::unique_ptr< V32Console > H( new V32Console ); V32Console& Console = *H;
    string A = NewCard( Console, "srv_a.memc" ), B = NewCard( Console, "srv_b.memc" );
    string Img = TmpFile( "srv_cc.img" ); MakeImage( Img, 8 );
    string E;
    Console.SetDevServerChannel( 0, "storage:" + Img, E );
    Console.SetDevServerEnabled( true );
    Console.LoadMemoryCard( A );
    Program P( Console, 0 );
    CHECK( P.HostAttached() );
    uint32_t SessionA = P.Rd( P.HP + DEV::HP_Session );

    Console.LoadMemoryCard( B );                                // Unload + Load, like the menu does
    CHECK( Console.IsDevServerActive() && P.HostAttached() );
    CHECK( P.Rd( P.HP + DEV::HP_Session ) != SessionA );
    // card A was left without a mailbox
    CHECK( FileWord( A, DEV::RegionStart + DEV::H_Magic ) == 0 );
    CHECK( FileWord( B, DEV::RegionStart + DEV::H_Magic ) == 0 );

    P.Open();
    uint32_t Op; vector< uint32_t > R;
    P.Send( 1, V( 1, 0, 1 ) ); Frame( Console );
    CHECK( P.Recv( Op, R ) && R[ 1 ] == 0 && R[ 2 ] == Pat( 0 ) );

    Console.UnloadMemoryCard();
    Console.LoadMemoryCard( A );
    CHECK( Console.IsDevServerActive() );
    Console.UnloadMemoryCard();
}

int main()
{
    InstallCallbacks();
    TestSpecs();
    TestEnableOnPlainCard();
    TestBlockedAndFormatted();
    TestStorage();
    TestSerialEcho();
    TestSerialTcp();
    TestEnvironmentAndSettings();
    TestCardChange();
    printf( "\n%d checks, %d failures\n", g_checks, g_failures );
    return g_failures ? 1 : 0;
}
