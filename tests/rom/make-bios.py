#!/usr/bin/env python3
"""Writes the input files for a stub BIOS that starts the cartridge right away
(the tests have no use for the real BIOS). A BIOS needs exactly one texture and
one sound, so those are 1x1 and 1 sample.

usage: make-bios.py <output dir>      then: assemble bios.asm; packrom bios.xml"""
import os
import struct
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "bios.asm"), "w").write(
    "; test BIOS: starts the cartridge right away\n"
    "; (the CPU starts at word 4 of the BIOS, so the first 4 words are never run;\n"
    ";  a hardware error would jump to word 0, which halts)\n"
    "  hlt\n  hlt\n  hlt\n  hlt\n  jmp 0x20000000\n")
open(os.path.join(out, "bios.vtex"), "wb").write(b"V32-VTEX" + struct.pack("<III", 1, 1, 0))
open(os.path.join(out, "bios.vsnd"), "wb").write(b"V32-VSND" + struct.pack("<II", 1, 0))
open(os.path.join(out, "bios.xml"), "w").write(
    '<?xml version="1.0" encoding="UTF-8" standalone="no" ?>\n'
    '<rom-definition version="1.0">\n'
    '    <rom type="bios" title="v32dev test bios" version="1.0" />\n'
    '    <binary path="bios.vbin" />\n'
    '    <textures><texture path="bios.vtex" /></textures>\n'
    '    <sounds><sound path="bios.vsnd" /></sounds>\n'
    '</rom-definition>\n')
