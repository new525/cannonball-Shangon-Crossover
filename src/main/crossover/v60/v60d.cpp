// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont

// PORT: MAME 0.264 src/devices/cpu/v60/v60d.cpp, verbatim except the class name
// (v60_disassembler -> V60Disassembler) and this block. See v60d.hpp.
#include "v60d.hpp"

#include <cstdio>
#include <type_traits>

namespace {
namespace util {

// PORT: util::stream_format (lib/util/strformat.h) for the argument types v60d.cpp passes
// (const char *, int, unsigned, u8/u16/u32): with these, MAME's type-safe formatter and
// printf produce the same text ("%X" of an int is its 32-bit two's complement in both,
// "%-8s" pads the same). A narrow signed type would differ (MAME prints it at its own
// width), hence the check.
template <typename T> inline T fmt_arg(T v)
{
	static_assert(std::is_same_v<T, const char *> || (std::is_integral_v<T> && (sizeof(T) == 4 || std::is_unsigned_v<T>)), "unexpected stream_format argument");
	return v;
}

template <typename... Args> void stream_format(std::ostream &stream, const char *format, Args... args)
{
	char buf[256];
	if constexpr (sizeof...(Args) == 0)
		snprintf(buf, sizeof(buf), format, 0);   // (a spare argument keeps -Wformat-security quiet; printf ignores it)
	else
		snprintf(buf, sizeof(buf), format, fmt_arg(args)...);
	stream << buf;
}

} // namespace util
} // anonymous namespace

// PORT: MAME's global u8..u64 / offs_t typedefs, for this file only
using u8 = V60Disassembler::u8;
using u16 = V60Disassembler::u16;
using u32 = V60Disassembler::u32;
using u64 = V60Disassembler::u64;
using offs_t = V60Disassembler::offs_t;

// Register names
const char *const V60Disassembler::v60_reg_names[69] = {
	"R0", "R1", "R2", "R3",
	"R4", "R5", "R6", "R7",
	"R8", "R9", "R10", "R11",
	"R12", "R13", "R14", "R15",
	"R16", "R17", "R18", "R19",
	"R20", "R21", "R22", "R23",
	"R24", "R25", "R26", "R27",
	"R28", "AP", "FP", "SP",
	"PC", "PSW","Unk","Unk",
	"ISP", "L0SP", "L1SP", "L2SP",
	"L3SP", "SBR","TR","SYCW",
	"TKCW", "PIR", "Reserved","Reserved",
	"Reserved","Reserved","Reserved","PSW2",
	"ATBR0", "ATLR0", "ATBR1", "ATLR1",
	"ATBR2", "ATLR2", "ATBR3", "ATLR3",
	"TRMODE", "ADTR0", "ADTR1","ADTMR0",
	"ADTMR1","Reserved","Reserved","Reserved"
};

void V60Disassembler::out_AM_Register(int reg, std::ostream &stream)
{
	stream << v60_reg_names[reg];
}

void V60Disassembler::out_AM_RegisterIndirect(int reg, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		stream << '@';
	util::stream_format(stream, "[%s]", v60_reg_names[reg]);
}

void V60Disassembler::out_AM_RegisterIndirectIndexed(int rn, int rx, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		util::stream_format(stream, "%s@[%s]", v60_reg_names[rx], v60_reg_names[rn]);
	else
		util::stream_format(stream, "[%s](%s)", v60_reg_names[rn], v60_reg_names[rx]);

}

void V60Disassembler::out_AM_Autoincrement(int reg, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		stream << '@';
	util::stream_format(stream, "[%s+]", v60_reg_names[reg]);
}

void V60Disassembler::out_AM_Autodecrement(int reg, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		stream << '@';
	util::stream_format(stream, "[-%s]", v60_reg_names[reg]);
}

void V60Disassembler::out_AM_Displacement(int reg, int disp, int opsize, std::ostream &stream)
{
	util::stream_format(stream, "%s%X%s[%s]",
			disp >= 0 ? "" : "-", disp >= 0 ? disp : -disp,
			opsize & 0x80 ? "@" : "",
			v60_reg_names[reg]);
}

void V60Disassembler::out_AM_DisplacementIndexed(int rn, int rx, int disp, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		util::stream_format(stream, "%s@%s%X[%s]", v60_reg_names[rx], disp >= 0 ? "" : "-", disp >= 0 ? disp : -disp,v60_reg_names[rn]);
	else
		util::stream_format(stream, "%s%X[%s](%s)", disp >= 0 ? "" : "-", disp >= 0 ? disp : -disp,v60_reg_names[rn], v60_reg_names[rx]);
}

void V60Disassembler::out_AM_PCDisplacement(offs_t pc, int disp, int opsize, std::ostream &stream)
{
	util::stream_format(stream, "%X%s[PC]", pc+disp, opsize & 0x80 ? "@" : "");
}

void V60Disassembler::out_AM_PCDisplacementIndexed(offs_t pc, int disp, int rx, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		util::stream_format(stream, "%s@%X[PC]", v60_reg_names[rx], pc+disp);
	else
		util::stream_format(stream, "%X[PC](%s)", pc+disp, v60_reg_names[rx]);
}

void V60Disassembler::out_AM_DisplacementIndirect(int reg, int disp, int opsize, std::ostream &stream)
{
	util::stream_format(stream, "%s[%s%X[%s]]",
			opsize & 0x80 ? "@" : "",
			disp >= 0 ? "" : "-", disp >= 0 ? disp : -disp,
			v60_reg_names[reg]);
}

void V60Disassembler::out_AM_DisplacementIndirectIndexed(int rn, int rx, int disp, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		util::stream_format(stream, "%s@[%s%X[%s]]", v60_reg_names[rx], disp >= 0 ? "" : "-", disp >= 0 ? disp : -disp,v60_reg_names[rn]);
	else
		util::stream_format(stream, "[%s%X[%s]](%s)", disp >= 0 ? "" : "-", disp >= 0 ? disp : -disp,v60_reg_names[rn], v60_reg_names[rx]);
}

void V60Disassembler::out_AM_PCDisplacementIndirect(offs_t pc, int disp, int opsize, std::ostream &stream)
{
	util::stream_format(stream, "%s[%X[PC]]", opsize & 0x80 ? "@" : "", pc+disp);
}

void V60Disassembler::out_AM_PCDisplacementIndirectIndexed(offs_t pc, int disp, int rx, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		util::stream_format(stream, "%s@[%X[PC]]", v60_reg_names[rx], pc+disp);
	else
		util::stream_format(stream, "[%X[PC]](%s)", pc+disp, v60_reg_names[rx]);
}

void V60Disassembler::out_AM_DoubleDisplacement(int reg, int disp2, int disp1, int opsize, std::ostream &stream)
{
	util::stream_format(stream, "%s%X%s[%s%X[%s]]",
			disp1 >= 0 ? "" : "-", disp1 >= 0 ? disp1 : -disp1,
			opsize & 0x80 ? "@" : "",
			disp2 >= 0 ? "" : "-", disp2 >= 0 ? disp2 : -disp2,
			v60_reg_names[reg]);
}

void V60Disassembler::out_AM_PCDoubleDisplacement(offs_t pc, int disp2, int disp1, int opsize, std::ostream &stream)
{
	util::stream_format(stream, "%s%X%s[%X[PC]]",
			disp1 >= 0 ? "" : "-", disp1 >= 0 ? disp1 : -disp1,
			opsize & 0x80 ? "@" : "",
			disp2 + pc);
}

void V60Disassembler::out_AM_DirectAddress(unsigned addr, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		stream << '@';
	util::stream_format(stream, "%X", addr);
}

void V60Disassembler::out_AM_DirectAddressIndexed(unsigned addr, int rx, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		util::stream_format(stream, "%s@%X", v60_reg_names[rx], addr);
	else
		util::stream_format(stream, "%X(%s)", addr, v60_reg_names[rx]);
}

void V60Disassembler::out_AM_DirectAddressDeferred(unsigned addr, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		stream << '@';
	util::stream_format(stream, "[%X]", addr);
}

void V60Disassembler::out_AM_DirectAddressDeferredIndexed(unsigned addr, int rx, int opsize, std::ostream &stream)
{
	if(opsize & 0x80)
		util::stream_format(stream, "%s@[%X]", v60_reg_names[rx], addr);
	else
		util::stream_format(stream, "[%X](%s)", addr, v60_reg_names[rx]);
}

void V60Disassembler::out_AM_Immediate(unsigned value, int opsize, std::ostream &stream)
{
	if(opsize == 0)
		value &= 0xff;
	else if(opsize == 1)
		value &= 0xffff;

	util::stream_format(stream, "#%X", value);
}

