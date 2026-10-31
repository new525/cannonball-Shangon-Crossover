// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// PORT: MAME 0.264 src/devices/cpu/v60/am2.hxx, verbatim except the class name
// (v60_device -> V60Cpu). Part of the crossover's V60/V70 port, see v60.hpp.
// AM2 Functions (for ReadAMAddress)
// *********************************

uint32_t V60Cpu::am2Register()
{
	m_amflag = 1;
	m_amout = m_modval & 0x1F;
	return 1;
}

uint32_t V60Cpu::am2RegisterIndirect()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F];
	return 1;
}

uint32_t V60Cpu::bam2RegisterIndirect()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F];
	m_bamoffset = 0;
	return 1;
}

uint32_t V60Cpu::am2RegisterIndirectIndexed()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_reg[m_modval2 & 0x1F] + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 2;
}

uint32_t V60Cpu::bam2RegisterIndirectIndexed()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval2 & 0x1F];
	m_bamoffset = m_reg[m_modval & 0x1F];
	return 2;
}

uint32_t V60Cpu::am2Autoincrement()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F];

	switch (m_moddim)
	{
	case 0:
		m_reg[m_modval & 0x1F] += 1;
		break;
	case 1:
		m_reg[m_modval & 0x1F] += 2;
		break;
	case 2:
		m_reg[m_modval & 0x1F] += 4;
		break;
	case 3:
		m_reg[m_modval & 0x1F] += 8;
		break;
	}

	return 1;
}

uint32_t V60Cpu::bam2Autoincrement()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F];
	m_bamoffset = 0;

	switch (m_moddim)
	{
	case 10:
		m_reg[m_modval & 0x1F] +=1;
		break;
	case 11:
		m_reg[m_modval & 0x1F] +=4;
		break;
	default:
		fatalerror("CPU - AM2 - 7 (t0 PC=%x)\n", PC);
		break;
	}

	return 1;
}

uint32_t V60Cpu::am2Autodecrement()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_reg[m_modval & 0x1F] -= 1;
		break;
	case 1:
		m_reg[m_modval & 0x1F] -= 2;
		break;
	case 2:
		m_reg[m_modval & 0x1F] -= 4;
		break;
	case 3:
		m_reg[m_modval & 0x1F] -= 8;
		break;
	}

	m_amout = m_reg[m_modval & 0x1F];
	return 1;
}

uint32_t V60Cpu::bam2Autodecrement()
{
	m_amflag = 0;
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
		fatalerror("CPU - BAM2 - 7 (PC=%06x)\n", PC);
		break;
	}

	m_amout = m_reg[m_modval & 0x1F];
	return 1;
}


uint32_t V60Cpu::am2Displacement8()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1);

	return 2;
}

uint32_t V60Cpu::bam2Displacement8()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F];
	m_bamoffset = (int8_t)OpRead8(m_modadd + 1);

	return 2;
}

uint32_t V60Cpu::am2Displacement16()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1);

	return 3;
}

uint32_t V60Cpu::bam2Displacement16()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F];
	m_bamoffset = (int16_t)OpRead16(m_modadd + 1);

	return 3;
}

uint32_t V60Cpu::am2Displacement32()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1);

	return 5;
}

uint32_t V60Cpu::bam2Displacement32()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval & 0x1F];
	m_bamoffset = OpRead32(m_modadd + 1);

	return 5;
}

uint32_t V60Cpu::am2DisplacementIndexed8()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam2DisplacementIndexed8()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2);
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 3;
}

uint32_t V60Cpu::am2DisplacementIndexed16()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 4;
}

uint32_t V60Cpu::bam2DisplacementIndexed16()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2);
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 4;
}

uint32_t V60Cpu::am2DisplacementIndexed32()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam2DisplacementIndexed32()
{
	m_amflag = 0;
	m_amout = m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2);
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 6;
}

uint32_t V60Cpu::am2PCDisplacement8()
{
	m_amflag = 0;
	m_amout = PC + (int8_t)OpRead8(m_modadd + 1);

	return 2;
}

uint32_t V60Cpu::bam2PCDisplacement8()
{
	m_amflag = 0;
	m_amout = PC;
	m_bamoffset = (int8_t)OpRead8(m_modadd + 1);

	return 2;
}

