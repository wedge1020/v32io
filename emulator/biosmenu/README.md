# biosmenu: emulator patch for selecting the BIOS from the GUI

Adds a **BIOS** menu to the Vircon32 DesktopEmulator (between *Memory Card* and *Gamepads*),
so a different BIOS can be chosen without editing `Config-Settings.xml`.
It is independent of the v32io and v32dev patches: it applies cleanly to stock
`ComputerSoftware`, to either of them, or to both, in any order.

`biosmenu.patch` is made against the same stock Vircon32 `ComputerSoftware` base as the other two patches.
Apply from the `ComputerSoftware/` folder:

```
patch -p0 --dry-run < biosmenu.patch      # check first
patch -p0 < biosmenu.patch
patch -R -p0 < biosmenu.patch             # to take it out again
```

## What it does

- The **BIOS** menu shows the loaded BIOS, then:
  - **Load BIOS...** (no BIOS loaded) or **Change BIOS...** (one is loaded): file dialog for any `*.v32`.
  - **Available BIOS files:** every `*.v32` in the emulator's `Bios` folder, the loaded one ticked.
    `StandardBios.v32` is always first, whenever it is present in that folder.
- Same session lock as cartridges: while the console is **on** the menu only shows the loaded BIOS and
  *(BIOS is locked)*. Power off to change it. A cartridge may stay inserted.
- A BIOS that fails to load shows an error and the previous BIOS is put back.
- A BIOS picked from the `Bios` folder also becomes the startup BIOS (saved in `Config-Settings.xml`
  as before). One picked from elsewhere with *Change BIOS...* is used for the session only.
- Savestates are tied to a BIOS (existing behaviour): loading a state made with another BIOS is refused.
- English and Spanish texts. The console refuses to power on with no BIOS loaded.

## Files changed

`Emulator/GUI.cpp`, `GUI.hpp`, `Languages.cpp`, `Languages.hpp`, `EmulatorControl.cpp`, and
`DesktopInfrastructure/FilePaths.cpp`, `FilePaths.hpp` (new `ListFilesWithExtension()`, POSIX and
Windows branches). Nothing under `ConsoleLogic/`, and none of the files the other two patches touch
except `GUI.cpp`, in separate places.

`mkpatch.sh <pristine> <patched> > biosmenu.patch` regenerates the patch.
