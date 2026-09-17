; cbdefs.inc -- zero page, banks and constants for networked Combat.
;
; EQUATES ONLY. Included BEFORE the ORG, like fujinet.inc. The code half is
; cbcore.inc and goes inside it.

; ---------------------------------------------------------------------------
; ZERO PAGE
;
; This console has 128 bytes of RAM and Combat already uses $80-$E4 of them,
; with the stack in $FA-$FF (its deepest call is three: MLOOP -> CHKSW ->
; ChkVM -> MOTORS). Scanning every zero-page operand in the original
; $F000-$F5C4 leaves exactly twenty-six bytes untouched by any addressing mode
; in the game:
;
;       $8F $90                 "seem to be reserved (missiles?) but not used"
;       $CD $CE $CF $D0         above HIRES, which is provably bounded to 0..15
;       $E6-$F9                 above COLcount, below the stack
;
; Twenty-six is not enough for a transport, a lockstep engine and a resync
; applier, so five of them are a UNION. The three path-buffer cells and the two
; input-scan cells belong to the boot bank, which runs the session -- the
; username appkey, the server URL, the WAITING screen -- and finishes before
; the game starts; the lockstep delay rings belong to the game, which cannot
; run until it has. The two sets are never live at the same time, and the only
; thing that could make them so is a cold start from inside a match, which
; empties everything anyway.
;
; The one hazard to remember: ClrGam clears $82-$A2, so $8F and $90 are wiped
; by a SELECT press. Nothing there may need to survive one -- both cells are
; recomputed every frame.

; ---------------- the input shadows Combat itself reads ----------------
; These ARE the patch: Combat reads them where it used to read the hardware.
; They sit above $A2, so ClearMem and ClrGam never touch them -- which matters,
; because SELECT runs ClrGam in the middle of a match.
CBTRIG  EQU     $CD             ; 2: triggers by player, CBTRIG,X -- patch at $F3BC
CBJOY   EQU     $CF             ; the synthetic SWCHA  -- patches at $F313, $F36E
CBSWB   EQU     $D0             ; the synthetic SWCHB  -- patches at $F157, $F192,
                                ;   $F30E and $F59C

; ---------------- the netcode, $E6-$F9 ----------------
CBENT   EQU     $E6             ; bank entry code, and the net machine's state.
                                ;   Bit 7 set means "the boot bank owns this
                                ;   frame"; $00 means "cold-start Combat".
CBSEQ   EQU     $E7             ; the sequence we are waiting for
CBERR   EQU     $E8             ; low nibble last transport error, high nibble
                                ;   consecutive stalled ticks -- the watchdog
                                ;   needs no cell of its own
CBTICK  EQU     $E9             ; sim tick, low 8 bits
CBNST   EQU     $EA             ; the network machine's state.
                                ;   There is no "frames left in this tick"
                                ;   cell: the tick is four frames and a
                                ;   boundary is CLOCK AND 3, which is why four
                                ;   and not the three the probe would allow.
CBCRCV  EQU     $EB             ; the sim checksum, SAMPLED AT A TICK BOUNDARY
                                ;   and always of the state ENTERING CBTICK.
                                ;   It cannot be sampled where it is sent: the
                                ;   transport free-runs and Combat's logic runs
                                ;   every frame, so "the state" changes four
                                ;   times a tick and two consoles reading it
                                ;   whenever their transport got round to it
                                ;   disagree for no reason at all. Sampled at
                                ;   the boundary of tick T and carried by the
                                ;   record stamped T+d, it is the same quantity
                                ;   on both consoles by construction.

; ---------------- the union ----------------
; Two sets of cells that are never live at the same time. The rings belong to a
; match; the transport's setup cells and the input scanner belong to the boot
; bank, which finishes before a match can start. Twenty-six free bytes is not
; enough for both, and the only thing that could make them overlap in time is a
; cold start from inside a match -- which empties everything anyway.
;
; Game side:
CBRWAT  EQU     $EC             ; highest remote tick received
CBRING  EQU     $ED             ; 8: remote input ring, index = tick AND 7
CBLOC   EQU     $F5             ; 4: local input ring, index = tick AND 3
CBRDN   EQU     $F9             ; records left to consume; also the window
                                ;   cursor, as (CBRDN-1)*4
