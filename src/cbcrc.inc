; cbcrc.inc -- the sim checksum, in the 50 bytes the score digits left behind.
;
; Bank 1 has four holes and no one of them is big enough for everything, so the
; netcode's game-side half is laid out to fit them: the gate in the 246 bytes
; where VOUT was, the capture in the 80 where the playfield maps were, and this
; in the 50 where NUMBERS was. tools/checkbanks.py and the guards in
; cbgame.asm are what stop a routine growing quietly into the next region.

; ---------------------------------------------------------------------------
; CBCRC -- a checksum of the sim state, in A.
;
; Twelve bytes: THE GAME VARIATION, the four bearings, the two missile
; lifetimes and the four Y positions.
;
; A checksum is only worth what it covers, and this one has now been widened
; twice for that reason. MisLife went in after two consoles that had each fired
; only their own missile agreed on every byte it covered. BINvar and GAMVAR went
; in after two consoles were photographed playing GAME 16 AND GAME 19 -- a
; biplane game against a jet game, different playfields, different colours --
; while the relay verified sixty thousand checksums without a single mismatch.
;
; The lesson generalises: put the cells in it that decide WHAT IS BEING
; SIMULATED, not only the ones that move while it runs.
;
; What is NOT in it matters as much. The colour cells are out because the B&W
; switch is deliberately local and colour never reaches physics. The kernel's
; pointers are out because their high bytes differ between a $F000 image and a
; cartridge bank. HIRES and SCROFF are out because they are rebuilt from the
; state every frame rather than being it. Tank X is not in RAM at all -- it
; lives in the TIA's HMP0/HMP1 and is applied incrementally by HMOVE -- so no
; checksum on this console can see it, and XOFFS is the nearest thing there is.
CBCRC:
        lda     BINvar          ; which of the 27 games this console thinks it
        asl     a               ;   is playing, and the decoded feature bits
        adc     GAMVAR
        ldx     #5              ; DIRECTN[4] and MisLife[2] are contiguous:
CBCRC1: asl     a               ;   $95-$98 and $99-$9A
        adc     DIRECTN,x
        dex
        bpl     CBCRC1
        ldx     #3
CBCRC2: asl     a
        adc     TankY0,x
        dex
        bpl     CBCRC2
        rts

; ---------------------------------------------------------------------------
; CBRSY -- the desync repair, pressed into this console's own wire byte.
;
; Entered by JMP from the end of CBCAP, with the finished byte in A, and it
; returns for CBCAP. It is here rather than there only because the capture hole
; ends at $17C6 and this is seventeen bytes; the score-digit hole had exactly
; seventeen left. That is the character of this bank and it is why every hole
; has a guard on it.
;
; A repair has to happen at the SAME TICK on both consoles or it is just a
; second desync. There is no spare byte of zero page to schedule one in, and
; there does not need to be: RESET is already an event the two machines agree
; about to the tick, because it is ANDed on the wire and applied through the
; delay ring like any other input. So the repair is a synthetic press and it
; arrives at both ends by the road every real press takes -- the one `make rig`
; has been pressing RESET across for its whole life.
;
; One console arming it is enough, for the same reason either player may start a
; game. Both usually do arm, because each one sees the other's checksum.
;
; One-shot: the bit is cleared as the press is made, so it lasts exactly one
; tick. That is four frames and GSGRCK acts on the first of them.
CBRSY:
        tay                     ; the byte parks in Y, because the only way to
                                ;   test a bit of CBENT is through A
        lda     CBENT
        and     #CBE_RSY
        beq     CBRSY9
        eor     CBENT           ; A was exactly the bit, so this is CBENT with
        sta     CBENT           ;   it cleared -- one-shot, in two instructions
        tya
        and     #$FF-IW_RST     ; RESET is active low, so clearing the bit IS
        rts                     ;   the press
CBRSY9: tya
        rts

; ---------------------------------------------------------------------------
; CBGONE -- the peer is gone: hand back to the boot bank, which is the only
; bank with a text kernel in it, and let it say so in words.
;
; A JUMP, not a call: the trampoline resets the stack, so the JSR that reached
; the gate is abandoned on purpose.
;
; It lives here rather than beside the stall counter that branches to it
; because the frame loop's hole ran out four bytes short -- which is the whole
; character of this bank. There are four holes in it and none of them is big
; enough for everything.
CBGONE:
        lda     CBENT
        ora     #CBE_LEFT
        sta     CBENT
        lda     #BANKBOOT
        jmp     CBGOTO

