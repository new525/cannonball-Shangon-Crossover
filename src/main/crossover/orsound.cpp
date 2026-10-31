/***************************************************************************
    The sound board of Sega's System Multi 32 (OutRunners). See orsound.hpp.
***************************************************************************/
#include "crossover/orsound.hpp"
#include "crossover/ymfm/ymfm_opn.h"

#include <cstring>
#include <algorithm>

static const uint32_t PCM_CLOCK = 10000000;     // MULTI32_CLOCK / 4
enum { SOUND_IRQ_YM3438 = 0, SOUND_IRQ_V60 = 1 };

struct ORSound::Iface : public ymfm::ymfm_interface
{
    ORSound* b;
    explicit Iface(ORSound* board) : b(board) {}
    // ymfm_device_base: timers at the chip's clock (the Z80's) from now
    void ymfm_set_timer(uint32_t tnum, int32_t clocks) override
    {
        if (tnum < 2) b->timer_end[tnum] = clocks >= 0 ? b->time() + clocks : -1;
    }
    void ymfm_set_busy_end(uint32_t clocks) override { b->busy_end = b->time() + clocks; }
    bool ymfm_is_busy() override { return b->time() < b->busy_end; }
    void ymfm_update_irq(bool asserted) override             // segas32 ym3438_irq_handler
    {
        b->ym_irq = asserted;
        if (asserted) b->signal_irq(SOUND_IRQ_YM3438); else b->clear_irq(SOUND_IRQ_YM3438);
    }
    // MAME's ym_generic_device: a mode-register write (timer loads, flag resets) and, while a CPU
    // is executing, an interrupt check go through scheduler().synchronize(): they happen at the
    // write's time, but after any timer that expired earlier in the instruction that made them
    void ymfm_sync_mode_write(uint8_t data) override
    {
        if (b->in_step) b->sync.push_back({ b->time(), 0, data });
        else m_engine->engine_mode_write(data);
    }
    void ymfm_sync_check_interrupts() override
    {
        if (b->in_step) b->sync.push_back({ b->time(), 1, 0 });
        else m_engine->engine_check_interrupts();
    }
    void expire(uint32_t tnum) { m_engine->engine_timer_expired(tnum); }
    void mode_write(uint8_t d) { m_engine->engine_mode_write(d); }
    void check_interrupts() { m_engine->engine_check_interrupts(); }
};

ORSound::ORSound() {}
ORSound::~ORSound() {}

bool ORSound::init(const std::vector<uint8_t>& soundcpu, const std::vector<uint8_t>& pcmrom)
{
    ready = false;
    if (soundcpu.size() < 0x80000 || pcmrom.size() < 0x400000) return false;
    rom = soundcpu; pcm_rom = pcmrom;
    iface.reset(new Iface(this));
    ym.reset(new ymfm::ym3438(*iface));
    ym_rate_ = ym->sample_rate((uint32_t) CLOCK);    // MAME's stream: the integer rate
    pcm.read_byte = [this](uint32_t a) -> uint8_t
    {
        // segas32 multipcm_map: 1 MB direct, then the two 512 KB bank windows
        if (a < 0x100000) return pcm_rom[a];
        if (a < 0x180000) return pcm_rom[(pcm_bank_lo * 0x80000 + (a - 0x100000)) % pcm_rom.size()];
        if (a < 0x200000) return pcm_rom[(pcm_bank_hi * 0x80000 + (a - 0x180000)) % pcm_rom.size()];
        return 0;
    };
    ready = true;
    power_on();
    return true;
}

void ORSound::power_on()
{
    std::memset(shared, 0, sizeof shared);
    z80_init(&cpu);
    cpu.read_byte = mem_r; cpu.write_byte = mem_w; cpu.port_in = io_r; cpu.port_out = io_w; cpu.userdata = this;
    cpu_held = true;
    cyc = 0;
    timer_end[0] = timer_end[1] = -1;
    busy_end = 0;
    ym_irq = false;
    ym->reset();
    pcm.init(PCM_CLOCK);
    pcm_bank_lo = pcm_bank_hi = 0;
    sound_bank = 0;
    irq_input = 0;
    std::memset(irq_control, 0, sizeof irq_control);
    dummy = 0;
    ym_gen = pcm_gen = 0;
    ym0.clear(); ym1.clear(); pcm0.clear(); pcm1.clear();
    mainw.clear(); mainw_pos = 0;
    visible_until = 0;
    sync.clear();
}

void ORSound::release_cpu()
{
    if (!cpu_held) return;
    z80_init(&cpu);
    cpu.read_byte = mem_r; cpu.write_byte = mem_w; cpu.port_in = io_r; cpu.port_out = io_w; cpu.userdata = this;
    cpu_held = false;
    released_at = cyc;
}

void ORSound::main_write(int64_t t, uint32_t offset, uint8_t data)
{
    mainw.push_back({ t, offset & 0x1fff, data });
}

