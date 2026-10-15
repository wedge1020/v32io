# v32dev

Vircon32 C drivers/libraries for programs that talk to the outside world
through the **memory card**: reading and writing files of the host computer
(`storage.h`), a terminal (`serial.h`), and later a network (`network.h`,
`netplay.h`). The devices are modeled on unix devices: each of the 4 channels
of the mailbox is a *block*, *char*, *pipe* or *socket* device, announced by
the host.

This is the **v32dev** half of the project. Its sibling, **v32io** (a keyboard
and a mouse through gamepad ports), is in `../v32io/`: see `../README.md` for
the two libraries side by side, and the README at the top of the project for
how they are installed. v32io works on real hardware; v32dev is an emulator
feature that needs the patch in `emulator/v32dev/`, and gives fast two-way
data in and out of the emulator.

## DRIVERS

| File | What it is |
| --- | --- |
| `v32dev.h` | core: channels, device types, streams, messages |
| `storage.h` | block device: sectors of a host file |
| `serial.h` | char device: a terminal-like stream |

The drivers include `v32dev.h` to handle the channels, in the same way
`keyboard.h` and `mouse.h` include `v32io.h`. A program includes only the
driver(s) it needs. Each driver is used through its own instance
(`storage_init (channel)`, `serial_init (channel)`) on a channel of the card,
and several can be used at the same time, one per channel.

## HOW IT WORKS

The last 64 KiB of the memory card are a mailbox of 4 **channels**; each is a
pair of rings (host to program, program to host) plus control words. A patched
emulator keeps the mailbox synchronized with the card file on disk, and a host
program (`tools/v32dev.py`) serves a device on each channel. The program only
reads and writes card memory.

| Layer | Where | What it does |
| --- | --- | --- |
| Transport | `emulator/v32dev/v32dev.patch` | keeps the mailbox in sync with the file, in an order that is safe to read |
| Device core | `v32dev.h`, `tools/v32dev.py` | channels, device types (class and kind), opening, liveness, streams and messages |
| Device drivers | `storage.h`, `serial.h` (later `network.h`, `netplay.h`) | what a class of device means (sectors, characters, connections) |

A request and its reply take at least one frame (the host only sees what the
program wrote at the end of a frame, and the program only sees what the host
wrote at the start of one). **Every driver has to be probed once on every
frame**; the calls that wait (`storage_wait_ready`, `storage_read`...) end
frames themselves and probe as they go. The full description of the layout, the
device classes, the rings and the restart handshakes is in
`docs/v32dev/PROTOCOL.md`.

### Device classes

| Class | Value | Like | Driver |
| --- | --- | --- | --- |
| block | 1 | a disk: fixed-size sectors, random access | `storage.h` |
| char | 2 | a terminal or serial port: a stream of words | `serial.h` |
| pipe | 3 | a one-way stream with end of file | reserved |
| socket | 4 | connections and datagrams | planned: `network.h`, `netplay.h` |
| custom | 128-255 | anything | your own, on `v32dev.h` |

A program says which class it expects (`v32dev_init (channel, class)`, which
`storage_init` and `serial_init` do for it), and the channel is reported as
`V32DEV_WRONG` if the host serves another one. Which device is on which channel
is decided by whoever formats the card and starts the host.

### Channel states

| State | Meaning |
| --- | --- |
| `V32DEV_NOCARD` | no memory card is connected |
| `V32DEV_NOEMU` | the card has no live mailbox: not formatted, `V32DEV_DISABLE` set, or an emulator without the patch |
| `V32DEV_NOHOST` | the mailbox is live but nobody serves this channel |
| `V32DEV_WRONG` | a host serves a device of another class |
| `V32DEV_READY` | the channel can be used |

`v32dev_state()` gives the state, and `v32dev_events()` what happened in the
latest probe (`V32DEV_EV_OPEN`, `V32DEV_EV_LOST`, `V32DEV_EV_RESET` when the host
restarted).

## QUICK START

1. Apply the patch to the emulator source and build it:
   `cd ComputerSoftware && patch -p0 < v32dev.patch` (try `--dry-run` first).
   It touches only `DesktopEmulator/ConsoleLogic/V32Console.{cpp,hpp}` and
   `V32MemoryCardController.{cpp,hpp}`. See `emulator/v32dev/README.md`.
