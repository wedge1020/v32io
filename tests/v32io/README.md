# v32io tests

End-to-end tests for the v32io drivers and the v32io-pico firmware,
run on a computer (no hardware or emulator window needed):

```
firmware modes        simulated USB reports       Vircon32 console logic
(kbd.c / mouse.c,  -> (1 ms polling, frame    ->  (DesktopEmulator sources)
 built for the PC)     jitter, split reports)     running test cartridges
                                                  built with keyboard.h / mouse.h
```

Each test checks what the program decodes against what the firmware sent:

- **keyboard**: every key event (code and press/release) arrives, in
  order, including bursts faster than the protocol (queued).
- **mouse**: the program's total movement equals the firmware's, every
  click is seen, every report changes at most one control per counter,
  and no invalid counter state is ever read. Run with steady frames,
  frame jitter, and reports split across frames.
- `setup_test`: mouse setup mode (enter/leave with the 3 buttons, clicks
  stepping through the gamepad buttons) and minimum click length.

The "2% late frames (known)" case shows the documented limit: when a
whole frame is skipped at full speed, the default 5 ms step can alias.
Build the firmware mouse with `-DMOUSE_STEP_MS=7` and it passes.

- **typed characters** (extended characters): scripted and random
  typing of accented letters and box drawing characters, read by the
  test cartridges in all 3 ways the library can be used
  (`v32kbd_getkey`, `v32kbd_read` + `v32kbd_glyph`, `v32kbd_readevent`),
  with frame jitter, reports split across frames, bursts and a full queue;
  and the library from before the extension (`compat/`) reading the same
  stream, which must never see a key press.
- `compose_test`: the adapter's compose layer (AltGr, dead keys, Alt
  codes) against the tables in `docs/v32io/COMPOSE.md`, 19,000 checks, no
  console needed.
- `emutest` (optional, `EMU=`): the patched DesktopEmulator's own `v32kbd`
  code (`GamepadsInput.cpp`, with `v32io.patch` applied), driven with
  synthetic SDL key and text events, against the same console logic and
  test cartridge: layout characters, dead keys, key repeat, a key press
  waiting for its text, queue overflow.

Run with:

```
VIRCON=/path/to/vircon32/ComputerSoftware \
DEVTOOLS=/path/to/devtools/bin \
TINYUSB=/path/to/pico-sdk/lib/tinyusb/src \
./run.sh
```

(or `make realcheck` from the top of the project). To add the emulator
device test, also give `EMU=/path/to/patched/DesktopEmulator` and
`SDL2INC=/path/to/SDL2/include/SDL2` (and `SDL2LIB` if `-lSDL2` isn't found
by itself); `emutest.cpp` and `emustubs.cpp` replace the parts of the emulator
that are not needed (window, audio, menus). `BUILD=<folder>` moves the build
products (default `./build`).

`harness.cpp` uses the firmware sources in `../../firmware/RP2350-USB-A/src`
and the library in `../../lib/v32io`.
