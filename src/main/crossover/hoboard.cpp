/***************************************************************************
    Emulated Sega Hang-On board. See hoboard.hpp.

    Transcribed from MAME 0.264 (BSD-3-Clause, Aaron Giles and the MAME team): segahang.cpp
    (address maps, PPI callbacks, inputs, timing), i8255.cpp (mode 0 and mode 2 port behaviour),
    adc0804.cpp (conversion timing).
***************************************************************************/
#include "crossover/hoboard.hpp"
#include "crossover/romset.hpp"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>

extern "C" {
#include "crossover/m68k/m68k.h"
}

HOBoard hoboard;

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool HOBoard::init(const RomSet& roms)
{
    ready = false;
    if (!roms.loaded()) return false;
    try
    {
        main_rom = roms["maincpu"].data;
        sub_rom  = roms["subcpu"].data;
    }
    catch (...) { return false; }
    if (main_rom.size() < 0x40000 || sub_rom.size() < 0x40000) return false;
    m68k_init();
    for (int i = 0; i < 2; i++) ctx[i].assign(m68k_context_size(), 0);
    ready = true;
    reset();
    return true;
}

void HOBoard::reset()
{
    if (!ready) return;
    xo_bus = this;
    std::memset(workram, 0, sizeof workram);
    std::memset(subram, 0, sizeof subram);
    std::memset(roadram, 0, sizeof roadram);
    std::memset(tileram, 0, sizeof tileram);
    std::memset(textram, 0, sizeof textram);
    std::memset(palram, 0, sizeof palram);
    std::memset(sprram, 0, sizeof sprram);
    adc_select = 0; adc_result = 0; adc_done = 0; adc_busy = false;
    // i8255 device_reset: set_mode(0x9b), all ports inputs; the tristate callbacks give 0
    ppi_set_mode(0, 0x9b);
    ppi_set_mode(1, 0x9b);

    for (int i = 0; i < 2; i++)
    {
        cur = i;
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_pulse_reset();
        m68k_get_context(ctx[i].data());
        cpu_time[i] = 0;
    }
    loaded = -1;
    irq4_main = false;
    frame_count = 0;
    first_frame = true;
    frame_len = 0;
}

// ------------------------------------------------------------------------------------------------
// 8255 PPIs (only what this board uses: mode 0 ports, and port A of the first in mode 2)
// ------------------------------------------------------------------------------------------------
void HOBoard::ppi_set_mode(int w, uint8_t data)
{
    ppi_ctl[w] = data;
    for (int p = 0; p < 3; p++) ppi_out[w][p] = 0;
    if (w == 0) obf_a = true;
    for (int p = 0; p < 3; p++) ppi_outputs(w, p);
}

// what the port's pins show, to the callbacks (MAME out_pX_cb; inputs float to 0 here)
void HOBoard::ppi_outputs(int w, int port)
{
    const uint8_t c = ppi_ctl[w];
    const int a_mode = (c >> 5) & 3;        // 0, 1, 2 (3 = 2)
    uint8_t v = 0;
    switch (port)
    {
        case 0:
            v = (a_mode == 0 && (c & 0x10)) ? 0 : ppi_out[w][0];
            if (w == 1)
            {
                // segahang sub_control_adc_w
                // MAME changes the second CPU's input lines through set_input_line, which
                // synchronises: the main CPU's slice ends here and the second CPU runs up to this
                // moment with the old line states (see run_until)
                adc_select = (v >> 2) & 3;
                bool irq = !(v & 0x40), rst = (v & 0x20) != 0;
                if (cur == 0 && loaded == 0 && in_execute)
                {
                    lines_pending = true; pend_irq = irq; pend_reset = rst;
                    sync_now = true; m68k_end_timeslice();
                }
                else { sub_irq = irq; sub_in_reset = rst; apply_irqs(); }
            }
            break;
        case 1:
            v = (c & 0x02) ? 0 : ppi_out[w][1];
            if (w == 0)
            {
                // segahang video_lamps_w
                flip = (v & 0x80) != 0;
                shadow = !(v & 0x40);
                if (sound_reset) sound_reset(!(v & 0x20));
                display_enable = (v & 0x10) != 0;
            }
            break;
        case 2:
        {
            uint8_t data = 0, mask = 0, bmask = 0x0f;
            if (a_mode == 0) { if (!(c & 0x08)) mask |= 0xf0; }
            else if (a_mode >= 2) { bmask = 0x07; data |= obf_a ? 0x80 : 0x00; }
            if (!(c & 0x01)) mask |= bmask;
            v = (uint8_t)(data | (ppi_out[w][2] & mask));
            if (w == 0)
            {
                // segahang tilemap_sound_w (bit 7: the Z80's NMI, bit 0: mute)
                colscroll = !(v & 0x04);
                rowscroll = !(v & 0x02);
            }
            break;
        }
    }
}

