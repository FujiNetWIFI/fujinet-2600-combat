-- inputs.lua -- every read of the console's input ports, with the PC that did it.
--
-- This is the 2600 form of the jzIntv write-watch that found Baseball's hidden
-- third input surface. Combat's disassembly says its whole input surface is
-- seven reads; this is what turns that from a claim into a measurement, and it
-- is how the patch map is proved COMPLETE rather than merely plausible.
--
-- It also logs the value, keyed to the game's own frame counter, so two builds'
-- input streams can be compared directly -- which is the only way to tell an
-- input-timing artefact in the harness from a real divergence in the game.
--
--   ./run.sh combat inputs 2>/dev/null | grep '^IN ' > build/in_split.txt

dofile(os.getenv("A2600_EMU") .. "/det.lua")

local CLOCK = 0x86
local PORTS = { [0x0280] = "SWCHA", [0x0282] = "SWCHB",
                [0x000C] = "INPT4", [0x000D] = "INPT5" }

local sp = manager.machine.devices[":maincpu"].spaces["program"]
local cpu = manager.machine.devices[":maincpu"]
local sites = {}
local frame, lastclock = 0, nil

for addr, name in pairs(PORTS) do
    _G["_in_" .. name] = sp:install_read_tap(addr, addr, name,
        function(off, data, mask)
            local pc = cpu.state["PC"].value
            -- The read instruction started three bytes back for an absolute
            -- read, two for zero-page; report where the operand is so it lines
            -- up with the patch map's addresses.
            sites[name .. " @" .. string.format("$%04X", pc)] =
                (sites[name .. " @" .. string.format("$%04X", pc)] or 0) + 1
            print(string.format("IN %d %s %02X %04X", frame, name, data, pc))
        end)
end

_G._in_vs = sp:install_write_tap(0x2C, 0x2C, "cxclr", function(off, data, mask)
    local c = sp:readv_u8(CLOCK)
    if lastclock == nil then frame = c
    elseif c ~= lastclock then frame = frame + ((c - lastclock) & 0xFF) end
    lastclock = c
end)

_G._in_stop = emu.add_machine_stop_notifier(function()
    local keys = {}
    for k in pairs(sites) do keys[#keys + 1] = k end
    table.sort(keys)
    print("SITES " .. #keys)
    for _, k in ipairs(keys) do print(string.format("  %-22s %d reads", k, sites[k])) end
end)
