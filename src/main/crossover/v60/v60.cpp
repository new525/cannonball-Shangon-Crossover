/***************************************************************************
    NEC V60 / V70 CPU core (V70 configuration), standalone. See v60.hpp for the bus and
    scheduling contracts.

    Transcribed from MAME 0.264 src/devices/cpu/v60/v60.cpp.
    license:BSD-3-Clause
    copyright-holders:Farfetch'd, R. Belmont

    Everything below the original header comment is MAME's v60.cpp with the device
    framework replaced; the addressing-mode and opcode files (am*.hxx, op*.hxx,
    optable.hxx) are included verbatim. Every deviation is marked "PORT:".

    PORT: dropped: DEFINE_DEVICE_TYPE, the v60_device (V60: 16-bit bus, 24-bit address,
    PIR 0x6000) configuration and its m_cache16 path, memory_space_config(),
    create_disassembler() (see v60d.hpp), save_item / state_add registration (no save
    states; state access is the accessors in v60.hpp).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// V60.C
// Undiscover the beast!
// Main hacking and coding by Farfetch'd
// Portability fixes by R. Belmont
//
// Emulation for the NEC V60 (uPD70615) and V70 (uPD70632) CPUs
//

/*
Taken from the NEC Semiconductor Selection Guide Guide Book (Oct. 1995):

uPD70615 (V60)
Features:
- Virtual memory (paging method)
- Level protection architecture - 4-level hierarchical protection function
    for system multi-programming.
- Abundant general registers - Thirty two 32-bit general registers for
    optimizing compiler
- Refined instruction set - 2-address method: Arbitrary addressing mode
    can be used independently for source operand and destination operand.
- Abundant address modes and data types - Auto increment/decrement mode
    for string process, and memory indirect addressing for pointer operation
- High cost-to performance chip
- No multiprocessor system - no FRM function for increasing system
    reliability using two or more processors.
- No V20/V30 simulation mode
Address bus: 24 bits
Data bus: 16 bits
Memory space: 4G bytes
Operating frequency: 16 MHz
Package: 120-pin QFP

uPD70616 (V60)
Features:
- Virtual memory (paging method)
- Level protection architecture - 4-level hierarchical protection function
    for system multi-programming.
- Abundant general registers - Thirty two 32-bit general registers for
    optimizing compiler
- Refined instruction set - 2-address method: Arbitrary addressing mode
    can be used independently for source operand and destination operand.
- Abundant address modes and data types - Auto increment/decrement mode
    for string process, and memory indirect addressing for pointer operation
- Multiprocessor system - FRM function for increasing system reliability
    using two or more processors.
- V20/V30 simulation mode
Address bus: 24 bits
Data bus: 16 bits
Memory space: 4G bytes
Operating frequency: 16 MHz
Package: 68-pin PGA

uPD70632 (V70)
Features:
- Virtual memory (paging method)
- Level protection architecture - 4-level hierarchical protection function
    for system multi-programming.
- Abundant general registers - Thirty two 32-bit general registers for
    optimizing compiler
- Refined instruction set - 2-address method: Arbitrary addressing mode
    can be used independently for source operand and destination operand.
- Abundant address modes and data types - Auto increment/decrement mode
    for string process, and memory indirect addressing for pointer operation
- Multiprocessor system - FRM function for increasing system reliability
    using two or more processors.
- V20/V30 simulation mode
Address bus: 32 bits
Data bus: 32 bits
Memory space: 4G bytes
Operating frequency: 20 MHz
Package: 132-pin PGA, 200-pin QFP
*/

#include "v60.hpp"

#include <cstdarg>
#include <cstdio>

// PORT: MAME's release builds (the configuration of MAME 0.264 as shipped) define NDEBUG,
// so the core's assert()s compile to nothing; keep that whatever this project's flags are.
#undef assert
#define assert(x) ((void)0)

// emu/emucore.h (MAME 0.264): reinterpret bits as float / float as bits
inline float u2f(uint32_t v)
{
	union {
		float ff;
		uint32_t vv;
	} u;
	u.vv = v;
	return u.ff;
}

inline uint32_t f2u(float f)
{
	union {
		float ff;
		uint32_t vv;
	} u;
	u.ff = f;
	return u.vv;
}

// PORT: DEFINE_DEVICE_TYPE(V60/V70) dropped.

