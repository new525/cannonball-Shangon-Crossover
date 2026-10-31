/***************************************************************************
    Virtual Sega sound board. See soundboard.hpp.
***************************************************************************/
#include "crossover/soundboard.hpp"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include "crossover/ymfm/ymfm_opm.h"
#include "crossover/ymfm/ymfm_opn.h"

static const int64_t CLOCK       = 4000000;    // Z80 clock (both boards); time is counted in Z80 cycles
static const int     PCM_BANKSHIFT = 12;       // BANK_512 (MAME segapcm.h set_bank)
static const int     PCM_BANKMASK  = 0x70;
// Mix (MAME routes): OutRun board YM2151 x 0.43 per side; Hang-On board YM2203 FM x 0.37 and each
// SSG channel x 0.13, both mono to both sides; PCM x 1.0 on both boards.
static const double  OPM_GAIN = 0.43, OPN_FM_GAIN = 0.37, OPN_SSG_GAIN = 0.13, PCM_GAIN = 1.0;

struct SegaSoundBoard::Iface : public ymfm::ymfm_interface
{
    SegaSoundBoard* b;
    explicit Iface(SegaSoundBoard* board) : b(board) {}
    void ymfm_set_timer(uint32_t tnum, int32_t clocks) override { b->ym_set_timer(tnum, clocks); }
    void ymfm_set_busy_end(uint32_t clocks) override { b->ym_set_busy_end(clocks); }
    bool ymfm_is_busy() override { return b->ym_is_busy(); }
    void ymfm_update_irq(bool asserted) override { b->ym_update_irq(asserted); }
    void expire(uint32_t tnum) { m_engine->engine_timer_expired(tnum); }
};

SegaSoundBoard::SegaSoundBoard() {}
SegaSoundBoard::~SegaSoundBoard() {}

bool SegaSoundBoard::init(Kind kind_, const std::vector<uint8_t>& z80rom, const std::vector<uint8_t>& pcm, int out_rate_, int fps_)
{
    ready = false;
    if (z80rom.empty() || pcm.empty()) return false;
    kind = kind_;
    out_rate = out_rate_; fps = fps_;
    size_t rom_end = kind == Kind::OUTRUN ? 0xF000 : 0x8000;
    std::memset(rom, 0, sizeof rom);
    std::memcpy(rom, z80rom.data(), std::min(z80rom.size(), rom_end));
    pcm_rom = pcm;
    uint32_t sz = 1;
    while (sz < pcm_rom.size()) sz <<= 1;
    pcm_rom.resize(sz, 0);
    pcm_mask = sz - 1;                         // device_rom_interface: a smaller region is mirrored
    iface.reset(new Iface(this));
    opm.reset(); opn.reset();
    if (kind == Kind::OUTRUN)
    {
        opm.reset(new ymfm::ym2151(*iface));
        ym_period = (double) CLOCK / opm->sample_rate(CLOCK);   // clock / 64
        pcm_div = 128;                                           // PCM at 4 MHz: clock / 128
        cmd_spacing = 192;     // 48 us between the 8 writes in MAME's log of Super Hang-On (sub_018E0)
    }
    else
    {
        opn.reset(new ymfm::ym2203(*iface));
        ym_period = (double) CLOCK / opn->sample_rate(CLOCK);
        pcm_div = 64;                                            // PCM at 8 MHz: 62500 Hz
        cmd_spacing = 230;     // 57.5 us between the 8 latch writes in MAME's log of Hang-On
    }
    reset();
    ready = true;
    return true;
}

void SegaSoundBoard::reset()
{
    std::memset(ram, 0, sizeof ram);
    std::memset(pcm_ram, 0xFF, sizeof pcm_ram);       // MAME segapcm device_start
    std::memset(pcm_low, 0, sizeof pcm_low);
    z80_init(&cpu);
    cpu.read_byte = mem_r;
    cpu.write_byte = mem_w;
    cpu.port_in = io_r;
    cpu.port_out = io_w;
    cpu.userdata = this;
    now = 0; frame_end = 0; frame_frac = 0; ym_clock = 0; pcm_clock = 0;
    timer_end[0] = timer_end[1] = -1;
    busy_end = 0;
    irq = false;
    pending.clear();
    latch = 0;
    ym_out.clear(); pcm_out.clear();
    frames_run = 0;
    if (opm) opm->reset();
    if (opn) opn->reset();
}

