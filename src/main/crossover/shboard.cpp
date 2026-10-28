/***************************************************************************
    Emulated Sega OutRun-hardware board running Super Hang-On. See shboard.hpp.

    Mapper and I/O behaviour transcribed from MAME 0.264 (BSD-3-Clause, Aaron Giles and the
    MAME team): segaorun.cpp (memory_mapper, shangon_custom_io_r/w, the scanline timer),
    315_5195.cpp (register file, region decoding), segaic16_road.cpp (road control).
***************************************************************************/
#include "crossover/shboard.hpp"
#include "crossover/romset.hpp"
#include "crossover/xobus.hpp"

#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdlib>

extern "C" {
#include "crossover/m68k/m68k.h"
}

SHBoard shboard;

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

SHBoard::SHBoard() {}
SHBoard::~SHBoard() {}

bool SHBoard::init(const RomSet& roms)
{
    ready = false;
    if (!roms.loaded()) return false;
    try
    {
        rom_data = roms["maincpu_data"].data;
        rom_op   = roms["maincpu_opcodes"].data;
        rom_raw  = roms["maincpu"].data;
        sub_rom  = roms["subcpu"].data;
    }
    catch (...) { return false; }
    if (rom_data.size() < 0x60000 || rom_op.size() < 0x60000 || sub_rom.size() < 0x60000) return false;

    static bool core_init = false;
    if (!core_init) { m68k_init(); core_init = true; }
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    for (int i = 0; i < 2; i++) ctx[i].assign(m68k_context_size(), 0);
    ready = true;
    reset();
    return true;
}

void SHBoard::reset()
{
    if (!ready) return;
    xo_bus = this;
    std::memset(workram, 0, sizeof workram);
    std::memset(cpu1ram, 0, sizeof cpu1ram);
    std::memset(roadram, 0, sizeof roadram);
    std::memset(roadbuf, 0, sizeof roadbuf);
    std::memset(tileram, 0, sizeof tileram);
    std::memset(textram, 0, sizeof textram);
    std::memset(palram, 0, sizeof palram);
    std::memset(sprram, 0, sizeof sprram);
    std::memset(regs, 0, sizeof regs);
    to_sound = from_sound = 0;
    irq2 = vblank = false;
    mapper_irq = 0;
    adc_select = adc_latch = 0;
    display_enable = false;
    road_control = 0;
    update_mapping();

    // Both CPUs from their reset vectors (MAME: the mapper holds the main CPU in reset while it
    // clears its registers, then releases it)
    for (int i = 0; i < 2; i++)
    {
        cur = i;
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_pulse_reset();
        m68k_get_context(ctx[i].data());
        cpu_time[i] = 0;
    }
    loaded = -1;
    sub_reset_pending = false;
    perfect_until = -1;
    frame_count = 0;
    first_frame = true;
    if (sound_reset) sound_reset(true);     // MAME: the PPI/output port powers up with the sound CPU in reset
}

bool SHBoard::set_widescreen(int xoff)
{
    // immediates of cmpi.w #$200,d0 at $2A50 and cmpi.w #$C0,d1 at $2A5E (program-key view)
    static const uint32_t A_RIGHT = 0x2A52, A_LEFT = 0x2A60;
    auto w16 = [&](std::vector<uint8_t>& r, uint32_t a, uint16_t v) { r[a] = v >> 8; r[a + 1] = (uint8_t) v; };
    if (!ready || be16(&rom_op[A_RIGHT - 2]) != 0x0C40 || be16(&rom_op[A_LEFT - 2]) != 0x0C41) return false;
    w16(rom_op, A_RIGHT, (uint16_t)(0x200 + xoff));
    w16(rom_op, A_LEFT,  (uint16_t)(0x0C0 - xoff));
    return true;
}

