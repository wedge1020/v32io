#!/bin/bash
# v32dev tests. Builds the PATCHED ConsoleLogic from the given ComputerSoftware
# tree and runs, in order:
#   1. dev_unit    unit tests on the real console classes: partial saving, planning,
#                  host-area / header carve-outs, commit words last, inbound sync,
#                  heartbeat, activation rules, the RunNextFrame hook
#   2. dev_order   (Linux) intercepts write(2) and checks the REAL sequence of file
#                  writes: no header/host-area writes, all data before commit words
#   3. e2e         the program side (C++, real console classes) and the host side
#                  (Python, separate process) exchange messages in both directions
#                  on all 4 channels, with ring wrap-around, several runs
#   4. real roms   (needs the Vircon32 tools) REAL C programs, compiled with the real
#                  Vircon32 compiler against lib/*.h (storage.h, serial.h), run in the
#                  patched console code with its own RunNextFrame loop, against the
#                  real host tool (`v32dev.py serve`) in a separate process: images
#                  read and written and checked on both sides, no host, wrong device,
#                  a host that crashes and returns, a stream bigger than the ring
# Note: 3 shows that the whole thing works under load; the ordering and carve-out
# guarantees themselves are checked deterministically by 1 and 2.
#
# usage: run.sh <ComputerSoftware dir with v32dev.patch applied> [workdir] [e2e runs]
# env:   V32TOOLS=<folder with compile, assemble, packrom and include/> to use installed
#        Vircon32 tools; otherwise they are built from the given source tree (g++ only)
set -e
[ -n "$1" ] || { echo "usage: $0 <ComputerSoftware dir> [workdir] [e2e runs]"; exit 2; }
SRC=$(cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
W=${2:-$(mktemp -d)}
RUNS=${3:-5}
CL=$SRC/DesktopEmulator/ConsoleLogic
mkdir -p "$W/obj" "$W/tmp"
W=$(cd "$W" && pwd)

echo "== building ConsoleLogic from $CL"
for f in "$CL"/*.cpp; do
    g++ -std=c++11 -O1 -I"$CL" -I"$CL/.." -c "$f" -o "$W/obj/$(basename "${f%.cpp}").o"
done
for t in dev_unit dev_e2e_cart; do
    g++ -std=c++11 -O1 -I"$CL" -I"$CL/.." "$HERE/$t.cpp" "$W"/obj/*.o -o "$W/$t"
done

echo "== 1. unit tests"
TEST_TMPDIR="$W/tmp" "$W/dev_unit"

if [ "$(uname)" = "Linux" ]; then
    echo "== 2. write-order test"
    gcc -shared -fPIC -O1 "$HERE/interpose.c" -o "$W/interpose.so" -ldl
    g++ -std=c++11 -O1 -I"$CL" -I"$CL/.." "$HERE/dev_order.cpp" "$W"/obj/*.o -o "$W/dev_order"
    rm -f "$W/tmp/order.log"
    TEST_TMPDIR="$W/tmp" DEV_WRITE_LOG="$W/tmp/order.log" LD_PRELOAD="$W/interpose.so" "$W/dev_order"
else
    echo "== 2. write-order test: skipped (needs Linux LD_PRELOAD)"
fi

echo "== 3. end-to-end, separate host process ($RUNS runs)"
"$HERE/stress.sh" "$W/dev_e2e_cart" "$RUNS" 300 0 "$W/tmp/e2e"
echo "== 4. real cartridges"
if [ -n "$V32TOOLS" ]; then
    TC="$V32TOOLS"
else
    "$HERE/rom/build-toolchain.sh" "$SRC" "$W/toolchain" > "$W/toolchain.log" 2>&1 || { cat "$W/toolchain.log"; exit 1; }
    TC="$W/toolchain/bin"
fi
"$HERE/rom/build-roms.sh" "$TC" "$HERE/../lib" "$W/roms"
g++ -std=c++11 -O1 -I"$CL" -I"$CL/.." "$HERE/dev_rom_runner.cpp" "$W"/obj/*.o -o "$W/dev_rom_runner"
python3 -I "$HERE/dev_rom_test.py" "$W/dev_rom_runner" "$W/roms/bios.v32" "$W/roms/carts" "$W/tmp/rom"
echo "ALL TESTS PASSED"