// PORT: V60Cpu / v70_device constructors folded into one: the v70_device arguments
// (databits 32, addrbits 32, pir 0x00007000) and the V60Cpu member initialisers;
// m_program_config / m_io_config become the Space32 / SpaceIo16 objects. device_start()
// runs here (MAME runs it when the machine starts).
// Set m_PIR (Processor ID) for NEC v70. LSB is reserved to NEC,
// so I don't know what it contains.
V60Cpu::V60Cpu(V60Bus& bus)
	: m_bus(bus)
	, m_start_pc(0xfffffff0)
{
	// PORT: MAME leaves the members device_start() does not set uninitialised; zero them.
	std::memset(m_reg, 0, sizeof(m_reg));
	m_PPC = 0;
	m_reg[45] = 0x00007000;

	m_program_space.m_bus = &bus;
	m_cache32.m_bus = &bus;
	m_io_space.m_bus = &bus;
	set_direct_decode_mask(0xffffffff);

	device_start();
}

void V60Cpu::set_direct_decode_mask(uint32_t decode_mask)
{
	if ((decode_mask & 0xffff) != 0xffff)
		throw std::invalid_argument("V60Cpu::set_direct_decode_mask: low 16 bits must be set");
	m_pages.assign((size_t(decode_mask) >> 16) + 1, v60mem::DirectPage());
	m_program_space.m_pages = m_cache32.m_pages = m_pages.data();
	m_program_space.m_pagemask = m_cache32.m_pagemask = decode_mask;
}

void V60Cpu::map_direct(uint32_t start, uint32_t end, uint8_t* read_base, uint8_t* write_base)
{
	const uint32_t mask = m_program_space.m_pagemask;
	if ((start & 0xffff) != 0 || ((end + 1) & 0xffff) != 0 || end < start || (end & ~mask) != 0)
		throw std::invalid_argument("V60Cpu::map_direct: range must be 64 KB pages inside the decode mask");
	for (uint32_t page = start >> 16; page <= (end >> 16); page++)
	{
		const size_t off = size_t(page - (start >> 16)) << 16;
		m_pages[page].r = read_base ? read_base + off : nullptr;
		m_pages[page].w = write_base ? write_base + off : nullptr;
	}
}

// PORT: MAME's fatalerror() throws emu_fatalerror and stops the machine.
void V60Cpu::fatalerror(const char* format, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	throw FatalError(buf);
}

// PORT: device_t::logerror() -> V60Bus::log()
void V60Cpu::logerror(const char* format, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	m_bus.log(buf);
}


// memory accessors
// PORT: m_pr8/m_pr16/m_pr32 are std::function lambdas in MAME; for the V70 (32-bit data
// bus) they call m_cache32.read_byte / read_word_unaligned / read_dword_unaligned.
#define OpRead8(a)   m_cache32.read_byte(a)
#define OpRead16(a)  m_cache32.read_word_unaligned(a)
#define OpRead32(a)  m_cache32.read_dword_unaligned(a)


// macros stolen from MAME for flags calc
// note that these types are in x86 naming:
// byte = 8 bit, word = 16 bit, long = 32 bit

// parameter x = result, y = source 1, z = source 2

#define SetOFL_Add(x, y,z)  (_OV = (((x) ^ (y)) & ((x) ^ (z)) & 0x80000000) ? 1: 0)
#define SetOFW_Add(x, y,z)  (_OV = (((x) ^ (y)) & ((x) ^ (z)) & 0x8000) ? 1 : 0)
#define SetOFB_Add(x, y,z)  (_OV = (((x) ^ (y)) & ((x) ^ (z)) & 0x80) ? 1 : 0)

#define SetOFL_Sub(x, y,z)  (_OV = (((z) ^ (y)) & ((z) ^ (x)) & 0x80000000) ? 1 : 0)
#define SetOFW_Sub(x, y,z)  (_OV = (((z) ^ (y)) & ((z) ^ (x)) & 0x8000) ? 1 : 0)
#define SetOFB_Sub(x, y,z)  (_OV = (((z) ^ (y)) & ((z) ^ (x)) & 0x80) ? 1 : 0)

#define SetCFB(x)           {_CY = ((x) & 0x100) ? 1 : 0; }
#define SetCFW(x)           {_CY = ((x) & 0x10000) ? 1 : 0; }
#define SetCFL(x)           {_CY = ((x) & (((uint64_t)1) << 32)) ? 1 : 0; }

#define SetSF(x)            (_S = (x))
#define SetZF(x)            (_Z = (x))

