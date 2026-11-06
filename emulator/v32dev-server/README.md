# v32dev-server: a v32dev host built into the DesktopEmulator

With **v32dev** a program uses files and a terminal of the computer through its
memory card, and a *host* serves the other end. Until now the host was a separate
program, `tools/v32dev.py`, that talked to the emulator through the card
**file**. This patch builds a host into the emulator: the same devices, the same
protocol, chosen from the **Memory card** menu, with no second program, no
formatting step and no polling of a file.

`v32dev-server.patch` goes **on top of** `../v32dev/v32dev.patch` (it uses the
mailbox that one adds to the console). The two hosts do not exclude each other:
without the server enabled the emulator behaves exactly as with `v32dev.patch`
alone, and `tools/v32dev.py` keeps working.

## apply the patch

The patch is made against the stock ComputerSoftware with `v32dev.patch`
already applied (upstream `main`, commit `49a90b3`, as for that patch). From the
`ComputerSoftware/` folder:

```
patch -p0 < v32dev.patch                  # first, if not applied yet
patch -p0 --dry-run < v32dev-server.patch # check
patch -p0 < v32dev-server.patch
patch -R -p0 < v32dev-server.patch        # to take it out again (v32dev.patch stays)
```

It creates `DesktopEmulator/ConsoleLogic/V32DevServer.{hpp,cpp}` and changes
`ConsoleLogic/CMakeLists.txt`, `V32Console.{cpp,hpp}`,
`V32MemoryCardController.{cpp,hpp}`, `Emulator/GUI.cpp` and
`Emulator/Settings.cpp`. It also applies together with `../v32io/v32io.patch`,
in either order (that patch also touches `GUI.cpp` and `Settings.cpp`; `patch`
reports offsets and a little fuzz, and no rejects).

`mkpatch.sh <tree with v32dev.patch> <same tree with the server too>`
regenerates the patch.

## using it

Open **Memory card → v32dev server**:

| Entry | What it does |
| --- | --- |
| **Enabled** | turns the server on or off. It serves the card that is loaded, and the next ones (also when card handling is *automatic*: the entry is in that menu too). With no card it waits for one |
| status line | `off`, `waiting for a memory card`, `serving the memory card`, or why the card cannot be served |
| **Channel N: …** | one submenu for each of the 4 channels; its title says what the channel serves |

In a channel's submenu:

| Entry | What the channel serves |
| --- | --- |
| **Off** | nothing |
| **Storage: open file…** | a file of the computer, as a block device (`storage.h`) |
| **Storage: new image…** | the same, creating an 8 MiB file (16384 sectors) if it is not there |
| **Byte mode** / **Read only** | switches for the *next* storage file you pick: byte mode serves ordinary files as they are (one byte per word: text files, FAT images); read only makes writes fail |
| **Serial: echo** | a character device that sends back what the program writes (to try things) |
| **Serial: console (stdin/stdout)** | the terminal the emulator was started from. One channel at a time. Not in Windows builds, which have no console |
| **Serial: TCP 127.0.0.1:320N** | a character device that is a TCP port of *this computer only* (`3200 + channel`): `nc 127.0.0.1 3201`, `telnet`, a script… |

The channel assignment is yours (the library does not fix it). Two disks for a
DOS-like program, say, are two `Storage` channels, which is what
`v32dev.py serve game.memc --ch0 storage:disk.img --ch1 storage:disk2.img`
did.

The menu also shows, for a channel that is served, what it serves and how many
words went each way, and, if a channel cannot be served (a file that cannot be
opened, a port that is taken), why. The same message appears in a box when you
pick the file.

### Without the menu: environment and settings

```
V32DEV_SERVER=1 V32DEV_CH0=storage:disk.img V32DEV_CH1=serial:tcp=4000 Vircon32
```

`V32DEV_SERVER` (anything but `0`) turns the server on and puts the environment
in charge: `V32DEV_CH0` to `V32DEV_CH3` give the channels (menu changes still
apply, for that session). The environment is not written to the settings file,
and the settings file's own choice is kept as it was. Without
`V32DEV_SERVER`, the menu's choices are saved in `Config-Settings.xml`, so they
are there next time:

```xml
<v32dev-server enabled="yes" channel0="storage:/home/me/disk.img,bytes" channel1="serial:tcp=3201"/>
```

The text of a channel is the same as `v32dev.py serve`'s `--chN`
(`storage:FILE[,bytes][,ro][,sector=N][,create=SECTORS]`, `serial:echo[,hup=N]`,
`serial:stdio`) plus `serial:tcp=PORT`. The options are read from the right, so
a file name can have commas. `V32DEV_DISABLE=1` (see `../v32dev/README.md`) turns
the server off as well.