; Session side:
FNDEV   EQU     $ED             ; transaction setup, read by FNBEG
FNCMD   EQU     $EE
FNNPR   EQU     $EF
FNTMO   EQU     $F0             ; timeout in ~0.6s quanta
FNCNT   EQU     $F1             ; timeout quanta remaining
FNPTRL  EQU     $F2             ; reply-window cursor
FNPTRH  EQU     $F3
FNPCNT  EQU     $F4             ; bytes written into the current path payload
INCUR   EQU     $F5             ; input scan, both halves
INPREV  EQU     $F6
CSDLY   EQU     $F7             ; a frame counter for the session's wait loops.
                                ;   IN MEMORY, NOT IN A REGISTER: the text
                                ;   kernel counts scanlines in X and Y and
                                ;   clobbers both, and a caller that counted its
                                ;   own repeats in one of them ran its wait 256
                                ;   times over. Battleship's port documents
                                ;   exactly this -- "seventy seconds where
                                ;   seventeen milliseconds were meant, which
                                ;   looks exactly like a server that never
                                ;   answers" -- and it is still true here.
; Shared by both, because it means the same thing to both:
FNSEQ   EQU     CBSEQ

; THE REMOTE RING IS EIGHT, AND THE EIGHT IS LOAD-BEARING. Console B can only
; advance to tick T_B with A's input for T_B, which A sent at its own tick
; T_B - d. So T_B <= T_A + d, and A has sent up to T_A + d <= T_B + 2d: B can
; be holding records for T_B .. T_B + 2d, which is 2d+1 slots. At d = 2 that is
; five, and eight is the next power of two, for a free AND #7 instead of a
; modulo. A four-slot ring silently overwrites a tick that has not been used
; yet, and the symptom is the peer's tank occasionally repeating a move.

; ---------------- per-frame, and therefore safe in ClrGam's range ----------
; ClrGam clears $82-$A2, so nothing here may need to survive a SELECT press.
; Both are recomputed every frame.
CBADV   EQU     $8F             ; nonzero: advance the sim this frame
CBTMP   EQU     $90             ; scratch, dead between routines

; Two more bytes, borrowed WITHIN the shim and never across it. TEMP1 and TEMP
; are Combat's own scratch, used inside CHKSW, COLIS, SCROT and LDSTEL -- every
; one of which runs AFTER the shim in the frame, and none of which expects
; anything to survive from the frame before. This is the escape hatch named
; below, taken deliberately and documented where it is taken.
CBW0    EQU     $D1             ; TEMP1: player 0's wire byte, during the shim
CBW1    EQU     $D2             ; TEMP:  player 1's

; If the net machine ever outgrows the above, five more bytes are provably
; available WITHIN a micro-step but never across one: TEMP1 ($D1), TEMP ($D2)
; and $D4 are scratch inside CHKSW/COLIS/SCROT/LDSTEL, all of which have
; finished in the game bank before the switch; NUMG0/NUMG1 ($DE/$DF) are
; written by VOUT AFTER the hook runs.

; ---------------------------------------------------------------------------
; THE TICK
;
; K = 4 video frames a tick, 15 ticks a second. Four and not the three the
; latency probe would allow, because a tick boundary is then `CLOCK AND 3` and
; costs nothing -- where three would want a modulo, and a cell to count it in
; that the zero page has not got. The probe measured a full WRITE/STATUS/READ
; cycle at three frames, so the transport keeps up with a four-frame tick with
; a frame to spare.
;
; Combat tolerates a coarse tick better than almost any game of its era,
; because its own input is already heavily quantised: TurnTimer gates a
; 22.5-degree rotation to one per FIFTEEN frames, and FwdTimer runs $F0 to $00
; -- sixteen frames -- before a velocity change lands. A four-frame tick is
; finer than either.
CBK     EQU     4               ; frames per tick
CBKM    EQU     CBK-1           ; ...as a mask, which is why it is a power of two