#define SetSZPF_Byte(x)     {_Z = ((uint8_t)(x) == 0);  _S = ((x)&0x80) ? 1 : 0; }
#define SetSZPF_Word(x)     {_Z = ((uint16_t)(x) == 0);  _S = ((x)&0x8000) ? 1 : 0; }
#define SetSZPF_Long(x)     {_Z = ((uint32_t)(x) == 0);  _S = ((x)&0x80000000) ? 1 : 0; }

#define ORB(dst, src)       { (dst) |= (src); _OV = 0; SetSZPF_Byte(dst); }
#define ORW(dst, src)       { (dst) |= (src); _OV = 0; SetSZPF_Word(dst); }
#define ORL(dst, src)       { (dst) |= (src); _OV = 0; SetSZPF_Long(dst); }

#define ANDB(dst, src)      { (dst) &= (src); _OV = 0; SetSZPF_Byte(dst); }
#define ANDW(dst, src)      { (dst) &= (src); _OV = 0; SetSZPF_Word(dst); }
#define ANDL(dst, src)      { (dst) &= (src); _OV = 0; SetSZPF_Long(dst); }

#define XORB(dst, src)      { (dst) ^= (src); _OV = 0; SetSZPF_Byte(dst); }
#define XORW(dst, src)      { (dst) ^= (src); _OV = 0; SetSZPF_Word(dst); }
#define XORL(dst, src)      { (dst) ^= (src); _OV = 0; SetSZPF_Long(dst); }

#define SUBB(dst, src)      { unsigned res = (dst) - (src); SetCFB(res); SetOFB_Sub(res, src, dst); SetSZPF_Byte(res); dst = (uint8_t)res; }
#define SUBW(dst, src)      { unsigned res = (dst) - (src); SetCFW(res); SetOFW_Sub(res, src, dst); SetSZPF_Word(res); dst = (uint16_t)res; }
#define SUBL(dst, src)      { uint64_t res = (uint64_t)(dst) - (int64_t)(src); SetCFL(res); SetOFL_Sub(res, src, dst); SetSZPF_Long(res); dst = (uint32_t)res; }

#define ADDB(dst, src)      { unsigned res = (dst) + (src); SetCFB(res); SetOFB_Add(res, src, dst); SetSZPF_Byte(res); dst = (uint8_t)res; }
#define ADDW(dst, src)      { unsigned res = (dst) + (src); SetCFW(res); SetOFW_Add(res, src, dst); SetSZPF_Word(res); dst = (uint16_t)res; }
#define ADDL(dst, src)      { uint64_t res = (uint64_t)(dst) + (uint64_t)(src); SetCFL(res); SetOFL_Add(res, src, dst); SetSZPF_Long(res); dst = (uint32_t)res; }

#define SETREG8(a, b)       (a) = ((a) & ~0xff) | ((b) & 0xff)
#define SETREG16(a, b)      (a) = ((a) & ~0xffff) | ((b) & 0xffff)


/*
 * Prevent warnings on NetBSD.  All identifiers beginning with an underscore
 * followed by an uppercase letter are reserved by the C standard (ISO / IEC
 * 9899:1999, 7.1.3) to be used by the implementation.  It'd be best to rename
 * all such instances, but this is less intrusive and error-prone.
 */
#undef _S

#define _CY     m_flags.CY
#define _OV     m_flags.OV
#define _S      m_flags.S
#define _Z      m_flags.Z


// Defines of all v60 register...
#define R0      m_reg[0]
#define R1      m_reg[1]
#define R2      m_reg[2]
#define R3      m_reg[3]
#define R4      m_reg[4]
#define R5      m_reg[5]
#define R6      m_reg[6]
#define R7      m_reg[7]
#define R8      m_reg[8]
#define R9      m_reg[9]
#define R10     m_reg[10]
#define R11     m_reg[11]
#define R12     m_reg[12]
#define R13     m_reg[13]
#define R14     m_reg[14]
#define R15     m_reg[15]
#define R16     m_reg[16]
#define R17     m_reg[17]
#define R18     m_reg[18]
#define R19     m_reg[19]
#define R20     m_reg[20]
#define R21     m_reg[21]
#define R22     m_reg[22]
#define R23     m_reg[23]
#define R24     m_reg[24]
#define R25     m_reg[25]
#define R26     m_reg[26]
#define R27     m_reg[27]
#define R28     m_reg[28]
#define AP      m_reg[29]
#define FP      m_reg[30]
#define SP      m_reg[31]

#define PC      m_reg[32]
#define PSW     m_reg[33]