uint32_t V60Cpu::am2PCDisplacement16()
{
	m_amflag = 0;
	m_amout = PC + (int16_t)OpRead16(m_modadd + 1);

	return 3;
}

uint32_t V60Cpu::bam2PCDisplacement16()
{
	m_amflag = 0;
	m_amout = PC;
	m_bamoffset = (int16_t)OpRead16(m_modadd + 1);

	return 3;
}

uint32_t V60Cpu::am2PCDisplacement32()
{
	m_amflag = 0;
	m_amout = PC + OpRead32(m_modadd + 1);

	return 5;
}

uint32_t V60Cpu::bam2PCDisplacement32()
{
	m_amflag = 0;
	m_amout = PC;
	m_bamoffset = OpRead32(m_modadd + 1);

	return 5;
}


uint32_t V60Cpu::am2PCDisplacementIndexed8()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = PC + (int8_t)OpRead8(m_modadd + 2) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam2PCDisplacementIndexed8()
{
	m_amflag = 0;
	m_amout = PC + (int8_t)OpRead8(m_modadd + 2);
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 3;
}

uint32_t V60Cpu::am2PCDisplacementIndexed16()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = PC + (int16_t)OpRead16(m_modadd + 2) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 4;
}

uint32_t V60Cpu::bam2PCDisplacementIndexed16()
{
	m_amflag = 0;
	m_amout = PC + (int16_t)OpRead16(m_modadd + 2);
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 4;
}

uint32_t V60Cpu::am2PCDisplacementIndexed32()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = PC + OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam2PCDisplacementIndexed32()
{
	m_amflag = 0;
	m_amout = PC + OpRead32(m_modadd + 2);
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 6;
}

uint32_t V60Cpu::am2DisplacementIndirect8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1));

	return 2;
}

uint32_t V60Cpu::bam2DisplacementIndirect8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1));
	m_bamoffset = 0;
	return 2;
}

uint32_t V60Cpu::am2DisplacementIndirect16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1));

	return 3;
}

uint32_t V60Cpu::bam2DisplacementIndirect16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1));
	m_bamoffset = 0;
	return 3;
}

uint32_t V60Cpu::am2DisplacementIndirect32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1));

	return 5;
}

uint32_t V60Cpu::bam2DisplacementIndirect32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1));
	m_bamoffset = 0;

	return 5;
}

uint32_t V60Cpu::am2DisplacementIndirectIndexed8()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam2DisplacementIndirectIndexed8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int8_t)OpRead8(m_modadd + 2));
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 3;
}

uint32_t V60Cpu::am2DisplacementIndirectIndexed16()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 4;
}

uint32_t V60Cpu::bam2DisplacementIndirectIndexed16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + (int16_t)OpRead16(m_modadd + 2));
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 4;
}

uint32_t V60Cpu::am2DisplacementIndirectIndexed32()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam2DisplacementIndirectIndexed32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval2 & 0x1F] + OpRead32(m_modadd + 2));
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 6;
}

uint32_t V60Cpu::am2PCDisplacementIndirect8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1));

	return 2;
}

uint32_t V60Cpu::bam2PCDisplacementIndirect8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1));
	m_bamoffset = 0;

	return 2;
}

uint32_t V60Cpu::am2PCDisplacementIndirect16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1));

	return 3;
}

uint32_t V60Cpu::bam2PCDisplacementIndirect16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1));
	m_bamoffset = 0;

	return 3;
}

uint32_t V60Cpu::am2PCDisplacementIndirect32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1));

	return 5;
}

uint32_t V60Cpu::bam2PCDisplacementIndirect32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1));
	m_bamoffset = 0;

	return 5;
}

uint32_t V60Cpu::am2PCDisplacementIndirectIndexed8()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 3;
}

uint32_t V60Cpu::bam2PCDisplacementIndirectIndexed8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 2));
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 3;
}

uint32_t V60Cpu::am2PCDisplacementIndirectIndexed16()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 4;
}


uint32_t V60Cpu::bam2PCDisplacementIndirectIndexed16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 2));
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 4;
}


uint32_t V60Cpu::am2PCDisplacementIndirectIndexed32()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam2PCDisplacementIndirectIndexed32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 2));
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 6;
}

uint32_t V60Cpu::am2DoubleDisplacement8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2);

	return 3;
}

