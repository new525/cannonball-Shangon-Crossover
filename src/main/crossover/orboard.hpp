/***************************************************************************
    The whole Sega System Multi 32 board of OutRunners (MAME set orunnersj): main V70, video,
    I/O, interrupts, timers, EEPROM, comm board and the sound board, run by a transcription of
    MAME 0.264's scheduler. Checked with tools/orbtest against MAME 0.264 recordings (two
    sessions of 8,000 and 7,000 frames: attract, coin, start, car select, races with steering,
    accelerator, brake and shift, player 2, test mode, a fresh and a used EEPROM): the main
    CPU's work RAM and the sound shared RAM are identical to MAME's at every frame, every I/O
    access has the same data at the same attosecond (session 1, also every Z80 port and
    MultiPCM write), both screens are pixel-identical at every snapshot, and the EEPROM ends
    identical.

    Transcribed from MAME 0.264 (all license:BSD-3-Clause, see orboard.LICENSE):
    - src/mame/sega/segas32.cpp, segas32.h (copyright-holders:Aaron Giles): the
      sega_multi32_analog_state machine (multi32_analog_map, interrupt controller, its two
      timers, VBLANK interrupts, random_number_r, sound interrupt controller hookup, the I/O chip
      callbacks, the ADC banking);
    - src/emu/schedule.cpp, diexec.cpp, timer.cpp, screen.cpp (copyright-holders:Aaron Giles):
      the device_scheduler (timeslices, timers, synchronize, abort_timeslice, suspend/resume),
      the device_execute_interface bookkeeping (local time, cycle arithmetic, input-line event
      queues, RESET), emu_timer, and the screen timing (VBLANK begin/end, line 0);
    - src/emu/emumem*.cpp (copyright-holders:Olivier Galibert): how handlers narrower than the
      32-bit bus are called (one call per active unit, in address order; unmapped lanes read as
      the unmap value);
    - src/emu/machine.cpp (copyright-holders:Aaron Giles): running_machine::rand().
    The devices are in crossover/s32io.* (315-5296, MSM6253, 93C46, comm board), the V70 in
    crossover/v60, the video in crossover/s32video.*, the sound board in crossover/orsound.*.

    ---------------------------------------------------------------------------------------
    Scheduling (MAME's device_scheduler for this machine):
    - two CPUs in MAME's execute order: the V70 (20 MHz) then the Z80 (8,053,975 Hz). A CPU
      held in reset is suspended with "eat cycles": its time advances by whole cycles without
      running. Each timeslice runs the V70 then the Z80 up to the target (the next timer, or
      the 60 Hz quantum), then fires the timers due. Each CPU's local time is its cycle count
      times its attoseconds per cycle (1e18 / clock, truncated), as MAME accumulates it.
    - timers, in MAME's list order (by expiry; an equal expiry goes after the earlier ones):
      screen VBLANK begin (every 1/60 s from 1/60 s), screen line 0 (VBLANK end), the driver's
      VBLANK-end interrupt and sprite update (50 us after it), the interrupt controller's two
      timers, the YM3438's two timers, the sound manager's 50 Hz stream update, and the
      synchronising timers: input-line changes (both CPUs' IRQ lines and the Z80's RESET go
      through MAME's per-line event queue) and the YM3438's mode writes / interrupt checks.
      Arming a timer that becomes the first one ends the executing CPU's timeslice
      (abort_timeslice) after its current instruction.
    - time() is the firing timer's expiry in a timer callback, the executing CPU's local time
      plus the cycles it has run in the slice (the V70 counts 8 cycles per instruction before
      its accesses), or the base time.
    - The frame point: at each VBLANK begin MAME draws both screens, then calls the Lua
      frame_done callbacks and updates the input ports, then signals the VBLANK interrupt.
      on_frame is called there (frame n = the n-th VBLANK, at n/60 s; MAME's Lua frame counter is
      n + 1 because it also counts the start-up frame at time 0).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "crossover/s32io.hpp"
#include "crossover/orsound.hpp"
#include "crossover/s32video.hpp"
#include "crossover/v60/v60.hpp"

class ORBoard : public V60Bus, public ORSound::Host
{
public:
    // ---- ROMs (MAME's regions of ROM_START( orunnersj )) ----------------------------------
    struct Roms
    {
        std::vector<uint8_t>  maincpu;      // 2 MB: epr-15616/15617 ROM_LOAD32_WORD_x4, mpr-15538/15539 at $100000
        std::vector<uint8_t>  soundcpu;     // 512 KB: epr-15550
        std::vector<uint8_t>  sega;         // 4 MB: mpr-15551, mpr-15552 (MultiPCM)
        std::vector<uint8_t>  gfx1;         // 4 MB: tiles
        std::vector<uint32_t> sprites;      // 16 MB as MAME holds ROM_REGION32_BE
    };
    // Loads the orunnersj files by their MAME names from `dir`
    static bool load_roms(const std::string& dir, Roms& roms, std::string& error);

    ORBoard();
    ~ORBoard();
    bool init(Roms&& roms);                 // device_start of everything
    bool ready() const { return ready_; }

    // Power-on (MAME start + hard reset). eeprom128: the EEPROM's 128 bytes (MAME's nvram file
    // format: 64 little-endian 16-bit cells), nullptr = a fresh EEPROM (every cell $FFFF).
    void power_on(const uint8_t* eeprom128 = nullptr);

    // ---- running -------------------------------------------------------------------------------
    void run_frame();                       // up to and including the next VBLANK begin's timeslice
    void run_until(const OrTime& t);        // timeslices until the base time reaches t
    OrTime now() const { return basetime; }
    uint64_t frame() const { return frame_no; }
    std::function<void(uint64_t frame)> on_frame;   // at each VBLANK begin (see above)

    // ---- inputs (MAME's port values; the defaults are MAME's with nothing pressed) ----------
    // Digital ports are active low (bit clear = pressed), as the I/O chips read them.
    struct Inputs
    {
        uint8_t p1_a = 0xff;        // io_chip_0 port A: bit 0 shift up, bit 1 shift down (player 1)
        uint8_t p2_a = 0xff;        // io_chip_0 port B: bit 0 DJ/music, 1 <<, 2 >> (player 1)
        uint8_t portc_a = 0xff;
        uint8_t service12_a = 0xff; // io_chip_0 port E: 1 service, 2 test, 4 coin 1, 10 start 1, 20 start 2
        uint8_t service34_a = 0x7f; // io_chip_0 port F bits 0-6 (DIP switches off, push switches); bit 7 = EEPROM
        uint8_t p1_b = 0xff;        // io_chip_1 port A: player 2's shift up/down
        uint8_t p2_b = 0xff;        // io_chip_1 port B: player 2's DJ/music, <<, >>
        uint8_t portc_b = 0xff;
        uint8_t service12_b = 0xff; // io_chip_1 port E: 1 service 2, 2 service mode 2, 4 coin 2, 10 start 2
        uint8_t service34_b = 0x7f; // io_chip_1 port F bits 0-6; bit 7 = EEPROM
        // ANALOG1-8: 1 P1 steering (0x80 = centre), 2 P1 accelerator, 3 P1 brake,
        // 4 P2 steering, 7 P2 accelerator, 8 P2 brake (ANALOG5/6 do not exist: read as 0)
        uint8_t analog[8] = { 0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00 };
    };
    Inputs in;
    // Conveniences for one player (0 = monitor A, 1 = monitor B)
    void set_steering(int player, uint8_t v)    { in.analog[player ? 3 : 0] = v; }
    void set_accelerator(int player, uint8_t v) { in.analog[player ? 6 : 1] = v; }
    void set_brake(int player, uint8_t v)       { in.analog[player ? 7 : 2] = v; }
    void set_shift_up(int player, bool down)    { bit(player ? in.p1_b : in.p1_a, 0x01, down); }
    void set_shift_down(int player, bool down)  { bit(player ? in.p1_b : in.p1_a, 0x02, down); }
    void set_coin(int player, bool down)        { bit(player ? in.service12_b : in.service12_a, 0x04, down); }
    void set_start(int player, bool down)       { bit(player ? in.service12_b : in.service12_a, 0x10, down); }
    void set_test(bool down)                    { bit(in.service12_a, 0x02, down); }
    void set_service(bool down)                 { bit(in.service12_a, 0x01, down); }

    // ---- outputs --------------------------------------------------------------------------------
    S32Video& video() { return *vid; }
    // the last frame drawn on each screen (0 = left/monitor A, 1 = right/monitor B):
    // screen_width(which) x 224 pixels, 0x00RRGGBB, row stride S32Video::SCREEN_W_MAX
    const uint32_t* screen_pixels(int which) const { return vid->screen_pixels(which); }
    int screen_width(int which) const { return vid->visible_width(which); }
    // Screens drawn at each VBLANK (bit 0 left, bit 1 right). Drawing is about 70 % of the
    // board's time; it changes nothing the CPUs see (verified: RAM and I/O identical with 0).
    uint8_t draw_mask = 3;
    ORSound& sound() { return snd; }        // its streams (ym0/ym1/pcm0/pcm1); flush_streams() after a frame
    uint8_t lamps_d[2] = {}, lamps_g[2] = {}; // I/O chips' port D and G outputs (lamps, coin counters)

    // ---- memory ---------------------------------------------------------------------------------
    uint8_t work_ram[0x20000];              // main $200000-$21FFFF (byte = address order)
    uint8_t* shared_ram() { return snd.shared; }        // main $700000-$701FFF = Z80 $E000-$FFFF
    uint8_t read8(uint32_t addr) const;     // work RAM, shared RAM or ROM (no side effects)
    void write8(uint32_t addr, uint8_t v);  // work RAM or shared RAM

    // ---- EEPROM ---------------------------------------------------------------------------------
    void eeprom_save(uint8_t* data128) const { eeprom.save(data128); }
    void eeprom_load(const uint8_t* data128) { eeprom.load(data128); }

    // ---- tests ----------------------------------------------------------------------------------
    // Every main-CPU access MAME's reference recorder taps (reads in $800000-$801FFF,
    // $C00000-$CFFFFF, $D80000-$DFFFFF; writes in $800000-$801FFF, $C00000-$D7FFFF), with the
    // dword address, the data (reads: the whole dword the handlers return), the mask and time().
    std::function<void(bool write, uint32_t addr, uint32_t data, uint32_t mask, const OrTime& t)> io_tap;
    uint64_t rand_calls = 0;
    V60Cpu& maincpu() { return *cpu; }

    // ---- instruction hooks (the crossover's vehicles, crossover/orferrari) --------------------
    // fn(ctx, pc) is called before the V70 executes the instruction at one of these addresses; it
    // may change the registers (the PC too: execution continues there) and memory. With no hooks
    // set (the default) the V70's instruction hook is off and the board runs exactly as checked
    // against MAME. A class derived from the board that overrides debug_instruction_hook (the
    // tools) replaces this.
    typedef void (*PcHook)(void* ctx, uint32_t pc);
    void set_pc_hooks(const uint32_t* pcs, int n, PcHook fn, void* ctx);
    uint32_t main_reg(int n) const { return cpu->reg(n); }            // V60Cpu::reg numbering (PC 32)
    void set_main_reg(int n, uint32_t v) { cpu->set_reg(n, v); }
    void debug_instruction_hook(uint32_t pc) override;
    OrTime time() const;                    // device_scheduler::time()

    // ---- V60Bus ---------------------------------------------------------------------------------
    uint32_t read32(uint32_t addr, uint32_t mem_mask) override;
    void     write32(uint32_t addr, uint32_t data, uint32_t mem_mask) override;

    // ---- ORSound::Host --------------------------------------------------------------------------
    void ym_timer(int tnum, int64_t expire) override;
    void ym_sync(int kind, uint8_t data) override;
    bool executing() override { return executing_ != EX_NONE; }
    void irq_line(int state, int vector) override;
    void main_irq() override { signal_v60_irq(MAIN_IRQ_SOUND); }

private:
    static void bit(uint8_t& port, uint8_t mask, bool down) { port = down ? (uint8_t)(port & ~mask) : (uint8_t)(port | mask); }

    bool ready_ = false;
    Roms roms;
    std::unique_ptr<V60Cpu> cpu;
    std::unique_ptr<S32Video> vid;
    ORSound snd;
    Sega315_5296 io_chip[2];
    Msm6253 adc;
    Eeprom93C46 eeprom;
    S32Comm comm;
    uint8_t analog_bank = 0;
    uint32_t rand_seed = 0x9d14abd7;       // running_machine::m_rand_seed
    uint32_t machine_rand();

    std::vector<uint8_t> hook_map;          // one byte per program address below $100000: 1 = hooked
    PcHook hook_fn = nullptr;
    void*  hook_ctx = nullptr;

    // ---- segas32.cpp ----
    enum { MAIN_IRQ_VBSTART = 0, MAIN_IRQ_VBSTOP = 1, MAIN_IRQ_SOUND = 2, MAIN_IRQ_TIMER0 = 3, MAIN_IRQ_TIMER1 = 4 };
    uint8_t v60_irq_control[0x10];
    void update_irq_state();
    void signal_v60_irq(int which);
    uint8_t int_control_r(uint32_t offset);
    void int_control_w(uint32_t offset, uint8_t data);
    void start_of_vblank_int();
    void end_of_vblank_int();
    void io_out_port(int chip, int port, uint8_t data);
    void io_out_cnt(int chip, int cnt, int state);
    uint8_t io_in_port(int chip, int port);
    uint32_t analog_read(int channel);

    // ---- scheduler ----
    enum { EX_NONE = -1, EX_V70 = 0, EX_Z80 = 1 };
    enum { SUSPEND_REASON_RESET = 0x0002 };
    struct Exec
    {
        int64_t  cycles = 0;            // every cycle since power-on (run or eaten): local time = cycles * apc
        uint32_t cps = 0;
        int64_t  apc = 0;               // m_attoseconds_per_cycle
        int      divshift = 0;
        uint32_t divisor = 0;
        uint32_t suspend = 0, nextsuspend = 0;
        bool     eatcycles = false, nexteatcycles = false;
        void     set_clock(uint32_t hz);
        OrTime   local() const;         // m_localtime
        OrTime   at(int64_t c) const;   // the local time after c cycles
    };
    Exec ex[2];
    int executing_ = EX_NONE;
    uint64_t v70_slice_base = 0;        // V60Cpu::total_cycles() at the slice's start
    OrTime basetime;
    bool suspend_changes_pending = true;
    OrTime quantum;

    enum TimerKind { TK_VBLANK_BEGIN, TK_SCANLINE0, TK_VBLANK_END_INT, TK_SPRITES, TK_V60_IRQ0, TK_V60_IRQ1,
                     TK_YM0, TK_YM1, TK_SOUND_UPDATE, TK_SYNC_INPUT, TK_SYNC_YM, TK_COUNT };
    struct Timer
    {
        OrTime expire = OrTime::never(), start, period = OrTime::never();
        bool enabled = false, temporary = false;
        int kind = 0, param = 0;
        int64_t aux = 0;                // TK_YM*: the expiry in Z80 cycles; TK_SYNC_YM: the data
    };
    Timer persistent[TK_SYNC_INPUT];
    std::vector<Timer*> active;          // m_timer_list without the never-expiring dummy
    std::vector<std::unique_ptr<Timer>> pool;
    std::vector<Timer*> free_timers;
    bool in_callback = false;
    Timer* callback_timer = nullptr;
    bool callback_timer_modified = false;
    OrTime callback_expire;

    Timer* timer_alloc_temp();
    void timer_list_insert(Timer* t);
    void timer_list_remove(Timer* t);
    void timer_adjust(Timer* t, OrTime delay, int param = 0, OrTime period = OrTime::never());
    void timer_adjust_at(Timer* t, const OrTime& expire, int param);
    void timer_enable(Timer* t, bool enable);
    void synchronize(int kind, int param, int64_t aux);
    void abort_timeslice();
    void timeslice();
    void execute_timers();
    void fire(Timer* t);

    // device_execute_interface::device_input event queues: V70 IRQ0, Z80 IRQ0, Z80 RESET
    enum { LINE_V70_IRQ0, LINE_Z80_IRQ0, LINE_Z80_RESET, LINE_COUNT };
    struct InputQueue { int32_t queue[32]; int qindex = 0; };
    InputQueue lines[LINE_COUNT];
    void set_state_synced(int line, int state, int vector);
    void empty_event_queue(int line);
    void suspend_z80(bool on);

    // screen (MAME's screen_device timing, both screens identical)
    int64_t frame_period = 0, scantime = 0, pixeltime = 0, vblank_period = 0;
    OrTime vblank_start_time;
    OrTime time_until_pos(int vpos, int hpos = 0) const;
    uint64_t frame_no = 0;
};