void HOBoard::ppi_write(int w, int port, uint8_t data)
{
    if (w == 0 && cur == 0 && sync_ppi) { sync_now = true; m68k_end_timeslice(); }
    if (port == 3)
    {
        if (data & 0x80) { ppi_set_mode(w, data); return; }
        // bit set/reset on port C
        int bit = (data >> 1) & 7;
        ppi_out[w][2] = (uint8_t)((ppi_out[w][2] & ~(1 << bit)) | ((data & 1) << bit));
        if (w == 0 && ((ppi_ctl[0] >> 5) & 3) >= 2 && bit == 7) obf_a = data & 1;
        ppi_outputs(w, 2);
        return;
    }
    const int a_mode = (ppi_ctl[w] >> 5) & 3;
    if (port == 0)
    {
        if (a_mode >= 2)
        {
            ppi_out[w][0] = data;
            if (w == 0 && sound_command) sound_command(data);
            if (w == 0) obf_a = false;      // the Z80 reads it later (its NMI); see ppi_read
            ppi_outputs(w, 2);
        }
        else if (!(ppi_ctl[w] & 0x10))
        {
            ppi_out[w][0] = data;
            if (w == 0 && sound_command) sound_command(data);
            ppi_outputs(w, 0);
        }
        return;
    }
    if (port == 1) { if (!(ppi_ctl[w] & 0x02)) { ppi_out[w][1] = data; ppi_outputs(w, 1); } return; }
    // port C
    ppi_out[w][2] = data;
    ppi_outputs(w, 2);
}

uint8_t HOBoard::ppi_read(int w, int port)
{
    const uint8_t c = ppi_ctl[w];
    const int a_mode = (c >> 5) & 3;
    switch (port)
    {
        case 0: return (a_mode == 0 && (c & 0x10)) ? 0 : ppi_out[w][0];       // inputs: nothing connected
        case 1: return (c & 0x02) ? 0 : ppi_out[w][1];
        case 2:
        {
            uint8_t data = 0, bmask = 0x0f;
            if (a_mode == 0) { if (!(c & 0x08)) data |= ppi_out[w][2] & 0xf0; }
            else if (a_mode >= 2)
            {
                // mode 2: /OBF stays low until the Z80 acknowledges, which this board does not model
                // (the game only counts the misses, at $20C460, which nothing reads). In MAME the
                // Z80 misses nearly every one of the game's checks too.
                bmask = 0x07;
                data |= obf_a ? 0x80 : 0x00;
            }
            if (!(c & 0x01)) data |= ppi_out[w][2] & bmask;
            return data;
        }
        default: return 0xff;
    }
}

// ------------------------------------------------------------------------------------------------
// ADC0804: a write starts a conversion (ignored while one runs) that ends 74 of its clocks later
// with the selected channel sampled; reads give the last result
// ------------------------------------------------------------------------------------------------
double HOBoard::cpu_now() const { return cpu_time[cur] + m68k_cycles_run(); }

void HOBoard::adc_update(double t)
{
    if (adc_busy && t >= adc_done) { adc_result = adc_select < 3 ? adc[adc_select] : 0; adc_busy = false; }
}

// ------------------------------------------------------------------------------------------------
// Buses
// ------------------------------------------------------------------------------------------------
uint16_t HOBoard::io_read(uint32_t a)
{
    const int sel = (a >> 12) & 3, off = (a >> 1) & 3;
    switch (sel)
    {
        case 0: return (uint16_t)(0xff00 | ppi_read(0, off));
        case 1:
        {
            static const int order[4] = { 0, 1, 2, 3 };    // SERVICE, COINAGE, DSW, UNKNOWN
            uint8_t v = 0xff;
            switch (order[off]) { case 0: v = port_service; break; case 1: v = port_coinage; break;
                                  case 2: v = port_dsw; break; default: v = port_unknown; break; }
            return (uint16_t)(0xff00 | v);
        }
        case 3:
            if (a & 0x20)
            {
                adc_update(cpu_now());
                if (getenv("HOB_ADCLOG")) fprintf(stderr, "f%llu adc read t=%.0f sel=%d -> %02x (done %.0f)\n", (unsigned long long) frame_count, cpu_now(), adc_select, adc_result, adc_done);
                return (uint16_t)(0xff00 | adc_result);
            }
            return (uint16_t)(0xff00 | ppi_read(1, off));
        default: return 0xffff;
    }
}

