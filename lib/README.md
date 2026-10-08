# v32io

Vircon32 C drivers/libraries for jury-rigged input devices that reach the
console  through  a  gamepad  port: `v32kbd`  (keyboard)  and  `v32mouse`
(mouse), both built on the `v32io` core.

## DRIVERS

| File         | What it is                                              |
| ------------ | ------------------------------------------------------- |
| `v32io.h`    | core: reads a gamepad's 11 controls and packs them      |
| `keyboard.h` | `v32kbd` driver: key events                             |
| `mouse.h`    | `v32mouse` driver: movement and buttons                 |

Both  the v32kbd  and v32mouse  drivers include  the `v32io.h`  header to
handle the gamepad button inputs. Each then takes that data and processes
it according to its specific scheme.

## DEMOS

| File         | What it is                                              |
| ------------ | ------------------------------------------------------- |
| `v32kbd.c`   | keyboard test program: type text on screen              |
| `v32mouse.c` | mouse test program: draw on screen                      |

A program  includes only the  driver(s) it  needs; each one  includes the
core. Keyboard and  mouse can be used  at the same time, each  on its own
gamepad port.

## HOW THE DEVICES WORK

A v32io device is  seen by the console as a regular  gamepad, but its 11
controls carry data instead of game presses. The core packs them into a
single value, the **packed controls**, with one bit per control in the
order of the console's INP ports:

| Bit | IOPort  | Control                  | `v32io.h` mask | v32kbd          | v32mouse              |
| --- | ------- | ------------------------ | -------------- | --------------- | --------------------- |
| 0   | `0x402` | `INP_GamepadLeft`        | `V32IO_LEFT`   | strobe          | X counter: trit −     |
| 1   | `0x403` | `INP_GamepadRight`       | `V32IO_RIGHT`  | strobe          | X counter: trit +     |
| 2   | `0x404` | `INP_GamepadUp`          | `V32IO_UP`     | key pressed     | Y counter: trit −     |
| 3   | `0x405` | `INP_GamepadDown`        | `V32IO_DOWN`   | key released    | Y counter: trit +     |
| 4   | `0x406` | `INP_GamepadButtonStart` | `V32IO_START`  | key code, bit 0 | middle button         |
| 5   | `0x407` | `INP_GamepadButtonA`     | `V32IO_A`      | key code, bit 1 | left button           |
| 6   | `0x408` | `INP_GamepadButtonB`     | `V32IO_B`      | key code, bit 2 | right button          |
| 7   | `0x409` | `INP_GamepadButtonX`     | `V32IO_X`      | key code, bit 3 | X counter: Gray high  |
| 8   | `0x40A` | `INP_GamepadButtonY`     | `V32IO_Y`      | key code, bit 4 | X counter: Gray low   |
| 9   | `0x40B` | `INP_GamepadButtonL`     | `V32IO_L`      | key code, bit 5 | Y counter: Gray high  |
| 10  | `0x40C` | `INP_GamepadButtonR`     | `V32IO_R`      | key code, bit 6 | Y counter: Gray low   |

So the bit number of each control is its port number minus `0x402`.

One  rule applies  to  every  device: the  console  never shows  opposite
directions (Left +  Right, or Up + Down) pressed  at once, since pressing
one releases  the other.  Both protocols  are designed  so that  it never
happens.

Since the console  reads gamepads once per frame, every  driver has to be
**probed once  on every  frame**. If  frames are  skipped, events  can be
lost.

## V32IO (CORE)

`v32io.h` knows nothing of what the  data means. It reads all 11 controls
of  a gamepad  with a  small in-RAM  machine code  routine and  keeps the
packed controls of the latest probe and the one before it.

```
v32io *io = v32io_init (SECOND_GAMEPAD_PORT);

while (true)
{
    v32io_probe (&io);                  // once per frame
    data = v32io_read (&io);            // packed controls
    ...
}
```

### instance: `v32io` struct

The gamepad id,  the packed controls of the latest  probe (`data`) and of
the  one  before  (`previous`),  the connection  state,  and  the  in-RAM
routine.

### initialize: `v32io_init()`

```
v32io  *v32io_init (int gamepad);
```

Allocates  the  instance, generates  its  in-RAM  routine and  reads  the
controls once, so  that `data` and `previous` start with  what the device
is showing right now.

### release: `v32io_free()`

```
void    v32io_free (v32io **io);
```

Frees the instance and sets the pointer to `NULL`.

### read the controls now: `v32io_scan()`

```
int     v32io_scan (v32io *io);
```

