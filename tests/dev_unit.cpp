// Unit tests for the v32dev mailbox patch: partial saving, planning with
// carve-outs, two-phase ordering, inbound sync, heartbeat, activation rules.
#include "tcommon.hpp"
#include <set>
#include <memory>

static string TmpCard( const char* Name )
{
    string Dir = getenv( "TEST_TMPDIR" ) ? getenv( "TEST_TMPDIR" ) : ".";
    return Dir + "/" + Name;
}

// ---------------------------------------------------------------------------
static void TestPlainCardPartialSave()
{
    printf( "plain card: partial save keeps untouched file regions\n" );
    string Path = TmpCard( "plain.memc" );
    std::unique_ptr< V32Console > ConsoleHeap( new V32Console ); V32Console& Console = *ConsoleHeap;
    Console.CreateMemoryCard( Path );
    Console.LoadMemoryCard( Path );
    CHECK( Console.HasMemoryCard() );
    CHECK( !Console.HasMemoryCardMailbox() );

    // program writes in page 0 and page 19
    CHECK( Console.MemoryCardController.WriteAddress( 10, W( 0x1111 ) ) );
    CHECK( Console.MemoryCardController.WriteAddress( 5000, W( 0x2222 ) ) );
    CHECK( Console.WasMemoryCardModified() );

    // an outside tool changes a word in a page the program has not touched
    SetFileWord( Path, 100000, 0x7777 );

    vector< DEV::SaveSegment > Segments;
    Console.MemoryCardController.PlanSave( Segments );
    CHECK( Segments.size() == 2 );
    CHECK( Segments[ 0 ].FirstWord == 0 && Segments[ 0 ].NumberOfWords == 256 && Segments[ 0 ].Phase == 1 );
    CHECK( Segments[ 1 ].FirstWord == 19 * 256 && Segments[ 1 ].NumberOfWords == 256 );

    Console.SaveMemoryCard();
    CHECK( !Console.WasMemoryCardModified() );
    CHECK( FileWord( Path, 10 ) == 0x1111 );
    CHECK( FileWord( Path, 5000 ) == 0x2222 );
    CHECK( FileWord( Path, 100000 ) == 0x7777 );        // untouched page survives

    // the signature stays valid and the file keeps its size
    std::ifstream f( Path, std::ios::binary | std::ios::ate );
    CHECK( (long)f.tellg() == 1048584 );
    f.seekg( 0 );
    char Sig[ 9 ] = { 0 };
    f.read( Sig, 8 );
    CHECK( string( Sig ) == "V32-MEMC" );

    // adjacent dirty pages are merged into one segment
    Console.MemoryCardController.WriteAddress( 256 * 7 + 3, W( 1 ) );
    Console.MemoryCardController.WriteAddress( 256 * 8 + 3, W( 2 ) );
    Console.MemoryCardController.WriteAddress( 256 * 9 + 3, W( 3 ) );
    Console.MemoryCardController.PlanSave( Segments );
    CHECK( Segments.size() == 1 && Segments[ 0 ].FirstWord == 256 * 7 && Segments[ 0 ].NumberOfWords == 768 );

    // unloading saves pending data
    Console.UnloadMemoryCard();
    CHECK( FileWord( Path, 256 * 8 + 3 ) == 2 );
}

// ---------------------------------------------------------------------------
static bool Excluded( int32_t Word, bool IncludeCommit )
{
    if( Word >= DEV::RegionStart && Word < DEV::CartAreaStart )
      return true;

    if( IncludeCommit )
      for( int c = 0; c < DEV::Channels; c++ )
      {
          int32_t Start = DEV::CartAreaStart + c * DEV::PartWords;
          if( Word >= Start && Word < Start + DEV::ControlWords )
            return true;
      }

    return false;
}

