# v32io protocols (reference)

All v32io devices  are an 11-button gamepad. Packed controls  = 1 bit per
control in INP port order (bit n  = port 0x402+n): Left, Right, Up, Down,
Start, A, B, X, Y, L, R.

Hard          rule          (verified         in          DesktopEmulator
V32GamepadController::SetGamepadControl):   the   console   never   shows
Left+Right or Up+Down together —  pressing one releases the other. Each
direction pair is therefore a 3-state "trit".

## Layering (in-environment)

1.  `v32io.h`  core: in-RAM  routine  reads  the  11 ports,  packs  them;
`v32io_probe()`  once per  frame  (repeat  calls in  the  same frame  are
no-ops), keeps `data`/`previous`, connection + `plugged` (just connected)
flags.

2. `keyboard.h` (v32kbd) and `mouse.h`  (v32mouse) each own a `v32io *io`
and interpret `v32io_read()`.

## v32kbd (USB `v32io:kbd`, CAFE:4B32)

Bits  0-1  strobe (alternates  L/R  per  event),  bit  2 pressed,  bit  3
released, bits  4-10 7-bit key  code. Firmware:  code first, strobe  8 ms
later, hold 34 ms. Setup mode: Scroll Lock, F1-F11 = buttons 0-10.

## v32mouse (USB `v32io:mouse`, CAFE:4D32)
- Start = middle, A = left, B = right (held as-is; each change lasts >= 25 ms).
- Movement = two 12-position counters, each one Gray pair + one trit; every step changes exactly one control:
  - X: trit Left(-)/Right(+), Gray X(high) Y(low). Y: trit Up(-)/Down(+), Gray L(high) R(low).
  - position 0..11 -> gray 00 00 00 01 01 01 11 11 11 10 10 10, trit - 0 + + 0 - - 0 + + 0 -
  - rest = position 1 (nothing pressed). Decode: group g from Gray, pos = 3g + (g even ? t : 2-t).
- Program reads delta mod 12 in -5..+5 (6 = ambiguous, taken as 0). Firmware: 4 counts/step, >= 5 ms between steps (from delivery), backlog 6 steps. A whole skipped frame at full speed can alias; MOUSE_STEP_MS=7 prevents it (143 steps/s).
- 1152 states = 8 button combos x 12 x 12: no room for the wheel.
- Setup mode: hold L+M+R 2 s; each left click presses the next gamepad button (0..10), right click goes back one.

## Firmware mode selection

First-plugged HID interface decides the mode (MODE_PRIORITY in main.c can
force keyboard/mouse).  Mode change =  USB disconnect, 300  ms, reconnect
with the other PID/product string.

## Typed characters (extended characters), v32kbd

Key codes identify keys, so they cannot say what a key typed when that is
not ASCII (an accented letter from a layout, dead key or AltGr; a euro
sign; a box drawing character). The BIOS font (CP1252, with box drawing
characters in the codes 14 to 31) has 224 such glyphs, more than the 128
values one 7-bit code field can carry.

**Scheme: codepage prefix + payload.** Key codes 30 and 31 were unused.
A typed character is 2 consecutive events:

| Event   | Key code field | Meaning                                  |
| ------- | -------------- | ---------------------------------------- |
| prefix  | 30 or 31       | `V32KEY_PAGE_HIGH` / `V32KEY_PAGE_LOW`   |
| payload | 0 to 127       | low 7 bits of the character              |

`PAGE_HIGH` + p = character 128 + p (128 to 255); `PAGE_LOW` + p =
character p (0 to 127, in practice the glyphs 14 to 31). Payload 0 is valid
(`PAGE_HIGH` + 0 is the euro sign).

* **Both events are key releases** (Down). A library from before this
  extension then sees two releases: `v32kbd_read()` skips them,
  `v32kbd_readevent()` returns them as releases, and no key press is ever
  produced (verified in `tests/v32io`, modes 3 and 4 of the harness). The
  only side effect there is that `held[]` of the key whose code equals the
  payload is cleared, in the unlikely case it was down. The new library
  accepts either action, so this could be changed later.
* The strobe, hold time and one-event-per-frame rules are unchanged: each
  of the two events is an ordinary event, so a character takes 2 frames.
* A prefix waits `V32KBD_PAGE_TIMEOUT` (30) frames for its payload; after
  that, or when the gamepad is plugged in again, it is forgotten and the
  next event is read normally. The payload is taken as such whatever its
  code and action, even 30 or 31.
* The key that typed a character is not reported (no press, no release),
  so the program sees either the key or the character.
* The queue (firmware: 128 events, emulator: 256) never splits a pair: when
  it is full, whole characters are discarded, oldest first; a payload
  whose prefix was already reported is kept and the new event is lost.

Where characters come from:

* **Emulator** (`emulator/v32io/v32io.patch`): SDL's `SDL_TEXTINPUT`, i.e. whatever
  the operating system's layout types. An ASCII key press is held back for
  one frame at the most, until the text it typed (if any) is known. If the
  text is a BIOS glyph that is not ASCII, the key press is replaced by the
  character; otherwise the key is reported as before.
* **Adapter** (`firmware/RP2350-USB-A/src/compose.c`): a USB keyboard only
  says which key is down, so the adapter has its own compose layer:
  AltGr + key, AltGr + dead key + letter, and Alt codes on the numeric
  keypad. See `COMPOSE.md`.

### Alternatives that were considered

1. **A third action in the Up/Down pair** (neither Up nor Down = "text"),
   with the 7-bit field as a payload for one character per event. It saves
   a frame per character, but 7 bits can't hold 224 glyphs (it would
   need a second packet anyway), and a library from before this change
   reads "neither" as a *release* of key `payload`, with no key at all
   left to explain it.
2. **Using the key code field's top values (127 or so) for prefixes.** Code
   127 is Delete, and codes 30 and 31 were free: no reason to take a real
   key's code.
3. **UTF-8 / Unicode.** The BIOS font has no more glyphs than CP1252 plus
   the box drawing set, so a program can't draw anything else; mapping to
   the font's own codes keeps the program side trivial.
4. **A press for the prefix and a release for the payload** (or both as
   presses). Rejected: a press would make old libraries queue ghost key
   presses, and old programs would type garbage.
5. **Layouts in the adapter** (keyboard-layout tables for several
   languages). Too large and not what the adapter is for; the compose layer
   covers the whole font from a US keyboard, and a layout can still be added
   later as a table for `compose_key()`.

Library side: `v32kbd_read()` returns the BIOS code (plus `V32KBD_GLYPH`
below 32 so it can't be confused with a key code) and `v32kbd_glyph(value)`
turns any value it returns into the code to draw.
