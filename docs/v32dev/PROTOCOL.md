# v32dev protocol (version 1)

v32dev lets a running Vircon32 program exchange data with the outside world
(files, a terminal, eventually a network) through its **memory card**. The card
stays a normal 1 MiB `.memc` file. A patched DesktopEmulator keeps the last
64 KiB of it synchronized with the file on disk while the program runs, and an
external **host** program reads and writes that file. The program only reads and
writes card memory: no new I/O ports, no new hardware.

It is separate from v32io (which carries keyboard and mouse as gamepad
buttons). v32io works on real hardware and needs no emulator change; v32dev
needs the emulator patch.

## 1. Layers

| Layer | Where | What it does |
|---|---|---|
| Transport | emulator patch (`emulator/v32dev/v32dev.patch`) | Keeps the mailbox region of the card in sync with the file, in an order that is safe to read. Moves words; knows nothing else. |
| Device core | `lib/v32dev/v32dev.h`, `tools/v32dev.py` | Channels, device types (class and kind), opening, liveness, streams and messages. |
| Device drivers | `lib/v32dev/storage.h`, `lib/v32dev/serial.h`, later `network.h`, `netplay.h` | What a class of device means (sectors, characters, connections). |

A program includes only the drivers it needs; each includes `v32dev.h`, the same
way `keyboard.h` and `mouse.h` include `v32io.h`.

## 2. Transport layer (the emulator patch)

- The mailbox is **active** when the card file's header (section 2.3) is valid when
  the card is loaded. A card without it behaves as a regular card. Setting the
  environment variable `V32DEV_DISABLE` to anything other than `0` turns the
  feature off.
- **Every frame, before the program runs**, the emulator reads the whole
  *host-owned area* from the file and replaces its copy in card memory. So the
  program only sees host changes at a frame boundary, never in the middle of a
  frame.
- **Every frame, after the program ran**, if the program wrote to the card, the
  emulator writes the modified parts to the file (in 256-word pages), in two phases:
  first all data, then the *commit words* (the 16 control words) of the touched
  cart-owned parts. It never writes the header or the host-owned area. The file is
  flushed after each phase.
- Latency: a request and its reply take at least one frame (16.7 ms at 60 fps),
  when the host answers before the next frame begins.
- The emulator only moves words. Everything else is up to the program and the host.

### 2.1 Layout

All sizes are in 32-bit words. Card word *N* is at memory address `0x30000000 + N`
and at file byte `8 + 4*N` (the file starts with the 8-byte signature `V32-MEMC`).
The region is the last 16384 words of the card.

| Part | First word | Memory address | Words |
|---|---|---|---|
| Header | 245760 (0x3C000) | `0x3003C000` | 64 |
| Host-owned area | 245824 (0x3C040) | `0x3003C040` | 8160 = 4 x 2040 |
| Cart-owned area | 253984 (0x3E020) | `0x3003E020` | 8160 = 4 x 2040 |

There are **4 channels**. Each area holds one **part** per channel, 2040 words, in
channel order: channel *c* has its host part at `0x3003C040 + c*0x7F8` and its
cart part at `0x3003E020 + c*0x7F8`. A part is 16 control words followed by a
2024-word ring. A channel is therefore a pair of rings: host to cart (in the host
part) and cart to host (in the cart part).

### 2.2 Rings

- Indices are **slot numbers in 0..2023**, not free-running counters.
- Empty: `head == tail`. Used: `(head - tail) mod 2024`. Free: `(tail - head - 1) mod
  2024`, so the usable capacity is **2023 words**. The producer owns the head and the
  data; the consumer owns the tail.
- Producer: write the data, then the new head. Consumer: read the head, then the
  data, then write the new tail.
- The host uses separate file writes (data first, then the index) and separate
  reads (index first, then data); the emulator's order (above) guarantees that a
  consumer that sees a new head also sees its data.

### 2.3 Header (static after formatting)

| Word | Name | Value |
|---|---|---|
| 0 | magic | `0x31564544` (bytes `DEV1`) |
| 1 | version | 1 |
| 2 | region words | 16384 |
| 3 | channels | 4 |
| 4 | part words | 2040 |
| 5 | control words | 16 |
| 8 | emulator flags | **emulator-owned, memory only.** Bit 0 = a patched emulator keeps this mailbox live |
| 9 | emulator heartbeat | **emulator-owned, memory only.** Incremented once per frame |

