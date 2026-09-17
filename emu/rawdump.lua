-- rawdump.lua -- a window of frames' zero page, byte for byte.
-- A checksum says two runs differ; this says which cell.
--   RAW_FROM=205 RAW_FRAMES=212 ./run.sh combat rawdump
-- It needs det.lua's input stream to reach anything interesting, so it loads
-- it: a divergence that only happens once a tank is moving cannot be found by
-- a run where nothing is pressed.
dofile(os.getenv("A2600_EMU") .. "/det.lua")
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local frame = 0
local N = tonumber(os.getenv("RAW_FRAMES") or "3")
_G._rd = sp:install_write_tap(0x2C, 0x2C, "cxclr", function(off, data, mask)
    frame = frame + 1
    local from = tonumber(os.getenv("RAW_FROM") or "1")
    if frame < from or frame > N then return end
    local out = {}
    for a = 0x80, 0xFF do out[#out + 1] = string.format("%02X", sp:readv_u8(a)) end
    print(string.format("F%d %s", frame, table.concat(out, " ")))
end)
