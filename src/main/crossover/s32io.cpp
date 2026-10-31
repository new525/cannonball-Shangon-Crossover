/***************************************************************************
    Devices of Sega's System Multi 32 board. See s32io.hpp for the sources and licenses.
***************************************************************************/
#include "crossover/s32io.hpp"

// ================================================================================================
// Sega 315-5296 I/O chip
// license:BSD-3-Clause, copyright-holders:hap, thanks-to:Charles MacDonald
// (MAME 0.264 src/mame/sega/315_5296.cpp)
// ================================================================================================
void Sega315_5296::reset()
{
    // all ports are set to input
    m_dir = 0;

    // clear output ports
    std::memset(m_output_latch, 0, sizeof(m_output_latch));
    m_cnt = 0;

    for (int i = 0; i < 8; i++)
        out(i, 0);
    for (int i = 0; i < 3; i++)
        if (out_cnt) out_cnt(i, 0);
}

uint8_t Sega315_5296::read(uint32_t offset)
{
    offset &= 0x3f;

    switch (offset)
    {
        // port A to H
        case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7:
            // if the port is configured as an output, return the last thing written
            if ((m_dir & m_dir_override) >> offset & 1)
                return m_output_latch[offset];

            // otherwise, return an input port
            return in((int) offset);

        // 'SEGA' protection
        case 0x8: return 'S';
        case 0x9: return 'E';
        case 0xa: return 'G';
        case 0xb: return 'A';

        // CNT register & mirror
        case 0xc: case 0xe:
            return m_cnt;

        // port direction register & mirror
        case 0xd: case 0xf:
            return m_dir;

        default:
            break;
    }

    return 0xff;
}

void Sega315_5296::write(uint32_t offset, uint8_t data)
{
    offset &= 0x3f;

    switch (offset)
    {
        // port A to H
        case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7:
            // if the port is configured as an output, write it
            if (m_dir >> offset & 1)
                out((int) offset, data);

            m_output_latch[offset] = data;
            break;

        // CNT register
        case 0xe:
            // d0-2: CNT0-2 output pins
            // note: When CNT2 is configured as clock output, bit 2 of this register has
            // no effect on the output level of CNT2.
            for (int i = 0; i < 3; i++)
                if (out_cnt) out_cnt(i, data >> i & 1);

            // d3: CNT2 output mode (1= Clock output, 0= Programmable output)
            // d4,5: CNT2 clock divider (0= CLK/4, 1= CLK/8, 2= CLK/16, 3= CLK/2)
            // d6,7: CKOT clock divider (0= CLK/4, 1= CLK/8, 2= CLK/16, 3= CLK/2)
            // TODO..
            m_cnt = data;
            break;

        // port direction register
        case 0xf:
            // refresh output ports if the direction changed
            for (int i = 0; i < 8; i++)
            {
                if ((m_dir ^ data) >> i & 1)
                    out(i, (data >> i & 1) ? m_output_latch[i] : 0);   // PORT: logerror dropped
            }

            m_dir = data;
            break;

        default:
            break;
    }
}

// ================================================================================================
// OKI MSM6253 A/D converter
// license:BSD-3-Clause, copyright-holders:AJR (MAME 0.264 src/devices/machine/msm6253.cpp)
// ================================================================================================
void Msm6253::address_w(uint32_t offset, uint8_t)
{
    // fill the shift register from the internal A/D latch
    m_shift_register = (uint8_t) input((int)(offset & 3));
}

void Msm6253::select_w(uint32_t, uint8_t data)
{
    // fill the shift register from the internal A/D latch
    m_shift_register = (uint8_t) input(data & 3);
}

bool Msm6253::shift_out()
{
    // capture the shifted bit
    bool msb = m_shift_register >> 7 & 1;

    // shift the bit out, with zero coming in on the other end
    m_shift_register <<= 1;                     // PORT: side_effects_disabled() is never set

    // return the bit
    return msb;
}

uint8_t Msm6253::d0_r(uint8_t unmap)
{
    // offset is ignored
    return (uint8_t)(shift_out() | (unmap & 0xfe));
}