uint32_t V60Cpu::bam2DoubleDisplacement8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int8_t)OpRead8(m_modadd + 1));
	m_bamoffset = (int8_t)OpRead8(m_modadd + 2);

	return 3;
}

uint32_t V60Cpu::am2DoubleDisplacement16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3);

	return 5;
}

uint32_t V60Cpu::bam2DoubleDisplacement16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + (int16_t)OpRead16(m_modadd + 1));
	m_bamoffset = (int8_t)OpRead8(m_modadd + 3);

	return 5;
}

uint32_t V60Cpu::am2DoubleDisplacement32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5);

	return 9;
}

uint32_t V60Cpu::bam2DoubleDisplacement32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(m_reg[m_modval & 0x1F] + OpRead32(m_modadd + 1));
	m_bamoffset = OpRead32(m_modadd + 5);

	return 9;
}


uint32_t V60Cpu::am2PCDoubleDisplacement8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1)) + (int8_t)OpRead8(m_modadd + 2);

	return 3;
}

uint32_t V60Cpu::bam2PCDoubleDisplacement8()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int8_t)OpRead8(m_modadd + 1));
	m_bamoffset = (int8_t)OpRead8(m_modadd + 2);

	return 3;
}

uint32_t V60Cpu::am2PCDoubleDisplacement16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1)) + (int16_t)OpRead16(m_modadd + 3);

	return 5;
}

uint32_t V60Cpu::bam2PCDoubleDisplacement16()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + (int16_t)OpRead16(m_modadd + 1));
	m_bamoffset = (int8_t)OpRead8(m_modadd + 3);

	return 5;
}

uint32_t V60Cpu::am2PCDoubleDisplacement32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1)) + OpRead32(m_modadd + 5);

	return 9;
}

uint32_t V60Cpu::bam2PCDoubleDisplacement32()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(PC + OpRead32(m_modadd + 1));
	m_bamoffset = OpRead32(m_modadd + 5);

	return 9;
}

uint32_t V60Cpu::am2DirectAddress()
{
	m_amflag = 0;
	m_amout = OpRead32(m_modadd + 1);

	return 5;
}

uint32_t V60Cpu::bam2DirectAddress()
{
	m_amflag = 0;
	m_amout = OpRead32(m_modadd + 1);
	m_bamoffset = 0;

	return 5;
}

uint32_t V60Cpu::am2DirectAddressIndexed()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = OpRead32(m_modadd + 2) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam2DirectAddressIndexed()
{
	m_amflag = 0;
	m_amout = OpRead32(m_modadd + 2);
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 6;
}

uint32_t V60Cpu::am2DirectAddressDeferred()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 1));

	return 5;
}

uint32_t V60Cpu::bam2DirectAddressDeferred()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 1));
	m_bamoffset = 0;

	return 5;
}

uint32_t V60Cpu::am2DirectAddressDeferredIndexed()
{
	m_amflag = 0;

	switch (m_moddim)
	{
	case 0:
		m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F];
		break;
	case 1:
		m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 2;
		break;
	case 2:
		m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 4;
		break;
	case 3:
		m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 2)) + m_reg[m_modval & 0x1F] * 8;
		break;
	}

	return 6;
}

uint32_t V60Cpu::bam2DirectAddressDeferredIndexed()
{
	m_amflag = 0;
	m_amout = m_program->read_dword_unaligned(OpRead32(m_modadd + 2));
	m_bamoffset = m_reg[m_modval & 0x1F];

	return 6;
}

uint32_t V60Cpu::am2Immediate()
{
	// ignore LDPR
	return am1Immediate();
}

uint32_t V60Cpu::am2ImmediateQuick()
{
	// ignore LDPR
	return am1ImmediateQuick();
}


// AM2 Tables (for ReadAMAddress)
// ******************************

uint32_t V60Cpu::am2Error1()
{
	// f1lap trips this, why?
	logerror("CPU - AM2 - 1 (PC=%06x)", PC);
	return 0;
}

