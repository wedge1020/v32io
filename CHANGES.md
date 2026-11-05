# Changes

## 20261105-dev
- Version stamped to 20261105-dev (`make version VERSION=...`).
- New `tools/v32io-encode.py`: reference encoder for the v32kbd and v32mouse protocols (`--frames` prints raw port frames).
- New `tests/vectors/`: six golden vector files (actions, hand-written decoded events, expected frames) checked by an independent decoder; part of `make test`.
- Docs: "at most 5 steps per axis per frame" is stated as part of the v32mouse protocol; status of the v32mouse event queue (not yet implemented; v32sim will emulate it).
- tests/README.md: v32dev test can use v32sim's headless runner.
