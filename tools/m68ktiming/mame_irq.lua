-- mame_irq.lua: runs irqprog.py's program on MAME 0.264's Hang-On main CPU and traces it, to
-- measure interrupt latency: the main CPU's level 4 autovector comes from VBLANK once a frame.
-- usage: MT_PROG=<prog.bin> MT_OUT=<trace file> MT_FRAMES=<n> mame hangon -debug -debugger none
--          -autoboot_script mame_irq.lua -video none -sound none -nothrottle -skip_gameinfo ...
-- The sub CPU is parked in a loop with interrupts masked; the level 4 vector is patched to $20C800.
local m = manager.machine
local main = m.devices[":maincpu"]
local sub = m.devices[":subcpu"]
local dbg = m.debugger
local START = 200
local FRAMES = tonumber(os.getenv("MT_FRAMES") or "3000")
local n = 0
emu.register_frame_done(function()
  n = n + 1
  if n == START then
    sub.spaces["program"]:write_u16(0x7c000, 0x60fe)
    sub.state["SR"].value = 0x2700
    sub.state["CURPC"].value = 0x7c000
    m.memory.regions[":maincpu"]:write_u32(28 * 4, 0x0020c800)
    local f = assert(io.open(os.getenv("MT_PROG"), "rb"))
    local img = f:read(0x4000); f:close()
    local ms = main.spaces["program"]
    for i = 0, 0x3fff, 4 do ms:write_u32(0x20c000 + i, (string.unpack(">I4", img, i + 1))) end
    main.state["SR"].value = 0x2700
    main.state["CURPC"].value = 0x20c000
    dbg.visible_cpu = main
    dbg:command("trace " .. os.getenv("MT_OUT") .. ",maincpu,noloop,{tracelog \"%X \",totalcycles}")
  elseif n == START + FRAMES then
    dbg:command("trace off,maincpu")
    m:exit()
  end
end, "mame_irq")