2. Format a card and serve a file on it (a text file, in byte mode, one
   character per word):

   ```
   tools/v32dev.py format game.memc --ch0 storage --ch1 serial
   tools/v32dev.py serve  game.memc --ch0 storage:notes.txt,bytes --ch1 serial:stdio
   ```
3. Run a program that uses it with `game.memc` as its memory card. Format
   before the emulator loads the card (never format a card the emulator has
   loaded). The host can be started before or after the program, and restarted
   while it runs.

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

`storage_probe()` (or the other blocking calls, which do it) must run once per
frame, like `v32io_probe()`. Every call that waits ends frames meanwhile; the
non-blocking `storage_read_begin()` family lets the program keep drawing.

## WHAT IS WHERE IN A PROGRAM

| You want | Use |
| --- | --- |
| to know if the mailbox works | `v32dev_state()`: `NOCARD`, `NOEMU`, `NOHOST`, `WRONG`, `READY` |
| a file or disk image | `storage.h` |
| a terminal | `serial.h` |
| a new kind of device | `v32dev.h` (`v32dev_init`, `v32dev_probe`, `v32dev_send`/`v32dev_recv`, or `v32dev_read`/`v32dev_write`) plus a device class on the host (`Device` in `tools/v32dev.py`) |

The API of each header is documented in the manual pages: `man v32dev`,
`man v32dev.h`, `man storage.h`, `man serial.h`, and one page for every
function (`man storage_read`) and constant (`man V32DEV_READY`). They are in
`man/v32dev/man3` and are installed with the library.

## THE HOST TOOL

`tools/v32dev.py` (Python 3, no dependencies; it only reads and writes the card
file, so it also works on Windows):

```
v32dev.py format game.memc --ch0 storage --ch1 serial     # declare types (creates the card if needed)
v32dev.py serve  game.memc --ch0 storage:disk.img --ch1 serial:stdio
v32dev.py info   game.memc
```

Devices for `serve`: `storage:FILE[,bytes][,ro][,sector=N][,create=SECTORS]`,
`serial:stdio`, `serial:echo`. It can also be used as a module (`Host`,
`Channel`, `StorageDevice`, `SerialDevice`) to write other devices.

## VERSION, BUILDING AND INSTALLING

The version of the library is the one of the whole project (`../../VERSION`).
`make version` at the top of the project stamps it into the `File version`
banner of each header and the manual pages. (`V32DEV_VERSION` in `v32dev.h` is
the version of the mailbox **protocol**, not of the release; the library
checks it.)

`lib/v32dev/Makefile` (this folder) handles the library on its own (the
Makefile at the top of the project calls it, and adds the manual pages and
documents, and chooses whether v32io, v32dev or both are installed):

| Command | What it does |
| --- | --- |
| `make` | builds the demo cartridge into `demos/bin` (needs the DevTools) |
| `make check` | only compiles the headers and demos |
| `make version` | stamps the date into the headers |
| `make install` | copies the headers into the DevTools' `include/` folder |
| `make uninstall` | removes them |

The compiler tools (`compile`, `assemble`, `packrom`) are taken from
`DEVTOOLS=<folder>` if given, then from the `PATH`. The headers are installed
in the folder where the Vircon32 DevTools keep theirs
(`/usr/local/Vircon32/DevTools/include` on Linux and macOS,
`<prefix>/DevTools/include` on Windows; `PREFIX`, `INCDIR` and `DESTDIR` work
as usual), next to the ones of v32io: no subfolder. The Vircon32 compiler has
no `-I` option and finds the headers a program includes in that folder, so once
they are installed a program only needs

```
#include "storage.h"
```

The demo in this folder includes `"../storage.h"` instead, since it sits next
to the headers in the source tree.

## DEMOS

| File | What it is |
| --- | --- |
| `storage.c` | browse (and mark) a host file on screen |

It expects the storage device in channel 0 of the card, and a host serving a
file there (a text file works best in byte mode, as its source says). The
cartridge `demos/bin/storage.v32` is part of the source tree, so it can be used
without the DevTools.

## TESTS

`tests/v32dev/run.sh <ComputerSoftware with the patch applied>` builds the
patched console code and runs the unit tests of the patch, a write-order test,
an end-to-end transport test with the host in a separate process, and real
cartridges compiled against these headers (see `tests/README.md` and the top of
`tests/v32dev/run.sh`).