// ------------------------------------------------------------------------------------------------
// Streams: MAME generates a stream up to the sample index of the current time, rounded up
// ------------------------------------------------------------------------------------------------
// The current time. MAME 0.264's Z80 charges each machine cycle after its access (rop: 4,
// arg: 3, a memory access: 3, an I/O access: 4), so an access happens after the cycles of the
// accesses before it. The sound program reaches its chips with two instructions only:
// OUT (n),A (the port write after 4 + 3 cycles) and LD (nn),A (the memory write after
// 4 + 3 + 3). Other accesses are taken at the end of the instruction.
int64_t ORSound::time() const
{
    if (!in_step) return cyc;
    uint8_t op = rom_byte(step_pc);
    if (op == 0xd3) return cyc + 7;
    if (op == 0x32) return cyc + 10;
    return cyc + (int64_t)(cpu.cyc - step_base);
}

uint8_t ORSound::rom_byte(uint16_t a) const
{
    if (a < 0xa000) return rom[a];
    if (a < 0xc000) return rom[((uint32_t) sound_bank * 0x2000 + (a - 0xa000)) % rom.size()];
    if (a >= 0xe000) return shared[a - 0xe000];
    return 0;
}

int64_t ORSound::samples_at(int64_t cycles, uint32_t rate)
{
    return (cycles * (int64_t) rate + CLOCK - 1) / CLOCK;
}

void ORSound::ym_catch_up()
{
    int64_t want = samples_at(time(), ym_rate_);
    while ((int64_t) ym_gen < want)
    {
        ymfm::ym3438::output_data out;
        ym->generate(&out, 1);
        ym0.push_back((float) out.data[0] / 32768.0f);       // put_int(i, v, 32768)
        ym1.push_back((float) out.data[1] / 32768.0f);
        ym_gen++;
    }
}

void ORSound::pcm_catch_up()
{
    int64_t want = samples_at(time(), pcm.sample_rate());
    if ((int64_t) pcm_gen >= want) return;
    int n = (int)(want - (int64_t) pcm_gen);
    size_t at = pcm0.size();
    pcm0.resize(at + (size_t) n); pcm1.resize(at + (size_t) n);
    pcm.generate(n, pcm0.data() + at, pcm1.data() + at);
    pcm_gen = (uint64_t) want;
}

// ------------------------------------------------------------------------------------------------
// Interrupt controller (segas32 update_sound_irq_state, signal_sound_irq, clear_sound_irq)
// ------------------------------------------------------------------------------------------------
void ORSound::update_irq()
{
    uint8_t effirq = irq_input & ~irq_control[3] & 0x07;
    int vector;
    for (vector = 0; vector < 3; vector++)
        if (effirq & (1 << vector)) break;
    cpu.int_pending = vector < 3;
    cpu.int_data = (uint8_t)(vector < 3 ? 2 * vector : 0);
}

void ORSound::signal_irq(int which)
{
    for (int i = 0; i < 3; i++) if (irq_control[i] == which) irq_input |= (uint8_t)(1 << i);
    update_irq();
}

void ORSound::clear_irq(int which)
{
    for (int i = 0; i < 3; i++) if (irq_control[i] == which) irq_input &= (uint8_t) ~(1 << i);
    update_irq();
}

// ------------------------------------------------------------------------------------------------
// Run
// ------------------------------------------------------------------------------------------------
// The events due by now, in time order as MAME's timer list has them (an event at the same time
// as an earlier-armed one comes after it): the YM3438's timers and the synchronised writes
void ORSound::timers()
{
    bool fired = false;
    for (;;)
    {
        int64_t best = -1; int which = -1;               // 0, 1: timers; 2: the first sync event
        for (int t = 0; t < 2; t++)
            if (timer_end[t] >= 0 && timer_end[t] <= cyc && (best < 0 || timer_end[t] < best)) { best = timer_end[t]; which = t; }
        if (!sync.empty() && (best < 0 || sync.front().t < best)) { best = sync.front().t; which = 2; }
        if (which < 0) break;
        int64_t keep = cyc;
        cyc = best;
        if (which < 2)
        {
            timer_end[which] = -1;                      // the engine re-arms from the expiry
            ym_catch_up();
            iface->expire((uint32_t) which);
        }
        else
        {
            SyncEvent e = sync.front();
            sync.erase(sync.begin());
            ym_catch_up();
            if (e.kind == 0) iface->mode_write(e.data); else iface->check_interrupts();
        }
        cyc = keep;
        visible_until = 0;                              // a new slice starts at the event
        fired = true;
    }
    // the IRQ line changes between instructions in MAME (a synchronised timer): the Z80 takes the
    // interrupt before its next instruction
    if (fired && !cpu_held)
    {
        unsigned long before = cpu.cyc;
        z80_take_interrupt(&cpu);
        cyc += (int64_t)(cpu.cyc - before);             // the acknowledge's cycles
    }
}

