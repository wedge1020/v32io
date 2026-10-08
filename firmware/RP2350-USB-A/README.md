# v32io-pico

Hardware v32io adapter: firmware  for the **Waveshare RP2350-USB-A** that
presents  a  USB keyboard  or  mouse  as  a  "gamepad" for  the  Vircon32
emulator.

```
USB keyboard   --> [type A]  RP2350-USB-A  [type C] --> computer running
or USB mouse        (USB host, PIO)        (USB device)   the Vircon32 emulator
```

The computer  sees an ordinary USB  gamepad with 11 buttons,  named after
what is plugged into the adapter:

| Plugged in | Mode          | Gamepad name  | USB ID        | Program reads it with |
| ---------- | ------------- | ------------- | ------------- | --------------------- |
| keyboard   | keyboard mode | `v32io:kbd`   | `CAFE:4B32`   | `keyboard.h`          |
| mouse      | mouse mode    | `v32io:mouse` | `CAFE:4D32`   | `mouse.h`             |

No  changes are  needed in  the  emulator: each  mode is  used through  a
regular joystick  profile, and Vircon32  programs read it with  the v32io
libraries (`keyboard.h` / `mouse.h`, both built on `v32io.h`).

## HARDWARE NOTES

- The type A port is driven with PIO on GPIO 12 (D+) and GPIO 13 (D-).
  These are the pins in the Pico SDK board definition for this board.
- **Resistor R13.** Waveshare states that R13 must be removed for the
  type A port to support hot-plugging and low-speed devices as a host,
  and that after removing it the port can't be used as a device. Most
  keyboards and mice are low-speed devices, so expect to need this.
- The device is powered from the type A port, which takes its power
  from the computer through the type C port. Devices with a lot of
  lighting may draw more than the board can pass through.

## MODES

The mode  follows what  is plugged  in. Changing  mode makes  the gamepad
unplug from  the computer and  plug back in (300  ms later) as  the other
device, since the computer  only reads a device's name and  ID when it is
plugged in.

When a  keyboard and a  mouse are  both plugged in  (through a hub,  or a
wireless receiver  for both),  **the one plugged  in first**  decides the
mode; for a single device with both, such as a wireless receiver, that is
whichever  it reports  first (usually  the  keyboard). When  that one  is
unplugged, the  adapter switches to the  other. This can be  changed with
`MODE_PRIORITY` at  the top  of `src/main.c`  (first plugged,  always the
keyboard, or always the mouse).

Only devices that support the USB  boot protocol are used. Every keyboard
and  mouse is  expected  to, but  some special  devices  (like the  extra
keyboard of some gaming mice) may be ignored or taken for a keyboard.

## STATUS LIGHT

The board's onboard RGB LED shows the state of the adapter:

| Light                                 | Meaning                              |
| ------------------------------------- | ------------------------------------ |
| solid red                             | powered, no keyboard or mouse        |
| blinking yellow                       | a device was plugged in and is being set up (at least 3 blinks) |
| 3 green blinks, then solid green      | keyboard mode, normal operation      |
| 3 cyan blinks, then solid cyan        | mouse mode, normal operation         |
| 2 quick blue blinks, then steady blue blinking | setup mode (see below)      |
| 3 blue blinks, then solid green / cyan | setup mode was left                 |

If something  that is not  a keyboard or mouse  is plugged in,  the light
blinks yellow for a moment and goes back to red.

Colors and blink  timings are at the top  of `src/led.c`. `LED_ORDER_GRB`
is set  to 0  there (red, green,  blue order), which  is what  this board
needs; set it to 1 if the "no device" light shows green instead of red.

## GAMEPAD PRESENCE

The computer only  sees the gamepad while a keyboard  or mouse is plugged
into the adapter. With none, the type C port keeps powering the board but
stays  disconnected  for  data,  so  for  the  computer  the  gamepad  is
unplugged. It comes back when a device is detected.

## PROTOCOLS

Gamepad buttons follow  the order of the console's INP  ports (`0x402` to
`0x40C`). Opposite directions are never pressed at once, in any mode (the
console would release one of them).

| Button | Vircon32 control | Keyboard mode   | Mouse mode               |
| ------ | ---------------- | --------------- | ------------------------ |
| 0      | Left             | strobe          | X counter: trit −        |
| 1      | Right            | strobe          | X counter: trit +        |
| 2      | Up               | key pressed     | Y counter: trit −        |
| 3      | Down             | key released    | Y counter: trit +        |
| 4      | Start            | key code, bit 0 | middle button            |
| 5      | A                | key code, bit 1 | left button              |
| 6      | B                | key code, bit 2 | right button             |
| 7      | X                | key code, bit 3 | X counter: Gray high     |
| 8      | Y                | key code, bit 4 | X counter: Gray low      |
| 9      | L                | key code, bit 5 | Y counter: Gray high     |
| 10     | R                | key code, bit 6 | Y counter: Gray low      |

The gamepad also reports X and Y axes that never move: they only exist so
that every system takes it for a gamepad. Both protocols are described in
full in the v32io library README.

### KEYBOARD TIMING

The emulator's own `v32kbd` device delivers  exactly 1 event per frame. A
real gamepad can't know when frames  happen, so the adapter instead holds
every key event long enough to be seen:

1. key code and action are sent (strobe unchanged)
2. 8 ms later, the strobe switches sides
3. that state is held for 34 ms (2 frames) before the next event

So events go out at about 23 per second (around 11 keystrokes, each being
a press and a  release). Faster bursts wait in a queue  of 128 events and
are delivered in order, slightly delayed.

`SETTLE_MS`, `HOLD_MS` and `QUEUE_SIZE` are at the top of `src/kbd.c`.