// ------------------------------------------------------------------------------------------------
// 315-5195 memory mapper
// ------------------------------------------------------------------------------------------------
void SHBoard::update_mapping()
{
    for (auto& p : pages) p = Page{ K_NONE, K_NONE, nullptr, nullptr, nullptr };

    static const uint32_t size_map[4] = { 0x00ffff, 0x01ffff, 0x07ffff, 0x1fffff };
    struct Entry { uint32_t offset, length, mirror; int kind; uint8_t* ptr; const uint8_t* op; uint32_t romsize; };

    // segaorun_state::memory_mapper, region by region; MAME installs region 7 first and region 0
    // last, and within a region in call order, each overriding what is already there
    for (int index = 7; index >= 0; index--)
    {
        std::vector<Entry> e;
        switch (index)
        {
            case 5:
                e.push_back({ 0x90000, 0x10000, 0xf00000, K_ROADCTL, nullptr, nullptr, 0 });
                e.push_back({ 0x80000, 0x01000, 0xf0f000, K_RAM, roadram, nullptr, 0 });
                e.push_back({ 0x60000, 0x08000, 0xf18000, K_RAM, cpu1ram, nullptr, 0 });
                e.push_back({ 0x00000, 0x60000, 0xf00000, K_SUBROM, sub_rom.data(), nullptr, 0 });
                break;
            case 4: e.push_back({ 0x90000, 0x10000, 0xf00000, K_MISCIO, nullptr, nullptr, 0 }); break;
            case 3: e.push_back({ 0x00000, 0x01000, 0xfff000, K_RAM, sprram, nullptr, 0 }); break;
            case 2: e.push_back({ 0x00000, 0x02000, 0xffe000, K_RAM, palram, nullptr, 0 }); break;
            case 1:
                e.push_back({ 0x00000, 0x10000, 0xfe0000, K_RAM, tileram, nullptr, 0 });
                e.push_back({ 0x10000, 0x01000, 0xfef000, K_RAM, textram, nullptr, 0 });
                break;
            case 0:
                e.push_back({ 0x60000, 0x08000, 0xf98000, K_RAM, workram, nullptr, 0 });
                e.push_back({ 0x00000, 0x60000, 0xf80000, K_ROM, rom_data.data(), rom_op.data(), 0x60000 });
                break;
            default: break;
        }
        uint32_t size_mask = size_map[regs[0x10 + 2 * index] & 3];
        uint32_t base = ((uint32_t) regs[0x11 + 2 * index] << 16) & ~size_mask;
        for (const Entry& en : e)
        {
            uint32_t mirror = en.mirror & size_mask;
            uint32_t start  = base + (en.offset & size_mask);
            uint32_t end    = start + std::min(en.length - 1, size_mask);
            uint32_t romend = end;
            if (en.kind == K_ROM && en.romsize && end + 1 - start >= en.romsize) romend = en.romsize - 1 + start;
            for (uint32_t p = 0; p < 0x1000; p++)
            {
                uint32_t a = (p << 12) & ~mirror & 0xffffff;
                if (a < start || a > end) continue;
                uint32_t off = a - start;
                Page& pg = pages[p];
                switch (en.kind)
                {
                    case K_RAM:
                        pg.rkind = pg.wkind = K_RAM;
                        pg.rptr = pg.wptr = en.ptr + off;
                        pg.optr = en.ptr + off;
                        break;
                    case K_ROM:
                        if (a <= romend) { pg.rkind = K_ROM; pg.rptr = en.ptr + off; pg.optr = en.op + off; }
                        else             { pg.rkind = K_NONE; }
                        pg.wkind = K_IGNORE;                    // unmap_write: writes are dropped
                        break;
                    case K_SUBROM:
                        pg.rkind = K_SUBROM; pg.rptr = en.ptr + off; pg.optr = en.ptr + off;
                        pg.wkind = K_IGNORE;                    // nop_w
                        break;
                    case K_MISCIO:  pg.rkind = pg.wkind = K_MISCIO; break;
                    case K_ROADCTL: pg.rkind = pg.wkind = K_ROADCTL; break;
                }
            }
        }
    }
}

uint16_t SHBoard::open_bus()
{
    // The last word on the bus: the (encrypted) prefetch at the main CPU's PC
    uint32_t pc = main_pc() & 0xfffffe;
    if (pc + 1 < rom_raw.size()) return be16(&rom_raw[pc]);
    return 0xffff;
}

uint8_t SHBoard::mapper_read(uint32_t offset)
{
    offset &= 0x1f;
    switch (offset)
    {
        case 0x00: case 0x01: return regs[offset];
        case 0x02: return (regs[0x02] & 3) == 3 ? 0x00 : 0x0f;
        case 0x03: return from_sound;
        default:   return (uint8_t) open_bus();
    }
}

