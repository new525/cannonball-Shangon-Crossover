/***************************************************************************
    Hitachi SH-1 / SH-2 CPU core: MAME's SH-2 family interpreter as a standalone class.
    See shcpu.hpp for the scope and the bus contract.

    Transcribed from MAME 0.264 src/devices/cpu/sh/sh.cpp, sh2.cpp and sh7032.cpp
    (BSD-3-Clause; copyright holders Juergen Buchmueller, R. Belmont, David Haywood,
    Angelo Salese). sh2.cpp itself is based on <tiraniddo@hotmail.com>'s C/C++ SH-2 core,
    with thanks in the original to Chuck Mason and Olivier Galibert.

    Notes on the transcription:
      - MAME's interpreter has no SH-1 specific behaviour: m_cpu_type is only consulted by
        the DRC (which treats DMULU/DMULS/MUL.L/MAC.L/BSRF/BRAF/DT/BTS/BFS as SH-2 only).
        The interpreter executes those on the SH7032 too, and so does this core.
      - The quirks of the original are kept on purpose (for example OR #imm,R0 taking two
        extra cycles, OR.B #imm,@(R0,GBR) taking none, JMP taking no extra cycle,
        MOV.x @Rm+,Rn not incrementing Rm when m == n).
      - BUSY_LOOP_HACKS is 0 in MAME 0.264, so those paths are not transcribed.
      - Arithmetic that would be signed overflow in C++ (negating INT_MIN in DMULS/MAC.L,
        the uint16 product in MULU) is done in unsigned arithmetic: same bits as MAME
        produces on its supported compilers, without undefined behaviour.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "shcpu.hpp"

namespace
{
    // SR bits (sh.h)
    constexpr uint32_t SH_T = 0x00000001;
    constexpr uint32_t SH_S = 0x00000002;
    constexpr uint32_t SH_I = 0x000000f0;
    constexpr uint32_t SH_Q = 0x00000100;
    constexpr uint32_t SH_M = 0x00000200;
    constexpr uint32_t SH_FLAGS = SH_M | SH_Q | SH_I | SH_S | SH_T;

    constexpr int SH2_INT_15 = 15;

    // util::sext
    inline int32_t sext(uint32_t value, int width)
    {
        const int shift = 32 - width;
        return int32_t(value << shift) >> shift;
    }

    inline uint32_t bit31(uint32_t v) { return (v >> 31) & 1; }

    inline uint32_t rotl_32(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
    inline uint32_t rotr_32(uint32_t v, int s) { return (v >> s) | (v << (32 - s)); }

    // address_space global mask for a space with the given number of address lines
    inline uint32_t space_mask(int lines)
    {
        return lines >= 32 ? 0xffffffffu : ((1u << lines) - 1);
    }

    inline int count_leading_zeros_32(uint32_t v)
    {
        if (!v) return 32;
        return __builtin_clz(v);
    }
}

#define REG_N  ((opcode >> 8) & 15)
#define REG_M  ((opcode >> 4) & 15)

ShCpu::ShCpu(Bus& bus, bool sh1, uint32_t address_mask, int addr_lines)
    : m_bus(bus)
    , m_am(address_mask)
    , m_space_mask(space_mask(addr_lines < 0 ? (sh1 ? 28 : 32) : addr_lines))
{
    // sh_common_execution::device_start leaves internal_irq_level at 0 until reset;
    // sh2_device::device_start sets m_nmi_line_state = 0.
    m_internal_irq_level = 0;
    m_nmi_line_state = 0;
}

/***************************************************************************
    sh2_device::device_reset (+ sh7032_device::device_reset, whose register
    block lives on the bus side)
***************************************************************************/

void ShCpu::reset()
{
    for (auto& r : m_r) r = 0;
    for (auto& s : m_irq_line_state) s = 0;

    m_pc = m_pr = m_sr = m_gbr = m_vbr = m_mach = m_macl = 0;
    m_ea = m_delay = 0;
    m_pending_irq = 0;
    m_pending_nmi = 0;
    m_sleep_mode = 0;
    m_internal_irq_level = -1;
    m_sr = SH_I;
    m_pc = read_long(0);
    m_r[15] = read_long(4);

    m_test_irq = 0;
    m_cpu_off = 0;
    m_internal_irq_vector = 0;
    // m_nmi_line_state is not reset by MAME's device_reset.
}

/***************************************************************************
    Memory access (sh2_device::read_xxx and write_xxx), plus the address space
    width mask MAME's address_space applies.
***************************************************************************/

uint8_t ShCpu::read_byte(uint32_t offset)
{
    if ((offset & 0xf0000000) == 0 || (offset & 0xf0000000) == 0x20000000)
        return m_bus.read8((offset & m_am) & m_space_mask);

    return m_bus.read8(offset & m_space_mask);
}

uint16_t ShCpu::read_word(uint32_t offset)
{
    if ((offset & 0xf0000000) == 0 || (offset & 0xf0000000) == 0x20000000)
        return m_bus.read16((offset & m_am) & m_space_mask);

    return m_bus.read16(offset & m_space_mask);
}

uint32_t ShCpu::read_long(uint32_t offset)
{
    /* 0x20000000 no Cache */
    /* 0x00000000 read thru Cache if CE bit is 1 */
    if ((offset & 0xf0000000) == 0 || (offset & 0xf0000000) == 0x20000000)
        return m_bus.read32((offset & m_am) & m_space_mask);

    return m_bus.read32(offset & m_space_mask);
}

void ShCpu::write_byte(uint32_t offset, uint8_t data)
{
    if ((offset & 0xf0000000) == 0 || (offset & 0xf0000000) == 0x20000000)
    {
        m_bus.write8((offset & m_am) & m_space_mask, data);
        return;
    }

    m_bus.write8(offset & m_space_mask, data);
}

void ShCpu::write_word(uint32_t offset, uint16_t data)
{
    if ((offset & 0xf0000000) == 0 || (offset & 0xf0000000) == 0x20000000)
    {
        m_bus.write16((offset & m_am) & m_space_mask, data);
        return;
    }

    m_bus.write16(offset & m_space_mask, data);
}

void ShCpu::write_long(uint32_t offset, uint32_t data)
{
    if ((offset & 0xf0000000) == 0 || (offset & 0xf0000000) == 0x20000000)
    {
        m_bus.write32((offset & m_am) & m_space_mask, data);
        return;
    }

    /* 0x20000000 no Cache */
    /* 0x00000000 read thru Cache if CE bit is 1 */
    m_bus.write32(offset & m_space_mask, data);
}

/***************************************************************************
    Interrupts (sh2_device)
***************************************************************************/

void ShCpu::check_pending_irq()
{
    if (m_pending_nmi)
    {
        sh2_exception(16);
        m_pending_nmi = 0;
    }
    else
    {
        int irq = m_internal_irq_level;
        if (m_pending_irq)
        {
            int external_irq = 15 - (count_leading_zeros_32(m_pending_irq) - 16);
            if (external_irq >= irq)
            {
                irq = external_irq;
            }
        }

        if (irq >= 0)
        {
            sh2_exception(irq);
        }
    }
}

void ShCpu::set_nmi_state(int state)
{
    // execute_set_input(INPUT_LINE_NMI, state), interpreter path
    if (m_nmi_line_state == state)
        return;

    m_nmi_line_state = int8_t(state);

    if (state != CLEAR_LINE)
    {
        m_pending_nmi = 1;

        if (m_delay)
            m_test_irq = 1;
        else
            check_pending_irq();
    }
}

void ShCpu::set_irq_state(int irqline, int state)
{
    // execute_set_input(irqline, state), interpreter path
    if (irqline < 0 || irqline > 15)
        return;

    if (m_irq_line_state[irqline] == state)
        return;

    m_irq_line_state[irqline] = int8_t(state);

    if (state == CLEAR_LINE)
    {
        m_pending_irq &= ~(1u << irqline);
    }
    else
    {
        m_pending_irq |= 1u << irqline;

        if (m_delay)
            m_test_irq = 1;
        else
            check_pending_irq();
    }
}

