#!/bin/bash
# usage: mkpatch-v32io.sh <pristine ComputerSoftware> <patched ComputerSoftware> > v32io.patch
A=$1; B=$2
for f in GUI.cpp GamepadsInput.cpp GamepadsInput.hpp Main.cpp Settings.cpp; do
  p=DesktopEmulator/Emulator/$f
  diff -u --strip-trailing-cr --label "$p" --label "$p" "$A/$p" "$B/$p"
done
true
