// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// PORT: MAME 0.264 src/devices/cpu/v60/am3.hxx, verbatim except the class name
// (v60_device -> V60Cpu). Part of the crossover's V60/V70 port, see v60.hpp.
// AM3 Functions (for ReadAM)
// **************************

uint32_t V60Cpu::am3Register()
{
	switch (m_moddim)
	{
	case 0:
		SETREG8(m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		SETREG16(m_reg[m_modval & 0x1F], m_modwritevalh);
		break;
	case 2:
		m_reg[m_modval & 0x1F] = m_modwritevalw;
		break;
	}

	return 1;
}

uint32_t V60Cpu::am3RegisterIndirect()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval & 0x1F], m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval & 0x1F], m_modwritevalw);
		break;
	}

	return 1;
}

uint32_t V60Cpu::am3RegisterIndirectIndexed()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 2;
}

uint32_t V60Cpu::am3Autoincrement()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval & 0x1F], m_modwritevalb);
		m_reg[m_modval & 0x1F] += 1;
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval & 0x1F], m_modwritevalh);
		m_reg[m_modval & 0x1F] += 2;
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval & 0x1F], m_modwritevalw);
		m_reg[m_modval & 0x1F] += 4;
		break;
	}

	return 1;
}

uint32_t V60Cpu::am3Autodecrement()
{
	switch (m_moddim)
	{
	case 0:
		m_reg[m_modval & 0x1F] -= 1;
		m_program->write_byte(m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_reg[m_modval & 0x1F] -= 2;
		m_program->write_word_unaligned(m_reg[m_modval & 0x1F], m_modwritevalh);
		break;
	case 2:
		m_reg[m_modval & 0x1F] -= 4;
		m_program->write_dword_unaligned(m_reg[m_modval & 0x1F], m_modwritevalw);
		break;
	}

	return 1;
}

uint32_t V60Cpu::am3Displacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1), m_modwritevalw);
		break;
	}

	return 2;
}

uint32_t V60Cpu::am3Displacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1), m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3Displacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1), m_modwritevalw);
		break;
	}

	return 5;
}


uint32_t V60Cpu::am3DisplacementIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3DisplacementIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 4;
}

uint32_t V60Cpu::am3DisplacementIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 6;
}


uint32_t V60Cpu::am3PCDisplacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(PC + (int8_t)OpRead8(m_modadd + 1), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(PC + (int8_t)OpRead8(m_modadd + 1), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1), m_modwritevalw);
		break;
	}

	return 2;
}

uint32_t V60Cpu::am3PCDisplacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(PC + (int16_t)OpRead16(m_modadd + 1), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(PC + (int16_t)OpRead16(m_modadd + 1), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1), m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3PCDisplacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(PC + OpRead32(m_modadd + 1), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(PC + OpRead32(m_modadd + 1), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(PC + OpRead32(m_modadd + 1), m_modwritevalw);
		break;
	}

	return 5;
}

uint32_t V60Cpu::am3PCDisplacementIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3PCDisplacementIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 4;
}

uint32_t V60Cpu::am3PCDisplacementIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 6;
}

uint32_t V60Cpu::am3DisplacementIndirect8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)), m_modwritevalw);
		break;
	}

	return 2;
}

uint32_t V60Cpu::am3DisplacementIndirect16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)), m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3DisplacementIndirect32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)), m_modwritevalw);
		break;
	}

	return 5;
}


uint32_t V60Cpu::am3DisplacementIndirectIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3DisplacementIndirectIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 4;
}

uint32_t V60Cpu::am3DisplacementIndirectIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 6;
}

uint32_t V60Cpu::am3PCDisplacementIndirect8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)), m_modwritevalw);
		break;
	}

	return 2;
}

uint32_t V60Cpu::am3PCDisplacementIndirect16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)), m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3PCDisplacementIndirect32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)), m_modwritevalw);
		break;
	}

	return 5;
}


uint32_t V60Cpu::am3PCDisplacementIndirectIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3PCDisplacementIndirectIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 4;
}

uint32_t V60Cpu::am3PCDisplacementIndirectIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 6;
}


uint32_t V60Cpu::am3DoubleDisplacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2), m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3DoubleDisplacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3), m_modwritevalw);
		break;
	}

	return 5;
}

uint32_t V60Cpu::am3DoubleDisplacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5), m_modwritevalw);
		break;
	}

	return 9;
}


uint32_t V60Cpu::am3PCDoubleDisplacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2), m_modwritevalw);
		break;
	}

	return 3;
}

uint32_t V60Cpu::am3PCDoubleDisplacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3), m_modwritevalw);
		break;
	}

	return 5;
}

uint32_t V60Cpu::am3PCDoubleDisplacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5), m_modwritevalw);
		break;
	}

	return 9;
}

uint32_t V60Cpu::am3DirectAddress()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(OpRead32(m_modadd + 1), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(OpRead32(m_modadd + 1), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(OpRead32(m_modadd + 1), m_modwritevalw);
		break;
	}

	return 5;
}

uint32_t V60Cpu::am3DirectAddressIndexed()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2, m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4, m_modwritevalw);
		break;
	}

	return 6;
}

