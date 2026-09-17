; cbgame.asm -- bank 1: Combat's frame loop and its seven game routines.
;
; Everything Combat does between VSYNC and the kernel. The kernel itself is in
; bank 2, because Combat is exactly 2048 bytes and a bank is exactly 2048
; bytes, so something had to move to make room for netcode -- and the kernel is
; the half with no netcode in it.
;
; Every byte here keeps the address it has in the cartridge dump, rebased
; $F000 -> $1000. The 259-byte hole at $1054-$1156, where VOUT used to be, is
; where the lockstep gate goes.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfg.inc"
        INCLUDE "cbdefs.inc"
; The shared transport's addresses, generated from the tail's own listing so
; there is no hand-kept list to go stale.
        INCLUDE "tail.inc"

CBBANK  EQU     BANKGAME

        INCLUDE "combat.inc"

; ---------------------------------------------------------------------------
; The hole where VOUT used to be: 243 bytes at the end of the frame loop,
; before the seven game routines start at their pinned $1157. CBRE0 is the end
; of the region the generator just emitted, so this cannot go stale when a
; patch changes the frame loop's length.
        ORG     CBRE0
        INCLUDE "cbinput.inc"

        IF      * > $1157
        ERROR   "the frame loop and the input shim have overrun GSGRCK at $1157"
        ENDIF

; ---------------------------------------------------------------------------
; The second hole: the 80 bytes where the playfield maps used to be, which are
; the kernel bank's now.
        ORG     CBRE4
        INCLUDE "cbcap.inc"

        IF      * > $17C6
        ERROR   "the capture routine has overrun SPRLO at $17C6"
        ENDIF

; ---------------------------------------------------------------------------
; The third hole: the 50 bytes where the score digits used to be. They are the
; kernel bank's now, and it is the only bank that draws a score.
        ORG     $15C5
        INCLUDE "cbcrc.inc"

        IF      * > $15F7
        ERROR   "the checksum has overrun Xoffsets at $15F7"
        ENDIF

; ---------------------------------------------------------------------------
; The fourth hole: the eight bytes the 1977 cartridge left between its last
; table and its reset vector. CBPHI is exactly eight bytes.
        ORG     $17F4
        INCLUDE "cbphi.inc"

        IF      * > $17FC
        ERROR   "the phase routine has overrun the vectors at $17FC"
        ENDIF

        END
