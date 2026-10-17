# v32dev: emulator patch for the memory card mailbox

This is the emulator side of **v32dev**  (data in and out of the emulator
through the memory card). The emulator side of **v32io** is separate: see
`../v32io/README.md`. v32dev  needs this  patch; v32io  does not  need it
when the RP2350-USB-A adapter is used.


`v32dev.patch`   (in   this   directory)   is   made   against   Vircon32
`ComputerSoftware  (upstream master,  commit  49a90b3`).  Apply from  the
``ComputerSoftware/` folder:

```
patch -p0 --dry-run < v32dev.patch      # check first
patch -p0 < v32dev.patch
patch -R -p0 < v32dev.patch             # to take it out again
```

It     changes    only     `DesktopEmulator/ConsoleLogic/V32Console.cpp`,
`V32Console.hpp`,            `V32MemoryCardController.cpp`            and
`V32MemoryCardController.hpp`:  no  other  source file,  nothing  in  the
GUI  or  the  settings,  so  it   should  coexist  with  the  v32io
patch  (which touches `GamepadsInput.*`,  `Main.cpp`, `GUI.cpp`,
`Settings.cpp`).

What it does (details in `docs/v32dev/PROTOCOL.md`, section 2):

- If the loaded card file has a valid v32dev header, the last 64 KiB of the card are a
  mailbox. Every frame, before the program runs, the host-owned part is read again from the
  file; after it runs, what the program wrote is saved, in an order that is safe for an
  external reader.
- Every card (mailbox or not) is now saved by modified 256-word pages instead of
  rewriting the whole file, which is also cheaper for ordinary saves.
- `V32DEV_DISABLE=1` in the environment turns the mailbox off.
- A card without the header behaves exactly as before.

`mkpatch.sh <pristine tree> <patched tree>` regenerates the patch.
