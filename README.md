# v32io and v32dev

Ways for a Vircon32 program to reach beyond the console: hardware accessories
and emulator features, and the in-environment Vircon32 C drivers (headers you
include into your CART or BIOS) that make use of them.

The project has two components. They are independent, share the same
version and the same install layout, and can be installed together or one
at a time.

| | **v32io** | **v32dev** |
| --- | --- | --- |
| What it gives a program | a keyboard and a mouse | files of the host, a terminal, later a network |
| Carried by | the 11 controls of a gamepad port | the memory card (a mailbox in its last 64 KiB) |
| Needs | an RP2350-USB-A / -USB-C / -USB-CM adapter *or* an emulator with the v32io patches | an emulator with the v32dev patch, and the host tool |
| Works on real hardware | yes (with the adapter) | no (it is an emulator feature) |
| C drivers | `v32io.h`, `keyboard.h`, `mouse.h` | `v32dev.h`, `storage.h`, `serial.h` |
| Manual | `man v32io` | `man v32dev` |
| Start reading | [`lib/v32io/README.md`](lib/v32io/README.md) | [`lib/v32dev/README.md`](lib/v32dev/README.md) |

## v32io: keyboard and mouse through gamepad ports

A v32io device is seen by the console as an ordinary gamepad, but its 11
controls carry data (a key event, a mouse movement) instead of game presses,
and a driver in the program decodes it. The libraries are `v32io` (the core,
which reads and packs the buttons), `v32kbd` (the full keyboard, including
typed characters outside ASCII) and `v32mouse` (a pointer with three buttons).

The conduit outside the console is one of:

* **The hardware adapter**: firmware for a Waveshare RP2350 board (a Pi Pico 2
  class board) that presents a USB keyboard or mouse to the computer as a USB
  gamepad. It works with any Vircon32 emulator, stock or modified, which does
  not know it is anything but a gamepad. Three boards are supported, all with
  the same gamepad and the same behavior:

  | Board | Computer end | Keyboard / mouse end | Read first |
  | --- | --- | --- | --- |
  | RP2350-USB-A | type C receptacle (native) | type A (PIO host; R13 removed) | [`firmware/RP2350-USB-A/README.md`](firmware/RP2350-USB-A/README.md) |
  | RP2350-USB-C | type C receptacle (native) | type C receptacle (PIO host; R13 removed) | [`firmware/RP2350-USB-C/README.md`](firmware/RP2350-USB-C/README.md) |
  | RP2350-USB-CM | type C **plug** (PIO device) | type C receptacle (native host) | [`firmware/RP2350-USB-CM/README.md`](firmware/RP2350-USB-CM/README.md) |

  On the -CM the ports have the opposite roles to the other two.
* **The emulator patch** (`v32io.patch`): a modified DesktopEmulator that has `v32kbd` and
  `v32mouse` in its Gamepads menu and uses the keyboard and mouse of the
  computer, with no hardware. See
  [`emulator/v32io/README.md`](emulator/v32io/README.md).

How the devices work, the key codes, the movement encoding and the API are in
[`lib/v32io/README.md`](lib/v32io/README.md); the protocols in
[`docs/v32io/PROTOCOLS.md`](docs/v32io/PROTOCOLS.md); typing special
characters on the adapter in [`docs/v32io/COMPOSE.md`](docs/v32io/COMPOSE.md).

## v32dev: files, a terminal and more through the memory card

v32dev lets a program exchange data with the computer that runs the emulator,
through its **memory card**. A patched DesktopEmulator keeps the last 64 KiB
of the card file synchronized while the program runs, and a *host* program
(`tools/v32dev.py`) reads and writes that file. The program only reads and
writes card memory: no new I/O ports, no new hardware.

The mailbox has 4 channels, and each is a device in the manner of unix
devices, announced by the host: a *block* device (`storage.h`: sectors of a
file), a *char* device (`serial.h`: a terminal), and later *pipe* and *socket*
devices (`network.h`, `netplay.h`). The C side is a core, `v32dev.h`, that the
drivers include, as `keyboard.h` and `mouse.h` include `v32io.h`.

The quick start, the API at a glance and the host tool are in
[`lib/v32dev/README.md`](lib/v32dev/README.md); the emulator patch in
[`emulator/v32dev/README.md`](emulator/v32dev/README.md); the full description
of the layout and the protocol in
[`docs/v32dev/PROTOCOL.md`](docs/v32dev/PROTOCOL.md).

## Layout

Everything that belongs to one component is under a folder of its name, so
that a part can be found, installed or left out on its own.

| Folder | What is in it |
| --- | --- |
| `lib/v32io/`, `lib/v32dev/` | the Vircon32 C libraries (headers, demos, a `Makefile` and a README each); `lib/README.md` is the index |
| `man/v32io/man3/`, `man/v32dev/man3/` | the manual pages of each component, section `3v32` |
| `docs/v32io/`, `docs/v32dev/` | protocol references (v32io: protocols, typing special characters; v32dev: the protocol) |
| `emulator/v32io/`, `emulator/v32dev/` | the DesktopEmulator patches of each component |
| `tests/v32io/`, `tests/v32dev/` | the end-to-end tests of each component; see `tests/README.md` |
| `firmware/` | RP2350-USB-A, -USB-C and -USB-CM adapter firmware (v32io only) |
| `tools/` | `v32dev.py`, the v32dev host tool; the character table generator (v32io); `check-firmware-sync.sh` (v32io); the manual page and version tools, which serve both |
| `cmake/` | the CMake uninstall script |