static void TestMailboxPlanning()
{
    printf( "mailbox: planning carves out host area and orders commit words last\n" );
    string Path = TmpCard( "mbox_plan.memc" );
    std::unique_ptr< V32Console > ConsoleHeap( new V32Console ); V32Console& Console = *ConsoleHeap;
    Console.CreateMemoryCard( Path );
    FormatMailbox( Path );
    Console.LoadMemoryCard( Path );
    CHECK( Console.HasMemoryCardMailbox() );

    V32MemoryCardController& Card = Console.MemoryCardController;

    // the emulator-owned header words are visible to the program but not saved
    CHECK( Card.Memory[ DEV::RegionStart + DEV::H_EmulatorFlags ].AsInteger == DEV::EmuFlagLive );

    // writes: ordinary card data, header, host area, cart data and cart control
    int32_t Writes[] = {
        1234,                                                   // ordinary save data
        DEV::RegionStart + 20,                                  // header (not allowed, must not reach the file)
        DEV::HostAreaStart + 3 * DEV::PartWords + 7,            // host area (not allowed)
        DEV::CartAreaStart + 0 * DEV::PartWords + 1,            // channel 0 commit word
        DEV::CartAreaStart + 0 * DEV::PartWords + 500,          // channel 0 data
        DEV::CartAreaStart + 3 * DEV::PartWords + 900,          // channel 3 data
    };

    std::set< int32_t > DirtyPageSet;
    for( size_t i = 0; i < sizeof(Writes) / sizeof(Writes[ 0 ]); i++ )
    {
        CHECK( Card.WriteAddress( Writes[ i ], W( 0x5000 + (int32_t)i ) ) );
        DirtyPageSet.insert( Writes[ i ] >> DEV::PageShift );
    }

    vector< DEV::SaveSegment > Segments;
    Card.PlanSave( Segments );

    // phase 1 segments cover exactly: dirty pages minus (header + host area + commit words)
    std::vector< int > Covered( DEV::CardWords, 0 );
    bool PhaseOrderOK = true;
    int LastPhase = 1;

    for( size_t i = 0; i < Segments.size(); i++ )
    {
        if( Segments[ i ].Phase < LastPhase )
          PhaseOrderOK = false;
        LastPhase = Segments[ i ].Phase;

        if( Segments[ i ].Phase == 1 )
          for( int32_t w = Segments[ i ].FirstWord; w < Segments[ i ].FirstWord + Segments[ i ].NumberOfWords; w++ )
            Covered[ w ]++;
    }

    CHECK( PhaseOrderOK );

    bool ExactCoverage = true;
    for( int32_t w = 0; w < DEV::CardWords; w++ )
    {
        int Expected = (DirtyPageSet.count( w >> DEV::PageShift ) && !Excluded( w, true )) ? 1 : 0;
        if( Covered[ w ] != Expected )
          ExactCoverage = false;
    }
    CHECK( ExactCoverage );

    // phase 2 is just the commit words of touched cart parts: channels 0 and 3 for sure
    std::set< int32_t > CommitStarts;
    for( size_t i = 0; i < Segments.size(); i++ )
      if( Segments[ i ].Phase == 2 )
      {
          CHECK( Segments[ i ].NumberOfWords == DEV::ControlWords );
          CommitStarts.insert( Segments[ i ].FirstWord );
      }
    CHECK( CommitStarts.count( DEV::CartAreaStart + 0 * DEV::PartWords ) == 1 );
    CHECK( CommitStarts.count( DEV::CartAreaStart + 3 * DEV::PartWords ) == 1 );

    // now save for real and check the file
    SetFileWord( Path, DEV::HostAreaStart + 3 * DEV::PartWords + 7, 0xAAAA );    // host changed it meanwhile
    Console.SaveMemoryCard();
    CHECK( FileWord( Path, 1234 ) == 0x5000 );
    CHECK( FileWord( Path, DEV::RegionStart + 20 ) == 0 );
    CHECK( FileWord( Path, DEV::HostAreaStart + 3 * DEV::PartWords + 7 ) == 0xAAAA );
    CHECK( FileWord( Path, DEV::CartAreaStart + 1 ) == 0x5003 );
    CHECK( FileWord( Path, DEV::CartAreaStart + 500 ) == 0x5004 );
    CHECK( FileWord( Path, DEV::CartAreaStart + 3 * DEV::PartWords + 900 ) == 0x5005 );

    // the header words owned by the emulator never reach the file
    CHECK( FileWord( Path, DEV::RegionStart + DEV::H_EmulatorFlags ) == 0 );
    CHECK( FileWord( Path, DEV::RegionStart + DEV::H_EmulatorBeat ) == 0 );

    Console.UnloadMemoryCard();
}

// ---------------------------------------------------------------------------
static void TestInboundSync()
{
    printf( "mailbox: inbound sync merges only the host area, heartbeat is RAM-only\n" );
    string Path = TmpCard( "mbox_sync.memc" );
    std::unique_ptr< V32Console > ConsoleHeap( new V32Console ); V32Console& Console = *ConsoleHeap;
    Console.CreateMemoryCard( Path );
    FormatMailbox( Path );
    Console.LoadMemoryCard( Path );
    CHECK( Console.HasMemoryCardMailbox() );

    V32MemoryCardController& Card = Console.MemoryCardController;
    int32_t HostWord = DEV::HostAreaStart + 1 * DEV::PartWords + 3;    // channel 1 h2c head

    SetFileWord( Path, HostWord, 42 );
    SetFileWord( Path, 1000, 99 );                                      // not in the host area
    SetFileWord( Path, DEV::CartAreaStart + 5, 77 );                    // cart-owned: must not be merged

    Console.SyncMemoryCardMailbox();
    CHECK( Card.Memory[ HostWord ].AsInteger == 42 );
    CHECK( Card.Memory[ 1000 ].AsInteger == 0 );
    CHECK( Card.Memory[ DEV::CartAreaStart + 5 ].AsInteger == 0 );
    CHECK( Card.Memory[ DEV::RegionStart + DEV::H_EmulatorBeat ].AsInteger == 1 );
    CHECK( !Console.WasMemoryCardModified() );                          // a sync is not a program write

    Console.SyncMemoryCardMailbox();
    CHECK( Card.Memory[ DEV::RegionStart + DEV::H_EmulatorBeat ].AsInteger == 2 );

    // a later change is seen too, including the unbuffered re-read of the same area
    SetFileWord( Path, HostWord, 43 );
    Console.SyncMemoryCardMailbox();
    CHECK( Card.Memory[ HostWord ].AsInteger == 43 );

    // the program reads host data through the normal memory interface
    V32Word Result;
    CHECK( Card.ReadAddress( HostWord, Result ) && Result.AsInteger == 43 );

    // after the card is saved the file still has the host's value
    Card.WriteAddress( 3000, W( 5 ) );
    Console.SaveMemoryCard();
    CHECK( FileWord( Path, HostWord ) == 43 );
    CHECK( FileWord( Path, 1000 ) == 99 );

    Console.UnloadMemoryCard();
    CHECK( !Console.HasMemoryCardMailbox() );
}

