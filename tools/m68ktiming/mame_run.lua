-- mame_run.lua: runs gen.py's timing batches on MAME 0.264's Hang-On sub CPU and traces them.
-- usage (from a MAME home with the hangon ROMs):
--   MT_BATCHES=<gen dir>/batches.bin MT_OUT=<out dir> mame hangon -debug -debugger none \
--     -autoboot_script mame_run.lua -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run 280
-- writes <out>/mame.tr (trace: "<totalcycles hex> <pc>: <disassembly>" per instruction),
-- <out>/mame.log (per batch status) and <out>/r_<batch>.ram (the sub CPU's RAM after each batch).
-- The main CPU is parked in a loop in its own RAM with interrupts masked; every exception vector
-- of the sub CPU's ROM is patched to the handler stub at $7EE80 (gen.py).
local m = manager.machine
local main = m.devices[":maincpu"]
local sub = m.devices[":subcpu"]
local dbg = m.debugger
local OUT = os.getenv("MT_OUT")
local bf = assert(io.open(os.getenv("MT_BATCHES"), "rb"))
local log = assert(io.open(OUT .. "/mame.log", "w"))
local START = 200
local n, cur, waited = 0, nil, 0

local function load_next()
  local hdr = bf:read(16)
  if not hdr or #hdr < 16 then return false end
  local magic, batch, start, endpc = string.unpack(">I4I4I4I4", hdr)
  local img = bf:read(0x4000)
  local ss = sub.spaces["program"]
  for i = 0, 0x3fff, 4 do ss:write_u32(0x7c000 + i, (string.unpack(">I4", img, i + 1))) end
  sub.state["SR"].value = 0x2700
  sub.state["CURPC"].value = start
  cur, waited = batch, 0
  return true
end

local function finish(status)
  local ss = sub.spaces["program"]
  local f = io.open(string.format("%s/r_%d.ram", OUT, cur), "wb")
  local t = {}
  for i = 0, 0x3fff, 4 do t[#t + 1] = string.pack(">I4", ss:read_u32(0x7c000 + i)) end
  f:write(table.concat(t)); f:close()
  log:write(string.format("batch %d status %s frames %d pc %x\n", cur, status, waited, sub.state["CURPC"].value))
end

emu.register_frame_done(function()
  n = n + 1
  if n < START then return end
  if n == START then
    main.spaces["program"]:write_u16(0x20c000, 0x60fe)
    main.state["SR"].value = 0x2700
    main.state["CURPC"].value = 0x20c000
    local reg = m.memory.regions[":subcpu"]
    for v = 2, 255 do reg:write_u32(v * 4, 0x0007ee80) end
    dbg.visible_cpu = sub
    dbg:command("trace " .. OUT .. "/mame.tr,subcpu,noloop,{tracelog \"%X \",totalcycles}")
    load_next()
    return
  end
  local done = sub.spaces["program"]:read_u16(0x7ee50)
  waited = waited + 1
  if done ~= 0 or waited >= 8 then
    finish(done ~= 0 and tostring(done) or "timeout")
    if not load_next() then
      dbg:command("trace off,subcpu")
      log:close()
      m:exit()
    end
  end
end, "mtime")