void ShCpu::sh2_exception(int irqline)
{
    int vector;

    if (irqline != 16)
    {
        if (irqline <= int((m_sr >> 4) & 15)) /* If the cpu forbids this interrupt */
            return;

        // if this is an sh2 internal irq, use its vector
        if (m_internal_irq_level == irqline)
        {
            vector = m_internal_irq_vector;
            /* avoid spurious irqs with this (TODO: needs a better fix) */
            m_internal_irq_level = -1;
        }
        else
        {
            m_bus.irq_acknowledge(irqline);   // standard_irq_callback(irqline, pc)
            vector = 64 + irqline / 2;
        }
    }
    else
    {
        vector = 11;
    }

    sh2_exception_internal(irqline, vector);
}

void ShCpu::sh2_exception_internal(int irqline, int vector)
{
    m_r[15] -= 4;
    write_long(m_r[15], m_sr);     /* push SR onto stack */
    m_r[15] -= 4;
    write_long(m_r[15], m_pc);     /* push PC onto stack */

    /* set I flags in SR */
    if (irqline > SH2_INT_15)
        m_sr = m_sr | SH_I;
    else
        m_sr = (m_sr & ~SH_I) | (uint32_t(irqline) << 4);

    /* fetch PC */
    m_pc = read_long(m_vbr + vector * 4);

    if (m_sleep_mode == 1)
        m_sleep_mode = 2;
}

/***************************************************************************
    sh2_device::execute_run (interpreter path)
***************************************************************************/

int ShCpu::run(int cycles)
{
    m_icount = cycles;
    m_stolen = 0;

    if (m_cpu_off)
    {
        m_icount = 0;
        return cycles;
    }

    do
    {
        if (m_hook)
            m_bus.instr_hook(m_pc);

        const uint16_t opcode = m_bus.fetch16((m_pc >= 0x40000000 ? m_pc : m_pc & m_am) & m_space_mask);

        if (m_delay)
        {
            m_pc = m_delay;
            m_delay = 0;
        }
        else
            m_pc += 2;

        execute_one(opcode);

        if (m_test_irq && !m_delay)
        {
            check_pending_irq();
            m_test_irq = 0;
        }
        m_icount--;
    } while (m_icount > 0);

    return cycles - m_stolen - m_icount;
}

void ShCpu::end_timeslice()
{
    // device_execute_interface::abort_timeslice: the remaining cycles are taken away from
    // this slice, so the instruction in progress finishes and the loop exits.
    m_stolen += m_icount;
    m_icount = 0;
}

/***************************************************************************
    Instructions (sh_common_execution)
***************************************************************************/

/*  ADD     Rm,Rn */
void ShCpu::ADD(uint32_t m, uint32_t n)
{
    m_r[n] += m_r[m];
}

/*  ADD     #imm,Rn */
void ShCpu::ADDI(uint32_t i, uint32_t n)
{
    m_r[n] += sext(i, 8);
}

/*  ADDC    Rm,Rn */
void ShCpu::ADDC(uint32_t m, uint32_t n)
{
    uint32_t tmp0, tmp1;

    tmp1 = m_r[n] + m_r[m];
    tmp0 = m_r[n];
    m_r[n] = tmp1 + (m_sr & SH_T);
    if (tmp0 > tmp1)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
    if (tmp1 > m_r[n])
        m_sr |= SH_T;
}

/*  ADDV    Rm,Rn */
void ShCpu::ADDV(uint32_t m, uint32_t n)
{
    int32_t dest = bit31(m_r[n]);
    int32_t src = bit31(m_r[m]);
    src += dest;

    m_r[n] += m_r[m];

    int32_t ans = bit31(m_r[n]);
    ans += dest;

    if (src != 1)
    {
        if (ans == 1)
            m_sr |= SH_T;
        else
            m_sr &= ~SH_T;
    }
    else
        m_sr &= ~SH_T;
}

/*  AND     Rm,Rn */
void ShCpu::AND(uint32_t m, uint32_t n)
{
    m_r[n] &= m_r[m];
}

/*  AND     #imm,R0 */
void ShCpu::ANDI(uint32_t i)
{
    m_r[0] &= i;
}

/*  AND.B   #imm,@(R0,GBR) */
void ShCpu::ANDM(uint32_t i)
{
    m_ea = m_gbr + m_r[0];
    uint32_t temp = i & read_byte(m_ea);
    write_byte(m_ea, uint8_t(temp));
    m_icount -= 2;
}

/*  BF      disp8 */
void ShCpu::BF(uint32_t d)
{
    if ((m_sr & SH_T) == 0)
    {
        int32_t disp = sext(d, 8);
        m_pc = m_ea = m_pc + disp * 2 + 2;
        m_icount -= 2;
    }
}

/*  BFS     disp8 */
void ShCpu::BFS(uint32_t d)
{
    if ((m_sr & SH_T) == 0)
    {
        int32_t disp = sext(d, 8);
        m_delay = m_ea = m_pc + disp * 2 + 2;
        m_icount--;
    }
}

/*  BRA     disp12 */
void ShCpu::BRA(uint32_t d)
{
    int32_t disp = sext(d, 12);
    m_delay = m_ea = m_pc + disp * 2 + 2;
    m_icount--;
}

/*  BRAF    Rm */
void ShCpu::BRAF(uint32_t m)
{
    m_delay = m_pc + m_r[m] + 2;
    m_icount--;
}

/*  BSR     disp12 */
void ShCpu::BSR(uint32_t d)
{
    int32_t disp = sext(d, 12);

    m_pr = m_pc + 2;
    m_delay = m_ea = m_pc + disp * 2 + 2;
    m_icount--;
}

/*  BSRF    Rm */
void ShCpu::BSRF(uint32_t m)
{
    m_pr = m_pc + 2;
    m_delay = m_pc + m_r[m] + 2;
    m_icount--;
}

/*  BT      disp8 */
void ShCpu::BT(uint32_t d)
{
    if ((m_sr & SH_T) != 0)
    {
        int32_t disp = sext(d, 8);
        m_pc = m_ea = m_pc + disp * 2 + 2;
        m_icount -= 2;
    }
}

/*  BTS     disp8 */
void ShCpu::BTS(uint32_t d)
{
    if ((m_sr & SH_T) != 0)
    {
        int32_t disp = sext(d, 8);
        m_delay = m_ea = m_pc + disp * 2 + 2;
        m_icount--;
    }
}

/*  CLRMAC */
void ShCpu::CLRMAC()
{
    m_mach = 0;
    m_macl = 0;
}

/*  CLRT */
void ShCpu::CLRT()
{
    m_sr &= ~SH_T;
}

