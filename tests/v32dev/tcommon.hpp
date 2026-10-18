// Shared helpers for the v32dev tests. These use the real V32Console code.
#ifndef TCOMMON_HPP
#define TCOMMON_HPP

#include "V32Console.hpp"
#include "ExternalInterfaces.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace V32;
using std::string;
using std::vector;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK( cond ) do { g_checks++; if( !(cond) ) { g_failures++; \
    printf( "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); } } while( 0 )

static void InstallCallbacks()
{
    Callbacks::LogLine = []( const string& Line ) { if( getenv( "TEST_VERBOSE" ) ) printf( "  [log] %s\n", Line.c_str() ); };
    Callbacks::ThrowException = []( const string& Message ) { throw std::runtime_error( Message ); };

    // video callbacks are only needed so that powering on does not call a null pointer
    Callbacks::ClearScreen = []( V32::GPUColor ) {};
    Callbacks::DrawQuad = []( V32::GPUQuad& ) {};
    Callbacks::SetMultiplyColor = []( V32::GPUColor ) {};
    Callbacks::SetBlendingMode = []( int ) {};
    Callbacks::SelectTexture = []( int ) {};
    Callbacks::LoadTexture = []( int, void* ) {};
    Callbacks::UnloadCartridgeTextures = []() {};
    Callbacks::UnloadBiosTexture = []() {};
}

// file position of card word N (the file starts with an 8-byte signature)
static long WordOffset( int32_t Word ) { return 8 + (long)Word * 4; }

static int32_t FileWord( const string& Path, int32_t Word )
{
    std::ifstream f( Path, std::ios::binary );
    int32_t Value = 0;
    f.seekg( WordOffset( Word ) );
    f.read( (char*)&Value, 4 );
    return Value;
}

static void SetFileWord( const string& Path, int32_t Word, int32_t Value )
{
    std::fstream f( Path, std::ios::in | std::ios::out | std::ios::binary );
    f.seekp( WordOffset( Word ) );
    f.write( (const char*)&Value, 4 );
    f.flush();
}

// formats the mailbox header the way an external tool would
static void FormatMailbox( const string& Path )
{
    SetFileWord( Path, DEV::RegionStart + DEV::H_Magic,        DEV::Magic );
    SetFileWord( Path, DEV::RegionStart + DEV::H_Version,      DEV::Version );
    SetFileWord( Path, DEV::RegionStart + DEV::H_RegionWords,  DEV::RegionWords );
    SetFileWord( Path, DEV::RegionStart + DEV::H_Channels,     DEV::Channels );
    SetFileWord( Path, DEV::RegionStart + DEV::H_PartWords,    DEV::PartWords );
    SetFileWord( Path, DEV::RegionStart + DEV::H_ControlWords, DEV::ControlWords );
}

static V32Word W( int32_t Value ) { V32Word w; w.AsInteger = Value; return w; }

#endif
