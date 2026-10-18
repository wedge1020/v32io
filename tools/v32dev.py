#!/usr/bin/env python3
"""v32dev host tool: the outside half of a v32dev mailbox.

A Vircon32 program can exchange data with the outside world through its
memory card (.memc file) when the emulator has the v32dev patch. This tool
formats a card for that, and serves the devices a program talks to through
the 4 channels of the mailbox (see docs/v32dev/PROTOCOL.md).

    v32dev.py format card.memc --ch0 storage --ch1 serial
    v32dev.py serve  card.memc --ch0 storage:disk.img --ch1 serial:stdio
    v32dev.py info   card.memc

    storage:FILE[,bytes][,ro][,sector=N][,create=SECTORS]
        a block device backed by a file on this computer (see storage.h)
    serial:stdio | serial:echo[,hup=N]
        a character device connected to this terminal (it hangs up at the end of
        its input), or to itself (hanging up after N words)

It only uses plain file reads and writes, so it works on Linux, macOS and
Windows while the (patched) emulator has the card loaded. It can also be
imported: Host, Channel and the device classes are what `serve` uses.
"""
import argparse
import os
import struct
import sys
import threading
import time

# ----------------------------------------------------------------------------
# layout (all values are 32-bit words; must match the emulator patch)
# ----------------------------------------------------------------------------
CARD_WORDS = 256 * 1024
FILE_SIGNATURE = b"V32-MEMC"
FILE_BYTES = 8 + CARD_WORDS * 4            # 1048584

REGION_WORDS = 16 * 1024
REGION_START = CARD_WORDS - REGION_WORDS
HEADER_WORDS = 64
CHANNELS = 4
PART_WORDS = 2040
CONTROL_WORDS = 16
RING_WORDS = PART_WORDS - CONTROL_WORDS    # 2024 slots; usable capacity is one less
HOST_AREA = REGION_START + HEADER_WORDS
CART_AREA = HOST_AREA + CHANNELS * PART_WORDS
MSG_MAX = 2020                             # most payload words in a message

MAGIC = 0x31564544                         # bytes "DEV1"
VERSION = 1
HEADER = [MAGIC, VERSION, REGION_WORDS, CHANNELS, PART_WORDS, CONTROL_WORDS]

# control word indices inside a host-owned part
HP_TYPE, HP_FLAGS, HP_BEAT, HP_H2C_HEAD, HP_C2H_TAIL, HP_SESSION, HP_STATUS = range(7)
HP_DESC = 8                                # 8 descriptor words
# control word indices inside a cart-owned part
CP_FLAGS, CP_C2H_HEAD, CP_H2C_TAIL, CP_BEAT, CP_SESSION, CP_STATUS = range(6)
CP_DESC = 8                                # 8 request words

HOST_FLAG_ATTACHED = 1
ST_READY, ST_ERROR, ST_HUP = 1, 2, 4

# ----------------------------------------------------------------------------
# device classes (in the manner of unix devices) and kinds
# ----------------------------------------------------------------------------
CLASS_NONE, CLASS_BLOCK, CLASS_CHAR, CLASS_PIPE, CLASS_SOCKET = 0, 1, 2, 3, 4


def make_type(cls, kind=0):
    return (cls << 24) | kind


TYPES = {
    "none": make_type(CLASS_NONE),
    "storage": make_type(CLASS_BLOCK, 1),
    "block": make_type(CLASS_BLOCK, 0),
    "serial": make_type(CLASS_CHAR, 1),
    "char": make_type(CLASS_CHAR, 0),
    "pipe": make_type(CLASS_PIPE, 1),
    "tcp": make_type(CLASS_SOCKET, 1),
    "udp": make_type(CLASS_SOCKET, 2),
    "netplay": make_type(CLASS_SOCKET, 3),
    "socket": make_type(CLASS_SOCKET, 0),
}
CLASS_NAMES = {0: "none", 1: "block", 2: "char", 3: "pipe", 4: "socket"}


def describe_type(t):
    cls, kind = t >> 24, t & 0xFFFFFF
    for name, value in TYPES.items():
        if value == t:
            return "%s (%s kind %d)" % (name, CLASS_NAMES.get(cls, "class %d" % cls), kind)
    return "%s kind %d" % (CLASS_NAMES.get(cls, "class %d" % cls), kind)


def host_part(ch):
    return HOST_AREA + ch * PART_WORDS


