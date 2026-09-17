-- det.lua -- a deterministic input stream and a per-frame state checksum.
--
-- Two jobs, and they are the same job. It proves the banked build plays
-- EXACTLY like stock (M2), and it is the determinism rig two consoles have to
-- pass before lockstep can work at all (M4): drive identical inputs, print a
-- checksum of the sim state every frame, and require the two streams to be
-- byte-identical.
--
-- The input has to be generated HERE and not by hand, because the whole point
-- is that both runs see the same bytes at the same frames. It is a plain
-- function of the frame counter -- no randomness, nothing read back off the
-- screen, nothing that depends on how fast the emulator ran.
--
-- AND IT IS KEYED TO A COUNT OF SIM FRAMES, NOT TO A COUNTER OF EMULATOR
-- FRAMES AND NOT TO CLOCK EITHER.
-- Combat runs a 263-line frame against MAME's 262, so the ROM's frame and the
-- emulator's drift apart by a line a frame -- and two builds start that drift
-- from different phases, because one of them boots through a bank switch. A
-- schedule keyed to an emulator-frame counter changes input at the same ORDINAL
-- frame but a different GAME frame, and the two runs answer an input change one
-- frame apart: a harness artefact that looks exactly like a desync.
--
-- CLOCK was the second attempt and is no better, for a reason particular to
-- this game: ClrGam clears $82-$A2 and CLOCK is $86, so every SELECT press
-- RESETS it. Keying to it made the schedule jump about whenever the variation
-- changed.
--
-- What both builds genuinely share is the number of SIM FRAMES they have run,
-- so that is the index: one per CXCLR strobe that was a real frame.
--
-- The family's form of this lesson is PORTING.md §7.17: breakpoint-forced
-- injection is not run-to-run deterministic, and a gate that needs exact
-- arithmetic has to generate its input in-ROM. This is the cheap half of that
-- -- key the schedule to something the ROM itself counts -- and it is enough
-- for a build-vs-build comparison. It would not be enough for two consoles.
--
--   SLOT=a26_2k_4k ./run.sh stock det > build/det_stock.txt
--   ./run.sh combat det > build/det_split.txt
--   python3 tools/ramdiff.py build/det_stock.txt build/det_split.txt
--
-- WHAT THE CHECKSUM COVERS IS THE WHOLE DESIGN OF IT. Not "all of Combat's
-- variables": three classes of cell in that range differ between two builds
-- that are playing identically, and including any of them makes the gate cry
-- wolf forever.
--
--   * ADDRESS cells. LORES ($B5-$BA) and SHAPES ($BB-$BC) hold pointers into
--     the playfield maps and the shape tables, and their HIGH BYTES are $F7/$F6
--     in a $F000 image and $17/$16 in a cartridge bank. The pointers are
--     correct in both; only the window moved.
--   * STACK-DERIVED cells. TMPSTK ($D3) is the stack pointer VOUT saves, and a
--     bank switch is a JUMP that resets the stack where a JSR would have left a
--     return address on it. Different by construction, and meaning the same.
--   * DERIVED cells generally. HIRES ($BD-$CC) is rebuilt by ROT every frame,
--     SCROFF ($E0-$E3) by SCROT, the colour cells ($D6-$DB) by LDSTEL. They are
--     functions of the state, not the state -- and the colours in particular
--     must stay out, because the B&W switch is deliberately LOCAL and never
--     reaches physics.
--
-- What is left is the authoritative sim state: the variation and its decoded
-- flags, the frame counter, the timers, the bearings, the missile lifetimes and
-- bounce state, the scores, the Y positions, the velocity ramps, the pacing
-- counters, the pending horizontal motion, and the game clock.
--
-- Tank X is NOT in RAM at all -- it lives in the TIA's HMP0/HMP1 and is applied
-- incrementally by HMOVE, so no checksum can see it. XOFFS/XoffBase ($B0/$B1)
-- are the pending motion and are the closest proxy there is.
local RANGES = {
    { 0x80, 0x8E },   -- variation, flags, CLOCK, GameOn, timers, velocity
    { 0x91, 0xB3 },   -- bearings, missiles, scores, Y positions, pacing, motion
    { 0xDD, 0xDD },   -- GameTimer, the 2:16 clock
}

local CLOCK = 0x86

local sp = manager.machine.devices[":maincpu"].spaces["program"]
local frame, lastclock = 0, nil
local held = {}

-- CACHED ONCE. A field looked up through manager.machine.ioport.ports at the
-- moment of pressing it is a fresh wrapper, and set_value on a fresh wrapper is
-- lost -- the raw port never changes. This harness pressed RESET for a hundred
-- runs without ever starting a game, and the comparison still passed, because
-- two builds sitting in attract mode agree just as well as two builds playing.
-- That is PORTING.md §7.31's trap wearing a different hat.
local FIELDS = {}
local function field(tag, name)
    local key = tag .. "|" .. name
    if FIELDS[key] == nil then
        local p = manager.machine.ioport.ports[tag]
        FIELDS[key] = (p and p.fields[name]) or false
    end
    return FIELDS[key]
end

local function set(tag, name, on)
    local f = field(tag, name)
    if f then f:set_value(on and 1 or 0) end
end