void SHBoard::mapper_write(uint32_t offset, uint8_t data)
{
    offset &= 0x1f;
    uint8_t old = regs[offset];
    regs[offset] = data;
    switch (offset)
    {
        case 0x02:
            if ((old ^ data) & 3)
                fprintf(stderr, "shboard: mapper reset control %02x (not emulated)\n", data);
            break;
        case 0x03:
            to_sound = data;
            if (sound_command) sound_command(data);
            break;
        case 0x04:
            if ((data & 7) != 7) { mapper_irq = ~data & 7; update_irqs(); }
            break;
        case 0x05:
            if (data == 0x01)
            {
                uint32_t addr = (regs[0x0a] << 17) | (regs[0x0b] << 9) | (regs[0x0c] << 1);
                main_write16(addr, (uint16_t)(regs[0x00] << 8 | regs[0x01]), 0xffff);
            }
            else if (data == 0x02)
            {
                uint32_t addr = (regs[0x07] << 17) | (regs[0x08] << 9) | (regs[0x09] << 1);
                uint16_t r = main_read16(addr, 0);
                regs[0x00] = r >> 8; regs[0x01] = (uint8_t) r;
            }
            break;
        default:
            if (offset >= 0x10 && old != data) update_mapping();
            break;
    }
}

// ------------------------------------------------------------------------------------------------
// I/O: Super Hang-On's custom I/O (shangon_custom_io_r/w) and the road chip's control register
// ------------------------------------------------------------------------------------------------
uint16_t SHBoard::miscio_read(uint32_t a)
{
    switch (a & 0x303e)
    {
        case 0x1000: return port_service;
        case 0x1002: return port_buttons;
        case 0x1004: return port_coinage;
        case 0x1006: return port_dsw;
        case 0x3020: return adc_latch;
        default:     return open_bus();
    }
}

void SHBoard::miscio_write(uint32_t a, uint16_t data, uint16_t mask)
{
    switch (a & 0x303e)
    {
        case 0x0000:
            if (mask & 0x00ff)
            {
                adc_select = (data >> 6) & 3;
                display_enable = (data >> 5) & 1;
            }
            break;
        case 0x0020:
            if (mask & 0x00ff)
                if (sound_reset) sound_reset(!(data & 1));
            break;
        case 0x3000: break;                                    // watchdog
        case 0x3020: adc_latch = adc[adc_select & 3]; break;   // start a conversion (done well before
                                                                // the game reads it: $5F66 waits ~150 us)
        default: break;
    }
}

uint16_t SHBoard::roadctl_read()
{
    // segaic16_road_control_0_r: swap the halves of the road RAM
    for (int i = 0; i < 0x1000; i++) std::swap(roadram[i], roadbuf[i]);
    return 0xffff;
}

void SHBoard::roadctl_write(uint16_t data, uint16_t mask)
{
    if (mask & 0x00ff) road_control = data & 3;            // ROAD_OUTRUN
}

// ------------------------------------------------------------------------------------------------
// Buses
// ------------------------------------------------------------------------------------------------
uint16_t SHBoard::main_read16(uint32_t a, int space)
{
    a &= 0xfffffe;
    const Page& pg = pages[a >> 12];
    uint32_t o = a & 0xfff;
    switch (pg.rkind)
    {
        case K_ROM:     return be16((space ? pg.optr : pg.rptr) + o);
        case K_RAM:
        case K_SUBROM:  return be16(pg.rptr + o);
        case K_MISCIO:  return space == 2 ? 0 : miscio_read(a);
        case K_ROADCTL: return space == 2 ? 0xffff : roadctl_read();
        default:        return space == 2 ? 0xffff : (uint16_t)(0xff00 | mapper_read(a >> 1));
    }
}

void SHBoard::main_write16(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xfffffe;
    const Page& pg = pages[a >> 12];
    uint32_t o = a & 0xfff;
    switch (pg.wkind)
    {
        case K_RAM:
            if (mask & 0xff00) pg.wptr[o] = v >> 8;
            if (mask & 0x00ff) pg.wptr[o + 1] = (uint8_t) v;
            if (wlog && pg.wptr >= tileram && pg.wptr < tileram + sizeof tileram)
                fprintf(wlog, "%llu %06x %05x %04x\n", (unsigned long long) frame_count, main_pc(), (unsigned)(pg.wptr + o - tileram), v);
            break;
        case K_MISCIO:  miscio_write(a, v, mask); break;
        case K_ROADCTL: roadctl_write(v, mask); break;
        case K_IGNORE:  break;
        default:
            if (mask & 0x00ff) mapper_write(a >> 1, (uint8_t) v);
            break;
    }
}

