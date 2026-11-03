/***************************************************************************
    Sega System Multi 32 (OutRunners) board. See orboard.hpp.

    Transcribed from MAME 0.264 (license:BSD-3-Clause): src/mame/sega/segas32.cpp
    (copyright-holders:Aaron Giles), src/emu/schedule.cpp, diexec.cpp, screen.cpp, machine.cpp
    (copyright-holders:Aaron Giles), src/emu/emumem_heu.cpp (copyright-holders:Olivier
    Galibert). Lines marked "PORT:" differ from MAME.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/orboard.hpp"
#include "crossover/rompath.hpp"

#include <algorithm>
#include <cstring>

// segas32.cpp
static const uint32_t V70_CLOCK = 20000000;                 // MULTI32_CLOCK/2 (XTAL 40 MHz)
static const uint32_t Z80_CLOCK = 32215900 / 4;             // MASTER_CLOCK/4
static const int64_t  TIMER_0_ATTOS = OrTime::ATTO / ((32215900 / 2) / 2048);   // from_hz(TIMER_0_CLOCK): 7865 Hz (integer)
// from_hz(TIMER_1_CLOCK): (XTAL(50'000'000)/16)/256 = 12207.03125 Hz (an XTAL, a double):
// attoseconds_t(1e18 / 12207.03125) = 81,920,000,000,000 exactly
static const int64_t  TIMER_1_ATTOS = 81920000000000LL;
enum { SOUND_IRQ_YM3438 = 0, SOUND_IRQ_V60 = 1 };
enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };

ORBoard::ORBoard() { std::memset(work_ram, 0, sizeof work_ram); std::memset(v60_irq_control, 0xff, sizeof v60_irq_control); }
ORBoard::~ORBoard() {}

// ------------------------------------------------------------------------------------------------
// ROMs
// ------------------------------------------------------------------------------------------------
bool ORBoard::load_roms(const std::string& dir, Roms& r, std::string& error)
{
    // ROM_REGION( 0x200000, "mainpcb:maincpu", 0 ): ROM_LOAD32_WORD_x4 / ROM_LOAD32_WORD: the 16-bit
    // words of each file go to every other word lane; the _x4 files are reloaded at +2, +4 and
    // +6 times their length
    struct { const char* name; uint32_t offset, length; int reloads; } files[] = {
        { "epr-15616.ic37", 0x000000, 0x020000, 4 },
        { "epr-15617.ic40", 0x000002, 0x020000, 4 },
        { "mpr-15538.ic36", 0x100000, 0x080000, 1 },
        { "mpr-15539.ic39", 0x100002, 0x080000, 1 },
    };
    r.maincpu.assign(0x200000, 0);
    for (auto& f : files)
    {
        std::vector<uint8_t> d;
        if (!read_rom(dir, f.name, d) || d.size() != f.length) { error = "cannot read " + rom_file(dir, f.name).where(); return false; }
        for (int k = 0; k < f.reloads; k++)
            for (uint32_t i = 0; i < f.length; i += 2)
            {
                uint32_t dst = f.offset + 2 * f.length * (uint32_t) k + i * 2;
                r.maincpu[dst] = d[i];
                r.maincpu[dst + 1] = d[i + 1];
            }
    }
    if (!read_rom(dir, "epr-15550.ic31", r.soundcpu) || r.soundcpu.size() != 0x80000) { error = "cannot read " + rom_file(dir, "epr-15550.ic31").where(); return false; }
    std::vector<uint8_t> p1, p2;
    if (!read_rom(dir, "mpr-15551.ic1", p1) || p1.size() != 0x200000) { error = "cannot read " + rom_file(dir, "mpr-15551.ic1").where(); return false; }
    if (!read_rom(dir, "mpr-15552.ic2", p2) || p2.size() != 0x200000) { error = "cannot read " + rom_file(dir, "mpr-15552.ic2").where(); return false; }
    r.sega = p1;
    r.sega.insert(r.sega.end(), p2.begin(), p2.end());
    return S32Video::load_orunnersj_regions(dir, r.gfx1, r.sprites, error);
}

// ------------------------------------------------------------------------------------------------
// device_execute_interface arithmetic (diexec.cpp interface_clock_changed, schedule.cpp)
// ------------------------------------------------------------------------------------------------
void ORBoard::Exec::set_clock(uint32_t hz)
{
    cps = hz;
    apc = OrTime::ATTO / hz;                            // HZ_TO_ATTOSECONDS(m_cycles_per_second)
    int64_t attos = apc;
    divshift = 0;
    while (attos >= (1LL << 31)) { divshift++; attos >>= 1; }
    divisor = (uint32_t) attos;
}

// MAME adds attotime(0, apc * ran) to m_localtime after each timeslice (ran < cps), so the
// local time is exactly cycles * apc
OrTime ORBoard::Exec::at(int64_t c) const
{
    __int128 t = (__int128) c * apc;
    return OrTime((int64_t)(t / OrTime::ATTO), (int64_t)(t % OrTime::ATTO));
}
OrTime ORBoard::Exec::local() const { return at(cycles); }

// ------------------------------------------------------------------------------------------------
// Setup
// ------------------------------------------------------------------------------------------------
bool ORBoard::init(Roms&& r)
{
    ready_ = false;
    roms = std::move(r);
    if (roms.maincpu.size() != 0x200000 || roms.soundcpu.size() != 0x80000 || roms.sega.size() != 0x400000) return false;
    cpu.reset(new V60Cpu(*this));
    // multi32_map: global_mask $FFFFFF; ROM $000000-$1FFFFF and $F00000-$FFFFFF (region offset
    // 0); work RAM $200000-$21FFFF mirrored over $200000-$2FFFFF
    cpu->set_direct_decode_mask(0xffffff);
    cpu->map_direct(0x000000, 0x1fffff, roms.maincpu.data(), nullptr);
    cpu->map_direct(0xf00000, 0xffffff, roms.maincpu.data(), nullptr);
    for (uint32_t m = 0; m < 8; m++)
        cpu->map_direct(0x200000 + m * 0x20000, 0x21ffff + m * 0x20000, work_ram, work_ram);
    vid.reset(new S32Video());
    if (!snd.init(roms.soundcpu, roms.sega)) return false;
    snd.attach(this);

    ex[EX_V70].set_clock(V70_CLOCK);
    ex[EX_Z80].set_clock(Z80_CLOCK);
    quantum = OrTime(0, OrTime::hz_to_attoseconds(60));    // maximum_quantum(attotime::from_hz(60))

    // screen_device::configure (both screens): set_refresh_hz(60), set_size(52*8, 262),
    // visible lines 0-223
    frame_period = OrTime::hz_to_attoseconds(60);
    scantime = frame_period / 262;
    pixeltime = frame_period / (262 * 416);
    vblank_period = scantime * (262 - 224);

    for (int k = 0; k < TK_SYNC_INPUT; k++) persistent[k].kind = k;

    // device input lines: the I/O chips call these
    for (int c = 0; c < 2; c++)
    {
        io_chip[c].in_port = [this, c](int port) { return io_in_port(c, port); };
        io_chip[c].out_port = [this, c](int port, uint8_t data) { io_out_port(c, port, data); };
        io_chip[c].out_cnt = [this, c](int cnt, int state) { io_out_cnt(c, cnt, state); };
    }
    adc.input = [this](int ch) { return analog_read(ch); };
    eeprom.now = [this]() { return time(); };
    ready_ = true;
    return true;
}

// ------------------------------------------------------------------------------------------------
// Power-on: device_start of everything, then the machine's hard reset
// ------------------------------------------------------------------------------------------------
void ORBoard::power_on(const uint8_t* eeprom128)
{
    if (!ready_) return;

    // scheduler (device_scheduler constructor, running_machine::start)
    basetime = OrTime::zero();
    active.clear();
    free_timers.clear();
    for (auto& t : pool) free_timers.push_back(t.get());
    for (int k = 0; k < TK_SYNC_INPUT; k++) { Timer& t = persistent[k]; t = Timer(); t.kind = k; }
    in_callback = false; callback_timer = nullptr; callback_timer_modified = false;
    executing_ = EX_NONE;
    suspend_changes_pending = true;
    for (Exec& e : ex) { e.cycles = 0; e.suspend = e.nextsuspend = 0; e.eatcycles = e.nexteatcycles = false; }
    for (InputQueue& q : lines) q.qindex = 0;
    rand_seed = 0x9d14abd7; rand_calls = 0;
    frame_no = 0;

    // sound_manager: the stream update timer, every 1/50 s (STREAMS_UPDATE_ATTOTIME)
    timer_adjust(&persistent[TK_SOUND_UPDATE], OrTime::from_hz(50), 0, OrTime::from_hz(50));

    // device_start: the devices' power-on state
    std::memset(work_ram, 0, sizeof work_ram);              // memory shares are zero-filled
    if (!vid->init(roms.gfx1.data(), (uint32_t) roms.gfx1.size(), roms.sprites.data(), (uint32_t) roms.sprites.size()))
        { ready_ = false; return; }
    snd.power_on();
    analog_bank = 0;                                        // sega_multi32_analog_state::device_start
    eeprom.device_start();
    if (eeprom128) eeprom.load(eeprom128); else eeprom.nvram_default();
    std::memset(comm.m_shared, 0, sizeof comm.m_shared);
    comm.m_linkenable = 0; comm.m_linktimer = 0; comm.m_linkalive = comm.m_linkid = comm.m_linkcount = 0;
    comm.m_zfg = 0;

    // screen device_start: m_vblank_start_time = 0; configure(): the VBLANK begin timer for
    // time_until_vblank_start() and the line 0 timer for time_until_pos(0)
    vblank_start_time = OrTime::zero();
    timer_adjust(&persistent[TK_VBLANK_BEGIN], time_until_pos(224));
    timer_adjust(&persistent[TK_SCANLINE0], time_until_pos(0));

    // hard reset (device_t::reset in device order: the driver, then maincpu, soundcpu,
    // io_chip_0, io_chip_1, eeprom, ..., ymsnd, sega, s32comm)
    std::memset(v60_irq_control, 0xff, sizeof v60_irq_control);   // segas32_state::device_reset
    set_state_synced(LINE_V70_IRQ0, CLEAR_LINE, 0);                // m_maincpu->set_input_line(0, CLEAR_LINE)
    cpu->reset();
    io_chip[0].reset();                                     // CNT2 = 0: the Z80's RESET asserted
    io_chip[1].reset();
    eeprom.device_reset();
    comm.device_reset();
}

// ------------------------------------------------------------------------------------------------
// Running
// ------------------------------------------------------------------------------------------------
void ORBoard::run_frame()
{
    if (!ready_) return;
    uint64_t f = frame_no;
    while (frame_no == f) timeslice();
}

void ORBoard::run_until(const OrTime& t)
{
    if (!ready_) return;
    while (basetime < t) timeslice();
}

// device_scheduler::time()
OrTime ORBoard::time() const
{
    if (in_callback) return callback_expire;
    if (executing_ == EX_V70) return ex[EX_V70].at(ex[EX_V70].cycles + (int64_t)(cpu->total_cycles() - v70_slice_base));
    if (executing_ == EX_Z80) return ex[EX_Z80].at(snd.now_cycles());
    return basetime;
}

// device_scheduler::timeslice
void ORBoard::timeslice()
{
    // loop until we hit the next timer
    while (basetime < (active.empty() ? OrTime::never() : active.front()->expire))
    {
        // by default, assume our target is the end of the next quantum
        OrTime target = basetime + quantum;

        // however, if the next timer is going to fire before then, override
        if (!active.empty() && active.front()->expire < target)
            target = active.front()->expire;

        // do we have pending suspension changes?
        if (suspend_changes_pending)
        {
            uint32_t changed = 0;
            for (Exec& e : ex)
            {
                changed |= e.suspend ^ e.nextsuspend;
                e.suspend = e.nextsuspend;
                e.eatcycles = e.nexteatcycles;
            }
            if (changed == 0) suspend_changes_pending = false;
        }

        // loop over all CPUs (rebuild_execute_list: the running ones in device order, then the
        // suspended ones; the V70 is never suspended, so the order is always V70, Z80)
        for (int i = 0; i < 2; i++)
        {
            Exec& e = ex[i];
            OrTime lt = e.local();
            if ((e.suspend == 0 || e.eatcycles) && target.s >= lt.s)
            {
                // compute how many attoseconds to execute this CPU
                int64_t delta = target.a - lt.a;
                if (delta < 0 && target.s > lt.s)
                    delta += OrTime::ATTO;

                // if we have enough for at least 1 cycle, do the math
                if (delta >= e.apc)
                {
                    // compute how many cycles we want to execute
                    int ran = (int)(((uint64_t) delta >> e.divshift) / e.divisor);

                    // if we're not suspended, actually execute
                    if (e.suspend == 0)
                    {
                        executing_ = i;
                        if (i == EX_V70)
                        {
                            v70_slice_base = cpu->total_cycles();
                            ran = cpu->run(ran);
                        }
                        else
                            ran = snd.execute(ran);
                        executing_ = EX_NONE;
                    }
                    else if (i == EX_Z80)
                        snd.advance(ran);

                    // account for these cycles
                    e.cycles += ran;

                    // if the new local CPU time is less than our target, move the target up, but not before the base
                    OrTime nl = e.local();
                    if (nl < target)
                        target = std::max(nl, basetime);
                }
            }
        }

        // update the base time
        basetime = target;
    }

    // execute timers
    execute_timers();
}

void ORBoard::abort_timeslice()
{
    if (executing_ == EX_V70) cpu->abort_timeslice();
    else if (executing_ == EX_Z80) snd.abort_execute();
}

// ------------------------------------------------------------------------------------------------
// Timers (emu_timer and the device_scheduler's timer list)
// ------------------------------------------------------------------------------------------------
ORBoard::Timer* ORBoard::timer_alloc_temp()
{
    if (free_timers.empty())
    {
        pool.emplace_back(new Timer());
        free_timers.push_back(pool.back().get());
    }
    Timer* t = free_timers.back();
    free_timers.pop_back();
    *t = Timer();
    return t;
}

void ORBoard::timer_list_insert(Timer* t)
{
    // disabled timers never expire (MAME keeps them in a separate list)
    if (t->expire.is_never() || !t->enabled) return;
    // an entry that expires after us is where we go: after any equal expiry
    auto it = active.begin();
    while (it != active.end() && !((*it)->expire > t->expire)) ++it;
    active.insert(it, t);
}

void ORBoard::timer_list_remove(Timer* t)
{
    auto it = std::find(active.begin(), active.end(), t);
    if (it != active.end()) active.erase(it);
}

// emu_timer::adjust
void ORBoard::timer_adjust(Timer* t, OrTime delay, int param, OrTime period)
{
    // if this is the callback timer, mark it modified
    if (in_callback && callback_timer == t)
        callback_timer_modified = true;

    // compute the time of the next firing and insert into the list
    t->param = param;
    t->enabled = true;

    // clamp negative times to 0
    if (delay.s < 0)
        delay = OrTime::zero();

    // set the start and expire times
    t->start = time();
    t->expire = t->start + delay;
    t->period = period;

    // remove and re-insert the timer in its new order
    timer_list_remove(t);
    timer_list_insert(t);

    // if this was inserted as the head, abort the current timeslice and resync
    if (!active.empty() && active.front() == t)
        abort_timeslice();
}

// emu_timer::enable
void ORBoard::timer_enable(Timer* t, bool enable)
{
    if (t->enabled != enable)
    {
        t->enabled = enable;
        timer_list_remove(t);
        timer_list_insert(t);
    }
}

// device_scheduler::synchronize: a temporary timer at the current time
void ORBoard::synchronize(int kind, int param, int64_t aux)
{
    Timer* t = timer_alloc_temp();
    t->kind = kind; t->param = param; t->aux = aux;
    t->temporary = true;
    t->period = OrTime::never();
    t->start = time();
    t->expire = t->start;
    t->enabled = true;
    timer_list_insert(t);
    if (!active.empty() && active.front() == t)
        abort_timeslice();
}

// device_scheduler::execute_timers
void ORBoard::execute_timers()
{
    // now process any timers that are overdue
    while (!active.empty() && active.front()->expire <= basetime)
    {
        // if this is a one-shot timer, disable it now
        Timer* t = active.front();
        bool was_enabled = t->enabled;
        if ((t->period.s == 0 && t->period.a == 0) || t->period.is_never())
            t->enabled = false;

        // set the global state of which callback we're in
        callback_timer_modified = false;
        callback_timer = t;
        callback_expire = t->expire;
        in_callback = true;

        // call the callback
        if (was_enabled)
            fire(t);

        // reset or remove the timer, but only if it wasn't modified during the callback
        if (!callback_timer_modified)
        {
            if (!t->temporary)
            {
                // schedule_next_period
                t->start = t->expire;
                t->expire = t->expire + t->period;
                timer_list_remove(t);
                timer_list_insert(t);
            }
            else
            {
                timer_list_remove(t);
                free_timers.push_back(t);
            }
        }
    }

    // clear the callback timer global
    callback_timer = nullptr;
    in_callback = false;
}

void ORBoard::fire(Timer* t)
{
    switch (t->kind)
    {
        case TK_VBLANK_BEGIN:
        {
            // screen_device::vblank_begin (the primary screen)
            vblank_start_time = time();

            // video_manager::frame_update: finish_screen_updates (both screens, in device
            // order), the Lua frame_done callbacks, then the input ports' frame update
            if (draw_mask & 1) vid->screen_update(0);
            if (draw_mask & 2) vid->screen_update(1);
            frame_no++;
            if (on_frame) on_frame(frame_no);

            // the screen's VBLANK callbacks: the V70's VBLANK interrupt (on_vblank)
            start_of_vblank_int();

            // reset the VBLANK start timer for the next frame
            timer_adjust(t, time_until_pos(224));
            // PORT: the screen's own VBLANK end timer, the second screen's timers and the
            // second screen's screen updates' configure() calls fire at the same instants as
            // timers modelled here and change nothing the emulation sees; they are not modelled
            break;
        }
        case TK_SCANLINE0:
            // screen_device::first_scanline_tick -> reset_partial_updates
            timer_adjust(t, time_until_pos(0));
            break;
        case TK_VBLANK_END_INT:
            end_of_vblank_int();
            break;
        case TK_SPRITES:
            vid->update_sprites();
            break;
        case TK_V60_IRQ0:
        case TK_V60_IRQ1:
            signal_v60_irq(t->param);                       // signal_v60_irq_callback
            break;
        case TK_YM0:
        case TK_YM1:
            snd.timer_expired(t->kind - TK_YM0, t->aux);    // ym_generic_device::fm_timer_handler
            break;
        case TK_SOUND_UPDATE:
            // sound_manager::update: the streams are brought up to date; that changes nothing
            // the emulation sees (the board generates them when a chip is written)
            break;
        case TK_SYNC_INPUT:
            empty_event_queue(t->param);
            break;
        case TK_SYNC_YM:
            snd.sync_event(t->param & 0xff, (uint8_t)(t->param >> 8), t->aux);
            break;
    }
}

// ------------------------------------------------------------------------------------------------
// Screen timing (screen_device::time_until_pos; time() as the scheduler's)
// ------------------------------------------------------------------------------------------------
OrTime ORBoard::time_until_pos(int vpos, int hpos) const
{
    // since we measure time relative to VBLANK, compute the scanline offset from VBLANK
    vpos += 262 - 224;
    vpos %= 262;

    // compute the delta for the given X,Y position
    int64_t targetdelta = (int64_t) vpos * scantime + (int64_t) hpos * pixeltime;

    // if we're past that time (within 1/2 of a pixel), head to the next frame
    int64_t curdelta = (time() - vblank_start_time).as_attoseconds();
    if (targetdelta <= curdelta + pixeltime / 2)
        targetdelta += frame_period;
    while (targetdelta <= curdelta)
        targetdelta += frame_period;

    // return the difference
    return OrTime(0, targetdelta - curdelta);
}

// ------------------------------------------------------------------------------------------------
// Input lines (device_execute_interface::device_input)
// ------------------------------------------------------------------------------------------------
void ORBoard::set_state_synced(int line, int state, int vector)
{
    InputQueue& q = lines[line];

    // if we're full of events, flush the queue and log a message
    int event_index = q.qindex++;
    if (event_index >= 32)
    {
        q.qindex--;
        empty_event_queue(line);
        event_index = q.qindex++;
    }

    // enqueue the event
    q.queue[event_index] = (state & 0xff) | (vector << 8);

    // if this is the first one, set the timer
    if (event_index == 0)
        synchronize(TK_SYNC_INPUT, line, 0);
}

void ORBoard::empty_event_queue(int line)
{
    InputQueue& q = lines[line];
    for (int curevent = 0; curevent < q.qindex; curevent++)
    {
        int32_t input_event = q.queue[curevent];
        int state = input_event & 0xff;
        int vector = input_event >> 8;
        switch (line)
        {
            case LINE_V70_IRQ0:
                cpu->set_input_line_and_vector(0, state, vector);   // execute_set_input (v60_try_irq)
                break;
            case LINE_Z80_IRQ0:
                snd.set_irq_line(state == CLEAR_LINE ? 0 : 1);
                break;
            case LINE_Z80_RESET:
                // if we're asserting the line, just halt the device
                if (state == ASSERT_LINE)
                    suspend_z80(true);
                // if we're clearing the line that was previously asserted, reset the device
                else if (ex[EX_Z80].nextsuspend & SUSPEND_REASON_RESET)
                {
                    snd.reset_cpu();                        // device_t::reset (z80_device::device_reset)
                    suspend_z80(false);                     // interface_pre_reset resume(ANY), resume(RESET)
                }
                break;
        }
    }
    q.qindex = 0;
}

// device_execute_interface::suspend(SUSPEND_REASON_RESET, true) / resume
void ORBoard::suspend_z80(bool on)
{
    Exec& z = ex[EX_Z80];
    if (on) { z.nextsuspend |= SUSPEND_REASON_RESET; z.nexteatcycles = true; }
    else z.nextsuspend = 0;
    suspend_changes_pending = true;                         // suspend_resume_changed
    if (executing_ == EX_Z80) snd.abort_execute();
}

// ------------------------------------------------------------------------------------------------
// segas32.cpp: interrupt controller
// ------------------------------------------------------------------------------------------------
void ORBoard::update_irq_state()
{
    uint8_t effirq = v60_irq_control[7] & ~v60_irq_control[6] & 0x1f;
    int vector;

    // loop over interrupt vectors, finding the highest priority one with an unmasked interrupt pending
    for (vector = 0; vector < 5; vector++)
        if (effirq & (1 << vector))
        {
            set_state_synced(LINE_V70_IRQ0, ASSERT_LINE, vector);
            break;
        }

    // if we didn't find any, clear the interrupt line (the stored vector: 0)
    if (vector == 5)
        set_state_synced(LINE_V70_IRQ0, CLEAR_LINE, 0);
}

void ORBoard::signal_v60_irq(int which)
{
    // see if this interrupt input is mapped to any vectors; if so, mark them
    for (int i = 0; i < 5; i++)
        if (v60_irq_control[i] == which)
            v60_irq_control[7] |= 1 << i;
    update_irq_state();
}

uint8_t ORBoard::int_control_r(uint32_t)
{
    // return all F's for everything except timer values (which are not emulated either)
    return 0xff;
}

void ORBoard::int_control_w(uint32_t offset, uint8_t data)
{
    int duration;
    switch (offset)
    {
        case 0: case 1: case 2: case 3: case 4:             // vectors
            v60_irq_control[offset] = data;
            break;

        case 5:                                             // unknown
            v60_irq_control[offset] = data;
            break;

        case 6:                                             // mask
            v60_irq_control[offset] = data;
            update_irq_state();
            break;

        case 7:                                             // acknowledge
            v60_irq_control[offset] &= data;
            update_irq_state();
            break;

        case 8: case 9:                                     // timer 0 count
            v60_irq_control[offset] = data;
            duration = v60_irq_control[8] + ((v60_irq_control[9] << 8) & 0xf00);
            if (duration)
                timer_adjust(&persistent[TK_V60_IRQ0], OrTime(0, TIMER_0_ATTOS) * (uint32_t) duration, MAIN_IRQ_TIMER0);
            break;

        case 10: case 11:                                   // timer 1 count
            v60_irq_control[offset] = data;
            duration = v60_irq_control[10] + ((v60_irq_control[11] << 8) & 0xf00);
            if (duration)
                timer_adjust(&persistent[TK_V60_IRQ1], OrTime(0, TIMER_1_ATTOS) * (uint32_t) duration, MAIN_IRQ_TIMER1);
            break;

        case 12: case 13: case 14: case 15:                 // signal IRQ to sound CPU
            snd.main_signal_irq();
            break;
    }
}

// INTERRUPT_GEN start_of_vblank_int
void ORBoard::start_of_vblank_int()
{
    signal_v60_irq(MAIN_IRQ_VBSTART);
    vid->system32_set_vblank(1);
    timer_adjust(&persistent[TK_VBLANK_END_INT], time_until_pos(0));
    comm.check_vint_irq();                                  // m_system32_prot_vblank is null
}

// TIMER_CALLBACK end_of_vblank_int
void ORBoard::end_of_vblank_int()
{
    signal_v60_irq(MAIN_IRQ_VBSTOP);
    if (vid->system32_set_vblank(0))
        timer_adjust(&persistent[TK_SPRITES], OrTime::from_usec(50), 1);
}

// running_machine::rand
uint32_t ORBoard::machine_rand()
{
    rand_calls++;
    rand_seed = 1664525 * rand_seed + 1013904223;
    // return rotated by 16 bits; the low bits have a short period and are frequently used
    return (rand_seed >> 16) | (rand_seed << 16);
}

// ------------------------------------------------------------------------------------------------
// The I/O chips' connections (sega_multi32_state::device_add_mconfig)
// ------------------------------------------------------------------------------------------------
uint8_t ORBoard::io_in_port(int chip, int port)
{
    switch (port)
    {
        case 0: return chip ? in.p1_b : in.p1_a;
        case 1: return chip ? in.p2_b : in.p2_a;
        case 2: return chip ? in.portc_b : in.portc_a;
        case 4: return chip ? in.service12_b : in.service12_a;
        // SERVICE34_A / SERVICE34_B: bit 7 is the EEPROM's DO (PORT_READ_LINE_DEVICE_MEMBER, active high)
        case 5: return (uint8_t)(((chip ? in.service34_b : in.service34_a) & 0x7f) | (eeprom.do_read() ? 0x80 : 0));
        default: return 0xff;                               // no callback: devcb's default 0xff
    }
}

void ORBoard::io_out_port(int chip, int port, uint8_t data)
{
    switch (port)
    {
        case 3: lamps_d[chip] = data; break;                // misc_output_0_w / _1_w (coin counters, orunners_sw1_output)
        case 6: lamps_g[chip] = data; break;                // sw2_output_0_w / _1_w (orunners_sw2_output)
        case 7:
            if (chip == 0)
                vid->tilebank_external_w(data);
            else
            {
                // io_chip_1 out_ph: eeprom di (bit 7), cs (bit 5), clk (bit 6), in that order
                eeprom.di_write(data >> 7 & 1);
                eeprom.cs_write(data >> 5 & 1);
                eeprom.clk_write(data >> 6 & 1);
            }
            break;
        default: break;
    }
}

void ORBoard::io_out_cnt(int chip, int cnt, int state)
{
    if (cnt == 1)
        vid->display_enable_w(chip, state);                 // display_enable_w<0> / <1>
    else if (cnt == 2 && chip == 0)
        set_state_synced(LINE_Z80_RESET, state ? CLEAR_LINE : ASSERT_LINE, 0);   // set_inputline(soundcpu, RESET).invert()
}

// MSM6253 inputs: ANALOG1, ANALOG2, in2_analog_read (ANALOG3 / ANALOG7), in3_analog_read
// (ANALOG4 / ANALOG8) by the analog bank
uint32_t ORBoard::analog_read(int channel)
{
    switch (channel)
    {
        case 0: return in.analog[0];
        case 1: return in.analog[1];
        case 2: return in.analog[analog_bank * 4 + 2];
        default: return in.analog[analog_bank * 4 + 3];
    }
}

// ------------------------------------------------------------------------------------------------
// Sound board hookup (ORSound::Host)
// ------------------------------------------------------------------------------------------------
void ORBoard::ym_timer(int tnum, int64_t expire)
{
    Timer* t = &persistent[TK_YM0 + tnum];
    if (expire < 0)
        timer_enable(t, false);
    else
    {
        // adjust(attotime::from_ticks(clocks, clock()), tnum) from the Z80's time
        t->aux = expire;
        timer_adjust(t, ex[EX_Z80].at(expire) - time(), tnum);
    }
}

void ORBoard::ym_sync(int kind, uint8_t data)
{
    synchronize(TK_SYNC_YM, kind | data << 8, snd.now_cycles());
}

void ORBoard::irq_line(int state, int vector)
{
    // update_sound_irq_state: set_input_line_and_vector(0, ASSERT_LINE, 2 * vector) or
    // set_input_line(0, CLEAR_LINE) (the stored vector: 0)
    set_state_synced(LINE_Z80_IRQ0, state ? ASSERT_LINE : CLEAR_LINE, state ? vector : 0);
}

// ------------------------------------------------------------------------------------------------
// Memory map (multi32_analog_map). A 16-bit handler is called once per 16-bit half the
// mem_mask selects, an 8-bit one once per selected byte lane, in address order (emumem's
// handler_entry_read_units / _write_units); lanes no handler covers read as $FF (unmap value
// high); covered lanes that are not accessed read as 0.
// ------------------------------------------------------------------------------------------------
uint8_t ORBoard::read8(uint32_t a) const
{
    a &= 0xffffff;
    if (a < 0x200000) return roms.maincpu[a];
    if (a < 0x300000) return work_ram[a & 0x1ffff];
    if (a >= 0x700000 && a < 0x800000) return snd.shared[a & 0x1fff];
    if (a >= 0xf00000) return roms.maincpu[a - 0xf00000];
    return 0xff;
}

void ORBoard::set_pc_hooks(const uint32_t* pcs, int n, PcHook fn, void* ctx)
{
    hook_map.assign(0x100000, 0);
    hook_fn = (fn && n > 0) ? fn : nullptr;
    hook_ctx = ctx;
    if (hook_fn)
        for (int i = 0; i < n; i++)
            if (pcs[i] < 0x100000) hook_map[pcs[i]] = 1;
    cpu->set_instruction_hook(hook_fn != nullptr);
}

void ORBoard::debug_instruction_hook(uint32_t pc)
{
    if (hook_fn && pc < 0x100000 && hook_map[pc]) hook_fn(hook_ctx, pc);
}

void ORBoard::write8(uint32_t a, uint8_t v)
{
    a &= 0xffffff;
    if (a >= 0x200000 && a < 0x300000) work_ram[a & 0x1ffff] = v;
    else if (a >= 0x700000 && a < 0x800000) snd.shared[a & 0x1fff] = v;
}

uint32_t ORBoard::read32(uint32_t addr, uint32_t mask)
{
    uint32_t a = addr & 0xfffffc;
    uint32_t r = 0xffffffff;
    switch (a >> 20)
    {
        case 0x0: case 0x1: case 0xf:                       // ROM (direct pages; reached by the debugger only)
        case 0x2:                                           // work RAM (direct)
            r = (uint32_t) read8(a) | (uint32_t) read8(a + 1) << 8 | (uint32_t) read8(a + 2) << 16 | (uint32_t) read8(a + 3) << 24;
            break;
        case 0x3:                                           // videoram_r
        {
            uint32_t off = (a & 0x1ffff) >> 1;
            r = 0;
            if (mask & 0x0000ffff) r |= vid->videoram_r(off);
            if (mask & 0xffff0000) r |= (uint32_t) vid->videoram_r(off + 1) << 16;
            break;
        }
        case 0x4:                                           // spriteram_r
        {
            uint32_t off = (a & 0x1ffff) >> 1;
            r = 0;
            if (mask & 0x0000ffff) r |= vid->spriteram_r(off);
            if (mask & 0xffff0000) r |= (uint32_t) vid->spriteram_r(off + 1) << 16;
            break;
        }
        case 0x5:                                           // sprite_control_r, umask32($00FF00FF)
        {
            uint32_t off = (a & 0xf) >> 1;
            r = 0xff00ff00;
            if (mask & 0x000000ff) r |= vid->sprite_control_r(off);
            if (mask & 0x00ff0000) r |= (uint32_t) vid->sprite_control_r(off + 1) << 16;
            break;
        }
        case 0x6:                                           // palette / mixer 0 ($6x0000) and 1 ($6x8000)
        {
            int which = (a >> 19) & 1;
            r = 0;
            if (a & 0x10000)
            {
                uint32_t off = (a & 0x7f) >> 1;
                if (mask & 0x0000ffff) r |= vid->mixer_r(which, off);
                if (mask & 0xffff0000) r |= (uint32_t) vid->mixer_r(which, off + 1) << 16;
            }
            else
            {
                uint32_t off = (a & 0xffff) >> 1;
                if (mask & 0x0000ffff) r |= vid->paletteram_r(which, off);
                if (mask & 0xffff0000) r |= (uint32_t) vid->paletteram_r(which, off + 1) << 16;
            }
            break;
        }
        case 0x7:                                           // shared_ram_r (8-bit, every lane)
        {
            uint32_t off = a & 0x1fff;
            r = 0;
            for (int lane = 0; lane < 4; lane++)
                if (mask >> (8 * lane) & 0xff) r |= (uint32_t) snd.shared[off + (uint32_t) lane] << (8 * lane);
            break;
        }
        case 0x8:
            if (a < 0x801000)                               // s32comm share_r, umask32($00FF00FF)
            {
                uint32_t off = (a & 0xfff) >> 1;
                r = 0xff00ff00;
                if (mask & 0x000000ff) r |= comm.share_r(off);
                if (mask & 0x00ff0000) r |= (uint32_t) comm.share_r(off + 1) << 16;
            }
            else if (a == 0x801000)                         // cn_r ($801000) and fg_r ($801002)
            {
                r = 0xff00ff00;
                if (mask & 0x000000ff) r |= comm.cn_r();
                if (mask & 0x00ff0000) r |= (uint32_t) comm.fg_r() << 16;
            }
            if (io_tap && a < 0x802000) io_tap(false, a, r, mask, time());
            break;
        case 0xc:
        {
            uint32_t reg = a & 0x7f;
            if (reg < 0x20)                                 // io_chip_0 / io_chip_1, umask32($00FF00FF)
            {
                Sega315_5296& chip = io_chip[(a >> 19) & 1];
                uint32_t off = reg >> 1;
                r = 0xff00ff00;
                if (mask & 0x000000ff) r |= chip.read(off);
                if (mask & 0x00ff0000) r |= (uint32_t) chip.read(off + 1) << 16;
            }
            else if (a < 0xc80000 && reg >= 0x50 && reg < 0x58)   // adc d7_r, umask32($00FF00FF)
            {
                r = 0xff00ff00;
                if (mask & 0x000000ff) r |= adc.d7_r(0xff);
                if (mask & 0x00ff0000) r |= (uint32_t) adc.d7_r(0xff) << 16;
            }
            if (io_tap) io_tap(false, a, r, mask, time());
            break;
        }
        case 0xd:
            if (a < 0xd80000)                               // int_control_r (8-bit, every lane)
            {
                r = 0;
                for (int lane = 0; lane < 4; lane++)
                    if (mask >> (8 * lane) & 0xff) r |= (uint32_t) int_control_r((a & 0xc) + (uint32_t) lane) << (8 * lane);
            }
            else                                            // random_number_r (16-bit)
            {
                r = 0;
                if (mask & 0x0000ffff) r |= machine_rand() & 0xffff;
                if (mask & 0xffff0000) r |= (machine_rand() & 0xffff) << 16;
                if (io_tap) io_tap(false, a, r, mask, time());
            }
            break;
        default:
            break;                                          // unmapped
    }
    return r;
}

void ORBoard::write32(uint32_t addr, uint32_t data, uint32_t mask)
{
    uint32_t a = addr & 0xfffffc;
    uint16_t lo = (uint16_t) data, hi = (uint16_t)(data >> 16), mlo = (uint16_t) mask, mhi = (uint16_t)(mask >> 16);
    switch (a >> 20)
    {
        case 0x2:                                           // work RAM (direct; not reached)
            for (int lane = 0; lane < 4; lane++)
                if (mask >> (8 * lane) & 0xff) work_ram[(a & 0x1ffff) + (uint32_t) lane] = (uint8_t)(data >> (8 * lane));
            break;
        case 0x3:
        {
            uint32_t off = (a & 0x1ffff) >> 1;
            if (mlo) vid->videoram_w(off, lo, mlo);
            if (mhi) vid->videoram_w(off + 1, hi, mhi);
            break;
        }
        case 0x4:
        {
            uint32_t off = (a & 0x1ffff) >> 1;
            if (mlo) vid->spriteram_w(off, lo, mlo);
            if (mhi) vid->spriteram_w(off + 1, hi, mhi);
            break;
        }
        case 0x5:
        {
            uint32_t off = (a & 0xf) >> 1;
            if (mask & 0x000000ff) vid->sprite_control_w(off, (uint8_t) data);
            if (mask & 0x00ff0000) vid->sprite_control_w(off + 1, (uint8_t)(data >> 16));
            break;
        }
        case 0x6:
        {
            int which = (a >> 19) & 1;
            if (a & 0x10000)
            {
                uint32_t off = (a & 0x7f) >> 1;
                if (mlo) vid->mixer_w(which, off, lo, mlo);
                if (mhi) vid->mixer_w(which, off + 1, hi, mhi);
            }
            else
            {
                uint32_t off = (a & 0xffff) >> 1;
                if (mlo) vid->paletteram_w(which, off, lo, mlo);
                if (mhi) vid->paletteram_w(which, off + 1, hi, mhi);
            }
            break;
        }
        case 0x7:
        {
            uint32_t off = a & 0x1fff;
            for (int lane = 0; lane < 4; lane++)
                if (mask >> (8 * lane) & 0xff) snd.shared[off + (uint32_t) lane] = (uint8_t)(data >> (8 * lane));
            break;
        }
        case 0x8:
            if (io_tap && a < 0x802000) io_tap(true, a, data, mask, time());
            if (a < 0x801000)
            {
                uint32_t off = (a & 0xfff) >> 1;
                if (mask & 0x000000ff) comm.share_w(off, (uint8_t) data);
                if (mask & 0x00ff0000) comm.share_w(off + 1, (uint8_t)(data >> 16));
            }
            else if (a == 0x801000)
            {
                if (mask & 0x000000ff) comm.cn_w((uint8_t) data);
                if (mask & 0x00ff0000) comm.fg_w((uint8_t)(data >> 16));
            }
            break;
        case 0xc:
        {
            if (io_tap) io_tap(true, a, data, mask, time());
            uint32_t reg = a & 0x7f;
            if (reg < 0x20)
            {
                Sega315_5296& chip = io_chip[(a >> 19) & 1];
                uint32_t off = reg >> 1;
                if (mask & 0x000000ff) chip.write(off, (uint8_t) data);
                if (mask & 0x00ff0000) chip.write(off + 1, (uint8_t)(data >> 16));
            }
            else if (a < 0xc80000 && reg >= 0x50 && reg < 0x58)   // adc address_w
            {
                uint32_t off = (reg - 0x50) >> 1;
                if (mask & 0x000000ff) adc.address_w(off, (uint8_t) data);
                if (mask & 0x00ff0000) adc.address_w(off + 1, (uint8_t)(data >> 16));
            }
            else if (a < 0xc80000 && reg == 0x60)           // analog_bank_w ($C00060, lane 0)
            {
                if (mask & 0x000000ff) analog_bank = data & 1;
            }
            break;
        }
        case 0xd:
            if (a < 0xd80000)
            {
                if (io_tap) io_tap(true, a, data, mask, time());
                for (int lane = 0; lane < 4; lane++)
                    if (mask >> (8 * lane) & 0xff) int_control_w((a & 0xc) + (uint32_t) lane, (uint8_t)(data >> (8 * lane)));
            }
            // random_number_w: nothing
            break;
        default:
            break;                                          // ROM, unmapped
    }
}
