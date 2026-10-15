#!/bin/bash
# usage: mkpatch.sh <pristine ComputerSoftware> <patched ComputerSoftware> > v32dev.patch
A=$1; B=$2
for f in V32Console.cpp V32Console.hpp V32MemoryCardController.cpp V32MemoryCardController.hpp; do
  p=DesktopEmulator/ConsoleLogic/$f
  diff -u --label "$p" --label "$p" "$A/$p" "$B/$p"
done
true
