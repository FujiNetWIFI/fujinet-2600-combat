# Porting Combat to networked play on the FujiNet 2600 cartridge

Written for you, six months from now, starting the next 2600 game. It follows
the shape of `intv-baseball-experiment/PORTING.md`, which is the accumulated
doctrine of fourteen Intellivision ports, and records only what is different
here — plus the things this cart taught that that document could not.

## 1. The model in one page

- **The sim clock is a tick of K video frames, and it is not the frame.**
  Combat's `CLOCK ($86)` is moved out of `VCNTRL` and into the gated logic
  chain, so it counts ticks the sim took and not frames the TV drew.
- **Input** is captured at tick `T`, sent tagged for `T+d`, and applied on both
  consoles at `T+d`.
- **A stall** is a frame that runs normally with the game-logic chain skipped.
  It is nearly free here, and §3.1 is why.
- **Desync** is detected by a checksum byte riding every input record and
  repaired by a full-state push — Combat's whole authoritative state is about
  fifty-five bytes.

The consequence that matters for feel: the sim advances at the rate of the
slower console, and input lag is `d` ticks.

## 2. Know your numbers before you write any code

Same rule, same reason. The Intellivision family believed a documented 30 Hz
tick for years; it was 10, and every latency estimate built on it was three
times too optimistic.

Two numbers govern this port, and **both are measured, not assumed**:

- **The VBLANK slack**, because there is no overscan (§3.2). `tools/cycles.py`
  says **482 cycles worst case** (a SELECT/RESET frame), median 1469.
- **The transaction latency `L`**, in frames. No 2600 client had ever run a
  mailbox transaction more often than every ninety frames, so nobody knew.
  `make echo` is the first thing that runs — before any Combat work at all.

### The measured answer

`make echo`, 25 s against a real `fujinet-pc` and a real TCP echo server,
**489 complete rounds, zero errors**:

```
OPEN    n=1    mean=1.00 min=1 max=1
WRITE   n=490  mean=1.00 min=1 max=1
STATUS  n=490  mean=1.00 min=1 max=1
READ    n=489  mean=1.00 min=1 max=1
TICK 3.0 frames for WRITE+STATUS+READ = 20.0 Hz
```

**One frame per transaction, three frames per lockstep tick, 20 Hz.** That is
twice the Intellivision family's validated 10 Hz, and it means `K = 3` (20 Hz,
`d = 2` → **100 ms**) or `K = 4` (15 Hz, `d = 2` → 133 ms) rather than the
`K = 6`, 200 ms the estimate allowed for.

One frame is the *floor*, not a measurement of the bus: in MAME the cartridge
answers inside the commit, and `PGO` draws one frame before its first look at
`FNACKS` precisely so that a transaction can never report zero. What the figure
does include is the whole FujiNet software path — BoIP, `fujinet-pc`, the N:
device, a real socket — so what is missing is only the physical cartridge bus,
which does not exist yet to measure. Re-measure on hardware; the design must
tolerate `L` growing, and §2.4's table says how `K` moves when it does.

## 3. What is different about this console

### 3.1 A stall is nearly free, and the reason is the collision latches

The Intellivision's stall is a busy-spin inside the timer dispatch with the ISR
phase counter frozen, and §7.5 of that document is about repairing display
state afterwards. None of it applies. The 2600 regenerates the entire picture
from state every frame, so a frozen sim draws a frozen picture and nothing
needs repair.

The deep reason is the collision latches. `VOUT` strobes `CXCLR` at `$F063`
every frame; the latches then accumulate over the drawing and are read by
`COLIS` in the next VBLANK. During a stall every frame draws an identical
picture from identical state, so it leaves identical latches. **A stall of any
length is invisible to `COLIS`** — which is what makes two consoles that
stalled for different numbers of frames resume in agreement.

### 3.2 There is no overscan, so Battleship's frame doctrine does not transfer

`dispgame.inc`'s whole discipline — `DFRAME2` arms the overscan timer and
returns, `DFRAME` waits it out at the start of the next frame, and every
between-frames job is spent inside those thirty lines — rests on an overscan
Combat does not have. `VOUT` returns at `$F156`, `JMP MLOOP` is at `$F02F`, and
`VCNTRL`'s `STA WSYNC / STA VBLANK` follows about twelve cycles later. The
kernel runs to the last line of the frame.

What survives: the per-frame hook inside a timed blank band, the bank switch
taken from inside the hook with the entered bank finishing the frame, and the
fact that the RIOT timer does not care which bank is mapped.

What replaces it is stronger. Battleship's hook gets a fixed budget it must fit
inside. Combat's hook is a loop that re-reads `INTIM` before every bounded
micro-step and stops when the slack is gone:

```
VOUT_VB LDA  INTIM        ; 4
        CMP  #5           ; 2      the gate
        BCC  VOUTW        ; 2      no room: fall into the stock spin
        JSR  NSTEP        ; one bounded micro-step, <= ~105 cycles
        JMP  VOUT_VB      ; 3
```

