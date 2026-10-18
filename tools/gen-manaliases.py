#!/usr/bin/env python3
"""Writes the alias pages of the manual: one page for every function,
structure and constant of the library headers, each only a pointer to the
page of its header (".so man3/<header>.3v32"), as the Vircon32 manual pages
do. The pages of the headers themselves are written by hand.

The manual is in two parts, one for each component of the project:

    man/v32io/man3     v32io.h, keyboard.h, mouse.h    (lib/v32io)
    man/v32dev/man3    v32dev.h, storage.h, serial.h   (lib/v32dev)

    tools/gen-manaliases.py [v32io | v32dev | all]        (default: all)

The symbols are read from the headers, so a function added to a header
shows up here: it also needs documenting in the header's page, which this
script checks (an undocumented symbol is an error).
"""
import os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

# per component: header -> prefixes of the functions and of the macros that
# belong to it; the structures; macros that do not start with those
# prefixes; and the internal functions that are deliberately not documented.
# The page named after the library itself (v32io(3v32), v32dev(3v32)) is
# the overview, so the structure of that name has no alias: it is described
# in the page of its header.
components = {
    'v32io': {
        'headers': {
            'v32io.h':    (('v32io_',),            ('V32IO_',)),
            'keyboard.h': (('v32kbd_', 'v32key_'), ('V32KBD_', 'V32KEY_')),
            'mouse.h':    (('v32mouse_',),         ('V32MOUSE_',)),
        },
        'structs': {'v32key': 'keyboard.h', 'v32kbd': 'keyboard.h',
                    'v32mouse': 'mouse.h'},
        'extra': {'v32io.h': ['FIRST_GAMEPAD_PORT', 'SECOND_GAMEPAD_PORT',
                              'THIRD_GAMEPAD_PORT', 'FOURTH_GAMEPAD_PORT']},
        'internal': set(),
    },
    'v32dev': {
        'headers': {
            'v32dev.h':   (('v32dev_',),           ('V32DEV_',)),
            'storage.h':  (('storage_',),          ('STORAGE_',)),
            'serial.h':   (('serial_',),           ()),
        },
        'structs': {'storage': 'storage.h', 'serial': 'serial.h'},
        'extra': {},
        'internal': {'v32dev_ring_get', 'v32dev_ring_put', 'v32dev_refresh',
                     'storage_finish', 'storage_describe', 'storage_start'},
    },
}

def generate(name, outdir=None):
    comp = components[name]
    headers = comp['headers']
    libdir = os.path.join(root, 'lib', name)
    outdir = outdir or os.path.join(root, 'man', name, 'man3')

    functions = {}   # name -> header
    constants = {}
    for header, (fprefixes, mprefixes) in headers.items():
        text = open(os.path.join(libdir, header)).read()
        # functions: definitions at the start of a line
        for m in re.finditer(r'^[a-z0-9_]+\s+\*?\s*([a-z0-9_]+)\s*\(', text, re.M):
            if m.group(1).startswith(fprefixes) and m.group(1) not in comp['internal']:
                functions[m.group(1)] = header
        # macros
        for m in re.finditer(r'^#define\s+([A-Z0-9_]+)', text, re.M):
            n = m.group(1)
            if (mprefixes and n.startswith(mprefixes)) or n in comp['extra'].get(header, []):
                constants[n] = header
    for st, h in comp['structs'].items():
        functions[st] = h

    # On a file system that ignores case (macOS, Windows) V32KBD_GLYPH and
    # v32kbd_glyph would be the same file: the function keeps the page, and the
    # constant is described in the same header page anyway.
    lower = {n.lower() for n in functions}
    skipped = sorted(n for n in constants if n.lower() in lower)
    symbols = dict(functions)
    symbols.update({n: h for n, h in constants.items() if n not in skipped})
    if skipped:
        print('no alias for (same name as a function, ignoring case): ' + ', '.join(skipped))

    # every symbol must be documented in the page of its header
    pages = {h: open(os.path.join(outdir, h + '.3v32')).read() for h in headers}
    missing = [s for s, h in list(symbols.items()) + [(n, constants[n]) for n in skipped]
               if s not in pages[h].replace('\\-', '-').replace('\\fB', '').replace('\\fP', '')]
    if missing:
        sys.exit('%s: not documented in their header page: %s' % (name, ', '.join(sorted(missing))))

    # start clean: remove alias pages of symbols that no longer exist
    for f in os.listdir(outdir):
        fp = os.path.join(outdir, f)
        if f.endswith('.3v32') and open(fp).readline().startswith('.so '):
            os.remove(fp)

    seen = {}
    for sym, header in sorted(symbols.items()):
        low = sym.lower()
        if low in seen or low in (h.lower() + '.3v32' for h in headers):
            sys.exit('names collide ignoring case: %s / %s' % (sym, seen.get(low)))
        seen[low] = sym
        with open(os.path.join(outdir, sym + '.3v32'), 'w') as f:
            f.write('.so man3/%s.3v32\n' % header)
    print('%s: %d alias pages in %s' % (name, len(symbols), outdir))

which = sys.argv[1] if len(sys.argv) > 1 else 'all'
if which != 'all' and which not in components:
    sys.exit('usage: gen-manaliases.py [v32io | v32dev | all]')
for name in (components if which == 'all' else [which]):
    generate(name)