u32 V60Disassembler::decode_AM(unsigned ipc, offs_t pc, int m, int opsize, const data_buffer &opcodes, std::ostream &stream)
{
	unsigned char mod = opcodes.r8(pc);
	if(m) {
		switch(mod>>5) {
		case 0: // Double displacement (8 bit)
			out_AM_DoubleDisplacement(mod&0x1F, int8_t(opcodes.r8(pc+1)), int8_t(opcodes.r8(pc+2)), opsize, stream);
			return 3;

		case 1: // Double displacement (16 bit)
			out_AM_DoubleDisplacement(mod&0x1F, int16_t(opcodes.r16(pc+1)), int16_t(opcodes.r16(pc+3)), opsize, stream);
			return 5;

		case 2: // Double displacement (32 bit)
			out_AM_DoubleDisplacement(mod&0x1F, int32_t(opcodes.r32(pc+1)), int32_t(opcodes.r32(pc+5)), opsize, stream);
			return 9;

		case 3: // Register
			out_AM_Register(mod&0x1F, stream);
			return 1;

		case 4: // Autoincrement
			out_AM_Autoincrement(mod&0x1F, opsize, stream);
			return 1;

		case 5: // Autodecrement
			out_AM_Autodecrement(mod&0x1F, opsize, stream);
			return 1;

		case 6:
			switch (opcodes.r8(pc+1)>>5)
				{
				case 0: // Displacement indexed (8 bit)
					out_AM_DisplacementIndexed(opcodes.r8(pc+1)&0x1F, mod&0x1F, int8_t(opcodes.r8(pc+2)), opsize, stream);
					return 3;

				case 1: // Displacement indexed (16 bit)
					out_AM_DisplacementIndexed(opcodes.r8(pc+1)&0x1F, mod&0x1F, int16_t(opcodes.r16(pc+2)), opsize, stream);
					return 4;

				case 2: // Displacement indexed (32 bit)
					out_AM_DisplacementIndexed(opcodes.r8(pc+1)&0x1F, mod&0x1F, int32_t(opcodes.r32(pc+2)), opsize, stream);
					return 6;

				case 3: // Register indirect indexed
					out_AM_RegisterIndirectIndexed(opcodes.r8(pc+1)&0x1F, mod&0x1F, opsize, stream);
					return 2;

				case 4: // Displacement indirect indexed (8 bit)
					out_AM_DisplacementIndirectIndexed(opcodes.r8(pc+1)&0x1F, mod&0x1F, int8_t(opcodes.r8(pc+2)), opsize, stream);
					return 3;

				case 5: // Displacement indirect indexed (16 bit)
					out_AM_DisplacementIndirectIndexed(opcodes.r8(pc+1)&0x1F, mod&0x1F, int16_t(opcodes.r16(pc+2)), opsize, stream);
					return 4;

				case 6: // Displacement indirect indexed (32 bit)
					out_AM_DisplacementIndirectIndexed(opcodes.r8(pc+1)&0x1F, mod&0x1F, int32_t(opcodes.r32(pc+2)), opsize, stream);
					return 6;

				case 7:
					switch (opcodes.r8(pc+1)&0x1F)
						{
						case 16: // PC Displacement Indexed (8 bit)
							out_AM_PCDisplacementIndexed(ipc, int8_t(opcodes.r8(pc+2)), mod&0x1F, opsize, stream);
							return 3;

						case 17: // PC Displacement Indexed (16 bit)
							out_AM_PCDisplacementIndexed(ipc, int16_t(opcodes.r16(pc+2)), mod&0x1F, opsize, stream);
							return 4;

						case 18: // PC Displacement Indexed (32 bit)
							out_AM_PCDisplacementIndexed(ipc, int32_t(opcodes.r32(pc+2)), mod&0x1F, opsize, stream);
							return 6;

						case 19: // Direct Address Indexed
							out_AM_DirectAddressIndexed(opcodes.r32(pc+2), mod&0x1F, opsize, stream);
							return 6;

						case 24: // PC Displacement Indirect Indexed(8 bit)
							out_AM_PCDisplacementIndirectIndexed(ipc, int8_t(opcodes.r8(pc+2)), mod&0x1F, opsize, stream);
							return 3;

						case 25: // PC Displacement Indirect Indexed (16 bit)
							out_AM_PCDisplacementIndirectIndexed(ipc, int16_t(opcodes.r16(pc+2)), mod&0x1F, opsize, stream);
							return 4;

						case 26: // PC Displacement Indirect Indexed (32 bit)
							out_AM_PCDisplacementIndirectIndexed(ipc, int32_t(opcodes.r32(pc+2)), mod&0x1F, opsize, stream);
							return 6;

						case 27: // Direct Address Deferred Indexed
							out_AM_DirectAddressDeferredIndexed(opcodes.r32(pc+2), mod&0x1F, opsize, stream);
							return 6;

						default:
							stream << "!ERRAM3";
							return 1;
						}

				default:
					stream << "!ERRAM2";
					return 1;
				}

		default:
			stream << "!ERRAM1";
			return 1;
		}
	} else {
		switch(mod>>5) {
		case 0: // Displacement (8 bit)
			out_AM_Displacement(mod&0x1F, int8_t(opcodes.r8(pc+1)), opsize, stream);
			return 2;

		case 1: // Displacement (16 bit)
			out_AM_Displacement(mod&0x1F, int16_t(opcodes.r16(pc+1)), opsize, stream);
			return 3;

		case 2: // Displacement (32 bit)
			out_AM_Displacement(mod&0x1F, int32_t(opcodes.r32(pc+1)), opsize, stream);
			return 5;

		case 3: // Register indirect
			out_AM_RegisterIndirect(mod&0x1F, opsize, stream);
			return 1;

		case 4: // Displacement indirect (8 bit)
			out_AM_DisplacementIndirect(mod&0x1F, int8_t(opcodes.r8(pc+1)), opsize, stream);
			return 2;

		case 5: // Displacement indirect (16 bit)
			out_AM_DisplacementIndirect(mod&0x1F, int16_t(opcodes.r16(pc+1)), opsize, stream);
			return 3;

		case 6: // Displacement indirect (32 bit)
			out_AM_DisplacementIndirect(mod&0x1F, int32_t(opcodes.r32(pc+1)), opsize, stream);
			return 5;

		case 7:
			switch(mod&0x1F) {
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
				out_AM_Immediate(mod&0x1F, opsize, stream);
				return 1;

			case 16: // PC Displacement (8 bit)
				out_AM_PCDisplacement(ipc, int8_t(opcodes.r8(pc+1)), opsize, stream);
				return 2;

			case 17: // PC Displacement (16 bit)
				out_AM_PCDisplacement(ipc, int16_t(opcodes.r16(pc+1)), opsize, stream);
				return 3;

			case 18: // PC Displacement (32 bit)
				out_AM_PCDisplacement(ipc, int32_t(opcodes.r32(pc+1)), opsize, stream);
				return 5;

			case 19: // Direct Address
				out_AM_DirectAddress(opcodes.r32(pc+1), opsize, stream);
				return 5;


			case 20:
				switch(opsize&0x7F) {
				case 0: // Immediate (8 bit)
					out_AM_Immediate(opcodes.r8(pc+1), opsize, stream);
					return 2;

				case 1: // Immediate (16 bit)
					out_AM_Immediate(opcodes.r16(pc+1), opsize, stream);
					return 3;

				case 2: // Immediate (32 bit)
					out_AM_Immediate(opcodes.r32(pc+1), opsize, stream);
					return 5;

				default:
					stream << "!ERRAM6";
					return 1;
				}

			case 24: // PC Displacement Indirect (8 bit)
				out_AM_PCDisplacementIndirect(ipc, int8_t(opcodes.r8(pc+1)), opsize, stream);
				return 2;

			case 25: // PC Displacement Indirect (16 bit)
				out_AM_PCDisplacementIndirect(ipc, int16_t(opcodes.r16(pc+1)), opsize, stream);
				return 3;

			case 26: // PC Displacement Indirect (32 bit)
				out_AM_PCDisplacementIndirect(ipc, int32_t(opcodes.r32(pc+1)), opsize, stream);
				return 5;

			case 27: // Direct Address Deferred
				out_AM_DirectAddressDeferred(opcodes.r32(pc+1), opsize, stream);
				return 5;

			case 28: // PC Double Displacement (8 bit)
				out_AM_PCDoubleDisplacement(ipc, int8_t(opcodes.r8(pc+1)), int8_t(opcodes.r8(pc+2)), opsize, stream);
				return 3;

			case 29: // PC Double Displacement (16 bit)
				out_AM_PCDoubleDisplacement(ipc, int16_t(opcodes.r16(pc+1)), int16_t(opcodes.r16(pc+3)), opsize, stream);
				return 5;

			case 30: // PC Double Displacement (32 bit)
				out_AM_PCDoubleDisplacement(ipc, int32_t(opcodes.r32(pc+1)), int32_t(opcodes.r32(pc+5)), opsize, stream);
				return 9;

			default:
				stream << "!ERRAM5";
				return 1;
			}

		default:
			stream << "!ERRAM4";
			return 1;
		}
	}
}