void SegaSoundBoard::cpu_reset()
{
    z80_init(&cpu);
    cpu.read_byte = mem_r;
    cpu.write_byte = mem_w;
    cpu.port_in = io_r;
    cpu.port_out = io_w;
    cpu.userdata = this;
}

void SegaSoundBoard::ym_set_timer(uint32_t tnum, int32_t clocks)
{
    if (getenv("SB_TIMER_DEBUG")) fprintf(stderr, "t%u set %d at %lld (%.6f s)\n", tnum, clocks, (long long) now, now / 4e6);
    if (tnum < 2) timer_end[tnum] = clocks >= 0 ? now + clocks : -1;
}

// ------------------------------------------------------------------------------------------------
// Chips
// ------------------------------------------------------------------------------------------------
void SegaSoundBoard::ym_catch_up()
{
    while (ym_clock + ym_period <= (double) now)
    {
        if (opm)
        {
            ymfm::ym2151::output_data out;
            opm->generate(&out, 1);
            ym_out.push((float)(out.data[0] * OPM_GAIN), (float)(out.data[1] * OPM_GAIN));
        }
        else
        {
            ymfm::ym2203::output_data out;                      // [0] FM, [1..3] SSG A-C
            opn->generate(&out, 1);
            float v = (float)(out.data[0] * OPN_FM_GAIN + (out.data[1] + out.data[2] + out.data[3]) * OPN_SSG_GAIN);
            ym_out.push(v, v);
        }
        ym_clock += ym_period;
    }
}

uint8_t SegaSoundBoard::ym_read(uint32_t offset)
{
    ym_catch_up();
    return opm ? opm->read(offset & 1) : opn->read(offset & 1);
}

void SegaSoundBoard::ym_write(uint32_t offset, uint8_t v)
{
    ym_catch_up();
    if ((offset & 1) == 0) ym_addr = v;
    else if (ym_log) fprintf(ym_log, "%u %02x %02x %lld\n", frames_run, ym_addr, v, (long long) now);
    if (opm) opm->write(offset & 1, v); else opn->write(offset & 1, v);
}

// MAME 0.264 segapcm_device::sound_stream_update, one sample
void SegaSoundBoard::pcm_sample(int32_t& l, int32_t& r)
{
    l = r = 0;
    for (int ch = 0; ch < 16; ch++)
    {
        uint8_t* regs = pcm_ram + 8 * ch;
        if (regs[0x86] & 1) continue;
        int offset = (regs[0x86] & PCM_BANKMASK) << PCM_BANKSHIFT;
        uint32_t addr = (regs[0x85] << 16) | (regs[0x84] << 8) | pcm_low[ch];
        uint32_t loop = (regs[0x05] << 16) | (regs[0x04] << 8);
        uint8_t end = (uint8_t)(regs[6] + 1);
        bool stopped = false;
        if ((addr >> 16) == end)
        {
            if (regs[0x86] & 2) { regs[0x86] |= 1; stopped = true; }
            else addr = loop;
        }
        if (!stopped)
        {
            int8_t v = (int8_t)(pcm_rom[(offset + (addr >> 8)) & pcm_mask] - 0x80);
            l += v * (regs[2] & 0x7F);
            r += v * (regs[3] & 0x7F);
            addr = (addr + regs[7]) & 0xFFFFFF;
        }
        regs[0x84] = (uint8_t)(addr >> 8);
        regs[0x85] = (uint8_t)(addr >> 16);
        pcm_low[ch] = (regs[0x86] & 1) ? 0 : (uint8_t) addr;
    }
}

void SegaSoundBoard::pcm_catch_up()
{
    while (pcm_clock + pcm_div <= now)
    {
        int32_t l, r;
        pcm_sample(l, r);
        pcm_out.push((float)(l * PCM_GAIN), (float)(r * PCM_GAIN));
        pcm_clock += pcm_div;
    }
}

// ------------------------------------------------------------------------------------------------
// Bus
// ------------------------------------------------------------------------------------------------
// Which chip a Z80 address selects (MAME map; unmapped reads return $FF)
enum { M_ROM, M_RAM, M_PCM, M_YM, M_NONE };
static inline int decode(SegaSoundBoard::Kind k, uint16_t a)
{
    if (k == SegaSoundBoard::Kind::OUTRUN)
        return a < 0xF000 ? M_ROM : a < 0xF800 ? M_PCM : M_RAM;
    if (a < 0x8000) return M_ROM;
    if (a >= 0xC000 && a < 0xD000) return M_RAM;
    if (a >= 0xD000 && a < 0xE000) return M_YM;
    if (a >= 0xE000 && a < 0xF000) return M_PCM;
    return M_NONE;
}