def cart_part(ch):
    return CART_AREA + ch * PART_WORDS


# ----------------------------------------------------------------------------
# card file access
# ----------------------------------------------------------------------------
class Card:
    def __init__(self, path):
        self.path = path
        self.fd = os.open(path, os.O_RDWR | getattr(os, "O_BINARY", 0))

    def close(self):
        os.close(self.fd)

    def read_words(self, word, count):
        os.lseek(self.fd, 8 + word * 4, os.SEEK_SET)
        data = b""
        while len(data) < count * 4:
            chunk = os.read(self.fd, count * 4 - len(data))
            if not chunk:
                raise IOError("short read from card file")
            data += chunk
        return list(struct.unpack("<%dI" % count, data))

    def read_word(self, word):
        return self.read_words(word, 1)[0]

    def write_words(self, word, values):
        data = struct.pack("<%dI" % len(values), *[v & 0xFFFFFFFF for v in values])
        os.lseek(self.fd, 8 + word * 4, os.SEEK_SET)
        view = memoryview(data)
        while len(view):
            written = os.write(self.fd, view)
            view = view[written:]

    def write_word(self, word, value):
        self.write_words(word, [value])


def format_card(path, types=None, create=True, force=False):
    """Creates (if needed) and formats the mailbox region of a memory card file.
    Everything outside the mailbox region is left untouched.

    Formatting overwrites the last 64 KiB of the card. To protect real save data,
    it refuses to do that when the region already holds data that does not belong
    to a v32dev mailbox, unless force is set. Do not format a card while an
    emulator has it loaded."""
    types = types or {}
    if not os.path.exists(path):
        if not create:
            raise FileNotFoundError(path)
        with open(path, "wb") as f:
            f.write(FILE_SIGNATURE + bytes(CARD_WORDS * 4))
    if os.path.getsize(path) != FILE_BYTES:
        raise ValueError("not a valid memory card file (size is not %d)" % FILE_BYTES)
    with open(path, "rb") as f:
        if f.read(8) != FILE_SIGNATURE:
            raise ValueError("not a valid memory card file (bad signature)")

    card = Card(path)
    try:
        region = card.read_words(REGION_START, REGION_WORDS)
        is_mailbox = region[:6] == HEADER
        if any(region) and not is_mailbox and not force:
            raise ValueError("the last 64 KiB of %s already contain data that is not a v32dev mailbox; "
                             "formatting would destroy it (use --force to do it anyway)" % path)

        # zero the whole region first (header written last, so a half-formatted card isn't valid)
        card.write_words(REGION_START, [0] * REGION_WORDS)
        for ch in range(CHANNELS):
            card.write_word(host_part(ch) + HP_TYPE, types.get(ch, 0))
        card.write_words(REGION_START, HEADER)
    finally:
        card.close()


