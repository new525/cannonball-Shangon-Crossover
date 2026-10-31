// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// PORT: MAME 0.264 src/devices/cpu/v60/am1.hxx, verbatim except the class name
// (v60_device -> V60Cpu). Part of the crossover's V60/V70 port, see v60.hpp.
// AM1 Functions (for ReadAM)
// **************************

uint32_t V60Cpu::am1Register()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = (uint8_t)m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = (uint16_t)m_reg[m_modval & 0x1F];
		break;
	case 2:
		m_amout = m_reg[m_modval & 0x1F];
		break;
	}

	return 1;
}

uint32_t V60Cpu::am1RegisterIndirect()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval & 0x1F]);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F]);
		break;
	}

	return 1;
}

uint32_t V60Cpu::bam1RegisterIndirect()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F]);
	return 1;
}

uint32_t V60Cpu::am1RegisterIndirectIndexed()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 2;
}

uint32_t V60Cpu::bam1RegisterIndirectIndexed()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + m_bamoffset / 8);
	m_bamoffset&=7;
	return 2;
}

uint32_t V60Cpu::am1Autoincrement()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval & 0x1F]);
		m_reg[m_modval & 0x1F]++;
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval & 0x1F]);
		m_reg[m_modval & 0x1F] +=2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F]);
		m_reg[m_modval & 0x1F] +=4;
		break;
	}

	return 1;
}

uint32_t V60Cpu::bam1Autoincrement()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F]);
	switch (m_moddim)
	{
	case 10:
		m_reg[m_modval & 0x1F] +=1;
		break;
	case 11:
		m_reg[m_modval & 0x1F] +=4;
		break;
	default:
		fatalerror("CPU - BAM1 - 7\n");
		break;
	}
	return 1;
}

uint32_t V60Cpu::am1Autodecrement()
{
	switch (m_moddim)
	{
	case 0:
		m_reg[m_modval & 0x1F]--;
		m_amout = m_program->read_byte(m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_reg[m_modval & 0x1F]-=2;
		m_amout = m_program->read_word_unaligned(m_reg[m_modval & 0x1F]);
		break;
	case 2:
		m_reg[m_modval & 0x1F]-=4;
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F]);
		break;
	}

	return 1;
}

uint32_t V60Cpu::bam1Autodecrement()
{
	m_bamoffset = 0;
	switch (m_moddim)
	{
	case 10:
		m_reg[m_modval & 0x1F]-=1;
		break;
	case 11:
		m_reg[m_modval & 0x1F]-=4;
		break;
	default:
		fatalerror("CPU - BAM1 - 7\n");
		break;
	}
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F]);
	return 1;
}

uint32_t V60Cpu::am1Displacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1));
		break;
	}

	return 2;
}

uint32_t V60Cpu::bam1Displacement8()
{
	m_bamoffset = m_program->read_byte(m_modadd + 1);
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + m_bamoffset / 8);
	m_bamoffset&=7;
	return 2;
}


uint32_t V60Cpu::am1Displacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1));
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1Displacement16()
{
	m_bamoffset = OpRead16(m_modadd + 1);
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}

uint32_t V60Cpu::am1Displacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1Displacement32()
{
	m_bamoffset = OpRead32(m_modadd + 1);
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + m_bamoffset / 8);
	m_bamoffset&=7;
	return 5;
}

uint32_t V60Cpu::am1DisplacementIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1DisplacementIndexed8()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}

uint32_t V60Cpu::am1DisplacementIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 4;
}

uint32_t V60Cpu::bam1DisplacementIndexed16()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 4;
}

uint32_t V60Cpu::am1DisplacementIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam1DisplacementIndexed32()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 6;
}


uint32_t V60Cpu::am1PCDisplacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(PC + (int8_t)OpRead8(m_modadd + 1));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(PC + (int8_t)OpRead8(m_modadd + 1));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1));
		break;
	}

	return 2;
}

uint32_t V60Cpu::bam1PCDisplacement8()
{
	m_bamoffset = OpRead8(m_modadd + 1);
	m_amout = m_program->read_dword_unaligned(PC + m_bamoffset / 8);
	m_bamoffset&=7;
	return 2;
}

uint32_t V60Cpu::am1PCDisplacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(PC + (int16_t)OpRead16(m_modadd + 1));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(PC + (int16_t)OpRead16(m_modadd + 1));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1));
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1PCDisplacement16()
{
	m_bamoffset = OpRead16(m_modadd + 1);
	m_amout = m_program->read_dword_unaligned(PC + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}

uint32_t V60Cpu::am1PCDisplacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(PC + OpRead32(m_modadd + 1));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(PC + OpRead32(m_modadd + 1));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1PCDisplacement32()
{
	m_bamoffset = OpRead32(m_modadd + 1);
	m_amout = m_program->read_dword_unaligned(PC + m_bamoffset / 8);
	m_bamoffset&=7;
	return 5;
}

uint32_t V60Cpu::am1PCDisplacementIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1PCDisplacementIndexed8()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}


