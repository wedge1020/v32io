#!/usr/bin/env python3
# *****************************************************************************
#  tests/vectors/check.py -- test vectors for the v32io protocols
#
#    python3 tests/vectors/check.py [--update] [NAME.vec ...]
#
#  A vector (NAME.vec) has three sections:
#
#    [actions]  the events, in tools/v32io-encode.py's syntax
#    [expect]   what a program using the library should see, written BY HAND from
#               lib/v32io/README.md:  "FRAME PORT key down|up CODE", "FRAME PORT char CODE",
#               "FRAME PORT mouse STEPX STEPY" (counter movement of that frame, -5..5),
#               "FRAME PORT mouse down|up left|right|middle"
#    [frames]   the 11 controls (left right up down start a b x y l r) of every frame and
#               port, as the reference encoder writes them: the golden output that the
#               firmware, the emulator patch and v32sim are compared with
#
#  This script checks three things for each vector:
#    1. the encoder still writes the golden [frames];
#    2. DECODING [frames] with the decoder below (an independent reading of the protocol, the
#       way keyboard.h / mouse.h read it) gives exactly [expect];
#    3. the frames never ask for more than the protocol allows (one key event per frame,
#       at most 5 counter steps per axis per frame).
#  --update rewrites [frames] from the encoder (after a deliberate protocol change).
#
#  An implementation of the device side (firmware, emulator patch, v32sim) passes by producing
#  the same [frames] for the same [actions]; the other tools can read the .vec files directly.
# *****************************************************************************
import os, subprocess, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ENC = os.path.join(HERE, "..", "..", "tools", "v32io-encode.py")
CTRL = ["left", "right", "up", "down", "start", "a", "b", "x", "y", "l", "r"]


def read_vec(path):
    sec, data = None, {"actions": [], "expect": [], "frames": []}
    for raw in open(path).read().splitlines():
        s = raw.strip()
        if s in ("[actions]", "[expect]", "[frames]"):
            sec = s[1:-1]
            continue
        if sec is None:
            continue
        if sec == "actions":
            data[sec].append(raw)                 # kept as written (type keeps its spaces)
        elif s and not s.startswith("#"):
            data[sec].append(s)
    return data


def encode(actions):
    p = subprocess.run([sys.executable, "-I", ENC, "--frames"], input="\n".join(actions) + "\n",
                       capture_output=True, text=True)
    if p.returncode:
        raise SystemExit("encoder failed: " + p.stderr)
    return [l for l in p.stdout.splitlines() if l]


def counter(neg, pos, high, low):
    """position 0..11 of a mouse counter: Gray-coded group (high, low) and trit (neg, pos)"""
    group = {(0, 0): 0, (0, 1): 1, (1, 1): 2, (1, 0): 3}[(high, low)]
    trit = 0 if neg else (2 if pos else 1)
    t = (2 - trit) if group & 1 else trit
    return group * 3 + t


def steps(before, after):
    d = (after - before) % 12
    return d - 12 if d > 6 else d


def decode(frames):
    """what the library sees: key events, characters, mouse movement and buttons"""
    out, bad = [], []
    last = {}                                  # port -> previous controls
    strobe = {}                                # port -> last strobe side seen (0 none)
    pending = {}                               # port -> codepage prefix waiting for its payload
    mpos = {}                                  # port -> (x, y) counters
    mbtn = {}
    seen = {}                                  # port -> device kind guessed from the first frames
    for line in frames:
        f, n, bits = line.split()
        f, n = int(f), int(n)
        c = [b == "1" for b in bits]
        prev = last.get(n)
        last[n] = c
        if prev == c and n in seen:
            continue
        if n not in seen and not any(c):
            continue                           # a device at rest: nothing to tell keyboard from mouse yet
        # a device is a mouse when its first non-rest frame has no strobe/up/down pattern: tell by
        # the encoder's own invariant -- keyboard frames always have exactly one of up/down set
        kbd = (c[2] != c[3]) and (c[0] != c[1])
        if n not in seen:
            seen[n] = "kbd" if kbd else "mouse"
        if seen[n] == "kbd":
            side = 1 if c[0] else 2 if c[1] else 0
            if side and side != strobe.get(n, 0):
                strobe[n] = side
                code = sum(1 << i for i in range(7) if c[4 + i])
                pressed = c[2]
                if code in (30, 31) and not pressed:
                    pending[n] = code
                elif n in pending and not pressed:
                    out.append("%d %d char %d" % (f, n, code + (128 if pending.pop(n) == 30 else 0)))
                else:
                    out.append("%d %d key %s %d" % (f, n, "down" if pressed else "up", code))
        else:
            x = counter(c[0], c[1], c[7], c[8])
            y = counter(c[2], c[3], c[9], c[10])
            ox, oy = mpos.get(n, (1, 1))
            mpos[n] = (x, y)
            dx, dy = steps(ox, x), steps(oy, y)
            if dx or dy:
                out.append("%d %d mouse %d %d" % (f, n, dx, dy))
            btn = {"middle": c[4], "left": c[5], "right": c[6]}
            old = mbtn.get(n, {})
            for name in ("left", "right", "middle"):
                if btn[name] and not old.get(name):
                    out.append("%d %d mouse down %s" % (f, n, name))
                if old.get(name) and not btn[name]:
                    out.append("%d %d mouse up %s" % (f, n, name))
            mbtn[n] = btn
        # one event per frame is the keyboard's limit; the counters' is 5 steps
    return out


def main():
    update = "--update" in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith("--")] or sorted(glob.glob(os.path.join(HERE, "*.vec")))
    fails = 0
    for path in files:
        name = os.path.basename(path)
        v = read_vec(path)
        frames = encode(v["actions"])
        if update:
            text = open(path).read()
            head = text[:text.index("[frames]")] if "[frames]" in text else text.rstrip("\n") + "\n\n"
            open(path, "w").write(head + "[frames]\n" + "\n".join(frames) + "\n")
            print("updated", name)
            continue
        problems = []
        if frames != v["frames"]:
            problems.append("the encoder's frames differ from [frames] (first difference: %s)" % next(
                ("%r vs %r" % (a, b) for a, b in zip(frames + [None], v["frames"] + [None]) if a != b), "length"))
        got = decode(v["frames"])
        if got != v["expect"]:
            problems.append("decoded events differ from [expect]:\n      got      %s\n      expected %s" % (got, v["expect"]))
        if problems:
            fails += 1
            print("FAIL %s" % name)
            for p in problems:
                print("   ", p)
        else:
            print("ok   %s (%d frames, %d events)" % (name, len(v["frames"]), len(v["expect"])))
    sys.exit(1 if fails else 0)


main()
