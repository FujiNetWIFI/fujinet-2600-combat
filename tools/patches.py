"""patches.py -- the declared patch map for networked Combat.

The family's discipline, from intv-baseball-experiment: the original source is
never edited, every change to it is DECLARED here, and tools/check_patch.py
fails the build on any difference between the built image and the cartridge
dump that is not on this list. A patch that changes nothing and a change that
was never declared are both build failures.

Each entry is anchored on a LINE NUMBER of rom/combat.asm plus the exact text
that line must contain. rom/combat.asm is the pristine 2002 disassembly and is
never edited, so the line numbers are stable -- and if one ever is not, the
`old` text check turns a silent mis-patch into a build error.

`addr` is where the patch lands in the stock ROM, for check_patch.py's byte
audit. `size` is 0 when the patch is the same length as what it replaces,
which every input-site patch is: `LDA SWCHA` is `ad 80 02` and `LDA NJOY` is
`ad cf 00` -- three bytes and four cycles either way. Nothing retimes.
"""

# Milestone 2: the bank split. Structural only -- the game still reads its own
# console, so a split build must play EXACTLY like stock.
STRUCTURAL = [
    dict(
        name="MLOOP: the lockstep gate, and the switch into the kernel bank",
        line=214, nlines=15, addr=0xF014, size=0,
        old="""MLOOP   JSR  VCNTRL             ; Generate a VSYNC and begin VBLANK
\t;
\t; VBLANK logic:
\t;
\tJSR  GSGRCK             ; Parse console switches
\tJSR  LDSTEL             ; Load Stella Registers
\tJSR  CHKSW              ; Check Joystick Switches
\tJSR  COLIS              ; Check Collision Registers
\tJSR  STPMPL             ; Setup Player, Missile Motion
\tJSR  ROT                ; Rotate Sprites
\tJSR  SCROT              ; Calculate Score Offsets
\t;
\tJSR  VOUT               ; do the Kernal (trashes the stack ptr,
\t                        ; but then restores it because it IS
\tJMP  MLOOP              ; used when we reiterate this loop)""",
        new="""MLOOP   JSR  VCNTRL             ; Generate a VSYNC and begin VBLANK
\t;
\t; VBLANK logic, now behind the lockstep gate.
\t;
\tJSR  CBSHIM             ; capture this console, and decide whether the
\t                        ;   sim advances at all this frame
\tLDA  CBADV
\tBEQ  MLSTALL
\t;
\t; INC CLOCK LIVES HERE NOW, not at the top of VCNTRL. All four things
\t; that read it -- the end-of-game score blink, the once-a-second
\t; SELECT debounce and GameTimer tick, ROT's player alternation and
\t; MisAge's missile ageing -- are in the chain below, so nothing reads
\t; it during a stall. But VCNTRL still runs during one, and a CLOCK
\t; that kept counting would leave two consoles that stalled for
\t; different numbers of frames with different values in it, and then
\t; diverge on all four. Moving one instruction makes CLOCK count TICKS
\t; RUN rather than frames drawn -- which is this console's form of the
\t; family's "the sim clock is not the frame", and it hands the lockstep
\t; a frame counter that agrees by construction.
\tINC  CLOCK
\tJSR  GSGRCK             ; Parse console switches
\tJSR  LDSTEL             ; Load Stella Registers
\tJSR  CHKSW              ; Check Joystick Switches
\tJSR  COLIS              ; Check Collision Registers
\tJSR  STPMPL             ; Setup Player, Missile Motion
\tJSR  ROT                ; Rotate Sprites
\tJMP  MLSCOR
\t;
\t; A stall reasserts the TIA and does nothing else. LDSTEL is a pure
\t; function of frozen state and costs 200 cycles there are, and it
\t; rewrites every register the picture needs that nothing else does --
\t; NUSIZ0/1, COLUPF, COLUBK.
MLSTALL\tJSR  LDSTEL
\t;
\t; SCROT RUNS ON EVERY FRAME, STALLED OR NOT. The kernel destroys
\t; SCROFF as it draws -- five INCs walking down the glyph -- and SCROT
\t; is what rebuilds it. Skip it on a stalled frame and the score draws
\t; garbage from the first stall onward. It is a pure function of SCORE,
\t; so running it always costs nothing and stays deterministic.
MLSCOR\tJSR  SCROT              ; Calculate Score Offsets
\t;
\tLDA  #BANKKERN          ; the kernel lives in its own bank now: a
\tJMP  CBGOTO             ;   switch is a JUMP, never a call, because the
\t                        ;   store that switches is the last instruction
\t                        ;   fetched from this bank""",
    ),
    dict(
        name="VCNTRL: CLOCK is not a frame counter any more",
        line=236, addr=0xF032, size=-3,
        old="VCNTRL  INC  CLOCK            ; Master frame count timer",
        new="VCNTRL\t;                     ; (INC CLOCK moved into MLOOP, behind\n"
            "\t;                     ;  the stall gate -- see the note there)",
    ),
    dict(
        name="VOUT: step the network machine while the vertical blank runs out",
        line=265, nlines=2, addr=0xF05C, size=0,
        old="VOUT_VB\tLDA  INTIM\n\tBNE  VOUT_VB            ; Wait for INTIM to time-out.",
        new="""; EXACTLY THE FIVE BYTES `LDA INTIM / BNE` TOOK, and that is the point.
\t; This is the only slack in a frame with no overscan, so the network
\t; machine has to be stepped from here -- but the kernel below is
\t; hand-cycled, and shifting it by even one byte would move every
\t; address in it and leave check_patch with nothing left to audit.
\t; CBWAIT does the wait AND the stepping, in three bytes and two NOPs.
VOUT_VB\tJSR  CBWAIT
\tNOP
\tNOP""",
    ),
    dict(
        name="VOUT: return to the game bank instead of RTS",
        line=449, addr=0xF156, size=+3,
        old="\tRTS",
        new="\tSTA  WSYNC              ; anchor the switch on a line boundary:\n"
            "\t;                       ;   without it the frame silently gains a\n"
            "\t;                       ;   scanline on some frames\n"
            "\tLDA  #BANKGAME\n"
            "\tJMP  CBGOTO",
    ),
    dict(
        name="bank entry: cold-start Combat, or resume the frame loop",
        line=203, addr=0xF000, size=+7,
        old="START\tSEI                     ; Disable interrupts",
        new="; The kernel bank returns here at the end of every frame, and the boot\n"
            "; bank comes here once when a match starts. CBENT tells them apart: the\n"
            "; cold stub in the fixed tail zeroes it, and START sets it before it\n"
            "; falls into MLOOP. This console does not clear its RAM on a reset, so a\n"
            "; bank that dispatched on an uninitialised cell would come back into the\n"
            "; warm path with none of the state the warm path assumes.\n"
            "CBGENT\tLDA  CBENT\n"
            "\tAND  #CBE_WARM\n"
            "\tBNE  MLOOP\n"
            "START\tSEI                     ; Disable interrupts",
    ),
    dict(
        name="START: mark the bank warm before falling into MLOOP",
        line=212, addr=0xF011, size=+5,
        old="\tJSR  ClrGam\t\t; clear game RAM $82-$A2",
        new="\tJSR  ClrGam\t\t; clear game RAM $82-$A2\n"
            "\tLDA  CBENT              ; from here on, an entry to this bank is a\n"
            "\tORA  #CBE_WARM          ;   frame boundary and not a cold start.\n"
            "\tSTA  CBENT              ;   ORA, not LDA: the boot bank set the\n"
            "\t;                       ;   networked bit and it must survive.",
    ),
    dict(
        name="START: clear all 128 bytes, not just $00-$A2",
        line=207, addr=0xF005, size=+3,
        old="\tLDX  #$5D",
        new="\t; ClearMem stops at $A2, so $A3-$FF is power-on garbage -- and\n"
            "\t; that is not one curiosity but five: MPace's pacing phase, the\n"
            "\t; COLcount debounce, GameTimer, and above all HIRES, which ROT\n"
            "\t; fills only eight bytes of per frame. Frame one draws garbage\n"
            "\t; sprites, and sprites feed GRP0/GRP1, which feed the COLLISION\n"
            "\t; LATCHES. On one console that is a cosmetic first frame; on two\n"
            "\t; it is a frame-one desync.\n"
            "; $80,X with X counting DOWN from $7F, not $00,X counting up.\n"
            "; Zero-page indexed wraps inside page zero, so STA $00,X over X =\n"
            "; 0..255 writes the TIA's whole write-register file on the way past\n"
            "; -- which STROBES WSYNC, RESP0 and RESP1. The first costs a\n"
            "; scanline; the other two are raster-position strobes and set the\n"
            "; tanks' starting columns from wherever the beam happened to be.\n"
            "; Combat clears the TIA deliberately a few instructions below, by\n"
            "; the same wrap, and that is the right place for it.\n"
            "; It stops at $E5, not $FF, and the stopping point is load-bearing:\n"
            "; $E6 upwards is the netcode's, and the boot bank has already put\n"
            "; the role, the tick, the watermark and the networked bit there\n"
            "; before it handed over. A clear that ran to $FF wiped all of it,\n"
            "; and the symptom was a match that paired, transported cleanly and\n"
            "; never advanced a single tick, because the game had quietly fallen\n"
            "; back to reading its own joysticks.\n"
            "\tLDA  #$00\n"
            "\tLDX  #$65\n"
            "CBCLR\tSTA  $80,X              ; $80+$65 = $E5 down to $80+0 = $80\n"
            "\tDEX\n"
            "\tBPL  CBCLR\n"
            "\tLDX  #$5D",
    ),
]

