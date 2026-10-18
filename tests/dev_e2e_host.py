#!/usr/bin/env python3
"""End-to-end test, external host side (a separate process from the emulator
code). Sends M messages per channel, expects each one echoed back
transformed, and also expects M messages the program generates on its own.
Exercises both directions of all 4 channel pairs at once, with ring wrap-around.

usage: dev_e2e_host.py <card.memc> <messages per channel> [seed]
"""
import os
import random
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import v32dev  # noqa: E402

M32 = 0xFFFFFFFF
KEY_BASE = 0x01010101


def gen(c, k, i):
    return ((((c + 1) * 0x9E3779B1 * (k + 1)) & M32) ^ ((i * 2654435761) & M32)) & M32


def stream_payload(c, k):
    return [gen(c, k, i) for i in range(((k * 7 + c * 3) % 60) + 1)]


def main():
    path = sys.argv[1]
    m = int(sys.argv[2])
    rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    chans = v32dev.CHANNELS

    host = v32dev.Host(path)
    for c in range(chans):
        host[c].announce(v32dev.make_type(v32dev.CLASS_CHAR))

    to_send = []
    exp_echo = []
    exp_stream = []
    for c in range(chans):
        msgs, echoes = [], []
        for k in range(m):
            # sizes up to ~250 words, so rings wrap around many times over the run
            payload = [rng.getrandbits(32) for _ in range(rng.randint(1, 250))]
            op = 0x1000 + (k & 0xFF)
            msgs.append((op, payload))
            echoes.append((op | 0x8000, [w ^ ((KEY_BASE * (c + 1)) & M32) for w in payload]))
        to_send.append(msgs)
        exp_echo.append(echoes)
        exp_stream.append([(0x4000 + c, stream_payload(c, k)) for k in range(m)])

    sent = [0] * chans
    got_echo = [0] * chans
    got_stream = [0] * chans
    errors = []
    bye_sent = False
    ack = None
    t0 = time.time()
    loops = 0

    while ack is None:
        loops += 1
        if time.time() - t0 > 170:
            errors.append("timeout")
            break
        if loops % 50 == 0:
            for c in range(chans):
                host[c].beat()

        for c in range(chans):
            # send as many queued messages as currently fit
            while sent[c] < m:
                op, payload = to_send[c][sent[c]]
                if not host[c].send(op, payload):
                    break
                sent[c] += 1

            for op, payload in host[c].recv():
                if 0x4000 <= op < 0x4000 + chans:
                    k = got_stream[c]
                    if k >= m or (op, payload) != exp_stream[c][k] or op != 0x4000 + c:
                        errors.append("stream mismatch ch%d #%d" % (c, k))
                    got_stream[c] += 1
                elif 0x9000 <= op <= 0x90FF:
                    k = got_echo[c]
                    if k >= m or (op, payload) != exp_echo[c][k]:
                        errors.append("echo mismatch ch%d #%d" % (c, k))
                    got_echo[c] += 1
                elif op == 0x2FFE and c == 0:
                    ack = payload
                else:
                    errors.append("unexpected opcode 0x%X on ch%d" % (op, c))

        if not bye_sent and all(s == m for s in sent):
            bye_sent = host[0].send(0x0FFF, [])
        if len(errors) > 20:
            break
        time.sleep(0.0002)

    bad_frames = sum(ch.bad for ch in host.channels)
    host.close()

    print("HOST: %d loops in %.1fs; sent %s; echoes %s; stream %s; bad frames %d" % (
        loops, time.time() - t0, sent, got_echo, got_stream, bad_frames))

    ok = (not errors and bad_frames == 0 and ack == [m] * chans
          and got_echo == [m] * chans and got_stream == [m] * chans)
    if not ok:
        print("HOST: FAILED", errors[:10], "ack=%s" % ack)
        return 1
    print("HOST: all messages verified in both directions on %d channels" % chans)
    return 0


if __name__ == "__main__":
    sys.exit(main())
