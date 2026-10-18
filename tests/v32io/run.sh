#!/bin/sh
# End-to-end tests: firmware modes (built for this computer) -> simulated
# USB reports -> Vircon32 console logic (from the DesktopEmulator sources)
# running test cartridges built with keyboard.h / mouse.h.
#
# Needs: g++, gcc; the Vircon32 sources, the DevTools (compile, assemble,
# packrom) and TinyUSB (for its HID definitions).
#
#   VIRCON=/path/to/vircon32/ComputerSoftware
#   DEVTOOLS=/path/to/devtools/bin   (folder with compile, assemble, packrom)
#   TINYUSB=/path/to/pico-sdk/lib/tinyusb/src
#
# Optional:
#   BUILD=/path/to/build/folder      (default: ./build next to this script)
#   EMU=/path/to/patched/DesktopEmulator   also run the emulator device
#        test (see emutest.cpp); needs the SDL2 headers (SDL2INC)
#   SDL2INC=/path/to/SDL2/include/SDL2
#
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$HERE/../../firmware/RP2350-USB-A/src
LIB=$HERE/../../lib/v32io
B=${BUILD:-$HERE/build}
mkdir -p $B/cl $B/cart

for v in VIRCON DEVTOOLS TINYUSB; do
  eval "val=\$$v"
  [ -n "$val" ] || { echo "run.sh: $v is not set (see the top of this file)" >&2; exit 2; }
done

CL=$VIRCON/DesktopEmulator/ConsoleLogic
[ -e $VIRCON/DesktopEmulator/VirconDefinitions ] || ln -s ../VirconDefinitions $VIRCON/DesktopEmulator/VirconDefinitions

# test cartridges
cp $LIB/*.h $HERE/cart/*.c $HERE/compat/*.h $B/cart/
cd $B/cart
for t in t_kbd t_kbdread t_kbdevent t_oldread t_oldevent t_mouse; do
  $DEVTOOLS/compile $t.c -o $t.asm
  $DEVTOOLS/assemble $t.asm -o $t.vbin
  printf '<?xml version="1.0" encoding="UTF-8" standalone="no" ?>\n<rom-definition version="1.0">\n<rom type="cartridge" title="%s" version="1.0" />\n<binary path="%s.vbin" />\n<textures></textures>\n<sounds></sounds>\n</rom-definition>\n' $t $t > $t.xml
  $DEVTOOLS/packrom $t.xml -o ../$t.v32 > /dev/null
done
cd $B

# console logic, firmware modes, harness
for f in $CL/*.cpp; do [ -f cl/$(basename $f .cpp).o ] || g++ -c -O2 -std=c++17 -w $f -o cl/$(basename $f .cpp).o; done
for f in kbd mouse keymap compose; do gcc -c -O1 -Wall -I $HERE/stub -I $TINYUSB -I $FW $FW/$f.c -o fw_$f.o; done
g++ -O2 -std=c++17 -w -DBIOS_PATH="\"$VIRCON/SimplifiedEmulator/Data/Bios/StandardBios.v32\"" \
    -I $CL -I $HERE/stub -I $TINYUSB -I $FW $HERE/harness.cpp fw_*.o cl/*.o -o harness
gcc -O1 -Wall -I $FW $HERE/setup_test.c $FW/mouse.c -o setup_test
gcc -O1 -Wall -I $HERE/stub -I $TINYUSB -I $FW $HERE/compose_test.c $FW/compose.c -o compose_test

./setup_test
./compose_test
./harness

# the emulator's own v32kbd/v32mouse device code, driven with synthetic
# SDL events, against the same console logic and test cartridge
if [ -n "$EMU" ]; then
  [ -n "$SDL2INC" ] || { echo "run.sh: EMU needs SDL2INC" >&2; exit 2; }
  export HERE B VIRCON EMU SDL2INC SDL2LIB
  sh $HERE/emutest.sh
fi