// Privileged registers
#define ISP     m_reg[36]
#define L0SP    m_reg[37]
#define L1SP    m_reg[38]
#define L2SP    m_reg[39]
#define L3SP    m_reg[40]
#define SBR     m_reg[41]
#define TR      m_reg[42]
#define SYCW    m_reg[43]
#define TKCW    m_reg[44]
#define PIR     m_reg[45]
//10-14 reserved
#define PSW2    m_reg[51]
#define ATBR0   m_reg[52]
#define ATLR0   m_reg[53]
#define ATBR1   m_reg[54]
#define ATLR1   m_reg[55]
#define ATBR2   m_reg[56]
#define ATLR2   m_reg[57]
#define ATBR3   m_reg[58]
#define ATLR3   m_reg[59]
#define TRMODE  m_reg[60]
#define ADTR0   m_reg[61]
#define ADTR1   m_reg[62]
#define ADTMR0  m_reg[63]
#define ADTMR1  m_reg[64]
//29-31 reserved

// Defines...
#define NORMALIZEFLAGS() \
{ \
	_S    = _S  ? 1 : 0; \
	_OV   = _OV ? 1 : 0; \
	_Z    = _Z  ? 1 : 0; \
	_CY   = _CY ? 1 : 0; \
}


void V60Cpu::v60SaveStack()
{
	if (PSW & 0x10000000)
		ISP = SP;
	else
		m_reg[37 + ((PSW >> 24) & 3)] = SP;
}

void V60Cpu::v60ReloadStack()
{
	if (PSW & 0x10000000)
		SP = ISP;
	else
		SP = m_reg[37 + ((PSW >> 24) & 3)];
}

uint32_t V60Cpu::v60ReadPSW()
{
	PSW &= 0xfffffff0;
	PSW |= (_Z?1:0) | (_S?2:0) | (_OV?4:0) | (_CY?8:0);
	return PSW;
}

void V60Cpu::v60WritePSW(uint32_t newval)
{
	/* determine if we need to save / restore the stacks */
	int updateStack = 0;

	/* if the interrupt state is changing, we definitely need to update */
	if ((newval ^ PSW) & 0x10000000)
		updateStack = 1;

	/* if we are not in interrupt mode and the level is changing, we also must update */
	else if (!(PSW & 0x10000000) && ((newval ^ PSW) & 0x03000000))
		updateStack = 1;

	/* save the previous stack value */
	if (updateStack)
		v60SaveStack();

	/* set the new value and update the flags */
	PSW = newval;
	_Z =  (uint8_t)(PSW & 1);
	_S =  (uint8_t)(PSW & 2);
	_OV = (uint8_t)(PSW & 4);
	_CY = (uint8_t)(PSW & 8);

	/* fetch the new stack value */
	if (updateStack)
		v60ReloadStack();
}


uint32_t V60Cpu::v60_update_psw_for_exception(int is_interrupt, int target_level)
{
	uint32_t oldPSW = v60ReadPSW();
	uint32_t newPSW = oldPSW;

	// Change to interrupt context
	newPSW &= ~(3 << 24);  // PSW.EL = 0
	newPSW |= target_level << 24; // set target level
	newPSW &= ~(1 << 18);  // PSW.IE = 0
	newPSW &= ~(1 << 16);  // PSW.TE = 0
	newPSW &= ~(1 << 27);  // PSW.TP = 0
	newPSW &= ~(1 << 17);  // PSW.AE = 0
	newPSW &= ~(1 << 29);  // PSW.EM = 0
	if (is_interrupt)
		newPSW |=  (1 << 28);// PSW.IS = 1
	newPSW |=  (1 << 31);  // PSW.ASA = 1
	v60WritePSW(newPSW);

	return oldPSW;
}


#define GETINTVECT(nint)                    m_program->read_dword((SBR & ~0xfff) + (nint) * 4)
#define EXCEPTION_CODE_AND_SIZE(code, size) (((code) << 16) | (size))


// Addressing mode decoding functions
#include "am.hxx"

// Opcode functions
#include "op12.hxx"
#include "op2.hxx"
#include "op3.hxx"
#include "op4.hxx"
#include "op5.hxx"
#include "op6.hxx"
#include "op7a.hxx"

uint32_t V60Cpu::opUNHANDLED()
{
	fatalerror("Unhandled OpCode found : %02x at %08x\n", OpRead16(PC), PC);
}

// Opcode jump table
#include "optable.hxx"