uint8_t Msm6253::d7_r(uint8_t unmap)
{
    // offset is ignored
    return (uint8_t)((shift_out() << 7) | (unmap & 0x7f));
}

// ================================================================================================
// 93C46 serial EEPROM (16-bit cells)
// license:BSD-3-Clause, copyright-holders:Aaron Giles
// (MAME 0.264 src/devices/machine/eeprom.cpp, src/devices/machine/eepromser.cpp)
// ================================================================================================
void Eeprom93C46::write(uint32_t address, uint32_t data)
{
    internal_write(address, data);                                  // PORT: "not ready" logerror dropped
    m_completion_time = now() + m_operation_time[WRITE_TIME];
}

void Eeprom93C46::write_all(uint32_t data)
{
    for (uint32_t address = 0; address < (1u << m_address_bits); address++)
        internal_write(address, internal_read(address) & data);
    m_completion_time = now() + m_operation_time[WRITE_ALL_TIME];
}

void Eeprom93C46::erase(uint32_t address)
{
    internal_write(address, ~0u);
    m_completion_time = now() + m_operation_time[ERASE_TIME];
}

void Eeprom93C46::erase_all()
{
    for (uint32_t address = 0; address < (1u << m_address_bits); address++)
        internal_write(address, ~0u);
    m_completion_time = now() + m_operation_time[ERASE_ALL_TIME];
}

void Eeprom93C46::device_reset()
{
    m_completion_time = OrTime::zero();
}

void Eeprom93C46::nvram_default()
{
    // no default value set, no default data, no region: every cell ~0
    for (uint32_t offs = 0; offs < (1u << m_address_bits); offs++)
        internal_write(offs, ~0u);
}

void Eeprom93C46::load(const uint8_t* data128) { std::memcpy(m_data, data128, sizeof m_data); }
void Eeprom93C46::save(uint8_t* data128) const { std::memcpy(data128, m_data, sizeof m_data); }

uint32_t Eeprom93C46::internal_read(uint32_t address) const
{
    // get_u16le(&m_data[address * 2])
    return (uint32_t) m_data[address * 2] | (uint32_t) m_data[address * 2 + 1] << 8;
}

void Eeprom93C46::internal_write(uint32_t address, uint32_t data)
{
    // put_u16le(&m_data[address * 2], data)
    m_data[address * 2] = (uint8_t) data;
    m_data[address * 2 + 1] = (uint8_t)(data >> 8);
}

// eeprom_serial_base_device::device_start (after eeprom_base_device::device_start)
void Eeprom93C46::device_start()
{
    set_state(STATE_IN_RESET);
    m_locked = true;
    m_bits_accum = 0;
    m_command_address_accum = 0;
    m_command = COMMAND_INVALID;
    m_address = 0;
    m_shift_register = 0;
}

void Eeprom93C46::cs_write(int state)                    // base_cs_write
{
    state &= 1;
    if (state == m_cs_state)
        return;

    m_cs_state = state;
    if (state == 1)
        m_last_cs_rising_edge_time = now();
    handle_event((m_cs_state == 1) ? EVENT_CS_RISING_EDGE : EVENT_CS_FALLING_EDGE);
}

void Eeprom93C46::clk_write(int state)                   // base_clk_write
{
    state &= 1;
    if (state == m_clk_state)
        return;

    m_clk_state = state;
    handle_event((m_clk_state == 1) ? EVENT_CLK_RISING_EDGE : EVENT_CLK_FALLING_EDGE);
}

void Eeprom93C46::di_write(int state)                    // base_di_write
{
    m_di_state = state & 1;
}

int Eeprom93C46::base_do_read() const
{
    return (m_state == STATE_READING_DATA && ((m_shift_register & 0x80000000) == 0)) ? 0 : 1;
}

int Eeprom93C46::base_ready_read() const
{
    return ready() ? 1 : 0;
}

// eeprom_serial_93cxx_device::do_read
int Eeprom93C46::do_read()
{
    return base_do_read() & ((m_state == STATE_WAIT_FOR_START_BIT) ? base_ready_read() : 1);
}

