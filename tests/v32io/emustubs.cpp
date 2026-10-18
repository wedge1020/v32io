// Stand-in for the emulator's control object, which GamepadsInput.cpp only
// asks whether the console is on. Kept apart from emutest.cpp because
// Globals.hpp declares the real object there.
#include "Emulator/EmulatorControl.hpp"

alignas( 16 ) char Emulator[ 1 << 16 ];

bool EmulatorControl::IsPowerOn() { return true; }
