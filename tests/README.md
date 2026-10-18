# Tests

End-to-end tests, one folder for each component. (`make test` at the top of the
project runs the cheaper checks that need nothing but this tree: the version
stamps, the manual pages and their examples, and the compile checks of the
headers and demos.)

| Folder | Tests | Needs | Run |
| --- | --- | --- | --- |
| [`v32io/`](v32io/README.md) | the firmware logic and the v32io library, on the console of the emulator | the Vircon32 sources, the DevTools, TinyUSB | `make realcheck` (or `sh tests/v32io/run.sh`) |
| `v32dev/` | the v32dev emulator patch, the host tool and the library, on the patched console | the Vircon32 sources with `emulator/v32dev/v32dev.patch` applied (the tools can be built from them) | `make realcheck COMPONENTS=v32dev COMPUTERSOFTWARE=<folder>` (or `bash tests/v32dev/run.sh <folder>`) |

The variables each needs are listed at the top of `v32io/run.sh` and
`v32dev/run.sh`. The v32dev tests are described in
`docs/v32dev/PROTOCOL.md` (section 6).
