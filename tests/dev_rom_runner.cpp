// Runs a REAL compiled cartridge in the (patched) console, headless, using the
// console's own RunNextFrame(): the stub BIOS starts the cartridge at once, the
// memory card is loaded the way the emulator loads it, and the mailbox is kept
// in sync every frame by the patched console code.
//
// usage: dev_rom_runner <bios.v32> <cart.v32> <card.memc> [microseconds per frame] [max frames]
// exit:  0 = the program ended (main returned), 1 = hardware error, 2 = usage/load error,
//        3 = frame limit reached
#include "tcommon.hpp"
#include <chrono>
#include <memory>
#include <unistd.h>

int main( int argc, char** argv )
{
    if( argc < 4 ) { printf( "usage: dev_rom_runner <bios.v32> <cart.v32> <card.memc> [micros] [max frames]\n" ); return 2; }
    int Micros = argc > 4 ? atoi( argv[ 4 ] ) : 1000;
    long MaxFrames = argc > 5 ? atol( argv[ 5 ] ) : 20000;

    InstallCallbacks();
    std::unique_ptr< V32Console > ConsoleHeap( new V32Console );
    V32Console& Console = *ConsoleHeap;

    try
    {
        Console.LoadBios( argv[ 1 ] );
        Console.LoadCartridge( argv[ 2 ] );
        Console.LoadMemoryCard( argv[ 3 ] );
    }
    catch( std::exception& e ) { printf( "RUNNER: load error: %s\n", e.what() ); return 2; }

    Console.SetPower( true );

    long Frames = 0;
    float MaxLoad = 0;
    while( Frames < MaxFrames && !Console.IsCPUHalted() )
    {
        Console.RunNextFrame();
        Frames++;
        if( Console.GetCPULoad() > MaxLoad ) MaxLoad = Console.GetCPULoad();
        if( Micros > 0 ) usleep( Micros );
    }

    int Code = 0;
    if( !Console.IsCPUHalted() )
    {
        printf( "RUNNER: frame limit reached (%ld frames)\n", Frames );
        Code = 3;
    }
    else if( (uint32_t)Console.CPU.InstructionPointer.AsBinary < (uint32_t)Constants::CartridgeProgramROMFirstAddress )
    {
        printf( "RUNNER: hardware error %d at %08X after %ld frames\n",
                Console.CPU.Registers[ 0 ].AsInteger, (unsigned)Console.CPU.Registers[ 1 ].AsBinary, Frames );
        Code = 1;
    }
    else
        printf( "RUNNER: program ended after %ld frames (busiest frame used %.1f%% of the CPU)\n", Frames, MaxLoad );

    Console.UnloadMemoryCard();
    return Code;
}
