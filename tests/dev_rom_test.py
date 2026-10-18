#!/usr/bin/env python3
"""Real-cartridge tests: a REAL compiled Vircon32 program (tests/rom/*.c, built by
the real Vircon32 compiler against the headers in lib/) runs in the patched console
code (dev_rom_runner, which uses the console's own RunNextFrame), while the host
side is a separate process: `v32dev.py serve`, the tool users would run.

The program tells what it observed through a report it writes in the card (words
65 and following); this test reads it and compares with what the host knows. The
host also checks the contents of the images the program wrote.

usage: dev_rom_test.py <runner> <bios.v32> <directory with the compiled *_test.v32 carts> <workdir> [--realtime]
"""
import os
import signal
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import v32dev  # noqa: E402

TOOL = os.path.join(HERE, "..", "tools", "v32dev.py")
M32 = 0xFFFFFFFF

checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("  FAIL: %s" % what)


def pattern(i):
    return ((i << 17) ^ (i * 3) ^ 0x80000001) & M32


# status codes of storage.h
OK, EINVAL, ERANGE, EROFS, EIO, ETIMEOUT, ENODEV, EBUSY = 0, 1, 2, 3, 4, 5, 6, 7
# channel states of v32dev.h
NOCARD, NOEMU, NOHOST, WRONG, READY = range(5)


