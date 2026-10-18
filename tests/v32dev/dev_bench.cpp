// Measures the per-frame cost of the mailbox: inbound sync, and saving a frame's worth of writes.
#include "tcommon.hpp"
#include <chrono>
#include <memory>

int main( int argc, char** argv )
{
    InstallCallbacks();
    string Dir = getenv( "TEST_TMPDIR" ) ? getenv( "TEST_TMPDIR" ) : ".";
    string Path = Dir + "/bench.memc";
    std::unique_ptr< V32Console > ConsoleHeap( new V32Console );
    V32Console& Console = *ConsoleHeap;
    Console.CreateMemoryCard( Path );
    FormatMailbox( Path );
    Console.LoadMemoryCard( Path );

    const int N = 20000;
    typedef std::chrono::steady_clock Clock;

    auto t0 = Clock::now();
    for( int i = 0; i < N; i++ )
      Console.SyncMemoryCardMailbox();
    double SyncUs = std::chrono::duration< double, std::micro >( Clock::now() - t0 ).count() / N;

    // a busy frame: the program writes 1000 words in the cart area, then the frame is saved
    t0 = Clock::now();
    for( int i = 0; i < N; i++ )
    {
        for( int w = 0; w < 1000; w++ )
          Console.MemoryCardController.WriteAddress( DEV::CartAreaStart + DEV::ControlWords + w, W( i + w ) );
        Console.SaveMemoryCard();
    }
    double SaveUs = std::chrono::duration< double, std::micro >( Clock::now() - t0 ).count() / N;

    // an idle frame with a mailbox does only the sync; a plain card with one small write per frame:
    Console.UnloadMemoryCard();
    Console.CreateMemoryCard( Path );
    Console.LoadMemoryCard( Path );
    t0 = Clock::now();
    for( int i = 0; i < N; i++ )
    {
        Console.MemoryCardController.WriteAddress( 10, W( i ) );
        Console.SaveMemoryCard();
    }
    double PlainUs = std::chrono::duration< double, std::micro >( Clock::now() - t0 ).count() / N;

    printf( "inbound sync (32 KB read + compare), per frame:        %7.1f us\n", SyncUs );
    printf( "busy frame, 1000 words written + saved, per frame:     %7.1f us\n", SaveUs );
    printf( "plain card, 1 word written + saved, per frame:         %7.1f us\n", PlainUs );
    printf( "(a 60 fps frame is 16667 us)\n" );
    Console.UnloadMemoryCard();
    return 0;
}