uint32_t V60Cpu::am2Error2()
{
	fatalerror("CPU - AM2 - 2 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am2Error3()
{
	fatalerror("CPU - AM2 - 3 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am2Error4()
{
	fatalerror("CPU - AM2 - 4 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::am2Error5()
{
	fatalerror("CPU - AM2 - 5 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam2Error1()
{
	fatalerror("CPU - BAM2 - 1 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam2Error2()
{
	fatalerror("CPU - BAM2 - 2 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam2Error3()
{
	fatalerror("CPU - BAM2 - 3 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam2Error4()
{
	fatalerror("CPU - BAM2 - 4 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam2Error5()
{
	fatalerror("CPU - BAM2 - 5 (PC=%06x)\n", PC);
}

uint32_t V60Cpu::bam2Error6()
{
	fatalerror("CPU - BAM2 - 6 (PC=%06x)\n", PC);
}


const V60Cpu::am_func V60Cpu::s_AMTable2_G7a[16] =
{
	&V60Cpu::am2PCDisplacementIndexed8,
	&V60Cpu::am2PCDisplacementIndexed16,
	&V60Cpu::am2PCDisplacementIndexed32,
	&V60Cpu::am2DirectAddressIndexed,
	&V60Cpu::am2Error5,
	&V60Cpu::am2Error5,
	&V60Cpu::am2Error5,
	&V60Cpu::am2Error5,
	&V60Cpu::am2PCDisplacementIndirectIndexed8,
	&V60Cpu::am2PCDisplacementIndirectIndexed16,
	&V60Cpu::am2PCDisplacementIndirectIndexed32,
	&V60Cpu::am2DirectAddressDeferredIndexed,
	&V60Cpu::am2Error5,
	&V60Cpu::am2Error5,
	&V60Cpu::am2Error5,
	&V60Cpu::am2Error5
};

const V60Cpu::am_func V60Cpu::s_BAMTable2_G7a[16] =
{
	&V60Cpu::bam2PCDisplacementIndexed8,
	&V60Cpu::bam2PCDisplacementIndexed16,
	&V60Cpu::bam2PCDisplacementIndexed32,
	&V60Cpu::bam2DirectAddressIndexed,
	&V60Cpu::bam2Error5,
	&V60Cpu::bam2Error5,
	&V60Cpu::bam2Error5,
	&V60Cpu::bam2Error5,
	&V60Cpu::bam2PCDisplacementIndirectIndexed8,
	&V60Cpu::bam2PCDisplacementIndirectIndexed16,
	&V60Cpu::bam2PCDisplacementIndirectIndexed32,
	&V60Cpu::bam2DirectAddressDeferredIndexed,
	&V60Cpu::bam2Error5,
	&V60Cpu::bam2Error5,
	&V60Cpu::bam2Error5,
	&V60Cpu::bam2Error5
};

uint32_t V60Cpu::am2Group7a()
{
	if (!(m_modval2 & 0x10))
		return am2Error4();

	return (this->*s_AMTable2_G7a[m_modval2 & 0xF])();
}

uint32_t V60Cpu::bam2Group7a()
{
	if (!(m_modval2 & 0x10))
		return bam2Error4();

	return (this->*s_BAMTable2_G7a[m_modval2 & 0xF])();
}

const V60Cpu::am_func V60Cpu::s_AMTable2_G7[32] =
{
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2ImmediateQuick,
	&V60Cpu::am2PCDisplacement8,
	&V60Cpu::am2PCDisplacement16,
	&V60Cpu::am2PCDisplacement32,
	&V60Cpu::am2DirectAddress,
	&V60Cpu::am2Immediate,
	&V60Cpu::am2Error2,
	&V60Cpu::am2Error2,
	&V60Cpu::am2Error2,
	&V60Cpu::am2PCDisplacementIndirect8,
	&V60Cpu::am2PCDisplacementIndirect16,
	&V60Cpu::am2PCDisplacementIndirect32,
	&V60Cpu::am2DirectAddressDeferred,
	&V60Cpu::am2PCDoubleDisplacement8,
	&V60Cpu::am2PCDoubleDisplacement16,
	&V60Cpu::am2PCDoubleDisplacement32,
	&V60Cpu::am2Error2
};

const V60Cpu::am_func V60Cpu::s_BAMTable2_G7[32] =
{
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2PCDisplacement8,
	&V60Cpu::bam2PCDisplacement16,
	&V60Cpu::bam2PCDisplacement32,
	&V60Cpu::bam2DirectAddress,
	&V60Cpu::bam2Error6,
	&V60Cpu::bam2Error2,
	&V60Cpu::bam2Error2,
	&V60Cpu::bam2Error2,
	&V60Cpu::bam2PCDisplacementIndirect8,
	&V60Cpu::bam2PCDisplacementIndirect16,
	&V60Cpu::bam2PCDisplacementIndirect32,
	&V60Cpu::bam2DirectAddressDeferred,
	&V60Cpu::bam2PCDoubleDisplacement8,
	&V60Cpu::bam2PCDoubleDisplacement16,
	&V60Cpu::bam2PCDoubleDisplacement32,
	&V60Cpu::bam2Error2
};

const V60Cpu::am_func V60Cpu::s_AMTable2_G6[8] =
{
	&V60Cpu::am2DisplacementIndexed8,
	&V60Cpu::am2DisplacementIndexed16,
	&V60Cpu::am2DisplacementIndexed32,
	&V60Cpu::am2RegisterIndirectIndexed,
	&V60Cpu::am2DisplacementIndirectIndexed8,
	&V60Cpu::am2DisplacementIndirectIndexed16,
	&V60Cpu::am2DisplacementIndirectIndexed32,
	&V60Cpu::am2Group7a
};

const V60Cpu::am_func V60Cpu::s_BAMTable2_G6[8] =
{
	&V60Cpu::bam2DisplacementIndexed8,
	&V60Cpu::bam2DisplacementIndexed16,
	&V60Cpu::bam2DisplacementIndexed32,
	&V60Cpu::bam2RegisterIndirectIndexed,
	&V60Cpu::bam2DisplacementIndirectIndexed8,
	&V60Cpu::bam2DisplacementIndirectIndexed16,
	&V60Cpu::bam2DisplacementIndirectIndexed32,
	&V60Cpu::bam2Group7a
};




uint32_t V60Cpu::am2Group6()
{
	m_modval2 = OpRead8(m_modadd + 1);
	return (this->*s_AMTable2_G6[m_modval2 >> 5])();
}
uint32_t V60Cpu::bam2Group6()
{
	m_modval2 = OpRead8(m_modadd + 1);
	return (this->*s_BAMTable2_G6[m_modval2 >> 5])();
}

uint32_t V60Cpu::am2Group7()
{
	return (this->*s_AMTable2_G7[m_modval & 0x1F])();
}
uint32_t V60Cpu::bam2Group7()
{
	return (this->*s_BAMTable2_G7[m_modval & 0x1F])();
}


const V60Cpu::am_func V60Cpu::s_AMTable2[2][8] =
{
	{
		&V60Cpu::am2Displacement8,
		&V60Cpu::am2Displacement16,
		&V60Cpu::am2Displacement32,
		&V60Cpu::am2RegisterIndirect,
		&V60Cpu::am2DisplacementIndirect8,
		&V60Cpu::am2DisplacementIndirect16,
		&V60Cpu::am2DisplacementIndirect32,
		&V60Cpu::am2Group7
	},

	{
		&V60Cpu::am2DoubleDisplacement8,
		&V60Cpu::am2DoubleDisplacement16,
		&V60Cpu::am2DoubleDisplacement32,
		&V60Cpu::am2Register,
		&V60Cpu::am2Autoincrement,
		&V60Cpu::am2Autodecrement,
		&V60Cpu::am2Group6,
		&V60Cpu::am2Error1
	}
};

const V60Cpu::am_func V60Cpu::s_BAMTable2[2][8] =
{
	{
		&V60Cpu::bam2Displacement8,
		&V60Cpu::bam2Displacement16,
		&V60Cpu::bam2Displacement32,
		&V60Cpu::bam2RegisterIndirect,
		&V60Cpu::bam2DisplacementIndirect8,
		&V60Cpu::bam2DisplacementIndirect16,
		&V60Cpu::bam2DisplacementIndirect32,
		&V60Cpu::bam2Group7
	},

	{
		&V60Cpu::bam2DoubleDisplacement8,
		&V60Cpu::bam2DoubleDisplacement16,
		&V60Cpu::bam2DoubleDisplacement32,
		&V60Cpu::bam2Error6,
		&V60Cpu::bam2Autoincrement,
		&V60Cpu::bam2Autodecrement,
		&V60Cpu::bam2Group6,
		&V60Cpu::bam2Error1
	}
};
