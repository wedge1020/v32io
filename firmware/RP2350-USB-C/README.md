# firmware: RP2350-USB-C

Hardware v32io adapter: firmware for the **Waveshare RP2350-USB-C** (the
variant with two type C *receptacles*) that presents a USB keyboard or
mouse as a "gamepad" for the Vircon32 emulator.

```
USB keyboard   --> [PIO USB port]   RP2350-USB-C   [native USB port] --> computer running
or USB mouse        (USB host, PIO)                   (USB device)       the Vircon32 emulator
```

It does the same job, with the same roles for the two ports, as the
RP2350-USB-A adapter: **everything that is not about the board itself is
identical, and is described in
[`../RP2350-USB-A/README.md`](../RP2350-USB-A/README.md)**: the modes (keyboard,
mouse), the status light, the protocols and their timing, typed characters,
and creating the emulator's joystick profiles. The gamepad has the same
names and USB IDs (`v32io:kbd` `CAFE:4B32`, `v32io:mouse` `CAFE:4D32`), so
profiles made for the USB-A adapter work with this one.

If you have the RP2350-USB-CM (the one with a type C plug) use
[`../RP2350-USB-CM`](../RP2350-USB-CM/README.md) instead: its roles are not the same.

**Status:** this firmware builds (Pico SDK 2.2.0, Pico-PIO-USB at its latest
commit) but has not been run on a board yet. The hardware notes come from
Waveshare's schematic and example code, not from measurements.

## WHICH PORT IS WHICH

| Port                  | Role   | Plugged to          | Notes                              |
| --------------------- | ------ | ------------------- | ---------------------------------- |
| native USB (type C)   | device | **the computer**    | the programming port: BOOT button, drive for flashing |
| PIO USB (type C)      | host   | **keyboard / mouse**| D+ is GPIO 12 and D- is GPIO 13    |

Both are receptacles, so the cable to the computer is type C to C or A to C,
and the keyboard needs a type C plug (see the CC notes below).
The native port is the one that must go into the computer: it is the only
one that can be a gamepad and the one that is used to flash the board.

## HARDWARE NOTES

- **Resistor R13.** In the schematic, R13 is a 1.5 kohm pull-up from 3.3 V to
  D+ (GPIO 12) of the PIO USB port, and it is fitted. A pull-up on D+ is
  what a *device* has, which conflicts with this port being a *host*. It is
  the same resistor, with the same name, that Waveshare says must be
  removed from the RP2350-USB-A for hot-plugging and low-speed devices to
  work (most keyboards and mice are low-speed). Expect to need to remove it
  here too. R10, the same footprint on D-, is not fitted: leave it.

- **Power.** The VBUS pins of both ports are wired straight to the board's
  5 V rail (no switch, no diode). The computer powers the board through the
  native port, and the keyboard is powered from that same rail through the
  PIO port. Nothing limits the current: a keyboard or mouse with a lot of
  lighting may draw more than the computer's port, or the cable, can give.
  (The 500 mA regulator on the board feeds only the chip.)
  Because the rails are shared, **never plug both ports into computers (or
  chargers) at once.**

- **CC resistors (type C power negotiation).** Every CC pin of both ports has
  a 5.1 kohm pull-down: both ports are *sinks*, they only take power. The
  PIO USB port has footprints for pull-ups to the 5 V rail (R20, R21, not
  fitted) to make it a source instead, if you ever need that.
  - The native port into the computer is the normal case (the computer
    is the source) and needs nothing.
  - For the keyboard, VBUS is present on the PIO port as soon as the board is
    powered, whatever the CC pins say. A USB-A keyboard on a type C to A
    "OTG" adapter should work, since those adapters carry VBUS and data and
    do not check CC. A keyboard with a type C plug and its own CC logic may
    wait for a source that never announces itself; that is what R20 / R21
    are for. Neither case was tested.

- The PIO USB port runs at full speed and low speed as a host (Pico-PIO-USB),
  and the 120 MHz system clock is a multiple of 12 MHz for that reason.

## BUILDING

Needs the Pico SDK (2.2 or later, with its TinyUSB submodule), the
arm-none-eabi GCC toolchain and Pico-PIO-USB. The SDK has no definition for
this board, so one is kept in `boards/` and used by the CMake file:

```
git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
mkdir build
cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make
```

This produces `v32io_usb_c.uf2`. (`-DPICO_PIO_USB_PATH=<dir>` if Pico-PIO-USB
is somewhere else.)

## FLASHING

Hold the BOOT button while plugging the **native** port into the computer.
The board shows up as a drive: copy `v32io_usb_c.uf2` into it.

## DIFFERENCES WITH RP2350-USB-A

Only these (the rest of `src/` is a plain copy; see `tools/check-firmware-sync.sh`):

| File                                  | Difference                                   |
| ------------------------------------- | -------------------------------------------- |
| `CMakeLists.txt`, `boards/`           | board definition (none in the SDK), target name |
| `src/main.c`, `src/tusb_config.h`     | comments only                                |
| `src/led.c`                           | `LED_ORDER_GRB` is 1: Waveshare's RGB demo for this board sends green first. If the "no device" light is green instead of red, set it to 0 |