## Building and installing

There is nothing to compile for the libraries themselves: they are Vircon32 C
headers. The project has a `Makefile` and, as an alternative that gives the
same result, a `CMakeLists.txt`. Both can install **v32io only (the default),
v32dev only, or both**; a component's manual pages, documents and demos are
installed only when its headers are.

```
make                              # build the demo cartridges of both components
make test                         # version stamps, manual pages and their examples
make sysinstall                   # v32io only (may need sudo)
make sysinstall COMPONENTS=v32dev # v32dev only
make sysinstall COMPONENTS=both   # both
make sysuninstall                 # remove what COMPONENTS names (same choices)
make version                      # show the version and stamp it (see below)
make archive                      # v32io-project.zip
```

`sysinstall-v32io`, `sysinstall-v32dev` and `sysinstall-both` (and the same with
`sysuninstall`) are the same thing as targets.

```
mkdir build && cd build
cmake ..                          # v32io only (the default)
cmake -DV32_COMPONENTS=v32dev ..  # v32dev only
cmake -DV32_COMPONENTS=both ..    # both
cmake --build .
ctest
cmake --install .                 # may need sudo
cmake --build . --target uninstall
cpack                             # .tar.gz / .deb / .rpm, or .zip on Windows
```

(`-DCMAKE_INSTALL_PREFIX=<dir>` installs elsewhere. The choice is made when
configuring, and the uninstall target removes what that install put down.)

Both put (by default, with `/usr/local` as the prefix on Linux and macOS):

| What | Where | Installed with |
| --- | --- | --- |
| `v32io.h`, `keyboard.h`, `mouse.h` | `Vircon32/DevTools/include/` | v32io |
| `v32dev.h`, `storage.h`, `serial.h` | `Vircon32/DevTools/include/` | v32dev |
| v32io manual pages (`3v32`) | `share/man/man3/` | v32io |
| v32dev manual pages (`3v32`) | `share/man/man3/` | v32dev |
| README, protocol documents, demos | `Vircon32/v32tools/docs/v32io/` | v32io |
| README, protocol document, demo | `Vircon32/v32tools/docs/v32dev/` | v32dev |

The headers of both components go **directly in the DevTools' `include/`
folder, with no subfolders**, because that is where the Vircon32 compiler (which
has no `-I` option) looks for the headers a program includes: afterwards a
program says `#include "keyboard.h"` or `#include "storage.h"` and that is all.
On Windows (MSYS2/MinGW) the layout is `DevTools/include` under the Vircon32
folder, as the DevTools use.

Note on the name: the folder that holds the headers is `DevTools` once the
Vircon32 tools are installed (`DevelopmentTools` is only the name of its source
folder in the ComputerSoftware repository). If your installation names it
differently, `DEVTOOLS_DIRNAME=<name>` (make) or `-DV32_DEVTOOLS_DIRNAME=<name>`
(CMake) changes it, and `INCDIR=<dir>` / `-DV32_INSTALL_INCLUDEDIR=<dir>` sets
the header folder directly. `PREFIX` and `DESTDIR` work as usual.
`lib/v32io/Makefile` and `lib/v32dev/Makefile` do the library part of each
component on their own (`make -C lib/v32dev`, see the READMEs there).

`make test` and `ctest` check that the version stamps agree and that the
manual pages of both components are well formed. With the Vircon32 DevTools
around (`DEVTOOLS=<folder with compile>` or on the `PATH`), they also compile
the examples in the manual pages and the demos. The end-to-end tests that run
the libraries on the console of the emulator are `make realcheck` (see
`tests/README.md`).

## Manual pages

Each header has a manual page in section `3v32` (the one the Vircon32 C
library uses), and so does every function, structure and constant. The pages
are in two sets, one for each component, and a set is installed only with its
component:

```
man v32io             # overview of v32io      man v32dev            # overview of v32dev
man keyboard.h        man mouse.h            man storage.h         man serial.h
man v32kbd_read       man V32KEY_PAGE_HIGH   man storage_read      man V32DEV_READY
```

They are in `man/v32io/man3` and `man/v32dev/man3`; the alias pages are
written by `tools/gen-manaliases.py` and checked by `tools/lint-man.py`
(neither mandoc nor groff are needed for that).

## Version

`VERSION` holds the version, for both components: the release date as
`YYYYMMDD`, then `-dev` or `-release` (now `20261014-dev`). `make version`
stamps it, and today's date, into `lib/v32io/v32io.h` (`V32IO_VERSION` and
`V32IO_VERSION_NUMBER`, which programs can read), the `File version` banner of
each header of both libraries, and the `.TH` line of every manual page (which
says `v32io <version>` or `v32dev <version>`). To release:

```
make version VERSION=20261101-release
```

(this also writes `VERSION`). CMake, `cpack` and the package names read the
same file. (`V32DEV_VERSION` in `v32dev.h` is the version of the mailbox
*protocol*, not of the release.)
