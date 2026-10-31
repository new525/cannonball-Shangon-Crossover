/***************************************************************************
    Devices of Sega's System Multi 32 board (OutRunners) other than the CPUs, the video and the
    sound: transcribed from MAME 0.264, one class per MAME device, each under its own MAME
    license and copyright holders (all BSD-3-Clause; see the block above each class and
    s32io.LICENSE).

      OrTime        MAME's attotime (src/emu/attotime.h/.cpp, license:BSD-3-Clause,
                    copyright-holders:Aaron Giles)
      Sega315_5296  Sega 315-5296 I/O chip (src/mame/sega/315_5296.cpp, license:BSD-3-Clause,
                    copyright-holders:hap)
      Msm6253       OKI MSM6253 A/D converter (src/devices/machine/msm6253.cpp,
                    license:BSD-3-Clause, copyright-holders:AJR)
      Eeprom93C46   93C46 serial EEPROM, 64 x 16 bits (src/devices/machine/eeprom.cpp and
                    eepromser.cpp, license:BSD-3-Clause, copyright-holders:Aaron Giles)
      S32Comm       System 32 / Multi 32 communication board, MAME's S32COMM_SIMULATION build
                    (src/mame/sega/s32comm.cpp, license:BSD-3-Clause,
                    copyright-holders:Ariane Fugmann)

    MAME's device framework is replaced by plain callbacks (std::function) and a clock
    callback (the scheduler's time()). Lines marked "PORT:" differ from MAME.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

// ------------------------------------------------------------------------------------------------
// OrTime: MAME's attotime (license:BSD-3-Clause, copyright-holders:Aaron Giles)
// ------------------------------------------------------------------------------------------------
struct OrTime
{
    static constexpr int64_t ATTO = 1000000000000000000LL;     // ATTOSECONDS_PER_SECOND
    static constexpr int64_t MAX_SECONDS = 1000000000;          // ATTOTIME_MAX_SECONDS
    int64_t s = 0, a = 0;

    constexpr OrTime() {}
    constexpr OrTime(int64_t sec, int64_t atto) : s(sec), a(atto) {}
    static constexpr OrTime never() { return OrTime(MAX_SECONDS, 0); }
    static constexpr OrTime zero() { return OrTime(0, 0); }
    bool is_never() const { return s >= MAX_SECONDS; }

    friend OrTime operator+(const OrTime& l, const OrTime& r)
    {
        if (l.s >= MAX_SECONDS || r.s >= MAX_SECONDS) return never();
        OrTime t(l.s + r.s, l.a + r.a);
        if (t.a >= ATTO) { t.a -= ATTO; t.s++; }
        if (t.s >= MAX_SECONDS) return never();
        return t;
    }
    friend OrTime operator-(const OrTime& l, const OrTime& r)
    {
        if (l.s >= MAX_SECONDS) return never();
        OrTime t(l.s - r.s, l.a - r.a);
        if (t.a < 0) { t.a += ATTO; t.s--; }
        return t;
    }
    OrTime& operator+=(const OrTime& r) { return *this = *this + r; }
    // attotime::operator*(u32): the exact product, normalised
    friend OrTime operator*(const OrTime& l, uint32_t factor)
    {
        if (l.s >= MAX_SECONDS) return never();
        if (factor == 0) return zero();
        __int128 at = (__int128) l.a * factor;
        __int128 sec = (__int128) l.s * factor + at / ATTO;
        if (sec >= MAX_SECONDS) return never();
        return OrTime((int64_t) sec, (int64_t)(at % ATTO));
    }
    bool operator<(const OrTime& o) const  { return s < o.s || (s == o.s && a < o.a); }
    bool operator>(const OrTime& o) const  { return o < *this; }
    bool operator<=(const OrTime& o) const { return !(o < *this); }
    bool operator>=(const OrTime& o) const { return !(*this < o); }
    bool operator==(const OrTime& o) const { return s == o.s && a == o.a; }
    bool operator!=(const OrTime& o) const { return !(*this == o); }
    int64_t as_attoseconds() const { return s * ATTO + a; }        // only for spans under 9 s
    double sec() const { return (double) s + (double) a * 1e-18; }

    static constexpr int64_t hz_to_attoseconds(int64_t hz) { return ATTO / hz; }   // HZ_TO_ATTOSECONDS
    static OrTime from_hz(int64_t hz) { return hz > 1 ? OrTime(0, ATTO / hz) : hz == 1 ? OrTime(1, 0) : never(); }
    static OrTime from_usec(int64_t us) { return OrTime(us / 1000000, (us % 1000000) * (ATTO / 1000000)); }
    // attotime::from_ticks(ticks, frequency)
    static OrTime from_ticks(uint64_t ticks, uint32_t frequency)
    {
        if (frequency == 0) return never();
        int64_t apc = ATTO / frequency;
        if (ticks < frequency) return OrTime(0, (int64_t) ticks * apc);
        return OrTime((int64_t)(ticks / frequency), (int64_t)(ticks % frequency) * apc);
    }
};

// ------------------------------------------------------------------------------------------------
// Sega 315-5296 I/O chip
// license:BSD-3-Clause, copyright-holders:hap, thanks-to:Charles MacDonald
// (MAME 0.264 src/mame/sega/315_5296.cpp)
//
// 8 bidirectional ports (A-H), the 'SEGA' protection bytes, the CNT register (three output
// pins) and the port direction register. 64 addresses; the System 32 maps use 16.
// ------------------------------------------------------------------------------------------------
class Sega315_5296
{
public:
    // devcb: in_port_cb[8] (unset: 0xff), out_port_cb[8], out_cnt_cb[3]
    std::function<uint8_t(int port)> in_port;            // PORT: one callback for the 8 ports; nullptr = 0xff
    std::function<void(int port, uint8_t data)> out_port;
    std::function<void(int cnt, int state)> out_cnt;

    void reset();                                        // device_reset
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t data);

    uint8_t m_output_latch[8] = {};
    uint8_t m_cnt = 0;
    uint8_t m_dir = 0;
    uint8_t m_dir_override = 0xff;

private:
    uint8_t in(int port) { return in_port ? in_port(port) : 0xff; }
    void out(int port, uint8_t data) { if (out_port) out_port(port, data); }
};

// ------------------------------------------------------------------------------------------------
// OKI MSM6253 8-bit 4-channel A/D converter
// license:BSD-3-Clause, copyright-holders:AJR (MAME 0.264 src/devices/machine/msm6253.cpp)
// ------------------------------------------------------------------------------------------------
class Msm6253
{
public:
    // m_analog_input_cb[4]: the value of each input (MAME: an ioport read or a driver callback)
    std::function<uint32_t(int channel)> input;

    void address_w(uint32_t offset, uint8_t data);       // fill the shift register from channel offset & 3
    void select_w(uint32_t offset, uint8_t data);        // ... from channel data & 3
    uint8_t d0_r(uint8_t unmap);                         // shift out to D0; unmap = the space's unmap value
    uint8_t d7_r(uint8_t unmap);                         // shift out to D7

    uint8_t m_shift_register = 0;

private:
    bool shift_out();
};

// ------------------------------------------------------------------------------------------------
// 93C46 serial EEPROM, 64 cells of 16 bits (MAME's EEPROM_93C46_16BIT: eeprom_serial_93cxx_device
// with streaming disabled, 6 command address bits, write/erase timing of eeprom_base_device)
// license:BSD-3-Clause, copyright-holders:Aaron Giles
// (MAME 0.264 src/devices/machine/eeprom.cpp, eepromser.cpp)
// ------------------------------------------------------------------------------------------------
class Eeprom93C46
{
public:
    std::function<OrTime()> now;                         // machine().time()

    void nvram_default();                                // every cell $FFFF (no default data or region)
    void load(const uint8_t* data128);                   // nvram_read: 128 bytes, 16-bit little-endian cells
    void save(uint8_t* data128) const;                   // nvram_write
    void device_start();
    void device_reset();

    // eeprom_serial_93cxx_device
    int  do_read();                                      // combined DO + READY/BUSY
    void cs_write(int state);
    void clk_write(int state);
    void di_write(int state);

    enum eeprom_state { STATE_IN_RESET, STATE_WAIT_FOR_START_BIT, STATE_WAIT_FOR_COMMAND, STATE_READING_DATA,
                        STATE_WAIT_FOR_DATA, STATE_WAIT_FOR_COMPLETION };
    enum eeprom_command { COMMAND_INVALID, COMMAND_READ, COMMAND_WRITE, COMMAND_ERASE, COMMAND_LOCK, COMMAND_UNLOCK,
                          COMMAND_WRITEALL, COMMAND_ERASEALL };
    enum eeprom_event { EVENT_CS_RISING_EDGE = 1 << 0, EVENT_CS_FALLING_EDGE = 1 << 1, EVENT_CLK_RISING_EDGE = 1 << 2,
                        EVENT_CLK_FALLING_EDGE = 1 << 3 };
    enum { WRITE_TIME, WRITE_ALL_TIME, ERASE_TIME, ERASE_ALL_TIME, TIMING_COUNT };

    // state (MAME's members)
    uint8_t  m_data[128] = {};
    OrTime   m_completion_time;
    eeprom_state m_state = STATE_IN_RESET;
    int      m_cs_state = 0;
    OrTime   m_last_cs_rising_edge_time;
    int      m_clk_state = 0;
    int      m_di_state = 0;
    bool     m_locked = true;
    uint32_t m_bits_accum = 0;
    uint32_t m_command_address_accum = 0;
    eeprom_command m_command = COMMAND_INVALID;
    uint32_t m_address = 0;
    uint32_t m_shift_register = 0;

private:
    static const int m_address_bits = 6, m_data_bits = 16, m_command_address_bits = 6;
    static const bool m_streaming_enabled = false, m_output_on_falling_clock_enabled = false;
    OrTime m_operation_time[TIMING_COUNT] = { OrTime::from_usec(1750), OrTime::from_usec(8000),
                                              OrTime::from_usec(1000), OrTime::from_usec(8000) };

    bool ready() const { return now() >= m_completion_time; }
    uint32_t read(uint32_t address) { return internal_read(address); }
    void write(uint32_t address, uint32_t data);
    void write_all(uint32_t data);
    void erase(uint32_t address);
    void erase_all();
    uint32_t internal_read(uint32_t address) const;
    void internal_write(uint32_t address, uint32_t data);
    int  base_do_read() const;
    int  base_ready_read() const;
    void set_state(eeprom_state newstate) { m_state = newstate; }   // PORT: m_do_cb is not connected
    void handle_event(eeprom_event event);
    void execute_command();
    void execute_write_command();
    void parse_command_and_address();
};

// ------------------------------------------------------------------------------------------------
// System 32 / Multi 32 communication board (837-8792 on OutRunners), as MAME 0.264 builds it:
// S32COMM_SIMULATION defined (s32comm.h), link type 15033 (init_orunners)
// license:BSD-3-Clause, copyright-holders:Ariane Fugmann (MAME 0.264 src/mame/sega/s32comm.cpp)
//
// PORT: MAME's simulation talks to other MAME instances through sockets (comm_localhost etc.);
// none is ever opened here, as for a cabinet without a link (comm_tick_15033's "link not yet
// established" path with both sockets missing).
// ------------------------------------------------------------------------------------------------
class S32Comm
{
public:
    void device_reset();
    uint8_t share_r(uint32_t offset) { return m_shared[offset & 0x7ff]; }
    void share_w(uint32_t offset, uint8_t data) { m_shared[offset & 0x7ff] = data; }
    uint8_t cn_r() { return m_cn | 0xfe; }
    void cn_w(uint8_t data);
    uint8_t fg_r() { return (uint8_t)(m_fg | (~m_zfg << 7) | 0x7e); }
    void fg_w(uint8_t data) { if (!m_cn) return; m_fg = data & 0x01; }
    void check_vint_irq() { comm_tick(); }

    uint8_t m_shared[0x800] = {};
    uint8_t m_zfg = 0, m_cn = 0, m_fg = 0;
    uint8_t m_linkenable = 0;
    uint16_t m_linktimer = 0;
    uint8_t m_linkalive = 0, m_linkid = 0, m_linkcount = 0;
    uint16_t m_linktype = 15033;

private:
    void comm_tick() { if (m_linktype == 15033) comm_tick_15033(); }
    void comm_tick_15033();
};