void V60Cpu::device_start()
{
	m_stall_io = 0;
	m_irq_line = CLEAR_LINE;
	m_nmi_line = CLEAR_LINE;

	for ( int i = 0; i < 68; i++ )
	{
		// Don't set SP (31), PCi (32), PSW (33), SBR (41), SYCW (43), TKCW (44), PIR (45), PSW2 (51)
		if ( i != 31 && i != 32 && i != 33 && i != 41 && i != 43 && i != 44 && i != 45 && i != 51 )
		{
			m_reg[i] = 0;
		}
	}

	m_flags.CY = 0;
	m_flags.OV = 0;
	m_flags.S = 0;
	m_flags.Z = 0;

	m_op1 = 0;
	m_op2 = 0;
	m_flag1 = 0;
	m_flag2 = 0;
	m_instflags = 0;
	m_lenop1 = 0;
	m_lenop2 = 0;
	m_subop = 0;
	m_bamoffset1 = 0;
	m_bamoffset2 = 0;
	m_amflag = 0;
	m_amout = 0;
	m_bamoffset = 0;
	m_amlength1 = 0;
	m_amlength2 = 0;
	m_modadd = 0;
	m_modm = 0;
	m_modval = 0;
	m_modval2 = 0;
	m_modwritevalb = 0;
	m_modwritevalh = 0;
	m_modwritevalw = 0;
	m_moddim = 0;

	// PORT: the 16-bit (V60) branch and the m_pr8/16/32 lambdas dropped, see OpRead8 above
	m_program = &m_program_space;

	m_io = &m_io_space;

	// PORT: save_item / state_add / set_icountptr dropped
}


// PORT: state_export / state_import / state_string_export of the PSW entries
uint32_t V60Cpu::psw()
{
	return v60ReadPSW();    // state_export(V60_PSW): m_debugger_temp = v60ReadPSW()
}

void V60Cpu::set_psw(uint32_t v)
{
	v60WritePSW(v);         // state_import(V60_PSW): v60WritePSW(m_debugger_temp)
}

std::string V60Cpu::flags_string() const
{
	std::string str;
	str += PSW & 1 ? 'Z' : '.';
	str += PSW & 2 ? 'S' : '.';
	str += PSW & 4 ? 'O' : '.';
	str += PSW & 8 ? 'C' : '.';
	return str;
}


void V60Cpu::device_reset()
{
	PSW   = 0x10000000;
	PC    = m_start_pc;
	SBR   = 0x00000000;
	SYCW  = 0x00000070;
	TKCW  = 0x0000e000;
	PSW2  = 0x0000f002;

	_CY   = 0;
	_OV   = 0;
	_S    = 0;
	_Z    = 0;
}


void V60Cpu::stall()
{
	m_stall_io = 1;
}


void V60Cpu::v60_do_irq(int vector)
{
	m_bus.debug_exception_hook(vector);  // PORT: debugger_exception_hook(vector)
	uint32_t oldPSW = v60_update_psw_for_exception(1, 0);

	// Push PC and PSW onto the stack
	SP-=4;
	m_program->write_dword_unaligned(SP, oldPSW);
	SP-=4;
	m_program->write_dword_unaligned(SP, PC);

	// Jump to vector for user interrupt
	PC = GETINTVECT(vector);
}

void V60Cpu::v60_try_irq()
{
	if(m_irq_line == CLEAR_LINE)
		return;
	if((PSW & (1 << 18)) != 0) {
		int vector;
		if(m_irq_line != ASSERT_LINE)
			m_irq_line = CLEAR_LINE;

		vector = standard_irq_callback(0, PC);

		v60_do_irq(vector + 0x40);
	}
}

void V60Cpu::execute_set_input(int irqline, int state)
{
	if(irqline == INPUT_LINE_NMI) {
		switch(state) {
		case ASSERT_LINE:
			if(m_nmi_line == CLEAR_LINE) {
				m_nmi_line = ASSERT_LINE;
				v60_do_irq(2);
			}
			break;
		case CLEAR_LINE:
			m_nmi_line = CLEAR_LINE;
			break;
		}
	} else {
		m_irq_line = state;
		v60_try_irq();
	}
}

// Actual cycles / instruction is unknown

void V60Cpu::execute_run()
{
	m_executing = true;     // PORT: lets set_input_line* catch calls from inside an instruction
	if (m_irq_line != CLEAR_LINE)
		v60_try_irq();

	while (m_icount > 0)
	{
		uint32_t inc;
		m_PPC = PC;
		if (m_instr_hook) m_bus.debug_instruction_hook(PC);  // PORT: debugger_instruction_hook(PC)
		m_icount -= 8;  /* fix me -- this is just an average */
		inc = (this->*s_OpCodeTable[OpRead8(PC)])();
		PC += inc;
		if (m_irq_line != CLEAR_LINE)
			v60_try_irq();
	}
	m_executing = false;   // PORT: see above
}