### MOUSE TIMING

Movement is sent as  2 counters (one per axis) that go  around a cycle of
12 positions, one step at a  time, each step changing exactly one button.
A program can tell apart up to 5 steps per frame, so:

- Every 4 mouse counts make 1 step (`MOUSE_DIVISOR`).
- Steps are spaced at least 5 ms apart per axis (`MOUSE_STEP_MS`), a speed
  limit of 200 steps per second. A program moves its pointer 2 pixels per
  step by default (its own setting), so 400 pixels per second.
- Movement beyond the speed limit waits, but only up to 6 steps
  (`MOUSE_BACKLOG`): the rest is dropped, so the pointer stops soon after
  the mouse does.
- Every button change lasts at least 25 ms (`MOUSE_CLICK_MS`), so even a
  very quick click is seen by the program.

If the emulator skips a whole frame  while the mouse moves at full speed,
the program can  see 6 or 7 steps  at once and read them  as movement the
other  way. Setting  `MOUSE_STEP_MS` to  7  prevents that,  with a  lower
speed  limit (143  steps per  second). All  of these  are at  the top  of
`src/mouse.c`,  and can  also be  set when  building (for  example `cmake
-DCMAKE_C_FLAGS=-DMOUSE_STEP_MS=7 ..`).

The mouse wheel is not reported: the 11 buttons have no room left for it.

## SETTING UP THE EMULATOR

The emulator needs  a joystick profile for each mode,  since the computer
sees them as 2 different gamepads. The  device has to be plugged into the
adapter  for the  computer to  see the  gamepad. Vircon32's  EditControls
creates profiles  by asking you to  press each control on  its own, which
neither protocol ever does. For this, each mode has a **setup mode**.

Both profiles map  the same way: button  0 to Left, button  1 to Right...
button 10 to R. Leave the Command button unmapped.

### KEYBOARD (`v32io:kbd`)

1. Press **Scroll Lock** on the keyboard to enter setup mode. The status
   light blinks blue while in this mode.
2. In EditControls, create a profile for the `v32io:kbd` joystick and
   press these keys when asked for each control:

   | Left | Right | Up | Down | Start | A  | B  | X  | Y  | L   | R   |
   | ---- | ----- | -- | ---- | ----- | -- | -- | -- | -- | --- | --- |
   | F1   | F2    | F3 | F4   | F5    | F6 | F7 | F8 | F9 | F10 | F11 |

3. Press **Scroll Lock** again to go back to normal operation (3 blue
   blinks, then solid green). Unplugging the keyboard also leaves setup
   mode.

### MOUSE (`v32io:mouse`)

1. Hold the **3 mouse buttons** (left, right and middle / wheel click)
   together for 2 seconds to enter setup mode. The status light blinks
   blue while in this mode. Release them.

2. In EditControls, create a profile for the `v32io:mouse` joystick. For
   each control, in order (Left, Right, Up, Down, Start, A, B, X, Y, L,
   R), select it in EditControls and then **left click** the adapter's
   mouse: each left click presses the next button (0, 1, 2... 10). If a
   click went to the wrong control, a **right click** goes back one
   button.

3. Hold the 3 buttons for 2 seconds again to go back to normal operation
   (3 blue blinks, then solid cyan). Unplugging the mouse also leaves
   setup mode.

The setup  clicks always go  in the order above,  so map the  controls in
that order. Use the computer's own mouse for EditControls itself.

### SELECT THE PROFILE

In the emulator,  menu Gamepads, select the profile for  the gamepad your
program expects (Gamepad 2 for the test programs).

Instead of setup mode, the profile can  also be written by hand: copy the
`v32io:kbd` entry in `Config-Controls.xml`  and change its nickname, name
and GUID to the ones EditControls shows for `v32io:mouse`:

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

Entering  or  leaving  setup  mode releases  all  buttons,  restarts  the
keyboard strobe  and puts the  mouse counters back at  rest; `keyboard.h`
handles that  with no phantom  key, and `mouse.h`  with at most  one jump
of  the pointer.  While  in  mouse setup  mode,  a  running program  sees
meaningless movement.

## BUILDING

Needs the  Pico SDK (2.2 or  later, which has the  board definition, with
its TinyUSB submodule), the arm-none-eabi GCC toolchain and Pico-PIO-USB:

```
git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
mkdir build
cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make
```

This produces `v32io_pico.uf2`. A prebuilt one is included.

## FLASHING

Hold the  BOOT button while plugging  the type C port  into the computer.
The board shows up as a drive: copy `v32io_pico.uf2` into it.

## SOURCE FILES

| File                    | What it does                                        |
| ----------------------- | --------------------------------------------------- |
| `src/main.c`            | USB host and device, mode selection, sending reports |
| `src/v32io.h`           | what all modes share: buttons, report, mode         |
| `src/kbd.c`, `v32kbd.h` | keyboard mode (v32kbd protocol, setup mode)         |
| `src/keymap.c`          | USB key usages to v32kbd key codes                  |
| `src/mouse.c`, `v32mouse.h` | mouse mode (v32mouse protocol, setup mode)      |
| `src/led.c`, `led.h`    | status light                                        |
| `src/usb_descriptors.c` | gamepad descriptors, by mode                        |

## NOTES

- Keyboards are used in USB boot protocol: up to 6 keys plus modifiers
  held at once. Mice too: 3 buttons and movement.
- The USB IDs (`0xCAFE:0x4B32` and `0xCAFE:0x4D32`) are not officially
  assigned. They can be changed in `src/usb_descriptors.c`.
- Events sent while the emulator is paused or its window has lost focus
  are not seen by the program. For the mouse, that can make the pointer
  jump once when the emulator resumes.