uint16_t SHBoard::sub_read16(uint32_t a)
{
    a &= 0xffffe;
    if (a < 0x60000)  return be16(&sub_rom[a]);
    if (a < 0x80000)  return be16(&cpu1ram[a & 0x7fff]);
    if (a < 0x90000)  return be16(&roadram[a & 0xfff]);
    if (a < 0xa0000)  return roadctl_read();
    return 0xffff;
}

void SHBoard::sub_write16(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xffffe;
    uint8_t* p = nullptr;
    if (a < 0x60000) return;
    else if (a < 0x80000) p = &cpu1ram[a & 0x7fff];
    else if (a < 0x90000) p = &roadram[a & 0xfff];
    else if (a < 0xa0000) { roadctl_write(v, mask); return; }
    else return;
    if (mask & 0xff00) p[0] = v >> 8;
    if (mask & 0x00ff) p[1] = (uint8_t) v;
}

uint32_t SHBoard::read(int cpu, uint32_t a, int size, int space)
{
    a &= 0xffffff;
    auto r16 = [&](uint32_t x) -> uint16_t {
        if (cpu == 0) return main_read16(x, space);
        if (space == 2 && ((x & 0xffffe) >= 0x90000 && (x & 0xffffe) < 0xa0000)) return 0xffff;
        return sub_read16(x);
    };
    switch (size)
    {
        case 1: { uint16_t w = r16(a & ~1u); return (a & 1) ? (w & 0xff) : (w >> 8); }
        case 2: return r16(a);
        default: return ((uint32_t) r16(a) << 16) | r16((a + 2) & 0xffffff);
    }
}

void SHBoard::write(int cpu, uint32_t a, int size, uint32_t v)
{
    a &= 0xffffff;
    auto w16 = [&](uint32_t x, uint16_t val, uint16_t mask) {
        if (cpu == 0) main_write16(x, val, mask); else sub_write16(x, val, mask);
    };
    switch (size)
    {
        case 1:
            if (a & 1) w16(a & ~1u, (uint16_t)(v & 0xff), 0x00ff);
            else       w16(a, (uint16_t)((v & 0xff) << 8), 0xff00);
            break;
        case 2: w16(a, (uint16_t) v, 0xffff); break;
        default:
            w16(a, (uint16_t)(v >> 16), 0xffff);
            w16((a + 2) & 0xffffff, (uint16_t) v, 0xffff);
            break;
    }
}

// ------------------------------------------------------------------------------------------------
// CPUs, interrupts, timing
// ------------------------------------------------------------------------------------------------
int SHBoard::int_ack(int level)
{
    // Level-held lines stay up until the board drops them; the mapper's HOLD_LINE interrupt
    // clears when acknowledged
    if (cur == 0 && mapper_irq && level == mapper_irq) { mapper_irq = 0; update_irqs(); }
    return M68K_INT_ACK_AUTOVECTOR;
}

void SHBoard::reset_out()
{
    if (cur == 0) sub_reset_pending = true;     // MAME m68k_reset_callback: pulse the second CPU's reset
}

void SHBoard::instr_hook(uint32_t pc)
{
    if (trace) fprintf(trace, "%c %06x\n", cur ? 's' : 'm', pc);
    if (cur == 0)
        for (int i = 0; i < n_hooks; i++)
            if (pc == hook_pc[i]) { hook_fn(hook_ctx, pc); break; }
}

void SHBoard::set_pc_hooks(const uint32_t* pcs, int n, PcHook fn, void* c)
{
    n_hooks = 0;
    if (!fn || n <= 0) return;
    for (int i = 0; i < n && i < MAX_HOOKS; i++) hook_pc[i] = pcs[i] & 0xffffff;
    hook_fn = fn; hook_ctx = c;
    n_hooks = std::min(n, MAX_HOOKS);
}

uint32_t SHBoard::main_reg(int r) const
{
    if (loaded == 0) return m68k_get_reg(nullptr, (m68k_register_t) r);
    return m68k_get_reg((void*) ctx[0].data(), (m68k_register_t) r);
}

void SHBoard::set_main_reg(int r, uint32_t v)
{
    if (loaded == 0) { m68k_set_reg((m68k_register_t) r, v); return; }
    // not in the core (only used from hooks, where it is): change the saved context
    void* saved = std::malloc(m68k_context_size());
    m68k_get_context(saved);
    m68k_set_context(ctx[0].data());
    m68k_set_reg((m68k_register_t) r, v);
    m68k_get_context(ctx[0].data());
    m68k_set_context(saved);
    std::free(saved);
}

