#!/bin/bash
# Compiles the test programs (tests/v32dev/rom/*.c) and the stub BIOS with the Vircon32 tools.
# The compiler only looks for headers in the include folder next to its own program,
# so the tools are copied to the output folder first: nothing in an installed
# DevTools folder is touched.
#
# usage: build-roms.sh <folder with compile, assemble, packrom and include/> <lib/v32dev dir> <output dir>
set -e
TC=$(cd "$1" && pwd)
LIB=$(cd "$2" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$3
mkdir -p "$OUT/tc"
OUT=$(cd "$OUT" && pwd)

cp "$TC"/compile "$TC"/assemble "$TC"/packrom "$OUT/tc/"
rm -rf "$OUT/tc/include"; cp -r "$TC/include" "$OUT/tc/include"
cp "$LIB"/*.h "$OUT/tc/include/"

python3 "$HERE/make-bios.py" "$OUT/bios"
( cd "$OUT/bios" && "$OUT/tc/assemble" bios.asm -o bios.vbin && "$OUT/tc/packrom" bios.xml -o ../bios.v32 )

mkdir -p "$OUT/carts"
for c in "$HERE"/*.c; do
    n=$(basename "${c%.c}")
    cp "$c" "$OUT/carts/$n.c"
    ( cd "$OUT/carts" \
      && "$OUT/tc/compile" "$n.c" -o "$n.asm" \
      && "$OUT/tc/assemble" "$n.asm" -o "$n.vbin" \
      && printf '<?xml version="1.0" encoding="UTF-8" standalone="no" ?>\n<rom-definition version="1.0">\n  <rom type="cartridge" title="%s" version="1.0" />\n  <binary path="%s.vbin" />\n  <textures />\n  <sounds />\n</rom-definition>\n' "$n" "$n" > "$n.xml" \
      && "$OUT/tc/packrom" "$n.xml" -o "$n.v32" )
done
# the demo programs of lib/v32dev/demos, compiled where they are (they include "../x.h")
rm -rf "$OUT/lib"; mkdir -p "$OUT/lib" "$OUT/demos"
cp -r "$LIB"/* "$OUT/lib/"
for c in "$OUT"/lib/demos/*.c; do
    n=$(basename "${c%.c}")
    ( cd "$OUT/lib/demos" \
      && "$OUT/tc/compile" "$n.c" -o "$n.asm" \
      && "$OUT/tc/assemble" "$n.asm" -o "$n.vbin" \
      && "$OUT/tc/packrom" "$n.xml" -o "$OUT/demos/$n.v32" )
done
echo "roms built in $OUT: $(ls "$OUT/carts"/*.v32 "$OUT/demos"/*.v32 | xargs -n1 basename | tr '\n' ' ')"