; d, the input delay, in ticks. Hardcoded rather than taken from START's delay
; byte: it sizes the local ring at assembly time, and a console that honoured a
; delay its ring could not hold would overwrite a tick it had not used yet.
; The server is asked for this value and START is checked against it.
CBD     EQU     2               ; 2 ticks = 8 frames = ~133 ms, symmetric
CBLM    EQU     3               ; local ring mask; the ring is 4, and 4 > d

; CBENT is TWO INDEPENDENT BITS, and keeping them independent matters.
;
;   bit 0  WARM. Set once START has run. The game bank is entered at $1000 by
;          the trampoline at the end of every single frame, so its first act is
;          to ask whether this is a frame boundary or a cold start; clear means
;          run Combat's own initialisation, set means carry on from MLOOP.
;   bit 1  NETWORKED. Set by the boot bank when a match has started.
;
; They were one value at first -- 0 cold, 1 local, 2 playing -- and the boot
; bank handing over with 2 meant the game bank read "not cold" and jumped
; straight to MLOOP, so COMBAT'S START NEVER RAN. The whole game came up with
; every variable zero: no playfield pointers, no shape pointers, tanks at Y=0.
; Both consoles did it identically, so the transport looked perfect and only
; the checksums disagreed, by a small constant, for two hundred ticks.
CBE_WARM EQU    $01
CBE_NET EQU     $02
CBE_PH  EQU     $C0             ; 2 bits: which frame of the tick this is.
                                ;
                                ; THE SIM CLOCK MUST NOT BE A CELL THE GAME CAN
                                ; WRITE. The tick boundary used to be
                                ; `CLOCK AND 3`, and Combat's ClrGam -- which
                                ; runs on every SELECT advance -- clears
                                ; $82-$A2 and CLOCK is $86. So the netcode's
                                ; clock was reset by the game, on a frame the
                                ; game chose, and the tick re-phased underneath
                                ; the transport. Here it is two bits of a cell
                                ; above $A2 that nothing in Combat addresses.
CBE_LEFT EQU    $10             ; the peer left: the boot bank says so in words
CBE_RAN EQU     $08             ; set = the last boundary ran its tick, so the
                                ;   next one must advance CBTICK before it
                                ;   samples anything. See the note on CBCRCV.
CBE_ROLE EQU    $04             ; set = guest, and it drives the RIGHT tank.

; ---------------------------------------------------------------------------
; CBE_RSY -- a desync has been seen and a repair is armed.
;
; THE REPAIR IS A SYNTHETIC RESET PRESS, AND THAT IS THE WHOLE TRICK. Combat's
; own new-game path already does everything a repair has to do: it clears
; $89-$A2 and re-runs ResetField, which re-homes the Y positions AND the sprite
; X positions -- and X is the one piece of state a push could never carry,
; because it is not in RAM at all. It lives in the TIA's HMP0/HMP1 and is
; applied incrementally by HMOVE, which is what ruled out state sync in the
; first place.
;
; Routing the repair through the INPUT path is what makes it agree. RESET is
; ANDed on the wire, so one console arming it is enough, and both consoles act
; on it at the SAME TICK through machinery that is already proven by `make rig`.
; A scheduled repair would have needed a tick to be agreed and stored, and there
; is not one spare byte of zero page left to store it in.
;
; It costs one bit, and it is one-shot: CBCAP clears it as it presses, so the
; press lasts exactly one tick -- four frames, which is far more than the one
; frame GSGRCK needs to see it.
;
; The scores are wiped, and that is CORRECT rather than a wart. The two consoles
; have just disagreed, so their scores may disagree too; the only state that is
; agreed by construction is the one Combat itself builds from nothing.
CBE_RSY EQU     $20             ; a checksum mismatch was seen: press RESET
                                ;   A bit and not a byte because the zero page
                                ;   has twenty-six free and the checksum needed
                                ;   one of them more.