void HOBoard::io_write(uint32_t a, uint16_t v, uint16_t mask)
{
    if (!(mask & 0x00ff)) return;
    const int sel = (a >> 12) & 3, off = (a >> 1) & 3;
    switch (sel)
    {
        case 0: ppi_write(0, off, (uint8_t) v); break;
        case 3:
            if (a & 0x20)
            {
                double t = cpu_now();
                adc_update(t);
                if (!adc_busy) { adc_done = t + ADC_CYCLES; adc_busy = true; }
                if (getenv("HOB_ADCLOG")) fprintf(stderr, "f%llu adc start t=%.0f sel=%d done %.0f\n", (unsigned long long) frame_count, t, adc_select, adc_done);
            }
            else ppi_write(1, off, (uint8_t) v);
            break;
        default: break;
    }
}

uint16_t HOBoard::main_read16(uint32_t a, int space)
{
    a &= 0xfffffe;
    if (a < 0x040000) return be16(&main_rom[a]);
    if (a >= 0x20c000 && a < 0x210000) return be16(&workram[a - 0x20c000]);
    if (a >= 0x400000 && a < 0x404000) return be16(&tileram[a - 0x400000]);
    if (a >= 0x410000 && a < 0x411000) return be16(&textram[a - 0x410000]);
    if (a >= 0x600000 && a < 0x600800) return be16(&sprram[a - 0x600000]);
    if (a >= 0xa00000 && a < 0xa01000) return be16(&palram[a - 0xa00000]);
    if (a >= 0xc00000 && a < 0xc40000) return be16(&sub_rom[a - 0xc00000]);
    if (a >= 0xc68000 && a < 0xc69000) return be16(&roadram[a - 0xc68000]);
    if (a >= 0xc7c000 && a < 0xc80000) return be16(&subram[a - 0xc7c000]);
    if (a >= 0xe00000) return space == 2 ? 0xffff : io_read(a);
    return 0xffff;
}

void HOBoard::main_write16(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xfffffe;
    uint8_t* p = nullptr;
    if (a >= 0x20c000 && a < 0x210000) p = &workram[a - 0x20c000];
    else if (a >= 0x400000 && a < 0x404000) p = &tileram[a - 0x400000];
    else if (a >= 0x410000 && a < 0x411000) p = &textram[a - 0x410000];
    else if (a >= 0x600000 && a < 0x600800) p = &sprram[a - 0x600000];
    else if (a >= 0xa00000 && a < 0xa01000) p = &palram[a - 0xa00000];
    else if (a >= 0xc68000 && a < 0xc69000) p = &roadram[a - 0xc68000];
    else if (a >= 0xc7c000 && a < 0xc80000) p = &subram[a - 0xc7c000];
    else if (a >= 0xe00000) { io_write(a, v, mask); return; }
    else return;
    static const char* wl = getenv("HOB_WATCH");          // "lo-hi" hex: log main-CPU writes there
    static uint32_t wlo = wl ? strtoul(wl, nullptr, 16) : 1, whi = wl && strchr(wl, '-') ? strtoul(strchr(wl, '-') + 1, nullptr, 16) : 0;
    if (a >= wlo && a <= whi) fprintf(stderr, "w f%llu %06x %04x %04x pc %06x t %.0f\n", (unsigned long long) frame_count + 1, a, v, mask, main_pc(), cpu_now());
    if (mask & 0xff00) p[0] = v >> 8;
    if (mask & 0x00ff) p[1] = (uint8_t) v;
}

uint16_t HOBoard::sub_read16(uint32_t a)
{
    a &= 0x7fffe;
    if (a < 0x40000) return be16(&sub_rom[a]);
    if (a >= 0x68000 && a < 0x69000) return be16(&roadram[a - 0x68000]);
    if (a >= 0x7c000) return be16(&subram[a - 0x7c000]);
    return 0xffff;
}

void HOBoard::sub_write16(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0x7fffe;
    uint8_t* p = nullptr;
    if (a >= 0x68000 && a < 0x69000) p = &roadram[a - 0x68000];
    else if (a >= 0x7c000) p = &subram[a - 0x7c000];
    else return;
    if (mask & 0xff00) p[0] = v >> 8;
    if (mask & 0x00ff) p[1] = (uint8_t) v;
}

uint32_t HOBoard::read(int cpu, uint32_t a, int size, int space)
{
    a &= 0xffffff;
    auto r16 = [&](uint32_t x) -> uint16_t { return cpu == 0 ? main_read16(x, space) : sub_read16(x); };
    switch (size)
    {
        case 1: { uint16_t w = r16(a & ~1u); return (a & 1) ? (w & 0xff) : (w >> 8); }
        case 2: return r16(a);
        default: return ((uint32_t) r16(a) << 16) | r16((a + 2) & 0xffffff);
    }
}

void HOBoard::write(int cpu, uint32_t a, int size, uint32_t v)
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
int HOBoard::int_ack(int level)
{
    if (cur == 0 && level == 4 && irq4_main) { irq4_main = false; apply_irqs(); }   // HOLD_LINE
    return (int) M68K_INT_ACK_AUTOVECTOR;
}

