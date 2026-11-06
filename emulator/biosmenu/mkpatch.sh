#!/bin/bash
# usage: mkpatch.sh <pristine ComputerSoftware> <patched ComputerSoftware> > biosmenu.patch
A=$1; B=$2
for f in DesktopInfrastructure/FilePaths.cpp DesktopInfrastructure/FilePaths.hpp \
         Emulator/EmulatorControl.cpp Emulator/GUI.cpp Emulator/GUI.hpp \
         Emulator/Languages.cpp Emulator/Languages.hpp; do
  p=DesktopEmulator/$f
  diff -u --label "$p" --label "$p" "$A/$p" "$B/$p"
done
true
