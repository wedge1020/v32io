# ----------------------------------------------------------------------------
#   CMake build for v32io: Vircon32 keyboard and mouse through gamepad ports
#
#   Laid out after the v32c++ CMake setup (which follows the Vircon32
#   DevTools' own). This is an ALTERNATIVE to the base Makefile, which keeps
#   working exactly as before (`make`, `make test`, `make sysinstall`); the
#   two never share build products. Build out of tree:
#
#       mkdir build && cd build
#       cmake ..                 (MSYS2/MinGW: cmake -G "MSYS Makefiles" ..)
#       cmake --build .
#       ctest
#       cmake --install .        (may need sudo / an elevated shell)
#       cmake --build . --target uninstall
#       cpack                    (.tar.gz / .deb / .rpm, or .zip on Windows)
#
#   There is nothing to compile: v32io is a Vircon32 C library, made of
#   headers that the Vircon32 compiler reads. The build step packs the manual
#   pages, and builds the demo cartridges when the Vircon32 DevTools are found
#   (compile, assemble and packrom on the PATH, or in DEVTOOLS_DIR).
#
#   Default install locations (the DevTools install their headers in
#   Vircon32/DevTools/include; the community tools live in v32tools/):
#
#       Linux / macOS: /usr/local/Vircon32/DevTools/include/{v32io,keyboard,mouse}.h
#                      /usr/local/share/man/man3/*.3v32.gz
#                      /usr/local/Vircon32/v32tools/docs/v32io/
#       Windows      : <Program Files>/Vircon32/DevTools/include/{v32io,keyboard,mouse}.h
#                      <Program Files>/Vircon32/v32tools/docs/v32io/
#                       (the README, the other documents, the demos, and the
#                       manual pages in man3/)
#
#   The headers go in the DevTools' own include/ folder because that is where
#   the Vircon32 compiler looks for the ones a program includes (it has no
#   -I option): with them there, a program says #include "keyboard.h" and
#   nothing else. Change the root with -DCMAKE_INSTALL_PREFIX=<dir> when
#   configuring, or just the header folder with
#   -DV32IO_INSTALL_INCLUDEDIR=<dir> (relative to the prefix, or absolute).
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.13...3.31)

# Refuse an in-source build: CMake's generated Makefile would overwrite the
# project's own hand-written base Makefile.
#
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "v32io: in-source CMake builds are not supported -- they would "
        "overwrite the base Makefile. Build in a separate directory:\n"
        "    mkdir build && cd build && cmake ..\n"
        "First remove the CMakeCache.txt and CMakeFiles/ just created here.")
endif()

# ----------------------------------------------------------------------------
#   VERSION (single source: the VERSION file)
# ----------------------------------------------------------------------------

# The full version string, e.g. "20261014-dev", as `make version` stamps it
# into the headers and the manual pages. Configuration re-runs automatically
# when the file changes.
#
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" V32IO_VERSION LIMIT_COUNT 1)
if(NOT V32IO_VERSION MATCHES "^[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-")
    message(FATAL_ERROR "v32io: VERSION must be YYYYMMDD-dev or YYYYMMDD-release (it is '${V32IO_VERSION}')")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# project() only takes a numeric version: derive YYYY.MM.DD from the leading
