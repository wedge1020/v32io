# Changes

## 20261105-dev
- New `emulator/v32dev-server/v32dev-server.patch`: a v32dev host built into the DesktopEmulator (on top of `v32dev.patch`). **Memory card → v32dev server** serves storage files and a terminal (`serial:stdio`, `serial:tcp` on 127.0.0.1, `serial:echo`) on the 4 channels, on any card: no `v32dev.py`, and no formatting (the mailbox header is put in card memory only, if the last 64 KiB of the card are free, and nothing of the mailbox is saved to the card file while the server is on). Also configurable by `V32DEV_SERVER` and `V32DEV_CH0..3`, and saved in the settings file.
- tests/v32dev: `dev_server_unit.cpp` (the server on the real console classes), the real-cartridge scenarios again against the built-in host plus a console cartridge over stdio and TCP, and `gui/` (the patched menu run on the real Dear ImGui); `run.sh` runs them when the server patch is applied.
- Version stamped to 20261105-dev (`make version VERSION=...`).
- New `tools/v32io-encode.py`: reference encoder for the v32kbd and v32mouse protocols (`--frames` prints raw port frames).
- New `tests/vectors/`: six golden vector files (actions, hand-written decoded events, expected frames) checked by an independent decoder; part of `make test`.
- Docs: "at most 5 steps per axis per frame" is stated as part of the v32mouse protocol; status of the v32mouse event queue (not yet implemented; v32sim will emulate it).
- tests/README.md: v32dev test can use v32sim's headless runner.
