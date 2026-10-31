/***************************************************************************
    NEC V60 / V70 CPU core: MAME's interpreter as a standalone C++ class, configured as
    MAME's v70_device (the main CPU of Sega's System Multi 32, 20 MHz on OutRunners).

    Transcribed from MAME 0.264 src/devices/cpu/v60 (v60.cpp, v60.h and the am*.hxx /
    op*.hxx / optable.hxx files). license:BSD-3-Clause, copyright-holders:Farfetch'd,
    R. Belmont. The .hxx files in this directory are MAME's, verbatim except for the class
    name (v60_device -> V60Cpu); v60.cpp is MAME's v60.cpp with the device framework
    replaced. Function bodies use MAME's types, so the integer/float promotions,
    truncations and evaluation order are the same. Every deviation is marked "PORT:".

    Bus contract (see V60Bus below): the core makes exactly the accesses MAME's
    address_space / memory_access cache make for MAME's V70 spaces:
      - program space: 32-bit data, 32-bit address, little-endian. Byte, word and dword
        accesses (aligned and unaligned) are split into dword-aligned native accesses with
        a byte-lane mem_mask by a transcription of emumem.h memory_read_generic /
        memory_write_generic. The address passed on is the full 32-bit byte address of the
        dword (unmasked: MAME's map global_mask, $FFFFFF on Multi 32, is the board's job).
      - opcode fetches (MAME's m_cache32) go through the same decomposition but call
        V60Bus::fetch32. In MAME a cache read of a handler-mapped address calls the handler
        (memory_access_cache::read_native -> m_cache_r->read), so fetch32 must behave like
        read32 there; its default does exactly that.
      - io space: 16-bit data, 24-bit address, little-endian (m_io_config in v60.cpp is
        ("io", ENDIANNESS_LITTLE, 16, 24, 0) for the V70 too). Word-aligned native accesses;
        the core applies the space's 24-bit address mask, as address_space::read_native
        does.
    For speed the board can hand the core direct pointers for ROM / RAM ranges
    (map_direct). Accesses to those pages skip the bus and behave like MAME's memory
    handlers (handler_entry_read_memory / handler_entry_write_memory): the full native
    dword is read; a write merges data under mem_mask. Nothing else changes.

    Scheduling contract: like MAME's scheduler, the board runs the core in timeslices with
    run() (or by setting m_icount and calling execute_run()). Interrupt line changes must
    be applied between instructions, outside execute_run(), as MAME's input-line event
    queue does: a bus handler that changes a line during an instruction (e.g. an
    interrupt controller acknowledge) must queue the change, call abort_timeslice() and
    apply it with set_input_line*() after run() returns. Applying a change calls
    v60_try_irq() at once, as MAME does, so the interrupt is taken (PSW/PC pushed) right
    then.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "v60mem.hpp"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "V60Cpu direct pages assume a little-endian host" // PORT: MAME's memory pointers are native-endian u32 arrays; here they are byte arrays in address order
#endif

#if defined(__GNUC__)
#define V60_PRINTF_FORMAT(f, a) __attribute__((format(printf, f, a)))
#else
#define V60_PRINTF_FORMAT(f, a)
#endif

// The board side of the V70. All addresses are byte addresses.
struct V60Bus
{
    virtual ~V60Bus() {}

    // Program space native accesses: addr has its low 2 bits clear; mem_mask bits 0-7 select
    // the byte at addr+0, bits 24-31 the byte at addr+3. A read returns the whole dword (the
    // core shifts out the lanes it wants), as MAME handlers do.
    virtual uint32_t read32(uint32_t addr, uint32_t mem_mask) = 0;
    virtual void     write32(uint32_t addr, uint32_t data, uint32_t mem_mask) = 0;

    // Opcode / operand-specifier fetches (MAME's m_cache32). Same rules as read32.
    virtual uint32_t fetch32(uint32_t addr, uint32_t mem_mask) { return read32(addr, mem_mask); }

    // IO space native accesses (16-bit, little-endian): addr has bit 0 clear and is masked to
    // 24 bits. MAME's Multi 32 maps nothing there: unmapped reads return 0.
    virtual uint16_t io_read16(uint32_t addr, uint16_t mem_mask) { (void)addr; (void)mem_mask; return 0; }
    virtual void     io_write16(uint32_t addr, uint16_t data, uint16_t mem_mask) { (void)addr; (void)data; (void)mem_mask; }

    // MAME's driver IRQ acknowledge callback (set_irq_acknowledge_callback): called from
    // standard_irq_callback after the default handling (HOLD_LINE clear); 'vector' is the one
    // set with the line. Returning it unchanged is MAME's behaviour without a driver callback
    // (Multi 32 has none).
    virtual int irq_acknowledge(int irqline, int vector) { (void)irqline; return vector; }

    // Debugger hooks (MAME's debugger_instruction_hook / interrupt_hook / exception_hook).
    // The instruction hook is only called when enabled with V60Cpu::set_instruction_hook().
    virtual void debug_instruction_hook(uint32_t pc) { (void)pc; }
    virtual void debug_interrupt_hook(int irqline, uint32_t pc) { (void)irqline; (void)pc; }
    virtual void debug_exception_hook(int exception) { (void)exception; }

    // MAME's logerror() from the core (unimplemented / skipped opcodes)
    virtual void log(const char* text) { (void)text; }
};

// Native accessors of the spaces (v60mem.hpp): direct page or bus.
template <bool Fetch> inline v60mem::u32 v60mem::Space32<Fetch>::read_native(offs_t address, NativeType mask)
{
    const DirectPage& p = m_pages[(address & m_pagemask) >> 16];
    if (p.r)
    {
        // handler_entry_read_memory::read: the whole native word
        u32 v;
        std::memcpy(&v, p.r + (address & 0xfffc), 4);
        return v;
    }
    if constexpr (Fetch)
        return m_bus->fetch32(address, mask);
    else
        return m_bus->read32(address, mask);
}

template <bool Fetch> inline void v60mem::Space32<Fetch>::write_native(offs_t address, NativeType data, NativeType mask)
{
    const DirectPage& p = m_pages[(address & m_pagemask) >> 16];
    if (p.w)
    {
        // handler_entry_write_memory::write: m_base[offset] = (m_base[offset] & ~mem_mask) | (data & mem_mask)
        u32 v;
        std::memcpy(&v, p.w + (address & 0xfffc), 4);
        v = (v & ~mask) | (data & mask);
        std::memcpy(p.w + (address & 0xfffc), &v, 4);
        return;
    }
    m_bus->write32(address, data, mask);
}

inline v60mem::u16 v60mem::SpaceIo16::read_native(offs_t offset, NativeType mask)
{
    return m_bus->io_read16(offset & m_addrmask, mask);
}

inline void v60mem::SpaceIo16::write_native(offs_t offset, NativeType data, NativeType mask)
{
    m_bus->io_write16(offset & m_addrmask, data, mask);
}

class V60Cpu
{
public:
    typedef uint32_t offs_t;

    // MAME line states and input lines (emu/diexec.h)
    enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };
    enum { MAX_INPUT_LINES = 64 + 3, INPUT_LINE_IRQ0 = 0, INPUT_LINE_NMI = MAX_INPUT_LINES - 3,
           INPUT_LINE_RESET = MAX_INPUT_LINES - 2, INPUT_LINE_HALT = MAX_INPUT_LINES - 1 };

    // MAME's fatalerror(): unimplemented opcodes / addressing modes end emulation.
    struct FatalError : std::runtime_error { using std::runtime_error::runtime_error; };

    // v70_device: 32-bit data bus, 32-bit address, PIR 0x00007000. Runs device_start().
    explicit V60Cpu(V60Bus& bus);
    V60Cpu(const V60Cpu&) = delete;              // the spaces point into the object
    V60Cpu& operator=(const V60Cpu&) = delete;

    // Direct ROM/RAM pages. 'decode_mask' is the address bits the board decodes (MAME's map
    // global_mask, $FFFFFF on Multi 32); the direct page table is indexed with
    // (addr & decode_mask) >> 16, so its low 16 bits must be set. Calling it clears the map.
    void set_direct_decode_mask(uint32_t decode_mask);
    // [start, end] (after decode_mask) is backed by 'read_base' / 'write_base' (byte arrays in
    // address order; byte at start = base[0]). start must be 64 KB aligned and end+1 a
    // multiple of 64 KB. write_base == nullptr: writes go to the bus (ROM: the board ignores
    // them, as MAME's unmapped write does). Call once per mirror.
    void map_direct(uint32_t start, uint32_t end, uint8_t* read_base, uint8_t* write_base);

    void reset();                    // device_reset (+ device_execute_interface pre/post reset: total cycles, input vectors)

    // device_execute_interface
    void execute_run();              // MAME's execute_run: runs while m_icount > 0
    int  run(int cycles);            // one timeslice (MAME scheduler bookkeeping); returns cycles run
    void abort_timeslice();          // device_execute_interface::abort_timeslice
    uint64_t total_cycles() const;   // device_execute_interface::total_cycles

    // set_input_line / set_input_line_and_vector / set_input_line_vector, applied now (this is
    // the moment MAME's event queue reaches execute_set_input). Not from inside execute_run().
    void set_input_line(int line, int state);
    void set_input_line_and_vector(int line, int state, int vector);
    void set_input_line_vector(int line, int vector);

    void stall();                    // v60_device::stall (m_stall_io)

    void set_instruction_hook(bool on) { m_instr_hook = on; }

    // State (MAME's state_add list; reg(n) indexes m_reg: R0..R28 = 0..28, AP 29, FP 30,
    // SP 31, PC 32, PSW 33, ISP 36, L0SP..L3SP 37..40, SBR 41, TR 42, SYCW 43, TKCW 44, PIR 45,
    // PSW2 51, ATBR0.. 52.., TRMODE 60, ADTR0 61, ADTR1 62, ADTMR0 63, ADTMR1 64)
    uint32_t reg(int n) const { return m_reg[n]; }
    void     set_reg(int n, uint32_t v) { m_reg[n] = v; }
    uint32_t pc() const  { return m_reg[32]; }
    uint32_t ppc() const { return m_PPC; }      // STATE_GENPCBASE
    uint32_t sp() const  { return m_reg[31]; }
    uint32_t psw();                             // state_export(V60_PSW): v60ReadPSW()
    void     set_psw(uint32_t v);               // state_import(V60_PSW): v60WritePSW()
    std::string flags_string() const;           // state_string_export(STATE_GENFLAGS)
    int      irq_line_state() const { return m_irq_line; }

    // device_execute_interface icount; the scheduler may read it. To end a timeslice early
    // use abort_timeslice(), which also keeps run()/total_cycles() right.
    int m_icount = 0;

private:
    // ---- PORT: replacements for MAME's device framework ----
    V60Bus& m_bus;
    bool m_instr_hook = false;
    bool m_executing = false;                  // inside execute_run (set_input_line* is refused there)

    // device_execute_interface::device_input (emu/diexec.h)
    struct device_input
    {
        int32_t m_stored_vector = 0;           // most recently written vector
        int32_t m_curvector = 0;               // most recently processed vector
        uint8_t m_curstate = CLEAR_LINE;       // most recently processed state
    };
    device_input m_input[MAX_INPUT_LINES];
    int default_irq_callback(int irqline);     // device_input::default_irq_callback
    int standard_irq_callback(int irqline, offs_t pc);

    // device_execute_interface scheduler bookkeeping
    int      m_cycles_running = 0;
    int      m_cycles_stolen = 0;
    uint64_t m_totalcycles = 0;

    // the spaces (v60mem.hpp)
    std::vector<v60mem::DirectPage> m_pages;
    v60mem::Space32<false> m_program_space;
    v60mem::Space32<true>  m_cache32;          // memory_access<32, 2, 0, ENDIANNESS_LITTLE>::cache
    v60mem::SpaceIo16      m_io_space;

    [[noreturn]] static void fatalerror(const char* format, ...) V60_PRINTF_FORMAT(1, 2);
    void logerror(const char* format, ...) V60_PRINTF_FORMAT(2, 3);

    void device_start();
    void device_reset();
    void execute_set_input(int inputnum, int state);

    // ---- v60.h (MAME 0.264), v60_device private section ----
	typedef uint32_t (V60Cpu::*am_func)();
	typedef uint32_t (V60Cpu::*op6_func)(int reg);

	static const am_func s_AMTable1_G7a[16];
	static const am_func s_BAMTable1_G7a[16];
	static const am_func s_AMTable1_G7[32];
	static const am_func s_BAMTable1_G7[32];
	static const am_func s_AMTable1_G6[8];
	static const am_func s_BAMTable1_G6[8];
	static const am_func s_AMTable1[2][8];
	static const am_func s_BAMTable1[2][8];
	static const am_func s_AMTable2_G7a[16];
	static const am_func s_BAMTable2_G7a[16];
	static const am_func s_AMTable2_G7[32];
	static const am_func s_BAMTable2_G7[32];
	static const am_func s_AMTable2_G6[8];
	static const am_func s_BAMTable2_G6[8];
	static const am_func s_AMTable2[2][8];
	static const am_func s_BAMTable2[2][8];
	static const am_func s_AMTable3_G7a[16];
	static const am_func s_AMTable3_G7[32];
	static const am_func s_AMTable3_G6[8];
	static const am_func s_AMTable3[2][8];
	static const am_func s_Op5FTable[32];
	static const am_func s_Op5CTable[32];
	static const op6_func s_OpC6Table[8];
	static const op6_func s_OpC7Table[8];
	static const am_func s_Op59Table[32];
	static const am_func s_Op5BTable[32];
	static const am_func s_Op5DTable[32];
	static const am_func s_Op58Table[32];
	static const am_func s_Op5ATable[32];
	static const am_func s_OpCodeTable[256];

	offs_t              m_start_pc;
	uint32_t              m_reg[68];
	struct {
		uint8_t CY;
		uint8_t OV;
		uint8_t S;
		uint8_t Z;
	}                   m_flags;
	uint8_t               m_irq_line;
	uint8_t               m_nmi_line;
	v60mem::Space32<false> *m_program;      // PORT: address_space *
	// PORT: m_cache16 (V60, 16-bit bus) and the m_pr8/m_pr16/m_pr32 std::function wrappers
	// dropped; OpRead8/16/32 call m_cache32 directly (what the V70's lambdas do).
	v60mem::SpaceIo16     *m_io;             // PORT: address_space *
	uint32_t              m_PPC;
	// PORT: int m_icount is public (above)
	int                 m_stall_io;

	uint32_t              m_op1, m_op2;
	uint8_t               m_flag1, m_flag2;
	uint8_t               m_instflags;
	uint32_t              m_lenop1, m_lenop2;
	uint8_t               m_subop;
	uint32_t              m_bamoffset1, m_bamoffset2;

	// Output variables for ReadAMAddress(cpustate)
	uint8_t               m_amflag;
	uint32_t              m_amout;
	uint32_t              m_bamoffset;

	// Appo temp var
	uint32_t              m_amlength1, m_amlength2;

	// Global vars used by AM functions
	uint32_t              m_modadd;
	uint8_t               m_modm;
	uint8_t               m_modval;
	uint8_t               m_modval2;
	uint8_t               m_modwritevalb;
	uint16_t              m_modwritevalh;
	uint32_t              m_modwritevalw;
	uint8_t               m_moddim;

	// PORT: m_debugger_temp dropped (psw() / set_psw() do the state import/export)


	inline void v60SaveStack();
	inline void v60ReloadStack();
	inline uint32_t v60ReadPSW();
	inline void v60WritePSW(uint32_t newval);
	inline uint32_t v60_update_psw_for_exception(int is_interrupt, int target_level);

	uint32_t am1Register();
	uint32_t am1RegisterIndirect();
	uint32_t bam1RegisterIndirect();
	uint32_t am1RegisterIndirectIndexed();
	uint32_t bam1RegisterIndirectIndexed();
	uint32_t am1Autoincrement();
	uint32_t bam1Autoincrement();
	uint32_t am1Autodecrement();
	uint32_t bam1Autodecrement();
	uint32_t am1Displacement8();
	uint32_t bam1Displacement8();
	uint32_t am1Displacement16();
	uint32_t bam1Displacement16();
	uint32_t am1Displacement32();
	uint32_t bam1Displacement32();
	uint32_t am1DisplacementIndexed8();
	uint32_t bam1DisplacementIndexed8();
	uint32_t am1DisplacementIndexed16();
	uint32_t bam1DisplacementIndexed16();
	uint32_t am1DisplacementIndexed32();
	uint32_t bam1DisplacementIndexed32();
	uint32_t am1PCDisplacement8();
	uint32_t bam1PCDisplacement8();
	uint32_t am1PCDisplacement16();
	uint32_t bam1PCDisplacement16();
	uint32_t am1PCDisplacement32();
	uint32_t bam1PCDisplacement32();
	uint32_t am1PCDisplacementIndexed8();
	uint32_t bam1PCDisplacementIndexed8();
	uint32_t am1PCDisplacementIndexed16();
	uint32_t bam1PCDisplacementIndexed16();
	uint32_t am1PCDisplacementIndexed32();
	uint32_t bam1PCDisplacementIndexed32();
	uint32_t am1DisplacementIndirect8();
	uint32_t bam1DisplacementIndirect8();
	uint32_t am1DisplacementIndirect16();
	uint32_t bam1DisplacementIndirect16();
	uint32_t am1DisplacementIndirect32();
	uint32_t bam1DisplacementIndirect32();
	uint32_t am1DisplacementIndirectIndexed8();
	uint32_t bam1DisplacementIndirectIndexed8();
	uint32_t am1DisplacementIndirectIndexed16();
	uint32_t bam1DisplacementIndirectIndexed16();
	uint32_t am1DisplacementIndirectIndexed32();
	uint32_t bam1DisplacementIndirectIndexed32();
	uint32_t am1PCDisplacementIndirect8();
	uint32_t bam1PCDisplacementIndirect8();
	uint32_t am1PCDisplacementIndirect16();
	uint32_t bam1PCDisplacementIndirect16();
	uint32_t am1PCDisplacementIndirect32();
	uint32_t bam1PCDisplacementIndirect32();
	uint32_t am1PCDisplacementIndirectIndexed8();
	uint32_t bam1PCDisplacementIndirectIndexed8();
	uint32_t am1PCDisplacementIndirectIndexed16();
	uint32_t bam1PCDisplacementIndirectIndexed16();
	uint32_t am1PCDisplacementIndirectIndexed32();
	uint32_t bam1PCDisplacementIndirectIndexed32();
	uint32_t am1DoubleDisplacement8();
	uint32_t bam1DoubleDisplacement8();
	uint32_t am1DoubleDisplacement16();
	uint32_t bam1DoubleDisplacement16();
	uint32_t am1DoubleDisplacement32();
	uint32_t bam1DoubleDisplacement32();
	uint32_t am1PCDoubleDisplacement8();
	uint32_t bam1PCDoubleDisplacement8();
	uint32_t am1PCDoubleDisplacement16();
	uint32_t bam1PCDoubleDisplacement16();
	uint32_t am1PCDoubleDisplacement32();
	uint32_t bam1PCDoubleDisplacement32();
	uint32_t am1DirectAddress();
	uint32_t bam1DirectAddress();
	uint32_t am1DirectAddressIndexed();
	uint32_t bam1DirectAddressIndexed();
	uint32_t am1DirectAddressDeferred();
	uint32_t bam1DirectAddressDeferred();
	uint32_t am1DirectAddressDeferredIndexed();
	uint32_t bam1DirectAddressDeferredIndexed();
	uint32_t am1Immediate();
	uint32_t am1ImmediateQuick();
	[[noreturn]] uint32_t am1Error1();
	[[noreturn]] uint32_t bam1Error1();
	[[noreturn]] uint32_t am1Error2();
	[[noreturn]] uint32_t bam1Error2();
	[[noreturn]] [[maybe_unused]] uint32_t am1Error3();
	[[noreturn]] [[maybe_unused]] uint32_t bam1Error3();
	[[noreturn]] uint32_t am1Error4();
	[[noreturn]] uint32_t bam1Error4();
	[[noreturn]] uint32_t am1Error5();
	[[noreturn]] uint32_t bam1Error5();
	[[noreturn]] uint32_t bam1Error6();
	uint32_t am1Group7a();
	uint32_t bam1Group7a();
	uint32_t am1Group6();
	uint32_t bam1Group6();
	uint32_t am1Group7();
	uint32_t bam1Group7();
	uint32_t am2Register();
	uint32_t am2RegisterIndirect();
	uint32_t bam2RegisterIndirect();
	uint32_t am2RegisterIndirectIndexed();
	uint32_t bam2RegisterIndirectIndexed();
	uint32_t am2Autoincrement();
	uint32_t bam2Autoincrement();
	uint32_t am2Autodecrement();
	uint32_t bam2Autodecrement();
	uint32_t am2Displacement8();
	uint32_t bam2Displacement8();
	uint32_t am2Displacement16();
	uint32_t bam2Displacement16();
	uint32_t am2Displacement32();
	uint32_t bam2Displacement32();
	uint32_t am2DisplacementIndexed8();
	uint32_t bam2DisplacementIndexed8();
	uint32_t am2DisplacementIndexed16();
	uint32_t bam2DisplacementIndexed16();
	uint32_t am2DisplacementIndexed32();
	uint32_t bam2DisplacementIndexed32();
	uint32_t am2PCDisplacement8();
	uint32_t bam2PCDisplacement8();
	uint32_t am2PCDisplacement16();
	uint32_t bam2PCDisplacement16();
	uint32_t am2PCDisplacement32();
	uint32_t bam2PCDisplacement32();
	uint32_t am2PCDisplacementIndexed8();
	uint32_t bam2PCDisplacementIndexed8();
	uint32_t am2PCDisplacementIndexed16();
	uint32_t bam2PCDisplacementIndexed16();
	uint32_t am2PCDisplacementIndexed32();
	uint32_t bam2PCDisplacementIndexed32();
	uint32_t am2DisplacementIndirect8();
	uint32_t bam2DisplacementIndirect8();
	uint32_t am2DisplacementIndirect16();
	uint32_t bam2DisplacementIndirect16();
	uint32_t am2DisplacementIndirect32();
	uint32_t bam2DisplacementIndirect32();
	uint32_t am2DisplacementIndirectIndexed8();
	uint32_t bam2DisplacementIndirectIndexed8();
	uint32_t am2DisplacementIndirectIndexed16();
	uint32_t bam2DisplacementIndirectIndexed16();
	uint32_t am2DisplacementIndirectIndexed32();
	uint32_t bam2DisplacementIndirectIndexed32();
	uint32_t am2PCDisplacementIndirect8();
	uint32_t bam2PCDisplacementIndirect8();
	uint32_t am2PCDisplacementIndirect16();
	uint32_t bam2PCDisplacementIndirect16();
	uint32_t am2PCDisplacementIndirect32();
	uint32_t bam2PCDisplacementIndirect32();
	uint32_t am2PCDisplacementIndirectIndexed8();
	uint32_t bam2PCDisplacementIndirectIndexed8();
	uint32_t am2PCDisplacementIndirectIndexed16();
	uint32_t bam2PCDisplacementIndirectIndexed16();
	uint32_t am2PCDisplacementIndirectIndexed32();
	uint32_t bam2PCDisplacementIndirectIndexed32();
	uint32_t am2DoubleDisplacement8();
	uint32_t bam2DoubleDisplacement8();
	uint32_t am2DoubleDisplacement16();
	uint32_t bam2DoubleDisplacement16();
	uint32_t am2DoubleDisplacement32();
	uint32_t bam2DoubleDisplacement32();
	uint32_t am2PCDoubleDisplacement8();
	uint32_t bam2PCDoubleDisplacement8();
	uint32_t am2PCDoubleDisplacement16();
	uint32_t bam2PCDoubleDisplacement16();
	uint32_t am2PCDoubleDisplacement32();
	uint32_t bam2PCDoubleDisplacement32();
	uint32_t am2DirectAddress();
	uint32_t bam2DirectAddress();
	uint32_t am2DirectAddressIndexed();
	uint32_t bam2DirectAddressIndexed();
	uint32_t am2DirectAddressDeferred();
	uint32_t bam2DirectAddressDeferred();
	uint32_t am2DirectAddressDeferredIndexed();
	uint32_t bam2DirectAddressDeferredIndexed();
	uint32_t am2Immediate();
	uint32_t am2ImmediateQuick();
	uint32_t am2Error1();
	[[noreturn]] uint32_t am2Error2();
	[[noreturn]] [[maybe_unused]] uint32_t am2Error3();
	[[noreturn]] uint32_t am2Error4();
	[[noreturn]] uint32_t am2Error5();
	[[noreturn]] uint32_t bam2Error1();
	[[noreturn]] uint32_t bam2Error2();
	[[noreturn]] [[maybe_unused]] uint32_t bam2Error3();
	[[noreturn]] uint32_t bam2Error4();
	[[noreturn]] uint32_t bam2Error5();
	[[noreturn]] uint32_t bam2Error6();
	uint32_t am2Group7a();
	uint32_t bam2Group7a();
	uint32_t am2Group6();
	uint32_t bam2Group6();
	uint32_t am2Group7();
	uint32_t bam2Group7();
	uint32_t am3Register();
	uint32_t am3RegisterIndirect();
	uint32_t am3RegisterIndirectIndexed();
	uint32_t am3Autoincrement();
	uint32_t am3Autodecrement();
	uint32_t am3Displacement8();
	uint32_t am3Displacement16();
	uint32_t am3Displacement32();
	uint32_t am3DisplacementIndexed8();
	uint32_t am3DisplacementIndexed16();
	uint32_t am3DisplacementIndexed32();
	uint32_t am3PCDisplacement8();
	uint32_t am3PCDisplacement16();
	uint32_t am3PCDisplacement32();
	uint32_t am3PCDisplacementIndexed8();
	uint32_t am3PCDisplacementIndexed16();
	uint32_t am3PCDisplacementIndexed32();
	uint32_t am3DisplacementIndirect8();
	uint32_t am3DisplacementIndirect16();
	uint32_t am3DisplacementIndirect32();
	uint32_t am3DisplacementIndirectIndexed8();
	uint32_t am3DisplacementIndirectIndexed16();
	uint32_t am3DisplacementIndirectIndexed32();
	uint32_t am3PCDisplacementIndirect8();
	uint32_t am3PCDisplacementIndirect16();
	uint32_t am3PCDisplacementIndirect32();
	uint32_t am3PCDisplacementIndirectIndexed8();
	uint32_t am3PCDisplacementIndirectIndexed16();
	uint32_t am3PCDisplacementIndirectIndexed32();
	uint32_t am3DoubleDisplacement8();
	uint32_t am3DoubleDisplacement16();
	uint32_t am3DoubleDisplacement32();
	uint32_t am3PCDoubleDisplacement8();
	uint32_t am3PCDoubleDisplacement16();
	uint32_t am3PCDoubleDisplacement32();
	uint32_t am3DirectAddress();
	uint32_t am3DirectAddressIndexed();
	uint32_t am3DirectAddressDeferred();
	uint32_t am3DirectAddressDeferredIndexed();
	[[noreturn]] uint32_t am3Immediate();
	[[noreturn]] uint32_t am3ImmediateQuick();
	[[noreturn]] uint32_t am3Error1();
	[[noreturn]] uint32_t am3Error2();
	[[noreturn]] [[maybe_unused]] uint32_t am3Error3();
	[[noreturn]] uint32_t am3Error4();
	[[noreturn]] uint32_t am3Error5();
	uint32_t am3Group7a();
	uint32_t am3Group6();
	uint32_t am3Group7();
	uint32_t ReadAM();
	uint32_t BitReadAM();
	uint32_t ReadAMAddress();
	uint32_t BitReadAMAddress();
	uint32_t WriteAM();
	void F12DecodeFirstOperand(am_func DecodeOp1, uint8_t dim1);
	void F12WriteSecondOperand(uint8_t dim2);
	void F12DecodeOperands(am_func DecodeOp1, uint8_t dim1, am_func DecodeOp2, uint8_t dim2);
	uint32_t opADDB();
	uint32_t opADDH();
	uint32_t opADDW();
	uint32_t opADDCB();
	uint32_t opADDCH();
	uint32_t opADDCW();
	uint32_t opANDB();
	uint32_t opANDH();
	uint32_t opANDW();
	uint32_t opCALL();
	uint32_t opCHKAR();
	uint32_t opCHKAW();
	uint32_t opCHKAE();
	uint32_t opCHLVL();
	uint32_t opCLR1();
	uint32_t opCMPB();
	uint32_t opCMPH();
	uint32_t opCMPW();
	uint32_t opDIVB();
	uint32_t opDIVH();
	uint32_t opDIVW();
	uint32_t opDIVX();
	uint32_t opDIVUX();
	uint32_t opDIVUB();
	uint32_t opDIVUH();
	uint32_t opDIVUW();
	uint32_t opINB();
	uint32_t opINH();
	uint32_t opINW();
	uint32_t opLDPR();
	uint32_t opLDTASK();
	uint32_t opMOVD();
	uint32_t opMOVB();
	uint32_t opMOVH();
	uint32_t opMOVW();
	uint32_t opMOVEAB();
	uint32_t opMOVEAH();
	uint32_t opMOVEAW();
	uint32_t opMOVSBH();
	uint32_t opMOVSBW();
	uint32_t opMOVSHW();
	uint32_t opMOVTHB();
	uint32_t opMOVTWB();
	uint32_t opMOVTWH();
	uint32_t opMOVZBH();
	uint32_t opMOVZBW();
	uint32_t opMOVZHW();
	uint32_t opMULB();
	uint32_t opMULH();
	uint32_t opMULW();
	uint32_t opMULUB();
	uint32_t opMULUH();
	uint32_t opMULUW();
	uint32_t opNEGB();
	uint32_t opNEGH();
	uint32_t opNEGW();
	uint32_t opNOTB();
	uint32_t opNOTH();
	uint32_t opNOTW();
	uint32_t opNOT1();
	uint32_t opORB();
	uint32_t opORH();
	uint32_t opORW();
	uint32_t opOUTB();
	uint32_t opOUTH();
	uint32_t opOUTW();
	uint32_t opREMB();
	uint32_t opREMH();
	uint32_t opREMW();
	uint32_t opREMUB();
	uint32_t opREMUH();
	uint32_t opREMUW();
	uint32_t opROTB();
	uint32_t opROTH();
	uint32_t opROTW();
	uint32_t opROTCB();
	uint32_t opROTCH();
	uint32_t opROTCW();
	uint32_t opRVBIT();
	uint32_t opRVBYT();
	uint32_t opSET1();
	uint32_t opSETF();
	uint32_t opSHAB();
	uint32_t opSHAH();
	uint32_t opSHAW();
	uint32_t opSHLB();
	uint32_t opSHLH();
	uint32_t opSHLW();
	uint32_t opSTPR();
	uint32_t opSUBB();
	uint32_t opSUBH();
	uint32_t opSUBW();
	uint32_t opSUBCB();
	uint32_t opSUBCH();
	uint32_t opSUBCW();
	uint32_t opTEST1();
	uint32_t opUPDPSWW();
	uint32_t opUPDPSWH();
	uint32_t opXCHB();
	uint32_t opXCHH();
	uint32_t opXCHW();
	uint32_t opXORB();
	uint32_t opXORH();
	uint32_t opXORW();
	uint32_t opMULX();
	uint32_t opMULUX();
	void F2DecodeFirstOperand(am_func DecodeOp1, uint8_t dim1);
	void F2DecodeSecondOperand(am_func DecodeOp2, uint8_t dim2);
	void F2WriteSecondOperand(uint8_t dim2);
	uint32_t opCVTWS();
	uint32_t opCVTSW();
	uint32_t opMOVFS();
	uint32_t opNEGFS();
	uint32_t opABSFS();
	uint32_t opADDFS();
	uint32_t opSUBFS();
	uint32_t opMULFS();
	uint32_t opDIVFS();
	uint32_t opSCLFS();
	uint32_t opCMPF();
	[[noreturn]] uint32_t op5FUNHANDLED();
	[[noreturn]] uint32_t op5CUNHANDLED();
	uint32_t op5F();
	uint32_t op5C();
	uint32_t opINCB();
	uint32_t opINCH();
	uint32_t opINCW();
	uint32_t opDECB();
	uint32_t opDECH();
	uint32_t opDECW();
	uint32_t opJMP();
	uint32_t opJSR();
	uint32_t opPREPARE();
	uint32_t opRET();
	uint32_t opTRAP();
	uint32_t opRETIU();
	uint32_t opRETIS();
	uint32_t opSTTASK();
	uint32_t opGETPSW();
	uint32_t opTASI();
	uint32_t opCLRTLB();
	uint32_t opPOPM();
	uint32_t opPUSHM();
	uint32_t opTESTB();
	uint32_t opTESTH();
	uint32_t opTESTW();
	uint32_t opPUSH();
	uint32_t opPOP();
	uint32_t opINCB_0();
	uint32_t opINCB_1();
	uint32_t opINCH_0();
	uint32_t opINCH_1();
	uint32_t opINCW_0();
	uint32_t opINCW_1();
	uint32_t opDECB_0();
	uint32_t opDECB_1();
	uint32_t opDECH_0();
	uint32_t opDECH_1();
	uint32_t opDECW_0();
	uint32_t opDECW_1();
	uint32_t opJMP_0();
	uint32_t opJMP_1();
	uint32_t opJSR_0();
	uint32_t opJSR_1();
	uint32_t opPREPARE_0();
	uint32_t opPREPARE_1();
	uint32_t opRET_0();
	uint32_t opRET_1();
	uint32_t opTRAP_0();
	uint32_t opTRAP_1();
	uint32_t opRETIU_0();
	uint32_t opRETIU_1();
	uint32_t opRETIS_0();
	uint32_t opRETIS_1();
	uint32_t opGETPSW_0();
	uint32_t opGETPSW_1();
	uint32_t opTASI_0();
	uint32_t opTASI_1();
	uint32_t opCLRTLB_0();
	uint32_t opCLRTLB_1();
	uint32_t opPOPM_0();
	uint32_t opPOPM_1();
	uint32_t opPUSHM_0();
	uint32_t opPUSHM_1();
	uint32_t opTESTB_0();
	uint32_t opTESTB_1();
	uint32_t opTESTH_0();
	uint32_t opTESTH_1();
	uint32_t opTESTW_0();
	uint32_t opTESTW_1();
	uint32_t opPUSH_0();
	uint32_t opPUSH_1();
	uint32_t opPOP_0();
	uint32_t opPOP_1();
	uint32_t opSTTASK_0();
	uint32_t opSTTASK_1();
	uint32_t opBGT8();
	uint32_t opBGT16();
	uint32_t opBGE8();
	uint32_t opBGE16();
	uint32_t opBLT8();
	uint32_t opBLT16();
	uint32_t opBLE8();
	uint32_t opBLE16();
	uint32_t opBH8();
	uint32_t opBH16();
	uint32_t opBNH8();
	uint32_t opBNH16();
	uint32_t opBNL8();
	uint32_t opBNL16();
	uint32_t opBL8();
	uint32_t opBL16();
	uint32_t opBNE8();
	uint32_t opBNE16();
	uint32_t opBE8();
	uint32_t opBE16();
	uint32_t opBNV8();
	uint32_t opBNV16();
	uint32_t opBV8();
	uint32_t opBV16();
	uint32_t opBP8();
	uint32_t opBP16();
	uint32_t opBN8();
	uint32_t opBN16();
	uint32_t opBR8();
	uint32_t opBR16();
	uint32_t opBSR();
	uint32_t opBRK();
	uint32_t opBRKV();
	uint32_t opCLRTLBA();
	uint32_t opDISPOSE();
	uint32_t opHALT();
	uint32_t opNOP();
	uint32_t opRSR();
	uint32_t opTRAPFL();
	uint32_t opTB(int reg);
	uint32_t opDBGT(int reg);
	uint32_t opDBLE(int reg);
	uint32_t opDBGE(int reg);
	uint32_t opDBLT(int reg);
	uint32_t opDBH(int reg);
	uint32_t opDBNH(int reg);
	uint32_t opDBL(int reg);
	uint32_t opDBNL(int reg);
	uint32_t opDBE(int reg);
	uint32_t opDBNE(int reg);
	uint32_t opDBV(int reg);
	uint32_t opDBNV(int reg);
	uint32_t opDBN(int reg);
	uint32_t opDBP(int reg);
	uint32_t opDBR(int reg);
	uint32_t opC6();
	uint32_t opC7();
	void F7aDecodeOperands(am_func DecodeOp1, uint8_t dim1, am_func DecodeOp2, uint8_t dim2);
	void F7bDecodeFirstOperand(am_func DecodeOp1, uint8_t dim1);
	void F7bWriteSecondOperand(uint8_t dim2);
	void F7bDecodeOperands(am_func DecodeOp1, uint8_t dim1, am_func DecodeOp2, uint8_t dim2);
	void F7cDecodeOperands(am_func DecodeOp1, uint8_t dim1, am_func DecodeOp2, uint8_t dim2);
	uint32_t opCMPSTRB(uint8_t bFill, uint8_t bStop);
	uint32_t opCMPSTRH(uint8_t bFill, uint8_t bStop);
	uint32_t opMOVSTRUB(uint8_t bFill, uint8_t bStop);
	uint32_t opMOVSTRDB(uint8_t bFill, uint8_t bStop);
	uint32_t opMOVSTRUH(uint8_t bFill, uint8_t bStop);
	uint32_t opMOVSTRDH(uint8_t bFill, uint8_t bStop);
	uint32_t opSEARCHUB(uint8_t bSearch);
	uint32_t opSEARCHUH(uint8_t bSearch);
	uint32_t opSEARCHDB(uint8_t bSearch);
	uint32_t opSEARCHDH(uint8_t bSearch);
	uint32_t opSCHCUB();
	uint32_t opSCHCUH();
	uint32_t opSCHCDB();
	uint32_t opSCHCDH();
	uint32_t opSKPCUB();
	uint32_t opSKPCUH();
	uint32_t opSKPCDB();
	uint32_t opSKPCDH();
	uint32_t opCMPCB();
	uint32_t opCMPCH();
	uint32_t opCMPCFB();
	uint32_t opCMPCFH();
	uint32_t opCMPCSB();
	uint32_t opCMPCSH();
	uint32_t opMOVCUB();
	uint32_t opMOVCUH();
	uint32_t opMOVCFUB();
	uint32_t opMOVCFUH();
	uint32_t opMOVCSUB();
	uint32_t opMOVCSUH();
	uint32_t opMOVCDB();
	uint32_t opMOVCDH();
	uint32_t opMOVCFDB();
	uint32_t opMOVCFDH();
	uint32_t opEXTBFZ();
	uint32_t opEXTBFS();
	uint32_t opEXTBFL();
	uint32_t opSCHBS(uint32_t bSearch1);
	uint32_t opSCH0BSU();
	uint32_t opSCH1BSU();
	uint32_t opINSBFR();
	uint32_t opINSBFL();
	uint32_t opMOVBSD();
	uint32_t opMOVBSU();
	uint32_t opADDDC();
	uint32_t opSUBDC();
	uint32_t opSUBRDC();
	uint32_t opCVTDPZ();
	uint32_t opCVTDZP();
	[[noreturn]] uint32_t op58UNHANDLED();
	[[noreturn]] uint32_t op5AUNHANDLED();
	[[noreturn]] uint32_t op5BUNHANDLED();
	[[noreturn]] uint32_t op5DUNHANDLED();
	[[noreturn]] uint32_t op59UNHANDLED();
	uint32_t op58();
	uint32_t op5A();
	uint32_t op5B();
	uint32_t op5D();
	uint32_t op59();
	[[noreturn]] uint32_t opUNHANDLED();
	void v60_do_irq(int vector);
	void v60_try_irq();
};