uint8_t SegaSoundBoard::mem_r(void* ud, uint16_t a)
{
    SegaSoundBoard* b = (SegaSoundBoard*) ud;
    switch (decode(b->kind, a))
    {
        case M_ROM: return b->rom[a];
        case M_RAM: return b->ram[a & 0x7FF];
        case M_YM:  return b->ym_read(a);
        case M_PCM:
            b->pcm_catch_up();
            if (b->pcm_log) fprintf(b->pcm_log, "r%02x %02x %lld\n", a & 0xFF, b->pcm_ram[a & 0xFF], (long long) b->now);
            return b->pcm_ram[a & 0xFF];
    }
    return 0xFF;
}

void SegaSoundBoard::mem_w(void* ud, uint16_t a, uint8_t v)
{
    SegaSoundBoard* b = (SegaSoundBoard*) ud;
    switch (decode(b->kind, a))
    {
        case M_RAM: b->ram[a & 0x7FF] = v; break;
        case M_YM:  b->ym_write(a, v); break;
        case M_PCM:
            b->pcm_catch_up();
            b->pcm_ram[a & 0xFF] = v;
            if (b->pcm_log) fprintf(b->pcm_log, "%02x %02x %lld\n", a & 0xFF, v, (long long) b->now);
            break;
    }
}

uint8_t SegaSoundBoard::io_r(z80* z, uint8_t port)
{
    SegaSoundBoard* b = (SegaSoundBoard*) z->userdata;
    if ((port & 0xC0) == 0x00 && b->kind == Kind::OUTRUN) return b->ym_read(port);
    if ((port & 0xC0) == 0x40) return b->latch;            // OutRun: 315-5195 pread; Hang-On: 8255 latch
    return 0xFF;                                           // map.unmap_value_high()
}

void SegaSoundBoard::io_w(z80* z, uint8_t port, uint8_t v)
{
    SegaSoundBoard* b = (SegaSoundBoard*) z->userdata;
    if ((port & 0xC0) == 0x00 && b->kind == Kind::OUTRUN) b->ym_write(port, v);
}

// ------------------------------------------------------------------------------------------------
// Time
// ------------------------------------------------------------------------------------------------
void SegaSoundBoard::run(int64_t cycles)
{
    int64_t stop = now + cycles;
    while (now < stop)
    {
        if (cpu_held) now += 4;
        else
        {
            cpu.int_pending = irq;                          // level-triggered INT from the YM2151
            cpu.int_data = 0xFF;
            unsigned long before = cpu.cyc;
            z80_step(&cpu);
            now += (int64_t)(cpu.cyc - before);
        }
        for (uint32_t t = 0; t < 2; t++)
            if (timer_end[t] >= 0 && now >= timer_end[t])
            {
                int64_t due = timer_end[t];
                timer_end[t] = -1;
                int64_t keep = now;
                now = due;                                  // the engine re-arms relative to expiry
                ym_catch_up();
                iface->expire(t);
                now = keep;
            }
    }
}

void SegaSoundBoard::frame(int32_t* mix, int samples)
{
    if (!ready) return;
    // The frame ends CLOCK / fps cycles after the last one ended, not after where the Z80's last
    // instruction left now: an instruction's overshoot would otherwise add up frame by frame
    double cyc = (double) CLOCK / fps + frame_frac;
    int64_t total = (int64_t) cyc;
    frame_frac = cyc - (double) total;
    frame_end += total;
    int64_t end = frame_end;

    while (!pending.empty() && now < end)
    {
        latch = pending.front();
        pending.pop_front();
        z80_gen_nmi(&cpu);
        run(std::min(cmd_spacing, end - now));
    }
    if (reset_after_commands) { reset_after_commands = false; cpu_held = false; cpu_reset(); }
    if (now < end) run(end - now);
    drain(mix, samples);
    frames_run++;
}

void SegaSoundBoard::drain(int32_t* mix, int samples)
{
    ym_catch_up();
    pcm_catch_up();
    // native -> output rate; ymfm and MAME's PCM both produce 16-bit-scale values
    ym_out.pull((double) CLOCK / ym_period / out_rate, mix, samples);
    pcm_out.pull((double) CLOCK / pcm_div / out_rate, mix, samples);
}