uint32_t V60Cpu::am3DirectAddressDeferred()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(OpRead32(m_modadd + 1)), m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 1)), m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 1)), m_modwritevalw);
		break;
	}

	return 5;
}

uint32_t V60Cpu::am3DirectAddressDeferredIndexed()
{
	switch (m_moddim)
	{
	case 0:
		m_program->write_byte(m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalb);
		break;
	case 1:
		m_program->write_word_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalh);
		break;
	case 2:
		m_program->write_dword_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F], m_modwritevalw);
		break;
	}

	return 6;
}

uint32_t V60Cpu::am3Immediate()
{
	fatalerror("CPU - AM3 - IMM (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am3ImmediateQuick()
{
	fatalerror("CPU - AM3 - IMMQ (PC=%06x)\n", PC);
}



// AM3 Tables (for ReadAMAddress)
// ******************************

uint32_t V60Cpu::am3Error1()
{
	fatalerror("CPU - AM3 - 1 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am3Error2()
{
	fatalerror("CPU - AM3 - 2 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am3Error3()
{
	fatalerror("CPU - AM3 - 3 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am3Error4()
{
	fatalerror("CPU - AM3 - 4 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am3Error5()
{
	fatalerror("CPU - AM3 - 5 (PC=%06x)\n", PC);
}

const V60Cpu::am_func V60Cpu::s_AMTable3_G7a[16] =
{
	&V60Cpu::am3PCDisplacementIndexed8,
	&V60Cpu::am3PCDisplacementIndexed16,
	&V60Cpu::am3PCDisplacementIndexed32,
	&V60Cpu::am3DirectAddressIndexed,
	&V60Cpu::am3Error5,
	&V60Cpu::am3Error5,
	&V60Cpu::am3Error5,
	&V60Cpu::am3Error5,
	&V60Cpu::am3PCDisplacementIndirectIndexed8,
	&V60Cpu::am3PCDisplacementIndirectIndexed16,
	&V60Cpu::am3PCDisplacementIndirectIndexed32,
	&V60Cpu::am3DirectAddressDeferredIndexed,
	&V60Cpu::am3Error5,
	&V60Cpu::am3Error5,
	&V60Cpu::am3Error5,
	&V60Cpu::am3Error5
};

uint32_t V60Cpu::am3Group7a()
{
	if (!(m_modval2 & 0x10))
		return am3Error4();

	return (this->*s_AMTable3_G7a[m_modval2 & 0xF])();
}

const V60Cpu::am_func V60Cpu::s_AMTable3_G7[32] =
{
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3ImmediateQuick,
	&V60Cpu::am3PCDisplacement8,
	&V60Cpu::am3PCDisplacement16,
	&V60Cpu::am3PCDisplacement32,
	&V60Cpu::am3DirectAddress,
	&V60Cpu::am3Immediate,
	&V60Cpu::am3Error2,
	&V60Cpu::am3Error2,
	&V60Cpu::am3Error2,
	&V60Cpu::am3PCDisplacementIndirect8,
	&V60Cpu::am3PCDisplacementIndirect16,
	&V60Cpu::am3PCDisplacementIndirect32,
	&V60Cpu::am3DirectAddressDeferred,
	&V60Cpu::am3PCDoubleDisplacement8,
	&V60Cpu::am3PCDoubleDisplacement16,
	&V60Cpu::am3PCDoubleDisplacement32,
	&V60Cpu::am3Error2
};

const V60Cpu::am_func V60Cpu::s_AMTable3_G6[8] =
{
	&V60Cpu::am3DisplacementIndexed8,
	&V60Cpu::am3DisplacementIndexed16,
	&V60Cpu::am3DisplacementIndexed32,
	&V60Cpu::am3RegisterIndirectIndexed,
	&V60Cpu::am3DisplacementIndirectIndexed8,
	&V60Cpu::am3DisplacementIndirectIndexed16,
	&V60Cpu::am3DisplacementIndirectIndexed32,
	&V60Cpu::am3Group7a
};




uint32_t V60Cpu::am3Group6()
{
	m_modval2 = OpRead8(m_modadd + 1);
	return (this->*s_AMTable3_G6[m_modval2 >> 5])();
}


uint32_t V60Cpu::am3Group7()
{
	return (this->*s_AMTable3_G7[m_modval & 0x1F])();
}



const V60Cpu::am_func V60Cpu::s_AMTable3[2][8] =
{
	{
		&V60Cpu::am3Displacement8,
		&V60Cpu::am3Displacement16,
		&V60Cpu::am3Displacement32,
		&V60Cpu::am3RegisterIndirect,
		&V60Cpu::am3DisplacementIndirect8,
		&V60Cpu::am3DisplacementIndirect16,
		&V60Cpu::am3DisplacementIndirect32,
		&V60Cpu::am3Group7
	},

	{
		&V60Cpu::am3DoubleDisplacement8,
		&V60Cpu::am3DoubleDisplacement16,
		&V60Cpu::am3DoubleDisplacement32,
		&V60Cpu::am3Register,
		&V60Cpu::am3Autoincrement,
		&V60Cpu::am3Autodecrement,
		&V60Cpu::am3Group6,
		&V60Cpu::am3Error1
	}
};
