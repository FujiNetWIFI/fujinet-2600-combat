; cbinput.inc -- the console capture, the lockstep gate, and the shadows.
;
; Combat's whole input surface is seven reads, and emu/inputs.lua proves it
; rather than assuming it: after the patch, every read of a console port in a
; whole run comes from in here. This is what fills the cells those seven sites
; now read.
;
; It runs from MLOOP, before anything else, inside the timed vertical blank
; where there are 2752 cycles to spend. Its other job is to decide whether the
; sim advances at all this frame, which is the lockstep gate.

        IF      CBLAG
; ---------------------------------------------------------------------------
; The interception proof (`make lag`): refresh the shadows once every CBLAGM+1
; frames. Combat then answers the stick in visible steps about a second apart
; -- and if it does not, something is still reading a live port.
CBLAGM  EQU     $3F
        ENDIF

; ---------------------------------------------------------------------------
; CBSHIM -- once a frame, from MLOOP.
;
; Returns with CBADV nonzero if the game logic should run this frame.
CBSHIM:
        lda     #$FF
        sta     CBADV           ; assume the sim advances

        IF      CBLAG
; CLOCK is incremented in MLOOP behind the gate, so the first frame through
; here sees 0 -- and the shadows' power-on value of $00 reads as EVERY console
; switch pressed, because SWCHB is active low.
        lda     CLOCK
        and     #CBLAGM
        beq     CBSHIM1
        rts
CBSHIM1:
        ENDIF

        lda     CBENT
        and     #CBE_NET
        bne     CBNET

; Not in a match: the console drives both tanks, exactly as it always did.
; The code is in the next hole along -- this one is full to the byte.
        jmp     CBLOC0

; ---------------------------------------------------------------------------
; In a match.
CBNET:
; Only a tick boundary has anything to decide; the other three frames of a tick
; re-use the shadows that are already there. The phase is two bits of CBENT and
; NOT `CLOCK AND 3` -- see the note on CBE_PH.
        lda     CBENT
        and     #CBE_PH
        beq     CBNT1
        jmp     CBPHI           ; mid-tick: bump the phase and run

CBNT1:
; A RECORD, ONCE SENT, IS IMMUTABLE. Everything below happens ONLY on a
; boundary that actually advanced the tick.
;
; This is the counterpart to "sample at every boundary", and getting one right
; broke the other. The transport free-runs: it sends the record for CBTICK+d
; whenever it reaches its WRITE step, several times per tick, and a duplicate is
; harmless only because it is IDENTICAL. Re-capturing on a stalled boundary made
; it not identical -- the peer had already stored and SIMULATED the first
; version of tick X while this console went on to use the second.
;
; It is invisible while nobody touches anything, because every capture returns
; the same idle byte. The moment somebody presses SELECT during a stall, one
; console counts the press and the other does not, and two consoles end up on
; different game variations -- 13 against 22, with the screens to prove it.
;
; So: advance, sample and capture together, or not at all.
        lda     CBENT
        and     #CBE_RAN
        bne     CBNT1A
        jmp     CBNT1B          ; the tick has not moved: nothing to re-commit
CBNT1A:
        inc     CBTICK
        lda     CBENT
        and     #$FF-CBE_RAN
        sta     CBENT

; The checksum, of the state ENTERING the tick just advanced to. The boundary is
; the one instant two consoles can agree on: Combat's logic runs on every frame
; and only the input changes at the tick rate, so the state moves four times a
; tick and a checksum taken wherever the transport got to is a different
; quantity on each machine. The record stamped Y carries the checksum of Y-d, on
; both, whatever either stalled through.
        jsr     CBCRC
        sta     CBCRCV

; And this console's own stick, for the tick d ahead. Captured once, here, and
; never revised.
        jsr     CBCAP
        pha
        lda     CBTICK
        clc
        adc     #CBD
        and     #CBLM
        tay
        pla
        sta     CBLOC,y
CBNT1B:

; Has the peer's input for this tick arrived? CBRWAT is the highest remote tick
; received.
;
; THIS IS THE ONE SIGNED COMPARE IN THE NETCODE, and it is signed on purpose:
; the tick is eight bits and wraps, and a window of +-127 either side is what
; makes the wrap invisible. Every other comparison here goes through the carry,
; because the Channel F port's settle loop used the sign on an unsigned
; quantity and 181 - 1 came out negative.
        lda     CBRWAT
        sec
        sbc     CBTICK
        bpl     CBNT2

