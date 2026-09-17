; cbboot.asm -- bank 0: the cold start and the session.
;
; For now it does the smallest thing that makes the split testable: hand
; straight over to the game bank, so a built image plays exactly like stock
; Combat and any difference is the bank split's fault and nothing else's.
;
; It grows into the session: the shared username out of the appkey, the server
; URL the Lobby left in another one, the N:TCP open, HELLO, and the WAITING /
; PAIRED / peer-left screens.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "cbdefs.inc"
; The shared transport's addresses, generated from the tail's own listing so
; there is no hand-kept list to go stale.
        INCLUDE "tail.inc"

CBBANK  EQU     BANKBOOT

        ORG     $1000

; The boot bank is entered twice: once at power-on, and once more if the peer
; leaves -- because this is the only bank with a text kernel in it, and
; "OPPONENT HAS LEFT" is worth saying in words.
CBBOOT: lda     CBENT
        and     #CBE_LEFT
        beq     CBBENT
        jmp     CSLEFT

        INCLUDE "cbsess.inc"
        INCLUDE "cbappk.inc"
        INCLUDE "cbdisp.inc"

        IF      * > $1800
        ERROR   "the boot bank has overflowed"
        ENDIF

        END
