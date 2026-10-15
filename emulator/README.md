# Vircon32 DesktopEmulator patches

Each  component of  this project  has  its own  patch set  for the  stock
Vircon32  DesktopEmulator (the  `ComputerSoftware` repository).  They are
independent; apply the ones you need.

| Directory   | For      | What it adds                                                    | Needed? |
| ----------- | -------- | --------------------------------------------------------------- | ------- |
| `v32io/`    | v32io    | `v32kbd` and `v32mouse` gamepad devices using the host keyboard and mouse | only to use v32io *without* the RP2350-USB-A adapter |
| `v32dev/`   | v32dev   | the memory card mailbox (host file, terminal, ... in and out of the program) | always, for v32dev |

See `v32io/README.md` and  `v32dev/README.md` for how to  apply them, and
`../README.md` for the overview of the project.
