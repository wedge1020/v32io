# The Vircon32 C libraries

Two independent libraries of headers that you include into your CART or
BIOS. There is nothing to link: the Vircon32 compiler reads the headers and
emits only the functions a program calls. They are installed together in the
Vircon32 DevTools' `include/` folder (flat, no subfolders), so a program
needs only `#include "keyboard.h"` or `#include "storage.h"`. See the
README at the top of the project for installing one or both.

| Folder | Library | Headers | What it gives a program | Needs |
| --- | --- | --- | --- | --- |
| [`v32io/`](v32io/README.md) | v32io | `v32io.h`, `keyboard.h`, `mouse.h` | a keyboard and a mouse, read through gamepad ports | the RP2350-USB-A adapter, or an emulator with the v32io patches |
| [`v32dev/`](v32dev/README.md) | v32dev | `v32dev.h`, `storage.h`, `serial.h` | files of the host, a terminal (later a network), through the memory card | an emulator with the v32dev patch, and `tools/v32dev.py` |

Each folder has its own README (the full description of that library), its
own `Makefile` (`make`, `make check`, `make install`, `make version`), and its
demo programs under `demos/`. In each, the core header (`v32io.h`,
`v32dev.h`) is included by the drivers on top of it, which are the ones a
program includes.

| | v32io | v32dev |
| --- | --- | --- |
| core | `v32io.h`: reads a gamepad's 11 controls and packs them | `v32dev.h`: channels of the memory card mailbox, streams and messages |
| drivers | `keyboard.h` (`v32kbd`), `mouse.h` (`v32mouse`) | `storage.h` (block device), `serial.h` (char device); later `network.h`, `netplay.h` |
| call the probe | once per frame | once per frame |
| demos | `v32kbd`, `v32mouse` (expect the device in Gamepad 4) | `storage` (browse a host file; the card's channel 0) |
| manual | `man v32io` | `man v32dev` |

The two can be used in the same program: they share no state and no names.
The version is shared too (`../VERSION`).
