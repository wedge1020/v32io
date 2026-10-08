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