# ----------------------------------------------------------------------------
# a channel, seen from the host
#
# Messages are [N][opcode][N payload words]. A message is published all at once
# (data first, then the ring index), so the receiver never sees half of one.
# ----------------------------------------------------------------------------
class Channel:
    def __init__(self, card, ch):
        self.card = card
        self.ch = ch
        self.hp = host_part(ch)
        self.cp = cart_part(ch)
        self.pending = []                   # received words not yet made into messages
        self.bad = 0                        # corrupt stream events seen
        self.cart_session = card.read_word(self.cp + CP_SESSION)

    # -- state -----------------------------------------------------------------
    def announce(self, type_word, desc=(), status=ST_READY):
        """Starts serving this channel: empties the rings, declares the device
        (descriptor and status first, the 'attached' flag last)."""
        c = self.card
        c.write_word(self.hp + HP_FLAGS, 0)
        self.reset()
        c.write_word(self.hp + HP_SESSION, (int(time.time() * 1000) ^ (os.getpid() << 8)) & 0x7FFFFFFF or 1)
        c.write_words(self.hp + HP_DESC, list(desc) + [0] * (8 - len(desc)))
        c.write_word(self.hp + HP_STATUS, status)
        c.write_word(self.hp + HP_TYPE, type_word)
        self.beat()
        c.write_word(self.hp + HP_FLAGS, HOST_FLAG_ATTACHED)

    def withdraw(self):
        self.card.write_word(self.hp + HP_FLAGS, 0)
        self.card.write_word(self.hp + HP_STATUS, 0)

    def beat(self):
        self.card.write_word(self.hp + HP_BEAT, (self.card.read_word(self.hp + HP_BEAT) + 1) & 0xFFFFFFFF)

    def set_status(self, status):
        self.card.write_word(self.hp + HP_STATUS, status)

    def reset(self):
        """Empties both rings, moving the end of each that the host owns."""
        c = self.card
        c.write_word(self.hp + HP_H2C_HEAD, c.read_word(self.cp + CP_H2C_TAIL) % RING_WORDS)
        c.write_word(self.hp + HP_C2H_TAIL, c.read_word(self.cp + CP_C2H_HEAD) % RING_WORDS)
        self.pending = []

    def cart_open(self):
        return bool(self.card.read_word(self.cp + CP_FLAGS) & 1)

    def check_cart(self):
        """True if the program opened the channel again since the last check (it
        restarted, or came back after losing the host). Opening empties the rings
        on the program's side by itself (each side moves the end it owns), and the
        program may already have sent its first request, so the rings are NOT touched
        here: only the words of an unfinished message from the old session are dropped."""
        session = self.card.read_word(self.cp + CP_SESSION)
        if session != self.cart_session:
            self.cart_session = session
            self.pending = []
            return True
        return False

    # -- host to cart ring: host produces -------------------------------------
    def free_words(self):
        head = self.card.read_word(self.hp + HP_H2C_HEAD)
        tail = self.card.read_word(self.cp + CP_H2C_TAIL)
        if head >= RING_WORDS or tail >= RING_WORDS:
            raise ValueError("channel %d has corrupt ring indices" % self.ch)
        return (tail - head - 1) % RING_WORDS

    def write(self, words):
        """Appends words to the host to cart ring. All or nothing: returns False if full."""
        if len(words) > self.free_words():
            return False
        if not words:
            return True
        head = self.card.read_word(self.hp + HP_H2C_HEAD)
        base = self.hp + CONTROL_WORDS
        first = min(len(words), RING_WORDS - head)
        # 1) data (possibly in two pieces when wrapping), 2) only then the head index
        self.card.write_words(base + head, words[:first])
        if first < len(words):
            self.card.write_words(base, words[first:])
        self.card.write_word(self.hp + HP_H2C_HEAD, (head + len(words)) % RING_WORDS)
        return True

    def send(self, opcode, payload=()):
        if len(payload) > MSG_MAX:
            raise ValueError("message too long")
        return self.write([len(payload), opcode & 0xFFFFFFFF] + [w & 0xFFFFFFFF for w in payload])

    # -- cart to host ring: host consumes -------------------------------------
    def read(self):
        """Takes every word currently available in the cart to host ring."""
        # the head index is read BEFORE the data it announces
        head = self.card.read_word(self.cp + CP_C2H_HEAD)
        tail = self.card.read_word(self.hp + HP_C2H_TAIL)
        if head >= RING_WORDS or tail >= RING_WORDS:
            raise ValueError("channel %d has corrupt ring indices" % self.ch)
        count = (head - tail) % RING_WORDS
        if count == 0:
            return []
        base = self.cp + CONTROL_WORDS
        first = min(count, RING_WORDS - tail)
        words = self.card.read_words(base + tail, first)
        if first < count:
            words += self.card.read_words(base, count - first)
        self.card.write_word(self.hp + HP_C2H_TAIL, head)
        return words

    def recv(self):
        """Returns the complete messages received so far as (opcode, payload) tuples."""
        self.pending.extend(self.read())
        out = []
        while len(self.pending) >= 2:
            n = self.pending[0]
            if n > MSG_MAX:                          # impossible length: stream is corrupt
                self.bad += 1
                self.pending.pop(0)
                continue
            if len(self.pending) < n + 2:
                break
            out.append((self.pending[1], self.pending[2:n + 2]))
            del self.pending[:n + 2]
        return out


class Host:
    """The 4 channels of one card."""

    def __init__(self, path):
        self.card = Card(path)
        if self.card.read_words(REGION_START, 6) != HEADER:
            self.card.close()
            raise ValueError("card is not formatted as a v32dev mailbox (run: v32dev.py format)")
        self.channels = [Channel(self.card, ch) for ch in range(CHANNELS)]

    def __getitem__(self, ch):
        return self.channels[ch]

    def close(self):
        self.card.close()