u32 V60Disassembler::decode_F1(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	unsigned char code = opcodes.r8(pc);
	util::stream_format(stream, "%-8s", opnm);
	if(code & 0x20) {
		int ret = decode_AM(ipc, pc+1, code & 0x40, opsize1, opcodes, stream) + 2;
		stream << ", ";
		out_AM_Register(code & 0x1f, stream);
		return ret;
	} else {
		out_AM_Register(code & 0x1f, stream);
		stream << ", ";
		return decode_AM(ipc, pc+1, code & 0x40, opsize1, opcodes, stream) + 2;
	}
}

u32 V60Disassembler::decode_F2(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	int ret;
	unsigned char code = opcodes.r8(pc);
	util::stream_format(stream, "%-8s", opnm);
	ret = decode_AM(ipc, pc+1, code & 0x40, opsize1, opcodes, stream);
	stream << ", ";
	ret += decode_AM(ipc, pc+1+ret, code & 0x20, opsize2, opcodes, stream);
	return ret+2;
}

u32 V60Disassembler::decode_F1F2(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	if(opcodes.r8(pc) & 0x80)
		return decode_F2(opnm, opsize1, opsize2, ipc, pc, opcodes, stream);
	else
		return decode_F1(opnm, opsize1, opsize2, ipc, pc, opcodes, stream);
}

u32 V60Disassembler::decode_F3(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "%-8s", opnm);
	return decode_AM(ipc, pc, opcodes.r8(pc-1) & 1, opsize1, opcodes, stream) + 1;
}

u32 V60Disassembler::decode_F4a(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "%-8s%X", opnm, ipc + int8_t(opcodes.r8(pc)));
	return 2;
}

u32 V60Disassembler::decode_F4b(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "%-8s%X", opnm, ipc + int16_t(opcodes.r16(pc)));
	return 3;
}

u32 V60Disassembler::decode_F5(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	stream << opnm;
	return 1;
}

u32 V60Disassembler::decode_F6(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "%-8s%s, %X[PC]", opnm, v60_reg_names[opcodes.r8(pc) & 0x1f], ipc + int16_t(opcodes.r16(pc+1)));
	return 4;
}

u32 V60Disassembler::decode_F7a(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	int ret;
	unsigned char code = opcodes.r8(pc);
	unsigned char code2;

	util::stream_format(stream, "%-8s", opnm);
	ret = decode_AM(ipc, pc+1, code & 0x40, opsize1, opcodes, stream);
	stream << ", ";

	code2 = opcodes.r8(pc+1+ret);
	if(code2 & 0x80)
		out_AM_Register(code2 & 0x1f, stream);
	else
		out_AM_Immediate(code2, 1, stream);
	stream << ", ";

	ret += decode_AM(ipc, pc+2+ret, code & 0x20, opsize2, opcodes, stream);
	stream << ", ";

	code2 = opcodes.r8(pc+2+ret);
	if(code2 & 0x80)
		out_AM_Register(code2 & 0x1f, stream);
	else
		out_AM_Immediate(code2, 1, stream);

	return ret+4;
}

u32 V60Disassembler::decode_F7b(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	int ret;
	unsigned char code = opcodes.r8(pc);
	unsigned char code2;

	util::stream_format(stream, "%-8s", opnm);
	ret = decode_AM(ipc, pc+1, code & 0x40, opsize1, opcodes, stream);
	stream << ", ";

	code2 = opcodes.r8(pc+1+ret);
	if(code2 & 0x80)
		out_AM_Register(code2 & 0x1f, stream);
	else
		out_AM_Immediate(code2, 1, stream);
	stream << ", ";

	ret += decode_AM(ipc, pc+2+ret, code & 0x20, opsize2, opcodes, stream);

	return ret+3;
}

u32 V60Disassembler::decode_F7c(const char *opnm, int opsize1, int opsize2, unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	int ret;
	unsigned char code = opcodes.r8(pc);
	unsigned char code2;

	util::stream_format(stream, "%-8s", opnm);
	ret = decode_AM(ipc, pc+1, code & 0x40, opsize1, opcodes, stream);
	stream << ", ";

	ret += decode_AM(ipc, pc+1+ret, code & 0x20, opsize2, opcodes, stream);
	stream << ", ";

	code2 = opcodes.r8(pc+1+ret);
	if(code2 & 0x80)
		out_AM_Register(code2 & 0x1f, stream);
	else
		out_AM_Immediate(code2, 1, stream);

	return ret+3;
}

u32 V60Disassembler::dopUNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$%02X", opcodes.r8(pc));
	return 1;
}

u32 V60Disassembler::dop58UNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$58");
	return 1;
}

u32 V60Disassembler::dop59UNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$59");
	return 1;
}

u32 V60Disassembler::dop5AUNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$5A");
	return 1;
}

u32 V60Disassembler::dop5BUNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$5B");
	return 1;
}

u32 V60Disassembler::dop5CUNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$5C");
	return 1;
}

u32 V60Disassembler::dop5DUNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$5D");
	return 1;
}

u32 V60Disassembler::dop5EUNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$5E");
	return 1;
}

u32 V60Disassembler::dop5FUNHANDLED(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	util::stream_format(stream, "$5F");
	return 1;
}

#define DEFINE_EASY_OPCODE(name, opnm, ftype, opsize1, opsize2) \
	u32 V60Disassembler::dop ## name(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) \
	{ \
		return decode_ ## ftype(opnm, opsize1, opsize2, ipc, pc, opcodes, stream); \
	}

#define DEFINE_EASY_OPCODE_EX(name, opnm, ftype, opsize1, opsize2, flags) \
	u32 V60Disassembler::dop ## name(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) \
	{ \
		return decode_ ## ftype(opnm, opsize1, opsize2, ipc, pc, opcodes, stream) | (flags); \
	}