void Eeprom93C46::handle_event(eeprom_event event)
{
    switch (m_state)
    {
        case STATE_IN_RESET:
            // CS is the only thing that brings us out of reset
            if (event == EVENT_CS_RISING_EDGE)
                set_state(STATE_WAIT_FOR_START_BIT);
            break;

        case STATE_WAIT_FOR_START_BIT:
            // CLK rising edge with DI high is a start bit
            if (event == EVENT_CLK_RISING_EDGE && m_di_state == 1 && ready() && now() > m_last_cs_rising_edge_time)
            {
                m_command_address_accum = m_bits_accum = 0;
                set_state(STATE_WAIT_FOR_COMMAND);
            }

            // CS going low returns us to reset
            else if (event == EVENT_CS_FALLING_EDGE)
                set_state(STATE_IN_RESET);
            break;

        case STATE_WAIT_FOR_COMMAND:
            // CLK rising edge clocks in a command bit
            if (event == EVENT_CLK_RISING_EDGE)
            {
                // clock in the next bit
                m_command_address_accum = (m_command_address_accum << 1) | (uint32_t) m_di_state;
                if (++m_bits_accum == 2 + m_command_address_bits)
                    execute_command();
            }

            // CS going low returns us to reset
            else if (event == EVENT_CS_FALLING_EDGE)
                set_state(STATE_IN_RESET);
            break;

        case STATE_READING_DATA:
            // CLK rising edge clocks out a data bit
            if (event == (m_output_on_falling_clock_enabled ? EVENT_CLK_FALLING_EDGE : EVENT_CLK_RISING_EDGE))
            {
                int bit_index = (int) m_bits_accum++;

                // wrapping the address at a cell boundary is only for streaming
                if (bit_index % m_data_bits == 0 && (bit_index == 0 || m_streaming_enabled))
                    m_shift_register = read((m_address + m_bits_accum / m_data_bits) & ((1u << m_address_bits) - 1)) << (32 - m_data_bits);
                else
                    m_shift_register = (m_shift_register << 1) | 1;
                // PORT: m_do_cb (not connected)
            }

            // CS going low returns us to reset
            else if (event == EVENT_CS_FALLING_EDGE)
                set_state(STATE_IN_RESET);                  // PORT: overclock logging dropped
            break;

        case STATE_WAIT_FOR_DATA:
            // CLK rising edge clocks in a data bit
            if (event == EVENT_CLK_RISING_EDGE)
            {
                // clock in the next bit
                m_shift_register = (m_shift_register << 1) | (uint32_t) m_di_state;
                if (++m_bits_accum == (uint32_t) m_data_bits)
                    execute_write_command();
            }

            // CS going low returns us to reset
            else if (event == EVENT_CS_FALLING_EDGE)
                set_state(STATE_IN_RESET);
            break;

        case STATE_WAIT_FOR_COMPLETION:
            // CS going low returns us to reset
            if (event == EVENT_CS_FALLING_EDGE)
                set_state(STATE_IN_RESET);
            break;
    }
}

void Eeprom93C46::execute_command()
{
    // parse into a generic command and reset the accumulator count
    parse_command_and_address();
    m_bits_accum = 0;

    // each command advances differently
    switch (m_command)
    {
        case COMMAND_READ:
            // reads begin with a 0 bit
            m_shift_register = 0;
            set_state(STATE_READING_DATA);
            break;

        case COMMAND_WRITE:
        case COMMAND_WRITEALL:
            // writes and erases have no output
            m_shift_register = 0;
            set_state(STATE_WAIT_FOR_DATA);
            break;

        case COMMAND_ERASE:
            if (m_locked)
            {
                set_state(STATE_IN_RESET);
                break;
            }
            erase(m_address);
            set_state(STATE_WAIT_FOR_COMPLETION);
            break;

        case COMMAND_LOCK:
            m_locked = true;
            set_state(STATE_IN_RESET);
            break;

        case COMMAND_UNLOCK:
            m_locked = false;
            set_state(STATE_IN_RESET);
            break;

        case COMMAND_ERASEALL:
            if (m_locked)
            {
                set_state(STATE_IN_RESET);
                break;
            }
            erase_all();
            set_state(STATE_WAIT_FOR_COMPLETION);
            break;

        default:
            // PORT: MAME throws emu_fatalerror (unreachable: every 2-bit opcode maps to a command)
            set_state(STATE_IN_RESET);
            break;
    }
}

