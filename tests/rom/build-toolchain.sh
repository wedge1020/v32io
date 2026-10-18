#!/bin/bash
# Builds the Vircon32 C compiler, assembler and ROM packer from a ComputerSoftware
# source tree, WITHOUT SDL2 (the tools only use it to find their own folder, which a
# 20 line stand-in does). Use this when the real DevTools are not installed.
#
# usage: build-toolchain.sh <ComputerSoftware dir> <output dir>
# result: <output dir>/bin/{compile,assemble,packrom} and <output dir>/bin/include/
set -e
SRC=$(cd "$1" && pwd)
OUT=$2
DT=$SRC/DevelopmentTools
INF=$DT/DevToolsInfrastructure
mkdir -p "$OUT/shim" "$OUT/bin"
OUT=$(cd "$OUT" && pwd)

cat > "$OUT/shim/SDL.h" <<'SHIM'
// minimal stand-in for the three SDL functions the tools use to find their own folder
#include <string>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <limits.h>
inline int SDL_Init(unsigned){return 0;}
inline void SDL_Quit(){}
inline void SDL_free(void*p){free(p);}
inline char* SDL_GetBasePath(){ char b[PATH_MAX]; ssize_t n=readlink("/proc/self/exe",b,sizeof b-1); if(n<0) return strdup("./"); b[n]=0; std::string s(b); s=s.substr(0,s.rfind('/')+1); return strdup(s.c_str()); }
SHIM

# the C compiler: the source list of the project's own CMakeLists.txt
CSRC=$(sed -n '/^set(C_COMPILER_SRC/,/)/p' "$DT/CMakeLists.txt" | grep -o '\${[A-Z_]*}/[A-Za-z]*\.cpp' \
       | sed "s|\${C_COMPILER_DIR}|$DT/CCompiler|; s|\${INFRASTRUCTURE_DIR}|$INF|")
g++ -std=c++11 -O1 -I"$OUT/shim" -I"$DT/CCompiler" -I"$INF" -I"$SRC/VirconDefinitions" $CSRC -o "$OUT/bin/compile"

g++ -std=c++11 -O1 -I"$OUT/shim" -I"$DT/Assembler" -I"$INF" -I"$SRC/VirconDefinitions" "$DT"/Assembler/*.cpp \
    "$INF/Definitions.cpp" "$INF/EnumStringConversions.cpp" "$INF/FilePaths.cpp" "$INF/StringFunctions.cpp" -o "$OUT/bin/assemble"

g++ -std=c++11 -O1 -I"$DT/ExternalLibraries/tinyxml2" -I"$DT/RomPacker" -I"$INF" -I"$SRC/VirconDefinitions" \
    "$DT/RomPacker/Main.cpp" "$DT/RomPacker/RomDefinition.cpp" "$INF/Definitions.cpp" "$INF/FilePaths.cpp" \
    "$INF/FileSignatures.cpp" "$INF/StringFunctions.cpp" "$DT/ExternalLibraries/tinyxml2/tinyxml2.cpp" -o "$OUT/bin/packrom"

mkdir -p "$OUT/bin/include"
cp "$DT"/Data/include/*.h "$OUT/bin/include/"
echo "toolchain built in $OUT/bin"
