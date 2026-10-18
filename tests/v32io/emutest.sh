#!/bin/sh
# Builds and runs emutest.cpp. Called by run.sh when EMU is set; it
# expects run.sh's variables (HERE, B, VIRCON, EMU, SDL2INC) and the t_kbd
# cartridge already built in $B.
#   EMU      patched DesktopEmulator folder (v32io.patch applied)
#   SDL2INC  SDL2 headers; SDL2LIB: folder with libSDL2.a (or -lSDL2 must work)
set -e
# the console logic includes ../VirconDefinitions from its own folder
[ -e $EMU/VirconDefinitions ] || ln -s $VIRCON/DesktopEmulator/VirconDefinitions $EMU/VirconDefinitions
mkdir -p $B/emu
g++ -O1 -std=c++17 -w -I $EMU -I $EMU/ExternalLibraries -I $SDL2INC -c $EMU/Emulator/GamepadsInput.cpp -o $B/emu/GamepadsInput.o
g++ -O1 -std=c++17 -w -I $EMU -I $EMU/ExternalLibraries -I $SDL2INC -c $HERE/emutest.cpp -o $B/emu/emutest.o
g++ -O1 -std=c++17 -w -I $EMU -I $EMU/ExternalLibraries -I $SDL2INC -c $HERE/emustubs.cpp -o $B/emu/emustubs.o
g++ $B/emu/emutest.o $B/emu/emustubs.o $B/emu/GamepadsInput.o $B/cl/*.o ${SDL2LIB:+-L$SDL2LIB} -lSDL2 -lm -ldl -lpthread -lrt -o $B/emutest
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $B/emutest $VIRCON/SimplifiedEmulator/Data/Bios/StandardBios.v32 $B/t_kbd.v32