-- 1 IS A PRESS, for the console switches too. SWCHB is active low on the
-- hardware and MAME applies that inversion itself, so a harness that writes 0
-- to "press" SELECT is really releasing it, and the 1 it writes to "release"
-- is a press that then never ends.
-- DET_QUIET: start a game and then touch nothing.
--
-- It exists because injecting stick movement through MAME's ports is NOT
-- frame-exact between two builds. Combat runs a 263-line frame against MAME's
-- 262, the ROM's frame and the emulator's drift a line apart every frame, and
-- two builds that boot through different amounts of code start that drift from
-- different phases -- so an input change lands on one side of one build's read
-- of SWCHA and the other side of the other's. emu/inputs.lua showed it
-- directly: at game frame 207 the stock build read $FB and the banked build
-- read $F7, which is the same Left-to-Right transition one frame apart.
--
-- A quiet run removes the variable. The input streams are then identical by
-- construction, so any difference in state is the bank split's and nothing
-- else's -- and the whole frame loop, the timers, the collisions, the sound and
-- the kernel all still run. Stick handling is covered by the run WITH input,
-- which agrees for as long as the injected inputs agree.
--
-- The real fix for two consoles is the family's (PORTING.md §7.17): generate
-- the input in the ROM and use the harness only to park and dump. That arrives
-- with the input shim, which is the next milestone.
local QUIET = os.getenv("DET_QUIET") ~= nil

local function drive(f)
    local want = {}
    if f >= 40 and f < 48 then want[":SWB|Reset Game"] = true end
    if f >= 90 and not QUIET then
        -- Both sticks, on different periods, so the two tanks are never doing
        -- the same thing and a swap between them would show.
        local a, b = (f // 37) % 5, (f // 23) % 5
        local dirs = { "Up", "Down", "Left", "Right" }
        if a > 0 then want[":joyport1:joy:JOY|P1 " .. dirs[a]] = true end
        if b > 0 then want[":joyport2:joy:JOY|P2 " .. dirs[b]] = true end
        if (f // 53) % 3 == 0 then want[":joyport1:joy:JOY|P1 Button 1"] = true end
        if (f // 71) % 3 == 0 then want[":joyport2:joy:JOY|P2 Button 1"] = true end
    end
    for k in pairs(held) do
        if not want[k] then
            local tag, name = k:match("^(.-)|(.*)$")
            set(tag, name, false)
            held[k] = nil
        end
    end
    for k in pairs(want) do
        if not held[k] then
            local tag, name = k:match("^(.-)|(.*)$")
            set(tag, name, true)
            held[k] = true
        end
    end
end

-- SAMPLED AT CXCLR, NOT AT VSYNC.
--
-- VOUT strobes CXCLR once a frame, at $F063, immediately before it turns the
-- picture on -- which is AFTER the whole game-logic chain has run and after
-- CLOCK has been incremented. VSYNC is the other end of the frame, and INC
-- CLOCK moved from VCNTRL into MLOOP to put it behind the stall gate, so a
-- checksum taken at VSYNC reads CLOCK one lower in the banked build than in
-- stock. Nothing in the game sees that -- all four readers of CLOCK are in the
-- chain, after the increment, in both builds -- but an observer at the wrong
-- end of the frame does, and reports a divergence that is entirely its own.
--
-- CXCLR is also the right point on its own merits: it is "the state this frame
-- computed", which is exactly what two consoles have to agree about.
-- INPUT IS DRIVEN FROM A FRAME NOTIFIER, NOT FROM THE MEMORY TAP BELOW.
-- set_value called from inside a tap is silently lost -- the tap runs in the
-- CPU's execution context and port state is settled at frame boundaries -- so
-- the raw port never changes and nothing reports an error. This harness pressed
-- RESET for a hundred runs without ever starting a game, and every comparison
-- still passed, because two builds sitting in attract mode agree just as well
-- as two builds playing.
_G._det_drive = emu.add_machine_frame_notifier(function()
    drive(frame)
end)

-- CLOCK == 0 is not a frame. Combat's ClearMem clears the TIA as well as the
-- RAM -- STA $A2,X sweeps $0100-$017F, and A7 clear there means TIA -- so it
-- STROBES CXCLR on its way past, and the tap sees a frame that never happened
-- with the state still zeroed. Skipping it costs one real sample every 256
-- frames, symmetrically on both builds.
_G._det_vs = sp:install_write_tap(0x2C, 0x2C, "cxclr", function(off, data, mask)
    -- CLOCK == 0 is not a frame. Combat's ClearMem clears the TIA as well as
    -- the RAM -- STA $A2,X sweeps $0100-$017F, and A7 clear there means TIA --
    -- so it STROBES CXCLR on its way past, and the tap would see a frame that
    -- never happened with the state still zeroed.
    if sp:readv_u8(CLOCK) == 0 then return end
    frame = frame + 1
    -- Rotate-and-add, not a plain sum: a plain sum cannot see two bytes that
    -- swapped, and two tanks that swapped is exactly the failure this is for.
    local c = 0
    for _, r in ipairs(RANGES) do
        for a = r[1], r[2] do
            c = ((c << 1) | (c >> 15)) & 0xFFFF
            c = (c + sp:readv_u8(a)) & 0xFFFF
        end
    end
    print(string.format("%d %04X", frame, c))
end)