`TIM64T` decrements every 64 cycles, so `INTIM >= 5` guarantees at least
`4 x 64 = 256` cycles remain against a worst case of ~154. **The hook cannot
overrun the kernel by construction** — there is no budget to get wrong. It also
means a stalled frame, which has ~872 cycles of chain it did not run, buys the
most transport steps, which is exactly when they are wanted.

### 3.3 A bank switch is ONE store

`FN_HOT_BANK` lives in the bit-7-set half of the control page, which is the
one-shot half: `sta $1D80+b` and nothing else. `$1DFF` (`FN_H_COMMIT`) belongs
to the `FN_REG_*` registers, and a store to it after a bank select would commit
whatever register was last armed with a garbage value. `bscore.inc`'s `BSGOTO`
is a single `sta FNRSEL,x`, and Battleship's `emu/banktime.lua` taps
`$1D80-$1D86` alone.

The switch must reset the stack (`ldx #$FF / txs`) because a switch is a jump
and nothing returns through one — Battleship leaked two bytes a poll and after
thirty-five polls the stack had eaten its own entry code. Combat wants
`SP = $FF` at `MLOOP` anyway, so the reset is free.

## 4. The expensive lessons

### 4.1 In lockstep, the LOCAL stick must be patched too

The obvious reading of "player 2's input arrives over the network" is that only
player 1's read needs hooking and the local console keeps driving player 0 from
its own stick. **That desyncs on the first stick movement.** In delay-based
lockstep both inputs are applied at the same tick on both machines; if the
local console reads its own port live at delay 0 while the peer applies it at
delay `d`, the two sims are running different inputs at the same tick.

