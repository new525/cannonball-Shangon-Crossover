/***************************************************************************
    System H1 sound (Cool Riders). See crsound.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/crsound.hpp"
#include "crossover/m68k/m68k.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

static const uint32_t CPU_HZ  = 16000000;      // XTAL(32'000'000)/2: the 68000 and the SH-1
static const uint32_t SCSP_HZ = 22579000;      // XTAL(22'579'000)

// ------------------------------------------------------------------------------------------------
// MAME device_execute_interface arithmetic
// ------------------------------------------------------------------------------------------------
void CRSound::Exec::set_clock(uint32_t hz)
{
    cps = hz;
    atto_per_cycle = CrTime::ATTO / (int64_t) hz;
    int64_t attos = atto_per_cycle;
    divshift = 0;
    while (attos >= (1LL << 31)) { divshift++; attos >>= 1; }
    divisor = (uint64_t) attos;
}

CrTime CRSound::Exec::cycles_to_time(int cycles) const
{
    if (cycles < (int) cps) return CrTime(0, atto_per_cycle * cycles);
    int64_t secs = cycles / cps, rem = cycles % cps;
    return CrTime(secs, rem * atto_per_cycle);
}

CRSound::CRSound() {}
CRSound::~CRSound() {}

// ------------------------------------------------------------------------------------------------
// Setup
// ------------------------------------------------------------------------------------------------
bool CRSound::init(const std::vector<uint8_t>& maincpu, const std::vector<uint8_t>& gfx, const std::vector<uint8_t>& sub)
{
    ready = false;
    if (maincpu.size() < 0x200000 || gfx.size() < 0x1000000 || sub.size() < 0x20000) return false;
    main_rom = maincpu; gfx_rom = gfx; sub_rom = sub;
    sh.reset(new ShCpu(*this, true, 0xc7ffffff));
    m68k_init();
    m68k_ctx.assign(m68k_context_size(), 0);
    ex[0].set_clock(CPU_HZ);
    ex[1].set_clock(CPU_HZ);
    frame_attos = CrTime::ATTO / 57;                        // set_refresh_hz(57)
    scan_attos = frame_attos / 512;                         // 512 lines
    sample_attos = CrTime::ATTO / (int64_t)(SCSP_HZ / 512);
    ready = true;
    return true;
}

void CRSound::power_on(const uint8_t* nv)
{
    std::memset(soundram, 0, sizeof soundram);
    std::memset(sh_ram_onchip, 0, sizeof sh_ram_onchip);
    std::memset(comm_ram, 0, sizeof comm_ram);
    std::memset(share3, 0, sizeof share3);
    std::memset(ram_5000, 0, sizeof ram_5000);
    std::memset(ram_5200, 0, sizeof ram_5200);
    std::memset(ram_6200, 0, sizeof ram_6200);
    std::memset(sh_regs, 0, sizeof sh_regs);
    std::memset(m68k_ram, 0, sizeof m68k_ram);
    if (nv) std::memcpy(nvram, nv, sizeof nvram); else std::memset(nvram, 0, sizeof nvram);
    std::memset(sound_dma, 0, sizeof sound_dma);
    sound_data = sound_fifo = 0;
    for (int i = 0; i < 7; i++) io_value[i] = 0xff;
    io_config = 0xff; io_mode = 0; io_an = 0;

    // scheduler state first (the SCSP callbacks use it)
    timers.clear(); timer_seq = 0;
    executing = -1;
    basetime = CrTime(); target = CrTime();
    for (Exec& e : ex) { e.local = CrTime(); e.suspended = e.next_suspended = false; e.cycles_running = 0; }
    suspend_changes = false;
    frame_no = 0; scanline = 0; frame_start = CrTime();
    mainw.clear(); mainw_pos = 0;
    for (int& v : irq68) v = 0;
    for (int& v : sh_irq_state) v = 0;
    sh_cycles = m68k_cycles = 0;

    // SCSPs: chip 1 interrupts the 68000 (irq_cb set) and the SH-1; chip 2 only the SH-1
    for (int c = 0; c < 2; c++)
    {
        Scsp& s = scsp[c];
        s.read_word  = [this, c](uint32_t a) -> uint16_t { return a < 0x80000 ? soundram[c][a >> 1] : 0; };
        s.write_word = [this, c](uint32_t a, uint16_t v) { if (a < 0x80000) soundram[c][a >> 1] = v; };
        s.has_irq_cb = (c == 0);
        s.irq_cb = [this](uint32_t level, int state) { set_68k_line((int) level, state); };
        s.main_irq_cb = [this, c](int state)
        {
            uint8_t bit = c == 0 ? 0x10 : 0x20;
            set_sh_line(0xe, state ? ShCpu::ASSERT_LINE : ShCpu::CLEAR_LINE);
            if (state) sound_data |= bit; else sound_data &= (uint8_t) ~bit;
        };
        s.timer_arm = [this, c](int which, const ScspTimerPeriod& p)
        {
            timer_remove(T_SCSP, c * 4 + which);
            timer_add(cur_time() + CrTime(p.seconds, p.attoseconds), T_SCSP, c * 4 + which);
        };
        s.rand = nullptr;                                   // the port's copy of MAME's generator
        s.init(SCSP_HZ);
        gen[c] = 0;
        tmp_l[c].clear(); tmp_r[c].clear();
    }
    mixed = 0;
    out_l.clear(); out_r.clear();

    timer_add(CrTime(), T_SCANLINE, 0);
    timer_add(CrTime(0, CrTime::ATTO / 50), T_STREAM, 0);

    // CPUs: the SH-1 from its vectors; the 68000 held in reset (machine_reset asserts its RESET)
    sh->reset();
    xo_bus = this;
    m68k_set_context(m68k_ctx.data());
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_get_context(m68k_ctx.data());
    m68k_loaded = false;
    m68k_reset_line = true;
    ex[0].suspended = ex[0].next_suspended = true;
}

void CRSound::main_write(const CrTime& t, uint32_t offset, uint32_t data, uint32_t mask)
{
    mainw.push_back({ t, offset & 0xfffc, data, mask });
}

void CRSound::apply_main_writes(const CrTime& upto)
{
    while (mainw_pos < mainw.size() && mainw[mainw_pos].t < upto)
    {
        const MainW& w = mainw[mainw_pos++];
        for (int b = 0; b < 4; b++)
        {
            uint32_t m = 0xffu << (24 - 8 * b);
            if (w.mask & m) share3[w.off + b] = (uint8_t)(w.data >> (24 - 8 * b));
        }
    }
    if (mainw_pos > 4096) { mainw.erase(mainw.begin(), mainw.begin() + (long) mainw_pos); mainw_pos = 0; }
}

// ------------------------------------------------------------------------------------------------
// Scheduler (MAME's device_scheduler::timeslice for these two CPUs)
// ------------------------------------------------------------------------------------------------
void CRSound::timer_add(const CrTime& when, int kind, int param)
{
    Timer t = { when, kind, param, timer_seq++ };
    auto it = timers.begin();
    while (it != timers.end() && it->expire <= when) ++it;     // after equal expiries, as MAME's list
    bool head = it == timers.begin();
    timers.insert(it, t);
    if (head) abort_slice();
}

void CRSound::timer_remove(int kind, int param)
{
    for (auto it = timers.begin(); it != timers.end(); ++it)
        if (it->kind == kind && it->param == param) { timers.erase(it); return; }
}

CrTime CRSound::cur_time() const
{
    if (executing == 0) return ex[0].local + ex[0].cycles_to_time(m68k_cycles_run() + m68k_access_cycles);
    if (executing == 1) return ex[1].local + ex[1].cycles_to_time(ex[1].cycles_running - sh->icount());
    return basetime;
}

void CRSound::abort_slice()
{
    if (executing == 0) m68k_end_timeslice();
    else if (executing == 1) sh->end_timeslice();
}

void CRSound::run_until(const CrTime& t)
{
    if (!ready) return;
    timer_add(t, T_MAINW, -1);              // a timer at t ends the run there
    while (basetime < t) timeslice();
    timer_remove(T_MAINW, -1);
    scsp_catch_up_all();
    mix_out();
}

void CRSound::timeslice()
{
    const CrTime quantum(0, CrTime::ATTO / 60);    // no perfect quantum: 60 Hz maximum
    while (!timers.empty() && basetime < timers.front().expire)
    {
        target = basetime + quantum;
        if (timers.front().expire < target) target = timers.front().expire;
        apply_main_writes(target);          // the main SH-2 runs first in MAME's execute order

        if (suspend_changes)
        {
            for (Exec& e : ex) e.suspended = e.next_suspended;
            suspend_changes = false;
        }

        for (int i = 0; i < 2; i++)
        {
            Exec& e = ex[i];
            if (e.suspended) { if (e.local < target) e.local = target; continue; }
            if (target.s < e.local.s) continue;
            CrTime d = target - e.local;
            if (d.s < 0 || (d.s == 0 && d.a < e.atto_per_cycle)) continue;
            int64_t delta = d.s * CrTime::ATTO + d.a;
            int want = e.cycles_running = (int)(((uint64_t) delta >> e.divshift) / e.divisor);
            int ran;
            executing = i;
            if (i == 0)
            {
                m68k_load();
                ran = m68k_execute(want);
                m68k_save();
                m68k_cycles += (uint64_t) ran;
            }
            else
            {
                ran = sh->run(want);
                sh_cycles += (uint64_t) ran;
            }
            executing = -1;
            e.local = e.local + e.cycles_to_time(ran);
            if (e.local < target) target = std::max(e.local, basetime);
        }
        basetime = target;
    }

    while (!timers.empty() && timers.front().expire <= basetime)
    {
        Timer t = timers.front();
        timers.erase(timers.begin());
        fire(t);
    }
}

void CRSound::fire(const Timer& t)
{
    switch (t.kind)
    {
        case T_SCANLINE:
        {
            // coolridr interrupt_sub: level 12 (HOLD_LINE) at scanline 384
            if (scanline == 384) set_sh_line(0xc, ShCpu::HOLD_LINE);
            if (++scanline == 512) { scanline = 0; frame_no++; frame_start = frame_start + CrTime(0, frame_attos); }
            timer_add(frame_start + CrTime(0, (int64_t) scanline * scan_attos), T_SCANLINE, 0);
            break;
        }
        case T_STREAM:
            scsp_catch_up_all();
            timer_add(t.expire + CrTime(0, CrTime::ATTO / 50), T_STREAM, 0);
            break;
        case T_SCSP:
            scsp_catch_up(t.param >> 2);
            scsp[t.param >> 2].timer_fire(t.param & 3);
            break;
        case T_SYNC_IRQ68:
            irq68[t.param >> 8] = t.param & 0xff;
            m68k_update_ipl();
            break;
        case T_SYNC_IRQSH:
            sh_irq_state[t.param >> 8] = t.param & 0xff;
            sh->set_irq_state(t.param >> 8, t.param & 0xff);
            break;
        case T_SYNC_RESET68:
            if (t.param)                                    // assert: the CPU halts
            {
                m68k_reset_line = true;
                ex[0].next_suspended = true; suspend_changes = true;
            }
            else if (m68k_reset_line)                       // clear after an assert: reset, then run
            {
                m68k_reset_line = false;
                m68k_load();
                m68k_pulse_reset();
                m68k_save();
                ex[0].next_suspended = false; suspend_changes = true;
            }
            break;
        case T_MAINW:
            break;
    }
}

// MAME's set_input_line goes through a synchronising timer at the current time
void CRSound::set_68k_line(int line, int state)  { timer_add(cur_time(), T_SYNC_IRQ68, (line << 8) | state); }
void CRSound::set_sh_line(int line, int state)   { timer_add(cur_time(), T_SYNC_IRQSH, (line << 8) | state); }
void CRSound::set_68k_reset(bool asserted)       { timer_add(cur_time(), T_SYNC_RESET68, asserted ? 1 : 0); }

// ------------------------------------------------------------------------------------------------
// 68000 (Musashi, shared with the other boards through xo_bus)
// ------------------------------------------------------------------------------------------------
void CRSound::m68k_load()
{
    xo_bus = this;
    m68k_set_context(m68k_ctx.data());
    m68k_loaded = true;
    m68k_update_ipl();
}

void CRSound::m68k_save()
{
    m68k_get_context(m68k_ctx.data());
    m68k_loaded = false;
}

void CRSound::m68k_update_ipl()
{
    int level = 0;
    for (int l = 7; l >= 1; l--) if (irq68[l]) { level = l; break; }
    // From a timer: the 68000 may have run past the timer's time (its last instruction ended
    // after it); MAME's core sees the new level that many cycles in the past.
    int ago = 0;
    if (executing != 0 && basetime < ex[0].local)
    {
        CrTime d = ex[0].local - basetime;
        ago = (int)((d.s * CrTime::ATTO + d.a) / ex[0].atto_per_cycle);
    }
    if (m68k_loaded) m68k_set_irq_ago((unsigned) level, ago);
    else
    {
        std::vector<uint8_t> saved(m68k_context_size());
        m68k_get_context(saved.data());
        m68k_set_context(m68k_ctx.data());
        m68k_set_irq_ago((unsigned) level, ago);
        m68k_get_context(m68k_ctx.data());
        m68k_set_context(saved.data());
    }
}

void CRSound::instr_hook(uint32_t pc)
{
    if (!m68k_trace) return;
    double t = cur_time().sec();
    if (t >= trace_from && t < trace_to)
        fprintf(m68k_trace, "%llX %06X\n", (unsigned long long)(m68k_cycles + (uint64_t) m68k_cycles_run()), pc & 0xffffff);
}

int CRSound::int_ack(int level)
{
    // HOLD_LINE (the SCSP's DMA-end interrupt) clears on acknowledge
    if (level >= 1 && level <= 7 && irq68[level] == 2) { irq68[level] = 0; m68k_update_ipl(); }
    return M68K_INT_ACK_AUTOVECTOR;
}

uint32_t CRSound::bus_read(uint32_t a, int size, int)
{
    a &= 0xffffff;
    if (size == 1) { uint16_t w = m68k_read16(a & ~1u); return (a & 1) ? (w & 0xff) : (w >> 8); }
    if (size == 2) return m68k_read16(a);
    return (uint32_t) m68k_read16(a) << 16 | m68k_read16(a + 2);
}

// Musashi makes an instruction's accesses at its start; MAME's 68000 makes them at their bus
// cycles. Most writes are the instruction's last data access, followed by the final prefetch: a
// word write starts 8 cycles before the end, a long write's words 12 and 8 cycles before it
// (MAME 0.264 m68000-sdf.cpp, e.g. move_w_imm16_dad_df: write after 8 of its 16 cycles).
void CRSound::bus_write(uint32_t a, int size, uint32_t v)
{
    a &= 0xffffff;
    int end = m68k_instruction_cycles();
    m68k_access_cycles = std::max(0, end - (size == 4 ? 12 : 8));
    bus_write_at(a, size, v);
    m68k_access_cycles = 0;
}

void CRSound::bus_write_at(uint32_t a, int size, uint32_t v)
{
    if (size == 1)
    {
        uint16_t w = (uint16_t)((v & 0xff) * 0x101);
        m68k_write16(a & ~1u, w, (a & 1) ? 0x00ff : 0xff00);
        return;
    }
    if (size == 2) { m68k_write16(a, (uint16_t) v, 0xffff); return; }
    m68k_write16(a, (uint16_t)(v >> 16), 0xffff);
    m68k_access_cycles += 4;
    m68k_write16(a + 2, (uint16_t) v, 0xffff);
}

// system_h1_sound_map
uint16_t CRSound::m68k_read16(uint32_t a)
{
    if (a < 0x080000) return soundram[0][a >> 1];
    if (a >= 0x100000 && a < 0x101000) { scsp_catch_up(0); return scsp[0].read((a - 0x100000) >> 1); }
    if (a >= 0x200000 && a < 0x280000) return soundram[1][(a - 0x200000) >> 1];
    if (a >= 0x300000 && a < 0x301000) { scsp_catch_up(1); return scsp[1].read((a - 0x300000) >> 1); }
    if ((a & ~0x20ffffu) == 0x800000)                     // $800000-$80FFFF, mirror $200000
    {
        uint32_t o = a & 0xfffe;
        return (uint16_t)(m68k_ram[o] << 8 | m68k_ram[o + 1]);
    }
    return 0;
}

void CRSound::m68k_write16(uint32_t a, uint16_t v, uint16_t mask)
{
    auto put = [&](uint16_t& w) { w = (uint16_t)((w & ~mask) | (v & mask)); };
    if (a < 0x080000) { put(soundram[0][a >> 1]); return; }
    if (scsp_log && ((a >= 0x100000 && a < 0x101000) || (a >= 0x300000 && a < 0x301000)))
    {
        CrTime t = cur_time();
        fprintf(scsp_log, "%lld %lld %06x %04x %04x\n", (long long) t.s, (long long) t.a, a, v, mask);
    }
    if (a >= 0x100000 && a < 0x101000) { scsp_catch_up(0); scsp[0].write((a - 0x100000) >> 1, v, mask); return; }
    if (a >= 0x200000 && a < 0x280000) { put(soundram[1][(a - 0x200000) >> 1]); return; }
    if (a >= 0x300000 && a < 0x301000) { scsp_catch_up(1); scsp[1].write((a - 0x300000) >> 1, v, mask); return; }
    if ((a & ~0x20ffffu) == 0x800000)
    {
        uint32_t o = a & 0xfffe;
        if (mask & 0xff00) m68k_ram[o] = (uint8_t)(v >> 8);
        if (mask & 0x00ff) m68k_ram[o + 1] = (uint8_t) v;
        return;
    }
    if (a == 0x900000 && (mask & 0x00ff)) sound_fifo = (uint8_t) v;   // $900001: sound_to_sh1_w
}

// ------------------------------------------------------------------------------------------------
// SH-1 (coolridr_submap)
// ------------------------------------------------------------------------------------------------
static inline uint32_t be32(const uint8_t* p) { return (uint32_t) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

uint8_t  CRSound::read8(uint32_t a)  { return (uint8_t) sh_read(a, 1); }
uint16_t CRSound::read16(uint32_t a) { return (uint16_t) sh_read(a & ~1u, 2); }
uint32_t CRSound::read32(uint32_t a) { return sh_read(a & ~3u, 4); }
void CRSound::write8(uint32_t a, uint8_t v)   { sh_write(a, 1, v); }
void CRSound::write16(uint32_t a, uint16_t v) { sh_write(a & ~1u, 2, v); }
void CRSound::write32(uint32_t a, uint32_t v) { sh_write(a & ~3u, 4, v); }

void CRSound::irq_acknowledge(int line)
{
    if (line >= 0 && line < 16 && sh_irq_state[line] == ShCpu::HOLD_LINE)
    {
        sh_irq_state[line] = 0;
        sh->set_irq_state(line, ShCpu::CLEAR_LINE);
    }
}

// Byte-addressed RAM areas of the SH-1's 32-bit big-endian space
static inline uint32_t ram_read(const uint8_t* m, uint32_t o, int size)
{
    if (size == 1) return m[o];
    if (size == 2) return (uint32_t) m[o] << 8 | m[o + 1];
    return be32(m + o);
}
static inline void ram_write(uint8_t* m, uint32_t o, int size, uint32_t v)
{
    for (int i = 0; i < size; i++) m[o + i] = (uint8_t)(v >> (8 * (size - 1 - i)));
}

uint32_t CRSound::sh_read(uint32_t a, int size)
{
    if (a < 0x20000) return ram_read(sub_rom.data(), a, size);
    if (a >= 0x01000000 && a < 0x01010000) return ram_read(comm_ram, a - 0x01000000, size);
    // sound RAM and SCSPs: 16-bit devices on the 32-bit bus
    int chip = -1; bool regs = false; uint32_t base = 0;
    if (a >= 0x03000000 && a < 0x03080000) { chip = 0; base = 0x03000000; }
    else if (a >= 0x03100000 && a < 0x03101000) { chip = 0; regs = true; base = 0x03100000; }
    else if (a >= 0x03200000 && a < 0x03280000) { chip = 1; base = 0x03200000; }
    else if (a >= 0x03300000 && a < 0x03301000) { chip = 1; regs = true; base = 0x03300000; }
    if (chip >= 0)
    {
        auto rd = [&](uint32_t wa) -> uint16_t
        {
            uint32_t off = (wa - base) >> 1;
            if (regs) { scsp_catch_up(chip); return scsp[chip].read(off); }
            return soundram[chip][off];
        };
        if (size == 4) return (uint32_t) rd(a) << 16 | rd(a + 2);
        uint16_t w = rd(a & ~1u);
        if (size == 2) return w;
        return (a & 1) ? (w & 0xff) : (w >> 8);
    }
    if (a >= 0x04000000 && a < 0x04000040)
    {
        uint32_t v = dma_r((int)((a - 0x04000000) >> 2));
        if (size == 4) return v;
        int sh = (int)(8 * (4 - size - (a & 3)));
        return (v >> sh) & (size == 2 ? 0xffff : 0xff);
    }
    if (a >= 0x05000000 && a < 0x05001000) return ram_read(ram_5000, a - 0x05000000, size);
    if (a >= 0x05200000 && a < 0x05200200) return ram_read(ram_5200, a - 0x05200000, size);
    if (a >= 0x05300000 && a < 0x05310000) return ram_read(share3, a - 0x05300000, size);
    if (a >= 0x05fffe00 && a < 0x06000000)
    {
        uint32_t o = (a - 0x05fffe00) >> 1;
        if (size == 4) return (uint32_t) sh_regs[o] << 16 | sh_regs[o + 1];
        uint16_t w = sh_regs[o];
        if (size == 2) return w;
        return (a & 1) ? (w & 0xff) : (w >> 8);
    }
    if (a >= 0x06000000 && a < 0x06000200) return ram_read(nvram, a - 0x06000000, size);
    if (a >= 0x06100000 && a < 0x06100020)
    {
        // 8-bit device on byte lanes 1 and 3 (umask32 0x00ff00ff)
        auto lane = [&](uint32_t ba) -> uint8_t { return (ba & 1) ? io_read((int)((ba - 0x06100000) >> 1)) : 0; };
        uint32_t v = 0;
        for (int i = 0; i < size; i++) v = v << 8 | lane(a + (uint32_t) i);
        return v;
    }
    if (a >= 0x06200000 && a < 0x06201000) return ram_read(ram_6200, a - 0x06200000, size);
    if (a >= 0x07ffe000 && a < 0x08000000) return ram_read(sh_ram_onchip, a - 0x07ffe000, size);
    return 0;
}

void CRSound::sh_write(uint32_t a, int size, uint32_t v)
{
    if (a < 0x20000) return;
    if (a >= 0x01000000 && a < 0x01010000) { ram_write(comm_ram, a - 0x01000000, size, v); return; }
    int chip = -1; bool regs = false; uint32_t base = 0;
    if (a >= 0x03000000 && a < 0x03080000) { chip = 0; base = 0x03000000; }
    else if (a >= 0x03100000 && a < 0x03101000) { chip = 0; regs = true; base = 0x03100000; }
    else if (a >= 0x03200000 && a < 0x03280000) { chip = 1; base = 0x03200000; }
    else if (a >= 0x03300000 && a < 0x03301000) { chip = 1; regs = true; base = 0x03300000; }
    if (chip >= 0)
    {
        auto wr = [&](uint32_t wa, uint16_t d, uint16_t mask)
        {
            uint32_t off = (wa - base) >> 1;
            if (regs) { scsp_catch_up(chip); scsp[chip].write(off, d, mask); return; }
            soundram[chip][off] = (uint16_t)((soundram[chip][off] & ~mask) | (d & mask));
        };
        if (size == 4) { wr(a, (uint16_t)(v >> 16), 0xffff); wr(a + 2, (uint16_t) v, 0xffff); return; }
        if (size == 2) { wr(a, (uint16_t) v, 0xffff); return; }
        wr(a & ~1u, (uint16_t)((v & 0xff) * 0x101), (a & 1) ? 0x00ff : 0xff00);
        return;
    }
    if (a >= 0x04000000 && a < 0x04000040)
    {
        int off = (int)((a - 0x04000000) >> 2);
        if (size == 4) { dma_w(off, v, 0xffffffff); return; }
        int sh = (int)(8 * (4 - size - (a & 3)));
        uint32_t m = (size == 2 ? 0xffffu : 0xffu) << sh;
        dma_w(off, v << sh, m);
        return;
    }
    if (a >= 0x05000000 && a < 0x05001000) { ram_write(ram_5000, a - 0x05000000, size, v); return; }
    if (a >= 0x05200000 && a < 0x05200200) { ram_write(ram_5200, a - 0x05200000, size, v); return; }
    if (a >= 0x05300000 && a < 0x05310000) { ram_write(share3, a - 0x05300000, size, v); return; }
    if (a >= 0x05fffe00 && a < 0x06000000)
    {
        uint32_t o = (a - 0x05fffe00) >> 1;
        if (size == 4) { sh_regs[o] = (uint16_t)(v >> 16); sh_regs[o + 1] = (uint16_t) v; return; }
        if (size == 2) { sh_regs[o] = (uint16_t) v; return; }
        uint16_t m = (a & 1) ? 0x00ff : 0xff00;
        sh_regs[o] = (uint16_t)((sh_regs[o] & ~m) | ((v & 0xff) * 0x101 & m));
        return;
    }
    if (a >= 0x06000000 && a < 0x06000200) { ram_write(nvram, a - 0x06000000, size, v); return; }
    if (a >= 0x06100000 && a < 0x06100020)
    {
        for (int i = 0; i < size; i++)
        {
            uint32_t ba = a + (uint32_t) i;
            if (ba & 1) io_write((int)((ba - 0x06100000) >> 1), (uint8_t)(v >> (8 * (size - 1 - i))));
        }
        return;
    }
    if (a >= 0x06200000 && a < 0x06201000) { ram_write(ram_6200, a - 0x06200000, size, v); return; }
    if (a >= 0x07ffe000 && a < 0x08000000) { ram_write(sh_ram_onchip, a - 0x07ffe000, size, v); return; }
}

// ------------------------------------------------------------------------------------------------
// Sound DMA at $04000000 (as MAME's driver describes it)
//   register 0/4: source (the main CPU's address space); 1/5: destination (the 68000's address space);
//   2/6: bits 31-16 = length in units of $40 bytes, bit 0 = start on a 0->1 transition (status reads 0);
//   8: read = interrupt status (bit 4 SCSP 1, bit 5 SCSP 2); write = the 68000's RESET line
// ------------------------------------------------------------------------------------------------
uint16_t CRSound::main_read16(uint32_t a) const
{
    a &= ~1u;
    if (a < 0x200000) return (uint16_t)(main_rom[a] << 8 | main_rom[a + 1]);
    if (a >= 0x01000000 && a < 0x02000000) { uint32_t o = a - 0x01000000; return (uint16_t)(gfx_rom[o] << 8 | gfx_rom[o + 1]); }
    if (a >= 0x20000000 && a < 0x20200000) { uint32_t o = a - 0x20000000; return (uint16_t)(main_rom[o] << 8 | main_rom[o + 1]); }
    return 0;                       // the main CPU's RAM and devices are not emulated
}

uint32_t CRSound::dma_r(int offset)
{
    if (offset == 8) return sound_data;
    if (offset == 2 || offset == 6) return 0;
    return sound_dma[offset];
}

void CRSound::dma_w(int offset, uint32_t data, uint32_t mask)
{
    if (offset == 8)
    {
        set_68k_reset(data != 0);
        return;
    }
    if ((offset == 2 || offset == 6) && (data & 1) && !(sound_dma[offset] & 1))
    {
        uint32_t src = sound_dma[offset - 2], dst = sound_dma[offset - 1];
        uint32_t size = (sound_dma[offset] >> 16) * 0x40;
        if (dma_log) fprintf(dma_log, "%.9f %d %08x %08x %x\n", cur_time().sec(), offset / 4, src, dst, size);
        for (uint32_t i = 0; i < size; i += 2)
        {
            m68k_write16(dst & 0xffffff, main_read16(src), 0xffff);
            src += 2; dst += 2;
        }
    }
    sound_dma[offset] = (sound_dma[offset] & ~mask) | (data & mask);
}

// 315-5649 (MAME's sega_315_5649_device)
uint8_t CRSound::io_read(int offset)
{
    uint8_t data = 0xff;
    switch (offset)
    {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6:
            data = (io_config >> offset) & 1 ? in_port[offset] : io_value[offset];
            break;
        case 0x0d: data = 0x0c; break;
        case 0x0f: data = an_port[io_an]; io_an = (io_an + 1) & 7; break;
        default: break;
    }
    return data;
}

void CRSound::io_write(int offset, uint8_t data)
{
    switch (offset)
    {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: io_value[offset] = data; break;
        case 0x08: io_config = data; break;
        case 0x0e: io_mode = data; break;
        case 0x0f: io_an = data & 7; break;
        default: break;
    }
}

// ------------------------------------------------------------------------------------------------
// SCSP output: generated up to the current time before each access (MAME's stream update), mixed
// as MAME's speakers mix both chips (each output in +-1.0 times the chip's MVOL gain)
// ------------------------------------------------------------------------------------------------
void CRSound::scsp_catch_up(int chip)
{
    CrTime t = cur_time();
    // MAME: the view ends at the buffer index of the current time, rounded up
    int64_t total = t.s * (int64_t)(SCSP_HZ / 512) + (t.a + sample_attos - 1) / sample_attos;
    if ((uint64_t) total <= gen[chip]) return;
    int n = (int)((uint64_t) total - gen[chip]);
    std::vector<int32_t> l((size_t) n), r((size_t) n);
    scsp[chip].generate(n, l.data(), r.data());
    float k = scsp[chip].output_gain() / (float) scsp[chip].output_max();
    for (int i = 0; i < n; i++)
    {
        tmp_l[chip].push_back((int32_t)((float) l[(size_t) i] * k * 32768.0f));
        tmp_r[chip].push_back((int32_t)((float) r[(size_t) i] * k * 32768.0f));
    }
    gen[chip] = (uint64_t) total;
    mix_out();
}

void CRSound::mix_out()
{
    size_t n = std::min(tmp_l[0].size(), tmp_l[1].size());
    for (size_t i = 0; i < n; i++)
    {
        out_l.push_back((float)(tmp_l[0][i] + tmp_l[1][i]) / 32768.0f);
        out_r.push_back((float)(tmp_r[0][i] + tmp_r[1][i]) / 32768.0f);
    }
    for (int c = 0; c < 2; c++)
    {
        tmp_l[c].erase(tmp_l[c].begin(), tmp_l[c].begin() + (long) n);
        tmp_r[c].erase(tmp_r[c].begin(), tmp_r[c].begin() + (long) n);
    }
    mixed += n;
}
