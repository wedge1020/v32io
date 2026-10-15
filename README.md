# v32io

Vircon32 hardware  accessories and  in-environment Vircon32 C  drivers to
support them.

## v32io Vircon32 C libraries

This repository hosts the in-environment Vircon32 drivers (in the form of
Vircon32 C libraries):  header files you include into  your existing BIOS
or CART  that provides the various  functions to read /  decode / process
the gamepad button-encoded data from the outside world.

Currently provided are:

  * `v32io`: the core data access library (handles processing of buttons)
  * `v32kbd`: the library allowing full keyboard functionality
  * `v32mouse`: the library providing mouse functionality

The Vircon32 C libraries is located under `lib/`

## v32io hardware firmware

In  order  for the  Vircon32  in-environment  drivers to  function,  some
external conduit is required. This has taken the form of two approaches:

### Hardware peripheral

An emulator-independent  solution takes the  form of custom  Raspberry pi
pico 2 firmware loaded onto a  specific and modified board (The Waveshare
RP2350-USB-A with its R13 resistor removed).

* URL: https://www.waveshare.com/rp2350-usb-a.htm?srsltid=AU7gw4X3Jb9rUmb0MDpPOuzu_z4PlolE_jQZhU-r79UBAfWo9jPr1wsY

The firmware provides support for  standard, conforming USB keyboards and
mice, using the color LED to  provide status indicators of the device (no
device recognized, device connecting/connected, and configuration mode).

The configured  hardware peripheral  (ie a Vircon32  EditControls profile
with  inputs mapped  to  gamepad buttons),  when  connected will  present
itself to any Vircon32 emulator (stock or modified) as one of `v32io:kbd`
or `v32io:mouse`, which can be selected by any of the four gamepad ports.
The emulator does not know it is anything other than a USB gamepad.

## DesktopEmulator patches

In  lieu  of using  the  `v32io`  Vircon32  Hardware Accessory,  you  can
instead  use a  modified  version of  the  Vircon32 DesktopEmulator.  The
stock/official emulator does not currently include this functionality, so
you have two options regarding the emulator: patching the stock emulator,
or using a fork of the  Vircon32 DesktopEmulator with the patches already
applied.

### Modified Emulator

The  stock   Vircon32  emulator   does  not   include  support   for  any
`v32io`-based peripherals.

