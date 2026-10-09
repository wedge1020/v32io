# v32io

Vircon32 hardware  accessories and  in-environment Vircon32 C  drivers to
support them.

## v32io Vircon32 C libraries

This repository hosts the in-environment Vircon32 drivers (in the form of
Vircon32 C libraries):  header files you include into  your existing BIOS
or CART  that provides the various  functions to read /  decode / process
the gamepad button-encoded data from the outside world.

Currently provided are:

  * `v32io`: the core data access library (handles processing of buttons)
  * `v32kbd`: the library allowing full keyboard functionality
  * `v32mouse`: the library providing mouse functionality

The Vircon32 C libraries is located under `lib/`

## v32io hardware firmware

In  order  for the  Vircon32  in-environment  drivers to  function,  some
external conduit is required. This has taken the form of two approaches:

### Hardware peripheral

An emulator-independent  solution takes the  form of custom  Raspberry pi
pico 2 firmware loaded onto a  specific and modified board (The Waveshare
RP2350-USB-A with its R13 resistor removed).

* URL: https://www.waveshare.com/rp2350-usb-a.htm?srsltid=AU7gw4X3Jb9rUmb0MDpPOuzu_z4PlolE_jQZhU-r79UBAfWo9jPr1wsY

The firmware provides support for  standard, conforming USB keyboards and
mice, using the color LED to  provide status indicators of the device (no
device recognized, device connecting/connected, and configuration mode).

The configured  hardware peripheral  (ie a Vircon32  EditControls profile
with  inputs mapped  to  gamepad buttons),  when  connected will  present
itself to any Vircon32 emulator (stock or modified) as one of `v32io:kbd`
or `v32io:mouse`, which can be selected by any of the four gamepad ports.
The emulator does not know it is anything other than a USB gamepad.

## DesktopEmulator patches

In  lieu  of using  the  `v32io`  Vircon32  Hardware Accessory,  you  can
instead  use a  modified  version of  the  Vircon32 DesktopEmulator.  The
stock/official emulator does not currently include this functionality, so
you have two options regarding the emulator: patching the stock emulator,
or using a fork of the  Vircon32 DesktopEmulator with the patches already
applied.

### Modified Emulator

The  stock   Vircon32  emulator   does  not   include  support   for  any
`v32io`-based peripherals.

This fork of the [Vircon32 ComputerSoftware](https://github.com/wedge1020/ComputerSoftware/tree/v32io) repository, under its `v32io` branch, has implemented emulator-level implementations of these `v32io` peripherals, requiring no physical hardware.

The use  of the provided `v32kbd`  device (under the Gamepad  menu), will
emulate the `v32io`  functionality on your system  keyboard, allowing for
the  full typing  experience  within Vircon32.  This  option is  mutually
exclusive with the existing `keyboard` device.  Only one or the other can
be enabled at any one time (on any gameport).

The use of the provided `v32mouse`  device (under the Gamepad menu), will
emulate the  `v32io` functionality with  your system mouse,  allowing for
mouse  functionality  within  Vircon32.  Note that  when  you  focus  the
Vircon32 DesktopEmulator window, the mouse  pointer WILL be captured, and
you  will need  to issue  `CTRL`+`ALT` (the  left `ALT`  specifically, to
reduce potential conflicts  with keyboards using `AltGr`)  to release the
pointer back to the system.

### Patch the stock DesktopEmulator

On this  repository, under  the `emulator/`  directory, are  two `.patch`
files, to  be used for patching  in `v32kbd` and `v32mouse`  support into
the stock Vircon32 DesktopEmulator.