; ---- a stall ----
; The body is in the next hole along; this one is full to the byte.
        jmp     CBSTALL

CBNT2:
; ---- the tick runs ----
        lda     CBERR           ; a tick that ran clears the stall counter
        and     #$0F
        sta     CBERR

; Our own byte for this tick was captured d ticks ago and has been sitting in
; the local ring ever since. That is the whole of delay-based lockstep: our
; input is delayed exactly as far as the peer's, so both players feel the same
; lag and neither is ahead of the other.
        lda     CBTICK
        and     #CBLM
        tay
        lda     CBLOC,y
        sta     CBW0

; The peer's byte for the same tick, out of the remote ring.
        lda     CBTICK
        and     #7
        tay
        lda     CBRING,y
        sta     CBW1

; The host drives the LEFT tank, which is Combat's player 0. A guest's own byte
; is therefore player 1's and the two have to change places.
        lda     CBENT
        and     #CBE_ROLE
        beq     CBMIX
        lda     CBW0
        ldy     CBW1
        sty     CBW0
        sta     CBW1

; ---------------------------------------------------------------------------
; CBMIX -- two wire bytes into the three cells Combat reads.
;
; Every bit keeps the hardware's sense on the wire, so this is masks and shifts
; and never an inversion -- which matters more than it sounds, because an
; inversion applied on one console and not the other is a desync that only
; shows when somebody presses something.
CBMIX:
; SWCHA: player 0 in bits 7-4, player 1 in bits 3-0.
        lda     CBW0
        asl     a
        asl     a
        asl     a
        asl     a
        sta     CBTMP
        lda     CBW1
        and     #IW_STICK
        ora     CBTMP
        sta     CBJOY

; The triggers: bit 7 of each byte, straight through, in the pair Combat
; indexes with the player number.
        lda     CBW0
        and     #IW_TRIG
        sta     CBTRIG
        lda     CBW1
        and     #IW_TRIG
        sta     CBTRIG+1

; SWCHB. RESET and SELECT are ANDed: both bits are active low, so the AND means
; EITHER PLAYER MAY PRESS THEM -- which is the couch semantics, preserved.
; There is no host authority here and none is needed: every bit is a pure
; function of the two wire bytes, so both consoles compute the same byte
; without a protocol for it.
        lda     CBW0
        and     CBW1
        lsr     a               ; IW_RST is bit 4 and SWCHB's reset is bit 0,
        lsr     a               ;   IW_SEL is bit 5 and select is bit 1
        lsr     a
        lsr     a
        and     #$03
; SELECT ONLY BETWEEN GAMES. Masked here rather than patched into ChkSel,
; because this is a decision about netplay and not about Combat -- and because
; the mask is a pure function of GameOn, which is sim state and therefore
; already agreed. Bit 1 forced high reads as "not pressed".
        ldy     GameOn
        bpl     CBMIX1          ; bit 7 clear: attract mode, SELECT is allowed
        ora     #$02
CBMIX1: sta     CBTMP
; B&W stays LOCAL, and it is provably safe: LDSTEL's B&W path reaches only the
; colour cells, and no colour cell reaches physics. Each player sees their own
; set in whichever they chose.
        lda     SWCHB
        and     #$08
        ora     CBTMP
        ora     #$34            ; bits 2, 4 and 5 read high on a real console
        sta     CBTMP
; The difficulty switches, uncrossed: SWCHB bit 6 is the LEFT player's and bit
; 7 the right's, and Combat reads them that way round -- CHKSW starts at X=1
; and tests bit 7, then shifts bit 6 up for player 0. Bit 7 is not cosmetic: it
; is tested at $F354 and changes how a tank accelerates.
        lda     CBW0
        and     #IW_DIFF
        ora     CBTMP
        sta     CBTMP
        lda     CBW1
        and     #IW_DIFF
        asl     a
        ora     CBTMP
        sta     CBSWB

; Mark the tick as run -- the next boundary does the increment, immediately
; before it samples -- and bump the phase, because this frame's logic will run.
        lda     CBENT
        ora     #CBE_RAN
        sta     CBENT
        jmp     CBPHI

; CBCAP itself lives in the next hole along -- see src/cbcap.inc. It is here in
; spirit and there in fact because this hole is 246 bytes and the shim wanted
; 248: the frame loop and the gate fill the space VOUT left behind almost
; exactly, and the 80 bytes the playfield maps left in the kernel bank are the
; nearest place to put the overflow.