# Address ranges of the STOCK rom that the build is allowed to differ from
# wholesale, because they have been restructured rather than edited. Keep this
# list as short as the work allows: every byte in it is a byte check_patch.py
# is no longer checking.
REWRITTEN = [
    (0xF000, 0xF054, "the frame loop: the bank entry, the full RAM clear, and "
                     "the switch into the kernel bank"),
]

# Single sites that change in place. `span` is how many STOCK bytes the patch
# may alter, starting at `addr`.
SPANS = [
    (0xF05C, 5, "VOUT's vblank spin becomes the network machine's step loop"),
    (0xF156, 1, "VOUT's RTS becomes the switch back to the game bank"),
    (0xF157, 3, "GSGRCK's SWCHB read becomes the switch shadow"),
    (0xF192, 3, "ChkSel's SWCHB read becomes the switch shadow"),
    (0xF30E, 3, "CHKSW's SWCHB read becomes the switch shadow"),
    (0xF313, 3, "player 1's SWCHA read becomes the synthetic stick"),
    (0xF36E, 3, "player 0's SWCHA read becomes the synthetic stick"),
    (0xF3BB, 2, "the trigger read becomes the trigger shadows"),
    (0xF59C, 3, "LDSTEL's SWCHB read becomes the switch shadow"),
    (0xF7F4, 8, "the eight bytes of $FF filler the 1977 cartridge left between "
                "its last table and its reset vector now hold CBPHI, the "
                "frame-in-tick phase -- the only free space Combat ever had, "
                "and exactly the size of the routine"),
]