Selects the instance's gamepad, `CALL`s  the in-RAM routine, restores the
previously selected gamepad and returns  the packed controls. It does not
update `data`/`previous`: drivers use `v32io_probe()` instead.

The routine is generated just once,  by `v32io_init()`, and stored in the
instance (so there  is nothing to release separately). It  reads each INP
control port, turns it into a 0 or 1, and packs them all together:

```
routine[0]            = 0x54000000;        // PUSH R0
routine[1]            = 0x54200000;        // PUSH R1
routine[2]            = 0x54400000;        // PUSH R2
routine[3]            = 0x4E200000;        // MOV R1, 0
routine[4]            = 0x00000000;        // immediate
routine[5]            = 0x4C424000;        // MOV R2, R1
for (index            = 0;
     index           <  11;
     index            = index + 1)
{
    offset            = ((index * 5) + 6);
    port              = index + 2;         // 0x402 to 0x40C

    routine[offset]   = 0x5C000400 | port; // IN  R0, port
    routine[offset+1] = 0x24040000;        // IGT R0, R2
    routine[offset+2] = 0x96000000;        // SHL R0, index
    routine[offset+3] = index;             // immediate value
    routine[offset+4] = 0x88200000;        // OR  R1, R0
}

offset                = (index * 5) + 6;
routine[offset]       = 0x4E034000;        // MOV [raw], R1
routine[offset+1]     = (int) &(io -> raw);
routine[offset+2]     = 0x58400000;        // POP R2
routine[offset+3]     = 0x58200000;        // POP R1
routine[offset+4]     = 0x58000000;        // POP R0
routine[offset+5]     = 0x10000000;        // RET
routine[offset+6]     = 0x00000000;        // HLT (for safety)
```

`routine` is a  68 element array (67 for operation,  1 for `HLT` safety),
packed one word  at a time in ascending order:  array elements are always
at  increasing addresses,  which  is  also the  order  in  which the  CPU
executes.

### probe: `v32io_probe()`

```
bool    v32io_probe (v32io **io);
```

Call it once every frame. Moves `data` to `previous` and reads new packed
controls. Further  calls within  the same frame  do nothing  (the console
only updates gamepads between frames), so  a driver can never consume the
same frame twice. Returns true when the packed controls changed.

### accessors

```
int     v32io_read      (v32io **io);            // packed controls (latest probe)
int     v32io_changed   (v32io **io);            // bits that changed in it
bool    v32io_isdown    (v32io **io, int mask);  // any control in mask held?
bool    v32io_connected (v32io **io);            // gamepad connected?
bool    v32io_plugged   (v32io **io);            // became connected in it?
```

A  gamepad that  has just  been connected  shows every  control released,
whatever the  device was  showing; `v32io_plugged()` lets  protocols that
depend on history (like the mouse counters) start over.

## V32KBD (KEYBOARD)

The device reports at most 1 key event per frame, and it keeps that state
until the  next event.  The **strobe**  is what  tells events  apart: the
first event presses Left, the next  one presses Right, then Left again...
so a  new event has arrived  whenever the pressed side  is different from
the last one seen. This is what  allows the same key to be received twice
in a row. Before the first event, all controls are unpressed.

### KEY CODES

Key codes  are 7 bits  (1 to 127) and  they identify **keys**,  not typed
characters. Keys that have an ASCII  character report it as typed with no
shift on a US layout: `a` to `z`, `0` to  `9`, space and `` ` - = [ ] \ ;
' ,  . /  ``. Shift is  reported as a  key of  its own, and  this library
applies it (along with caps lock) to obtain the typed character.

The other keys use these codes:

| Code    | Key (`V32KEY_` name)            | Code    | Key (`V32KEY_` name)     |
| ------- | ------------------------------- | ------- | ------------------------ |
| 1       | `UP`                            | 11      | `RCTRL`                  |
| 2       | `DOWN`                          | 12      | `LALT` (left option)     |
| 3       | `LEFT`                          | 13      | `ENTER`                  |
| 4       | `RIGHT`                         | 14 - 25 | `F1` to `F12`            |
| 5       | `CAPSLOCK`                      | 26      | `RALT` (right option)    |
| 6       | `LSHIFT`                        | 27      | `ESCAPE`                 |
| 7       | `RSHIFT`                        | 28      | `LGUI` (left command)    |
| 8       | `BACKSPACE`                     | 29      | `RGUI` (right command)   |
| 9       | `TAB`                           | 30, 31  | unused                   |
| 10      | `LCTRL`                         | 127     | `DELETE`                 |

Code 0  is never reported. Numeric  keypad keys report the  same codes as
their main keyboard equivalents.

### API

Typical use:

```
v32kbd *keyboard  = v32kbd_init (SECOND_GAMEPAD_PORT);  // port id 1

