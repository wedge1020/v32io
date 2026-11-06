#!/bin/bash
# Builds and runs gui_menu_test: the v32dev server menu of the DesktopEmulator, driven
# by synthetic mouse clicks on the real Dear ImGui (see gui_menu_test.cpp).
# It needs no SDL2 or OpenGL: only the ComputerSoftware tree with the v32dev and
# v32dev-server patches applied, and g++.
#
# usage: build-gui-test.sh <ComputerSoftware dir> [workdir]
set -e
SRC=$(cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
W=${2:-$(mktemp -d)}
mkdir -p "$W/gui/obj" "$W/gui/tmp"
W=$(cd "$W/gui" && pwd)
DE=$SRC/DesktopEmulator
GUI=$DE/Emulator/GUI.cpp
CL=$DE/ConsoleLogic

# cut the code under test out of GUI.cpp: the dev server helpers and the menu
python3 -I - "$GUI" "$W/gui_menu_extract.inc" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
a = src.index("enum class DelayedFileActions")
b = src.index("// =============================================================================\n//      INDIVIDUAL MENUS IN THE MENU BAR")
c = src.index("// short text for what a channel serves, to label its menu")
d = src.index("void ProcessMenuMemoryCard()")
text = src[a:b] + "\n" + src[c:d]
# the menu entries are drawn through recorders that remember where they are
text = text.replace("ImGui::MenuItem(", "Rec::MenuItem(").replace("ImGui::BeginMenu(", "Rec::BeginMenu(")
open(sys.argv[2], "w").write(text)
PY

# the console (no OpenGL needed) and Dear ImGui's core, with its assertions on
for f in "$CL"/*.cpp; do
    o="$W/obj/$(basename "${f%.cpp}").o"
    [ "$o" -nt "$f" ] || g++ -std=c++11 -O1 -I"$CL" -I"$CL/.." -c "$f" -o "$o"
done
for f in imgui imgui_draw imgui_widgets imgui_tables imgui_demo; do
    o="$W/obj/imgui_$f.o"
    [ -f "$o" ] || g++ -std=c++11 -O1 -I"$DE/ExternalLibraries" -c "$DE/ExternalLibraries/imgui/$f.cpp" -o "$o"
done
g++ -std=c++11 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I"$W" -I"$CL" -I"$CL/.." -I"$DE/ExternalLibraries" \
    "$HERE/gui_menu_test.cpp" "$W"/obj/*.o -o "$W/gui_menu_test"
TEST_TMPDIR="$W/tmp" "$W/gui_menu_test"
