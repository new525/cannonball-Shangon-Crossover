/***************************************************************************
    The sound side of Sega's System H1 board (Cool Riders), emulated for Cool Riders' music.

    The board as MAME 0.264 describes it (src/mame/sega/coolridr.cpp, used as documentation only):
    - an SH-1 (SH7032, 16 MHz, program ep17662.12) that streams sample data: through a 2-channel
      DMA unit at $04000000 it copies blocks from the main SH-2's address space (its program ROM and
      the 16 MB data ROMs) into the sound RAMs, and it takes the main CPU's sound commands from a
      ring in the shared communication RAM ($05300400-$0530047F, write index byte $05300501);
    - a 68000 (16 MHz) whose program the SH-1 copies to sound RAM 1 and releases from reset (DMA
      register 8); it drives the two SCSPs;
    - two SCSPs (YMF292-F, 22.579 MHz), each with 512 KB of sample RAM that all three see. SCSP 1
      interrupts the 68000 and both interrupt the SH-1 (level 14, status at DMA register 8);
    - the SH-1 also gets a level-12 interrupt at scanline 384 of the 57 Hz, 512-line screen, and
      reads the controls through a 315-5649 I/O chip.

    Scheduling follows MAME's device_scheduler: the CPUs run in MAME's order (68000, then SH-1) in
    timeslices that end at the next timer (scanlines, the SCSP timers, the 50 Hz stream update) or
    when an input line changes (MAME's synchronised set_input_line), with MAME's attosecond time and
    cycle arithmetic. The main SH-2 is not emulated: its writes to the shared RAM (the commands) are
    given to the board with their times (main_write).

    Cores: SH-1 crossover/sh (from MAME, BSD-3-Clause), 68000 Musashi (shared, through xo_bus),
    SCSP crossover/scsp (from MAME, BSD-3-Clause).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <functional>
#include <memory>
#include "crossover/xobus.hpp"
#include "crossover/sh/shcpu.hpp"
#include "crossover/scsp/scsp.hpp"

// MAME's attotime: whole seconds and attoseconds
struct CrTime
{
    static constexpr int64_t ATTO = 1000000000000000000LL;
    int64_t s = 0, a = 0;
    CrTime() {}
    CrTime(int64_t sec, int64_t atto) : s(sec), a(atto) { norm(); }
    void norm() { while (a >= ATTO) { a -= ATTO; s++; } while (a < 0) { a += ATTO; s--; } }
    bool operator<(const CrTime& o) const  { return s < o.s || (s == o.s && a < o.a); }
    bool operator<=(const CrTime& o) const { return !(o < *this); }
    bool operator>=(const CrTime& o) const { return !(*this < o); }
    bool operator==(const CrTime& o) const { return s == o.s && a == o.a; }
    CrTime operator+(const CrTime& o) const { return CrTime(s + o.s, a + o.a); }
    CrTime operator-(const CrTime& o) const { return CrTime(s - o.s, a - o.a); }
    double sec() const { return (double) s + (double) a * 1e-18; }
    static CrTime never() { return CrTime(1000000000, 0); }
};

class RomSet;

class CRSound : public XoBus, public ShCpu::Bus
{
public:
    CRSound();
    ~CRSound();

    // maincpu: the SH-2 program ROM image (2 MB, big-endian); gfx: the data ROMs (16 MB, big-endian);
    // sub: the SH-1 program (big-endian). nvram: the SH-1's 512-byte backup RAM (nullptr = zeros).
    bool init(const std::vector<uint8_t>& maincpu, const std::vector<uint8_t>& gfx, const std::vector<uint8_t>& sub);
    void power_on(const uint8_t* nvram = nullptr);
    bool ready = false;

    // A write by the main CPU to the shared communication RAM (main $03F00000 + offset), at time t
    void main_write(const CrTime& t, uint32_t offset, uint32_t data, uint32_t mask);

    // Run the board up to time t
    void run_until(const CrTime& t);
    CrTime now() const { return basetime; }

    // Output: per sample (44099 Hz), MAME's mixed value for each speaker (both SCSPs summed, in
    // MAME's +-1.0 scale before the final clamp)
    std::vector<float> out_l, out_r;
    uint32_t sample_rate() const { return scsp[0].sample_rate(); }

    // Memory, for verification and tests
    uint16_t soundram[2][0x40000];      // 512 KB each, host-order words
    uint8_t  sh_ram_onchip[0x2000];     // $07FFE000-$07FFFFFF
    uint8_t  comm_ram[0x10000];         // $01000000
    uint8_t  share3[0x10000];           // $05300000 (main $03F00000)
    uint8_t  ram_5000[0x1000], ram_5200[0x200], ram_6200[0x1000], nvram[0x200];
    uint16_t sh_regs[0x100];            // $05FFFE00 internal registers (RAM in MAME 0.264)
    uint8_t  m68k_ram[0x10000];         // $800000
    ShCpu&   sh1() { return *sh; }
    FILE*    dma_log = nullptr;
    FILE*    scsp_log = nullptr;          // the 68000's SCSP register writes: "<s> <atto> <addr> <data> <mask>"
    uint64_t sh_cycles = 0, m68k_cycles = 0;

    // XoBus (68000)
    uint32_t bus_read(uint32_t a, int size, int space) override;
    void     bus_write(uint32_t a, int size, uint32_t v) override;
    void     bus_write_at(uint32_t a, int size, uint32_t v);
    int      int_ack(int level) override;
    void     reset_out() override {}
    void     instr_hook(uint32_t pc) override;
    FILE*    m68k_trace = nullptr;        // tests: "<total cycles hex> <pc hex>" per 68000 instruction
    double   trace_from = 0, trace_to = 0;

    // ShCpu::Bus (SH-1)
    uint8_t  read8(uint32_t a) override;
    uint16_t read16(uint32_t a) override;
    uint32_t read32(uint32_t a) override;
    void     write8(uint32_t a, uint8_t v) override;
    void     write16(uint32_t a, uint16_t v) override;
    void     write32(uint32_t a, uint32_t v) override;
    void     irq_acknowledge(int line) override;

    // the controls as the 315-5649 reads them (MAME's defaults with nothing pressed)
    uint8_t in_port[7] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t an_port[8] = { 0x80, 0xff, 0xff, 0xff, 0x80, 0xff, 0xff, 0xff };

private:
    std::vector<uint8_t> main_rom, gfx_rom, sub_rom;
    std::unique_ptr<ShCpu> sh;
    Scsp scsp[2];
    uint32_t rng_seed = 0x9d14abd7;
    std::vector<uint8_t> m68k_ctx;

    // ---- scheduler (MAME's device_scheduler, reduced to this board) ----
    struct Exec
    {
        CrTime   local;
        int64_t  atto_per_cycle = 0;
        uint32_t cps = 0;
        int      divshift = 0;
        uint64_t divisor = 0;
        bool     suspended = false, next_suspended = false;
        int      cycles_running = 0;
        void     set_clock(uint32_t hz);
        CrTime   cycles_to_time(int cycles) const;
    };
    Exec ex[2];                         // 0 = 68000, 1 = SH-1 (MAME's execute order)
    int  executing = -1;
    CrTime basetime, target;
    bool suspend_changes = false;

    struct Timer { CrTime expire; int kind; int param; uint64_t seq; };
    std::vector<Timer> timers;          // pending
    uint64_t timer_seq = 0;
    enum { T_SCANLINE, T_STREAM, T_SCSP, T_SYNC_IRQ68, T_SYNC_IRQSH, T_SYNC_RESET68, T_MAINW };
    void timer_add(const CrTime& when, int kind, int param);
    void timer_remove(int kind, int param);
    CrTime cur_time() const;            // scheduler time(): the executing CPU's local time
    void abort_slice();
    void timeslice();                   // one pass: CPUs up to the next timer, then that timer
    void fire(const Timer& t);

    // scanlines: 512 per frame of HZ_TO_ATTOSECONDS(57)
    int64_t frame_attos = 0, scan_attos = 0;
    uint64_t frame_no = 0; int scanline = 0;
    CrTime frame_start;

    // main CPU writes waiting for their timeslice
    struct MainW { CrTime t; uint32_t off, data, mask; };
    std::vector<MainW> mainw;
    size_t mainw_pos = 0;
    void apply_main_writes(const CrTime& upto);

    // ---- interrupts ----
    int  irq68[8] = {};                 // 68000 input line states (MAME line state values)
    bool m68k_reset_line = true;        // held by machine_reset
    void m68k_update_ipl();
    int  m68k_access_cycles = 0;        // cycles from the instruction's start to the bus access being made
    int  sh_irq_state[16] = {};
    void set_68k_line(int line, int state);       // MAME set_input_line (synchronised)
    void set_sh_line(int line, int state);
    void set_68k_reset(bool asserted);
    std::vector<std::pair<int,int>> pend68, pendsh;
    int pend_reset = -1;
    bool m68k_loaded = false;
    void m68k_load();
    void m68k_save();

    // ---- SCSP glue ----
    uint64_t gen[2] = {0, 0};          // samples generated per chip
    int64_t  sample_attos = 0;
    void scsp_catch_up(int chip);
    void scsp_catch_up_all() { scsp_catch_up(0); scsp_catch_up(1); }
    std::vector<int32_t> tmp_l[2], tmp_r[2];
    uint64_t mixed = 0;
    void mix_out();
    uint8_t sound_data = 0, sound_fifo = 0;

    // ---- DMA ($04000000) and I/O ----
    uint32_t sound_dma[16] = {};
    uint32_t dma_r(int offset);
    void dma_w(int offset, uint32_t data, uint32_t mask);
    uint16_t ram_word(int chip, uint32_t byte_addr) const { return soundram[chip][(byte_addr >> 1) & 0x3ffff]; }
    uint16_t main_read16(uint32_t a) const;           // the main SH-2's space, for the DMA
    uint8_t  io_value[7] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, io_config = 0xff, io_mode = 0, io_an = 0;
    uint8_t  io_read(int offset);
    void     io_write(int offset, uint8_t data);

    uint32_t sh_read(uint32_t a, int size);
    void     sh_write(uint32_t a, int size, uint32_t v);
    uint16_t m68k_read16(uint32_t a);
    void     m68k_write16(uint32_t a, uint16_t v, uint16_t mask);
};