static int level_main(bool irq2, bool vblank, int mapper_irq)
{
    int l = 0;
    if (irq2) l = 2;
    if (vblank) l = 4;
    if (irq2 && vblank) l = 6;
    return std::max(l, mapper_irq);
}

void SHBoard::update_irqs()
{
    // applied to each core when it is next selected (and now, for the one in the core)
    if (loaded == 0) m68k_set_irq(level_main(irq2, vblank, mapper_irq));
    if (loaded == 1) m68k_set_irq(vblank ? 4 : 0);
    if (irq2 || vblank) perfect_until = now + PERFECT;
}

void SHBoard::select(int cpu)
{
    if (loaded == cpu) return;
    if (loaded >= 0) m68k_get_context(ctx[loaded].data());
    m68k_set_context(ctx[cpu].data());
    loaded = cpu;
    cur = cpu;
    if (cpu == 1 && sub_reset_pending) { m68k_pulse_reset(); sub_reset_pending = false; }
    m68k_set_irq(cpu == 0 ? level_main(irq2, vblank, mapper_irq) : (vblank ? 4 : 0));
}

uint32_t SHBoard::main_pc() const
{
    if (loaded == 0) return m68k_get_reg(nullptr, M68K_REG_PC);
    return m68k_get_reg((void*) ctx[0].data(), M68K_REG_PC);
}

uint32_t SHBoard::sub_pc() const
{
    if (loaded == 1) return m68k_get_reg(nullptr, M68K_REG_PC);
    return m68k_get_reg((void*) ctx[1].data(), M68K_REG_PC);
}

void SHBoard::run_until(double t)
{
    while (now < t)
    {
        bool perfect = now < perfect_until;
        double tq = std::min(t, now + (perfect ? 1.0 : QUANTUM));
        for (int c = 0; c < 2; c++)
        {
            if (cpu_time[c] >= tq) continue;
            select(c);
            int n = (int) std::ceil(tq - cpu_time[c]);
            cpu_time[c] += m68k_execute(n);
        }
        now = tq;
    }
}

void SHBoard::run_frame()
{
    if (!ready) return;
    xo_bus = this;                                          // the Musashi core is this board's
    loaded = -1;                                            // (another board may have used it)
    const double L = LINE_CYCLES;
    // The frame runs from the start of line 224 (VBLANK, when the screen is drawn) to the next
    // one; the very first runs from power-on at line 0.
    int start_line = first_frame ? 0 : 224;
    auto at = [&](int line, double hfrac) { return ((line - start_line + 262) % 262 + hfrac) * L; };
    double end = first_frame ? 224 * L : 262 * L;

    for (int c = 0; c < 2; c++) cpu_time[c] -= frame_len;   // carry overshoot into this frame
    now = 0;
    frame_len = end;

    struct Ev { double t; int what; };
    std::vector<Ev> ev;
    if (!first_frame) ev.push_back({ 0.0, 0 });             // line 224: VBLANK off
    for (int line : { 65, 129, 193 })
    {
        ev.push_back({ at(line, 320.0 / 400.0), 1 });        // IRQ2 on (horizontal blank)
        ev.push_back({ at(line + 1, 0.0), 2 });              // IRQ2 off
    }
    ev.push_back({ at(223, 0.0), 3 });                       // VBLANK on
    ev.push_back({ at(261, 0.0), 4 });                       // tilemap registers latched
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });

    for (const Ev& e : ev)
    {
        if (e.t >= end) continue;
        run_until(e.t);
        switch (e.what)
        {
            case 0: vblank = false; break;
            case 1: irq2 = true; break;
            case 2: irq2 = false; break;
            case 3: vblank = true; break;
            case 4:
                for (int i = 0; i < 4; i++)
                {
                    latched_page[i]    = be16(&textram[0xe80 + 2 * i]);
                    latched_yscroll[i] = be16(&textram[0xe90 + 2 * i]);
                    latched_xscroll[i] = be16(&textram[0xe98 + 2 * i]);
                }
                continue;                                   // no interrupt change
        }
        update_irqs();
    }
    run_until(end);
    if (loaded >= 0) m68k_get_context(ctx[loaded].data());
    loaded = -1;
    first_frame = false;
    frame_count++;
}