// MAME's scheduler runs the main V60 first in each timeslice, up to the slice's end (the next
// timer, or the 60 Hz frame), then the Z80: the Z80 sees every write the V60 makes in a slice from
// the slice's start. The slices here end at the YM3438's timers and at each 1/60 s.
int64_t ORSound::slice_end() const
{
    int64_t e = ((cyc * 60) / CLOCK + 1) * CLOCK / 60;
    while (e <= cyc) e = ((e * 60) / CLOCK + 1) * CLOCK / 60 + 1;
    for (int k = 0; k < 2; k++) if (timer_end[k] > cyc && timer_end[k] < e) e = timer_end[k];
    return e;
}

void ORSound::run_until(int64_t t)
{
    if (!ready) return;
    while (cyc < t)
    {
        if (cyc >= visible_until) visible_until = slice_end();
        while (mainw_pos < mainw.size() && mainw[mainw_pos].t < visible_until)
        {
            shared[mainw[mainw_pos].off] = mainw[mainw_pos].data;
            mainw_pos++;
        }
        if (mainw_pos > 4096) { mainw.erase(mainw.begin(), mainw.begin() + (long) mainw_pos); mainw_pos = 0; }
        if (cpu_held) cyc += 4;
        else
        {
            if (trace && cyc >= trace_from && cyc < trace_to)
                fprintf(trace, "%llX %04X\n", (unsigned long long)(cyc - released_at), cpu.pc);
            step_base = cpu.cyc; in_step = true; step_pc = cpu.pc;
            z80_step(&cpu);
            in_step = false;
            cyc += (int64_t)(cpu.cyc - step_base);
        }
        timers();
    }
    ym_catch_up();
    pcm_catch_up();
}

// ------------------------------------------------------------------------------------------------
// Z80 memory and ports (segas32 multi32_sound_map, multi32_sound_portmap)
// ------------------------------------------------------------------------------------------------
uint8_t ORSound::mem_r(void* ud, uint16_t a)
{
    ORSound* b = (ORSound*) ud;
    if (a < 0xa000) return b->rom[a];
    if (a < 0xc000) return b->rom[((uint32_t) b->sound_bank * 0x2000 + (a - 0xa000)) % b->rom.size()];
    if (a < 0xe000) return b->pcm.read();
    return b->shared[a - 0xe000];
}

void ORSound::mem_w(void* ud, uint16_t a, uint8_t v)
{
    ORSound* b = (ORSound*) ud;
    if (a >= 0xe000) { b->shared[a - 0xe000] = v; return; }
    if (a >= 0xc000)
    {
        if (b->chip_log) fprintf(b->chip_log, "%lld pcm %04x %02x\n", (long long) b->time(), a, v);
        if (b->op_log) fprintf(b->op_log, "pcmw %02x %02x %02x\n", mem_r(ud, b->step_pc), mem_r(ud, (uint16_t)(b->step_pc + 1)), mem_r(ud, (uint16_t)(b->step_pc + 2)));
        b->pcm_catch_up();
        b->pcm.write((uint32_t)(a - 0xc000), v);
    }
}

uint8_t ORSound::io_r(z80* z, uint8_t port)
{
    ORSound* b = (ORSound*) z->userdata;
    if (b->op_log) fprintf(b->op_log, "ior %02x %02x\n", mem_r(b, b->step_pc), mem_r(b, (uint16_t)(b->step_pc + 1)));
    if ((port & 0xf0) == 0x80) { b->ym_catch_up(); return b->ym->read(port & 3); }
    if (port == 0xf1) return b->dummy;
    return 0xff;                                        // unmap_value_high
}

void ORSound::io_w(z80* z, uint8_t port, uint8_t v)
{
    ORSound* b = (ORSound*) z->userdata;
    if (b->chip_log) fprintf(b->chip_log, "%lld io %02x %02x\n", (long long) b->time(), port, v);
    if (b->op_log) fprintf(b->op_log, "iow %02x %02x\n", mem_r(b, b->step_pc), mem_r(b, (uint16_t)(b->step_pc + 1)));
    switch (port & 0xf0)
    {
        case 0x80: b->ym_catch_up(); b->ym->write(port & 3, v); break;
        case 0xa0: b->sound_bank = (uint8_t)((b->sound_bank & ~0x3f) | (v & 0x3f)); break;
        case 0xb0:                                      // multipcm_bank_w (rom_bank_pre_change: the stream first)
            b->pcm_catch_up();
            b->pcm_bank_hi = (v >> 3) & 7; b->pcm_bank_lo = v & 7;
            break;
        case 0xc0:                                      // sound_int_control_lo_w
            if (port & 1) { b->irq_input &= v; b->update_irq(); }
            break;                                      // (port & 4: an IRQ to the V60, not emulated)
        case 0xd0:
            if ((port & 0x0f) < 8) { b->irq_control[port & 3] = v; b->update_irq(); }
            break;
        case 0xf0:
            if (port == 0xf1) b->dummy = v;
            break;
    }
}