#define DEFINE_TRIPLE_OPCODE(name, string, ftype) \
	DEFINE_EASY_OPCODE(name##B,string ".b", ftype, 0, 0) \
	DEFINE_EASY_OPCODE(name##H,string ".h", ftype, 1, 1) \
	DEFINE_EASY_OPCODE(name##W,string ".w", ftype, 2, 2)

#define DEFINE_DOUBLE_OPCODE(name, string, ftype) \
	DEFINE_EASY_OPCODE(name##B,string ".b", ftype, 0, 0) \
	DEFINE_EASY_OPCODE(name##H,string ".h", ftype, 1, 1)

#define DEFINE_FPU_OPCODE(name,string,ftype) \
	DEFINE_EASY_OPCODE(name##S,string ".s", ftype, 2, 2) \
	DEFINE_EASY_OPCODE(name##L,string ".l", ftype, 2, 2)


DEFINE_FPU_OPCODE(ABSF, "absf", F2)
DEFINE_TRIPLE_OPCODE(ADD, "add", F1F2)
DEFINE_TRIPLE_OPCODE(ADDC, "addc", F1F2)
DEFINE_EASY_OPCODE(ADDDC, "adddc", F7c, 0, 0)
DEFINE_FPU_OPCODE(ADDF, "addf", F2)
DEFINE_TRIPLE_OPCODE(AND, "and", F1F2)
DEFINE_EASY_OPCODE(ANDBSU, "andbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(ANDBSD, "andbsd", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(ANDNBSU, "andnbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(ANDNBSD, "andnbsd", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE_EX(BGT8, "bgt", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BGT16, "bgt", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BGE8, "bge", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BGE16, "bge", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BLT8, "blt", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BLT16, "blt", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BLE8, "ble", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BLE16, "ble", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BH8, "bh", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BH16, "bh", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNL8, "bnl", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNL16, "bnl", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BL8, "bl", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BL16, "bl", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNH8, "bnh", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNH16, "bnh", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BE8, "be", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BE16, "be", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNE8, "bne", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNE16, "bne", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BV8, "bv", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BV16, "bv", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNV8, "bnv", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BNV16, "bnv", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BN8, "bn", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BN16, "bn", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BP8, "bp", F4a, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(BP16, "bp", F4b, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE(BR8, "br", F4a, 0, 0)
DEFINE_EASY_OPCODE(BR16, "br", F4b, 0, 0)
DEFINE_EASY_OPCODE_EX(BRK, "brk", F5, 0, 0, STEP_OVER)
DEFINE_EASY_OPCODE_EX(BRKV, "brkv", F5, 0, 0, STEP_OVER | STEP_COND)
DEFINE_EASY_OPCODE_EX(BSR, "bsr", F4b, 0, 0, STEP_OVER)
DEFINE_EASY_OPCODE_EX(CALL, "call", F1F2, 0, 2, STEP_OVER)
DEFINE_EASY_OPCODE(CAXI, "caxi", F1, 2, 2)
DEFINE_EASY_OPCODE(CHKAR, "chkar", F1F2, 0, 0) // ?
DEFINE_EASY_OPCODE(CHKAW, "chkaw", F1F2, 0, 0) // ?
DEFINE_EASY_OPCODE(CHKAE, "chkae", F1F2, 0, 0)
DEFINE_EASY_OPCODE(CHLVL, "chlvl", F1F2, 0, 0)
DEFINE_EASY_OPCODE(CLR1, "clr1", F1F2, 2, 2)
DEFINE_EASY_OPCODE(CLRTLB, "clrtlb", F3, 0, 0) // ?
DEFINE_EASY_OPCODE(CLRTLBA, "clrtlba", F5, 0, 0)
DEFINE_TRIPLE_OPCODE(CMP, "cmp", F1F2)
DEFINE_EASY_OPCODE(CMPBFS, "cmpbfs", F7b, 0x82, 2)
DEFINE_EASY_OPCODE(CMPBFZ, "cmpbfz", F7b, 0x82, 2)
DEFINE_EASY_OPCODE(CMPBFL, "cmpbfl", F7b, 0x82, 2)
DEFINE_DOUBLE_OPCODE(CMPC, "cmpc", F7a)
DEFINE_DOUBLE_OPCODE(CMPCF, "cmpcf", F7a)
DEFINE_DOUBLE_OPCODE(CMPCS, "cmpcs", F7a)
DEFINE_FPU_OPCODE(CMPF, "cmpf", F2)
DEFINE_EASY_OPCODE(CVTSL, "cvt.sl", F2, 0, 1)
DEFINE_EASY_OPCODE(CVTLS, "cvt.ls", F2, 1, 0)
DEFINE_EASY_OPCODE(CVTWS, "cvt.ws", F2, 2, 0)
DEFINE_EASY_OPCODE(CVTWL, "cvt.wl", F2, 2, 1)
DEFINE_EASY_OPCODE(CVTSW, "cvt.sw", F2, 0, 2)
DEFINE_EASY_OPCODE(CVTLW, "cvt.lw", F2, 1, 2)
DEFINE_EASY_OPCODE(CVTDPZ, "cvtd.pz", F7c, 0, 1)
DEFINE_EASY_OPCODE(CVTDZP, "cvtd.zp", F7c, 1, 0)
DEFINE_EASY_OPCODE_EX(DBGT, "dbgt", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBGE, "dbge", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBLT, "dbgt", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBLE, "dbge", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBH, "dbh", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBNL, "dbnl", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBL, "dbl", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBNH, "dbnh", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBE, "dbe", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBNE, "dbne", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBV, "dbe", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBNV, "dbne", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBN, "dbn", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBP, "dbp", F6, 0, 0, STEP_COND)
DEFINE_EASY_OPCODE_EX(DBR, "dbr", F6, 0, 0, STEP_COND)
DEFINE_TRIPLE_OPCODE(DEC, "dec", F3)
DEFINE_EASY_OPCODE(DISPOSE, "dispose", F5, 0, 0)
DEFINE_TRIPLE_OPCODE(DIV, "div", F1F2)
DEFINE_FPU_OPCODE(DIVF, "divf", F2)
DEFINE_TRIPLE_OPCODE(DIVU, "divu", F1F2)
DEFINE_EASY_OPCODE(DIVX, "divx", F1F2, 2, 3)
DEFINE_EASY_OPCODE(DIVUX, "divux", F1F2, 2, 3)
DEFINE_EASY_OPCODE(EXTBFS, "extbfs", F7b, 0x82, 2)
DEFINE_EASY_OPCODE(EXTBFZ, "extbfz", F7b, 0x82, 2)
DEFINE_EASY_OPCODE(EXTBFL, "extbfl", F7b, 0x82, 2)
DEFINE_EASY_OPCODE(GETATE, "getate", F1F2, 0, 3) // ?
DEFINE_EASY_OPCODE(GETPSW, "getpsw", F3, 2, 0)
DEFINE_EASY_OPCODE(GETPTE, "getpte", F1F2, 0, 2) // ?
DEFINE_EASY_OPCODE(GETRA, "getra", F1F2, 0, 2) // ?
DEFINE_EASY_OPCODE(HALT, "halt", F5, 0, 0)
DEFINE_TRIPLE_OPCODE(IN, "in", F1F2)
DEFINE_TRIPLE_OPCODE(INC, "inc", F3)
DEFINE_EASY_OPCODE(INSBFL, "insbfl", F7c, 2, 0x82)
DEFINE_EASY_OPCODE(INSBFR, "insbfr", F7c, 2, 0x82)
DEFINE_EASY_OPCODE(JMP, "jmp", F3, 0, 0)
DEFINE_EASY_OPCODE_EX(JSR, "jsr", F3, 0, 0, STEP_OVER)
DEFINE_EASY_OPCODE(LDPR, "ldpr", F1F2, 2, 2)
DEFINE_EASY_OPCODE(LDTASK, "ldtask", F1F2, 2, 2)
DEFINE_TRIPLE_OPCODE(MOV, "mov", F1F2)
DEFINE_EASY_OPCODE(MOVBSU, "movbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(MOVBSD, "movbsd", F7b, 0x80, 0x80)
DEFINE_DOUBLE_OPCODE(MOVCU, "movcu", F7a)
DEFINE_DOUBLE_OPCODE(MOVCD, "movcd", F7a)
DEFINE_DOUBLE_OPCODE(MOVCFU, "movcfu", F7a)
DEFINE_DOUBLE_OPCODE(MOVCFD, "movcfd", F7a)
DEFINE_DOUBLE_OPCODE(MOVCS, "movcs", F7a)
DEFINE_EASY_OPCODE(MOVD, "mov.d", F1F2, 3, 3)
DEFINE_EASY_OPCODE(MOVEAB, "movea.b", F1F2, 0, 2)
DEFINE_EASY_OPCODE(MOVEAH, "movea.h", F1F2, 1, 2)
DEFINE_EASY_OPCODE(MOVEAW, "movea.w", F1F2, 2, 2)
DEFINE_FPU_OPCODE(MOVF, "movf", F2)
DEFINE_EASY_OPCODE(MOVSBH, "movs.bh", F1F2, 0, 1)
DEFINE_EASY_OPCODE(MOVSBW, "movs.bw", F1F2, 0, 2)
DEFINE_EASY_OPCODE(MOVSHW, "movs.hw", F1F2, 1, 2)
DEFINE_EASY_OPCODE(MOVTHB, "movt.hb", F1F2, 1, 0)
DEFINE_EASY_OPCODE(MOVTWB, "movt.wb", F1F2, 2, 0)
DEFINE_EASY_OPCODE(MOVTWH, "movt.wh", F1F2, 2, 1)
DEFINE_EASY_OPCODE(MOVZBH, "movz.bh", F1F2, 0, 1)
DEFINE_EASY_OPCODE(MOVZBW, "movz.bw", F1F2, 0, 2)
DEFINE_EASY_OPCODE(MOVZHW, "movz.hw", F1F2, 1, 2)
DEFINE_TRIPLE_OPCODE(MUL, "mul", F1F2)
DEFINE_FPU_OPCODE(MULF, "mulf", F2)
DEFINE_TRIPLE_OPCODE(MULU, "mulu", F1F2)
DEFINE_EASY_OPCODE(MULX, "mulx", F1F2, 2, 3)
DEFINE_EASY_OPCODE(MULUX, "mulux", F1F2, 2, 3)
DEFINE_TRIPLE_OPCODE(NEG, "neg", F1F2)
DEFINE_FPU_OPCODE(NEGF, "negf", F2)
DEFINE_EASY_OPCODE(NOP, "nop", F5, 0, 0)
DEFINE_TRIPLE_OPCODE(NOT, "not", F1F2)
DEFINE_EASY_OPCODE(NOT1, "not1", F1F2, 2, 2)
DEFINE_EASY_OPCODE(NOTBSU, "notbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(NOTBSD, "notbsd", F7b, 0x80, 0x80)
DEFINE_TRIPLE_OPCODE(OR, "or", F1F2)
DEFINE_EASY_OPCODE(ORBSU, "orbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(ORBSD, "orbsd", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(ORNBSU, "ornbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(ORNBSD, "ornbsd", F7b, 0x80, 0x80)
DEFINE_TRIPLE_OPCODE(OUT, "out", F1F2)
DEFINE_EASY_OPCODE(POP, "pop", F3, 2, 0)
DEFINE_EASY_OPCODE(POPM, "popm", F3, 2, 0)
DEFINE_EASY_OPCODE(PREPARE, "prepare", F3, 2, 0)
DEFINE_EASY_OPCODE(PUSH, "push", F3, 2, 0)
DEFINE_EASY_OPCODE(PUSHM, "pushm", F3, 2, 0)
DEFINE_TRIPLE_OPCODE(REM, "rem", F1F2)
DEFINE_TRIPLE_OPCODE(REMU, "remu", F1F2)
DEFINE_EASY_OPCODE_EX(RET, "ret", F3, 2, 0, STEP_OUT)
DEFINE_EASY_OPCODE_EX(RETIU, "retiu", F3, 1, 0, STEP_OUT)
DEFINE_EASY_OPCODE_EX(RETIS, "retis", F3, 1, 0, STEP_OUT)
DEFINE_EASY_OPCODE(ROTB, "rot.b", F1F2, 0, 0)
DEFINE_EASY_OPCODE(ROTH, "rot.h", F1F2, 0, 1)
DEFINE_EASY_OPCODE(ROTW, "rot.w", F1F2, 0, 2)
DEFINE_EASY_OPCODE(ROTCB, "rotc.b", F1F2, 0, 0)
DEFINE_EASY_OPCODE(ROTCH, "rotc.h", F1F2, 0, 1)
DEFINE_EASY_OPCODE(ROTCW, "rotc.w", F1F2, 0, 2)
DEFINE_EASY_OPCODE_EX(RSR, "rsr", F5, 0, 0, STEP_OUT)
DEFINE_EASY_OPCODE(RVBIT, "rvbit", F1F2, 0, 0)
DEFINE_EASY_OPCODE(RVBYT, "rvbyt", F1F2, 2, 2)
DEFINE_EASY_OPCODE(SCH0BSU, "sch0bsu", F7b, 0x80, 2)
DEFINE_EASY_OPCODE(SCH0BSD, "sch0bsd", F7b, 0x80, 2)
DEFINE_EASY_OPCODE(SCH1BSU, "sch1bsu", F7b, 0x80, 2)
DEFINE_EASY_OPCODE(SCH1BSD, "sch1bsd", F7b, 0x80, 2)
DEFINE_EASY_OPCODE(SCHCUB, "schcu.b", F7b, 0, 0)
DEFINE_EASY_OPCODE(SCHCDB, "schcd.b", F7b, 0, 0)
DEFINE_EASY_OPCODE(SCHCUH, "schcu.h", F7b, 1, 1)
DEFINE_EASY_OPCODE(SCHCDH, "schcd.h", F7b, 1, 1)
DEFINE_EASY_OPCODE(SCLFS, "sclf.s", F2, 1, 2)
DEFINE_EASY_OPCODE(SCLFL, "sclf.l", F2, 1, 2)
DEFINE_EASY_OPCODE(SET1, "set1", F1F2, 2, 2)
DEFINE_EASY_OPCODE(SETF, "setf", F1F2, 0, 0)
DEFINE_EASY_OPCODE(SHAB, "sha.b", F1F2, 0, 0)
DEFINE_EASY_OPCODE(SHAH, "sha.h", F1F2, 0, 1)
DEFINE_EASY_OPCODE(SHAW, "sha.w", F1F2, 0, 2)
DEFINE_EASY_OPCODE(SHLB, "shl.b", F1F2, 0, 0)
DEFINE_EASY_OPCODE(SHLH, "shl.h", F1F2, 0, 1)
DEFINE_EASY_OPCODE(SHLW, "shl.w", F1F2, 0, 2)
DEFINE_EASY_OPCODE(SKPCUB, "skpcu.b", F7b, 0, 0)
DEFINE_EASY_OPCODE(SKPCDB, "skpcd.b", F7b, 0, 0)
DEFINE_EASY_OPCODE(SKPCUH, "skpcu.h", F7b, 1, 1)
DEFINE_EASY_OPCODE(SKPCDH, "skpcd.h", F7b, 1, 1)
DEFINE_EASY_OPCODE(STPR, "stpr", F1F2, 2, 2)
DEFINE_EASY_OPCODE(STTASK, "sttask", F3, 2, 0)
DEFINE_TRIPLE_OPCODE(SUB, "sub", F1F2)
DEFINE_TRIPLE_OPCODE(SUBC, "subc", F1F2)
DEFINE_EASY_OPCODE(SUBDC, "subdc", F7c, 0, 0)
DEFINE_EASY_OPCODE(SUBRDC, "subrdc", F7c, 0, 0)
DEFINE_FPU_OPCODE(SUBF, "subf", F2)
DEFINE_EASY_OPCODE(TASI, "tasi", F3, 0, 0)
DEFINE_EASY_OPCODE_EX(TB, "tb", F6, 0, 0, STEP_COND)
DEFINE_TRIPLE_OPCODE(TEST, "test", F3)
DEFINE_EASY_OPCODE(TEST1, "test1", F1F2, 2, 2)
DEFINE_EASY_OPCODE_EX(TRAP, "trap", F3, 0, 0, STEP_OVER)
DEFINE_EASY_OPCODE(TRAPFL, "trapfl", F5, 0, 0)
DEFINE_EASY_OPCODE(UPDATE, "update", F1F2, 0, 3) // ?
DEFINE_EASY_OPCODE(UPDPSWH, "updpsw.h", F1F2, 2, 2)
DEFINE_EASY_OPCODE(UPDPSWW, "updpsw.w", F1F2, 2, 2)
DEFINE_EASY_OPCODE(UPDPTE, "updpte", F1F2, 0, 2) // ?
DEFINE_TRIPLE_OPCODE(XCH, "xch", F1)
DEFINE_TRIPLE_OPCODE(XOR, "xor", F1F2)
DEFINE_EASY_OPCODE(XORBSU, "xorbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(XORBSD, "xorbsd", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(XORNBSU, "xornbsu", F7b, 0x80, 0x80)
DEFINE_EASY_OPCODE(XORNBSD, "xornbsd", F7b, 0x80, 0x80)

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_58[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopCMPCB,
	/* 0x01 */ &V60Disassembler::dopCMPCFB,
	/* 0x02 */ &V60Disassembler::dopCMPCSB,
	/* 0x03 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x04 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x05 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x06 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x07 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x08 */ &V60Disassembler::dopMOVCUB,
	/* 0x09 */ &V60Disassembler::dopMOVCDB,
	/* 0x0A */ &V60Disassembler::dopMOVCFUB,
	/* 0x0B */ &V60Disassembler::dopMOVCFDB,
	/* 0x0C */ &V60Disassembler::dopMOVCSB,
	/* 0x0D */ &V60Disassembler::dop58UNHANDLED,
	/* 0x0E */ &V60Disassembler::dop58UNHANDLED,
	/* 0x0F */ &V60Disassembler::dop58UNHANDLED,
	/* 0x10 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x11 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x12 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x13 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x14 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x15 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x16 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x17 */ &V60Disassembler::dop58UNHANDLED,
	/* 0x18 */ &V60Disassembler::dopSCHCUB,
	/* 0x19 */ &V60Disassembler::dopSCHCDB,
	/* 0x1A */ &V60Disassembler::dopSKPCUB,
	/* 0x1B */ &V60Disassembler::dopSKPCDB,
	/* 0x1C */ &V60Disassembler::dop58UNHANDLED,
	/* 0x1D */ &V60Disassembler::dop58UNHANDLED,
	/* 0x1E */ &V60Disassembler::dop58UNHANDLED,
	/* 0x1F */ &V60Disassembler::dop58UNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_59[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopADDDC,
	/* 0x01 */ &V60Disassembler::dopSUBDC,
	/* 0x02 */ &V60Disassembler::dopSUBRDC,
	/* 0x03 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x04 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x05 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x06 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x07 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x08 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x09 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x0A */ &V60Disassembler::dop59UNHANDLED,
	/* 0x0B */ &V60Disassembler::dop59UNHANDLED,
	/* 0x0C */ &V60Disassembler::dop59UNHANDLED,
	/* 0x0D */ &V60Disassembler::dop59UNHANDLED,
	/* 0x0E */ &V60Disassembler::dop59UNHANDLED,
	/* 0x0F */ &V60Disassembler::dop59UNHANDLED,
	/* 0x10 */ &V60Disassembler::dopCVTDPZ,
	/* 0x11 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x12 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x13 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x14 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x15 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x16 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x17 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x18 */ &V60Disassembler::dopCVTDZP,
	/* 0x19 */ &V60Disassembler::dop59UNHANDLED,
	/* 0x1A */ &V60Disassembler::dop59UNHANDLED,
	/* 0x1B */ &V60Disassembler::dop59UNHANDLED,
	/* 0x1C */ &V60Disassembler::dop59UNHANDLED,
	/* 0x1D */ &V60Disassembler::dop59UNHANDLED,
	/* 0x1E */ &V60Disassembler::dop59UNHANDLED,
	/* 0x1F */ &V60Disassembler::dop59UNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_5A[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopCMPCH,
	/* 0x01 */ &V60Disassembler::dopCMPCFH,
	/* 0x02 */ &V60Disassembler::dopCMPCSH,
	/* 0x03 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x04 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x05 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x06 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x07 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x08 */ &V60Disassembler::dopMOVCUH,
	/* 0x09 */ &V60Disassembler::dopMOVCDH,
	/* 0x0A */ &V60Disassembler::dopMOVCFUH,
	/* 0x0B */ &V60Disassembler::dopMOVCFDH,
	/* 0x0C */ &V60Disassembler::dopMOVCSH,
	/* 0x0D */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x0E */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x0F */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x10 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x11 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x12 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x13 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x14 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x15 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x16 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x17 */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x18 */ &V60Disassembler::dopSCHCUH,
	/* 0x19 */ &V60Disassembler::dopSCHCDH,
	/* 0x1A */ &V60Disassembler::dopSKPCUH,
	/* 0x1B */ &V60Disassembler::dopSKPCDH,
	/* 0x1C */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x1D */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x1E */ &V60Disassembler::dop5AUNHANDLED,
	/* 0x1F */ &V60Disassembler::dop5AUNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_5B[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopSCH0BSU,
	/* 0x01 */ &V60Disassembler::dopSCH0BSD,
	/* 0x02 */ &V60Disassembler::dopSCH1BSU,
	/* 0x03 */ &V60Disassembler::dopSCH1BSD,
	/* 0x04 */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x05 */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x06 */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x07 */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x08 */ &V60Disassembler::dopMOVBSU,
	/* 0x09 */ &V60Disassembler::dopMOVBSD,
	/* 0x0A */ &V60Disassembler::dopNOTBSU,
	/* 0x0B */ &V60Disassembler::dopNOTBSD,
	/* 0x0C */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x0D */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x0E */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x0F */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x10 */ &V60Disassembler::dopANDBSU,
	/* 0x11 */ &V60Disassembler::dopANDBSD,
	/* 0x12 */ &V60Disassembler::dopANDNBSU,
	/* 0x13 */ &V60Disassembler::dopANDNBSD,
	/* 0x14 */ &V60Disassembler::dopORBSU,
	/* 0x15 */ &V60Disassembler::dopORBSD,
	/* 0x16 */ &V60Disassembler::dopORNBSU,
	/* 0x17 */ &V60Disassembler::dopORNBSD,
	/* 0x18 */ &V60Disassembler::dopXORBSU,
	/* 0x19 */ &V60Disassembler::dopXORBSD,
	/* 0x1A */ &V60Disassembler::dopXORNBSU,
	/* 0x1B */ &V60Disassembler::dopXORNBSD,
	/* 0x1C */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x1D */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x1E */ &V60Disassembler::dop5BUNHANDLED,
	/* 0x1F */ &V60Disassembler::dop5BUNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_5C[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopCMPFS,
	/* 0x01 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x02 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x03 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x04 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x05 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x06 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x07 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x08 */ &V60Disassembler::dopMOVFS,
	/* 0x09 */ &V60Disassembler::dopNEGFS,
	/* 0x0A */ &V60Disassembler::dopABSFS,
	/* 0x0B */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x0C */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x0D */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x0E */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x0F */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x10 */ &V60Disassembler::dopSCLFS,
	/* 0x11 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x12 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x13 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x14 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x15 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x16 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x17 */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x18 */ &V60Disassembler::dopADDFS,
	/* 0x19 */ &V60Disassembler::dopSUBFS,
	/* 0x1A */ &V60Disassembler::dopMULFS,
	/* 0x1B */ &V60Disassembler::dopDIVFS,
	/* 0x1C */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x1D */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x1E */ &V60Disassembler::dop5CUNHANDLED,
	/* 0x1F */ &V60Disassembler::dop5CUNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_5D[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopCMPBFS,
	/* 0x01 */ &V60Disassembler::dopCMPBFZ,
	/* 0x02 */ &V60Disassembler::dopCMPBFL,
	/* 0x03 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x04 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x05 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x06 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x07 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x08 */ &V60Disassembler::dopEXTBFS,
	/* 0x09 */ &V60Disassembler::dopEXTBFZ,
	/* 0x0A */ &V60Disassembler::dopEXTBFL,
	/* 0x0B */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x0C */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x0D */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x0E */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x0F */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x10 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x11 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x12 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x13 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x14 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x15 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x16 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x17 */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x18 */ &V60Disassembler::dopINSBFR,
	/* 0x19 */ &V60Disassembler::dopINSBFL,
	/* 0x1A */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x1B */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x1C */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x1D */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x1E */ &V60Disassembler::dop5DUNHANDLED,
	/* 0x1F */ &V60Disassembler::dop5DUNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_5E[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopCMPFL,
	/* 0x01 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x02 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x03 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x04 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x05 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x06 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x07 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x08 */ &V60Disassembler::dopMOVFL,
	/* 0x09 */ &V60Disassembler::dopNEGFL,
	/* 0x0A */ &V60Disassembler::dopABSFL,
	/* 0x0B */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x0C */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x0D */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x0E */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x0F */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x10 */ &V60Disassembler::dopSCLFL,
	/* 0x11 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x12 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x13 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x14 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x15 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x16 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x17 */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x18 */ &V60Disassembler::dopADDFL,
	/* 0x19 */ &V60Disassembler::dopSUBFL,
	/* 0x1A */ &V60Disassembler::dopMULFL,
	/* 0x1B */ &V60Disassembler::dopDIVFL,
	/* 0x1C */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x1D */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x1E */ &V60Disassembler::dop5EUNHANDLED,
	/* 0x1F */ &V60Disassembler::dop5EUNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_5F[32])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopCVTWS,
	/* 0x01 */ &V60Disassembler::dopCVTSW,
	/* 0x02 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x03 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x04 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x05 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x06 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x07 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x08 */ &V60Disassembler::dopCVTLS,
	/* 0x09 */ &V60Disassembler::dopCVTLW,
	/* 0x0A */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x0B */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x0C */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x0D */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x0E */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x0F */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x10 */ &V60Disassembler::dopCVTSL,
	/* 0x11 */ &V60Disassembler::dopCVTWL,
	/* 0x12 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x13 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x14 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x15 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x16 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x17 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x18 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x19 */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x1A */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x1B */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x1C */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x1D */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x1E */ &V60Disassembler::dop5FUNHANDLED,
	/* 0x1F */ &V60Disassembler::dop5FUNHANDLED
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_C6[8])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x0 */ &V60Disassembler::dopDBV,
	/* 0x1 */ &V60Disassembler::dopDBL,
	/* 0x2 */ &V60Disassembler::dopDBE,
	/* 0x3 */ &V60Disassembler::dopDBNH,
	/* 0x4 */ &V60Disassembler::dopDBN,
	/* 0x5 */ &V60Disassembler::dopDBR,
	/* 0x6 */ &V60Disassembler::dopDBLT,
	/* 0x7 */ &V60Disassembler::dopDBLE
};

u32 (V60Disassembler::*const V60Disassembler::dasm_optable_C7[8])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x0 */ &V60Disassembler::dopDBNV,
	/* 0x1 */ &V60Disassembler::dopDBNL,
	/* 0x2 */ &V60Disassembler::dopDBNE,
	/* 0x3 */ &V60Disassembler::dopDBH,
	/* 0x4 */ &V60Disassembler::dopDBP,
	/* 0x5 */ &V60Disassembler::dopTB,
	/* 0x6 */ &V60Disassembler::dopDBGE,
	/* 0x7 */ &V60Disassembler::dopDBGT
};

u32 V60Disassembler::dop58(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_58[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dop59(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_59[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dop5A(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_5A[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dop5B(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_5B[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dop5C(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_5C[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dop5D(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_5D[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dop5E(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_5E[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dop5F(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_5F[opcodes.r8(pc) & 0x1f])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dopC6(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_C6[opcodes.r8(pc) >> 5])(ipc, pc, opcodes, stream);
}

u32 V60Disassembler::dopC7(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream)
{
	return (this->*dasm_optable_C7[opcodes.r8(pc) >> 5])(ipc, pc, opcodes, stream);
}

u32 (V60Disassembler::*const V60Disassembler::dasm_optable[256])(unsigned ipc, offs_t pc, const data_buffer &opcodes, std::ostream &stream) =
{
	/* 0x00 */ &V60Disassembler::dopHALT,
	/* 0x01 */ &V60Disassembler::dopLDTASK,
	/* 0x02 */ &V60Disassembler::dopSTPR,
	/* 0x03 */ &V60Disassembler::dopGETRA,
	/* 0x04 */ &V60Disassembler::dopGETPTE,
	/* 0x05 */ &V60Disassembler::dopGETATE,
	/* 0x06 */ &V60Disassembler::dopUNHANDLED,
	/* 0x07 */ &V60Disassembler::dopUNHANDLED,
	/* 0x08 */ &V60Disassembler::dopRVBIT,
	/* 0x09 */ &V60Disassembler::dopMOVB,
	/* 0x0A */ &V60Disassembler::dopMOVSBH,
	/* 0x0B */ &V60Disassembler::dopMOVZBH,
	/* 0x0C */ &V60Disassembler::dopMOVSBW,
	/* 0x0D */ &V60Disassembler::dopMOVZBW,
	/* 0x0E */ &V60Disassembler::dopUNHANDLED,
	/* 0x0F */ &V60Disassembler::dopUNHANDLED,
	/* 0x10 */ &V60Disassembler::dopCLRTLBA,
	/* 0x11 */ &V60Disassembler::dopUNHANDLED,
	/* 0x12 */ &V60Disassembler::dopLDPR,
	/* 0x13 */ &V60Disassembler::dopUPDPSWW,
	/* 0x14 */ &V60Disassembler::dopUPDPTE,
	/* 0x15 */ &V60Disassembler::dopUPDATE,
	/* 0x16 */ &V60Disassembler::dopUNHANDLED,
	/* 0x17 */ &V60Disassembler::dopUNHANDLED,
	/* 0x18 */ &V60Disassembler::dopUNHANDLED,
	/* 0x19 */ &V60Disassembler::dopMOVTHB,
	/* 0x1A */ &V60Disassembler::dopUNHANDLED,
	/* 0x1B */ &V60Disassembler::dopMOVH,
	/* 0x1C */ &V60Disassembler::dopMOVSHW,
	/* 0x1D */ &V60Disassembler::dopMOVZHW,
	/* 0x1E */ &V60Disassembler::dopUNHANDLED,
	/* 0x1F */ &V60Disassembler::dopUNHANDLED,
	/* 0x20 */ &V60Disassembler::dopINB,
	/* 0x21 */ &V60Disassembler::dopOUTB,
	/* 0x22 */ &V60Disassembler::dopINH,
	/* 0x23 */ &V60Disassembler::dopOUTH,
	/* 0x24 */ &V60Disassembler::dopINW,
	/* 0x25 */ &V60Disassembler::dopOUTW,
	/* 0x26 */ &V60Disassembler::dopUNHANDLED,
	/* 0x27 */ &V60Disassembler::dopUNHANDLED,
	/* 0x28 */ &V60Disassembler::dopUNHANDLED,
	/* 0x29 */ &V60Disassembler::dopMOVTWB,
	/* 0x2A */ &V60Disassembler::dopUNHANDLED,
	/* 0x2B */ &V60Disassembler::dopMOVTWH,
	/* 0x2C */ &V60Disassembler::dopRVBYT,
	/* 0x2D */ &V60Disassembler::dopMOVW,
	/* 0x2E */ &V60Disassembler::dopUNHANDLED,
	/* 0x2F */ &V60Disassembler::dopUNHANDLED,
	/* 0x30 */ &V60Disassembler::dopUNHANDLED,
	/* 0x31 */ &V60Disassembler::dopUNHANDLED,
	/* 0x32 */ &V60Disassembler::dopUNHANDLED,
	/* 0x33 */ &V60Disassembler::dopUNHANDLED,
	/* 0x34 */ &V60Disassembler::dopUNHANDLED,
	/* 0x35 */ &V60Disassembler::dopUNHANDLED,
	/* 0x36 */ &V60Disassembler::dopUNHANDLED,
	/* 0x37 */ &V60Disassembler::dopUNHANDLED,
	/* 0x38 */ &V60Disassembler::dopNOTB,
	/* 0x39 */ &V60Disassembler::dopNEGB,
	/* 0x3A */ &V60Disassembler::dopNOTH,
	/* 0x3B */ &V60Disassembler::dopNEGH,
	/* 0x3C */ &V60Disassembler::dopNOTW,
	/* 0x3D */ &V60Disassembler::dopNEGW,
	/* 0x3E */ &V60Disassembler::dopUNHANDLED,
	/* 0x3F */ &V60Disassembler::dopMOVD,
	/* 0x40 */ &V60Disassembler::dopMOVEAB,
	/* 0x41 */ &V60Disassembler::dopXCHB,
	/* 0x42 */ &V60Disassembler::dopMOVEAH,
	/* 0x43 */ &V60Disassembler::dopXCHH,
	/* 0x44 */ &V60Disassembler::dopMOVEAW,
	/* 0x45 */ &V60Disassembler::dopXCHW,
	/* 0x46 */ &V60Disassembler::dopUNHANDLED,
	/* 0x47 */ &V60Disassembler::dopSETF,
	/* 0x48 */ &V60Disassembler::dopBSR,
	/* 0x49 */ &V60Disassembler::dopCALL,
	/* 0x4A */ &V60Disassembler::dopUPDPSWH,
	/* 0x4B */ &V60Disassembler::dopCHLVL,
	/* 0x4C */ &V60Disassembler::dopCAXI,
	/* 0x4D */ &V60Disassembler::dopCHKAR,
	/* 0x4E */ &V60Disassembler::dopCHKAW,
	/* 0x4F */ &V60Disassembler::dopCHKAE,
	/* 0x50 */ &V60Disassembler::dopREMB,
	/* 0x51 */ &V60Disassembler::dopREMUB,
	/* 0x52 */ &V60Disassembler::dopREMH,
	/* 0x53 */ &V60Disassembler::dopREMUH,
	/* 0x54 */ &V60Disassembler::dopREMW,
	/* 0x55 */ &V60Disassembler::dopREMUW,
	/* 0x56 */ &V60Disassembler::dopUNHANDLED,
	/* 0x57 */ &V60Disassembler::dopUNHANDLED,
	/* 0x58 */ &V60Disassembler::dop58,
	/* 0x59 */ &V60Disassembler::dop59,
	/* 0x5A */ &V60Disassembler::dop5A,
	/* 0x5B */ &V60Disassembler::dop5B,
	/* 0x5C */ &V60Disassembler::dop5C,
	/* 0x5D */ &V60Disassembler::dop5D,
	/* 0x5E */ &V60Disassembler::dop5E,
	/* 0x5F */ &V60Disassembler::dop5F,
	/* 0x60 */ &V60Disassembler::dopBV8,
	/* 0x61 */ &V60Disassembler::dopBNV8,
	/* 0x62 */ &V60Disassembler::dopBL8,
	/* 0x63 */ &V60Disassembler::dopBNL8,
	/* 0x64 */ &V60Disassembler::dopBE8,
	/* 0x65 */ &V60Disassembler::dopBNE8,
	/* 0x66 */ &V60Disassembler::dopBNH8,
	/* 0x67 */ &V60Disassembler::dopBH8,
	/* 0x68 */ &V60Disassembler::dopBN8,
	/* 0x69 */ &V60Disassembler::dopBP8,
	/* 0x6A */ &V60Disassembler::dopBR8,
	/* 0x6B */ &V60Disassembler::dopUNHANDLED,
	/* 0x6C */ &V60Disassembler::dopBLT8,
	/* 0x6D */ &V60Disassembler::dopBGE8,
	/* 0x6E */ &V60Disassembler::dopBLE8,
	/* 0x6F */ &V60Disassembler::dopBGT8,
	/* 0x70 */ &V60Disassembler::dopBV16,
	/* 0x71 */ &V60Disassembler::dopBNV16,
	/* 0x72 */ &V60Disassembler::dopBL16,
	/* 0x73 */ &V60Disassembler::dopBNL16,
	/* 0x74 */ &V60Disassembler::dopBE16,
	/* 0x75 */ &V60Disassembler::dopBNE16,
	/* 0x76 */ &V60Disassembler::dopBNH16,
	/* 0x77 */ &V60Disassembler::dopBH16,
	/* 0x78 */ &V60Disassembler::dopBN16,
	/* 0x79 */ &V60Disassembler::dopBP16,
	/* 0x7A */ &V60Disassembler::dopBR16,
	/* 0x7B */ &V60Disassembler::dopUNHANDLED,
	/* 0x7C */ &V60Disassembler::dopBLT16,
	/* 0x7D */ &V60Disassembler::dopBGE16,
	/* 0x7E */ &V60Disassembler::dopBLE16,
	/* 0x7F */ &V60Disassembler::dopBGT16,
	/* 0x80 */ &V60Disassembler::dopADDB,
	/* 0x81 */ &V60Disassembler::dopMULB,
	/* 0x82 */ &V60Disassembler::dopADDH,
	/* 0x83 */ &V60Disassembler::dopMULH,
	/* 0x84 */ &V60Disassembler::dopADDW,
	/* 0x85 */ &V60Disassembler::dopMULW,
	/* 0x86 */ &V60Disassembler::dopMULX,
	/* 0x87 */ &V60Disassembler::dopTEST1,
	/* 0x88 */ &V60Disassembler::dopORB,
	/* 0x89 */ &V60Disassembler::dopROTB,
	/* 0x8A */ &V60Disassembler::dopORH,
	/* 0x8B */ &V60Disassembler::dopROTH,
	/* 0x8C */ &V60Disassembler::dopORW,
	/* 0x8D */ &V60Disassembler::dopROTW,
	/* 0x8E */ &V60Disassembler::dopUNHANDLED,
	/* 0x8F */ &V60Disassembler::dopUNHANDLED,
	/* 0x90 */ &V60Disassembler::dopADDCB,
	/* 0x91 */ &V60Disassembler::dopMULUB,
	/* 0x92 */ &V60Disassembler::dopADDCH,
	/* 0x93 */ &V60Disassembler::dopMULUH,
	/* 0x94 */ &V60Disassembler::dopADDCW,
	/* 0x95 */ &V60Disassembler::dopMULUW,
	/* 0x96 */ &V60Disassembler::dopMULUX,
	/* 0x97 */ &V60Disassembler::dopSET1,
	/* 0x98 */ &V60Disassembler::dopSUBCB,
	/* 0x99 */ &V60Disassembler::dopROTCB,
	/* 0x9A */ &V60Disassembler::dopSUBCH,
	/* 0x9B */ &V60Disassembler::dopROTCH,
	/* 0x9C */ &V60Disassembler::dopSUBCW,
	/* 0x9D */ &V60Disassembler::dopROTCW,
	/* 0x9E */ &V60Disassembler::dopUNHANDLED,
	/* 0x9F */ &V60Disassembler::dopUNHANDLED,
	/* 0xA0 */ &V60Disassembler::dopANDB,
	/* 0xA1 */ &V60Disassembler::dopDIVB,
	/* 0xA2 */ &V60Disassembler::dopANDH,
	/* 0xA3 */ &V60Disassembler::dopDIVH,
	/* 0xA4 */ &V60Disassembler::dopANDW,
	/* 0xA5 */ &V60Disassembler::dopDIVW,
	/* 0xA6 */ &V60Disassembler::dopDIVX,
	/* 0xA7 */ &V60Disassembler::dopCLR1,
	/* 0xA8 */ &V60Disassembler::dopSUBB,
	/* 0xA9 */ &V60Disassembler::dopSHLB,
	/* 0xAA */ &V60Disassembler::dopSUBH,
	/* 0xAB */ &V60Disassembler::dopSHLH,
	/* 0xAC */ &V60Disassembler::dopSUBW,
	/* 0xAD */ &V60Disassembler::dopSHLW,
	/* 0xAE */ &V60Disassembler::dopUNHANDLED,
	/* 0xAF */ &V60Disassembler::dopUNHANDLED,
	/* 0xB0 */ &V60Disassembler::dopXORB,
	/* 0xB1 */ &V60Disassembler::dopDIVUB,
	/* 0xB2 */ &V60Disassembler::dopXORH,
	/* 0xB3 */ &V60Disassembler::dopDIVUH,
	/* 0xB4 */ &V60Disassembler::dopXORW,
	/* 0xB5 */ &V60Disassembler::dopDIVUW,
	/* 0xB6 */ &V60Disassembler::dopDIVUX,
	/* 0xB7 */ &V60Disassembler::dopNOT1,
	/* 0xB8 */ &V60Disassembler::dopCMPB,
	/* 0xB9 */ &V60Disassembler::dopSHAB,
	/* 0xBA */ &V60Disassembler::dopCMPH,
	/* 0xBB */ &V60Disassembler::dopSHAH,
	/* 0xBC */ &V60Disassembler::dopCMPW,
	/* 0xBD */ &V60Disassembler::dopSHAW,
	/* 0xBE */ &V60Disassembler::dopUNHANDLED,
	/* 0xBF */ &V60Disassembler::dopUNHANDLED,
	/* 0xC0 */ &V60Disassembler::dopUNHANDLED,
	/* 0xC1 */ &V60Disassembler::dopUNHANDLED,
	/* 0xC2 */ &V60Disassembler::dopUNHANDLED,
	/* 0xC3 */ &V60Disassembler::dopUNHANDLED,
	/* 0xC4 */ &V60Disassembler::dopUNHANDLED,
	/* 0xC5 */ &V60Disassembler::dopUNHANDLED,
	/* 0xC6 */ &V60Disassembler::dopC6,
	/* 0xC7 */ &V60Disassembler::dopC7,
	/* 0xC8 */ &V60Disassembler::dopBRK,
	/* 0xC9 */ &V60Disassembler::dopBRKV,
	/* 0xCA */ &V60Disassembler::dopRSR,
	/* 0xCB */ &V60Disassembler::dopTRAPFL,
	/* 0xCC */ &V60Disassembler::dopDISPOSE,
	/* 0xCD */ &V60Disassembler::dopNOP,
	/* 0xCE */ &V60Disassembler::dopUNHANDLED,
	/* 0xCF */ &V60Disassembler::dopUNHANDLED,
	/* 0xD0 */ &V60Disassembler::dopDECB,
	/* 0xD1 */ &V60Disassembler::dopDECB,
	/* 0xD2 */ &V60Disassembler::dopDECH,
	/* 0xD3 */ &V60Disassembler::dopDECH,
	/* 0xD4 */ &V60Disassembler::dopDECW,
	/* 0xD5 */ &V60Disassembler::dopDECW,
	/* 0xD6 */ &V60Disassembler::dopJMP,
	/* 0xD7 */ &V60Disassembler::dopJMP,
	/* 0xD8 */ &V60Disassembler::dopINCB,
	/* 0xD9 */ &V60Disassembler::dopINCB,
	/* 0xDA */ &V60Disassembler::dopINCH,
	/* 0xDB */ &V60Disassembler::dopINCH,
	/* 0xDC */ &V60Disassembler::dopINCW,
	/* 0xDD */ &V60Disassembler::dopINCW,
	/* 0xDE */ &V60Disassembler::dopPREPARE,
	/* 0xDF */ &V60Disassembler::dopPREPARE,
	/* 0xE0 */ &V60Disassembler::dopTASI,
	/* 0xE1 */ &V60Disassembler::dopTASI,
	/* 0xE2 */ &V60Disassembler::dopRET,
	/* 0xE3 */ &V60Disassembler::dopRET,
	/* 0xE4 */ &V60Disassembler::dopPOPM,
	/* 0xE5 */ &V60Disassembler::dopPOPM,
	/* 0xE6 */ &V60Disassembler::dopPOP,
	/* 0xE7 */ &V60Disassembler::dopPOP,
	/* 0xE8 */ &V60Disassembler::dopJSR,
	/* 0xE9 */ &V60Disassembler::dopJSR,
	/* 0xEA */ &V60Disassembler::dopRETIU,
	/* 0xEB */ &V60Disassembler::dopRETIU,
	/* 0xEC */ &V60Disassembler::dopPUSHM,
	/* 0xED */ &V60Disassembler::dopPUSHM,
	/* 0xEE */ &V60Disassembler::dopPUSH,
	/* 0xEF */ &V60Disassembler::dopPUSH,
	/* 0xF0 */ &V60Disassembler::dopTESTB,
	/* 0xF1 */ &V60Disassembler::dopTESTB,
	/* 0xF2 */ &V60Disassembler::dopTESTH,
	/* 0xF3 */ &V60Disassembler::dopTESTH,
	/* 0xF4 */ &V60Disassembler::dopTESTW,
	/* 0xF5 */ &V60Disassembler::dopTESTW,
	/* 0xF6 */ &V60Disassembler::dopGETPSW,
	/* 0xF7 */ &V60Disassembler::dopGETPSW,
	/* 0xF8 */ &V60Disassembler::dopTRAP,
	/* 0xF9 */ &V60Disassembler::dopTRAP,
	/* 0xFA */ &V60Disassembler::dopRETIS,
	/* 0xFB */ &V60Disassembler::dopRETIS,
	/* 0xFC */ &V60Disassembler::dopSTTASK,
	/* 0xFD */ &V60Disassembler::dopSTTASK,
	/* 0xFE */ &V60Disassembler::dopCLRTLB,
	/* 0xFF */ &V60Disassembler::dopCLRTLB
};

offs_t V60Disassembler::disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params)
{
	return (this->*dasm_optable[opcodes.r8(pc)])(pc, pc+1, opcodes, stream) | SUPPORTED;
}

u32 V60Disassembler::opcode_alignment() const
{
	return 1;
}