void HOBoard::apply_irqs()
{
    if (loaded == 0) m68k_set_irq(irq4_main ? 4 : 0);
    if (loaded == 1) m68k_set_irq(sub_irq ? 4 : 0);
}

void HOBoard::select(int cpu)
{
    if (loaded == cpu) return;
    if (loaded >= 0) m68k_get_context(ctx[loaded].data());
    m68k_set_context(ctx[cpu].data());
    loaded = cpu;
    cur = cpu;
    m68k_set_irq(cpu == 0 ? (irq4_main ? 4 : 0) : (sub_irq ? 4 : 0));
}

void HOBoard::instr_hook(uint32_t pc)
{
    if (trace) fprintf(trace, "%c %06x\n", cur ? 's' : 'm', pc);
    static const char* idle = getenv("HOB_IDLELOG");
    if (idle && cur == 0 && pc == 0x17e0 && !idle_seen) { idle_seen = true; fprintf(stderr, "idle f%llu t=%.0f\n", (unsigned long long) frame_count, cpu_now()); }
    if (cur == 0)
        for (int i = 0; i < n_hooks; i++)
            if (pc == hook_pc[i]) { hook_fn(hook_ctx, pc); break; }
}

void HOBoard::set_pc_hooks(const uint32_t* pcs, int n, PcHook fn, void* c)
{
    n_hooks = 0;
    if (!fn || n <= 0) return;
    for (int i = 0; i < n && i < MAX_HOOKS; i++) hook_pc[i] = pcs[i] & 0xffffff;
    hook_fn = fn; hook_ctx = c;
    n_hooks = std::min(n, MAX_HOOKS);
}

uint32_t HOBoard::main_reg(int r) const
{
    if (loaded == 0) return m68k_get_reg(nullptr, (m68k_register_t) r);
    return m68k_get_reg((void*) ctx[0].data(), (m68k_register_t) r);
}

void HOBoard::set_main_reg(int r, uint32_t v)
{
    if (loaded == 0) { m68k_set_reg((m68k_register_t) r, v); return; }
    void* saved = std::malloc(m68k_context_size());
    m68k_get_context(saved);
    m68k_set_context(ctx[0].data());
    m68k_set_reg((m68k_register_t) r, v);
    m68k_get_context(ctx[0].data());
    m68k_set_context(saved);
    std::free(saved);
}

uint32_t HOBoard::main_pc() const
{
    if (loaded == 0) return m68k_get_reg(nullptr, M68K_REG_PC);
    return m68k_get_reg((void*) ctx[0].data(), M68K_REG_PC);
}

uint32_t HOBoard::sub_pc() const
{
    if (loaded == 1) return m68k_get_reg(nullptr, M68K_REG_PC);
    return m68k_get_reg((void*) ctx[1].data(), M68K_REG_PC);
}

void HOBoard::run_until(double t)
{
    // MAME's scheduler runs each CPU up to the end of a time slice: the next timer event or one
    // quantum. Events here: the ADC's conversion end, and the synchronisation every write to the
    // first PPI asks for (segahang sync_ppi_w), which ends the main CPU's slice at the write.
    while (now < t)
    {
        double tq = std::min(t, now + QUANTUM);
        if (adc_busy && adc_done > now) tq = std::min(tq, std::ceil(adc_done));
        for (int c = 0; c < 2; c++)
        {
            if (cpu_time[c] >= tq) continue;
            if (c == 1 && sub_in_reset)
            {
                // held in reset: it restarts from its vectors when released
                cpu_time[1] = tq;
                select(1);
                m68k_pulse_reset();
                continue;
            }
            select(c);
            int n = (int) std::ceil(tq - cpu_time[c]);
            sync_now = false;
            in_execute = true;
            cpu_time[c] += m68k_execute(n);
            in_execute = false;
            if (c == 0 && sync_now && cpu_time[0] < tq) tq = cpu_time[0];
        }
        now = tq;
        if (lines_pending)
        {
            lines_pending = false;
            sub_irq = pend_irq; sub_in_reset = pend_reset;
            apply_irqs();
        }
    }
}

void HOBoard::run_frame()
{
    if (!ready) return;
    xo_bus = this;
    loaded = -1;
    const double L = LINE_CYCLES;
    // From the start of line 224 (VBLANK, when the screen is drawn) to the next one; the first
    // frame runs from power-on at line 0.
    double end = first_frame ? 224 * L : 262 * L;
    for (int c = 0; c < 2; c++) cpu_time[c] -= frame_len;
    if (adc_busy) adc_done -= frame_len;
    now = 0;
    frame_len = end;
    if (!first_frame) { irq4_main = true; }              // VBLANK: irq4_line_hold
    idle_seen = false;
    run_until(end);
    if (loaded >= 0) m68k_get_context(ctx[loaded].data());
    loaded = -1;
    first_frame = false;
    frame_count++;
}