uint32_t V60Cpu::am1PCDisplacementIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 4;
}

uint32_t V60Cpu::bam1PCDisplacementIndexed16()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 4;
}

uint32_t V60Cpu::am1PCDisplacementIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam1PCDisplacementIndexed32()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 6;
}

uint32_t V60Cpu::am1DisplacementIndirect8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)));
		break;
	}

	return 2;
}

uint32_t V60Cpu::bam1DisplacementIndirect8()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)));
	return 2;
}

uint32_t V60Cpu::am1DisplacementIndirect16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)));
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1DisplacementIndirect16()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)));
	return 3;
}

uint32_t V60Cpu::am1DisplacementIndirect32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1DisplacementIndirect32()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)));
	return 5;
}

uint32_t V60Cpu::am1DisplacementIndirectIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1DisplacementIndirectIndexed8()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}

uint32_t V60Cpu::am1DisplacementIndirectIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 4;
}

uint32_t V60Cpu::bam1DisplacementIndirectIndexed16()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 4;
}

uint32_t V60Cpu::am1DisplacementIndirectIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam1DisplacementIndirectIndexed32()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 6;
}

uint32_t V60Cpu::am1PCDisplacementIndirect8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)));
		break;
	}

	return 2;
}

uint32_t V60Cpu::bam1PCDisplacementIndirect8()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)));
	return 2;
}

uint32_t V60Cpu::am1PCDisplacementIndirect16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)));
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1PCDisplacementIndirect16()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)));
	return 3;
}

uint32_t V60Cpu::am1PCDisplacementIndirect32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)));
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)));
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1PCDisplacementIndirect32()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)));
	return 5;
}

uint32_t V60Cpu::am1PCDisplacementIndirectIndexed8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1PCDisplacementIndirectIndexed8()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}

uint32_t V60Cpu::am1PCDisplacementIndirectIndexed16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 4;
}

uint32_t V60Cpu::bam1PCDisplacementIndirectIndexed16()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 4;
}

uint32_t V60Cpu::am1PCDisplacementIndirectIndexed32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F]);
		break;
	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2);
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam1PCDisplacementIndirectIndexed32()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 6;
}

uint32_t V60Cpu::am1DoubleDisplacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2));
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1DoubleDisplacement8()
{
	m_bamoffset = OpRead8(m_modadd + 2);
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}

uint32_t V60Cpu::am1DoubleDisplacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1DoubleDisplacement16()
{
	m_bamoffset = OpRead16(m_modadd + 3);
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 5;
}

uint32_t V60Cpu::am1DoubleDisplacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5));
		break;
	}

	return 9;
}

uint32_t V60Cpu::bam1DoubleDisplacement32()
{
	m_bamoffset = OpRead32(m_modadd + 5);
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 9;
}

uint32_t V60Cpu::am1PCDoubleDisplacement8()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2));
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam1PCDoubleDisplacement8()
{
	m_bamoffset = OpRead8(m_modadd + 2);
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 3;
}

uint32_t V60Cpu::am1PCDoubleDisplacement16()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1PCDoubleDisplacement16()
{
	m_bamoffset = OpRead16(m_modadd + 3);
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 5;
}


uint32_t V60Cpu::am1PCDoubleDisplacement32()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5));
		break;
	}

	return 9;
}

uint32_t V60Cpu::bam1PCDoubleDisplacement32()
{
	m_bamoffset = OpRead32(m_modadd + 5);
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 9;
}

uint32_t V60Cpu::am1DirectAddress()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(OpRead32(m_modadd + 1));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(OpRead32(m_modadd + 1));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 1));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1DirectAddress()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 1));
	return 5;
}

uint32_t V60Cpu::am1DirectAddressIndexed()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F]);
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2);
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam1DirectAddressIndexed()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 2) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 6;
}

uint32_t V60Cpu::am1DirectAddressDeferred()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(OpRead32(m_modadd + 1)));
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 1)));
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 1)));
		break;
	}

	return 5;
}

uint32_t V60Cpu::bam1DirectAddressDeferred()
{
	m_bamoffset = 0;
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 1)));
	return 5;
}

uint32_t V60Cpu::am1DirectAddressDeferredIndexed()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_byte(m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F]);
		break;

	case 1:
		m_amout = m_program->read_word_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2);
		break;

	case 2:
		m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4);
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam1DirectAddressDeferredIndexed()
{
	m_bamoffset = m_reg[m_modval & 0x1F];
	m_amout = m_program->read_dword_unaligned(m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_bamoffset / 8);
	m_bamoffset&=7;
	return 6;
}

