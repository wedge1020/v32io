# ----------------------------------------------------------------------------
#   CMake build for v32io and v32dev: Vircon32 peripherals and external I/O
#
#     v32io    keyboard and mouse, read through gamepad ports
#     v32dev   files, a terminal and more, through the memory card
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
#   WHAT IS INSTALLED. There are three install targets, chosen when
#   configuring with V32_COMPONENTS:
#
#       cmake ..                              v32io only (the default)
#       cmake -DV32_COMPONENTS=v32dev ..      v32dev only
#       cmake -DV32_COMPONENTS=both ..        both
#
#   Only the manual pages, documents and demos of a component that is chosen
#   are installed (and built), together with its headers. The install can
#   also be restricted afterwards with `cmake --install . --component v32io`
#   (or v32dev) for a component that was chosen.
#
#   There is nothing to compile: both are Vircon32 C libraries, made of
#   headers that the Vircon32 compiler reads. The build step packs the manual
#   pages, and builds the demo cartridges when the Vircon32 DevTools are found
#   (compile, assemble and packrom on the PATH, or in V32_DEVTOOLS_DIR).
#
#   Default install locations (the DevTools install their headers in
#   Vircon32/DevTools/include; the community tools live in v32tools/). The
#   headers of both components go directly in that include folder, with no
#   subfolders:
#
#       Linux / macOS: /usr/local/Vircon32/DevTools/include/{v32io,keyboard,mouse}.h
#                      /usr/local/Vircon32/DevTools/include/{v32dev,storage,serial}.h
#                      /usr/local/share/man/man3/*.3v32.gz
#                      /usr/local/Vircon32/v32tools/docs/{v32io,v32dev}/
#       Windows      : <Program Files>/Vircon32/DevTools/include/ (the same headers)
#                      <Program Files>/Vircon32/v32tools/docs/{v32io,v32dev}/
#                       (the README, the other documents, the demos, and the
#                       manual pages in man3/)
#
#   The headers go in the DevTools' own include/ folder because that is where
#   the Vircon32 compiler looks for the ones a program includes (it has no
#   -I option): with them there, a program says #include "keyboard.h" or
#   #include "storage.h" and nothing else. Change the root with
#   -DCMAKE_INSTALL_PREFIX=<dir> when configuring, just the header folder with
#   -DV32_INSTALL_INCLUDEDIR=<dir> (relative to the prefix, or absolute), or
#   the name of the DevTools folder with -DV32_DEVTOOLS_DIRNAME=<name>. (The
#   folder is called DevTools once installed; DevelopmentTools is the name of
#   its source folder in the ComputerSoftware repository.)
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
#   VERSION (single source: the VERSION file, for both components)
# ----------------------------------------------------------------------------

# The full version string, e.g. "20261014-dev", as `make version` stamps it
# into the headers and the manual pages. Configuration re-runs automatically
# when the file changes.
#
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" V32_VERSION LIMIT_COUNT 1)
if(NOT V32_VERSION MATCHES "^[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-")
    message(FATAL_ERROR "v32io: VERSION must be YYYYMMDD-dev or YYYYMMDD-release (it is '${V32_VERSION}')")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# project() only takes a numeric version: derive YYYY.MM.DD from the leading