while (true)
{
    v32kbd_probe (&keyboard);           // once per frame

    key           = v32kbd_read (&keyboard);
    while (key   >  0)                  // process all key presses
    {
        ...
        key       = v32kbd_read (&keyboard);
    }

    end_frame ();
}
```

The API  is the same as  before the split into  `v32io`; programs written
for the previous `keyboard.h` keep working unchanged.

- **`v32key` struct**: a single key event in the input list: `value` (key
  code), `symbol` (typed character, with shift and caps lock applied as
  they were when the key was pressed) and `pressed` (true for press,
  false for release).
- **`v32kbd` struct**: a keyboard instance. It holds its `v32io` instance
  (`io`), the input list, the last strobe side seen, the caps lock state
  and which keys are held.
- **`v32kbd_init (int gamepad)`**: allocates the instance and its `v32io`
  instance. It takes the current strobe side as starting point, so a
  state left in the device from before (the device is not affected by
  console resets) is not taken as a new key.
- **`v32kbd_free (v32kbd **)`**: frees the instance, its `v32io` instance
  and any unread key events, and sets the pointer to `NULL`.
- **`v32kbd_probe (v32kbd **)`**: call it once every frame. It probes the
  `v32io` instance and, if the strobe changed side, adds the new key
  event to the input list and updates the held keys and caps lock.
  Returns true when a key event was received. Up to 64 events can wait in
  the list; beyond that, new ones are dropped.
- **`v32kbd_read (v32kbd **)`**: returns the next key **press** (releases
  are skipped) as its typed character (compatible with the BIOS font).
  Keys with no character return their key code, which is always under 32
  or 127. Returns 0 when there is nothing left to read.
- **`v32kbd_readevent (v32kbd **, bool *pressed)`**: like `v32kbd_read()`
  but it returns every event (presses and releases) as a key code, with
  no shift applied. Use one or the other on a given keyboard: both take
  events from the same list.
- **`v32kbd_isdown (v32kbd **, int keyval)`**: is the key held? For
  modifiers and game-like controls, e.g.
  `v32kbd_isdown (&keyboard, V32KEY_LCTRL)`.
- **`v32kbd_symbol (v32kbd *, int keyval)`**: applies the current shift and
  caps lock state to a key code (US layout).
- **`v32kbd_scan (v32kbd *)`**: kept for compatibility; same as
  `v32io_scan()` on the keyboard's `v32io` instance.
- `v32key_newkey()`, `v32kbd_addkey()`, `v32kbd_getkey()`: input list
  handling. The caller of `v32kbd_getkey()` becomes responsible for
  calling `free()` on the returned key.

## V32MOUSE (MOUSE)

### BUTTONS

Left, right and  middle (wheel click) are reported as  they are, held for
as long as they  are. The adapter keeps every button  change for at least
25 ms, so even a very quick click lasts more than a frame.

### MOVEMENT

Movement is not sent as deltas (a delta that is held for a frame can't be
told apart from the same delta sent twice). Instead, the device keeps **2
counters**, one  per axis, that go  around a cycle of  12 positions. Each
step moves a counter one position forward  (right / down) or back (left /
up), and every  step changes **exactly one control**, so  the console can
never see  a state that is  halfway between 2 positions.  Each frame, the
library compares counter positions with the previous ones.

Each counter is  made of a 2-bit  Gray code (4 groups) and  a **trit**: a
pair of  opposite directions,  which the console  never shows  pressed at
once, so  it has 3 states  (negative, none, positive). Within  each group
the trit goes negative / none / positive, and in odd groups backwards:

| Position | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| -------- | - | - | - | - | - | - | - | - | - | - | -- | -- |
| Gray     | 00| 00| 00| 01| 01| 01| 11| 11| 11| 10| 10 | 10 |
| Trit     | − | 0 | + | + | 0 | − | − | 0 | + | + | 0  | −  |

For X the  trit is Left (−) /  Right (+) and the Gray code  is X (high)
and Y (low). For Y the trit is Up (−) / Down (+) and the Gray code is L
(high) and R (low). At rest after power on, both counters are at position
1, which has no controls pressed at all.

With 12  positions, a counter  can move up to  5 positions per  frame and
still be read correctly (6 can't be told apart from −6, and is taken as
no movement). The  adapter sends at most  1 step per axis every  5 ms (so
the limit is 200  steps per second) to stay within  that, even for frames
that come a few milliseconds late.

This layout  uses every state  the 11 controls  can show: 7  buttons (128
combinations) times 2  direction pairs (3 states each)  make 1152 states,
which  is  exactly 8  mouse  button  combinations  ×  12 ×  12  counter
positions.  There is  no room  left for  the wheel,  so scrolling  is not
reported.

### API

Typical use:

```
v32mouse *mouse  = v32mouse_init (SECOND_GAMEPAD_PORT);  // port id 1

