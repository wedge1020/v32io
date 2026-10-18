# firmware: RP2350-USB-CM

Hardware v32io adapter: firmware for the **Waveshare RP2350-USB-CM** (the
variant with a type C *plug* next to a type C receptacle) that presents a
USB keyboard or mouse as a "gamepad" for the Vircon32 emulator.

```
USB keyboard   --> [native USB port]   RP2350-USB-CM   [PIO USB port] --> computer running
or USB mouse        (receptacle, host)                   (plug, device)    the Vircon32 emulator
```

It does the same job as the RP2350-USB-A and RP2350-USB-C adapters:
**everything that is not about the board itself is identical, and is
described in [`../RP2350-USB-A/README.md`](../RP2350-USB-A/README.md)**: the modes
(keyboard, mouse), the status light, the protocols and their timing, typed
characters, and creating the emulator's joystick profiles. The gamepad has
the same names and USB IDs (`v32io:kbd` `CAFE:4B32`, `v32io:mouse`
`CAFE:4D32`), so profiles made for the USB-A adapter work with this one.

**Status:** this firmware builds (Pico SDK 2.2.0, Pico-PIO-USB at its latest
commit) but has not been run on a board yet. The hardware notes come from
Waveshare's schematic and example code, not from measurements. Of the three
boards this is the one most likely to need debugging: see "How the gamepad
works here" below.

## THE PORTS ARE THE REVERSE OF THE OTHER TWO BOARDS

On this board the type C **plug** is the *PIO* USB port, and the receptacle
is the chip's *native* USB port (the programming one, with the BOOT button).
The plug is the one that goes into the computer, so:

| Port                       | Role   | Plugged to          | Notes                           |
| -------------------------- | ------ | ------------------- | ------------------------------- |
| PIO USB, type C plug       | device | **the computer**    | D+ is GPIO 13 and D- is GPIO 12 (swapped, compared to the other boards) |
| native USB, type C receptacle | host | **keyboard / mouse**| also the port used to flash the board |

So the gamepad is made with PIO, and the keyboard is read by the chip's own
USB controller. (A bonus: the native controller handles low-speed devices
directly, so no resistor needs to be removed for the keyboard to work.)

## HARDWARE NOTES

- **Resistor R10.** In the schematic, R10 is a 1.5 kohm pull-up from 3.3 V to D+
  (GPIO 13) of the plug, and it is fitted. That is exactly what a USB
  *device* needs, so **nothing has to be removed or bridged** on this board.
  (R13, the same footprint on D-, is not fitted: leave it.)

- **Power and CC.** The plug has one CC pin, with a 5.1 kohm pull-down (R21):
  it is a *sink*, it takes power from the computer. That is what is wanted
  here. The alternative, a source, would need that resistor replaced by a
  pull-up (R19, not fitted): do not do that for this use.
  The VBUS pins of the plug and of the receptacle are both wired straight to
  the board's 5 V rail (no switch, no diode), so the keyboard is powered from
  the computer through the plug, and the receptacle's CC pins are
  pull-downs too (sink). A USB-A keyboard on a type C to A "OTG" adapter
  should work, since those adapters carry VBUS and data and do not check CC;
  a keyboard with a type C plug and its own CC logic may wait for a source
  that never announces itself. Neither case was tested.
  Nothing limits the current: a keyboard or mouse with a lot of lighting may
  draw more than the computer's port can give.

- **Never connect both ports to computers at once.** Their power rails are the
  same, so each computer would try to power the other.

## HOW THE GAMEPAD WORKS HERE

Pico-PIO-USB can be a USB *device*, but with some limits compared to the
native controller on the other two boards. These are what the code does
about them, and where problems would most likely be:

- TinyUSB's PIO USB device driver in Pico SDK 2.2.0 always uses D+ = GPIO 12
  and D- = GPIO 13, and lacks `dcd_sof_enable()`, which the device stack needs
  to link. `src/main.c` wraps the driver's start-up call
  (`-Wl,--wrap=pio_usb_device_init` in `CMakeLists.txt`) to give it this
  board's pins, and has an empty `dcd_sof_enable()` (PIO USB never reports
  start-of-frame).

- The driver does nothing when asked to connect or disconnect, and the
  pull-up cannot be switched off, so the adapter could not be unplugged from
  the computer, which it has to be: the computer only reads the name and ID
  of a device when it is plugged in, and the mode (keyboard / mouse) changes
  them. Instead, `src/main.c` **drives D+ low** to unplug and releases it to
  plug back in. The computer sees both lines low, as with an unplugged
  device. (This is also how it shows no gamepad while no keyboard or mouse is
  plugged in.)

- When the board is first powered, the pull-up is there before the firmware is
  running, so the computer may notice a device and fail to talk to it for a
  moment. Some systems log a message like "device descriptor read error"
  once, then the gamepad appears.

- PIO USB is full speed only and is serviced by the CPU, but a gamepad needs
  very little of that.

If the computer never sees the gamepad, or sees it fail every time, this is
the part to look at first.

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

This produces `v32io_usb_cm.uf2`. (`-DPICO_PIO_USB_PATH=<dir>` if Pico-PIO-USB
is somewhere else.)

## FLASHING

**The plug cannot be used to flash the board:** the BOOT mode is on the native
port, the receptacle. Unplug the plug from the computer first (see the power
note above), then hold the BOOT button while plugging a cable from the
computer into the **receptacle**. The board shows up as a drive: copy
`v32io_usb_cm.uf2` into it. Then unplug the cable, and plug the plug into the
computer for use.

## DIFFERENCES WITH RP2350-USB-A

The rest of `src/` is a plain copy (see `tools/check-firmware-sync.sh`).

| File                           | Difference                                         |
| ------------------------------ | -------------------------------------------------- |
| `CMakeLists.txt`, `boards/`    | board definition (none in the SDK), target name, the `--wrap` |
| `src/tusb_config.h`            | host on port 0 (native), device on port 1 (PIO USB) |
| `src/main.c`                   | roles of the ports, plugging in / out with D+, pins of the device, `dcd_sof_enable()` |
| `src/led.c`                    | `LED_ORDER_GRB` is 1: Waveshare's RGB demo for this board sends green first. If the "no device" light is green instead of red, set it to 0 |