// ---------------------------------------------------------------------------
static void TestActivationRules()
{
    printf( "activation: needs a complete matching header; V32DEV_DISABLE turns it off\n" );

    {
        // a card with a partial or different header stays a regular card
        string Path = TmpCard( "mbox_bad.memc" );
        std::unique_ptr< V32Console > ConsoleHeap( new V32Console ); V32Console& Console = *ConsoleHeap;
        Console.CreateMemoryCard( Path );
        SetFileWord( Path, DEV::RegionStart + DEV::H_Magic, DEV::Magic );     // only the magic
        Console.LoadMemoryCard( Path );
        CHECK( !Console.HasMemoryCardMailbox() );
        Console.UnloadMemoryCard();

        FormatMailbox( Path );
        SetFileWord( Path, DEV::RegionStart + DEV::H_PartWords, 1000 );       // different geometry
        Console.LoadMemoryCard( Path );
        CHECK( !Console.HasMemoryCardMailbox() );

        // with no mailbox, nothing is carved out and sync does nothing
        Console.SyncMemoryCardMailbox();
        Console.MemoryCardController.WriteAddress( DEV::HostAreaStart + 1, W( 9 ) );
        Console.SaveMemoryCard();
        CHECK( FileWord( Path, DEV::HostAreaStart + 1 ) == 9 );
        Console.UnloadMemoryCard();
    }

    {
        string Path = TmpCard( "mbox_off.memc" );
        std::unique_ptr< V32Console > ConsoleHeap( new V32Console ); V32Console& Console = *ConsoleHeap;
        Console.CreateMemoryCard( Path );
        FormatMailbox( Path );

        setenv( "V32DEV_DISABLE", "1", 1 );
        Console.LoadMemoryCard( Path );
        CHECK( !Console.HasMemoryCardMailbox() );
        Console.UnloadMemoryCard();

        setenv( "V32DEV_DISABLE", "0", 1 );
        Console.LoadMemoryCard( Path );
        CHECK( Console.HasMemoryCardMailbox() );
        Console.UnloadMemoryCard();

        unsetenv( "V32DEV_DISABLE" );
        Console.LoadMemoryCard( Path );
        CHECK( Console.HasMemoryCardMailbox() );
        Console.UnloadMemoryCard();
    }
}

// ---------------------------------------------------------------------------
static void TestRunNextFrame()
{
    printf( "frame hook: RunNextFrame syncs before running and saves after\n" );
    string Path = TmpCard( "mbox_frame.memc" );
    std::unique_ptr< V32Console > ConsoleHeap( new V32Console ); V32Console& Console = *ConsoleHeap;
    Console.CreateMemoryCard( Path );
    FormatMailbox( Path );
    Console.LoadMemoryCard( Path );
    CHECK( Console.HasMemoryCardMailbox() );

    Console.SetPower( true );
    SetFileWord( Path, DEV::HostAreaStart + 4, 1234 );
    Console.MemoryCardController.WriteAddress( DEV::CartAreaStart + 4, W( 4321 ) );

    Console.RunNextFrame();
    CHECK( Console.MemoryCardController.Memory[ DEV::HostAreaStart + 4 ].AsInteger == 1234 );
    CHECK( FileWord( Path, DEV::CartAreaStart + 4 ) == 4321 );
    CHECK( !Console.WasMemoryCardModified() );
    CHECK( Console.MemoryCardController.Memory[ DEV::RegionStart + DEV::H_EmulatorBeat ].AsInteger == 1 );

    Console.UnloadMemoryCard();
}

int main()
{
    InstallCallbacks();

    try
    {
        TestPlainCardPartialSave();
        TestMailboxPlanning();
        TestInboundSync();
        TestActivationRules();
        TestRunNextFrame();
    }
    catch( const std::exception& e )
    {
        printf( "EXCEPTION: %s\n", e.what() );
        return 2;
    }

    printf( "\n%d checks, %d failures\n", g_checks, g_failures );
    return g_failures ? 1 : 0;
}
