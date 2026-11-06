#!/bin/bash
# usage: mkpatch.sh <ComputerSoftware with v32dev.patch applied> <same with v32dev-server.patch also applied> > v32dev-server.patch
# (new files are made by the patch too; files that are not in the first tree are diffed against nothing)
A=$1; B=$2
for p in DesktopEmulator/ConsoleLogic/CMakeLists.txt \
         DesktopEmulator/ConsoleLogic/V32Console.cpp \
         DesktopEmulator/ConsoleLogic/V32Console.hpp \
         DesktopEmulator/ConsoleLogic/V32DevServer.cpp \
         DesktopEmulator/ConsoleLogic/V32DevServer.hpp \
         DesktopEmulator/ConsoleLogic/V32MemoryCardController.cpp \
         DesktopEmulator/ConsoleLogic/V32MemoryCardController.hpp \
         DesktopEmulator/Emulator/GUI.cpp \
         DesktopEmulator/Emulator/Settings.cpp; do
  if [ -e "$A/$p" ]; then
    diff -u --strip-trailing-cr --label "$p" --label "$p" "$A/$p" "$B/$p"
  else
    # a new file: "/dev/null" is what makes patch create it, and remove it again with -R
    diff -uN --strip-trailing-cr --label /dev/null --label "$p" "$A/$p" "$B/$p"
  fi
done
true