Words 8 and 9 exist only in the emulator's copy of the card; they never reach the
file.

### 2.4 Control words

Host part (the host writes, the program reads):

| Word | Name | Meaning |
|---|---|---|
| 0 | type | class and kind of the device (section 3.1); 0 = unused |
| 1 | host flags | bit 0 = a host is attached |
| 2 | host heartbeat | the host increments it often, so the program can tell it is alive |
| 3 | `h2c_head` | write index of the host to cart ring |
| 4 | `c2h_tail` | read index of the cart to host ring |
| 5 | host session | changes each time the host starts serving the channel |
| 6 | host status | bit 0 ready, bit 1 error, bit 2 hang up (section 3.4) |
| 7 | reserved | |
| 8-15 | descriptor | 8 words describing the device; meaning depends on the class |
| 16-2039 | ring | host to cart data, 2024 slots |

Cart part (the program writes, the host reads); words 0-15 are the commit words:

| Word | Name | Meaning |
|---|---|---|
| 0 | cart flags | bit 0 = the program has opened this channel |
| 1 | `c2h_head` | write index of the cart to host ring |
| 2 | `h2c_tail` | read index of the host to cart ring |
| 3 | cart heartbeat | optional, program-defined |
| 4 | cart session | +1 each time the program opens the channel |
| 5 | cart status | bit 0 ready, bit 1 error, bit 2 hang up |
| 6-7 | reserved | |
| 8-15 | request words | 8 words the program sets for the device; meaning depends on the class |
| 16-2039 | ring | cart to host data, 2024 slots |

## 3. Device layer

### 3.1 Device types: class and kind

The host announces what is at its end of a channel as one **type** word:
`(class << 24) | kind`, in the manner of unix devices.

| Class | Value | Like | Kinds | Driver |
|---|---|---|---|---|
| none | 0 | unused | | |
| block | 1 | a disk: fixed-size sectors, random access | 1 storage | `storage.h` |
| char | 2 | a terminal or serial port: a stream of words | 1 serial | `serial.h` |
| pipe | 3 | a one-way stream with end of file | 1 stream | reserved |
| socket | 4 | connections and datagrams | 1 tcp, 2 udp, 3 netplay | planned: `network.h`, `netplay.h` |
| custom | 128-255 | anything | | |

Kind 0 is a generic device of the class. A program says which class it expects
(`v32dev_init (channel, class)`), and the channel is reported as `WRONG` if the host
serves another one. The channel assignment (which device is on which channel) is
decided by whoever formats the card and starts the host: the library does not
fix it.

### 3.2 Channel states (program side)

`v32dev_probe()` runs once per frame and classifies the channel:

| State | Meaning |
|---|---|
| `V32DEV_NOCARD` | no memory card is connected |
| `V32DEV_NOEMU` | the card has no live mailbox: not formatted, `V32DEV_DISABLE` set, or an emulator without the patch. Detected by the header magic **plus** an emulator heartbeat that changes (a stock emulator can show the header, but never changes the heartbeat) |
| `V32DEV_NOHOST` | the mailbox is live but nobody serves this channel: the host flag is clear, or the host heartbeat has not changed for 120 frames (2 s) |
| `V32DEV_WRONG` | a host serves a device of another class |
| `V32DEV_READY` | the channel can be used |

Reading card memory without a card is a hardware error on the console, so
`v32dev_probe()` checks that the card is connected before touching it.

### 3.3 Starting and restarting

Host, when it starts serving a channel (`Channel.announce`), in this order: empty
both rings (move the ends it owns), write a new session, the descriptor, the
status, the type, the heartbeat, and **last** the attached flag. When it stops:
clear the flag. The host then refreshes its heartbeat every ~10 ms.

Program, when the channel becomes `READY` (the first time, or after losing the host):
empty both rings (set `h2c_tail := h2c_head`, `c2h_head := c2h_tail`), set the
open flag, add 1 to its session. If the host session changes while ready, the
host restarted: the rings are emptied the same way and the event `V32DEV_EV_RESET`
is reported.

