/***************************************************************************
    Hitachi SH-1 / SH-2 CPU core: a standalone C++ class holding MAME's SH-2 family
    *interpreter* (not the DRC), used for the SH7032 (SH-1) sound/sub CPU. The aim is a
    faithful transcription: instruction semantics, icount decrements, delay-slot handling,
    interrupt-check timing and exception stacking all follow MAME so that the core can be
    validated against MAME instruction by instruction.

    Transcribed from MAME 0.264 src/devices/cpu/sh/sh.cpp, sh2.cpp and sh7032.cpp
    (BSD-3-Clause; copyright holders Juergen Buchmueller, R. Belmont, David Haywood,
    Angelo Salese).

    What is left out on purpose:
      - all DRC code;
      - the SH7032 on-chip register block at 0x05fffe00-0x05ffffff: in MAME 0.264 it is a
        plain 256-word RAM in the CPU's internal map, so the bus provides it;
      - internal interrupt sources (MAME's SH7032 has none; internal_irq_level stays -1).

    Bus contract: the core applies MAME's address masking (the rule in sh2_device's
    read_xxx/write_xxx functions and the one in its opcode fetch) and then the address-space
    width mask (28 address lines for the SH-1, 32 for the SH-2) before calling the bus, so
    the bus sees the same addresses as MAME's address map handlers. MAME's 32-bit big-endian space forces word/long accesses to their
    natural alignment (the low address bits are ignored); doing that is up to the bus.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

class ShCpu
{
public:
    // Big-endian 32-bit bus. Addresses arrive already masked by the core as MAME does.
    struct Bus
    {
        virtual ~Bus() {}
        virtual uint8_t  read8(uint32_t a) = 0;
        virtual uint16_t read16(uint32_t a) = 0;
        virtual uint32_t read32(uint32_t a) = 0;
        virtual void     write8(uint32_t a, uint8_t v) = 0;
        virtual void     write16(uint32_t a, uint16_t v) = 0;
        virtual void     write32(uint32_t a, uint32_t v) = 0;
        virtual uint16_t fetch16(uint32_t a) { return read16(a); }  // opcode fetch

        // Called where MAME calls standard_irq_callback(irqline, pc) in sh2_exception, i.e.
        // when an external IRQ is accepted, before SR/PC are stacked. To emulate MAME's
        // HOLD_LINE, call set_irq_line(line, false) from here (MAME does exactly that
        // synchronously for a line held with HOLD_LINE).
        virtual void irq_acknowledge(int line) { (void)line; }

        // Called before every instruction (MAME's debugger_instruction_hook) when enabled
        // with ShCpu::set_instr_hook(true). pc is the address of the instruction about to run.
        virtual void instr_hook(uint32_t pc) { (void)pc; }
    };

    // MAME line states, for set_irq_state()
    enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };

    // sh1: SH7032 defaults (28 address lines); otherwise 32 address lines (SH7604).
    // addr_lines < 0 picks that default.
    explicit ShCpu(Bus& bus, bool sh1 = true, uint32_t address_mask = 0xc7ffffff, int addr_lines = -1);

    void reset();                         // sh2_device::device_reset (PC/SP from vectors 0/4)
    int  run(int cycles);                 // sh2_device::execute_run (interpreter); returns cycles used (may overshoot)
    void end_timeslice();                 // like MAME's abort_timeslice: run() returns after the current instruction

    void set_irq_line(int line, bool asserted) { set_irq_state(line, asserted ? ASSERT_LINE : CLEAR_LINE); }
    void set_irq_state(int line, int state);  // execute_set_input for lines 0..15, MAME line state values
    void set_nmi(bool asserted) { set_nmi_state(asserted ? ASSERT_LINE : CLEAR_LINE); }
    void set_nmi_state(int state);            // execute_set_input(INPUT_LINE_NMI, state)

    void set_instr_hook(bool on) { m_hook = on; }

    // State access for debugging and validation
    uint32_t pc() const   { return m_pc; }
    uint32_t reg(int n) const { return m_r[n & 15]; }
    uint32_t sr() const   { return m_sr; }
    uint32_t pr() const   { return m_pr; }
    uint32_t gbr() const  { return m_gbr; }
    uint32_t vbr() const  { return m_vbr; }
    uint32_t mach() const { return m_mach; }
    uint32_t macl() const { return m_macl; }
    uint32_t ea() const   { return m_ea; }
    uint32_t delay() const { return m_delay; }      // pending delayed-branch target (0 = none)
    uint32_t pending_irq() const { return m_pending_irq; }
    int  icount() const   { return m_icount; }      // remaining cycles in the current run()
    int  sleep_mode() const { return m_sleep_mode; }

    // Writing PC behaves like MAME's state_import for PC: it cancels a pending delay slot.
    void set_pc(uint32_t v) { m_pc = v; m_delay = 0; }
    void set_reg(int n, uint32_t v) { m_r[n & 15] = v; }
    // Writing SR behaves like MAME's state_import for SR: it checks for pending interrupts.
    void set_sr(uint32_t v) { m_sr = v; check_pending_irq(); }

private:
    Bus& m_bus;
    const uint32_t m_am;          // sh2_device::m_am
    const uint32_t m_space_mask;  // address space width (28 or 32 lines)
    bool m_hook = false;

    // internal_sh2_state (interpreter fields)
    uint32_t m_pc = 0, m_pr = 0, m_sr = 0, m_mach = 0, m_macl = 0;
    uint32_t m_r[16] = {};
    uint32_t m_ea = 0;
    uint32_t m_pending_irq = 0;
    uint32_t m_pending_nmi = 0;
    int      m_internal_irq_level = 0;
    int      m_icount = 0;
    uint8_t  m_sleep_mode = 0;
    uint32_t m_gbr = 0, m_vbr = 0;
    uint32_t m_delay = 0;

    // sh2_device
    uint32_t m_test_irq = 0;
    int32_t  m_internal_irq_vector = 0;
    int8_t   m_nmi_line_state = 0;
    uint32_t m_cpu_off = 0;
    int8_t   m_irq_line_state[17] = {};

    int m_stolen = 0;             // cycles removed by end_timeslice() during run()

    // memory access (sh2_device::read_xxx and write_xxx)
    uint8_t  read_byte(uint32_t offset);
    uint16_t read_word(uint32_t offset);
    uint32_t read_long(uint32_t offset);
    void     write_byte(uint32_t offset, uint8_t data);
    void     write_word(uint32_t offset, uint16_t data);
    void     write_long(uint32_t offset, uint32_t data);

    void check_pending_irq();
    void sh2_exception(int irqline);
    void sh2_exception_internal(int irqline, int vector);

    void execute_one(uint16_t opcode);
    void execute_one_0000(uint16_t opcode);
    void execute_one_4000(uint16_t opcode);
    void op0010(uint16_t opcode);
    void op0011(uint16_t opcode);
    void op0110(uint16_t opcode);
    void op1000(uint16_t opcode);
    void op1100(uint16_t opcode);

    void ADD(uint32_t m, uint32_t n);
    void ADDI(uint32_t i, uint32_t n);
    void ADDC(uint32_t m, uint32_t n);
    void ADDV(uint32_t m, uint32_t n);
    void AND(uint32_t m, uint32_t n);
    void ANDI(uint32_t i);
    void ANDM(uint32_t i);
    void BF(uint32_t d);
    void BFS(uint32_t d);
    void BRA(uint32_t d);
    void BRAF(uint32_t m);
    void BSR(uint32_t d);
    void BSRF(uint32_t m);
    void BT(uint32_t d);
    void BTS(uint32_t d);
    void CLRMAC();
    void CLRT();
    void CMPEQ(uint32_t m, uint32_t n);
    void CMPGE(uint32_t m, uint32_t n);
    void CMPGT(uint32_t m, uint32_t n);
    void CMPHI(uint32_t m, uint32_t n);
    void CMPHS(uint32_t m, uint32_t n);
    void CMPPL(uint32_t n);
    void CMPPZ(uint32_t n);
    void CMPSTR(uint32_t m, uint32_t n);
    void CMPIM(uint32_t i);
    void DIV0S(uint32_t m, uint32_t n);
    void DIV0U();
    void DIV1(uint32_t m, uint32_t n);
    void DMULS(uint32_t m, uint32_t n);
    void DMULU(uint32_t m, uint32_t n);
    void DT(uint32_t n);
    void EXTSB(uint32_t m, uint32_t n);
    void EXTSW(uint32_t m, uint32_t n);
    void EXTUB(uint32_t m, uint32_t n);
    void EXTUW(uint32_t m, uint32_t n);
    void JMP(uint32_t m);
    void JSR(uint32_t m);
    void LDCGBR(uint32_t m);
    void LDCVBR(uint32_t m);
    void LDCMGBR(uint32_t m);
    void LDCMVBR(uint32_t m);
    void LDSMACH(uint32_t m);
    void LDSMACL(uint32_t m);
    void LDSPR(uint32_t m);
    void LDSMMACH(uint32_t m);
    void LDSMMACL(uint32_t m);
    void LDSMPR(uint32_t m);
    void MAC_L(uint32_t m, uint32_t n);
    void MAC_W(uint32_t m, uint32_t n);
    void MOV(uint32_t m, uint32_t n);
    void MOVBS(uint32_t m, uint32_t n);
    void MOVWS(uint32_t m, uint32_t n);
    void MOVLS(uint32_t m, uint32_t n);
    void MOVBL(uint32_t m, uint32_t n);
    void MOVWL(uint32_t m, uint32_t n);
    void MOVLL(uint32_t m, uint32_t n);
    void MOVBM(uint32_t m, uint32_t n);
    void MOVWM(uint32_t m, uint32_t n);
    void MOVLM(uint32_t m, uint32_t n);
    void MOVBP(uint32_t m, uint32_t n);
    void MOVWP(uint32_t m, uint32_t n);
    void MOVLP(uint32_t m, uint32_t n);
    void MOVBS0(uint32_t m, uint32_t n);
    void MOVWS0(uint32_t m, uint32_t n);
    void MOVLS0(uint32_t m, uint32_t n);
    void MOVBL0(uint32_t m, uint32_t n);
    void MOVWL0(uint32_t m, uint32_t n);
    void MOVLL0(uint32_t m, uint32_t n);
    void MOVI(uint32_t i, uint32_t n);
    void MOVWI(uint32_t d, uint32_t n);
    void MOVLI(uint32_t d, uint32_t n);
    void MOVBLG(uint32_t d);
    void MOVWLG(uint32_t d);
    void MOVLLG(uint32_t d);
    void MOVBSG(uint32_t d);
    void MOVWSG(uint32_t d);
    void MOVLSG(uint32_t d);
    void MOVBS4(uint32_t d, uint32_t n);
    void MOVWS4(uint32_t d, uint32_t n);
    void MOVLS4(uint32_t m, uint32_t d, uint32_t n);
    void MOVBL4(uint32_t m, uint32_t d);
    void MOVWL4(uint32_t m, uint32_t d);
    void MOVLL4(uint32_t m, uint32_t d, uint32_t n);
    void MOVA(uint32_t d);
    void MOVT(uint32_t n);
    void MULL(uint32_t m, uint32_t n);
    void MULS(uint32_t m, uint32_t n);
    void MULU(uint32_t m, uint32_t n);
    void NEG(uint32_t m, uint32_t n);
    void NEGC(uint32_t m, uint32_t n);
    void NOP();
    void NOT(uint32_t m, uint32_t n);
    void OR(uint32_t m, uint32_t n);
    void ORI(uint32_t i);
    void ORM(uint32_t i);
    void ROTCL(uint32_t n);
    void ROTCR(uint32_t n);
    void ROTL(uint32_t n);
    void ROTR(uint32_t n);
    void RTS();
    void SETT();
    void SHAL(uint32_t n);
    void SHAR(uint32_t n);
    void SHLL(uint32_t n);
    void SHLL2(uint32_t n);
    void SHLL8(uint32_t n);
    void SHLL16(uint32_t n);
    void SHLR(uint32_t n);
    void SHLR2(uint32_t n);
    void SHLR8(uint32_t n);
    void SHLR16(uint32_t n);
    void SLEEP();
    void STCSR(uint32_t n);
    void STCGBR(uint32_t n);
    void STCVBR(uint32_t n);
    void STCMSR(uint32_t n);
    void STCMGBR(uint32_t n);
    void STCMVBR(uint32_t n);
    void STSMACH(uint32_t n);
    void STSMACL(uint32_t n);
    void STSPR(uint32_t n);
    void STSMMACH(uint32_t n);
    void STSMMACL(uint32_t n);
    void STSMPR(uint32_t n);
    void SUB(uint32_t m, uint32_t n);
    void SUBC(uint32_t m, uint32_t n);
    void SUBV(uint32_t m, uint32_t n);
    void SWAPB(uint32_t m, uint32_t n);
    void SWAPW(uint32_t m, uint32_t n);
    void TAS(uint32_t n);
    void TST(uint32_t m, uint32_t n);
    void TSTI(uint32_t i);
    void TSTM(uint32_t i);
    void XOR(uint32_t m, uint32_t n);
    void XORI(uint32_t i);
    void XORM(uint32_t i);
    void XTRCT(uint32_t m, uint32_t n);

    // sh2_device overrides
    void RTE();
    void LDCSR(uint16_t opcode);
    void LDCMSR(uint16_t opcode);
    void TRAPA(uint32_t i);
    void ILLEGAL();
};