## what happens to the card

The server serves the **memory** of the loaded card, from inside the frame loop:
before each frame it answers what the program wrote in the last one. Because
nothing goes through the file:

- **The card does not need to be formatted.** If the last 64 KiB of the card
  hold nothing (they are all zero, as in a card the emulator creates), the server
  puts the mailbox header there *in memory only*; the program sees a live mailbox,
  the file never has it. Turning the server off, or changing the card, takes
  it away again. A card that holds data in those 64 KiB (a program keeping its
  own save there) is left alone, and the status line says why it is not served.
- A card that *was* formatted by `v32dev.py format` also works: the server takes
  the place of the host program (do not run `v32dev.py serve` on the same card at
  the same time; whatever it writes is ignored while the server is on).
- **Nothing of the mailbox is written to the card file** while the server is on:
  not the rings, not the control words. The program's other card data is saved as
  always. So a card used with the server stays clean, and a program that crashes
  leaves no stale mailbox state behind.
- The protocol is the one in `../../docs/v32dev/PROTOCOL.md`, so programs cannot
  tell the two hosts apart. At the speed of a real console (60 frames per second)
  they are equally fast: the test cartridge that reads and writes an image takes
  20 frames with either. The server's answer is always ready at the next frame,
  though, while `v32dev.py` polls every few milliseconds: when the emulator runs
  faster than that (fast-forward, headless tests at 2 ms a frame) the same cartridge
  takes about 40 frames with `v32dev.py` and still 20 with the server.

### What a program can reach

Only what you put on a channel. A storage channel is one file you picked; a TCP
channel listens on `127.0.0.1` only and takes one connection at a time (a new
one replaces the old, and the program sees a hang up between them). There is no
way for the program to ask for anything else, and a channel with nothing on it
serves nothing. This is the "host is the policy point" rule of the protocol
document, with the menu as the policy. Output of a TCP channel while nobody is
connected is dropped, like a serial line with nothing plugged in: have the
program show a prompt until someone answers.

## how it is built

`V32DevServer.cpp` is the whole server and has no dependency but the C++11
standard library and the system's sockets:

- `ServerChannel`: the host side of a channel (rings, session, heartbeat, messages)
  on card memory; a port of `Channel` in `tools/v32dev.py`. It never trusts the
  indices the program wrote.
- `StorageDevice` and `SerialDevice` (with echo, console and TCP endpoints): ports
  of the Python devices, plus the checks a C++ host needs (sector numbers that
  could wrap around, bounded queues).
- `Server`: the 4 channels, their configuration and devices; `V32Console` calls it
  once per frame (`SyncMemoryCardMailbox`) and attaches it when a card is loaded.
- `V32MemoryCardController::PlanSave` leaves the whole mailbox region out of
  the saves while the server is on (there is no outside reader to order writes for).

Everything runs in the emulator's thread, between frames. There are no locks and
no threads.

## tested

`tests/v32dev/run.sh <ComputerSoftware with both patches>` adds to the v32dev
tests (see `../../tests/README.md`):

- 174 unit checks of the server on the real console classes (activation rules,
  a mailbox that exists only in memory, what is saved, every storage request and
  error, a program that writes garbage indices, serial echo, a real TCP client,
  configuration);
- the real-cartridge scenarios of v32dev (compiled with the real Vircon32 compiler)
  again, against the server, on cards that were **never formatted**, plus a
  console program over `serial:stdio` and over `serial:tcp`;
- the menu: the code of the patched `GUI.cpp` is cut out and run on the real
  Dear ImGui (with its assertions on), clicking through the menu.

## not covered, limits

- **The running emulator window has not been exercised**: the test machines had no
  SDL2/OpenGL, so `GUI.cpp` and `Settings.cpp` were compiled (with a stand-in for
  SDL's headers) and the menu run under Dear ImGui as above, but nobody has clicked
  it in the real window or loaded `Config-Settings.xml` through the real loader.
- **Windows is untested**: the sockets code has a Winsock path (`ws2_32` is linked),
  and the console device is turned off there; none of it has been built.
- The menu texts are English only (as the v32io menu entries are), and the TCP
  port of a menu-made channel is `3200 + channel` (a different one: settings file
  or environment).
- There is no terminal window inside the emulator; `serial:tcp` is how a GUI-only
  build gets a terminal.
- The server takes the whole mailbox: as with the external host, a program that
  keeps its own data in the last 64 KiB of the card conflicts with it.
- Savestates do not include the card (see the protocol document), with the server
  as with any host.