void Eeprom93C46::execute_write_command()
{
    switch (m_command)
    {
        case COMMAND_WRITE:
            if (m_locked)
            {
                set_state(STATE_IN_RESET);
                break;
            }
            write(m_address, m_shift_register);
            set_state(STATE_WAIT_FOR_COMPLETION);
            break;

        case COMMAND_WRITEALL:
            if (m_locked)
            {
                set_state(STATE_IN_RESET);
                break;
            }
            write_all(m_shift_register);
            set_state(STATE_WAIT_FOR_COMPLETION);
            break;

        default:
            set_state(STATE_IN_RESET);                      // PORT: MAME throws emu_fatalerror (unreachable)
            break;
    }
}

// eeprom_serial_93cxx_device::parse_command_and_address
void Eeprom93C46::parse_command_and_address()
{
    // set the defaults
    m_command = COMMAND_INVALID;
    m_address = m_command_address_accum & ((1u << m_command_address_bits) - 1);

    // extract the command portion and handle it
    switch (m_command_address_accum >> m_command_address_bits)
    {
        // opcode 0 needs two more bits to decode the operation
        case 0:
            switch (m_address >> (m_command_address_bits - 2))
            {
                case 0: m_command = COMMAND_LOCK;       break;
                case 1: m_command = COMMAND_WRITEALL;   break;
                case 2: m_command = COMMAND_ERASEALL;   break;
                case 3: m_command = COMMAND_UNLOCK;     break;
            }
            m_address = 0;
            break;

        case 1: m_command = COMMAND_WRITE;  break;
        case 2: m_command = COMMAND_READ;   break;
        case 3: m_command = COMMAND_ERASE;  break;
    }
}

// ================================================================================================
// System 32 communication board (simulation, link type 15033)
// license:BSD-3-Clause, copyright-holders:Ariane Fugmann (MAME 0.264 src/mame/sega/s32comm.cpp)
// ================================================================================================
void S32Comm::device_reset()
{
    m_zfg = 0;
    m_cn = 0;
    m_fg = 0;
}

void S32Comm::cn_w(uint8_t data)
{
    m_cn = data & 0x01;

    if (!m_cn)
    {
        // reset command
        m_linkenable = 0x00;
    }
    else
    {
        // init command
        m_linkenable = 0x01;
        m_linkid = 0x00;
        m_linkalive = 0x00;
        m_linkcount = 0x00;
        m_linktimer = 0x00e8; // 58 fps * 4s

        comm_tick();
    }
}

void S32Comm::comm_tick_15033()
{
    // m_shared[0] = node count
    // m_shared[1] = node id
    // m_shared[2] = node mode (0 = slave, 1 = master, ff = relay)
    // m_shared[3] = node ready-to-send
    // m_shared[4] = node link status (0 = offline, 1 = online)
    if (m_linkenable == 0x01)
    {
        if (m_linkalive == 0x02)
        {
            // link failed...
            m_shared[0] = 0xff;
            return;
        }
        else if (m_linkalive == 0x00)
        {
            // link not yet established...
            if (m_shared[0] == 0x56 && m_shared[1] == 0x37 && m_shared[2] == 0x30)
            {
                for (int j = 0x003 ; j < 0x0800 ; j++)
                {
                    m_shared[j] = 0;
                }
                m_shared[0x08] = 0x5A;
                m_shared[0x09] = 0x38;
                m_shared[0x0A] = 0x30;
            }

            // waiting...
            m_shared[4] = 0x00;

            // PORT: MAME opens its rx and tx sockets here and, if both open, runs the link
            // protocol; with no socket nothing else happens and the link is never established
        }

        // PORT: "if link established" (m_linkalive == 0x01) is unreachable without sockets
    }
}
