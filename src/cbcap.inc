; cbcap.inc -- this console's own half of the stick, packed into one wire byte.
;
; Split out of cbinput.inc only because bank 1's first hole is 246 bytes and
; the shim wanted 248. It lives in the 80 bytes the playfield maps vacated when
; they moved to the kernel bank.

; ---------------------------------------------------------------------------
; CBCAP -- this console's own half of the stick, packed into one wire byte.
;
; Which half depends on the role, and this is the only place the role decides
; anything about the local hardware: a host reads the left stick and a guest
; the right, so both players use joystick 1 on their own console and neither
; has to plug into port 2 to play.
CBCAP:
; THE LEFT PORT, ON BOTH CONSOLES, WHATEVER THE ROLE.
;
; This read used to depend on the role -- a host took SWCHA's high nibble and a
; guest its low one -- on the reasoning that the guest "is player 1". That is
; true of the SIMULATION and false of the FURNITURE: each player sits at their
; own console with one joystick in port 1, and a guest who had to plug into
; port 2 to drive their own tank would be right about nothing except the bit
; numbering. It is the kind of mistake that is invisible in a rig, where
; nobody's hands are on anything, and obvious the moment two people play.
;
; The role decides which TANK this byte drives, and that is settled by the swap
; in the gate, where it belongs. Here there is no role at all.
        lda     SWCHA           ; bits 7-4: the LEFT port, joystick 1
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     CBTMP
        lda     INPT4           ; and its trigger
        and     #IW_TRIG        ; bit 7, straight through
        ora     CBTMP
        sta     CBTMP

;
; RESET and SELECT, SHIFTED UP FROM BITS 0 AND 1 TO BITS 4 AND 5.
;
; That shift is the whole of a bug worth remembering. SWCHB puts RESET at bit 0
; and SELECT at bit 1; the wire byte puts them at 4 and 5, because 0-3 are the
; stick. Masking SWCHB with #$30 and calling it done reads the console's UNUSED
; bits 4 and 5 instead -- which read high on real hardware and in MAME, so the
; wire said "not pressed" for ever and NEITHER SWITCH EVER REACHED THE OTHER
; CONSOLE. The mixer at the far end shifts them back down correctly, so both
; halves looked right in isolation and the pair was silently broken.
;
; It survived every gate because no gate pressed anything: the rig's verdict
; checked that the two consoles AGREED, and two consoles that both ignore
; SELECT agree perfectly.
;
; SWCHB IS READ ONCE AND PARKED ON THE STACK, not read twice. Two `LDA SWCHB`
; are six bytes; one plus a PHA/PLA is five, and this hole has to end at $17C6
; exactly. The stack can afford it: the deepest this path ever goes is MLOOP ->
; CBSHIM -> CBCAP, which is four bytes of the six between $FA and $FF.
        lda     SWCHB
        pha
        and     #$03            ; bit 0 RESET, bit 1 SELECT, both active low
        asl     a
        asl     a
        asl     a
        asl     a               ; -> IW_RST and IW_SEL
        ora     CBTMP
        sta     CBTMP
        pla
        and     #$40            ; SWCHB bit 6: the LEFT player's difficulty
        ora     CBTMP
; The desync repair is a synthetic RESET pressed into this byte, and it lives in
; the score-digit hole because this one ends here. See CBRSY in cbcrc.inc.
        jmp     CBRSY

; ---------------------------------------------------------------------------
; ---------------------------------------------------------------------------
; CBSTALL -- the peer's input for this tick has not arrived.
;
; Nothing else happens. The frame still runs: VCNTRL still puts out VSYNC,
; LDSTEL still reasserts the TIA, SCROT still rebuilds the score offsets the
; kernel destroys, and the kernel still draws. What does not happen is the game
; logic, so the picture freezes coherently and the display never shudders.
;
; That is nearly free on this console, and the collision latches are why: VOUT
; strobes CXCLR every frame and the kernel refills them from unchanged state, so
; N frozen frames leave exactly what one would. Two consoles that stalled for
; different lengths resume in agreement.
;
; The stall count lives in CBERR's high nibble, saturating. A stall is normal;
; fifteen in a row at four frames each is a second, and that is a peer that has
; gone. Saturation is the ONE "the peer is gone" condition: a clean leave
; arrives as a PEER_LEFT frame and the transport saturates this on the spot, and
; a console that was unplugged saturates it by simply not arriving.
CBSTALL:
        lda     #0
        sta     CBADV
        lda     CBERR
        cmp     #$F0            ; the low nibble is an error code below $10, so
        bcc     CBSTA1          ;   this is exactly "high nibble is already $F"
        jmp     CBGONE
CBSTA1: clc
        adc     #$10            ; +1 in the high nibble, low nibble untouched
        sta     CBERR
        rts

; ---------------------------------------------------------------------------
; Not in a match: the console drives both tanks, exactly as it always did.
; It lives here because the frame loop's hole is full to the byte.
;
; This path is byte-for-byte the behaviour `make det` compares against stock,
; and it is why the shim went in before the network did: a build that plays
; differently here would make every later difference ambiguous.
CBLOC0:
        lda     SWCHA           ; both sticks in one byte: player 0 in bits
        sta     CBJOY           ;   7-4, player 1 in 3-0, and Combat pulls them
                                ;   apart itself
        lda     INPT4           ; the triggers are separate ports but Combat
        sta     CBTRIG          ;   reads them as one indexed pair, so the
        lda     INPT5           ;   shadows have to be a pair in the same order
        sta     CBTRIG+1
        lda     SWCHB
        sta     CBSWB
        rts