/*  CMP_EQ  Rm,Rn */
void ShCpu::CMPEQ(uint32_t m, uint32_t n)
{
    if (m_r[n] == m_r[m])
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  CMP_GE  Rm,Rn */
void ShCpu::CMPGE(uint32_t m, uint32_t n)
{
    if (int32_t(m_r[n]) >= int32_t(m_r[m]))
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  CMP_GT  Rm,Rn */
void ShCpu::CMPGT(uint32_t m, uint32_t n)
{
    if (int32_t(m_r[n]) > int32_t(m_r[m]))
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  CMP_HI  Rm,Rn */
void ShCpu::CMPHI(uint32_t m, uint32_t n)
{
    if (m_r[n] > m_r[m])
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  CMP_HS  Rm,Rn */
void ShCpu::CMPHS(uint32_t m, uint32_t n)
{
    if (m_r[n] >= m_r[m])
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  CMP_PL  Rn */
void ShCpu::CMPPL(uint32_t n)
{
    if (int32_t(m_r[n]) > 0)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  CMP_PZ  Rn */
void ShCpu::CMPPZ(uint32_t n)
{
    if (int32_t(m_r[n]) >= 0)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  CMP_STR  Rm,Rn */
void ShCpu::CMPSTR(uint32_t m, uint32_t n)
{
    uint32_t temp = m_r[n] ^ m_r[m];
    uint8_t upper_byte = (temp >> 24) & 0xff;
    uint8_t mid_upper_byte = (temp >> 16) & 0xff;
    uint8_t mid_lower_byte = (temp >> 8) & 0xff;
    uint8_t lower_byte = temp & 0xff;
    if (upper_byte && mid_upper_byte && mid_lower_byte && lower_byte)
        m_sr &= ~SH_T;
    else
        m_sr |= SH_T;
}

/*  CMP/EQ #imm,R0 */
void ShCpu::CMPIM(uint32_t i)
{
    uint32_t imm = uint32_t(sext(i, 8));

    if (m_r[0] == imm)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  DIV0S   Rm,Rn */
void ShCpu::DIV0S(uint32_t m, uint32_t n)
{
    if (!bit31(m_r[n]))
        m_sr &= ~SH_Q;
    else
        m_sr |= SH_Q;

    if (!bit31(m_r[m]))
        m_sr &= ~SH_M;
    else
        m_sr |= SH_M;

    if (bit31(m_r[m] ^ m_r[n]))
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  DIV0U */
void ShCpu::DIV0U()
{
    m_sr &= ~(SH_M | SH_Q | SH_T);
}

/*  DIV1 Rm,Rn */
void ShCpu::DIV1(uint32_t m, uint32_t n)
{
    uint32_t old_q = m_sr & SH_Q;
    if (0x80000000 & m_r[n])
        m_sr |= SH_Q;
    else
        m_sr &= ~SH_Q;

    m_r[n] = (m_r[n] << 1) | (m_sr & SH_T);

    if (!old_q)
    {
        if (!(m_sr & SH_M))
        {
            uint32_t tmp = m_r[n];
            m_r[n] -= m_r[m];
            if (!(m_sr & SH_Q))
            {
                if (m_r[n] > tmp)
                    m_sr |= SH_Q;
                else
                    m_sr &= ~SH_Q;
            }
            else
            {
                if (m_r[n] > tmp)
                    m_sr &= ~SH_Q;
                else
                    m_sr |= SH_Q;
            }
        }
        else
        {
            uint32_t tmp = m_r[n];
            m_r[n] += m_r[m];
            if (!(m_sr & SH_Q))
            {
                if (m_r[n] < tmp)
                    m_sr &= ~SH_Q;
                else
                    m_sr |= SH_Q;
            }
            else
            {
                if (m_r[n] < tmp)
                    m_sr |= SH_Q;
                else
                    m_sr &= ~SH_Q;
            }
        }
    }
    else
    {
        if (!(m_sr & SH_M))
        {
            uint32_t tmp = m_r[n];
            m_r[n] += m_r[m];
            if (!(m_sr & SH_Q))
            {
                if (m_r[n] < tmp)
                    m_sr |= SH_Q;
                else
                    m_sr &= ~SH_Q;
            }
            else
            {
                if (m_r[n] < tmp)
                    m_sr &= ~SH_Q;
                else
                    m_sr |= SH_Q;
            }
        }
        else
        {
            uint32_t tmp = m_r[n];
            m_r[n] -= m_r[m];
            if (!(m_sr & SH_Q))
            {
                if (m_r[n] > tmp)
                    m_sr &= ~SH_Q;
                else
                    m_sr |= SH_Q;
            }
            else
            {
                if (m_r[n] > tmp)
                    m_sr |= SH_Q;
                else
                    m_sr &= ~SH_Q;
            }
        }
    }

    uint32_t tmp = (m_sr & (SH_Q | SH_M));
    if (tmp == 0 || tmp == 0x300) /* if Q == M set T else clear T */
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  DMULS.L Rm,Rn */
void ShCpu::DMULS(uint32_t m, uint32_t n)
{
    uint32_t tempn = m_r[n];
    uint32_t tempm = m_r[m];
    bool fnlml = bit31(tempn ^ tempm) != 0;

    if (int32_t(tempn) < 0)
        tempn = 0 - tempn;
    if (int32_t(tempm) < 0)
        tempm = 0 - tempm;

    uint32_t rn_l = tempn & 0x0000ffff;
    uint32_t rn_h = tempn >> 16;
    uint32_t rm_l = tempm & 0x0000ffff;
    uint32_t rm_h = tempm >> 16;

    uint32_t temp0 = rm_l * rn_l;
    uint32_t temp1 = rm_h * rn_l;
    uint32_t temp2 = rm_l * rn_h;
    uint32_t temp3 = rm_h * rn_h;

    uint32_t res2 = 0;
    uint32_t res1 = temp1 + temp2;
    if (res1 < temp1)
        res2 += 0x00010000;
    temp1 = res1 << 16;

    uint32_t res0 = temp0 + temp1;
    if (res0 < temp0)
        res2++;
    res2 = res2 + (res1 >> 16) + temp3;

    if (fnlml)
    {
        res2 = ~res2;
        if (res0 == 0)
            res2++;
        else
            res0 = (~res0) + 1;
    }

    m_mach = res2;
    m_macl = res0;
    m_icount--;
}

/*  DMULU.L Rm,Rn */
void ShCpu::DMULU(uint32_t m, uint32_t n)
{
    uint32_t rn_l = m_r[n] & 0x0000ffff;
    uint32_t rn_h = m_r[n] >> 16;
    uint32_t rm_l = m_r[m] & 0x0000ffff;
    uint32_t rm_h = m_r[m] >> 16;

    uint32_t temp0 = rm_l * rn_l;
    uint32_t temp1 = rm_h * rn_l;
    uint32_t temp2 = rm_l * rn_h;
    uint32_t temp3 = rm_h * rn_h;

    uint32_t res2 = 0;
    uint32_t res1 = temp1 + temp2;
    if (res1 < temp1)
        res2 += 0x00010000;

    temp1 = res1 << 16;
    uint32_t res0 = temp0 + temp1;
    if (res0 < temp0)
        res2++;

    res2 = res2 + (res1 >> 16) + temp3;

    m_mach = res2;
    m_macl = res0;
    m_icount--;
}

/*  DT      Rn */
void ShCpu::DT(uint32_t n)
{
    m_r[n]--;
    if (m_r[n] == 0)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  EXTS.B  Rm,Rn */
void ShCpu::EXTSB(uint32_t m, uint32_t n)
{
    m_r[n] = uint32_t(sext(m_r[m], 8));
}

/*  EXTS.W  Rm,Rn */
void ShCpu::EXTSW(uint32_t m, uint32_t n)
{
    m_r[n] = uint32_t(sext(m_r[m], 16));
}

/*  EXTU.B  Rm,Rn */
void ShCpu::EXTUB(uint32_t m, uint32_t n)
{
    m_r[n] = m_r[m] & 0x000000ff;
}

/*  EXTU.W  Rm,Rn */
void ShCpu::EXTUW(uint32_t m, uint32_t n)
{
    m_r[n] = m_r[m] & 0x0000ffff;
}

/*  JMP     @Rm */
void ShCpu::JMP(uint32_t m)
{
    m_delay = m_ea = m_r[m];
    // no icount-- here in MAME ("not in SH4 implementation?")
}

/*  JSR     @Rm */
void ShCpu::JSR(uint32_t m)
{
    m_pr = m_pc + 2;
    m_delay = m_ea = m_r[m];
    m_icount--;
}

/*  LDC     Rm,GBR */
void ShCpu::LDCGBR(uint32_t m)
{
    m_gbr = m_r[m];
}

/*  LDC     Rm,VBR */
void ShCpu::LDCVBR(uint32_t m)
{
    m_vbr = m_r[m];
}

/*  LDC.L   @Rm+,GBR */
void ShCpu::LDCMGBR(uint32_t m)
{
    m_ea = m_r[m];
    m_gbr = read_long(m_ea);
    m_r[m] += 4;
    m_icount -= 2;
}

/*  LDC.L   @Rm+,VBR */
void ShCpu::LDCMVBR(uint32_t m)
{
    m_ea = m_r[m];
    m_vbr = read_long(m_ea);
    m_r[m] += 4;
    m_icount -= 2;
}

/*  LDS     Rm,MACH */
void ShCpu::LDSMACH(uint32_t m)
{
    m_mach = m_r[m];
}

/*  LDS     Rm,MACL */
void ShCpu::LDSMACL(uint32_t m)
{
    m_macl = m_r[m];
}

/*  LDS     Rm,PR */
void ShCpu::LDSPR(uint32_t m)
{
    m_pr = m_r[m];
}

/*  LDS.L   @Rm+,MACH */
void ShCpu::LDSMMACH(uint32_t m)
{
    m_ea = m_r[m];
    m_mach = read_long(m_ea);
    m_r[m] += 4;
}

/*  LDS.L   @Rm+,MACL */
void ShCpu::LDSMMACL(uint32_t m)
{
    m_ea = m_r[m];
    m_macl = read_long(m_ea);
    m_r[m] += 4;
}

/*  LDS.L   @Rm+,PR */
void ShCpu::LDSMPR(uint32_t m)
{
    m_ea = m_r[m];
    m_pr = read_long(m_ea);
    m_r[m] += 4;
}

/*  MAC.L   @Rm+,@Rn+ */
void ShCpu::MAC_L(uint32_t m, uint32_t n)
{
    uint32_t tempn = read_long(m_r[n]);
    m_r[n] += 4;

    uint32_t tempm = read_long(m_r[m]);
    m_r[m] += 4;

    bool fnlml = bit31(tempn ^ tempm) != 0;

    if (int32_t(tempn) < 0)
        tempn = 0 - tempn;
    if (int32_t(tempm) < 0)
        tempm = 0 - tempm;

    uint32_t rn_l = tempn & 0x0000ffff;
    uint32_t rn_h = tempn >> 16;
    uint32_t rm_l = tempm & 0x0000ffff;
    uint32_t rm_h = tempm >> 16;

    uint32_t temp0 = rm_l * rn_l;
    uint32_t temp1 = rm_h * rn_l;
    uint32_t temp2 = rm_l * rn_h;
    uint32_t temp3 = rm_h * rn_h;

    uint32_t res2 = 0;
    uint32_t res1 = temp1 + temp2;
    if (res1 < temp1)
        res2 += 0x00010000;
    temp1 = res1 << 16;

    uint32_t res0 = temp0 + temp1;
    if (res0 < temp0)
        res2++;
    res2 = res2 + (res1 >> 16) + temp3;

    if (fnlml)
    {
        res2 = ~res2;
        if (res0 == 0)
            res2++;
        else
            res0 = (~res0) + 1;
    }

    if (m_sr & SH_S)
    {
        res0 = m_macl + res0;
        if (m_macl > res0)
            res2++;
        res2 += m_mach & 0x0000ffff;
        if (int32_t(res2) < 0 && res2 < 0xffff8000)
        {
            res2 = 0x00008000;
            res0 = 0x00000000;
        }
        else if (int32_t(res2) > 0 && res2 > 0x00007fff)
        {
            res2 = 0x00007fff;
            res0 = 0xffffffff;
        }
        m_mach = res2;
        m_macl = res0;
    }
    else
    {
        res0 = m_macl + res0;
        if (m_macl > res0)
            res2++;
        res2 += m_mach;
        m_mach = res2;
        m_macl = res0;
    }
    m_icount -= 2;
}

/*  MAC.W   @Rm+,@Rn+ */
void ShCpu::MAC_W(uint32_t m, uint32_t n)
{
    int32_t tempn = int32_t(int16_t(read_word(m_r[n])));
    m_r[n] += 2;

    int32_t tempm = int32_t(int16_t(read_word(m_r[m])));
    m_r[m] += 2;

    uint32_t templ = m_macl;
    tempm *= tempn;   // int16 * int16 always fits in int32

    int32_t dest = bit31(m_macl);
    int32_t src = int32_t(bit31(uint32_t(tempm))) + dest;
    tempn = bit31(uint32_t(tempm)) ? -1 : 0;

    m_macl += uint32_t(tempm);

    int32_t ans = int32_t(bit31(m_macl)) + dest;

    if (m_sr & SH_S)
    {
        if (ans == 1)
        {
            if (src == 0)
                m_macl = 0x7fffffff;
            else if (src == 2)
                m_macl = 0x80000000;
        }
    }
    else
    {
        m_mach += uint32_t(tempn);
        if (templ > m_macl)
            m_mach += 1;
    }
    m_icount -= 2;
}

/*  MOV     Rm,Rn */
void ShCpu::MOV(uint32_t m, uint32_t n)
{
    m_r[n] = m_r[m];
}

/*  MOV.B   Rm,@Rn */
void ShCpu::MOVBS(uint32_t m, uint32_t n)
{
    m_ea = m_r[n];
    write_byte(m_ea, uint8_t(m_r[m] & 0x000000ff));
}

/*  MOV.W   Rm,@Rn */
void ShCpu::MOVWS(uint32_t m, uint32_t n)
{
    m_ea = m_r[n];
    write_word(m_ea, uint16_t(m_r[m] & 0x0000ffff));
}

/*  MOV.L   Rm,@Rn */
void ShCpu::MOVLS(uint32_t m, uint32_t n)
{
    m_ea = m_r[n];
    write_long(m_ea, m_r[m]);
}

/*  MOV.B   @Rm,Rn */
void ShCpu::MOVBL(uint32_t m, uint32_t n)
{
    m_ea = m_r[m];
    m_r[n] = uint32_t(sext(read_byte(m_ea), 8));
}

/*  MOV.W   @Rm,Rn */
void ShCpu::MOVWL(uint32_t m, uint32_t n)
{
    m_ea = m_r[m];
    m_r[n] = uint32_t(sext(read_word(m_ea), 16));
}

/*  MOV.L   @Rm,Rn */
void ShCpu::MOVLL(uint32_t m, uint32_t n)
{
    m_ea = m_r[m];
    m_r[n] = read_long(m_ea);
}

/*  MOV.B   Rm,@-Rn */
void ShCpu::MOVBM(uint32_t m, uint32_t n)
{
    uint8_t data = uint8_t(m_r[m]);

    m_r[n] -= 1;
    write_byte(m_r[n], data);
}

/*  MOV.W   Rm,@-Rn */
void ShCpu::MOVWM(uint32_t m, uint32_t n)
{
    uint16_t data = uint16_t(m_r[m]);

    m_r[n] -= 2;
    write_word(m_r[n], data);
}

/*  MOV.L   Rm,@-Rn */
void ShCpu::MOVLM(uint32_t m, uint32_t n)
{
    uint32_t data = m_r[m];

    m_r[n] -= 4;
    write_long(m_r[n], data);
}

/*  MOV.B   @Rm+,Rn */
void ShCpu::MOVBP(uint32_t m, uint32_t n)
{
    m_r[n] = uint32_t(sext(read_byte(m_r[m]), 8));
    if (n != m)
        m_r[m] += 1;
}

/*  MOV.W   @Rm+,Rn */
void ShCpu::MOVWP(uint32_t m, uint32_t n)
{
    m_r[n] = uint32_t(sext(read_word(m_r[m]), 16));
    if (n != m)
        m_r[m] += 2;
}

/*  MOV.L   @Rm+,Rn */
void ShCpu::MOVLP(uint32_t m, uint32_t n)
{
    m_r[n] = read_long(m_r[m]);
    if (n != m)
        m_r[m] += 4;
}

/*  MOV.B   Rm,@(R0,Rn) */
void ShCpu::MOVBS0(uint32_t m, uint32_t n)
{
    m_ea = m_r[n] + m_r[0];
    write_byte(m_ea, uint8_t(m_r[m]));
}

/*  MOV.W   Rm,@(R0,Rn) */
void ShCpu::MOVWS0(uint32_t m, uint32_t n)
{
    m_ea = m_r[n] + m_r[0];
    write_word(m_ea, uint16_t(m_r[m]));
}

/*  MOV.L   Rm,@(R0,Rn) */
void ShCpu::MOVLS0(uint32_t m, uint32_t n)
{
    m_ea = m_r[n] + m_r[0];
    write_long(m_ea, m_r[m]);
}

/*  MOV.B   @(R0,Rm),Rn */
void ShCpu::MOVBL0(uint32_t m, uint32_t n)
{
    m_ea = m_r[m] + m_r[0];
    m_r[n] = uint32_t(sext(read_byte(m_ea), 8));
}

/*  MOV.W   @(R0,Rm),Rn */
void ShCpu::MOVWL0(uint32_t m, uint32_t n)
{
    m_ea = m_r[m] + m_r[0];
    m_r[n] = uint32_t(sext(read_word(m_ea), 16));
}

/*  MOV.L   @(R0,Rm),Rn */
void ShCpu::MOVLL0(uint32_t m, uint32_t n)
{
    m_ea = m_r[m] + m_r[0];
    m_r[n] = read_long(m_ea);
}

/*  MOV     #imm,Rn */
void ShCpu::MOVI(uint32_t i, uint32_t n)
{
    m_r[n] = uint32_t(sext(i, 8));
}

/*  MOV.W   @(disp8,PC),Rn */
void ShCpu::MOVWI(uint32_t d, uint32_t n)
{
    uint32_t disp = d & 0xff;
    m_ea = m_pc + disp * 2 + 2;
    m_r[n] = uint32_t(sext(read_word(m_ea), 16));
}

/*  MOV.L   @(disp8,PC),Rn */
void ShCpu::MOVLI(uint32_t d, uint32_t n)
{
    uint32_t disp = d & 0xff;
    m_ea = ((m_pc + 2) & ~3u) + disp * 4;
    m_r[n] = read_long(m_ea);
}

/*  MOV.B   @(disp8,GBR),R0 */
void ShCpu::MOVBLG(uint32_t d)
{
    uint32_t disp = d & 0xff;
    m_ea = m_gbr + disp;
    m_r[0] = uint32_t(sext(read_byte(m_ea), 8));
}

/*  MOV.W   @(disp8,GBR),R0 */
void ShCpu::MOVWLG(uint32_t d)
{
    uint32_t disp = d & 0xff;
    m_ea = m_gbr + disp * 2;
    m_r[0] = uint32_t(sext(read_word(m_ea), 16));
}

/*  MOV.L   @(disp8,GBR),R0 */
void ShCpu::MOVLLG(uint32_t d)
{
    uint32_t disp = d & 0xff;
    m_ea = m_gbr + disp * 4;
    m_r[0] = read_long(m_ea);
}

/*  MOV.B   R0,@(disp8,GBR) */
void ShCpu::MOVBSG(uint32_t d)
{
    uint32_t disp = d & 0xff;
    m_ea = m_gbr + disp;
    write_byte(m_ea, uint8_t(m_r[0]));
}

/*  MOV.W   R0,@(disp8,GBR) */
void ShCpu::MOVWSG(uint32_t d)
{
    uint32_t disp = d & 0xff;
    m_ea = m_gbr + disp * 2;
    write_word(m_ea, uint16_t(m_r[0]));
}

/*  MOV.L   R0,@(disp8,GBR) */
void ShCpu::MOVLSG(uint32_t d)
{
    uint32_t disp = d & 0xff;
    m_ea = m_gbr + disp * 4;
    write_long(m_ea, m_r[0]);
}

/*  MOV.B   R0,@(disp4,Rn) */
void ShCpu::MOVBS4(uint32_t d, uint32_t n)
{
    uint32_t disp = d & 0x0f;
    m_ea = m_r[n] + disp;
    write_byte(m_ea, uint8_t(m_r[0]));
}

/*  MOV.W   R0,@(disp4,Rn) */
void ShCpu::MOVWS4(uint32_t d, uint32_t n)
{
    uint32_t disp = d & 0x0f;
    m_ea = m_r[n] + disp * 2;
    write_word(m_ea, uint16_t(m_r[0]));
}

/* MOV.L Rm,@(disp4,Rn) */
void ShCpu::MOVLS4(uint32_t m, uint32_t d, uint32_t n)
{
    uint32_t disp = d & 0x0f;
    m_ea = m_r[n] + disp * 4;
    write_long(m_ea, m_r[m]);
}

/*  MOV.B   @(disp4,Rm),R0 */
void ShCpu::MOVBL4(uint32_t m, uint32_t d)
{
    uint32_t disp = d & 0x0f;
    m_ea = m_r[m] + disp;
    m_r[0] = uint32_t(sext(read_byte(m_ea), 8));
}

/*  MOV.W   @(disp4,Rm),R0 */
void ShCpu::MOVWL4(uint32_t m, uint32_t d)
{
    uint32_t disp = d & 0x0f;
    m_ea = m_r[m] + disp * 2;
    m_r[0] = uint32_t(sext(read_word(m_ea), 16));
}

/*  MOV.L   @(disp4,Rm),Rn */
void ShCpu::MOVLL4(uint32_t m, uint32_t d, uint32_t n)
{
    uint32_t disp = d & 0x0f;
    m_ea = m_r[m] + disp * 4;
    m_r[n] = read_long(m_ea);
}

/*  MOVA    @(disp8,PC),R0 */
void ShCpu::MOVA(uint32_t d)
{
    uint32_t disp = d & 0xff;
    m_ea = ((m_pc + 2) & ~3u) + disp * 4;
    m_r[0] = m_ea;
}

/*  MOVT    Rn */
void ShCpu::MOVT(uint32_t n)
{
    m_r[n] = m_sr & SH_T;
}

/*  MUL.L   Rm,Rn */
void ShCpu::MULL(uint32_t m, uint32_t n)
{
    m_macl = m_r[n] * m_r[m];
    m_icount--;
}

/*  MULS    Rm,Rn */
void ShCpu::MULS(uint32_t m, uint32_t n)
{
    m_macl = uint32_t(int32_t(int16_t(m_r[n])) * int32_t(int16_t(m_r[m])));
}

/*  MULU    Rm,Rn */
void ShCpu::MULU(uint32_t m, uint32_t n)
{
    m_macl = uint32_t(uint16_t(m_r[n])) * uint32_t(uint16_t(m_r[m]));
}

/*  NEG     Rm,Rn */
void ShCpu::NEG(uint32_t m, uint32_t n)
{
    m_r[n] = 0 - m_r[m];
}

/*  NEGC    Rm,Rn */
void ShCpu::NEGC(uint32_t m, uint32_t n)
{
    uint32_t temp = m_r[m];
    m_r[n] = -temp - (m_sr & SH_T);
    if (temp || (m_sr & SH_T))
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  NOP */
void ShCpu::NOP()
{
}

/*  NOT     Rm,Rn */
void ShCpu::NOT(uint32_t m, uint32_t n)
{
    m_r[n] = ~m_r[m];
}

/*  OR      Rm,Rn */
void ShCpu::OR(uint32_t m, uint32_t n)
{
    m_r[n] |= m_r[m];
}

/*  OR      #imm,R0 */
void ShCpu::ORI(uint32_t i)
{
    m_r[0] |= i;
    m_icount -= 2;   // sic (MAME)
}

/*  OR.B    #imm,@(R0,GBR) */
void ShCpu::ORM(uint32_t i)
{
    m_ea = m_gbr + m_r[0];
    write_byte(m_ea, read_byte(m_ea) | uint8_t(i));
    // no extra cycles here in MAME
}

/*  ROTCL   Rn */
void ShCpu::ROTCL(uint32_t n)
{
    uint32_t temp = (m_r[n] >> 31) & SH_T;
    m_r[n] = (m_r[n] << 1) | (m_sr & SH_T);
    m_sr = (m_sr & ~SH_T) | temp;
}

/*  ROTCR   Rn */
void ShCpu::ROTCR(uint32_t n)
{
    uint32_t temp = (m_sr & SH_T) << 31;
    if (m_r[n] & SH_T)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
    m_r[n] = (m_r[n] >> 1) | temp;
}

/*  ROTL    Rn */
void ShCpu::ROTL(uint32_t n)
{
    m_sr = (m_sr & ~SH_T) | ((m_r[n] >> 31) & SH_T);
    m_r[n] = rotl_32(m_r[n], 1);
}

/*  ROTR    Rn */
void ShCpu::ROTR(uint32_t n)
{
    m_sr = (m_sr & ~SH_T) | (m_r[n] & SH_T);
    m_r[n] = rotr_32(m_r[n], 1);
}

/*  RTS */
void ShCpu::RTS()
{
    m_delay = m_ea = m_pr;
    m_icount--;
}

/*  SETT */
void ShCpu::SETT()
{
    m_sr |= SH_T;
}

/*  SHAL    Rn      (same as SHLL) */
void ShCpu::SHAL(uint32_t n)
{
    m_sr = (m_sr & ~SH_T) | ((m_r[n] >> 31) & SH_T);
    m_r[n] <<= 1;
}

/*  SHAR    Rn */
void ShCpu::SHAR(uint32_t n)
{
    m_sr = (m_sr & ~SH_T) | (m_r[n] & SH_T);
    m_r[n] = uint32_t(int32_t(m_r[n]) >> 1);
}

/*  SHLL    Rn      (same as SHAL) */
void ShCpu::SHLL(uint32_t n)
{
    m_sr = (m_sr & ~SH_T) | ((m_r[n] >> 31) & SH_T);
    m_r[n] <<= 1;
}

/*  SHLL2   Rn */
void ShCpu::SHLL2(uint32_t n)
{
    m_r[n] <<= 2;
}

/*  SHLL8   Rn */
void ShCpu::SHLL8(uint32_t n)
{
    m_r[n] <<= 8;
}

/*  SHLL16  Rn */
void ShCpu::SHLL16(uint32_t n)
{
    m_r[n] <<= 16;
}

/*  SHLR    Rn */
void ShCpu::SHLR(uint32_t n)
{
    m_sr = (m_sr & ~SH_T) | (m_r[n] & SH_T);
    m_r[n] >>= 1;
}

/*  SHLR2   Rn */
void ShCpu::SHLR2(uint32_t n)
{
    m_r[n] >>= 2;
}

/*  SHLR8   Rn */
void ShCpu::SHLR8(uint32_t n)
{
    m_r[n] >>= 8;
}

/*  SHLR16  Rn */
void ShCpu::SHLR16(uint32_t n)
{
    m_r[n] >>= 16;
}

/*  STC     SR,Rn */
void ShCpu::STCSR(uint32_t n)
{
    m_r[n] = m_sr;
}

/*  STC     GBR,Rn */
void ShCpu::STCGBR(uint32_t n)
{
    m_r[n] = m_gbr;
}

/*  STC     VBR,Rn */
void ShCpu::STCVBR(uint32_t n)
{
    m_r[n] = m_vbr;
}

/*  STC.L   SR,@-Rn */
void ShCpu::STCMSR(uint32_t n)
{
    m_r[n] -= 4;
    m_ea = m_r[n];
    write_long(m_ea, m_sr);
    m_icount--;
}

/*  STC.L   GBR,@-Rn */
void ShCpu::STCMGBR(uint32_t n)
{
    m_r[n] -= 4;
    m_ea = m_r[n];
    write_long(m_ea, m_gbr);
    m_icount--;
}

/*  STC.L   VBR,@-Rn */
void ShCpu::STCMVBR(uint32_t n)
{
    m_r[n] -= 4;
    m_ea = m_r[n];
    write_long(m_ea, m_vbr);
    m_icount--;
}

/*  STS     MACH,Rn */
void ShCpu::STSMACH(uint32_t n)
{
    m_r[n] = m_mach;
}

/*  STS     MACL,Rn */
void ShCpu::STSMACL(uint32_t n)
{
    m_r[n] = m_macl;
}

/*  STS     PR,Rn */
void ShCpu::STSPR(uint32_t n)
{
    m_r[n] = m_pr;
}

/*  STS.L   MACH,@-Rn */
void ShCpu::STSMMACH(uint32_t n)
{
    m_r[n] -= 4;
    m_ea = m_r[n];
    write_long(m_ea, m_mach);
}

/*  STS.L   MACL,@-Rn */
void ShCpu::STSMMACL(uint32_t n)
{
    m_r[n] -= 4;
    m_ea = m_r[n];
    write_long(m_ea, m_macl);
}

/*  STS.L   PR,@-Rn */
void ShCpu::STSMPR(uint32_t n)
{
    m_r[n] -= 4;
    m_ea = m_r[n];
    write_long(m_ea, m_pr);
}

/*  SUB     Rm,Rn */
void ShCpu::SUB(uint32_t m, uint32_t n)
{
    m_r[n] -= m_r[m];
}

/*  SUBC    Rm,Rn */
void ShCpu::SUBC(uint32_t m, uint32_t n)
{
    uint32_t tmp1 = m_r[n] - m_r[m];
    uint32_t tmp0 = m_r[n];
    m_r[n] = tmp1 - (m_sr & SH_T);
    if (tmp0 < tmp1)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
    if (tmp1 < m_r[n])
        m_sr |= SH_T;
}

/*  SUBV    Rm,Rn */
void ShCpu::SUBV(uint32_t m, uint32_t n)
{
    int32_t dest = bit31(m_r[n]);
    int32_t src = bit31(m_r[m]);
    src += dest;

    m_r[n] -= m_r[m];

    int32_t ans = bit31(m_r[n]);
    ans += dest;

    if (src == 1 && ans == 1)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  SWAP.B  Rm,Rn */
void ShCpu::SWAPB(uint32_t m, uint32_t n)
{
    uint32_t temp = m_r[m] & 0xffff0000;
    temp |= (m_r[m] & 0x000000ff) << 8;
    m_r[n] = uint8_t(m_r[m] >> 8);
    m_r[n] = m_r[n] | temp;
}

/*  SWAP.W  Rm,Rn */
void ShCpu::SWAPW(uint32_t m, uint32_t n)
{
    uint32_t temp = m_r[m] >> 16;
    m_r[n] = (m_r[m] << 16) | temp;
}

/*  TAS.B   @Rn */
void ShCpu::TAS(uint32_t n)
{
    m_ea = m_r[n];

    /* Bus Lock enable */
    uint32_t temp = read_byte(m_ea);
    if (temp == 0)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
    temp |= 0x80;
    /* Bus Lock disable */
    write_byte(m_ea, uint8_t(temp));
    m_icount -= 3;
}

/*  TST     Rm,Rn */
void ShCpu::TST(uint32_t m, uint32_t n)
{
    if ((m_r[n] & m_r[m]) == 0)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  TST     #imm,R0 */
void ShCpu::TSTI(uint32_t i)
{
    uint32_t imm = i & 0xff;

    if ((imm & m_r[0]) == 0)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
}

/*  TST.B   #imm,@(R0,GBR) */
void ShCpu::TSTM(uint32_t i)
{
    uint32_t imm = i & 0xff;

    m_ea = m_gbr + m_r[0];
    if ((imm & read_byte(m_ea)) == 0)
        m_sr |= SH_T;
    else
        m_sr &= ~SH_T;
    m_icount -= 2;
}

/*  XOR     Rm,Rn */
void ShCpu::XOR(uint32_t m, uint32_t n)
{
    m_r[n] ^= m_r[m];
}

/*  XOR     #imm,R0 */
void ShCpu::XORI(uint32_t i)
{
    m_r[0] ^= i & 0x000000ff;
}

/*  XOR.B   #imm,@(R0,GBR) */
void ShCpu::XORM(uint32_t i)
{
    m_ea = m_gbr + m_r[0];
    write_byte(m_ea, read_byte(m_ea) ^ uint8_t(i));
    m_icount -= 2;
}

/*  XTRCT   Rm,Rn */
void ShCpu::XTRCT(uint32_t m, uint32_t n)
{
    m_r[n] = (m_r[n] >> 16) | (m_r[m] << 16);
}

/*  SLEEP */
void ShCpu::SLEEP()
{
    /* 0 = normal mode */
    /* 1 = enters into power-down mode */
    /* 2 = go out the power-down mode after an exception */
    if (m_sleep_mode != 2)
        m_pc -= 2;
    m_icount -= 2;
    /* Wait_for_exception; */
    if (m_sleep_mode == 0)
        m_sleep_mode = 1;
    else if (m_sleep_mode == 2)
        m_sleep_mode = 0;
}

/***************************************************************************
    sh2_device overrides
***************************************************************************/

/*  LDC.L   @Rm+,SR */
void ShCpu::LDCMSR(uint16_t opcode) // passes Rn
{
    const uint32_t rn = REG_N;
    m_ea = m_r[rn];
    m_sr = read_long(m_ea) & SH_FLAGS;
    m_r[rn] += 4;
    m_icount -= 2;
    m_test_irq = 1;
}

/*  LDC     Rm,SR */
void ShCpu::LDCSR(uint16_t opcode) // passes Rn
{
    m_sr = m_r[REG_N] & SH_FLAGS;
    m_test_irq = 1;
}

/*  RTE */
void ShCpu::RTE()
{
    m_ea = m_r[15];
    m_delay = read_long(m_ea);
    m_r[15] += 4;
    m_ea = m_r[15];
    m_sr = read_long(m_ea) & SH_FLAGS;
    m_r[15] += 4;
    m_icount -= 3;
    m_test_irq = 1;
}

/*  TRAPA   #imm */
void ShCpu::TRAPA(uint32_t i)
{
    uint32_t imm = i & 0xff;

    m_ea = m_vbr + imm * 4;

    m_r[15] -= 4;
    write_long(m_r[15], m_sr);
    m_r[15] -= 4;
    write_long(m_r[15], m_pc);

    m_pc = read_long(m_ea);

    m_icount -= 7;
}

/*  ILLEGAL */
void ShCpu::ILLEGAL()
{
    m_r[15] -= 4;
    write_long(m_r[15], m_sr);     /* push SR onto stack */
    m_r[15] -= 4;
    write_long(m_r[15], m_pc - 2); /* push PC onto stack */

    /* fetch PC */
    m_pc = read_long(m_vbr + 4 * 4);

    /* TODO: timing is a guess */
    m_icount -= 5;
}

/***************************************************************************
    Dispatch (sh_common_execution::execute_one and friends; F-line goes to
    sh2_device::execute_one_f000, which is ILLEGAL)
***************************************************************************/

void ShCpu::op0010(uint16_t opcode)
{
    switch (opcode & 15)
    {
    case  0: MOVBS(REG_M, REG_N);   break;
    case  1: MOVWS(REG_M, REG_N);   break;
    case  2: MOVLS(REG_M, REG_N);   break;
    case  3: ILLEGAL();             break;
    case  4: MOVBM(REG_M, REG_N);   break;
    case  5: MOVWM(REG_M, REG_N);   break;
    case  6: MOVLM(REG_M, REG_N);   break;
    case  7: DIV0S(REG_M, REG_N);   break;
    case  8: TST(REG_M, REG_N);     break;
    case  9: AND(REG_M, REG_N);     break;
    case 10: XOR(REG_M, REG_N);     break;
    case 11: OR(REG_M, REG_N);      break;
    case 12: CMPSTR(REG_M, REG_N);  break;
    case 13: XTRCT(REG_M, REG_N);   break;
    case 14: MULU(REG_M, REG_N);    break;
    case 15: MULS(REG_M, REG_N);    break;
    }
}

void ShCpu::op0011(uint16_t opcode)
{
    switch (opcode & 15)
    {
    case  0: CMPEQ(REG_M, REG_N);   break;
    case  1: ILLEGAL();             break;
    case  2: CMPHS(REG_M, REG_N);   break;
    case  3: CMPGE(REG_M, REG_N);   break;
    case  4: DIV1(REG_M, REG_N);    break;
    case  5: DMULU(REG_M, REG_N);   break;
    case  6: CMPHI(REG_M, REG_N);   break;
    case  7: CMPGT(REG_M, REG_N);   break;
    case  8: SUB(REG_M, REG_N);     break;
    case  9: ILLEGAL();             break;
    case 10: SUBC(REG_M, REG_N);    break;
    case 11: SUBV(REG_M, REG_N);    break;
    case 12: ADD(REG_M, REG_N);     break;
    case 13: DMULS(REG_M, REG_N);   break;
    case 14: ADDC(REG_M, REG_N);    break;
    case 15: ADDV(REG_M, REG_N);    break;
    }
}

void ShCpu::op0110(uint16_t opcode)
{
    switch (opcode & 15)
    {
    case  0: MOVBL(REG_M, REG_N);   break;
    case  1: MOVWL(REG_M, REG_N);   break;
    case  2: MOVLL(REG_M, REG_N);   break;
    case  3: MOV(REG_M, REG_N);     break;
    case  4: MOVBP(REG_M, REG_N);   break;
    case  5: MOVWP(REG_M, REG_N);   break;
    case  6: MOVLP(REG_M, REG_N);   break;
    case  7: NOT(REG_M, REG_N);     break;
    case  8: SWAPB(REG_M, REG_N);   break;
    case  9: SWAPW(REG_M, REG_N);   break;
    case 10: NEGC(REG_M, REG_N);    break;
    case 11: NEG(REG_M, REG_N);     break;
    case 12: EXTUB(REG_M, REG_N);   break;
    case 13: EXTUW(REG_M, REG_N);   break;
    case 14: EXTSB(REG_M, REG_N);   break;
    case 15: EXTSW(REG_M, REG_N);   break;
    }
}

void ShCpu::op1000(uint16_t opcode)
{
    switch ((opcode >> 8) & 15)
    {
    case  0: MOVBS4(opcode & 0x0f, REG_M);  break;
    case  1: MOVWS4(opcode & 0x0f, REG_M);  break;
    case  2: ILLEGAL();                     break;
    case  3: ILLEGAL();                     break;
    case  4: MOVBL4(REG_M, opcode & 0x0f);  break;
    case  5: MOVWL4(REG_M, opcode & 0x0f);  break;
    case  6: ILLEGAL();                     break;
    case  7: ILLEGAL();                     break;
    case  8: CMPIM(opcode & 0xff);          break;
    case  9: BT(opcode & 0xff);             break;
    case 10: ILLEGAL();                     break;
    case 11: BF(opcode & 0xff);             break;
    case 12: ILLEGAL();                     break;
    case 13: BTS(opcode & 0xff);            break;
    case 14: ILLEGAL();                     break;
    case 15: BFS(opcode & 0xff);            break;
    }
}

void ShCpu::op1100(uint16_t opcode)
{
    switch ((opcode >> 8) & 15)
    {
    case  0: MOVBSG(opcode & 0xff);     break;
    case  1: MOVWSG(opcode & 0xff);     break;
    case  2: MOVLSG(opcode & 0xff);     break;
    case  3: TRAPA(opcode & 0xff);      break;
    case  4: MOVBLG(opcode & 0xff);     break;
    case  5: MOVWLG(opcode & 0xff);     break;
    case  6: MOVLLG(opcode & 0xff);     break;
    case  7: MOVA(opcode & 0xff);       break;
    case  8: TSTI(opcode & 0xff);       break;
    case  9: ANDI(opcode & 0xff);       break;
    case 10: XORI(opcode & 0xff);       break;
    case 11: ORI(opcode & 0xff);        break;
    case 12: TSTM(opcode & 0xff);       break;
    case 13: ANDM(opcode & 0xff);       break;
    case 14: XORM(opcode & 0xff);       break;
    case 15: ORM(opcode & 0xff);        break;
    }
}

void ShCpu::execute_one_0000(uint16_t opcode)
{
    switch (opcode & 0x3f)
    {
    case 0x00: ILLEGAL();               break;
    case 0x01: ILLEGAL();               break;
    case 0x02: STCSR(REG_N);            break;
    case 0x03: BSRF(REG_N);             break;
    case 0x04: MOVBS0(REG_M, REG_N);    break;
    case 0x05: MOVWS0(REG_M, REG_N);    break;
    case 0x06: MOVLS0(REG_M, REG_N);    break;
    case 0x07: MULL(REG_M, REG_N);      break;
    case 0x08: CLRT();                  break;
    case 0x09: NOP();                   break;
    case 0x0a: STSMACH(REG_N);          break;
    case 0x0b: RTS();                   break;
    case 0x0c: MOVBL0(REG_M, REG_N);    break;
    case 0x0d: MOVWL0(REG_M, REG_N);    break;
    case 0x0e: MOVLL0(REG_M, REG_N);    break;
    case 0x0f: MAC_L(REG_M, REG_N);     break;

    case 0x10: ILLEGAL();               break;
    case 0x11: ILLEGAL();               break;
    case 0x12: STCGBR(REG_N);           break;
    case 0x13: ILLEGAL();               break;
    case 0x14: MOVBS0(REG_M, REG_N);    break;
    case 0x15: MOVWS0(REG_M, REG_N);    break;
    case 0x16: MOVLS0(REG_M, REG_N);    break;
    case 0x17: MULL(REG_M, REG_N);      break;
    case 0x18: SETT();                  break;
    case 0x19: DIV0U();                 break;
    case 0x1a: STSMACL(REG_N);          break;
    case 0x1b: SLEEP();                 break;
    case 0x1c: MOVBL0(REG_M, REG_N);    break;
    case 0x1d: MOVWL0(REG_M, REG_N);    break;
    case 0x1e: MOVLL0(REG_M, REG_N);    break;
    case 0x1f: MAC_L(REG_M, REG_N);     break;

    case 0x20: ILLEGAL();               break;
    case 0x21: ILLEGAL();               break;
    case 0x22: STCVBR(REG_N);           break;
    case 0x23: BRAF(REG_N);             break;
    case 0x24: MOVBS0(REG_M, REG_N);    break;
    case 0x25: MOVWS0(REG_M, REG_N);    break;
    case 0x26: MOVLS0(REG_M, REG_N);    break;
    case 0x27: MULL(REG_M, REG_N);      break;
    case 0x28: CLRMAC();                break;
    case 0x29: MOVT(REG_N);             break;
    case 0x2a: STSPR(REG_N);            break;
    case 0x2b: RTE();                   break;
    case 0x2c: MOVBL0(REG_M, REG_N);    break;
    case 0x2d: MOVWL0(REG_M, REG_N);    break;
    case 0x2e: MOVLL0(REG_M, REG_N);    break;
    case 0x2f: MAC_L(REG_M, REG_N);     break;

    case 0x30: ILLEGAL();               break;
    case 0x31: ILLEGAL();               break;
    case 0x32: ILLEGAL();               break;
    case 0x33: ILLEGAL();               break;
    case 0x34: MOVBS0(REG_M, REG_N);    break;
    case 0x35: MOVWS0(REG_M, REG_N);    break;
    case 0x36: MOVLS0(REG_M, REG_N);    break;
    case 0x37: MULL(REG_M, REG_N);      break;
    case 0x38: ILLEGAL();               break;
    case 0x39: ILLEGAL();               break;
    case 0x3a: ILLEGAL();               break;
    case 0x3b: ILLEGAL();               break;
    case 0x3c: MOVBL0(REG_M, REG_N);    break;
    case 0x3d: MOVWL0(REG_M, REG_N);    break;
    case 0x3e: MOVLL0(REG_M, REG_N);    break;
    case 0x3f: MAC_L(REG_M, REG_N);     break;
    }
}

void ShCpu::execute_one_4000(uint16_t opcode)
{
    switch (opcode & 0x3f)
    {
    case 0x00: SHLL(REG_N);         break;
    case 0x01: SHLR(REG_N);         break;
    case 0x02: STSMMACH(REG_N);     break;
    case 0x03: STCMSR(REG_N);       break;
    case 0x04: ROTL(REG_N);         break;
    case 0x05: ROTR(REG_N);         break;
    case 0x06: LDSMMACH(REG_N);     break;
    case 0x07: LDCMSR(opcode);      break;
    case 0x08: SHLL2(REG_N);        break;
    case 0x09: SHLR2(REG_N);        break;
    case 0x0a: LDSMACH(REG_N);      break;
    case 0x0b: JSR(REG_N);          break;
    case 0x0c: ILLEGAL();           break;
    case 0x0d: ILLEGAL();           break;
    case 0x0e: LDCSR(opcode);       break;
    case 0x0f: MAC_W(REG_M, REG_N); break;

    case 0x10: DT(REG_N);           break;
    case 0x11: CMPPZ(REG_N);        break;
    case 0x12: STSMMACL(REG_N);     break;
    case 0x13: STCMGBR(REG_N);      break;
    case 0x14: ILLEGAL();           break;
    case 0x15: CMPPL(REG_N);        break;
    case 0x16: LDSMMACL(REG_N);     break;
    case 0x17: LDCMGBR(REG_N);      break;
    case 0x18: SHLL8(REG_N);        break;
    case 0x19: SHLR8(REG_N);        break;
    case 0x1a: LDSMACL(REG_N);      break;
    case 0x1b: TAS(REG_N);          break;
    case 0x1c: ILLEGAL();           break;
    case 0x1d: ILLEGAL();           break;
    case 0x1e: LDCGBR(REG_N);       break;
    case 0x1f: MAC_W(REG_M, REG_N); break;

    case 0x20: SHAL(REG_N);         break;
    case 0x21: SHAR(REG_N);         break;
    case 0x22: STSMPR(REG_N);       break;
    case 0x23: STCMVBR(REG_N);      break;
    case 0x24: ROTCL(REG_N);        break;
    case 0x25: ROTCR(REG_N);        break;
    case 0x26: LDSMPR(REG_N);       break;
    case 0x27: LDCMVBR(REG_N);      break;
    case 0x28: SHLL16(REG_N);       break;
    case 0x29: SHLR16(REG_N);       break;
    case 0x2a: LDSPR(REG_N);        break;
    case 0x2b: JMP(REG_N);          break;
    case 0x2c: ILLEGAL();           break;
    case 0x2d: ILLEGAL();           break;
    case 0x2e: LDCVBR(REG_N);       break;
    case 0x2f: MAC_W(REG_M, REG_N); break;

    case 0x3f: MAC_W(REG_M, REG_N); break;
    default:   ILLEGAL();           break;   // 0x30-0x3e
    }
}

void ShCpu::execute_one(uint16_t opcode)
{
    switch ((opcode >> 12) & 15)
    {
        case  0: execute_one_0000(opcode);              break;
        case  1: MOVLS4(REG_M, opcode & 0xf, REG_N);    break;
        case  2: op0010(opcode);                        break;
        case  3: op0011(opcode);                        break;
        case  4: execute_one_4000(opcode);              break;
        case  5: MOVLL4(REG_M, opcode & 0x0f, REG_N);   break;
        case  6: op0110(opcode);                        break;
        case  7: ADDI(opcode & 0xff, REG_N);            break;
        case  8: op1000(opcode);                        break;
        case  9: MOVWI(opcode & 0xff, REG_N);           break;
        case 10: BRA(opcode & 0xfff);                   break;
        case 11: BSR(opcode & 0xfff);                   break;
        case 12: op1100(opcode);                        break;
        case 13: MOVLI(opcode & 0xff, REG_N);           break;
        case 14: MOVI(opcode & 0xff, REG_N);            break;
        case 15: ILLEGAL();                             break;  // sh2_device::execute_one_f000
    }
}