So **both** `SWCHA` sites are patched — `$F313` (player 1's nibble) and `$F36E`
(player 0's) — and both read the same synthetic byte `NJOY`, assembled once a
tick from the two wire bytes. Seven sites, not six.

### 4.2 `SCROT` must run on a stalled frame

`VOUT` destroys `SCROFF` — five `INC SCROFF`/`+1`/`+2`/`+3` at `$F0BE-$F0C5`,
walking down the glyph as it draws. `SCROT` is what rebuilds it. Skip it on a
stalled frame and the score draws garbage from the first stall onward.

It is a pure function of `SCORE`, so running it always costs nothing and stays
deterministic. `LDSTEL` is in the same class and should also run always: it is
a pure function of frozen state and it re-asserts every TIA register the
picture needs that nothing else writes — `NUSIZ0/1`, `COLUPF`, `COLUBK`. That
is §7.5's "reassert from the header" idea, available here for free. It **must**
stay before `CHKSW`, where stock puts it, because `ChkVM` afterwards overwrites
`Color0` for invisible tanks.

### 4.3 `ClearMem` stops at `$A2`, and `$A3-$FF` is power-on garbage

The source flags `MPace ($AC-$AF)` as "never initialized!", which reads like
one curiosity. It is five:

- `MPace $AC-$AF` — `INC`'d and masked to pace diagonal motion. A different
  phase is different movement.
- `COLcount $E4-$E5` — `DEC COLcount,X` can run before the first `STA`.
- `GameTimer $DD`.
- **`HIRES $BD-$CC`** — `ROT` writes eight of the sixteen bytes per frame,
  alternating players on `CLOCK & 1`, so it takes two frames to fill. Frame one
  draws garbage sprites, and **sprites feed `GRP0`/`GRP1`, which feed the
  collision latches.** That is not cosmetic; it is a frame-one desync.

The fix is seven bytes at `START`: clear `$00-$FF` instead of `$00-$A2`, then
re-seed the netcode cells from constants — the TCP connection lives in the
cartridge, not in RAM, so the clear does not drop it.

### 4.4 The stray `STA WSYNC` in `InitPF` is load-bearing

At `$F550`, and the source itself says "This MUST be something that dropped
through the cracks, there is NO reason!". There is a reason: it is 23 cycles
upstream of `STA RESP0` at `$F563`, and `RESP0` is a **raster-position strobe**
— it is what fixes player 0's starting X. The 2002 commenter did not follow it
eight instructions forward. Reclaiming those 76 cycles randomises the left
tank's start column.

The related trap: `BMI IFnoPlane` at `$F54E` skips that WSYNC for **plane**
games, so in stock Combat a plane game's start X already depends on the whole
path back to `VCNTRL`. Anything inserted in `MLOOP` shifts it again. It is a
fidelity divergence, not a desync — both consoles run the identical path on the
identical frame — but do not write a gate asserting "start positions match
stock" without deciding it first, because it will fail for a reason that looks
exactly like a bug.

### 4.5 Everything the wire carries is sampled at the tick boundary, whether or
not the tick then runs

This is the sharpest lesson of the port and it cost two evenings in two
different disguises. The rule:

> A quantity sampled only on the ticks that RAN is a function of the local
> stall pattern — and the stall pattern is the one thing that differs between
> two consoles by design, because absorbing it is what lockstep is *for*.

**First disguise: the local input capture.** The gate captured this console's
own stick at the end of the ran-the-tick path. On a stalled boundary it never
ran, so the ring slot for tick `T+d` still held whatever was in it — and the
transport, which free-runs and stamps `CBTICK+d` whenever it gets round to it,
duly sent that. The peer stored a stale byte for a tick it then simulated,
while this console simulated the same tick from the byte it had captured for
itself. What it looked like on screen was **each console firing only its own
missile**, and it was invisible for a long time because both tanks still moved
and the checksum did not cover `MisLife`.

**Second disguise: the checksum itself.** Moved to the boundary but still only
on ticks that ran, so a console that stalled at the boundary of `T` sent a
record stamped `T+d` carrying the checksum of `T-1`. The two consoles stall at
different moments, so the relay paired checksums of different ticks and
reported a mismatch on a pair of consoles whose state was byte-identical —
verified by snapshotting both at the same tick and diffing sixteen cells.

Both fixes are the same three lines moved above the stall test.

**The corollary, which is where the first disguise hid:** a checksum is only
worth what it covers. `MisLife` went in after the fact, and if it had been
there from the start the missile bug would have been a `CRC MISMATCH` line on
the first run instead of an evening.

### 4.6 A record, once sent, is IMMUTABLE

This is the counterpart to §4.5, and getting one right broke the other.

The transport free-runs. It sends the record for `CBTICK+d` whenever it reaches
its WRITE step, several times a tick, and a duplicate is harmless *only because
it is identical*. §4.5 moved the local capture above the stall test so a stalled
boundary would not leave the slot empty — and that made the capture run again on
every stalled boundary, **rewriting a record the peer had already stored and
already simulated**. The sender then went on to use the newer byte.

It is invisible while nobody touches anything, because every capture returns the
same idle byte. The moment somebody presses SELECT during a stall, one console
counts the press and the other does not — and two consoles end up on different
game variations, 13 against 22, playing different games entirely. Pressing RESET
in that window forks them into a maze on one screen and biplanes on the other.

So the rule is both halves together: **advance the tick, sample the checksum and
capture the stick as one indivisible act, on a boundary that actually advanced —
or do none of them.** A slot is written once and never revised.

Two consequences worth stating. The session hands over with `CBTICK = $FF` and
the "ran" bit SET, so the very first boundary advances to tick 0 and takes its
first checksum and capture through the same path every later tick takes, rather
than a special case that has to be kept in step with it. And the watermark
starts at `$FE`, not `$FF`, because `$FF` reads as "tick 0 is ready" the instant
`CBTICK` reaches 0.

**How it was found:** not by a gate. Two screenshots, from somebody playing.
Every automated check was green, because nothing in the rig had ever pressed
anything — see §4.18.

### 4.7 A checksum cannot be sampled where it is sent

Combat's game logic runs on **every frame**; only the *input* changes at the
tick rate. So "the state" moves four times a tick, and the transport is
asynchronous — it reaches its WRITE step wherever the frame budget lets it. A
checksum computed in the WRITE step is therefore a different quantity on the
two machines every single time, and the symptom is one console reporting a
value that varies wildly while the other reports a constant.

The fix is to sample it at the one instant both consoles can agree on — the
tick boundary — and let the record carry it. The relay pairs by the stamp and
never has to know the rule.

### 4.8 Seed both rings with IDLE, not with zero

A record is stamped `CBTICK+d`, so the first one anybody sends is for tick `d`
and the slots for ticks `0..d-1` are never sent by anyone. Both consoles have
to agree about ticks neither has heard anything about, so both fill those slots
with the same constant — and the constant is not zero. **The wire keeps the
hardware's active-low sense**, so a zero byte reads as every console switch
pressed and the stick shoved in all four directions. Every match began with two
seconds of both players holding everything down.

Filling them with a *local* capture instead would be asymmetric, which is
worse: the peer's idle byte is not knowable.

### 4.9 Size the remote ring `2d+1`, not `d`

Console B can advance to tick `T_B` only with A's input for `T_B`, which A sent
at its own tick `T_B - d`. So `T_B <= T_A + d`, and A has sent up to
`T_A + d <= T_B + 2d`: B can be holding records for `T_B .. T_B + 2d`, which is
**`2d+1` slots**. At `d = 2` that is five, and the next power of two — for a
free `AND #7` instead of a modulo — is eight. A four-slot ring silently
overwrites a tick that has not been used yet and the symptom is the peer's tank
occasionally repeating a move.

### 4.10 `LDA CTRLTBL,Y` indexes out of bounds, and that is fine

The source says so at line 1739. The maximum index is `CtrlBase[2] + 15 = $25`
against a 33-byte table, so impossible joystick nibbles read into `SNDV`/`SNDC`.
It reads ROM and cannot corrupt anything, and both consoles compute the same
index from the same wire byte. **Do not sanitise the nibble** — clamping on one
side and not the other is the only way to break it.

### 4.11 Zero-page indexed addressing wraps inside page zero

`sta $80,x` with `X = $FF` writes `$7F`, not `$17F`. The RAM-clear loop counts
`X` down from `$7F` for that reason. Combat's own `ClearMem` exploits the same
wrap deliberately — `STA $A2,X` with `X >= $5E` addresses `$0100-$01A1`, and
`$0100-$017F` has A7 clear, so **those writes land on TIA registers**. It
clears the TIA and the RAM in one loop.

### 4.12 Two entry bits, not one entry value

`CBENT` says both "has Combat's START run" and "is this a match". They were one
value at first — 0 cold, 1 local, 2 playing — and the boot bank handing over
with 2 meant the game bank read "not cold" and jumped straight to `MLOOP`, so
**Combat's `START` never ran**. The game came up with every variable zero: no
playfield pointers, no shape pointers, tanks at Y=0. Both consoles did it
identically, so the transport looked perfect and only the checksums disagreed,
by a small constant, for two hundred ticks.

The sibling trap, immediately after: `START`'s own RAM clear ran to `$FF` and
wiped the networked bit the boot bank had just set. The match paired,
transported cleanly, and never advanced a tick, because the game had quietly
fallen back to reading its own joysticks. The clear now stops at `$E5`, which
is where the netcode's cells begin.

### 4.13 A cartridge with no server is still a Combat cartridge

Any session failure — no FujiNet, no relay, a refused connection, a protocol
mismatch — hands over **cold and not networked**, which is the unpatched game
in every respect that matters. It keeps `make det` honest (the local path is
compared byte for byte against the 1977 ROM), and it is the right product
behaviour besides. The reason is kept in the low nibble of `CBERR`, because a
console that silently fell back to playing alone looks exactly like a console
that never got a turn, and the rig has to tell them apart.

### 4.14 The stamp must be stall-independent too, not just the sample

Moving the checksum's *sampling* to the tick boundary (§4.5) was necessary and
not sufficient. The record is stamped `CBTICK + d` and carries `CBCRCV`, and
`CBTICK` was incremented at the *end* of a tick that ran — so after a boundary
that ran, the stamp was `T+1+d` while the checksum was still `CRC(T)`, and
after one that stalled the stamp was `T+d` with the same `CRC(T)`. The offset
between stamp and checksum therefore depended on the stall pattern, which is
the one thing two consoles are guaranteed to disagree about.

The fix is to increment `CBTICK` at the **head of the next boundary**, right
before the sample, behind a "did the last tick run" bit in `CBENT`. The
invariant then holds unconditionally: **at any moment, `CBCRCV` is the checksum
of the state entering `CBTICK`**, so the record stamped `Y` carries the
checksum of `Y-d` on both consoles whatever either stalled through.

The general rule this is the third instance of: *if a quantity and its label
are sampled at different moments, the relationship between them is a fact about
the local machine, not about the simulation.*

### 4.15 A shared display kernel counts in X and Y, and will eat your loop counter

Battleship's port says it plainly — *"seventy seconds where seventeen
milliseconds were meant, which looks exactly like a server that never
answers"* — and this port walked into it anyway, twice in one file. The text
kernel counts scanlines in X and the glyph row in Y, so

```
        ldx     #90
loop:   jsr     CDFRAME
        dex
        bne     loop
```

is not a delay of ninety frames; it is a delay of whatever X happens to hold.
Both of the session's wait loops now count in a memory cell (`CSDLY`). The
symptom was a console that reached the local-play fallback, said so on screen,
and then never handed over to the game at all.

### 4.16 Frame length is measured, not counted

The text kernel's arithmetic says 262 lines and the measurement says 263,
because a frame is measured from one VSYNC *write* to the next and that write
sits inside a line of its own. Combat measures 263 by the same rule. Set the
overscan by what `make frames` reports, not by what the addition says — and
match the game's number, because the peer-left screen appears in the middle of
a session, long after the gate stops excusing the handover.

### 4.17 MAME needs `-skip_gameinfo`, and a teardown needs to escalate

Two small ones that cost real time in front of an audience. Without
`-skip_gameinfo` MAME opens on its machine-information screen and holds the
emulation there until somebody presses a key: headless runs never noticed,
because `-seconds_to_run` counts wall clock and the ROM simply got fewer of
them, but a windowed pair just sits there looking broken.

And `pkill -x mame` is a suggestion, not a teardown. MAME did not act on the
TERM while it owned an SDL window, so every launch stacked two more windows on
top of the last two. `test/stop.sh` now sends TERM, waits, and sends KILL.

### 4.18 A harness that presses nothing proves nothing

Three separate defects hid behind this one, and all three were green.

**`set_value` from inside a memory tap is silently lost.** The tap runs in the
CPU's execution context and port state settles at frame boundaries, so the raw
port never changes and nothing reports an error. Both harnesses drove input that
way. `make det` had been pressing RESET for its whole life without ever starting
a game — comparing two builds sitting in attract mode, which agree just as well
as two builds playing. Drive input from `emu.add_machine_frame_notifier`.

**A field looked up at the moment of pressing is a fresh wrapper**, and
`set_value` on it does nothing either. Cache the field objects once at load, as
every other harness in the family does.

**The wire put RESET and SELECT at bits 4 and 5** — because 0-3 are the stick —
and the capture masked `SWCHB` with `#$30`, reading the console's *unused* bits
4 and 5, which read high always. The wire said "not pressed" for ever. The mixer
at the far end shifted them back down correctly, so both halves looked right in
isolation and the pair was silently broken.

The rig's verdict asked whether the two consoles AGREED. Two consoles that both
ignore SELECT agree perfectly. **A gate that only checks agreement will pass a
pair of machines that are agreeing about nothing** — so the rig now presses both
switches on a schedule and asserts that the variation CHANGED as well as that it
matches.

### 4.19 The tick is eight bits and it wraps — on the SERVER too

Seventeen seconds at fifteen ticks a second. The console handles it: every
comparison of two ticks is signed, which makes a wrap invisible across a window
of ±127, and the ring indices are masks.

The relay is where it bit. It pairs the two consoles' checksums by tick, in a
dict it bounded by popping the *lowest* key — which keeps whatever is
numerically smallest, not whatever is oldest. A straggler from before the match
settled therefore survived, and paired falsely against a genuine record 256
ticks later. It reported `CRC MISMATCH` on two consoles whose zero page was
byte-identical, which is the most expensive kind of wrong a gate can be.

Evicting by **age** rather than by value fixes the stragglers, but not the wrap
itself: at 45 seconds a run crosses two laps and lap 2's tick 5 is a different
tick from lap 1's. The relay now reconstructs the lap from the stream — a tick
that jumps backwards by more than half the range is a wrap, not a reorder — and
pairs by `lap*256 + tick`. Two consoles a few ticks apart either side of a wrap
still line up, and nothing on the console side had to change: every comparison
it makes is already signed.

### 4.20 The sim clock must not be a cell the game can write

Held SELECT was the case that broke it, and the symptom was spectacular: the two
consoles walked the variation at different rates and ended up playing different
games — a maze on one screen and biplanes on the other — while the relay
verified tens of thousands of checksums without a single mismatch.

The tick boundary was `CLOCK AND 3`. `CLOCK` is `$86`, and Combat's `ClrGam`
clears `$80-$A2` on every SELECT advance. So the netcode's own clock was a cell
**the simulation resets**, and it reset it on exactly the frames where the two
consoles were least likely to be in step. Re-phasing is not a desync by itself —
the two machines re-phase at the same simulated frame, so if they were in step
they stay in step — but the moment anything makes one of them advance a frame
earlier, the clock they use to measure the disagreement is itself derived from
the disagreement, and every later boundary is wrong on both sides in different
ways. The two consoles were thirteen ticks apart and reported 808 mismatches.

The rule is simple and it is the 2600 expression of the family's "the sim clock
is not the frame": **the cell that says which frame of the tick this is must
live somewhere the game has no instruction that touches.** Here it is bits 6-7
of `CBENT`, the entry-mode byte, stepped once per frame by an eight-byte routine
called `CBPHI`:

```
CBPHI:  lda     CBENT
        clc
        adc     #$40            ; +1 in bits 6-7; carry out of bit 7 discarded
        sta     CBENT
        rts
```

Adding `$40` steps a two-bit counter in the top of a byte without disturbing the
flags below it and without a mask, and the carry out of bit 7 is the wrap. It
went into the eight bytes of `$FF` filler the 1977 cartridge left between its
last table and its reset vector — the only free space Combat ever had, and
exactly the size of the routine.

The same rule applies to the harness. `emu/rig.lua` had taken its frame-exact
snapshot at `CLOCK AND 3 == 1` for the same reason and with the same flaw; it
now reads `CBENT AND $C0`, and `CLOCK` survives only as a byte in the snapshot,
where a disagreement about it is evidence rather than the instrument.

### 4.21 A checksum must cover what is being simulated, not just what is moving

`CBCRC` covered positions, directions and missile lifetimes — everything that
moves. It did not cover `BINvar` or `GAMVAR`, the game variation. So two
consoles on variation 16 and variation 19, drawing visibly different games,
agreed perfectly on every checksum they exchanged: the tanks were at the same
coordinates, because nothing had yet happened to move them apart.

Widening the checksum to twelve bytes, `BINvar` and `GAMVAR` first, turns that
from an invisible divergence into a reported one on the next tick. The general
form: **the checksum must cover every cell that selects behaviour, not only the
cells behaviour writes.** A variation number, a difficulty switch and a chosen
map are all cheaper to checksum than to debug, and they are precisely the state
that disagrees silently for a long time before it disagrees visibly.

### 4.22 A Lua closure captures a local declared below it as nil

The harness defect that made the fix above look like it had failed. In
`emu/rig.lua` the snapshot tap used a table `phase` that was declared with
`local` sixty lines *further down the file*. Lua binds a local at the point of
its `local` statement, so inside the closure the name was a global, and a global
is `nil`. `#phase` on `nil` raises, the tap died on its first frame, and MAME
printed nothing — so the snapshot was never taken, the rig reported `SNAP never
reached the snapshot tick`, and two verdict lines failed **vacuously** in a way
that reads exactly like a desync.

A second variable had the same shape with a quieter ending: the tap assigned a
*global* `firstsel`, a `local firstsel` below shadowed it for the report, and
the report printed `never` for a press that had plainly happened.

Declare every cell a tap closes over above every tap, and treat a diagnostic
that has never once printed a value as a broken diagnostic rather than as
evidence. Both of these were introduced while *adding instrumentation to chase a
real bug*, which is when this class of mistake is hardest to see: the output got
worse at the same moment the ROM got better.

### 4.23 A gate compares the simulation, not the furniture

The last thing standing between a correct ROM and a green rig, and it failed in
the honest direction: the rig's snapshot line carried the raw `SWCHB` port and
the ring slot holding the peer's record alongside the zero page, and the verdict
compared the line whole. With SELECT held on one console for forty-five seconds
those two bytes differ — `$2D` against `$2F`, and `$BF` against `$9F` — *because
the press is working*. The gate was asserting that both players had their hands
in the same place, and reporting a desync on two machines whose zero page was
identical to the byte.

Two lines now: `SNAP` is the tick, the checksum, the **mixed** stick and switch
bytes the game actually reads, and the zero page — all quantities both consoles
compute from the same inputs, compared whole. `LOCAL` is the raw port and the
peer's slot, printed and never compared, where a difference is the evidence.

The same distinction fixed the last apparent wobble. The variation trace stamped
itself from the harness's *extended* tick counter, refreshed once per video
frame by the frame notifier — and two consoles in simulation lockstep are not in
wall-clock lockstep, so a write happening mid-frame on both was stamped one
apart purely by where each notifier had last run. Stamping from `CBTICK` read at
the instant of the write, which is the ROM's own cell, the two traces are
identical across a five-advance hold: `t33 t33 t49 t49 t65 t65 t81 t81 t97`, on
both machines, to the byte.

**Anything the harness measures with its own clock, it is measuring wrong.**

### 4.24 The transport cannot be locked to the tick, so the record must be a window

The bug a person found by playing, after every switch gate was green.

The transport free-runs a WRITE/STATUS/READ cycle and sends whatever tick the
sim is on when the WRITE comes round. That is normally one record per tick and
the arithmetic says it fits — but only normally. A burst of records to consume
is one step each, and a cycle that runs long means **the sim advances twice
between two WRITEs and a tick's record is never sent at all**. You cannot send N
ticks in fewer than N writes, so there is no scheduling fix; the record has to
carry more than one tick.

What made it expensive was the second half. The peer's ring is indexed by
`tick AND 7` and its watermark tracks only the **newest** tick received, so a
hole in the middle left the *previous lap's* byte in that slot and the gate used
it without a murmur. In attract mode every byte on the wire is IDLE, so a stale
IDLE and a fresh one are the same byte — the pair agreed on sixty thousand
checksums. The instant somebody pressed RESET, one console restarted the game on
a frame the other did not, and the two were one frame of simulation apart for
ever after: `FwdTimer` `$F1` against `$F0`, then a steady four, which is one
whole tick.

Every record now carries a **window of three ticks of input, newest first**, and
the receiver writes each byte into the slot for its own tick. A record that is
never sent is filled in by the next one or the one after. Three is not
arbitrary: the local ring is four slots, so `t`, `t-1` and `t-2` are all still
live, and a wider gap would need the peer to be more than three ticks ahead of
our transport — which lockstep prevents, because if our records stop the peer
stalls, which stops the peer's records, which stalls us, and a stalled sim does
not advance `CBTICK` while the transport keeps running. It is self-correcting in
the direction that matters.

Re-writing a ring slot is safe and does not contradict §4.6: the value written
is the peer's capture for that tick, fixed the moment it was captured, so the
second write puts the same byte back.

The general rule, and it is the one to carry to the next port: **a lockstep
transport that sends exactly one tick per packet is betting that it never misses
a beat, and it will.** Send a window. It costs three bytes of a record that was
already padded to eight and it converts a silent, permanent desync into nothing
at all.

### 4.25 A harness that never moves the stick proves nothing either

§4.18 said a harness that presses nothing proves nothing, and the rig was duly
taught to press RESET and SELECT. It still never touched a joystick — so the
only inputs any gate ever exercised were the two that are **ANDed on the wire
and therefore identical on both machines by construction**. The gap above lived
underneath all of them.

`emu/play.lua` is the answer: both consoles play, each driving its own left port
on a schedule keyed to the emulator's frame counter and deliberately *different*
on the two machines. The asynchrony is the property under test, not sloppiness —
a player's thumb has no idea what tick it is, and the design's whole claim is
that it does not need to. That is the opposite of `emu/det.lua`'s situation,
where two *builds* must see identical bytes, and it is why the two harnesses
generate input in opposite ways.

It dumps the authoritative sim state once per tick at the boundary frame, and
`tools/playdiff.py` lines the two dumps up and names the first cell to disagree.
"The two consoles desynced" is what the relay already says and is not
actionable; "`$8D FwdTimer` is one lower on the guest from tick 44" is a fix.

Add `PLAY_WINDOW=lo,hi` and it prints every *frame* in that tick range with the
gate's own verdict, the stall counter, the mixed switch byte and **both rings in
full** — which is what actually found it: the host's local ring read
`AF AF AF AF` while the guest's remote ring read `… AF AF BF AF …`, one slot
holding the wrong lap's byte, in a run where every other number agreed.

### 4.26 The repair is a synthetic RESET, because the X positions are not in RAM

Desync *detection* had always worked and had always been useless, because it
lived in the relay — a log on a third machine cannot repair anything. Two things
were missing: the console had to notice, and it had to be able to do something
about it.

**Noticing is nearly free.** Every record already carries the sender's checksum,
sampled at its own last tick boundary — which is the boundary of tick `b2 - d`,
since the record is stamped `d` ticks ahead. So when that tick is the one *we*
are on, the peer's checksum and our `CBCRCV` are samples of the same instant and
must be equal. `CBCRCV` is a single cell and there is no zero page left for a
history of them, so a record whose tick does not line up is simply not compared.
That costs nothing: a desync is persistent, and the ticks line up within a few
of them. A false positive is impossible — a difference here *is* a difference in
the simulation — and a corrupt record was already dropped by the check byte.

**Repairing is where the console's shape decides the design.** The obvious
repair is a state push, and it cannot work here: **tank X is not in RAM at all**.
It lives in the TIA's `HMP0`/`HMP1` and is applied incrementally by `HMOVE`, so
there is nothing to push and nothing to restore. That is the same fact that
ruled out state sync in the first place.

What *can* restore it is Combat itself. `GSGRCK`'s new-game path clears
`$89-$A2` and re-runs `ResetField`, which re-homes the Y positions **and** the
sprite X positions, through `RESP1`/`HMP0`/`HMP1`/`HMOVE`. So the repair is:
restart the round.

The hard part of restarting it is agreeing *when*. A repair that happens at
different ticks on the two consoles is just a second desync, and there was not
one spare byte of zero page to store a scheduled tick in. There did not need to
be: **RESET is already an event the two machines agree about to the tick**,
because it is ANDed on the wire and applied through the delay ring like any
other input. So the repair is a **synthetic RESET press, made into this
console's own capture**, and it reaches both ends by the road every real press
takes — the one `make rig` has been pressing RESET across for its whole life. It
costs one bit of `CBENT` and is one-shot: `CBRSY` clears the bit as it presses,
so the press lasts one tick.

One console arming it is enough, for the same reason either player may start a
game. Both usually do arm, because each one sees the other's checksum.

**The scores are wiped, and that is correct rather than a wart.** The two
consoles have just disagreed, so their scores may disagree too. The only state
that is agreed by construction is the one Combat itself builds from nothing.

Measured by `make rig-repair`: `TankY0` nudged by one scanline at tick 120 —
the smallest desync there is — detected, repaired, and **back in agreement after
4 to 8 ticks — a third of a second** — with the following 120 ticks
byte-identical.

Which needed a harness that could break things on purpose. Recovery cannot be
tested by waiting for a bug, because a correct pair never diverges; `PLAY_INJECT`
corrupts one console mid-game and the gate asserts the opposite of every other
gate here — that the relay *did* report mismatches, and that they stopped.

### 4.27 SECAM is refused at build time, because a 2600 cannot detect its own television

The refusal had to go somewhere it could actually be made, and that is not the
console. **A 2600 generates its own video timing.** There is no register, no
interrupt and no external reference that reveals which kind of television is on
the other end of the cable — which is why every 2600 game in history shipped as
separate NTSC, PAL and SECAM images rather than detecting it. A runtime probe is
not a thing that can be written, and anyone who sets out to write one will spend
an afternoon proving it.

So the standard is **declared**: `build.sh` takes `TVSTD=ntsc|pal`, bakes it into
`CBTVSTD`, and **refuses to produce a SECAM image at all**, with the reason on
the terminal. The console puts the byte in `HELLO` — which moved the protocol
version to 2, because inserting a byte between the version and the name is a
payload *shape* change and an older relay would read it as the first letter of
somebody's name. The relay refuses a console that declares SECAM as a second
lock, since a hand-built image can claim anything.

**What is actually wrong with SECAM is the TIA, not the network.** A SECAM TIA
renders eight colours, chosen by the hue nibble, and ignores the luminance bits
entirely. Combat's two tanks are the same shape and are told apart *only* by
colour, and several of its variations separate the pair by luminance alone — so
on a SECAM set two players can be looking at two identical sprites. No amount of
lockstep fixes that. It is not two-player Combat, so it is refused rather than
guarded.

**PAL is not refused, and deliberately so.** The simulation is driven by ticks
and not by wall clock, so a 50 Hz console runs the pair at its own rate and
computes exactly the same states; `make sim` asserts that a PAL console pairs
with an NTSC one.

### 4.28 Known, not yet acted on

- **Nagle is on for `N:TCP` sockets.** `NetworkProtocolTCP::open_client_connection`
  never calls `setNoDelay(true)`; only the modem devices do. A four-byte record
  per tick is exactly the pathological case. Logged in the Intellivision family
  as §7.18 and unfixed there too.
- **`NParser::read` copies past the end of `receiveBuffer`** when
  `0 < avail < length` — undefined behaviour, not an error path, returning
  plausible garbage. The `READ((avail>>2)<<2)` rule prevents it. This makes
  "never ask for more than the last `STATUS` reported" a stronger rule here
  than the family states it.

## 5. The toolchain

Macroassembler AS (`asl` + `p2bin`), not dasm — that is what the whole FujiNet
2600 client family uses and what `checkbanks.py`, `mktail.py` and `checkdefs.py`
parse. `rom/combat.asm` is the pristine 2002 DASM disassembly and is **never
edited**; `tools/dasm2as.py` regenerates the AS translation on every
`make verify-org`, which then has to be `rom/combat.bin` byte for byte. That is
the anti-drift guard for everything downstream, and it caught the conversion
being right on the first run rather than asserting it.

The dialect differences are few and all mechanical: `processor` -> `CPU`,
`include vcs.h` -> `INCLUDE "vcs.inc"`, `.byte`/`.BYTE`/`byte` -> `DB`,
`.word` -> `DW`, `=` -> `EQU`. The only one with a trap: AS has neither DASM's
`<`/`>` prefix operators nor `lo()`/`hi()`, and the family spells a low byte
`#(X)&$FF`. DASM also tolerates a stray `#` inside a `.byte` list — Combat's
`SPRLO`/`SPRHI` tables are written `.BYTE #<TankShape` — where an immediate
marker means nothing and AS reads it as a symbol.

One collision to know about: **AS is case-insensitive**, and `PF0`/`PF1`/`PF2`
are TIA playfield registers in `vcs.inc`. A local label called `PF1` is a
double definition. Combat's own 191 labels happen to have no case collisions,
which is the only reason the conversion is as clean as it is.

## 6. Status

Every gate below passes.

| Gate | Result |
|---|---|
| `make verify-org` | PASS — stock Combat, rebuilt from the DASM disassembly through `tools/dasm2as.py`, byte-identical to the cartridge dump |
| `make echo` | PASS — 489 rounds, 0 errors, **1 frame per transaction, 3 frames per WRITE/STATUS/READ cycle = 20 Hz** |
| `make combat` | PASS — 8192 bytes; `checkdefs`, `checkrom`, `checkbanks` and `mktail` all clean; `check_patch` reports 99 bytes changed and **all declared** |
| `make frames` | PASS — 717 frames, every one 263 lines, through **two bank switches a frame**; stock Combat measures 263 too |
| `make det` | PASS — 1195 frames identical to the 1977 ROM, 1193 distinct states |
| `make inputs` | PASS — **4 sites, all inside `CBSHIM`**; the game's own seven are gone |
| `make lag` | PASS — the shadows refresh 1.1 times a second instead of sixty |
| `make sim` | PASS — 20 protocol conformance checks |
| `make session` | PASS — socket opened, HELLO delivered, relay reports the player by name |
| `make rig` | PASS — two consoles, **429 and 432 ticks, zero CRC mismatches**, byte-identical zero page at tick 150, through the 8-bit tick wrap, with SELECT and RESET pressed on ONE console and obeyed by both |
| `make rig RIG_HOLD=select SECS=45 SNAPTICK=250` | PASS — **651 and 654 ticks** with SELECT held down for the whole run: the variation walks `0 1 2 3 4` at ticks 33, 49, 65, 81, 97 on **both** consoles, identically, and the zero page still matches at tick 250 |
| `make rig-play` | PASS — two consoles with their **hands on the stick**, playing asynchronously for 40 seconds: **581 ticks, 0 CRC mismatches, and byte-identical sim state at every single tick**. Before the input window went in, the same gate reported 1191 mismatches and a divergence from tick 44 |
| `make rig-repair` | PASS — one console's `TankY0` deliberately nudged by one scanline at tick 120: the relay saw it, both consoles saw it, and they were **back in agreement 4 to 8 ticks later** with the following 120 byte-identical |
| `make sim` television | PASS — a SECAM console is refused, an unknown standard is refused, a PAL console pairs with an NTSC one |
| `TVSTD=secam ./build.sh` | REFUSED, with the reason |
| `make ladder` | PASS — the whole ladder, in order, in one run |
| `make rig SECS=45` | PASS — **655 and 658 ticks across 2.5 tick-wraps**, zero mismatches, and the peer-left screen on the survivor |
| `make play` | two windows, paired, playing |

Measured on the shipping build by `emu/slack.lua`: the vertical-blank slack the
network machine lives in is **448 cycles at worst, 960 on average**, and only
0.4% of frames fall below the gate and run no step at all.

Remaining, and deliberately so: real hardware, which does not exist yet, and
the Nagle fix in `NetworkProtocolTCP::open_client_connection`, which belongs to
the firmware tree rather than to this one.
