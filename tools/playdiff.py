#!/usr/bin/env python3
"""playdiff.py -- which cell went first, and at which tick.

Two per-tick dumps from emu/play.lua, one per console. Lines up the ticks,
finds the first one at which the authoritative sim state differs, and NAMES THE
CELLS -- because "the two consoles desynced" is what the relay already says and
is not actionable, while "$A4 TankY0 is one lower on the guest from tick 63" is.

    tools/playdiff.py build/rig/c1.out build/rig/c2.out
"""
import re
import sys

BASE = 0x80
# How many ticks at the end of the run must agree for a repair to count as one.
# 120 is eight seconds at fifteen ticks a second -- long enough that a pair that
# merely happened to coincide for a moment cannot pass.
RECOVER_TAIL = 120
# Combat's own names, from rom/combat.asm. Only the cells the dump covers.
NAMES = {
    0x80: "BINvar", 0x81: "BCDvar", 0x82: "GAMSHP", 0x83: "PF_PONG",
    0x84: "GUIDED", 0x85: "MisMode", 0x86: "CLOCK", 0x87: "GameOn",
    0x88: "SelDbnce", 0x89: "SelDbnce", 0x8A: "FwdTimer", 0x8B: "FwdTimer",
    0x8C: "Velocity", 0x8D: "Velocity", 0x8E: "TurnTimer",
    0x91: "TurnTimer", 0x92: "Score0", 0x93: "Score1", 0x94: "BounceCt",
    0x95: "DIRECTN0", 0x96: "DIRECTN1", 0x97: "MisDIR0", 0x98: "MisDIR1",
    0x99: "MisLife0", 0x9A: "MisLife1", 0x9B: "MisBounce",
    0xA3: "GAMVAR", 0xA4: "TankY0", 0xA5: "TankY1",
    0xA6: "MissileY0", 0xA7: "MissileY1",
    0xA8: "MVadjA0", 0xA9: "MVadjA1", 0xAA: "MVadjB0", 0xAB: "MVadjB1",
    0xAC: "MPace0", 0xAD: "MPace1", 0xAE: "MPace2", 0xAF: "MPace3",
    0xB0: "XOFFS0", 0xB1: "XOFFS1", 0xB2: "XOFFS2", 0xB3: "XOFFS3",
    0xDD: "GameTimer",
}


def load(path):
    """Index by the harness's boundary COUNT, and check the ROM's raw tick.

    The count is authoritative: the tap fires once per completed boundary, so
    counting them is the tick exactly. The raw CBTICK rides along only so a
    disagreement between the two numbering schemes is visible instead of being
    silently absorbed into the diff.
    """
    out, raw = {}, {}
    for line in open(path, errors="replace"):
        m = re.match(r"^S (\d+) (\d+) ([0-9A-F]+)$", line.strip())
        if m:
            n = int(m.group(1))
            out[n] = m.group(3)
            raw[n] = int(m.group(2))
    return out, raw


def addr_of(i):
    return 0xDD if i == 0x34 else BASE + i


def main():
    (a, ra), (b, rb) = (load(p) for p in sys.argv[1:3] if not p.startswith("--"))
    if not a or not b:
        print("playdiff: one of the consoles printed no state at all")
        return 1
    common = sorted(set(a) & set(b))
    print("%d ticks from console 1, %d from console 2, %d in common"
          % (len(a), len(b), len(common)))
    # The two consoles number their boundaries from their own first one. If the
    # ROM's raw tick disagrees by a constant, the dumps are simply offset and
    # every "divergence" below is that offset; if it disagrees by a VARYING
    # amount, the two consoles really are running different numbers of ticks.
    off = {((ra[t] - rb[t]) & 0xFF) for t in common}
    if off != {0}:
        print("raw-tick offsets seen between the two dumps: %s"
              % " ".join("%+d" % (o - 256 if o > 127 else o)
                         for o in sorted(off)))
    first = None
    counts = {}
    for t in common:
        x, y = a[t], b[t]
        if x == y:
            continue
        bad = [i for i in range(len(x) // 2)
               if x[2 * i:2 * i + 2] != y[2 * i:2 * i + 2]]
        for i in bad:
            counts[addr_of(i)] = counts.get(addr_of(i), 0) + 1
        if first is None:
            first = t
            print("\nFIRST DIVERGENCE at tick %d" % t)
            for i in bad:
                ad = addr_of(i)
                u, v = int(x[2 * i:2 * i + 2], 16), int(y[2 * i:2 * i + 2], 16)
                print("  $%02X %-10s  c1=$%02X  c2=$%02X  (%+d)"
                      % (ad, NAMES.get(ad, ""), u, v, v - u))
            # The three ticks either side, so the run-up is visible.
            for u in [t2 for t2 in common if t - 3 <= t2 <= t + 3]:
                print("    t%-5d %s" % (u, "SAME" if a[u] == b[u] else "DIFF"))
    # --repair inverts the question. A correct pair NEVER diverges, so recovery
    # cannot be tested by waiting for a bug: the harness breaks one console on
    # purpose (PLAY_INJECT) and the assertion is that they come back together.
    if "--repair" in sys.argv:
        bad = [t for t in common if a[t] != b[t]]
        tail = common[-RECOVER_TAIL:]
        healed = all(a[t] == b[t] for t in tail)
        print()
        if not bad:
            print("NO DIVERGENCE AT ALL -- the injection never landed, so this "
                  "run proves nothing about the repair")
            return 1
        print("diverged at tick %d, %d divergent ticks in all, last at %d"
              % (bad[0], len(bad), bad[-1]))
        print("recovered after %d ticks (%.1f seconds at 15 a second)"
              % (bad[-1] - bad[0] + 1, (bad[-1] - bad[0] + 1) / 15.0))
        if healed:
            print("the last %d ticks agree byte for byte" % len(tail))
        else:
            print("THE LAST %d TICKS DO NOT AGREE -- no repair happened"
                  % len(tail))
        return 0 if healed else 1

    if first is None:
        print("\nNO DIVERGENCE -- the two consoles agree at every common tick")
        return 0
    print("\nevery cell that ever differed, by how many ticks:")
    for ad in sorted(counts, key=lambda k: -counts[k]):
        print("  $%02X %-10s %d" % (ad, NAMES.get(ad, ""), counts[ad]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
