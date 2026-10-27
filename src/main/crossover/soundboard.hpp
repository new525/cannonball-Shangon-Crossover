/***************************************************************************
    Virtual Sega sound board: Z80 + YM2151 + Sega PCM (315-5218).

    The sound hardware of OutRun and Super Hang-On, as MAME 0.264 describes it (segaorun.cpp):
    Z80, YM2151 and PCM all at 4 MHz; PCM in BANK_512 mode; Z80 map ROM $0000-$EFFF, PCM
    $F000-$F0FF (mirrored to $F7FF), RAM $F800-$FFFF; ports $00-$01 (mirror $3E) YM2151, $40
    (mirror $3F) the sound command; mix YM2151 x 0.43, PCM x 1.0 per side.

    - Z80: crossover/z80 (MIT; passes zexdoc and zexall with exact cycle counts).
    - YM2151: ymfm as shipped with MAME 0.264 (BSD-3, Aaron Giles), driven the way MAME's
      ymfm_device_base drives it: timers, busy flag and IRQ in chip-clock time, and the chip's
      output brought up to date before every register access.
    - PCM: MAME 0.264's segapcm.cpp transcribed (native rate clock / 128 = 31250 Hz).
    - The main CPU's command writes arrive through the 315-5195 mapper: each write latches a
      byte and raises NMI; the Z80 reads it at port $40.

    Chip outputs are generated at their native rates and converted to the output rate by linear
    interpolation at the end of each frame.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <deque>
#include <memory>

extern "C" {
#include "crossover/z80/z80.h"
}

namespace ymfm { class ym2151; }

class SegaSoundBoard
{
public:
    SegaSoundBoard();
    ~SegaSoundBoard();

    // z80rom: program (up to $F000 bytes); pcm: the sample ROM region as MAME's "pcm" region
    bool init(const std::vector<uint8_t>& z80rom, const std::vector<uint8_t>& pcm, int out_rate, int fps);
    void reset();
    void cpu_reset();       // the Z80 alone (the main CPU's sound reset line); chips keep running
    bool cpu_held = false;  // reset line asserted: the Z80 does not run
    bool reset_after_commands = false;  // tests: cpu_reset() after this frame's command bytes

    // One byte written by the main CPU to the sound command register (mapper register 3)
    void command(uint8_t b) { pending.push_back(b); }

    // Run one video frame: deliver pending commands, run the Z80 and the chips, and add the
    // stereo output (interleaved L/R, 'samples' frames, 16-bit scale) into mix.
    void frame(int32_t* mix, int samples);

    // Timed control (verification against MAME logs): absolute chip-clock time
    int64_t time() const { return now; }
    void run_to(int64_t t) { if (t > now) run(t - now); }
    void command_now(uint8_t b) { latch = b; z80_gen_nmi(&cpu); }
    void drain(int32_t* mix, int samples);    // convert what has been generated so far

    // Optional log of YM2151 writes ("<frame> <reg> <value>", hex) for verification
    void set_ym_log(FILE* f) { ym_log = f; }
    void set_pcm_log(FILE* f) { pcm_log = f; }   // Z80 writes to the PCM chip: "<offset> <value> <cycle>"
    uint32_t frames_run = 0;
    bool ready = false;
    const uint8_t* z80_ram() const { return ram; }      // $F800-$FFFF (tests)
    const z80& z80_state() const { return cpu; }

    // ymfm interface glue (public for the adapter class)
    void ym_set_timer(uint32_t tnum, int32_t clocks);
    void ym_set_busy_end(uint32_t clocks) { busy_end = now + clocks; }
    bool ym_is_busy() const { return now < busy_end; }
    void ym_update_irq(bool asserted) { irq = asserted; }

private:
    struct Iface;
    std::unique_ptr<Iface> iface;
    std::unique_ptr<ymfm::ym2151> ym;

    z80 cpu;
    uint8_t rom[0x10000];
    uint8_t ram[0x800];
    uint8_t pcm_ram[0x800];
    uint8_t pcm_low[16];
    std::vector<uint8_t> pcm_rom;
    uint32_t pcm_mask = 0;

    int out_rate = 44100, fps = 60;
    int64_t now = 0;                // Z80 / chip clock cycles since reset
    double  frame_frac = 0;
    int64_t ym_clock = 0, pcm_clock = 0;
    int64_t timer_end[2] = { -1, -1 };
    int64_t busy_end = 0;
    bool    irq = false;

    std::deque<uint8_t> pending;
    uint8_t latch = 0;
    uint8_t ym_addr = 0;

    // native-rate output not yet converted: stereo pairs
    std::vector<int32_t> ym_buf, pcm_buf;
    double ym_pos = 0, pcm_pos = 0;     // read position (in native samples) into the buffers

    FILE* ym_log = nullptr;
    FILE* pcm_log = nullptr;

    void run(int64_t cycles);
    void ym_catch_up();
    void pcm_catch_up();
    void pcm_sample(int32_t& l, int32_t& r);
    void resample(std::vector<int32_t>& buf, double& pos, double step, int32_t* mix, int samples, double gain);

    static uint8_t mem_r(void* ud, uint16_t a);
    static void    mem_w(void* ud, uint16_t a, uint8_t v);
    static uint8_t io_r(z80* z, uint8_t port);
    static void    io_w(z80* z, uint8_t port, uint8_t v);
};