This fork of the [Vircon32 ComputerSoftware](https://github.com/wedge1020/ComputerSoftware/tree/v32io) repository, under its `v32io` branch, has implemented emulator-level implementations of these `v32io` peripherals, requiring no physical hardware.

The use  of the provided `v32kbd`  device (under the Gamepad  menu), will
emulate the `v32io`  functionality on your system  keyboard, allowing for
the  full typing  experience  within Vircon32.  This  option is  mutually
exclusive with the existing `keyboard` device.  Only one or the other can
be enabled at any one time (on any gameport).

The use of the provided `v32mouse`  device (under the Gamepad menu), will
emulate the  `v32io` functionality with  your system mouse,  allowing for
mouse  functionality  within  Vircon32.  Note that  when  you  focus  the
Vircon32 DesktopEmulator window, the mouse  pointer WILL be captured, and
you  will need  to issue  `CTRL`+`ALT` (the  left `ALT`  specifically, to
reduce potential conflicts  with keyboards using `AltGr`)  to release the
pointer back to the system.

### Patch the stock DesktopEmulator

On this  repository, under  the `emulator/`  directory, are  three `.patch`
files, to  be used for patching  in `v32kbd` and `v32mouse`  support into
the stock Vircon32 DesktopEmulator (`v32kbd.patch`, `v32mouse.patch` and
`v32kbd-text.patch`, applied in that order: see `emulator/README.md`).
The last one makes `v32kbd` also send the *typed characters* described
below; the other two work without it.

## Typed characters

Key codes identify keys, not what they type, so the accented letters, the
euro sign and the box drawing characters of the Vircon32 BIOS font could
not be typed. They can now: the device sends such a character as 2 events
(a codepage prefix, using the key codes 30 and 31 that were unused, then
the low 7 bits of the character), both as key releases so programs built
with the older library just see releases and never a key press. In a
program:

```
glyph = v32kbd_glyph (v32kbd_read (&keyboard));   // character to draw, or 0
```

* The patched **emulator** reports whatever the computer's keyboard layout
  types (dead keys, AltGr...) if the BIOS font has it.
* The **hardware adapter** has its own compose layer (AltGr, dead keys and
  Alt codes on a US keyboard): see `docs/COMPOSE.md`. Its prebuilt
  `firmware/RP2350-USB-A/v32io_pico.uf2` predates it and has to be rebuilt
  to get it.

Details and the alternatives that were considered: `docs/PROTOCOLS.md`.

## Building and installing

There is nothing to compile for the library itself: it is Vircon32 C
headers. The project has a `Makefile` and, as an alternative that gives the
same result, a `CMakeLists.txt`:

```
make                  # build the demo cartridges (lib/demos/bin)
make test             # version stamps, manual pages and their examples
make sysinstall       # headers, manual pages, documents (may need sudo)
make sysuninstall
make version          # show the version and stamp it (see below)
make archive          # v32io-project.zip
```

```
mkdir build && cd build
cmake ..              # -DCMAKE_INSTALL_PREFIX=<dir> to install elsewhere
cmake --build .
ctest
cmake --install .     # may need sudo
cmake --build . --target uninstall
cpack                 # .tar.gz / .deb / .rpm, or .zip on Windows
```

Both put (by default, with `/usr/local` as the prefix on Linux and macOS):

| What                   | Where                                           |
| ---------------------- | ----------------------------------------------- |
| `v32io.h`, `keyboard.h`, `mouse.h` | `Vircon32/DevTools/include/`        |
| manual pages (`3v32`)  | `share/man/man3/`                               |
| README, docs, demos    | `Vircon32/v32tools/docs/v32io/`                 |

The headers go in the **DevTools' `include/` folder** because that is where
the Vircon32 compiler (which has no `-I` option) looks for the headers a
program includes: afterwards a program says `#include "keyboard.h"` and
that is all. On Windows (MSYS2/MinGW) the layout is `DevTools/include`
under the Vircon32 folder, as the DevTools use. `PREFIX`, `DESTDIR`, and
for CMake `-DV32IO_INSTALL_INCLUDEDIR=<dir>`, change it. `lib/Makefile`
does the library part on its own (`make -C lib`, see `lib/README.md`).

`make test` and `ctest` check that the version stamps agree and that the
manual pages are well formed. With the Vircon32 DevTools around
(`DEVTOOLS=<folder with compile>` or on the `PATH`), they also compile the
examples in the manual pages and the demos. The end-to-end tests that run
the firmware logic and the library on the console of the emulator are
`make realcheck` (see `v32io-tests/README.md`).

## Manual pages

Each header has a manual page in section `3v32` (the one the Vircon32 C
library uses), and so does every function, structure and constant:

```
man keyboard.h        man v32kbd_read        man V32KEY_PAGE_HIGH
man mouse.h           man v32mouse_probe
man v32io.h           man v32io             # overview
```

They are in `man/man3`; the alias pages are written by
`tools/gen-manaliases.py` and checked by `tools/lint-man.py` (neither
mandoc nor groff are needed for that).

## Version

`VERSION` holds the version: the release date as `YYYYMMDD`, then `-dev`
or `-release` (now `20261014-dev`). `make version` stamps it, and today's
date, into `lib/v32io.h` (`V32IO_VERSION` and `V32IO_VERSION_NUMBER`, which
programs can read), the `File version` banner of each header and the
`.TH` line of every manual page. To release:

```
make version VERSION=20261101-release
```

(this also writes `VERSION`). CMake, `cpack` and the package names read the
same file.

## Layout

| Folder           | What is in it                                            |
| ---------------- | -------------------------------------------------------- |
| `lib/`           | the Vircon32 C library (headers, demos), `lib/Makefile`  |
| `man/man3/`      | manual pages                                             |
| `docs/`          | protocol reference, typing special characters            |
| `firmware/`      | RP2350-USB-A adapter firmware                            |
| `emulator/`      | DesktopEmulator patches                                  |
| `v32io-tests/`   | end-to-end tests                                         |
| `tools/`         | character table generator, manual page tools, checks     |
| `cmake/`         | the CMake uninstall script                               |

# v32dev

Vircon32 programs talking to the outside world through the **memory card**:
reading and writing files of the host computer (`storage.h`), a terminal
(`serial.h`), and later a network (`network.h`, `netplay.h`). The devices are
modeled on unix devices: each of the 4 channels of the mailbox is a *block*,
*char*, *pipe* or *socket* device, announced by the host.

v32dev is the sibling of v32io: v32io carries keyboard and mouse on a gamepad
port (it works on real hardware); v32dev needs the emulator patch, and gives
fast two-way data in and out of the emulator.

```
v32dev/
  emulator/v32dev.patch     DesktopEmulator patch (patch -p0, from ComputerSoftware/)
  lib/v32dev.h              core: channels, types, streams, messages (like v32io.h)
  lib/storage.h             block device: sectors of a host file
  lib/serial.h              char device: a terminal-like stream
  lib/demos/storage.c       browse (and mark) a host file on screen
  tools/v32dev.py           host side: format a card, serve devices
  docs/v32dev-protocol.md   the full description
  tests/run.sh              all the tests (see its header)
```

## Quick start

1. Apply the patch to the emulator source and build it:
   `cd ComputerSoftware && patch -p0 < v32dev.patch` (try `--dry-run` first).
   It touches only `DesktopEmulator/ConsoleLogic/V32Console.{cpp,hpp}` and
   `V32MemoryCardController.{cpp,hpp}`.
2. Format a card and serve a file on it (a text file, in byte mode, one character
   per word):

   ```
   tools/v32dev.py format game.memc --ch0 storage --ch1 serial
   tools/v32dev.py serve  game.memc --ch0 storage:notes.txt,bytes --ch1 serial:stdio
   ```
3. Run a program that uses it with `game.memc` as its memory card. Format before the
   emulator loads the card (never format a card the emulator has loaded). The host can
   be started before or after the program, and restarted while it runs.

In a program:

```c
#include "storage.h"

void main (void)
{
    int [128] words;
    storage  *disk = storage_init (0);            // channel 0

    if (storage_wait_ready (&disk, 600))          // waits for the host
    {
        storage_read_words (&disk, 0, words, 128);     // first 128 words of the file
        storage_write_words (&disk, 0, words, 128);    // and write them back
        storage_flush (&disk);
    }
    storage_free (&disk);
}
```

`storage_probe()` (or the other blocking calls, which do it) must run once per frame, like
`v32io_probe()`. Every call that waits ends frames meanwhile; the non-blocking
`storage_read_begin()` family lets the program keep drawing.

## What is where in a program

| You want | Use |
|---|---|
| to know if the mailbox works | `v32dev_state()`: `NOCARD`, `NOEMU`, `NOHOST`, `WRONG`, `READY` |
| a file or disk image | `storage.h` |
| a terminal | `serial.h` |
| a new kind of device | `v32dev.h` (`v32dev_init`, `v32dev_probe`, `v32dev_send`/`v32dev_recv`, or `v32dev_read`/`v32dev_write`) plus a device class on the host (`Device` in `v32dev.py`) |

See `docs/v32dev-protocol.md` for the layout, the device classes, and the design of
the network and netplay devices.