# ----------------------------------------------------------------------------
# devices: what is at the host end of a channel
# ----------------------------------------------------------------------------
class Device:
    type_word = TYPES["none"]

    def start(self, channel):
        """Called once: declares the device on the channel."""
        self.channel = channel
        channel.announce(self.type_word, self.descriptor())

    def descriptor(self):
        return []

    def poll(self):
        """Called often: does whatever work is available. Returns True if it did some."""
        return False

    def stop(self):
        pass


# -- storage (block class) ----------------------------------------------------
STORAGE_OP_READ, STORAGE_OP_WRITE, STORAGE_OP_FLUSH = 1, 2, 3
STORAGE_REPLY = 128
S_OK, S_EINVAL, S_ERANGE, S_EROFS, S_EIO = 0, 1, 2, 3, 4
STORAGE_FLAG_READONLY, STORAGE_FLAG_BYTES = 1, 2


class StorageDevice(Device):
    """A file on this computer, served as a block device.

    Word mode (default): every word is stored as 4 bytes, little endian, and a
    sector is 128 words (512 bytes). Byte mode: every word is one byte (the
    program gets 0..255, and only the low 8 bits of what it writes are kept), and
    a sector is 512 words (512 bytes). Byte mode serves ordinary files, such as
    text files or FAT images, as they are. A trailing part of the file that does
    not fill a sector is not accessible."""
    type_word = TYPES["storage"]

    def __init__(self, path, bytes_mode=False, readonly=False, sector_words=None, create_sectors=None):
        self.path = path
        self.bytes_mode = bytes_mode
        self.readonly = readonly
        self.sector_words = sector_words or (512 if bytes_mode else 128)
        self.word_size = 1 if bytes_mode else 4
        self.sector_bytes = self.sector_words * self.word_size
        if create_sectors is not None and not os.path.exists(path):
            with open(path, "wb") as f:
                f.truncate(create_sectors * self.sector_bytes)
        self.file = open(path, "rb" if readonly else "r+b")
        self.file.seek(0, os.SEEK_END)
        self.sectors = self.file.tell() // self.sector_bytes
        self.max_sectors = (MSG_MAX - 4) // self.sector_words
        if self.max_sectors < 1:
            raise ValueError("sector size is too big (at most %d words)" % (MSG_MAX - 4))
        self.outbox = []                     # replies the ring had no room for
        self.stats = {"read": 0, "write": 0, "flush": 0, "bad": 0}

    def descriptor(self):
        flags = (STORAGE_FLAG_READONLY if self.readonly else 0) | (STORAGE_FLAG_BYTES if self.bytes_mode else 0)
        return [self.sector_words, self.sectors, flags, self.max_sectors]

    def stop(self):
        self.file.close()

    # words <-> file bytes
    def _to_words(self, data):
        if self.bytes_mode:
            return list(data)
        return list(struct.unpack("<%dI" % (len(data) // 4), data))

    def _to_bytes(self, words):
        if self.bytes_mode:
            return bytes(w & 0xFF for w in words)
        return struct.pack("<%dI" % len(words), *[w & 0xFFFFFFFF for w in words])

    def handle(self, opcode, payload):
        """Returns (reply opcode, reply payload) for a request."""
        reply = STORAGE_REPLY + opcode
        if not payload:
            return reply, [0, S_EINVAL]
        tag = payload[0]
        if opcode == STORAGE_OP_FLUSH:
            self.stats["flush"] += 1
            try:
                self.file.flush()
                os.fsync(self.file.fileno())
            except OSError:
                return reply, [tag, S_EIO]
            return reply, [tag, S_OK]
        if opcode not in (STORAGE_OP_READ, STORAGE_OP_WRITE) or len(payload) < 3:
            self.stats["bad"] += 1
            return reply, [tag, S_EINVAL]
        sector, count = payload[1], payload[2]
        if count < 1 or count > self.max_sectors:
            return reply, [tag, S_EINVAL]
        if sector + count > self.sectors:
            return reply, [tag, S_ERANGE]
        try:
            self.file.seek(sector * self.sector_bytes)
            if opcode == STORAGE_OP_READ:
                if len(payload) != 3:
                    return reply, [tag, S_EINVAL]
                data = self.file.read(count * self.sector_bytes)
                if len(data) != count * self.sector_bytes:
                    return reply, [tag, S_EIO]
                self.stats["read"] += count
                return reply, [tag, S_OK] + self._to_words(data)
            if self.readonly:
                return reply, [tag, S_EROFS]
            if len(payload) != 3 + count * self.sector_words:
                return reply, [tag, S_EINVAL]
            self.file.write(self._to_bytes(payload[3:]))
            self.file.flush()
            self.stats["write"] += count
            return reply, [tag, S_OK]
        except OSError:
            return reply, [tag, S_EIO]

    def poll(self):
        did = False
        if self.channel.check_cart():
            self.outbox = []
            did = True
        while self.outbox:
            if not self.channel.send(*self.outbox[0]):
                return did
            self.outbox.pop(0)
            did = True
        for opcode, payload in self.channel.recv():
            self.outbox.append(self.handle(opcode, payload))
            did = True
        while self.outbox:
            if not self.channel.send(*self.outbox[0]):
                break
            self.outbox.pop(0)
        return did


# -- serial (char class) ------------------------------------------------------
class SerialDevice(Device):
    """A stream of words, in both directions (like a terminal). What the program
    writes goes to `sink` (a function taking a list of words); what is passed
    to feed() reaches the program, in order. With echo set, the program's own
    output is sent back to it. The host hangs up (status HUP: no more data once
    what was sent has been read) when hangup() is called and everything fed has
    been delivered, or, with hup_after=N, after N words have been received from
    the program and echoed back."""
    type_word = TYPES["serial"]

    def __init__(self, sink=None, echo=False, hup_after=None):
        self.sink = sink
        self.echo = echo
        self.hup_after = hup_after
        self.received = 0
        self.hung_up = False
        self.hup_requested = False
        self.inbox = []
        self.lock = threading.Lock()

    def feed(self, words):
        with self.lock:
            self.inbox.extend(words)

    def hangup(self):
        self.hup_requested = True

    def poll(self):
        did = False
        if self.channel.check_cart():
            with self.lock:
                self.inbox = []
            self.received = 0
            if self.hung_up:                 # the program came back: a new conversation
                self.hung_up = False
                self.channel.set_status(ST_READY)
            did = True
        words = self.channel.read()
        if words:
            did = True
            self.received += len(words)
            if self.sink:
                self.sink(words)
            if self.echo:
                self.feed(words)
        with self.lock:
            if self.inbox:
                room = self.channel.free_words()
                chunk = self.inbox[:room]
                if chunk and self.channel.write(chunk):
                    del self.inbox[:len(chunk)]
                    did = True
            drained = not self.inbox
        if (not self.hung_up and drained
                and (self.hup_requested or (self.hup_after is not None and self.received >= self.hup_after))):
            self.hung_up = True
            self.channel.set_status(ST_READY | ST_HUP)
            did = True
        return did


def stdio_serial():
    """A serial device connected to this terminal: program output is printed (low
    8 bits of each word), and lines typed here are sent to the program."""
    dev = SerialDevice(sink=lambda words: (sys.stdout.write("".join(chr(w & 0xFF) for w in words)), sys.stdout.flush()))

    def reader():
        while True:
            line = sys.stdin.readline()
            if not line:
                dev.hangup()                 # end of input
                break
            dev.feed([ord(c) for c in line])

    threading.Thread(target=reader, daemon=True).start()
    return dev


# ----------------------------------------------------------------------------
# serving
# ----------------------------------------------------------------------------
def serve(host, devices, interval=0.004, stop=None, log=None):
    """Runs devices ({channel number: Device}) until stop() returns True.
    The heartbeat is refreshed about every 10 ms: the program takes a host as
    gone when it has not changed for 2 seconds (120 frames)."""
    for ch, dev in devices.items():
        dev.start(host[ch])
        if log:
            log("channel %d: %s" % (ch, describe_type(dev.type_word)))
    last_beat = 0.0
    try:
        while not (stop and stop()):
            now = time.time()
            if now - last_beat >= 0.01:
                for ch in devices:
                    host[ch].beat()
                last_beat = now
            busy = False
            for dev in devices.values():
                busy = dev.poll() or busy
            if not busy:
                time.sleep(interval)
    finally:
        for ch, dev in devices.items():
            host[ch].withdraw()
            dev.stop()


def parse_device(spec):
    kind, _, rest = spec.partition(":")
    kind = kind.lower()
    opts = [o for o in rest.split(",")] if rest else []
    if kind == "storage":
        path, flags = opts[0], opts[1:]
        kw = {}
        for f in flags:
            if f == "bytes":
                kw["bytes_mode"] = True
            elif f == "ro":
                kw["readonly"] = True
            elif f.startswith("sector="):
                kw["sector_words"] = int(f[7:], 0)
            elif f.startswith("create="):
                kw["create_sectors"] = int(f[7:], 0)
            else:
                raise ValueError("unknown storage option: " + f)
        return StorageDevice(path, **kw)
    if kind == "serial":
        if opts == ["stdio"]:
            return stdio_serial()
        if opts and opts[0] == "echo":
            hup = None
            for f in opts[1:]:
                if f.startswith("hup="):
                    hup = int(f[4:], 0)
                else:
                    raise ValueError("unknown serial option: " + f)
            return SerialDevice(echo=True, hup_after=hup)
        raise ValueError("serial needs stdio or echo[,hup=N]")
    raise ValueError("unknown device: " + spec)


# ----------------------------------------------------------------------------
# command line
# ----------------------------------------------------------------------------
def pack_text(text):
    data = text.encode("utf-8")
    data += bytes((-len(data)) % 4)
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("format", help="create/format a card as a v32dev mailbox")
    p.add_argument("card")
    p.add_argument("--force", action="store_true", help="format even if the last 64 KiB hold other data")
    for ch in range(CHANNELS):
        p.add_argument("--ch%d" % ch, default="none", help="device type: %s or a number" % ", ".join(TYPES))

    p = sub.add_parser("serve", help="serve devices on the channels until interrupted")
    p.add_argument("card")
    for ch in range(CHANNELS):
        p.add_argument("--ch%d" % ch, help="device to serve (see above)")

    p = sub.add_parser("info", help="show the mailbox state")
    p.add_argument("card")

    args = ap.parse_args(argv)

    if args.cmd == "format":
        types = {}
        for ch in range(CHANNELS):
            value = getattr(args, "ch%d" % ch)
            try:
                types[ch] = TYPES[value] if value in TYPES else int(value, 0)
            except ValueError:
                print("error: --ch%d: unknown device type %r" % (ch, value), file=sys.stderr)
                return 1
            if not 0 <= types[ch] <= 0xFFFFFFFF:
                print("error: --ch%d: type does not fit in a word" % ch, file=sys.stderr)
                return 1
        try:
            format_card(args.card, types, force=args.force)
        except (ValueError, OSError) as e:
            print("error: %s" % e, file=sys.stderr)
            return 1
        print("formatted %s (%s)" % (args.card, ", ".join("channel %d: %s" % (ch, describe_type(t))
                                                           for ch, t in sorted(types.items()) if t)))
        return 0

    try:
        host = Host(args.card)
    except (ValueError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    try:
        if args.cmd == "info":
            for ch in range(CHANNELS):
                hp, cp = host_part(ch), cart_part(ch)
                r = host.card.read_word
                print("channel %d: %s, host %s, program %s, session %d/%d, h2c(head=%d tail=%d) c2h(head=%d tail=%d)" % (
                    ch, describe_type(r(hp + HP_TYPE)),
                    "attached" if r(hp + HP_FLAGS) & 1 else "absent",
                    "open" if r(cp + CP_FLAGS) & 1 else "closed",
                    r(hp + HP_SESSION), r(cp + CP_SESSION),
                    r(hp + HP_H2C_HEAD), r(cp + CP_H2C_TAIL), r(cp + CP_C2H_HEAD), r(hp + HP_C2H_TAIL)))
            return 0

        if args.cmd == "serve":
            devices = {}
            for ch in range(CHANNELS):
                spec = getattr(args, "ch%d" % ch)
                if spec:
                    try:
                        devices[ch] = parse_device(spec)
                    except (ValueError, OSError, IndexError) as e:
                        print("error: channel %d: %s" % (ch, e), file=sys.stderr)
                        return 1
            if not devices:
                print("error: nothing to serve (use --ch0 ... --ch3)", file=sys.stderr)
                return 1
            print("serving; press Ctrl+C to stop", file=sys.stderr)
            try:
                serve(host, devices, log=lambda s: print(s, file=sys.stderr))
            except KeyboardInterrupt:
                pass
            return 0
    finally:
        host.close()


if __name__ == "__main__":
    sys.exit(main())