# Milestone 3: the input surface. Seven read sites, proved complete by
# emu/inputs.lua rather than merely believed from the disassembly, and every one
# of them the same length and the same cycle count as what it replaces:
# `LDA SWCHA` is `ad 80 02` and `LDA >CBJOY` is `ad cf 00`. The `>` is not
# decoration -- without it AS folds a zero-page operand into the two-byte form,
# every byte after it shifts, and the patch audit has nothing left to audit.
INPUTS = [
    dict(name="GSGRCK: the RESET switch comes from the agreed shadow",
         line=459, addr=0xF157, size=0,
         old="\tLDA  SWCHB              ; Start/Reset button....",
         new="\tLDA  >CBSWB             ; Start/Reset, from the agreed shadow"),
    dict(name="ChkSel: the SELECT switch comes from the agreed shadow",
         line=507, addr=0xF192, size=0,
         old="ChkSel  LDA  SWCHB              ; Select button???",
         new="ChkSel  LDA  >CBSWB             ; Select, from the agreed shadow"),
    dict(name="CHKSW: the difficulty switches come from the agreed shadow",
         line=828, addr=0xF30E, size=0,
         old="\tLDA  SWCHB             ; Console switches.",
         new="\tLDA  >CBSWB            ; Console switches, agreed: bit 7 is not\n"
             "\t;                      ;   cosmetic, it changes the physics"),
    dict(name="CHKSW: player 1's stick",
         line=833, addr=0xF313, size=0,
         old="\tLDA  SWCHA             ; Joysticks. Before we return via",
         new="\tLDA  >CBJOY            ; the synthetic stick, both nibbles"),
    dict(name="CHKSW: player 0's stick -- NOT left reading the local port",
         line=909, addr=0xF36E, size=0,
         old="\tLDA  SWCHA              ; Joysticks..",
         new="\tLDA  >CBJOY             ; the SAME synthetic stick. In lockstep\n"
             "\t;                       ;   both inputs are applied at the same\n"
             "\t;                       ;   tick on both consoles; a console that\n"
             "\t;                       ;   read its own port live would be running\n"
             "\t;                       ;   one player at delay 0 and the other at\n"
             "\t;                       ;   delay d, and would desync on the first\n"
             "\t;                       ;   stick movement"),
    dict(name="ChkVM: the triggers",
         line=961, addr=0xF3BB, size=0,
         old="\tLDA  INPT4,X            ; Read Input (Trigger) X.",
         new="\tLDA  CBTRIG,X           ; the trigger shadows. Zero page indexed\n"
             "\t;                       ;   either way: INPT4 is $0C in this\n"
             "\t;                       ;   source, not the $30-$3D mirror"),
    dict(name="LDSTEL: the B&W switch",
         line=1341, addr=0xF59C, size=0,
         old="\tLDA  SWCHB",
         new="\tLDA  >CBSWB             ; B&W rides in the shadow too, but it is\n"
             "\t;                       ;   filled from the LOCAL switch: colour\n"
             "\t;                       ;   never reaches physics")
]

