// Deterministic write-order test. Run with LD_PRELOAD=interpose.so and
// DEV_WRITE_LOG=<file>. Checks the REAL write(2) sequence that SaveMemoryCard
// issues for a mailbox card:
//   - nothing in the header or host-owned area is ever written
//   - every write that carries ring data comes before any write of commit words
//   - the commit words of each touched cart part are written exactly once
#include "tcommon.hpp"
#include <memory>
#include <unistd.h>

struct LoggedWrite { long FirstWord; long Words; };

static vector< LoggedWrite > ReadLog( const string& Path )
{
    vector< LoggedWrite > Out;
    FILE* f = fopen( Path.c_str(), "r" );
    if( !f ) return Out;
    long Pos, Len;
    while( fscanf( f, "%ld %ld", &Pos, &Len ) == 2 )
    {
        LoggedWrite w = { (Pos - 8) / 4, Len / 4 };
        Out.push_back( w );
    }
    fclose( f );
    return Out;
}

int main()
{
    InstallCallbacks();
    string Dir = getenv( "TEST_TMPDIR" ) ? getenv( "TEST_TMPDIR" ) : ".";
    string Path = Dir + "/order.memc";
    string Log = getenv( "DEV_WRITE_LOG" ) ? getenv( "DEV_WRITE_LOG" ) : "order.log";

    if( !getenv( "DEV_WRITE_LOG" ) ) { printf( "run with LD_PRELOAD=interpose.so and DEV_WRITE_LOG set\n" ); return 2; }

    std::unique_ptr< V32Console > ConsoleHeap( new V32Console );
    V32Console& Console = *ConsoleHeap;
    Console.CreateMemoryCard( Path );
    FormatMailbox( Path );
    Console.LoadMemoryCard( Path );
    CHECK( Console.HasMemoryCardMailbox() );

    V32MemoryCardController& Card = Console.MemoryCardController;

    // the program "sends" on channels 0 and 2: ring data first, then the index (as a program would)
    for( int c = 0; c < 3; c += 2 )
    {
        int32_t Part = DEV::CartAreaStart + c * DEV::PartWords;
        for( int i = 0; i < 300; i++ )
          Card.WriteAddress( Part + DEV::ControlWords + i, W( 0x1000 + i ) );
        Card.WriteAddress( Part + 1, W( 300 ) );             // c2h head
        Card.WriteAddress( Part + 2, W( 7 ) );               // h2c tail
    }
    Card.WriteAddress( 2000, W( 55 ) );                      // and ordinary save data

    unlink( Log.c_str() );
    Console.SaveMemoryCard();

    vector< LoggedWrite > Writes = ReadLog( Log );
    CHECK( !Writes.empty() );

    long LastDataWrite = -1, FirstCommitWrite = -1;
    int CommitWrites[ DEV::Channels ] = { 0 };
    bool TouchedProtected = false;

    for( size_t i = 0; i < Writes.size(); i++ )
    {
        long First = Writes[ i ].FirstWord, End = First + Writes[ i ].Words;

        // header and host-owned area
        if( First < DEV::CartAreaStart && End > DEV::RegionStart )
          TouchedProtected = true;

        bool IsCommit = false;
        for( int c = 0; c < DEV::Channels; c++ )
        {
            long Start = DEV::CartAreaStart + c * DEV::PartWords;
            if( First == Start && Writes[ i ].Words == DEV::ControlWords )
            {
                IsCommit = true;
                CommitWrites[ c ]++;
            }
        }

        if( IsCommit )
        {
            if( FirstCommitWrite < 0 ) FirstCommitWrite = (long)i;
        }
        else
            LastDataWrite = (long)i;
    }

    CHECK( !TouchedProtected );
    CHECK( FirstCommitWrite >= 0 );
    CHECK( LastDataWrite >= 0 && LastDataWrite < FirstCommitWrite );    // all data before any commit
    CHECK( CommitWrites[ 0 ] == 1 && CommitWrites[ 2 ] == 1 );          // touched channels committed once

    // every ring data word the program wrote really is in the file
    CHECK( FileWord( Path, DEV::CartAreaStart + DEV::ControlWords + 299 ) == 0x1000 + 299 );
    CHECK( FileWord( Path, DEV::CartAreaStart + 1 ) == 300 );
    CHECK( FileWord( Path, 2000 ) == 55 );

    if( getenv( "TEST_VERBOSE" ) )
      for( size_t i = 0; i < Writes.size(); i++ )
        printf( "  write %zu: words [%ld, +%ld)\n", i, Writes[ i ].FirstWord, Writes[ i ].Words );

    Console.UnloadMemoryCard();
    printf( "write-order test: %d checks, %d failures\n", g_checks, g_failures );
    return g_failures ? 1 : 0;
}