Each side moves only the ring end that it owns, so a restart on either side leaves
both rings empty without anybody writing the other's words. **The host does not
touch the rings when it sees the program's session change**: the program may
already have sent its first request by then. The host only drops the words of an
unfinished message, and its device forgets what it was doing.

The program tells the host died when the heartbeat stops (120 frames); the host
tells the program restarted from the session word.

### 3.4 Streams and messages

A channel carries either:

- a **stream**: words with no boundaries (`v32dev_read`, `v32dev_write`; they take
  what fits and return how many), or
- **messages** (`v32dev_send`, `v32dev_recv`): `[N][opcode][N payload words]`, up to
  2020 payload words, published all at once. A receiver that sees any of a message
  sees all of it. There is no checksum: the transport is ordered and reliable (the
  ordering guarantees are tested), and a length that does not fit empties the
  ring and counts an error.

Which one a channel uses depends on its class. Do not mix them in a channel.

Host status bits: `READY` (1) the device works; `ERROR` (2) it has failed; `HUP`
(4) the other end is gone and there will be no more data once the ring has been
emptied (`serial_hup()`).

## 4. Classes

### 4.1 block: storage (`storage.h`)

A file on the host, served as a device of **sectors**. Messages, in both directions;
one request at a time; each carries a `tag` so late replies are ignored.

Host descriptor words (read by the program at no cost, with no round trip):

| Descriptor | Meaning |
|---|---|
| 0 | words per sector |
| 1 | number of sectors |
| 2 | flags: bit 0 read only, bit 1 byte mode |
| 3 | most sectors per request |

Requests and replies (opcode: payload):

| Request | Reply |
|---|---|
| 1 READ: `tag, sector, count` | 129: `tag, status, count*sector words` |
| 2 WRITE: `tag, sector, count, words...` | 130: `tag, status` |
| 3 FLUSH: `tag` | 131: `tag, status` |

Status: 0 ok, 1 invalid request, 2 range, 3 read only, 4 I/O error. The library adds
5 timeout (300 frames), 6 no device, 7 busy, 8 out of memory, 9 aborted (host restarted).

A request has at most `descriptor 3` sectors: the library splits bigger ones. Only
whole sectors cross the wire; `storage_read_words`/`storage_write_words` give access
by word at any position (reading the partial sectors at the ends first when
writing).

**Word mode** (default): each word is stored as 4 bytes, little endian; a sector is
128 words = 512 bytes. **Byte mode** (`,bytes`): each word is one byte (the program
sees 0..255; of what it writes only the low 8 bits are kept); a sector is 512 words
= 512 bytes. Byte mode serves ordinary files unchanged, such as text files or a FAT
image, one character per word. A part of the file that does not fill a sector is not
accessible.

The host can also serve a file read only (`,ro`): writes then fail with status 3.

Throughput: the reply of a request has to fit in the ring, so a request moves at most
~2000 words and takes at least one frame. At best (the host answers within the frame)
that is about 115 000 words/s in word mode (15 sectors of 128 words per request, 460
KB/s), and about 92 KB/s in byte mode (3 sectors of 512 words per request). The program
keeps one request in flight at a time, so a slow host lowers it.

### 4.2 char: serial (`serial.h`)

A stream of words in both directions, no messages. What is at the host's end is the
host's choice: `serial:stdio` connects it to the terminal that runs the host tool (the
low 8 bits of each word are a character), `serial:echo` sends back what it receives.
Reading and writing never block (`serial_write_all` and `serial_wait_ready` do
wait, ending frames). `serial_hup()` tells that the host hung up.

### 4.3 pipe (reserved)

Like char, but one-way and with end of file: the host would bind the channel to a
file, a FIFO, or the standard input/output of a process, and set `HUP` after the last
word. Direction is announced in a descriptor word. Not implemented.

### 4.4 socket: network and netplay (design, not implemented)

This is the plan for `network.h` and `netplay.h`; it follows from the rest of this
document and nothing in the transport or the core has to change.