uint32_t V60Cpu::am1Immediate()
{
	switch (m_moddim)
	{
	case 0:
		m_amout = OpRead8(m_modadd + 1);
		return 2;

	case 1:
		m_amout = OpRead16(m_modadd + 1);
		return 3;

	case 2:
		m_amout = OpRead32(m_modadd + 1);
		return 5;
	}

	// It should not be here!  Written to avoid warning
	assert(0);
	return 1;
}

uint32_t V60Cpu::am1ImmediateQuick()
{
	m_amout = m_modval & 0xF;
	return 1;
}




// AM1 Tables (for ReadAM)
// ***********************

uint32_t V60Cpu::am1Error1()
{
	fatalerror("CPU - AM1 - 1 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam1Error1()
{
	fatalerror("CPU - BAM1 - 1 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am1Error2()
{
	fatalerror("CPU - AM1 - 2 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam1Error2()
{
	fatalerror("CPU - BAM1 - 2 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am1Error3()
{
	fatalerror("CPU - AM1 - 3 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam1Error3()
{
	fatalerror("CPU - BAM1 - 3 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am1Error4()
{
	fatalerror("CPU - AM1 - 4 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam1Error4()
{
	fatalerror("CPU - BAM1 - 4 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am1Error5()
{
	fatalerror("CPU - AM1 - 5 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam1Error5()
{
	fatalerror("CPU - BAM1 - 5 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam1Error6()
{
	fatalerror("CPU - BAM1 - 6 (PC=%06x)\n", PC);
}

const V60Cpu::am_func V60Cpu::s_AMTable1_G7a[16] =
{
	&V60Cpu::am1PCDisplacementIndexed8,
	&V60Cpu::am1PCDisplacementIndexed16,
	&V60Cpu::am1PCDisplacementIndexed32,
	&V60Cpu::am1DirectAddressIndexed,
	&V60Cpu::am1Error5,
	&V60Cpu::am1Error5,
	&V60Cpu::am1Error5,
	&V60Cpu::am1Error5,
	&V60Cpu::am1PCDisplacementIndirectIndexed8,
	&V60Cpu::am1PCDisplacementIndirectIndexed16,
	&V60Cpu::am1PCDisplacementIndirectIndexed32,
	&V60Cpu::am1DirectAddressDeferredIndexed,
	&V60Cpu::am1Error5,
	&V60Cpu::am1Error5,
	&V60Cpu::am1Error5,
	&V60Cpu::am1Error5
};

const V60Cpu::am_func V60Cpu::s_BAMTable1_G7a[16] =
{
	&V60Cpu::bam1PCDisplacementIndexed8,
	&V60Cpu::bam1PCDisplacementIndexed16,
	&V60Cpu::bam1PCDisplacementIndexed32,
	&V60Cpu::bam1DirectAddressIndexed,
	&V60Cpu::bam1Error5,
	&V60Cpu::bam1Error5,
	&V60Cpu::bam1Error5,
	&V60Cpu::bam1Error5,
	&V60Cpu::bam1PCDisplacementIndirectIndexed8,
	&V60Cpu::bam1PCDisplacementIndirectIndexed16,
	&V60Cpu::bam1PCDisplacementIndirectIndexed32,
	&V60Cpu::bam1DirectAddressDeferredIndexed,
	&V60Cpu::bam1Error5,
	&V60Cpu::bam1Error5,
	&V60Cpu::bam1Error5,
	&V60Cpu::bam1Error5
};


uint32_t V60Cpu::am1Group7a()
{
	if (!(m_modval2 & 0x10))
		return am1Error4();

	return (this->*s_AMTable1_G7a[m_modval2 & 0xF])();
}

uint32_t V60Cpu::bam1Group7a()
{
	if (!(m_modval2 & 0x10))
		return bam1Error4();

	return (this->*s_BAMTable1_G7a[m_modval2 & 0xF])();
}

const V60Cpu::am_func V60Cpu::s_AMTable1_G7[32] =
{
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1ImmediateQuick,
	&V60Cpu::am1PCDisplacement8,
	&V60Cpu::am1PCDisplacement16,
	&V60Cpu::am1PCDisplacement32,
	&V60Cpu::am1DirectAddress,
	&V60Cpu::am1Immediate,
	&V60Cpu::am1Error2,
	&V60Cpu::am1Error2,
	&V60Cpu::am1Error2,
	&V60Cpu::am1PCDisplacementIndirect8,
	&V60Cpu::am1PCDisplacementIndirect16,
	&V60Cpu::am1PCDisplacementIndirect32,
	&V60Cpu::am1DirectAddressDeferred,
	&V60Cpu::am1PCDoubleDisplacement8,
	&V60Cpu::am1PCDoubleDisplacement16,
	&V60Cpu::am1PCDoubleDisplacement32,
	&V60Cpu::am1Error2
};

const V60Cpu::am_func V60Cpu::s_BAMTable1_G7[32] =
{
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1PCDisplacement8,
	&V60Cpu::bam1PCDisplacement16,
	&V60Cpu::bam1PCDisplacement32,
	&V60Cpu::bam1DirectAddress,
	&V60Cpu::bam1Error6,
	&V60Cpu::bam1Error2,
	&V60Cpu::bam1Error2,
	&V60Cpu::bam1Error2,
	&V60Cpu::bam1PCDisplacementIndirect8,
	&V60Cpu::bam1PCDisplacementIndirect16,
	&V60Cpu::bam1PCDisplacementIndirect32,
	&V60Cpu::bam1DirectAddressDeferred,
	&V60Cpu::bam1PCDoubleDisplacement8,
	&V60Cpu::bam1PCDoubleDisplacement16,
	&V60Cpu::bam1PCDoubleDisplacement32,
	&V60Cpu::bam1Error2
};



const V60Cpu::am_func V60Cpu::s_AMTable1_G6[8] =
{
	&V60Cpu::am1DisplacementIndexed8,
	&V60Cpu::am1DisplacementIndexed16,
	&V60Cpu::am1DisplacementIndexed32,
	&V60Cpu::am1RegisterIndirectIndexed,
	&V60Cpu::am1DisplacementIndirectIndexed8,
	&V60Cpu::am1DisplacementIndirectIndexed16,
	&V60Cpu::am1DisplacementIndirectIndexed32,
	&V60Cpu::am1Group7a
};

const V60Cpu::am_func V60Cpu::s_BAMTable1_G6[8] =
{
	&V60Cpu::bam1DisplacementIndexed8,
	&V60Cpu::bam1DisplacementIndexed16,
	&V60Cpu::bam1DisplacementIndexed32,
	&V60Cpu::bam1RegisterIndirectIndexed,
	&V60Cpu::bam1DisplacementIndirectIndexed8,
	&V60Cpu::bam1DisplacementIndirectIndexed16,
	&V60Cpu::bam1DisplacementIndirectIndexed32,
	&V60Cpu::bam1Group7a
};


uint32_t V60Cpu::am1Group6()
{
	m_modval2 = OpRead8(m_modadd + 1);
	return (this->*s_AMTable1_G6[m_modval2 >> 5])();
}

uint32_t V60Cpu::bam1Group6()
{
	m_modval2 = OpRead8(m_modadd + 1);
	return (this->*s_BAMTable1_G6[m_modval2 >> 5])();
}


uint32_t V60Cpu::am1Group7()
{
	return (this->*s_AMTable1_G7[m_modval & 0x1F])();
}

uint32_t V60Cpu::bam1Group7()
{
	return (this->*s_BAMTable1_G7[m_modval & 0x1F])();
}

const V60Cpu::am_func V60Cpu::s_AMTable1[2][8] =
{
	{
		&V60Cpu::am1Displacement8,
		&V60Cpu::am1Displacement16,
		&V60Cpu::am1Displacement32,
		&V60Cpu::am1RegisterIndirect,
		&V60Cpu::am1DisplacementIndirect8,
		&V60Cpu::am1DisplacementIndirect16,
		&V60Cpu::am1DisplacementIndirect32,
		&V60Cpu::am1Group7
	},

	{
		&V60Cpu::am1DoubleDisplacement8,
		&V60Cpu::am1DoubleDisplacement16,
		&V60Cpu::am1DoubleDisplacement32,
		&V60Cpu::am1Register,
		&V60Cpu::am1Autoincrement,
		&V60Cpu::am1Autodecrement,
		&V60Cpu::am1Group6,
		&V60Cpu::am1Error1
	}
};


const V60Cpu::am_func V60Cpu::s_BAMTable1[2][8] =
{
	{
		&V60Cpu::bam1Displacement8,
		&V60Cpu::bam1Displacement16,
		&V60Cpu::bam1Displacement32,
		&V60Cpu::bam1RegisterIndirect,
		&V60Cpu::bam1DisplacementIndirect8,
		&V60Cpu::bam1DisplacementIndirect16,
		&V60Cpu::bam1DisplacementIndirect32,
		&V60Cpu::bam1Group7
	},

	{
		&V60Cpu::bam1DoubleDisplacement8,
		&V60Cpu::bam1DoubleDisplacement16,
		&V60Cpu::bam1DoubleDisplacement32,
		&V60Cpu::bam1Error6,
		&V60Cpu::bam1Autoincrement,
		&V60Cpu::bam1Autodecrement,
		&V60Cpu::bam1Group6,
		&V60Cpu::bam1Error1
	}
};