# date (the "-dev"/"-release" status is kept in V32IO_VERSION).
#
if(V32IO_VERSION MATCHES "^([0-9][0-9][0-9][0-9])([0-9][0-9])([0-9][0-9])")
    set(V32IO_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
endif()

# ----------------------------------------------------------------------------
#   DEFINE THE PROJECT
# ----------------------------------------------------------------------------

project(v32io
        VERSION ${V32IO_NUMERIC_VERSION}
        DESCRIPTION "Keyboard and mouse for Vircon32, through gamepad ports"
        HOMEPAGE_URL "https://github.com/wedge1020/v32io"
        LANGUAGES NONE)

# ----------------------------------------------------------------------------
#   IDENTIFY HOST ENVIRONMENT
# ----------------------------------------------------------------------------

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TARGET_OS "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TARGET_OS "mac")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TARGET_OS "linux")
else()
    set(TARGET_OS "${CMAKE_SYSTEM_NAME}")
endif()

# ----------------------------------------------------------------------------
#   INSTALL LOCATIONS
# ----------------------------------------------------------------------------

# On Windows, CMake's default prefix would be "<Program Files>/v32io".
# Install under "<Program Files>/Vircon32" instead -- the DevTools' own root
# -- so the headers land in DevTools/include. An explicit
# -DCMAKE_INSTALL_PREFIX always wins. (Only when the default has the usual
# "<Program Files>/v32io" shape.)
#
if(TARGET_OS STREQUAL "windows" AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    get_filename_component(V32IO_PREFIX_NAME   "${CMAKE_INSTALL_PREFIX}" NAME)
    get_filename_component(V32IO_PROGRAM_FILES "${CMAKE_INSTALL_PREFIX}" DIRECTORY)
    if(V32IO_PREFIX_NAME STREQUAL PROJECT_NAME)
        set(CMAKE_INSTALL_PREFIX "${V32IO_PROGRAM_FILES}/Vircon32" CACHE PATH
            "Install path prefix, prepended onto install directories." FORCE)
    endif()
endif()

# (GNUInstallDirs is not used: it wants a language enabled to pick a library
# directory, and only the man directory is needed here; CMAKE_INSTALL_PREFIX
# defaults to /usr/local on Unix)
set(CMAKE_INSTALL_MANDIR "share/man")

if(TARGET_OS STREQUAL "windows")
    set(V32IO_TOOLS_ROOT           "v32tools")
    set(V32IO_DEFAULT_MANROOT      "v32tools/docs/v32io")
    set(V32IO_DEFAULT_INCLUDEDIR   "DevTools/include")
else()
    set(V32IO_TOOLS_ROOT           "Vircon32/v32tools")
    set(V32IO_DEFAULT_MANROOT      "${CMAKE_INSTALL_MANDIR}")
    set(V32IO_DEFAULT_INCLUDEDIR   "Vircon32/DevTools/include")
endif()
set(V32IO_DEFAULT_DOCDIR           "${V32IO_TOOLS_ROOT}/docs/v32io")

set(V32IO_INSTALL_INCLUDEDIR "${V32IO_DEFAULT_INCLUDEDIR}" CACHE STRING
    "Where v32io.h, keyboard.h and mouse.h go, relative to CMAKE_INSTALL_PREFIX or absolute (the Vircon32 DevTools' include folder)")
# The root of a manual tree: man/man3/*.3v32 go into its man3/.
set(V32IO_INSTALL_MANROOT "${V32IO_DEFAULT_MANROOT}" CACHE STRING
    "Manual tree the man3/ pages are installed under (relative to CMAKE_INSTALL_PREFIX)")
set(V32IO_INSTALL_DOCDIR "${V32IO_DEFAULT_DOCDIR}" CACHE STRING
    "Where the README, docs/ and the demos are installed (relative to CMAKE_INSTALL_PREFIX)")

# The absolute header directory.
#
if(IS_ABSOLUTE "${V32IO_INSTALL_INCLUDEDIR}")
    set(V32IO_HEADER_PATH "${V32IO_INSTALL_INCLUDEDIR}")
else()
    set(V32IO_HEADER_PATH "${CMAKE_INSTALL_PREFIX}/${V32IO_INSTALL_INCLUDEDIR}")
endif()

# the folder above it (the DevTools'), by name: the uninstall script prunes it
# after the header folder is gone, when "<folder>/.." no longer resolves
get_filename_component(V32IO_HEADER_PARENT "${V32IO_HEADER_PATH}" DIRECTORY)

# ----------------------------------------------------------------------------
#   THE VIRCON32 DEVTOOLS (optional: to build the demo cartridges)
# ----------------------------------------------------------------------------

set(V32IO_DEVTOOLS_DIR "" CACHE PATH
    "Folder with the Vircon32 compile, assemble and packrom (empty = look on the PATH)")
if(V32IO_DEVTOOLS_DIR)
    set(V32IO_FIND_HINTS HINTS "${V32IO_DEVTOOLS_DIR}" NO_DEFAULT_PATH)
else()
    set(V32IO_FIND_HINTS "")
endif()
find_program(V32IO_COMPILE  compile  ${V32IO_FIND_HINTS})
find_program(V32IO_ASSEMBLE assemble ${V32IO_FIND_HINTS})
find_program(V32IO_PACKROM  packrom  ${V32IO_FIND_HINTS})
if(V32IO_COMPILE AND V32IO_ASSEMBLE AND V32IO_PACKROM)
    set(V32IO_HAVE_DEVTOOLS TRUE)
else()
    set(V32IO_HAVE_DEVTOOLS FALSE)
endif()

# ----------------------------------------------------------------------------
#   SHOW BUILD INFORMATION IN PRETTY FORMAT
# ----------------------------------------------------------------------------

message(STATUS "******** v32io ********")
message(STATUS "Version: ${V32IO_VERSION}")
message(STATUS "Target OS: ${TARGET_OS}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS " * headers     -> ${V32IO_INSTALL_INCLUDEDIR}")
message(STATUS " * man pages   -> ${V32IO_INSTALL_MANROOT}/man3")
message(STATUS " * documents   -> ${V32IO_INSTALL_DOCDIR}")
if(V32IO_HAVE_DEVTOOLS)
    message(STATUS "Demos: built with ${V32IO_COMPILE}")
else()
    message(STATUS "Demos: the cartridges shipped in lib/demos/bin (Vircon32 DevTools not found)")
endif()

# ----------------------------------------------------------------------------
#   DEMO PROGRAMS
# ----------------------------------------------------------------------------

# The demos include "../keyboard.h" (they live beside the headers in the
# source tree). Installed next to the documents the include is rewritten to
# "keyboard.h", which the compiler finds in its own include/ folder.
#
set(V32IO_DEMOS v32kbd v32mouse)
set(V32IO_DEMO_CARTRIDGES "")
set(V32IO_DEMO_SOURCES "")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/demos")
foreach(V32IO_DEMO ${V32IO_DEMOS})
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lib/demos/${V32IO_DEMO}.c" V32IO_SOURCE)
    string(REGEX REPLACE "\"\\.\\./([a-z0-9]+\\.h)\"" "\"\\1\"" V32IO_SOURCE "${V32IO_SOURCE}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.c" "${V32IO_SOURCE}")
    list(APPEND V32IO_DEMO_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/lib/demos/${V32IO_DEMO}.c")

    if(V32IO_HAVE_DEVTOOLS)
        # compile reads the headers by their relative path, so it runs on the
        # source in the tree; assemble and packrom work in the build tree
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lib/demos/${V32IO_DEMO}.xml"
                       "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.xml" COPYONLY)
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.v32"
            COMMAND "${V32IO_COMPILE}" "${CMAKE_CURRENT_SOURCE_DIR}/lib/demos/${V32IO_DEMO}.c"
                    -o "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.asm"
            COMMAND "${V32IO_ASSEMBLE}" "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.asm"
                    -o "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.vbin"
            COMMAND "${V32IO_PACKROM}" "${V32IO_DEMO}.xml" -o "${V32IO_DEMO}.v32"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/demos"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lib/demos/${V32IO_DEMO}.c"
                    "${CMAKE_CURRENT_SOURCE_DIR}/lib/demos/${V32IO_DEMO}.xml"
                    "${CMAKE_CURRENT_SOURCE_DIR}/lib/v32io.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/lib/keyboard.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/lib/mouse.h"
            COMMENT "Building demo cartridge ${V32IO_DEMO}.v32"
            VERBATIM)
        list(APPEND V32IO_DEMO_CARTRIDGES "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32IO_DEMO}.v32")
    else()
        list(APPEND V32IO_DEMO_CARTRIDGES "${CMAKE_CURRENT_SOURCE_DIR}/lib/demos/bin/${V32IO_DEMO}.v32")
    endif()
endforeach()
if(V32IO_HAVE_DEVTOOLS)
    add_custom_target(demos ALL DEPENDS ${V32IO_DEMO_CARTRIDGES})
endif()

# ----------------------------------------------------------------------------
#   DEFINE THE INSTALL PROCESS
# ----------------------------------------------------------------------------

install(FILES lib/v32io.h lib/keyboard.h lib/mouse.h
        DESTINATION "${V32IO_INSTALL_INCLUDEDIR}"
        COMPONENT headers)

# Every page in man/man3. `make version` in the base Makefile stamps the
# version and date into them.
#
# On Linux / macOS they are installed as the Makefile's `sysinstall` does:
# gzip -9'd at build time (target `manpages`, into <build>/man), mode 0644,
# and each alias page (a one-line `.so man3/<page>`) as a symbolic link to
# its target's .gz. The links are made at configure time; CMake installs a
# symbolic link as a link, so the uninstall manifest lists them too. On
# Windows, or with no gzip found, the pages are installed as they are.
file(GLOB V32IO_MAN3 "${CMAKE_CURRENT_SOURCE_DIR}/man/man3/*.3v32")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${V32IO_MAN3})
if(NOT TARGET_OS STREQUAL "windows")
    find_program(V32IO_GZIP gzip)
endif()
set(V32IO_MAN_MODE OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
if(V32IO_GZIP)
    set(V32IO_OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/man/man3")
    file(MAKE_DIRECTORY "${V32IO_OUTDIR}")
    set(V32IO_GZ_PAGES "")
    set(V32IO_LINK_PAGES "")
    set(V32IO_ALL_GZ "")
    foreach(V32IO_PAGE ${V32IO_MAN3})
        get_filename_component(V32IO_NAME "${V32IO_PAGE}" NAME)
        file(STRINGS "${V32IO_PAGE}" V32IO_FIRST LIMIT_COUNT 1)
        if(V32IO_FIRST MATCHES "^\\.so [^/]*/(.*)$")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink
                            "${CMAKE_MATCH_1}.gz" "${V32IO_OUTDIR}/${V32IO_NAME}.gz")
            list(APPEND V32IO_LINK_PAGES "${V32IO_OUTDIR}/${V32IO_NAME}.gz")
        else()
            add_custom_command(
                OUTPUT "${V32IO_OUTDIR}/${V32IO_NAME}.gz"
                COMMAND "${CMAKE_COMMAND}" -E copy "${V32IO_PAGE}" "${V32IO_OUTDIR}/${V32IO_NAME}"
                COMMAND "${V32IO_GZIP}" -9 -n -f "${V32IO_OUTDIR}/${V32IO_NAME}"
                DEPENDS "${V32IO_PAGE}"
                COMMENT "gzip man3/${V32IO_NAME}"
                VERBATIM)
            list(APPEND V32IO_GZ_PAGES "${V32IO_OUTDIR}/${V32IO_NAME}.gz")
            list(APPEND V32IO_ALL_GZ "${V32IO_OUTDIR}/${V32IO_NAME}.gz")
        endif()
    endforeach()
    install(FILES ${V32IO_GZ_PAGES} ${V32IO_LINK_PAGES}
            DESTINATION "${V32IO_INSTALL_MANROOT}/man3"
            PERMISSIONS ${V32IO_MAN_MODE}
            COMPONENT documentation)
    add_custom_target(manpages ALL DEPENDS ${V32IO_ALL_GZ})
else()
    install(FILES ${V32IO_MAN3}
            DESTINATION "${V32IO_INSTALL_MANROOT}/man3"
            PERMISSIONS ${V32IO_MAN_MODE}
            COMPONENT documentation)
endif()

install(FILES README.md LICENSE docs/PROTOCOLS.md docs/COMPOSE.md
        DESTINATION "${V32IO_INSTALL_DOCDIR}"
        COMPONENT documentation)
install(FILES lib/README.md
        DESTINATION "${V32IO_INSTALL_DOCDIR}"
        RENAME LIBRARY.md
        COMPONENT documentation)
install(FILES ${V32IO_DEMO_SOURCES} ${V32IO_DEMO_CARTRIDGES}
        DESTINATION "${V32IO_INSTALL_DOCDIR}/demos"
        COMPONENT documentation)
foreach(V32IO_DEMO ${V32IO_DEMOS})
    install(FILES lib/demos/${V32IO_DEMO}.xml
            DESTINATION "${V32IO_INSTALL_DOCDIR}/demos"
            COMPONENT documentation)
endforeach()

# Make everything installed readable by ordinary users, whatever umask the
# install ran under: sudo often has 077, and the directories CMake creates
# are masked by it. Only adds go+rX -- never removes a permission -- on the
# documents, the shared Vircon32 folders above them, the header folder, the
# prefix and the man3 folder. Linux / macOS only; DESTDIR-aware.
if(NOT TARGET_OS STREQUAL "windows")
    set(V32IO_READABLE_TREES
        "${CMAKE_INSTALL_PREFIX}/${V32IO_INSTALL_DOCDIR}")
    set(V32IO_READABLE_DIRS
        "${V32IO_HEADER_PATH}"
        "${V32IO_HEADER_PATH}/.."
        "${V32IO_HEADER_PATH}/../.."
        "${CMAKE_INSTALL_PREFIX}"
        "${CMAKE_INSTALL_PREFIX}/${V32IO_INSTALL_DOCDIR}/.."
        "${CMAKE_INSTALL_PREFIX}/${V32IO_INSTALL_DOCDIR}/../.."
        "${CMAKE_INSTALL_PREFIX}/${V32IO_INSTALL_MANROOT}"
        "${CMAKE_INSTALL_PREFIX}/${V32IO_INSTALL_MANROOT}/man3")
    install(CODE "
        foreach(T ${V32IO_READABLE_TREES})
            if(IS_DIRECTORY \"\$ENV{DESTDIR}\${T}\")
                execute_process(COMMAND chmod -R go+rX \"\$ENV{DESTDIR}\${T}\")
            endif()
        endforeach()
        foreach(D ${V32IO_READABLE_DIRS})
            if(IS_DIRECTORY \"\$ENV{DESTDIR}\${D}\")
                execute_process(COMMAND chmod go+rx \"\$ENV{DESTDIR}\${D}\")
            endif()
        endforeach()
    " COMPONENT documentation)
endif()

# `cmake --build . --target uninstall` removes whatever the last install put
# down (CMake has no built-in uninstall; this replays install_manifest.txt),
# then the directories that leaves empty.
#
if(TARGET_OS STREQUAL "windows")
    set(V32IO_UNIX_VIRCON32_DIR "")                               # that is the prefix
    # the manual pages live under the documents there: prune their folder
    set(V32IO_UNINSTALL_MAN3 "${CMAKE_INSTALL_PREFIX}/${V32IO_INSTALL_MANROOT}/man3")
else()
    set(V32IO_UNIX_VIRCON32_DIR "${CMAKE_INSTALL_PREFIX}/Vircon32")
    # share/man/man3 is the system's: never removed
    set(V32IO_UNINSTALL_MAN3 "")
endif()
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        @ONLY)
    add_custom_target(uninstall
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        COMMENT "Uninstalling v32io")
endif()

# ----------------------------------------------------------------------------
#   TESTS (ctest)
# ----------------------------------------------------------------------------

# The same checks as the base Makefile's `test`: the version stamps in
# VERSION, lib/v32io.h and the manual pages agree; the manual pages are well
# formed (and with the Vircon32 compiler around, their examples compile); the
# demos compile. Needs sh and python3 for the first two. The end-to-end tests
# of v32io-tests are `make realcheck`.
#
option(BUILD_TESTING "Define the ctest tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_program(V32IO_SH sh)
    find_package(Python3 COMPONENTS Interpreter)
    if(V32IO_SH)
        add_test(NAME version
                 COMMAND "${V32IO_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-version.sh")
    endif()
    if(Python3_Interpreter_FOUND)
        add_test(NAME manpages
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint-man.py")
        if(V32IO_HAVE_DEVTOOLS)
            get_filename_component(V32IO_COMPILE_DIR "${V32IO_COMPILE}" DIRECTORY)
            add_test(NAME manpage-examples
                     COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint-man.py"
                             --compile "${V32IO_COMPILE_DIR}")
        endif()
    endif()
    if(V32IO_HAVE_DEVTOOLS)
        # the demos are built by the `demos` target: this test builds them
        add_test(NAME demos
                 COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}" --target demos)
    endif()
endif()

# ----------------------------------------------------------------------------
#   DEFINE THE PACKAGE CREATION WITH CPACK
# ----------------------------------------------------------------------------

# `cpack` (from the build directory) makes packages, as the Vircon32 DevTools
# do: .deb/.rpm/.tar.gz on Linux, .tar.gz on macOS, .zip on Windows. They
# carry the full version string in their file name; the package-manager
# version is the numeric YYYY.MM.DD date. The library is headers, so the
# packages are not specific to a processor.
#
set(CPACK_PACKAGE_NAME "v32io")
set(CPACK_PACKAGE_VENDOR "Matthew Haas")
set(CPACK_PACKAGE_CONTACT "Matthew Haas")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Keyboard and mouse for Vircon32, through gamepad ports")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wedge1020/v32io")
set(CPACK_PACKAGE_FILE_NAME "v32io-${V32IO_VERSION}-${TARGET_OS}")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(TARGET_OS STREQUAL "linux")
    set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Matthew Haas")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")
    set(CPACK_RPM_PACKAGE_LICENSE "BSD-3-Clause")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "noarch")
    set(CPACK_GENERATOR "TGZ")
    find_program(V32IO_DPKG_DEB dpkg-deb)
    if(V32IO_DPKG_DEB)
        list(APPEND CPACK_GENERATOR "DEB")
    endif()
    find_program(V32IO_RPMBUILD rpmbuild)
    if(V32IO_RPMBUILD)
        list(APPEND CPACK_GENERATOR "RPM")
    endif()
elseif(TARGET_OS STREQUAL "windows")
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