**`network.h`** (class socket) uses messages. One channel serves one endpoint at a
time (up to 4 at once). Kinds: tcp (reliable ordered stream; `SEND`/`RECV` carry bytes
as words), udp (each message is one datagram). Operations: `CONNECT`, `LISTEN`,
`ACCEPT`, `SEND`, `RECV`, `CLOSE`, and a status word per channel (connected, hung up,
error). **The host is the policy point**: the program cannot reach anywhere the host
tool was not told it may. A channel is configured when the host is started
(`--ch2 socket:tcp,allow=example.org:7000`, or a listener port), and a program can
only use `CONNECT`/`LISTEN` within that list. By default a socket channel reaches
nothing.

**`netplay.h`** (socket kind 3, built on `network.h`'s message conventions) does what
a game needs for network play: sessions (host or join), the list of peers, and
**lockstep input exchange**: each frame the program hands in its local input and gets
back every player's input for a frame that all of them have supplied. The host side
does the networking (UDP, relay, hole punching, resending), so the program only sees
frames of input, which keeps the program deterministic. The mailbox costs one or two
frames each way, which becomes part of the input delay a game schedules (say 3 to 6
frames). Rollback is possible in the program if it can save and restore its state; the
channel offers the same messages for it.

## 5. Host tool

`tools/v32dev.py` (Python 3, no dependencies; it only reads and writes the card file, so
it also works on Windows).

```
v32dev.py format game.memc --ch0 storage --ch1 serial     # declare types (creates the card if needed)
v32dev.py serve  game.memc --ch0 storage:disk.img --ch1 serial:stdio
v32dev.py info   game.memc
```

Devices for `serve`: `storage:FILE[,bytes][,ro][,sector=N][,create=SECTORS]`,
`serial:stdio`, `serial:echo`. It can also be used as a module (`Host`, `Channel`,
`StorageDevice`, `SerialDevice`) to write other devices.

`format` refuses to overwrite data in the last 64 KiB of the card that does not belong
to a mailbox unless `--force` is given. Do not format a card that an emulator has
loaded.

## 6. Tests

`tests/v32dev/run.sh <ComputerSoftware with the patch applied>` builds the patched console code and runs:

1. unit tests of the patch on the real console classes (61 checks);
2. a write-order test that intercepts `write(2)` (9 checks): nothing in the header or
   host area is written, and all data comes before any commit word;
3. an end-to-end transport test with the program side and the host in separate
   processes, 4 channels in both directions, with ring wrap-around;
4. **real cartridges**: C programs compiled with the real Vircon32 compiler against
   `lib/v32dev/*.h`, booted in the patched console code with its own `RunNextFrame` loop,
   against `v32dev.py serve` in a separate process: images read and written (every
   word checked on both sides), byte mode and read only, no host, no mailbox,
   mailbox disabled, wrong device class, a host that is killed and comes back, a
   5000-word stream through a ring that holds 2023.

The Vircon32 tools are built from the source tree if `V32TOOLS` is not set (this needs
only `g++`; SDL2 is replaced by a small stand-in, because the tools use it only to
find their own folder).

## 7. Limits and caveats

- The mailbox occupies the last 64 KiB of the card. A program that keeps its own data
  there conflicts with it.
- Everything else on the card is saved as before, except that only modified 256-word
  pages are written. An external change to such an area while the emulator runs is
  **not** picked up, and may be overwritten.
- This is not part of the Vircon32 specification. The specification leaves the
  access to card memory to the implementation (part 6, section 4.2.1), and a card
  formatted this way is still a valid card (size and signature unchanged). An
  unpatched emulator just sees ordinary card memory, and the program's heartbeat
  check says nobody is listening.
- Savestates do not appear to include the card (the emulator's savestate code has no
  card handling), so loading one can rewind the program but not the mailbox. The
  state of a channel is kept in the card (indices, sessions) rather than in the
  program's own memory for that reason, and the open handshake repairs it.
- Linux and macOS are what the tests cover. The code uses standard C++ streams and
  `os.open`, so Windows should work, but how the card file is shared with another
  process on Windows has not been tested.
- A request answered with the wrong session (a reply to a request of a program that has
  since restarted) is ignored by its tag; the tag restarts from 0 with each run.
