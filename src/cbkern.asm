; cbkern.asm -- bank 2: Combat's display kernel, and the netcode.
;
; The kernel is 259 bytes and its data another 129, so this bank has about
; 1650 spare -- which is why the whole network state machine lives here. The
; mailbox at $1D00-$1FFF is fixed and visible from every bank, so the netcode
; does not care which bank it runs in; it should run where the room is.
;
; The machine is stepped from inside VOUT's wait for the vertical blank to run
; out, which is the only slack in a frame that has no overscan at all.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "cbdefs.inc"
; The shared transport's addresses, generated from the tail's own listing so
; there is no hand-kept list to go stale.
        INCLUDE "tail.inc"

CBBANK  EQU     BANKKERN

; ---------------------------------------------------------------------------
; The entry. The trampoline enters every bank at $1000, and VOUT is pinned at
; $1054, so this is the 84 bytes in front of it.
        ORG     $1000

; CLD is insurance, not ceremony: Combat runs SED in SelGO and COLDET and
; closes each with CLD, but both are in the game bank, and an ADC in the net
; machine that ran in decimal mode would be a desync that only showed up after
; a score.
CBKENT: cld
        jmp     VOUT

        INCLUDE "combat.inc"

; ---------------------------------------------------------------------------
; The netcode, in the 1134 bytes between the kernel and the score digits.
; CBRE1 is the end of the region the generator just emitted, so this cannot go
; stale when a patch changes the kernel's length.
        ORG     CBRE1
        INCLUDE "cbnet.inc"

        IF      * > $15C5
        ERROR   "the network machine has overrun NUMBERS at $15C5"
        ENDIF

        END
