# Networked two-player Combat, on an Atari 2600

Two consoles, two FujiNets, one relay, one game of Combat — the 1977 cartridge,
patched at seven read sites and re-laid across three banks, playing in
delay-based input lockstep over a TCP socket.

```sh
make ladder          # every gate, in order
make verify-org      # stock Combat, rebuilt from the disassembly, byte-identical
make echo            # what does a mailbox transaction cost, in frames?
make combat          # build/combat.bin, 8192 bytes
make frames          # every frame the same length, through two bank switches
make det             # the patched build plays exactly like the 1977 ROM
make inputs          # the game no longer reads a console port at all
make sim             # the relay protocol, with no emulator anywhere
make rig             # two consoles, one match, zero desyncs
make rig-hold        # ...with SELECT held down for the whole run
make rig-play        # ...with both players' hands on the stick
make rig-repair      # break one console on purpose; it has to heal
```

`TVSTD=ntsc` (the default) or `TVSTD=pal`. `TVSTD=secam` is refused: a SECAM
TIA renders eight colours chosen by the hue nibble and ignores the luminance
bits, and Combat tells its two tanks apart by colour alone — so on a SECAM set
both players can be looking at identical sprites. The refusal lives in the
build because a 2600 cannot detect its own television standard at runtime: the
ROM generates the video timing and there is nothing to read.

**The cartridge is not in this repository.** Combat is Atari's, 1977, and the
commented disassembly is Harry Dodgson's, Nick Bensema's and Roger Williams'
work on top of it; this is a patch and a server, not a place to redistribute
either. Put your own `combat.bin` and `combat.asm` in `rom/` — see
[`rom/README.md`](rom/README.md) for what the build expects and the md5 it
checks against.

`build.sh` needs Macroassembler AS (`asl`/`p2bin`, on `PATH` or in `~/asl`) and
the firmware tree at `$FUJI_FIRMWARE` (default `~/Workspace/fn-2600`, which
must be on the `2600-experiment` branch). `run.sh` needs a MAME with
`pico/atari-2600/emu/apply.sh` applied, and anything touching the network needs
a `fujinet-pc`.

## How it plays

The FujiNet Lobby lists the Combat room; picking it writes the relay's URL to
an appkey and boots this ROM. The ROM opens `N:TCP://host:9600/`, says hello,
and the relay pairs the first two consoles that turn up. Both players use
joystick 1 on their own console — the host drives the left tank, the guest the
right — and each player's own difficulty switch controls their own tank. RESET
and SELECT work from either console: the two are ANDed on the wire, so either
player may press them and both machines act on the result at the same tick.
SELECT steps the game variation **between games only** — it is ignored once a
game is running, so pick the variation first and then press RESET.

With no FujiNet, no relay or no opponent, it is simply Combat.

## How it works

**Delay-based input lockstep.** Both consoles run the whole game. A sim tick is
four video frames, so fifteen ticks a second; each console sends one packed
input byte per tick, stamped two ticks ahead, and both apply both players'
inputs at the same tick. Local input is delayed exactly as far as the peer's,
so the lag is symmetric — about 133 ms — and neither player is ahead.

**A stall is nearly free here**, which is the one thing easier on this console
than on the Intellivision. When the peer's input has not arrived, the frame runs
normally with the game-logic chain skipped: the picture is regenerated from
unchanged state every frame, so it freezes coherently and the display never
shudders. The collision latches are what make it exact — `VOUT` strobes `CXCLR`
every frame and the kernel refills them from frozen state, so N stalled frames
leave what one would, and two consoles that stalled for different lengths resume
in agreement.

**There is no overscan.** Combat's kernel runs to the last line of the frame, so
the only slack is the vertical-blank wait — measured at 482 cycles on the worst
frame in the game. The network machine is a chain of bounded micro-steps run
from a loop that re-reads `INTIM` before each one, so it cannot overrun the
kernel by construction.

**Combat is fully deterministic** — no RNG, no LFSR, nothing seeded from the
timer — which is the single luckiest fact about it as a netplay target.

`PORTING.md` is the rest of it, written for whoever ports the next 2600 game.

## What is in here

| | |
|---|---|
| `rom/` | the pristine 2002 disassembly and the cartridge dump. **Never edited.** |
| `tools/dasm2as.py` | the DASM → Macroassembler AS conversion, proved by `verify-org` |
| `tools/patches.py` | every change made to Combat, declared |
| `tools/mkbanks.py` | the bank split, derived from the assembler's own listing |
| `tools/check_patch.py` | the audit: an undeclared change fails the build |
| `tools/recon.py` | the port map generator, for the next 2600 game |
| `src/` | the three banks, the fixed tail, and the netcode |
| `server/` | the relay, and the Lobby registration |
| `emu/` | the MAME Lua harnesses |
| `test/` | the rigs |

## If the two consoles ever disagree

They put themselves back together. A checksum of the simulation rides in every
record, and each console compares the peer's against its own at the one instant
they are samples of the same tick. On a difference it presses RESET — not on its
own console, but **into its own wire byte**, so the press is ANDed and delivered
through the delay ring like any real one and both machines restart the round on
exactly the same tick.

It has to be a restart rather than a state push, because tank X is not in RAM at
all: it lives in the TIA's `HMP0`/`HMP1` and is applied incrementally by `HMOVE`,
so there is nothing to push. Combat's own new-game path re-homes it. The scores
go with it, and that is right — the two consoles have just disagreed, so their
scores may have too, and the only state agreed by construction is the one
Combat builds from nothing.

`make rig-repair` nudges one console's `TankY0` by a single scanline mid-game
and asserts that they come back: measured at **4 to 8 ticks, a third of a
second**.

## Not done yet

- **Real hardware.** There is no 2600 FujiNet board yet; the cartridge firmware
  says so itself. Everything here is MAME against a live `fujinet-pc`.
- **An appkey.** Provision one on the fujinet-firmware wiki registry; the
  server defaults to 24 pending that.
- **Nagle.** `NetworkProtocolTCP::open_client_connection` never calls
  `setNoDelay(true)`. That is the firmware tree's line to change, not this
  one's, and eight bytes a tick is exactly the pathological case.
