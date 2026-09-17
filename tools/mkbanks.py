#!/usr/bin/env python3
"""mkbanks.py -- split Combat across the FujiNet cartridge's banks.

A FujiNet bank is the 2K at $1000-$17FF; the 2K above it is the cartridge's
mailbox and the client only owns the 220-byte tail inside it. Combat is exactly
2048 bytes, so it fills a bank precisely and leaves nowhere for netcode -- which
is why it is split: the display kernel and the network state machine in one
bank, the frame loop and the seven game routines in another.

THE REGIONS ARE DERIVED FROM THE ASSEMBLER'S OWN LISTING, never from line
numbers in this file. Every source line's address comes out of
build/combat_org.lst, which `make verify-org` has just proved reproduces the
cartridge dump byte for byte. A region boundary cannot drift away from the code
it is supposed to bound.

EVERY BYTE KEEPS THE ADDRESS IT HAS TODAY, rebased $F000 -> $1000. Each bank
simply leaves the other's regions empty. That is deliberate and it buys two
things: every absolute JMP/JSR target, every PLFPNT low byte and every
`LDA #>PF0_0` page constant stays byte-identical, so check_patch.py can be a
real byte audit rather than a formality; and InitPF (game bank) can take the
ADDRESS of a playfield map that lives in the kernel bank without anything
having to extract it from the other bank's listing.

Usage: mkbanks.py <stock.lst> <combat.asm> <out.inc>
"""

import re
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dasm2as
import patches as patchmap

BASE = 0xF000           # where the disassembly puts Combat
WINDOW = 0x1000         # where a FujiNet bank is mapped
REBASE = BASE - WINDOW

# (first, last+1, bank). Addresses are the STOCK ones; see the module docstring.
# `kind` matters to more than documentation: tools/checkrom.py walks every bank
# linearly looking for banned opcodes, and a linear walk through a data table is
# misaligned garbage. See tools/checkrom_filter.py.
REGIONS = [
    (0xF000, 0xF054, "GAME", "START, MLOOP and VCNTRL", "code"),
    (0xF054, 0xF157, "KERN", "VOUT -- the display kernel", "code"),
    (0xF157, 0xF5C5, "GAME", "the seven game routines", "code"),
    (0xF5C5, 0xF5F7, "KERN", "NUMBERS -- the score digits", "data"),
    (0xF5F7, 0xF779, "GAME", "Xoffsets, MVtable, the shapes, CTRLTBL, sound, ColorTbl", "data"),
    (0xF779, 0xF7C6, "KERN", "the playfield maps", "data"),
    (0xF7C6, 0xF800, "GAME", "SPRLO/SPRHI, PLFPNT, VARMAP, AudPitch", "data"),
]

# Labels the OTHER bank needs the address of. InitPF is in the game bank and
# loads the playfield maps' address into LORES; the kernel dereferences it.
# Nothing else crosses -- and the rest are deliberately left undefined in the
# bank that does not own them, so an accidental cross-bank JMP or JSR is a
# build error rather than a jump into whatever the other bank has there.
EXPORT = {(0xF779, 0xF7C6)}

LST = re.compile(r"^\s*(\d+)/\s*([0-9A-F]{1,4}) :")
SYM = re.compile(r"^\s*\*?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([0-9A-F]{4})\s")
ORG = re.compile(r"^(\s*)ORG(\s+)\$F([0-9A-F]{3})\b", re.IGNORECASE)


def line_addresses(path):
    """line number -> the address the assembler put that line at."""
    addr = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = LST.match(line)
            if m:
                addr.setdefault(int(m.group(1)), int(m.group(2), 16))
    return addr


def symbols(path):
    """label -> address, from the listing's symbol table."""
    out, in_tab = {}, False
    with open(path, errors="replace") as f:
        for line in f:
            if "Symbol Table" in line:
                in_tab = True
                continue
            if not in_tab:
                continue
            for part in line.split("|"):
                m = SYM.match(part)
                if m:
                    out[m.group(1)] = int(m.group(2), 16)
    return out


def region_of(addr):
    for i, (lo, hi, _bank, _why, _kind) in enumerate(REGIONS):
        if lo <= addr < hi:
            return i
    return None


def main():
    lst, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
    addr = line_addresses(lst)
    syms = symbols(lst)

    lines = patchmap.apply(open(src).readlines())

    # Bucket every source line into the region its STOCK address falls in.
    # A comment line carries the address of the next byte to be emitted, so
    # header comments group with the code they introduce, which is what makes
    # the output readable rather than merely correct.
    buckets = [[] for _ in REGIONS]
    for n, raw in enumerate(lines, 1):
        a = addr.get(n)
        if a is None or a < BASE:
            continue                    # the equates and the header
        conv = dasm2as.convert(raw)
        # The body's own ORGs are rebased; the one at line 201 that opens the
        # image is dropped, because the generator emits a region's ORG itself.
        if n == 201:
            continue
        conv = ORG.sub(lambda m: "%sORG%s$1%s" % (m.group(1), m.group(2), m.group(3)),
                       conv)
        r = region_of(a)
        if r is not None:
            buckets[r].append(conv)

    # The preamble: everything before the ORG. Equates only, and they are the
    # same in every bank. `processor`/`include` are dropped -- each bank file
    # supplies its own CPU and includes vcs.inc itself.
    pre = []
    for n, raw in enumerate(lines, 1):
        if addr.get(n, 0) >= BASE or n == 201:
            continue
        conv = dasm2as.convert(raw)
        if re.match(r"^\s*(CPU|INCLUDE)\b", conv, re.IGNORECASE):
            continue
        pre.append(conv)

    with open(out, "w") as f:
        f.write("; generated by tools/mkbanks.py -- do not edit.\n"
                "; Combat, split across the FujiNet cartridge's banks. Every byte keeps\n"
                "; the address it has in the cartridge dump, rebased $F000 -> $1000.\n"
                "; Selected by CBBANK; see tools/mkbanks.py for why the regions are what\n"
                "; they are, and tools/patches.py for every change made to the original.\n\n")
        f.writelines(pre)

        for i, (lo, hi, bank, why, _kind) in enumerate(REGIONS):
            end = hi
            f.write("\n; ---------------------------------------------------------------\n")
            f.write("; $%04X-$%04X  %s bank: %s\n"
                    % (lo - REBASE, end - 1 - REBASE, bank.lower(), why))
            f.write("        IF      CBBANK = BANK%s\n" % bank)
            f.write("        ORG     $%04X\n" % (lo - REBASE))
            f.writelines(buckets[i])
            # A label at the region's end, so the bank file can ORG into the
            # hole that follows without a hardcoded address that would go stale
            # the moment a patch changed a region's length.
            f.write("CBRE%-4dEQU     *\n" % i)
            f.write("        ENDIF\n")

            if (lo, hi) in EXPORT:
                f.write("        IF      CBBANK <> BANK%s\n" % bank)
                f.write("; The other bank needs these ADDRESSES but not these bytes.\n")
                for name, a in sorted(syms.items(), key=lambda kv: kv[1]):
                    if lo <= a < hi:
                        f.write("%-8sEQU     $%04X\n" % (name, a - REBASE))
                f.write("        ENDIF\n")

    sizes = {}
    for i, (lo, hi, bank, _why, _kind) in enumerate(REGIONS):
        sizes[bank] = sizes.get(bank, 0) + hi - lo
    print("mkbanks: %s" % ", ".join("%s %d bytes" % (b, n) for b, n in sorted(sizes.items())))


if __name__ == "__main__":
    main()