# date (the "-dev"/"-release" status is kept in V32_VERSION).
#
if(V32_VERSION MATCHES "^([0-9][0-9][0-9][0-9])([0-9][0-9])([0-9][0-9])")
    set(V32_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
endif()

# ----------------------------------------------------------------------------
#   DEFINE THE PROJECT
# ----------------------------------------------------------------------------

project(v32io
        VERSION ${V32_NUMERIC_VERSION}
        DESCRIPTION "Keyboard and mouse through gamepad ports (v32io), and files, a terminal and more through the memory card (v32dev), for Vircon32"
        HOMEPAGE_URL "https://github.com/wedge1020/v32io"
        LANGUAGES NONE)

# ----------------------------------------------------------------------------
#   WHAT TO BUILD AND INSTALL: v32io (default), v32dev or both
# ----------------------------------------------------------------------------

set(V32_COMPONENTS "v32io" CACHE STRING
    "What to build and install: v32io (default), v32dev or both")
set_property(CACHE V32_COMPONENTS PROPERTY STRINGS v32io v32dev both)

string(TOLOWER "${V32_COMPONENTS}" V32_COMPONENTS_LOWER)
if(V32_COMPONENTS_LOWER STREQUAL "both")
    set(V32_ENABLED v32io v32dev)
elseif(V32_COMPONENTS_LOWER STREQUAL "v32io" OR V32_COMPONENTS_LOWER STREQUAL "v32dev")
    set(V32_ENABLED ${V32_COMPONENTS_LOWER})
else()
    message(FATAL_ERROR "v32io: V32_COMPONENTS must be v32io, v32dev or both (it is '${V32_COMPONENTS}')")
endif()
list(JOIN V32_ENABLED "-" V32_ENABLED_NAME)
list(JOIN V32_ENABLED " " V32_ENABLED_TEXT)

# What each component is made of. Headers are in lib/<component> and are
# installed flat; the demos are lib/<component>/demos/<name>.{c,xml} with a
# cartridge in lib/<component>/demos/bin; documents are listed relative to
# the top of the project.
set(V32_v32io_HEADERS   v32io.h keyboard.h mouse.h)
set(V32_v32io_DEMOS     v32kbd v32mouse)
set(V32_v32io_DOCS      README.md LICENSE docs/v32io/PROTOCOLS.md docs/v32io/COMPOSE.md)
set(V32_v32dev_HEADERS  v32dev.h storage.h serial.h)
set(V32_v32dev_DEMOS    storage)
set(V32_v32dev_DOCS     README.md LICENSE docs/v32dev/PROTOCOL.md)

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
    get_filename_component(V32_PREFIX_NAME   "${CMAKE_INSTALL_PREFIX}" NAME)
    get_filename_component(V32_PROGRAM_FILES "${CMAKE_INSTALL_PREFIX}" DIRECTORY)
    if(V32_PREFIX_NAME STREQUAL PROJECT_NAME)
        set(CMAKE_INSTALL_PREFIX "${V32_PROGRAM_FILES}/Vircon32" CACHE PATH
            "Install path prefix, prepended onto install directories." FORCE)
    endif()
endif()

# (GNUInstallDirs is not used: it wants a language enabled to pick a library
# directory, and only the man directory is needed here; CMAKE_INSTALL_PREFIX
# defaults to /usr/local on Unix)
set(CMAKE_INSTALL_MANDIR "share/man")

# The names this project had when it was only v32io (the options below
# started with V32IO_) are still understood on the first configure.
if(DEFINED V32IO_INSTALL_INCLUDEDIR AND NOT DEFINED V32_INSTALL_INCLUDEDIR)
    set(V32_INSTALL_INCLUDEDIR "${V32IO_INSTALL_INCLUDEDIR}" CACHE STRING "")
endif()
if(DEFINED V32IO_DEVTOOLS_DIR AND NOT DEFINED V32_DEVTOOLS_DIR)
    set(V32_DEVTOOLS_DIR "${V32IO_DEVTOOLS_DIR}" CACHE PATH "")
endif()

set(V32_DEVTOOLS_DIRNAME "DevTools" CACHE STRING
    "Name of the Vircon32 DevTools folder, once installed (its source folder is called DevelopmentTools)")

if(TARGET_OS STREQUAL "windows")
    set(V32_TOOLS_ROOT           "v32tools")
    set(V32_DEFAULT_INCLUDEDIR   "${V32_DEVTOOLS_DIRNAME}/include")
else()
    set(V32_TOOLS_ROOT           "Vircon32/v32tools")
    set(V32_DEFAULT_INCLUDEDIR   "Vircon32/${V32_DEVTOOLS_DIRNAME}/include")
endif()

# Where the headers of both components go: flat, in the include folder of
# the DevTools.
set(V32_INSTALL_INCLUDEDIR "${V32_DEFAULT_INCLUDEDIR}" CACHE STRING
    "Where the headers of v32io and v32dev go, with no subfolders, relative to CMAKE_INSTALL_PREFIX or absolute (the Vircon32 DevTools' include folder)")
# The folder with a documents folder for each component (<root>/v32io, <root>/v32dev).
set(V32_INSTALL_DOCROOT "${V32_TOOLS_ROOT}/docs" CACHE STRING
    "Where the documents are installed, in a folder for each component (relative to CMAKE_INSTALL_PREFIX)")
# The root of a manual tree: the man/<component>/man3/*.3v32 pages go into its
# man3/. Empty means the default: share/man on Linux and macOS, and the
# documents folder of each component on Windows.
set(V32_INSTALL_MANROOT "" CACHE STRING
    "Manual tree the man3/ pages are installed under (relative to CMAKE_INSTALL_PREFIX; empty: share/man, or the documents folder on Windows)")

# The absolute header directory.
#
if(IS_ABSOLUTE "${V32_INSTALL_INCLUDEDIR}")
    set(V32_HEADER_PATH "${V32_INSTALL_INCLUDEDIR}")
else()
    set(V32_HEADER_PATH "${CMAKE_INSTALL_PREFIX}/${V32_INSTALL_INCLUDEDIR}")
endif()

# the folder above it (the DevTools'), by name: the uninstall script prunes it
# after the header folder is gone, when "<folder>/.." no longer resolves
get_filename_component(V32_HEADER_PARENT "${V32_HEADER_PATH}" DIRECTORY)

# ----------------------------------------------------------------------------
#   THE VIRCON32 DEVTOOLS (optional: to build the demo cartridges)
# ----------------------------------------------------------------------------

set(V32_DEVTOOLS_DIR "" CACHE PATH
    "Folder with the Vircon32 compile, assemble and packrom (empty = look on the PATH)")
if(V32_DEVTOOLS_DIR)
    set(V32_FIND_HINTS HINTS "${V32_DEVTOOLS_DIR}" NO_DEFAULT_PATH)
else()
    set(V32_FIND_HINTS "")
endif()
find_program(V32_COMPILE  compile  ${V32_FIND_HINTS})
find_program(V32_ASSEMBLE assemble ${V32_FIND_HINTS})
find_program(V32_PACKROM  packrom  ${V32_FIND_HINTS})
if(V32_COMPILE AND V32_ASSEMBLE AND V32_PACKROM)
    set(V32_HAVE_DEVTOOLS TRUE)
else()
    set(V32_HAVE_DEVTOOLS FALSE)
endif()

# ----------------------------------------------------------------------------
#   SHOW BUILD INFORMATION IN PRETTY FORMAT
# ----------------------------------------------------------------------------

message(STATUS "******** v32io / v32dev ********")
message(STATUS "Version: ${V32_VERSION}")
message(STATUS "Target OS: ${TARGET_OS}")
message(STATUS "Components: ${V32_ENABLED_TEXT}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS " * headers     -> ${V32_INSTALL_INCLUDEDIR}")
foreach(V32_C ${V32_ENABLED})
    if(V32_INSTALL_MANROOT)
        message(STATUS " * ${V32_C} man pages -> ${V32_INSTALL_MANROOT}/man3")
    elseif(TARGET_OS STREQUAL "windows")
        message(STATUS " * ${V32_C} man pages -> ${V32_INSTALL_DOCROOT}/${V32_C}/man3")
    else()
        message(STATUS " * ${V32_C} man pages -> ${CMAKE_INSTALL_MANDIR}/man3")
    endif()
    message(STATUS " * ${V32_C} documents -> ${V32_INSTALL_DOCROOT}/${V32_C}")
endforeach()
if(V32_HAVE_DEVTOOLS)
    message(STATUS "Demos: built with ${V32_COMPILE}")
else()
    message(STATUS "Demos: the cartridges shipped in lib/*/demos/bin (Vircon32 DevTools not found)")
endif()

# ----------------------------------------------------------------------------
#   PER COMPONENT: DEMO PROGRAMS, MANUAL PAGES, INSTALL RULES
# ----------------------------------------------------------------------------

if(NOT TARGET_OS STREQUAL "windows")
    find_program(V32_GZIP gzip)
endif()
set(V32_MAN_MODE OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

set(V32_ALL_CARTRIDGES "")     # every demo cartridge to build
set(V32_ALL_GZ "")             # every gzipped manual page to build
set(V32_PRUNE "")              # directories the uninstall removes when empty
set(V32_READABLE_TREES "")
set(V32_READABLE_DIRS "${V32_HEADER_PATH}" "${V32_HEADER_PATH}/.." "${V32_HEADER_PATH}/../.."
                      "${CMAKE_INSTALL_PREFIX}")

foreach(V32_C ${V32_ENABLED})
    set(V32_LIBDIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/${V32_C}")
    set(V32_DOCDIR "${V32_INSTALL_DOCROOT}/${V32_C}")
    if(V32_INSTALL_MANROOT)
        set(V32_MANROOT "${V32_INSTALL_MANROOT}")
    elseif(TARGET_OS STREQUAL "windows")
        set(V32_MANROOT "${V32_DOCDIR}")
    else()
        set(V32_MANROOT "${CMAKE_INSTALL_MANDIR}")
    endif()

    # ------------------------------------------------------------------------
    #   The demo programs
    # ------------------------------------------------------------------------

    # The demos include "../keyboard.h" (they live beside the headers in the
    # source tree). Installed next to the documents the include is rewritten
    # to "keyboard.h", which the compiler finds in its own include/ folder.
    #
    set(V32_DEMO_CARTRIDGES "")
    set(V32_DEMO_SOURCES "")
    set(V32_HEADER_PATHS "")
    foreach(V32_H ${V32_${V32_C}_HEADERS})
        list(APPEND V32_HEADER_PATHS "${V32_LIBDIR}/${V32_H}")
    endforeach()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}")
    foreach(V32_DEMO ${V32_${V32_C}_DEMOS})
        file(READ "${V32_LIBDIR}/demos/${V32_DEMO}.c" V32_SOURCE)
        string(REGEX REPLACE "\"\\.\\./([a-z0-9]+\\.h)\"" "\"\\1\"" V32_SOURCE "${V32_SOURCE}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.c" "${V32_SOURCE}")
        list(APPEND V32_DEMO_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.c")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${V32_LIBDIR}/demos/${V32_DEMO}.c")

        if(V32_HAVE_DEVTOOLS)
            # compile reads the headers by their relative path, so it runs on
            # the source in the tree; assemble and packrom work in the build
            # tree
            configure_file("${V32_LIBDIR}/demos/${V32_DEMO}.xml"
                           "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.xml" COPYONLY)
            add_custom_command(
                OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.v32"
                COMMAND "${V32_COMPILE}" "${V32_LIBDIR}/demos/${V32_DEMO}.c"
                        -o "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.asm"
                COMMAND "${V32_ASSEMBLE}" "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.asm"
                        -o "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.vbin"
                COMMAND "${V32_PACKROM}" "${V32_DEMO}.xml" -o "${V32_DEMO}.v32"
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}"
                DEPENDS "${V32_LIBDIR}/demos/${V32_DEMO}.c"
                        "${V32_LIBDIR}/demos/${V32_DEMO}.xml"
                        ${V32_HEADER_PATHS}
                COMMENT "Building demo cartridge ${V32_DEMO}.v32"
                VERBATIM)
            list(APPEND V32_DEMO_CARTRIDGES "${CMAKE_CURRENT_BINARY_DIR}/demos/${V32_C}/${V32_DEMO}.v32")
        else()
            list(APPEND V32_DEMO_CARTRIDGES "${V32_LIBDIR}/demos/bin/${V32_DEMO}.v32")
        endif()
    endforeach()
    if(V32_HAVE_DEVTOOLS)
        list(APPEND V32_ALL_CARTRIDGES ${V32_DEMO_CARTRIDGES})
    endif()

    # ------------------------------------------------------------------------
    #   Install: the headers, flat in the include folder of the DevTools
    # ------------------------------------------------------------------------

    install(FILES ${V32_HEADER_PATHS}
            DESTINATION "${V32_INSTALL_INCLUDEDIR}"
            COMPONENT ${V32_C})

    # ------------------------------------------------------------------------
    #   Install: the manual pages of this component, every page in
    #   man/<component>/man3. `make version` in the base Makefile stamps the
    #   version and date into them.
    #
    #   On Linux / macOS they are installed as the Makefile's `sysinstall`
    #   does: gzip -9'd at build time (target `manpages`, into <build>/man),
    #   mode 0644, and each alias page (a one-line `.so man3/<page>`) as a
    #   symbolic link to its target's .gz. The links are made at configure
    #   time; CMake installs a symbolic link as a link, so the uninstall
    #   manifest lists them too. On Windows, or with no gzip found, the pages
    #   are installed as they are.
    # ------------------------------------------------------------------------

    file(GLOB V32_MAN3 "${CMAKE_CURRENT_SOURCE_DIR}/man/${V32_C}/man3/*.3v32")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${V32_MAN3})
    if(V32_GZIP)
        set(V32_OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/man/${V32_C}/man3")
        file(MAKE_DIRECTORY "${V32_OUTDIR}")
        set(V32_GZ_PAGES "")
        set(V32_LINK_PAGES "")
        foreach(V32_PAGE ${V32_MAN3})
            get_filename_component(V32_NAME "${V32_PAGE}" NAME)
            file(STRINGS "${V32_PAGE}" V32_FIRST LIMIT_COUNT 1)
            if(V32_FIRST MATCHES "^\\.so [^/]*/(.*)$")
                execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink
                                "${CMAKE_MATCH_1}.gz" "${V32_OUTDIR}/${V32_NAME}.gz")
                list(APPEND V32_LINK_PAGES "${V32_OUTDIR}/${V32_NAME}.gz")
            else()
                add_custom_command(
                    OUTPUT "${V32_OUTDIR}/${V32_NAME}.gz"
                    COMMAND "${CMAKE_COMMAND}" -E copy "${V32_PAGE}" "${V32_OUTDIR}/${V32_NAME}"
                    COMMAND "${V32_GZIP}" -9 -n -f "${V32_OUTDIR}/${V32_NAME}"
                    DEPENDS "${V32_PAGE}"
                    COMMENT "gzip ${V32_C} man3/${V32_NAME}"
                    VERBATIM)
                list(APPEND V32_GZ_PAGES "${V32_OUTDIR}/${V32_NAME}.gz")
                list(APPEND V32_ALL_GZ "${V32_OUTDIR}/${V32_NAME}.gz")
            endif()
        endforeach()
        install(FILES ${V32_GZ_PAGES} ${V32_LINK_PAGES}
                DESTINATION "${V32_MANROOT}/man3"
                PERMISSIONS ${V32_MAN_MODE}
                COMPONENT ${V32_C})
    else()
        install(FILES ${V32_MAN3}
                DESTINATION "${V32_MANROOT}/man3"
                PERMISSIONS ${V32_MAN_MODE}
                COMPONENT ${V32_C})
    endif()

    # ------------------------------------------------------------------------
    #   Install: the documents and the demos of this component
    # ------------------------------------------------------------------------

    install(FILES ${V32_${V32_C}_DOCS}
            DESTINATION "${V32_DOCDIR}"
            COMPONENT ${V32_C})
    install(FILES lib/${V32_C}/README.md
            DESTINATION "${V32_DOCDIR}"
            RENAME LIBRARY.md
            COMPONENT ${V32_C})
    install(FILES ${V32_DEMO_SOURCES} ${V32_DEMO_CARTRIDGES}
            DESTINATION "${V32_DOCDIR}/demos"
            COMPONENT ${V32_C})
    foreach(V32_DEMO ${V32_${V32_C}_DEMOS})
        install(FILES lib/${V32_C}/demos/${V32_DEMO}.xml
                DESTINATION "${V32_DOCDIR}/demos"
                COMPONENT ${V32_C})
    endforeach()

    # what the uninstall prunes (deepest first), and what is made readable
    list(APPEND V32_PRUNE "${CMAKE_INSTALL_PREFIX}/${V32_DOCDIR}/demos")
    if(TARGET_OS STREQUAL "windows" AND NOT V32_INSTALL_MANROOT)
        # the manual pages live under the documents there: prune their folder
        list(APPEND V32_PRUNE "${CMAKE_INSTALL_PREFIX}/${V32_MANROOT}/man3")
    endif()
    list(APPEND V32_PRUNE "${CMAKE_INSTALL_PREFIX}/${V32_DOCDIR}")
    list(APPEND V32_READABLE_TREES "${CMAKE_INSTALL_PREFIX}/${V32_DOCDIR}")
    list(APPEND V32_READABLE_DIRS "${CMAKE_INSTALL_PREFIX}/${V32_DOCDIR}/.."
                                  "${CMAKE_INSTALL_PREFIX}/${V32_DOCDIR}/../.."
                                  "${CMAKE_INSTALL_PREFIX}/${V32_MANROOT}"
                                  "${CMAKE_INSTALL_PREFIX}/${V32_MANROOT}/man3")
endforeach()

if(V32_HAVE_DEVTOOLS)
    add_custom_target(demos ALL DEPENDS ${V32_ALL_CARTRIDGES})
endif()
if(V32_GZIP)
    add_custom_target(manpages ALL DEPENDS ${V32_ALL_GZ})
endif()

# Make everything installed readable by ordinary users, whatever umask the
# install ran under: sudo often has 077, and the directories CMake creates
# are masked by it. Only adds go+rX -- never removes a permission -- on the
# documents, the shared Vircon32 folders above them, the header folder, the
# prefix and the man3 folder. Linux / macOS only; DESTDIR-aware.
if(NOT TARGET_OS STREQUAL "windows")
    list(REMOVE_DUPLICATES V32_READABLE_DIRS)
    foreach(V32_C ${V32_ENABLED})
        install(CODE "
            foreach(T ${V32_READABLE_TREES})
                if(IS_DIRECTORY \"\$ENV{DESTDIR}\${T}\")
                    execute_process(COMMAND chmod -R go+rX \"\$ENV{DESTDIR}\${T}\")
                endif()
            endforeach()
            foreach(D ${V32_READABLE_DIRS})
                if(IS_DIRECTORY \"\$ENV{DESTDIR}\${D}\")
                    execute_process(COMMAND chmod go+rx \"\$ENV{DESTDIR}\${D}\")
                endif()
            endforeach()
        " COMPONENT ${V32_C})
    endforeach()
endif()

# `cmake --build . --target uninstall` removes whatever the last install put
# down (CMake has no built-in uninstall; this replays install_manifest.txt),
# then the directories that leaves empty: the ones of each component, then
# the ones they shared. share/man/man3 is the system's: never removed.
#
list(APPEND V32_PRUNE
     "${V32_HEADER_PATH}"
     "${V32_HEADER_PARENT}"
     "${CMAKE_INSTALL_PREFIX}/${V32_INSTALL_DOCROOT}"
     "${CMAKE_INSTALL_PREFIX}/${V32_TOOLS_ROOT}")
if(NOT TARGET_OS STREQUAL "windows")
    list(APPEND V32_PRUNE "${CMAKE_INSTALL_PREFIX}/Vircon32")
endif()
list(REMOVE_DUPLICATES V32_PRUNE)
set(V32_PRUNE_DIRS "")
foreach(V32_DIR ${V32_PRUNE})
    string(APPEND V32_PRUNE_DIRS " \"${V32_DIR}\"")
endforeach()
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        @ONLY)
    add_custom_target(uninstall
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        COMMENT "Uninstalling ${V32_ENABLED_TEXT}")
endif()

# ----------------------------------------------------------------------------
#   TESTS (ctest)
# ----------------------------------------------------------------------------

# The same checks as the base Makefile's `test`: the version stamps in
# VERSION, lib/v32io/v32io.h and the manual pages of both components agree;
# the manual pages are well formed (and with the Vircon32 compiler around,
# their examples compile); the demos of the chosen components compile. Needs
# sh and python3 for the first two. The end-to-end tests of tests/ are
# `make realcheck`.
#
option(BUILD_TESTING "Define the ctest tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_program(V32_SH sh)
    find_package(Python3 COMPONENTS Interpreter)
    if(V32_SH)
        add_test(NAME version
                 COMMAND "${V32_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-version.sh")
    endif()
    if(Python3_Interpreter_FOUND)
        add_test(NAME manpages
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint-man.py" all)
        if(V32_HAVE_DEVTOOLS)
            get_filename_component(V32_COMPILE_DIR "${V32_COMPILE}" DIRECTORY)
            add_test(NAME manpage-examples
                     COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint-man.py"
                             --compile "${V32_COMPILE_DIR}" all)
        endif()
    endif()
    if(V32_HAVE_DEVTOOLS)
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
# carry the components that were chosen when configuring (v32io, v32dev, or
# v32io-v32dev) and the full version string in their file name; the
# package-manager version is the numeric YYYY.MM.DD date. The libraries are
# headers, so the packages are not specific to a processor.
#
set(CPACK_PACKAGE_NAME "${V32_ENABLED_NAME}")
set(CPACK_PACKAGE_VENDOR "Matthew Haas")
set(CPACK_PACKAGE_CONTACT "Matthew Haas")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wedge1020/v32io")
set(CPACK_PACKAGE_FILE_NAME "${V32_ENABLED_NAME}-${V32_VERSION}-${TARGET_OS}")
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
    find_program(V32_DPKG_DEB dpkg-deb)
    if(V32_DPKG_DEB)
        list(APPEND CPACK_GENERATOR "DEB")
    endif()
    find_program(V32_RPMBUILD rpmbuild)
    if(V32_RPMBUILD)
        list(APPEND CPACK_GENERATOR "RPM")
    endif()
elseif(TARGET_OS STREQUAL "windows")
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
