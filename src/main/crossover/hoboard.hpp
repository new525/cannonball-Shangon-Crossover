/***************************************************************************
    Emulated Sega Hang-On board running Hang-On (1985, Rev A).

    The board as MAME 0.264 describes it (segahang.cpp: hangon, shared_base/hangon_base):
    - main 68000 at 6.2937 MHz (25.1748 MHz / 4), IRQ4 at the start of VBLANK (line 224),
      held until acknowledged;
    - second 68000 at 6.2937 MHz; its IRQ4 and RESET lines are bits 6 and 5 of the second PPI's
      port A, driven by the main CPU;
    - two 8255 PPIs: the first sends the sound commands (port A, mode 2 with handshake: the
      Z80's NMI), screen controls (port B: shadow/highlight, display enable, sound CPU reset) and
      tilemap scroll modes / sound mute (port C); the second controls the second CPU and the ADC
      channel select (port A);
    - ADC0804 for steering, accelerator and brake (74 clocks of 1.049 MHz per conversion);
    - 262 lines of 400 pixels at 6.2937 MHz (60.05 Hz), 1/6000 s maximum interleave quantum.

    CPU core: Musashi (crossover/m68k, MIT), shared with the Super Hang-On board through xo_bus.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <functional>
#include "crossover/xobus.hpp"

class RomSet;

class HOBoard : public XoBus
{
public:
    bool init(const RomSet& roms);          // HangOnRoms
    void reset();
    void run_frame();                       // from the start of line 224 (VBLANK) to the next one
    bool ready = false;

    // ---- inputs (MAME's ports for hangon; digital ports active low) ----
    uint8_t port_service = 0xFF;            // b0 coin 1, b1 coin 2, b2 test, b3 service, b4 start
    uint8_t port_coinage = 0xFF;
    uint8_t port_dsw     = 0xFE;            // demo sound on, medium difficulty, normal time, music on
    uint8_t port_unknown = 0xFF;
    uint8_t adc[3] = { 0x80, 0x00, 0x00 };  // steering (as the board reads it), accelerator, brake

    // ---- outputs ----
    std::function<void(uint8_t)> sound_command;     // a byte written to the sound latch (PPI 1 port A)
    std::function<void(bool)>    sound_reset;       // true = the sound CPU is held in reset
    bool display_enable = false;
    bool shadow = false;                    // sprite shadow (true) or highlight (false) on palette $3F
    bool flip = false;
    bool colscroll = false, rowscroll = false;      // tilemap modes (PPI 1 port C bits 2, 1, inverted)

    // ---- hardware RAM (68000 byte order) ----
    uint8_t workram[0x4000];                // main $20C000
    uint8_t subram[0x4000];                 // main $C7C000, second CPU $7C000
    uint8_t roadram[0x1000];                // main $C68000, second CPU $68000
    uint8_t tileram[0x4000];
    uint8_t textram[0x1000];
    uint8_t palram[0x1000];
    uint8_t sprram[0x800];

    std::vector<uint8_t> main_rom, sub_rom;

    // Main-CPU instruction hooks (as SHBoard)
    using PcHook = void (*)(void* ctx, uint32_t pc);
    void     set_pc_hooks(const uint32_t* pcs, int n, PcHook fn, void* ctx);
    uint32_t main_reg(int r) const;
    void     set_main_reg(int r, uint32_t v);

    uint64_t frame_count = 0;
    uint32_t main_pc() const;
    uint32_t sub_pc() const;
    FILE* trace = nullptr;

    // XoBus
    uint32_t bus_read(uint32_t a, int size, int space) override { return read(cur, a, size, space); }
    void     bus_write(uint32_t a, int size, uint32_t v) override { write(cur, a, size, v); }
    int      int_ack(int level) override;
    void     reset_out() override {}
    void     instr_hook(uint32_t pc) override;

    uint32_t read(int cpu, uint32_t a, int size, int space);
    void     write(int cpu, uint32_t a, int size, uint32_t v);
    int      cur = 0;

private:
    static constexpr double CPU_HZ  = 25174800.0 / 4.0;
    static constexpr double LINE_CYCLES = 400.0;            // CPU clock = pixel clock
    static constexpr double QUANTUM = CPU_HZ / 6000.0;
    static constexpr double ADC_CYCLES = 74.0 * 6.0;        // 74 clocks of CPU_HZ / 6

    std::vector<uint8_t> ctx[2];
    double cpu_time[2] = { 0, 0 };
    double now = 0, frame_len = 0;
    bool   first_frame = true;
    int    loaded = -1;
    bool   idle_seen = false;
    bool   sync_now = false, in_execute = false;
    bool   lines_pending = false, pend_irq = false, pend_reset = false;
public:
    bool   sync_ppi = true;     // end the main CPU's time slice at writes to the first PPI, as MAME
private:

    bool irq4_main = false;                 // VBLANK, HOLD_LINE
    bool sub_irq = false;                   // PPI 2 port A bit 6 low
    bool sub_in_reset = false;              // PPI 2 port A bit 5 high
    void apply_irqs();
    void select(int cpu);
    void run_until(double t);

    // 8255s: control word, output latches; PPI 1 port A in mode 2 keeps /OBF
    uint8_t ppi_ctl[2] = { 0x9b, 0x9b }, ppi_out[2][3] = {};
    bool    obf_a = true;
    void ppi_write(int which, int port, uint8_t data);
    uint8_t ppi_read(int which, int port);
    void ppi_set_mode(int which, uint8_t data);
    void ppi_outputs(int which, int port);

    // ADC0804
    uint8_t adc_select = 0, adc_result = 0;
    double  adc_done = 0;                   // board time the running conversion ends
    bool    adc_busy = false;
    void    adc_update(double t);
    double  cpu_now() const;      // board time of the CPU in the core

    static const int MAX_HOOKS = 16;
    uint32_t hook_pc[MAX_HOOKS] = {};
    int      n_hooks = 0;
    PcHook   hook_fn = nullptr;
    void*    hook_ctx = nullptr;

    uint16_t main_read16(uint32_t a, int space);
    void     main_write16(uint32_t a, uint16_t v, uint16_t mask);
    uint16_t sub_read16(uint32_t a);
    void     sub_write16(uint32_t a, uint16_t v, uint16_t mask);
    uint16_t io_read(uint32_t a);
    void     io_write(uint32_t a, uint16_t v, uint16_t mask);
};

extern HOBoard hoboard;
