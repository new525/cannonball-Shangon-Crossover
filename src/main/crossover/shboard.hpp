/***************************************************************************
    Emulated Sega OutRun-hardware board running Super Hang-On.

    The board as MAME 0.264 describes it (segaorun.cpp, outrun_base + shangon_fd1089b;
    315_5195.cpp):
    - main 68000 at 10 MHz (FD1089B-encrypted program, key 317-0034) behind the 315-5195
      memory mapper; second 68000 at 10 MHz with a fixed map; Z80 sound board (crossover/
      soundboard, fed through the mapper's sound latch);
    - 262 lines of 400 pixels at 6.2937 MHz (60.05 Hz). Main CPU: IRQ2 at the horizontal blank
      of lines 65, 129 and 193 (off at the start of the next line), IRQ4 for line 223 (VBLANK),
      IRQ6 when both are up. Second CPU: IRQ4 for line 223.
    - Interleave: a 1/6000 s maximum quantum, and instruction-level interleave for 100 us after
      an interrupt line changes while one is asserted (MAME's perfect_quantum).

    CPU core: Musashi (crossover/m68k, MIT). Both 68000s share Musashi's single global state;
    the board swaps contexts between time slices.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <functional>

class RomSet;

class SHBoard
{
public:
    SHBoard();
    ~SHBoard();

    bool init(const RomSet& roms);          // SuperHangOnRoms (maincpu_data/_opcodes, subcpu, ...)
    void reset();
    void run_frame();                       // from the start of line 224 to the next one
    bool ready = false;

    // ---- inputs (as MAME's ports for shangon; digital ports are active low) ----
    uint8_t port_service = 0xFF;            // "SERVICE": unused on Super Hang-On
    uint8_t port_buttons = 0xFF;            // "UNKNOWN": b0 coin1, b1 coin2, b2 test, b3 service, b4 start, b5 supercharger
    uint8_t port_coinage = 0xFF;            // "COINAGE"
    uint8_t port_dsw     = 0xFE;            // "DSW" defaults: demo sound on, normal difficulty and time
    uint8_t adc[4] = { 0x80, 0x00, 0x00, 0x00 };   // steering (as the board reads it), gas, brake, -

    // ---- outputs ----
    std::function<void(uint8_t)> sound_command;     // a byte written to the sound latch
    std::function<void(bool)>    sound_reset;       // true = the sound CPU is held in reset
    bool display_enable = false;
    uint8_t road_control = 0;
    // tilemap scroll/page registers latched from text RAM at line 261 (segaic16 16B tilemaps):
    // [0..3] = foreground, background, alternate foreground, alternate background
    uint16_t latched_page[4] = {}, latched_yscroll[4] = {}, latched_xscroll[4] = {};

    // ---- hardware RAM (68000 byte order) ----
    uint8_t workram[0x8000];
    uint8_t cpu1ram[0x8000];                // shared: main via the mapper, second CPU at $60000
    uint8_t roadram[0x1000];
    uint8_t roadbuf[0x1000];                // the road chip's second half (swapped on a control read)
    uint8_t tileram[0x10000];
    uint8_t textram[0x1000];
    uint8_t palram[0x2000];
    uint8_t sprram[0x1000];

    // ---- ROM ----
    std::vector<uint8_t> rom_data, rom_op, rom_raw, sub_rom;

    // ---- debugging / verification ----
    uint64_t frame_count = 0;
    uint32_t main_pc() const;
    uint32_t sub_pc() const;
    FILE* trace = nullptr;                  // per-instruction PC trace ("m pc" / "s pc") when set
    double cycles_per_line() const { return LINE_CYCLES; }

    // Musashi glue (public for the C callbacks)
    uint32_t read(int cpu, uint32_t a, int size, int space);   // space 0 data, 1 program
    void     write(int cpu, uint32_t a, int size, uint32_t v);
    int      int_ack(int level);
    void     reset_out();
    void     instr_hook(uint32_t pc);
    int      cur = 0;                       // CPU running in the Musashi core: 0 main, 1 second

private:
    static constexpr double CPU_HZ   = 10000000.0;              // 40 MHz / 4
    static constexpr double PIX_HZ   = 25174800.0 / 4.0;         // 6.2937 MHz
    static constexpr double LINE_CYCLES = CPU_HZ * 400.0 / PIX_HZ;
    static constexpr double QUANTUM  = CPU_HZ / 6000.0;
    static constexpr double PERFECT  = CPU_HZ * 100e-6;

    std::vector<uint8_t> ctx[2];            // Musashi contexts
    double  cpu_time[2] = { 0, 0 };         // cycles run, relative to the frame start
    double  perfect_until = -1;             // instruction-level interleave until this time
    double  now = 0;                        // board time, cycles from the frame start
    double  frame_len = 0;                  // length of the frame just run
    bool    first_frame = true;
    int     loaded = -1;                    // context currently in the core

    // interrupt lines
    bool irq2 = false, vblank = false;
    int  mapper_irq = 0;                    // HOLD_LINE interrupt from mapper register 4 (0 = none)
    void update_irqs();
    bool sub_reset_pending = false;

    // 315-5195 mapper
    uint8_t regs[0x20];
    uint8_t to_sound = 0, from_sound = 0;
    struct Page { uint8_t rkind, wkind; uint8_t* rptr; const uint8_t* optr; uint8_t* wptr; };
    enum { K_NONE, K_ROM, K_RAM, K_SUBROM, K_MISCIO, K_ROADCTL, K_IGNORE };
    Page pages[0x1000];                     // 4 KB pages over the 24-bit space
    void update_mapping();
    uint8_t mapper_read(uint32_t offset);
    void    mapper_write(uint32_t offset, uint8_t data);
    uint16_t open_bus();

    // I/O
    uint8_t adc_select = 0, adc_latch = 0;
    uint16_t miscio_read(uint32_t a);
    void     miscio_write(uint32_t a, uint16_t data, uint16_t mask);
    uint16_t roadctl_read();
    void     roadctl_write(uint16_t data, uint16_t mask);

    uint16_t main_read16(uint32_t a, int space);
    void     main_write16(uint32_t a, uint16_t v, uint16_t mask);
    uint16_t sub_read16(uint32_t a);
    void     sub_write16(uint32_t a, uint16_t v, uint16_t mask);

    void select(int cpu);
    void run_until(double t);               // both CPUs up to time t (cycles from the frame start)
};

extern SHBoard shboard;
