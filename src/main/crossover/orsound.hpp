/***************************************************************************
    The sound board of Sega's System Multi 32 (OutRunners), emulated for OutRunners' music.

    As MAME 0.264 describes it (src/mame/sega/segas32.cpp, BSD-3-Clause, Aaron Giles):
    - a Z80 at 8.053975 MHz (MASTER_CLOCK / 4) running the sound program (epr-15550): ROM at
      $0000-$9FFF, an 8 KB window at $A000-$BFFF into the 512 KB ROM (ports $A0-$AF), the
      MultiPCM at $C000-$DFFF, and RAM shared with the main V60 at $E000-$FFFF;
    - a YM3438 at the same clock (ports $80-$83), whose IRQ goes to the Z80 through a small
      interrupt controller (ports $C0-$CF acknowledge, $D0-$D3 map sources to vectors and mask);
    - a MultiPCM (Sega 315-5560) at 10 MHz (MULTI32_CLOCK / 4) with 4 MB of samples: the first
      1 MB direct, two 512 KB windows selected by port $B0-$BF.
    The main V60 talks to the sound program only through the shared RAM (the program polls it).

    MAME's outputs: the YM3438's output 1 and the MultiPCM's output 1 go to the left speaker
    (x 0.40 and x 1.0), output 0 of each to the right. In a race the game puts each player's
    sound on one side: player 1 (monitor A) on the left.

    Time is counted in Z80 cycles. The YM3438's timers run in the same clock; its output and the
    MultiPCM's are generated as MAME's streams are: up to the sample index of the current time,
    rounded up, at the streams' integer rates (55,930 and 44,642 Hz).

    Cores: Z80 crossover/z80 (MIT), YM3438 ymfm (BSD-3-Clause), MultiPCM crossover/multipcm
    (from MAME 0.264, BSD-3-Clause).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
extern "C" {
#include "crossover/z80/z80.h"
}
#include "crossover/multipcm/multipcm.hpp"
#include "crossover/resample.hpp"

namespace ymfm { class ym3438; }

class ORSound
{
public:
    ORSound();
    ~ORSound();

    static const int64_t CLOCK = 8053975;          // Z80 and YM3438: 32,215,900 / 4

    // soundcpu: epr-15550 (512 KB); pcm: the MultiPCM region (4 MB)
    bool init(const std::vector<uint8_t>& soundcpu, const std::vector<uint8_t>& pcm);
    bool ready = false;

    // Power-on: everything cleared, the Z80 held in reset until release_cpu()
    void power_on();
    void release_cpu();                         // the main CPU releases the Z80's RESET (I/O chip CNT2)

    // A write by the main CPU to the shared RAM (main $700000 + offset, a byte), at Z80 cycle t
    void main_write(int64_t t, uint32_t offset, uint8_t data);

    // Runs the board until its time reaches t (Z80 cycles); the last instruction may run past it
    void run_until(int64_t t);
    int64_t now() const { return cyc; }

    // The streams generated so far, in MAME's float scale, not yet taken: each chip's outputs 0
    // (to MAME's right speaker) and 1 (left)
    std::vector<float> ym0, ym1, pcm0, pcm1;
    uint32_t ym_rate() const { return ym_rate_; }
    uint32_t pcm_rate() const { return pcm.sample_rate(); }

    // Tests: every YM3438 and MultiPCM write and port write ("<cycle> io|pcm <addr> <data>")
    FILE* chip_log = nullptr;
    FILE* trace = nullptr;                      // "<cycles since release hex> <pc hex>" per instruction
    int64_t trace_from = 0, trace_to = 0;       // in board cycles

    uint8_t shared[0x2000];

private:
    std::vector<uint8_t> rom, pcm_rom;
    z80 cpu;
    bool cpu_held = true;
    int64_t released_at = 0;
    bool in_step = false;
    unsigned long step_base = 0;
    uint16_t step_pc = 0;
public:
    FILE* op_log = nullptr;                     // tests: the opcode bytes of every chip access
private:
    int64_t time() const;
    uint8_t rom_byte(uint16_t a) const;
    int64_t cyc = 0;

    struct Iface;
    std::unique_ptr<Iface> iface;
    std::unique_ptr<ymfm::ym3438> ym;
    uint32_t ym_rate_ = 0;
    int64_t timer_end[2] = { -1, -1 };
    int64_t busy_end = 0;
    bool ym_irq = false;
    struct SyncEvent { int64_t t; int kind; uint8_t data; };
    std::vector<SyncEvent> sync;                // synchronised YM3438 work waiting for its time

    MultiPcm pcm;
    uint32_t pcm_bank_lo = 0, pcm_bank_hi = 0;

    uint8_t  sound_bank = 0;
    uint8_t  irq_input = 0, irq_control[4] = {};
    uint8_t  dummy = 0;

    uint64_t ym_gen = 0, pcm_gen = 0;           // stream samples generated
    void ym_catch_up();
    void pcm_catch_up();
    static int64_t samples_at(int64_t cycles, uint32_t rate);

    struct MainW { int64_t t; uint32_t off; uint8_t data; };
    std::vector<MainW> mainw;
    size_t mainw_pos = 0;
    int64_t visible_until = 0;                  // main writes before this are visible (the slice's end)
    int64_t slice_end() const;

    void update_irq();
    void signal_irq(int which);
    void clear_irq(int which);
    void timers();

    friend struct ORSoundAccess;
    static uint8_t mem_r(void* ud, uint16_t a);
    static void    mem_w(void* ud, uint16_t a, uint8_t v);
    static uint8_t io_r(z80* z, uint8_t port);
    static void    io_w(z80* z, uint8_t port, uint8_t v);
};