while (true)
{
    v32mouse_probe (&mouse);            // once per frame
    v32mouse_position (&mouse, &x, &y);

    if (v32mouse_pressed (&mouse, V32MOUSE_LEFT))
    {
        ...                             // clicked at x, y
    }

    end_frame ();
}
```

- **`v32mouse` struct**: a mouse instance. It holds its `v32io` instance
  (`io`), the last counter positions, the movement of the latest probe in
  steps (`stepx`, `stepy`) and pixels (`dx`, `dy`), the pointer (`x`,
  `y`), its bounds and scale, and the button states (`buttons`, plus
  `down`/`up` for the changes in the latest probe).
- **`v32mouse_init (int gamepad)`**: allocates the instance and its
  `v32io` instance, and takes the current counter positions as the
  starting point. The pointer starts at the center of the screen,
  bounded to the screen (0 to 639, 0 to 359), with a scale of 2 pixels
  per step.
- **`v32mouse_free (v32mouse **)`**: frees the instance and its `v32io`
  instance, and sets the pointer to `NULL`.
- **`v32mouse_probe (v32mouse **)`**: call it once every frame. Probes
  the `v32io` instance, works out the movement, moves the pointer and
  updates the buttons. Returns true if the mouse moved or any button
  changed. When the gamepad has just been connected, the counters start
  over (no movement is taken from that frame).
- **`v32mouse_delta (v32mouse **, int *dx, int *dy)`**: movement in the
  latest probe, in pixels. Either pointer can be `NULL`.
- **`v32mouse_position (v32mouse **, int *x, int *y)`**: the pointer.
- **`v32mouse_setposition (v32mouse **, int x, int y)`**: place the
  pointer (kept within bounds).
- **`v32mouse_setbounds (v32mouse **, int minx, int miny, int maxx, int maxy)`**:
  area the pointer can move in (inclusive).
- **`v32mouse_setscale (v32mouse **, int scale)`**: pixels per step.
- **`v32mouse_isdown (v32mouse **, int button)`**: is the button held?
  Buttons are `V32MOUSE_LEFT`, `V32MOUSE_RIGHT`, `V32MOUSE_MIDDLE` (and
  `V32MOUSE_ANY`).
- **`v32mouse_pressed (v32mouse **, int button)`** /
  **`v32mouse_released (v32mouse **, int button)`**: did the button go
  down / up in the latest probe?
- `v32mouse_counter()`, `v32mouse_steps()`: decoding helpers (a counter
  position from the packed controls, and the movement between 2
  positions).

## USING IT IN THE EMULATOR

### MODIFIED DESKTOP EMULATOR

Open menu Gamepads, pick a gamepad  and select `v32kbd` or `v32mouse`.

NOTE: The `v32kbd`  emulated device cannot be selected  while any gamepad
uses the  `Keyboard` device (and vice  versa), since both need  access to
the host keyboard.

Emulator hotkeys  (Esc, F2, F4, F5  and the Ctrl shortcuts)  keep working
and are also sent to the program.

### HARDWARE ADAPTER (v32io-pico)

The adapter  shows up as  a joystick named `v32io:kbd`  or `v32io:mouse`,
depending on what is plugged into it. Each needs its own joystick profile
in the emulator: see the adapter's  README for creating them (both have a
setup mode for EditControls).

Both  profiles  map the  same  way,  button *n*  to  control  *n*, so  in
`Config-Controls.xml` they only differ in nickname, name and GUID:

```
<joystick nickname="v32io-mouse">
    <guid>...</guid>
    <name>v32io: mouse</name>
    <left button="0" />
    <right button="1" />
    <up button="2" />
    <down button="3" />
    <button-start button="4" />
    <button-a button="5" />
    <button-b button="6" />
    <button-x button="7" />
    <button-y button="8" />
    <button-l button="9" />
    <button-r button="10" />
</joystick>
```

The GUID  (and the exact name)  depend on the computer's  system, so they
are best  taken from EditControls,  which shows  them for the  plugged in
device.

The  test  programs  (`v32kbd.c`,  `v32mouse.c`)  expect  the  device  in
**Gamepad 2** (id 1), leaving Gamepad 1 free for a regular gamepad.