; ---------------------------------------------------------------------------
; THE WIRE
;
; One byte carries a player's whole console, which is what lets the ring be a
; ring. The senses are the hardware's, straight through, so the shim is masks
; and shifts and never an inversion.
IW_TRIG EQU     $80             ; 1 = released      (INPT4's sense)
IW_DIFF EQU     $40             ; 1 = A / Pro       (SWCHB's sense)
IW_SEL  EQU     $20             ; 1 = not pressed
IW_RST  EQU     $10             ; 1 = not pressed
IW_STICK EQU    $0F             ; 1 = not pushed    (SWCHA's sense)

; Idle: nothing pressed, nothing pushed, difficulty B. Both rings are SEEDED
; with it rather than with zero, and the difference is not cosmetic -- zero
; reads as EVERY switch pressed and the stick shoved in all four directions,
; because the wire keeps the hardware's active-low sense.
;
; The seeding is what makes the first d ticks honest. A record is stamped
; CBTICK+d, so the first one anybody sends is for tick d and the slots for ticks
; 0..d-1 are never sent by anyone. Both consoles filling them with the same
; constant is the only way the two agree about a tick neither of them has heard
; anything about; filling them with a LOCAL capture would be asymmetric, and
; leaving them zero starts every match with both players holding everything
; down for two ticks.
IW_IDLE EQU     IW_TRIG|IW_SEL|IW_RST|IW_STICK

; A record is six bytes of payload, eight on the wire with len and type.
IR_TICK EQU     0
IR_IN   EQU     1
IR_CRC  EQU     2
IR_CHK  EQU     3
IR_LEN  EQU     8               ; ON THE WIRE, and a power of two on purpose
IR_MASK EQU     $F8
IR_SEED EQU     $A5             ; the check byte's salt

; ---------------------------------------------------------------------------
; The television standard, declared at BUILD time because a 2600 cannot measure
; it. The ROM generates the video timing itself and there is no register, no
; interrupt and no external reference to read -- which is exactly why every 2600
; game in history shipped as separate NTSC, PAL and SECAM images. A runtime
; probe is not a thing that can be written.
;
; So the refusal lives in build.sh, which will not produce a SECAM image, and in
; the relay, which will not pair a console that declares one. See the note on
; TVSECAM below for what is actually wrong with SECAM.
TVNTSC  EQU     0
TVPAL   EQU     1
; On a SECAM console the TIA renders EIGHT colours, chosen by the hue nibble,
; with the luminance bits ignored entirely. Combat's two tanks are the same
; shape and are told apart ONLY by colour, and its ColorTbl separates several
; variations' pairs by luminance alone -- so on a SECAM set the two players can
; be looking at two identical sprites. That is not a synchronisation problem and
; no amount of lockstep fixes it: it makes two-player Combat unplayable, which
; is why this is refused outright rather than guarded.
CBWIN   EQU     3               ; ticks of input in every record, newest first.
                                ; THE TRANSPORT IS NOT LOCKED TO THE TICK: a
                                ; WRITE/STATUS/READ cycle that runs long means a
                                ; tick's record is never sent at all, and the
                                ; peer's watermark tracks only the newest tick
                                ; received, so the hole left the PREVIOUS LAP's
                                ; byte in that ring slot and the gate used it.
                                ; Three covers any gap the local ring can still
                                ; supply, which is one less than its four slots.

; ---------------------------------------------------------------------------
; THE NETWORK MICRO-STEP MACHINE
;
; Stepped from inside VOUT's wait for the vertical blank to run out, which is
; the only slack in a frame that has no overscan at all. The loop re-reads INTIM
; before every step and stops when the slack is gone, so THE HOOK CANNOT
; OVERRUN THE KERNEL BY CONSTRUCTION -- there is no budget to get wrong.
;
; TIM64T decrements every 64 cycles, so a gate of 8 guarantees at least 7 x 64 =
; 448 cycles remain, against a worst step of about 310. On the worst frame in
; the game -- a SELECT or RESET press, where GSGRCK also runs ClearMem and
; InitPF and only 482 cycles of vertical blank are left -- INTIM is about 7 and
; no step runs at all. That is the right answer: a tick may be a frame late, and
; the lockstep is built to absorb exactly that.
CBGATE  EQU     8

NS_IDLE EQU     0               ; no socket, or no match: do nothing
NS_WISS EQU     2               ; issue the WRITE and launch it
NS_WACK EQU     4               ; wait for the cartridge to acknowledge
NS_SISS EQU     6               ; issue the STATUS
NS_SACK EQU     8               ; ...and read how many bytes are waiting
NS_RISS EQU     10              ; issue the READ
NS_RACK EQU     12              ; ...and turn its length into a record count
NS_CONS EQU     14              ; consume one record from the reply window
NS_FAIL EQU     16              ; count it and start the cycle again
NS_LAST EQU     NS_FAIL         ; the dispatch table's last entry

; ---------------------------------------------------------------------------
; BANKS
;
; An image is (N+1) x 2048 -- N banks then the fixed half -- and MAME's cart
; slot accepts only N in {1,3,7,15}. Three banks, and the fourth slot is the
; fixed half.
BANKBOOT EQU    0               ; cold start, the session, the WAITING screen
BANKGAME EQU    1               ; Combat's frame loop and its seven routines
BANKKERN EQU    2               ; Combat's kernel, and the whole netcode
NBANKS   EQU    3

; The trampoline, at a fixed address in the fixed tail because the banks are
; assembled separately. A bank switch is a JUMP, never a call: the store that
; switches is the last instruction fetched from the old bank, so there is no
; return address to come back to. "Where to carry on" is DATA, in CBENT.
CBGOTO  EQU     FNTAIL

ENCOLD  EQU     0               ; bank 0: power-on
ENLOGIC EQU     1               ; bank 1: the top of a frame
ENKERN  EQU     2               ; bank 2: the net step, then the kernel
ENPLAY  EQU     3               ; bank 1: a match has started, cold-start Combat

; ---------------------------------------------------------------------------
; THE WIRE
;
; frame := len(1) type(1) payload(len-1). `len` counts type and payload, so a
; frame is never zero-length and the reader can reject a bad one on sight.
WT_HELO EQU     $01
WT_LOBBY EQU    $03
WT_JOIN EQU     $04
WT_START EQU    $05
WT_INPUT EQU    $06
WT_CRC  EQU     $07
WT_STATE EQU    $08
WT_RESYN EQU    $09
WT_BYE  EQU     $0A
WT_LEFT EQU     $0B
WT_PING EQU     $0C
WT_PONG EQU     $0D

WPROTO  EQU     2               ; HELLO's version byte. 2 added the television
                                ; standard between the version and the name --
                                ; a payload shape change, so the version had to
                                ; move with it or an old relay would read the
                                ; standard byte as the first letter of a name.

; ---------------------------------------------------------------------------
; APPKEYS
;
; Two of them, both under the family's shared creator and app:
;
;   key 0            the FujiNet USERNAME, which every game in the family reads
;                    -- that is the whole point of it
;   key CBAPPKEY     the room's own URL, which the Lobby writes there on the way
;                    out and the booted game reads on the way in. There is no
;                    HTTP "join a room" call anywhere in this family; joining is
;                    a local write and a reboot.
;
; Both are read into a CARTRIDGE PATH BUFFER and never into console RAM. That
; is not thrift, it is necessity: a name is eight bytes and a URL is sixty-four,
; against twenty-six free in the whole machine -- and the reply window they
; arrive in is repainted by the very next transaction.
CBAPPKEY EQU    24              ; pending provisioning on the firmware wiki
AKNAMEK EQU     0
PB_NAME EQU     FP_SEL0         ; path buffer 0: the username
PB_URL  EQU     FP_SEL1         ; path buffer 1: the server URL
NAMEMIN EQU     2
NAMEMAX EQU     8
