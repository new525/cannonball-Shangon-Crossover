/***************************************************************************
    Virtual Sega sound board. See soundboard.hpp.
***************************************************************************/
#include "crossover/soundboard.hpp"

#include <cstring>
#include <algorithm>
#include "crossover/ymfm/ymfm_opm.h"

static const int64_t CLOCK       = 4000000;    // Z80, YM2151 and PCM (MAME segaorun.cpp: SOUND_CLOCK / 4)
static const int64_t YM_DIV      = 64;         // ymfm ym2151::sample_rate = clock / 64
static const int64_t PCM_DIV     = 128;        // MAME segapcm: clock / 128
static const int64_t CMD_SPACING = 192;       // Z80 cycles between the main CPU's 8 writes: 48 us apart
                                               // in MAME's log of Super Hang-On (sub_018E0's busy-wait)
static const int     PCM_BANKSHIFT = 12;       // BANK_512 (MAME segapcm.h set_bank)
static const int     PCM_BANKMASK  = 0x70;
static const double  YM_GAIN = 0.43, PCM_GAIN = 1.0;   // MAME segaorun.cpp routes

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

bool SegaSoundBoard::init(const std::vector<uint8_t>& z80rom, const std::vector<uint8_t>& pcm, int out_rate_, int fps_)
{
    ready = false;
    if (z80rom.empty() || pcm.empty()) return false;
    out_rate = out_rate_; fps = fps_;
    std::memset(rom, 0, sizeof rom);
    std::memcpy(rom, z80rom.data(), std::min(z80rom.size(), (size_t) 0xF000));
    pcm_rom = pcm;
    uint32_t sz = 1;
    while (sz < pcm_rom.size()) sz <<= 1;
    pcm_rom.resize(sz, 0);
    pcm_mask = sz - 1;                         // device_rom_interface: a smaller region is mirrored
    iface.reset(new Iface(this));
    ym.reset(new ymfm::ym2151(*iface));
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
    now = 0; frame_frac = 0; ym_clock = 0; pcm_clock = 0;
    timer_end[0] = timer_end[1] = -1;
    busy_end = 0;
    irq = false;
    pending.clear();
    latch = 0;
    ym_buf.clear(); pcm_buf.clear();
    ym_pos = pcm_pos = 0;
    frames_run = 0;
    ym->reset();
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
    if (tnum < 2) timer_end[tnum] = clocks >= 0 ? now + clocks : -1;
}

// ------------------------------------------------------------------------------------------------
// Chips
// ------------------------------------------------------------------------------------------------
void SegaSoundBoard::ym_catch_up()
{
    ymfm::ym2151::output_data out;
    while (ym_clock + YM_DIV <= now)
    {
        ym->generate(&out, 1);
        ym_buf.push_back(out.data[0]);
        ym_buf.push_back(out.data[1]);
        ym_clock += YM_DIV;
    }
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
    while (pcm_clock + PCM_DIV <= now)
    {
        int32_t l, r;
        pcm_sample(l, r);
        pcm_buf.push_back(l);
        pcm_buf.push_back(r);
        pcm_clock += PCM_DIV;
    }
}

// ------------------------------------------------------------------------------------------------
// Bus
// ------------------------------------------------------------------------------------------------
uint8_t SegaSoundBoard::mem_r(void* ud, uint16_t a)
{
    SegaSoundBoard* b = (SegaSoundBoard*) ud;
    if (a < 0xF000) return b->rom[a];
    if (a < 0xF800)
    {
        b->pcm_catch_up();
        if (b->pcm_log) fprintf(b->pcm_log, "r%02x %02x %lld\n", a & 0xFF, b->pcm_ram[a & 0xFF], (long long) b->now);
        return b->pcm_ram[a & 0xFF];
    }
    return b->ram[a & 0x7FF];
}

void SegaSoundBoard::mem_w(void* ud, uint16_t a, uint8_t v)
{
    SegaSoundBoard* b = (SegaSoundBoard*) ud;
    if (a < 0xF000) return;
    if (a < 0xF800)
    {
        b->pcm_catch_up();
        b->pcm_ram[a & 0xFF] = v;
        if (b->pcm_log) fprintf(b->pcm_log, "%02x %02x %lld\n", a & 0xFF, v, (long long) b->now);
    }
    else b->ram[a & 0x7FF] = v;
}

uint8_t SegaSoundBoard::io_r(z80* z, uint8_t port)
{
    SegaSoundBoard* b = (SegaSoundBoard*) z->userdata;
    if ((port & 0xC0) == 0x00) { b->ym_catch_up(); return b->ym->read(port & 1); }
    if ((port & 0xC0) == 0x40) return b->latch;            // 315-5195 pread (NMI line cleared)
    return 0xFF;                                           // map.unmap_value_high()
}

void SegaSoundBoard::io_w(z80* z, uint8_t port, uint8_t v)
{
    SegaSoundBoard* b = (SegaSoundBoard*) z->userdata;
    if ((port & 0xC0) != 0x00) return;
    b->ym_catch_up();
    if ((port & 1) == 0) b->ym_addr = v;
    else if (b->ym_log) fprintf(b->ym_log, "%u %02x %02x %lld\n", b->frames_run, b->ym_addr, v, (long long) b->now);
    b->ym->write(port & 1, v);
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

void SegaSoundBoard::resample(std::vector<int32_t>& buf, double& pos, double step, int32_t* mix, int samples, double gain)
{
    size_t n = buf.size() / 2;
    for (int i = 0; i < samples; i++)
    {
        size_t k = (size_t) pos;
        double f = pos - (double) k;
        for (int c = 0; c < 2; c++)
        {
            double a = k < n ? buf[2 * k + c] : (n ? buf[2 * (n - 1) + c] : 0);
            double b = k + 1 < n ? buf[2 * (k + 1) + c] : a;
            mix[2 * i + c] += (int32_t)((a + (b - a) * f) * gain);
        }
        pos += step;
    }
    size_t used = std::min((size_t) pos, n);
    buf.erase(buf.begin(), buf.begin() + 2 * used);
    pos -= (double) used;
}

void SegaSoundBoard::frame(int32_t* mix, int samples)
{
    if (!ready) return;
    double cyc = (double) CLOCK / fps + frame_frac;
    int64_t total = (int64_t) cyc;
    frame_frac = cyc - (double) total;
    int64_t end = now + total;

    while (!pending.empty() && now < end)
    {
        latch = pending.front();
        pending.pop_front();
        z80_gen_nmi(&cpu);
        run(std::min(CMD_SPACING, end - now));
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
    resample(ym_buf,  ym_pos,  (double)(CLOCK / YM_DIV)  / out_rate, mix, samples, YM_GAIN);
    resample(pcm_buf, pcm_pos, (double)(CLOCK / PCM_DIV) / out_rate, mix, samples, PCM_GAIN);
}
