-- ORBoard reference recorder (orunnersj, MAME 0.264).
-- OR_SCHED: "<frame> set PORT mask 0/1 | aset PORT mask value | snap | dump | exit"
-- OR_IOLOG: every main-CPU read of $800000-$801FFF, $C00000-$CFFFFF, $D80000-$DFFFFF and write
--           to $800000-$801FFF, $C00000-$D7FFFF: "R|W <s> <atto> <addr> <data> <mask>",
--           and "F <f> <s> <atto>" at each frame_done
-- OR_FRAMES: command line of a pipe that gets "FRAM" <f> <len> + work RAM ($200000-$21FFFF)
--            + shared RAM ($700000-$701FFF) at each frame_done
-- OR_DUMPDIR: full work RAM dumps for "dump"
local sched = {}
for l in io.lines(os.getenv("OR_SCHED")) do
  local f, c = l:match("^(%d+)%s+(.*)$")
  if f then sched[#sched + 1] = { tonumber(f), c } end
end
local f = 0
local main = manager.machine.devices[":mainpcb:maincpu"].spaces["program"]
local iolog = io.open(os.getenv("OR_IOLOG"), "w")
local function lg(k, offset, data, mask)
  local t = manager.machine.time
  iolog:write(string.format("%s %d %d %06x %08x %08x\n", k, t.seconds, t.attoseconds, offset, data, mask))
end
orb_taps = {}  -- global: a collected tap object removes its handler
orb_taps[#orb_taps + 1] = main:install_read_tap(0xc00000, 0xcfffff, "rio", function(o, d, m) lg("R", o, d, m) return d end)
orb_taps[#orb_taps + 1] = main:install_read_tap(0xd80000, 0xdfffff, "rrn", function(o, d, m) lg("R", o, d, m) return d end)
orb_taps[#orb_taps + 1] = main:install_read_tap(0x800000, 0x801fff, "rcm", function(o, d, m) lg("R", o, d, m) return d end)
orb_taps[#orb_taps + 1] = main:install_write_tap(0xc00000, 0xd7ffff, "wio", function(o, d, m) lg("W", o, d, m) end)
orb_taps[#orb_taps + 1] = main:install_write_tap(0x800000, 0x801fff, "wcm", function(o, d, m) lg("W", o, d, m) end)
-- OR_ZLOG: the Z80's port writes and MultiPCM writes: "<s> <atto> io|pcm <addr> <data>"
local zlog = os.getenv("OR_ZLOG") and io.open(os.getenv("OR_ZLOG"), "w")
if zlog then
  local zc = manager.machine.devices[":mainpcb:soundcpu"]
  orb_taps[#orb_taps + 1] = zc.spaces["io"]:install_write_tap(0x00, 0xff, "zio", function(o, d, m)
    local t = manager.machine.time
    zlog:write(string.format("%d %d io %02x %02x\n", t.seconds, t.attoseconds, o & 0xff, d & 0xff))
  end)
  orb_taps[#orb_taps + 1] = zc.spaces["program"]:install_write_tap(0xc000, 0xdfff, "zpcm", function(o, d, m)
    local t = manager.machine.time
    zlog:write(string.format("%d %d pcm %04x %02x\n", t.seconds, t.attoseconds, o, d & 0xff))
  end)
end
local fr = io.popen(os.getenv("OR_FRAMES"), "w")
local held = {}
local function field(port, mask)
  local p = manager.machine.ioport.ports[":mainpcb:" .. port]
  for _, fl in pairs(p.fields) do if fl.mask == mask then return fl end end
end
emu.register_frame_done(function()
  f = f + 1
  local t = manager.machine.time
  iolog:write(string.format("F %d %d %d\n", f, t.seconds, t.attoseconds))
  local w = main:read_range(0x200000, 0x21ffff, 8)
  local s = main:read_range(0x700000, 0x701fff, 8)
  fr:write("FRAM" .. string.pack("<I4I4", f, #w + #s) .. w .. s)
  for _, e in ipairs(sched) do
    if e[1] == f then
      local c = e[2]
      if c == "snap" then manager.machine.video:snapshot()
      elseif c == "dump" then
        local o = io.open(os.getenv("OR_DUMPDIR") .. "/" .. f .. ".bin", "wb")
        o:write(w) o:close()
      elseif c == "exit" then iolog:close() fr:close() if zlog then zlog:close() end manager.machine:exit()
      else
        local ap, am, av = c:match("^aset%s+(%S+)%s+(%x+)%s+(%d+)$")
        if ap then held[ap .. am] = {field(ap, tonumber(am, 16)), tonumber(av)} end
        local port, mask, v = c:match("^set%s+(%S+)%s+(%x+)%s+(%d+)$")
        if port then
          local fl = field(port, tonumber(mask, 16))
          if tonumber(v) ~= 0 then fl:set_value(1) else fl:clear_value() end
        end
      end
    end
  end
  for _, h in pairs(held) do h[1]:set_value(h[2]) end
end)