ALL = STRUCTURAL + INPUTS


def apply(lines, patches=ALL):
    """Apply the declared patches to rom/combat.asm's lines (1-based).

    A patch replaces one line by default, or `nlines` of them: MLOOP is a block
    and replacing it a line at a time would be a worse record of what changed
    than replacing it whole.
    """
    out = list(lines)
    for p in sorted(patches, key=lambda q: -q["line"]):
        i = p["line"] - 1
        n = p.get("nlines", 1)
        if n > 1:
            have = "\n".join(l.rstrip() for l in out[i:i + n])
            if have != p["old"].rstrip():
                raise SystemExit(
                    "patches: lines %d-%d are not what %r expects.\n"
                    "  want: %r\n  have: %r"
                    % (p["line"], p["line"] + n - 1, p["name"], p["old"], have))
            # The LINE COUNT IS PRESERVED, deliberately. tools/mkbanks.py maps
            # every source line to the address the assembler put it at, using
            # the listing of the UNPATCHED source, and then walks the patched
            # lines by the same index. A patch that collapsed fifteen lines
            # into one would shift every line after it and silently file half
            # the game into the wrong bank.
            out[i] = p["new"] + "\n"
            out[i + 1:i + n] = ["\n"] * (n - 1)
            continue
        # Trailing whitespace is stripped from both sides: the disassembly has
        # a fair amount of it and an anchor that failed on an invisible space
        # would be a maddening build error, not a useful one.
        have = out[i].rstrip()
        if have != p["old"].rstrip():
            raise SystemExit(
                "patches: line %d is not what %r expects.\n  want: %r\n  have: %r"
                % (p["line"], p["name"], p["old"], have))
        out[i] = p["new"] + "\n"
    return out
