; cbphi.inc -- the frame-in-tick phase, in the eight bytes at the very top of
; the game bank.
;
; Those eight bytes are the gap the 1977 cartridge left between its last table
; and its reset vector -- the only free space Combat ever had. CBPHI is exactly
; eight bytes long, which is the sort of coincidence this bank has to live on:
; four holes, none of them big enough for everything, and a routine placed in
; each by size.

; ---------------------------------------------------------------------------
; CBPHI -- advance the frame-in-tick phase, and return.
;
; FOUR BYTES OF WORK. The phase is the top two bits of CBENT, so adding $40
; increments it and the carry out of bit 7 is simply discarded -- the low bits,
; which are the entry and role flags, cannot be disturbed because nothing
; carries into them. Masking would be three instructions of ceremony.
;
; Called on every frame whose logic will run, and on NO stalled frame: a stall
; leaves the phase at 0 so the next frame is the same boundary again, which is
; exactly what retrying a tick means.
CBPHI:
        lda     CBENT
        clc
        adc     #$40
        sta     CBENT
        rts