//**************************************************************************
//  PORT: the parts of MAME's device_execute_interface (emu/diexec.cpp) and
//  device_scheduler (emu/schedule.cpp) the core depends on
//**************************************************************************

void V60Cpu::reset()
{
	m_executing = false;    // PORT: in case a FatalError left execute_run()

	// device_execute_interface::interface_pre_reset: reset the total number of cycles
	m_totalcycles = 0;

	device_reset();

	// device_execute_interface::interface_post_reset: reset the interrupt vectors
	for (device_input &input : m_input)
		input.m_curvector = input.m_stored_vector = 0;   // device_input::reset(); execute_default_irq_vector() is 0
}

// device_input::default_irq_callback
int V60Cpu::default_irq_callback(int irqline)
{
	device_input &input = m_input[irqline];
	int const vector = input.m_curvector;

	// if the IRQ state is HOLD_LINE, clear it
	if (input.m_curstate == HOLD_LINE)
	{
		execute_set_input(irqline, CLEAR_LINE);
		input.m_curstate = CLEAR_LINE;
	}
	return vector;
}

// device_execute_interface::standard_irq_callback
int V60Cpu::standard_irq_callback(int irqline, offs_t pc)
{
	// get the default vector and acknowledge the interrupt if needed
	int vector = default_irq_callback(irqline);

	// if there's a driver callback, run it to get the vector
	vector = m_bus.irq_acknowledge(irqline, vector);   // PORT: m_driver_irq

	// notify the debugger
	m_bus.debug_interrupt_hook(irqline, pc);

	return vector;
}

// device_execute_interface::set_input_line_vector
void V60Cpu::set_input_line_vector(int line, int vector)
{
	m_input[line].m_stored_vector = vector;
}

void V60Cpu::set_input_line(int line, int state)
{
	set_input_line_and_vector(line, state, m_input[line].m_stored_vector);   // USE_STORED_VECTOR
}

// device_input::set_state_synced + empty_event_queue for one event, applied now.
// PORT: MAME queues the event and applies it from a scheduler timer, i.e. between
// timeslices; the board does that scheduling (see v60.hpp).
void V60Cpu::set_input_line_and_vector(int line, int state, int vector)
{
	if (m_executing)
		throw std::logic_error("V60Cpu::set_input_line called from inside execute_run; queue it and apply it after run()");
	if (line < 0 || line >= MAX_INPUT_LINES || line == INPUT_LINE_RESET || line == INPUT_LINE_HALT)
		throw std::invalid_argument("V60Cpu::set_input_line: unsupported line"); // PORT: RESET/HALT are scheduler-level in MAME
	if (state != ASSERT_LINE && state != HOLD_LINE && state != CLEAR_LINE)
		throw std::invalid_argument("V60Cpu::set_input_line: bad state");

	device_input &input = m_input[line];

	// set the input line state and vector
	input.m_curstate = state;
	input.m_curvector = vector;

	// switch off the requested state
	switch (input.m_curstate)
	{
		case HOLD_LINE:
		case ASSERT_LINE:
			execute_set_input(line, ASSERT_LINE);
			break;

		case CLEAR_LINE:
			execute_set_input(line, CLEAR_LINE);
			break;
	}
	// PORT: signal_interrupt_trigger() (wakes devices spinning until an interrupt) is the board's job
}

// device_scheduler::timeslice, the part that runs one device
int V60Cpu::run(int cycles)
{
	int ran = m_cycles_running = cycles;
	m_cycles_stolen = 0;
	m_icount = m_cycles_running;
	execute_run();
	ran -= m_icount;
	ran -= m_cycles_stolen;
	m_totalcycles += ran;
	m_cycles_running = 0;
	m_icount = 0;
	return ran;
}

// device_execute_interface::abort_timeslice
void V60Cpu::abort_timeslice()
{
	// swallow the remaining cycles
	int delta = m_icount;
	m_cycles_stolen += delta;
	m_cycles_running -= delta;
	m_icount -= delta;
}

// device_execute_interface::total_cycles
uint64_t V60Cpu::total_cycles() const
{
	if (m_executing)
		return m_totalcycles + m_cycles_running - m_icount;
	return m_totalcycles;
}