class Env:
    def __init__(self, runner, bios, cartdir, work):
        self.runner, self.bios, self.cartdir, self.work = runner, bios, cartdir, work
        self.cart = os.path.join(cartdir, "storage_test.v32")
        os.makedirs(work, exist_ok=True)

    def path(self, name):
        return os.path.join(self.work, name)

    def make_card(self, scenario, types=None, format=True):
        card = self.path("card.memc")
        if os.path.exists(card):
            os.remove(card)
        if format:
            v32dev.format_card(card, types or {})
        else:
            v32dev.format_card(card, {})
            # an unformatted card: clear the header again
            c = v32dev.Card(card)
            c.write_words(v32dev.REGION_START, [0] * 6)
            c.close()
        c = v32dev.Card(card)
        c.write_word(64, scenario)
        c.close()
        return card

    def report(self, card):
        c = v32dev.Card(card)
        r = c.read_words(65, 48)
        c.close()
        return r

    def serve(self, card, *specs):
        args = [sys.executable, "-I", TOOL, "serve", card]
        for ch, spec in specs:
            args += ["--ch%d" % ch, spec]
        log = open(self.path("serve-%d.log" % int(time.time() * 1000)), "w")
        proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
        # wait for every channel to be declared (the 'attached' flag is written last)
        deadline = time.time() + 10
        c = v32dev.Card(card)
        try:
            while time.time() < deadline:
                if all(c.read_word(v32dev.host_part(ch) + v32dev.HP_FLAGS) & 1 for ch, _ in specs):
                    return proc
                time.sleep(0.02)
        finally:
            c.close()
        proc.kill()
        raise RuntimeError("host did not start")

    def stop(self, proc):
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()

    def run(self, card, micros=2000, frames=20000, env=None, timeout=120, cart="storage_test"):
        e = dict(os.environ)
        e.update(env or {})
        p = subprocess.run([self.runner, self.bios, os.path.join(self.cartdir, cart + ".v32"), card, str(micros), str(frames)],
                           capture_output=True, text=True, env=e, timeout=timeout)
        return p.returncode, p.stdout.strip()

    def run_bg(self, card, micros=2000, frames=20000):
        return subprocess.Popen([self.runner, self.bios, self.cart, card, str(micros), str(frames)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def make_word_image(path, sectors, sector_words=128):
    with open(path, "wb") as f:
        f.write(struct.pack("<%dI" % (sectors * sector_words), *[pattern(i) for i in range(sectors * sector_words)]))


def read_word_image(path):
    data = open(path, "rb").read()
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


# ----------------------------------------------------------------------------
def test_rw(env, micros):
    print("scenario 1: read and write a word mode image (%d us per frame)" % micros)
    img = env.path("disk.img")
    make_word_image(img, 64)
    card = env.make_card(1, {1: v32dev.TYPES["storage"]})
    host = env.serve(card, (1, "storage:%s" % img))
    code, out = env.run(card, micros=micros)
    env.stop(host)
    print("  runner:", out)
    check(code == 0, "program ended normally")
    r = env.report(card)

    check(r[0] == 0x54504552 and r[2] == 1 and r[1] == 99, "report complete")
    check(r[40] == READY and r[41] == 1, "channel became ready (state %d)" % r[40])
    check(r[42] == v32dev.CLASS_BLOCK and r[43] == 1, "class block, kind storage")
    check(r[3:8] == [128, 64, 0, 0, 15], "geometry seen by the program: %s" % r[3:8])
    check(r[8] == OK and r[9] == 0, "whole sector read matches host data (%s, %d bad)" % (r[8], r[9]))
    check(r[10] == 0, "unaligned word read of 1000 words matches (%d bad)" % r[10])
    check(1 <= r[11] <= 200, "that took %d frames" % r[11])
    check(r[12] == OK and r[13] == OK, "word write + flush ok (%d, %d)" % (r[12], r[13]))
    check(r[14] == OK and r[15] == 0, "read back: written words present, neighbours intact (%d bad)" % r[15])
    check(r[16] == OK and r[17] == OK and r[18] == OK and r[19] == 0,
          "5 sector write, flush, read back (%s %s %s, %d bad)" % (r[16], r[17], r[18], r[19]))
    check(r[20] == ERANGE and r[21] == ERANGE and r[22] == ERANGE and r[23] == OK,
          "range errors (and zero sectors is a no-op): %s" % r[20:24])
    check(r[24] == OK and r[25] == EBUSY and r[26] == OK and r[28] == 0, "async request: %s" % r[24:29])
    check(1 <= r[27] <= 100, "a one sector request took %d frames" % r[27])
    check(r[29] == 0, "last words of the device readable")

    # what the program wrote is really in the file on the host
    words = read_word_image(img)
    expect = [pattern(i) for i in range(64 * 128)]
    for i in range(700):
        expect[2000 + i] = 0xC0DE0000 + i
    for i in range(5 * 128):
        expect[40 * 128 + i] = (~i) & M32
    check(len(words) == len(expect), "image size unchanged")
    check(words == expect, "image on the host has exactly the program's writes")
    return r


def test_bytes(env):
    print("scenario 2: byte mode, read only image")
    img = env.path("text.img")
    text = b"Hello from the host!\nline two\n"
    with open(img, "wb") as f:
        f.write(text + bytes((i * 7) & 255 for i in range(16 * 512 - len(text))))
    before = open(img, "rb").read()
    card = env.make_card(2, {1: v32dev.TYPES["storage"]})
    host = env.serve(card, (1, "storage:%s,bytes,ro" % img))
    code, out = env.run(card)
    env.stop(host)
    check(code == 0, "program ended normally: " + out)
    r = env.report(card)
    check(r[3:8] == [512, 16, 1, 1, 3], "geometry: %s" % r[3:8])
    check(r[8] == OK and r[16:48] == list(before[:32]), "first 32 bytes arrive as words: %s" % r[16:24])
    check(r[9] == EROFS and r[10] == EROFS and r[11] == EROFS, "writes refused: %s" % r[9:12])
    check(open(img, "rb").read() == before, "read only image untouched")


def test_states(env):
    print("scenario 3-6: what the program sees when there is no usable host")
    # no host serving
    card = env.make_card(3, {1: v32dev.TYPES["storage"]})
    code, out = env.run(card, frames=3000)
    r = env.report(card)
    check(code == 0 and r[1] == 99 and r[40] == NOHOST and r[41] == 0, "no host: state %d (NOHOST)" % r[40])

    # card without a mailbox (an ordinary card)
    card = env.make_card(4, format=False)
    code, out = env.run(card, frames=3000)
    r = env.report(card)
    check(code == 0 and r[40] == NOEMU, "plain card: state %d (NOEMU)" % r[40])

    # formatted card, but the emulator feature is disabled (an emulator without the patch behaves the same)
    img = env.path("disk.img")
    make_word_image(img, 4)
    card = env.make_card(5, {1: v32dev.TYPES["storage"]})
    host = env.serve(card, (1, "storage:%s" % img))
    code, out = env.run(card, frames=3000, env={"V32DEV_DISABLE": "1"})
    env.stop(host)
    r = env.report(card)
    check(code == 0 and r[40] == NOEMU, "mailbox disabled: state %d (NOEMU)" % r[40])

    # a device of another class on the channel
    card = env.make_card(6, {1: v32dev.TYPES["serial"]})
    host = env.serve(card, (1, "serial:echo"))
    code, out = env.run(card, frames=3000)
    env.stop(host)
    r = env.report(card)
    check(code == 0 and r[40] == WRONG and r[41] == 0, "serial device where a block device was expected: state %d (WRONG)" % r[40])


def test_survive(env):
    print("scenario 7: the host crashes, then comes back")
    img = env.path("disk.img")
    make_word_image(img, 4)
    card = env.make_card(7, {1: v32dev.TYPES["storage"]})
    host = env.serve(card, (1, "storage:%s" % img))
    prog = env.run_bg(card, micros=2000, frames=12000)
    time.sleep(1.0)
    host.kill()                              # no cleanup: the host just disappears
    host.wait()
    time.sleep(1.2)                          # long enough for the program to notice (120 frames)
    check(prog.poll() is None, "program is still running while the host is gone")
    host = env.serve(card, (1, "storage:%s" % img))
    try:
        out, _ = prog.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        prog.kill()
        out = "timeout"
    env.stop(host)
    r = env.report(card)
    check(prog.returncode == 0, "program ended: " + out.strip())
    good, fails, first, after, rounds = r[3:8]
    print("  program saw: %d good reads, %d failures (first: status %d), %d good reads after the host returned, %d rounds" % (
        good, fails, first, after, rounds))
    check(good > 10, "reads worked before the crash (%d)" % good)
    check(fails >= 1 and first in (ETIMEOUT, ENODEV, 1000), "the program saw the failure (%d failures, first %d)" % (fails, first))
    check(after >= 20, "reads worked again after the host came back (%d)" % after)


def test_serial(env):
    print("scenario 8: serial (character device): 5000 words each way, in a stream")
    card = env.make_card(8, {2: v32dev.TYPES["serial"]})
    host = env.serve(card, (2, "serial:echo,hup=5006"))
    code, out = env.run(card, cart="serial_test", frames=20000)
    env.stop(host)
    print("  runner:", out)
    check(code == 0, "program ended normally")
    r = env.report(card)
    check(r[1] == 99 and r[2] == 1 and r[3] == READY, "channel ready (state %d)" % r[3])
    check(r[4] == v32dev.CLASS_CHAR and r[5] == 1, "class char, kind serial")
    check(r[6] == 5000 and r[7] == 5000, "sent %d and got back %d words" % (r[6], r[7]))
    check(r[8] == 0, "every word came back in order (%d bad)" % r[8])
    check(r[9] >= 3, "it took %d frames (the ring holds 2023 words)" % r[9])
    check(r[10] == 5 and r[11] == 6 and r[16:22] == [104, 101, 108, 108, 111, 90],
          "text and single word echoed: %s" % r[16:22])
    check(r[12] == 1 and r[13] == 0, "host hung up once everything had been echoed (hup=%d, read %d)" % (r[12], r[13]))


def test_demo(env):
    print("scenario 9: the storage demo (lib/demos/storage.c) browsing a host text file")
    img = env.path("notes.txt")
    with open(img, "wb") as f:
        f.write(b"Hello from the host!\n" * 200)
    card = env.make_card(9, {0: v32dev.TYPES["storage"]})
    host = env.serve(card, (0, "storage:%s,bytes" % img))
    p = subprocess.run([env.runner, env.bios, os.path.join(env.cartdir, "..", "demos", "storage.v32"), card, "2000", "300"],
                       capture_output=True, text=True)
    env.stop(host)
    # it never ends by itself: reaching the frame limit (exit 3) means it ran without a hardware error
    check(p.returncode == 3, "demo ran for 300 frames without a hardware error: " + p.stdout.strip())
    c = v32dev.Card(card)
    check(c.read_word(v32dev.cart_part(0) + v32dev.CP_FLAGS) & 1 == 1, "demo opened the channel")
    c.close()


def main():
    runner, bios, cart, work = sys.argv[1:5]
    env = Env(runner, bios, cart, work)
    test_rw(env, 2000)
    test_bytes(env)
    test_states(env)
    test_survive(env)
    test_serial(env)
    test_demo(env)
    if "--realtime" in sys.argv:
        test_rw(env, 16667)
    print("real cartridge tests: %d checks, %d failures" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
