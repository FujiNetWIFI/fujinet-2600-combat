# The cartridge goes here

Two files, neither of which is in this repository:

| File | What | md5 |
|---|---|---|
| `combat.bin` | the 2048-byte Combat cartridge dump | `0eb6597ade38ffcdedae982e08760509` |
| `combat.asm` | the commented DASM disassembly of it | — |

**Why they are not here.** Combat is Atari's, published 1977 and written by
Larry Wagner. The disassembly is Harry Dodgson's original work, commented
further by Nick Bensema in 1997 and overhauled by Roger Williams in 2002 — the
version this project builds against is the one whose header carries all three
names and whose ORGs are at `$F000`. This repository is a patch and a server. It
is not a place to redistribute either file, so you bring your own.

**What the build does with them.**

- `combat.asm` is the source of record. `tools/dasm2as.py` converts it to
  Macroassembler AS on every build and `tools/patches.py` applies the declared
  patch map to it, so there is no hand-maintained copy of Combat in this tree
  for a patch to drift away from.
- `combat.bin` is the proof. `make verify-org` rebuilds the disassembly and
  requires the result to be **byte-identical** to the dump, and
  `tools/check_patch.py` diffs the patched build against it and fails on any
  change that is not on the declared list.

Without them `make verify-org`, `make combat` and everything downstream will
not run. Nothing else in the repository needs them.
