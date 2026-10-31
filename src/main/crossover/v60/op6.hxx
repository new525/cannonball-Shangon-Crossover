// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// PORT: MAME 0.264 src/devices/cpu/v60/op6.hxx, verbatim except the class name
// (v60_device -> V60Cpu). Part of the crossover's V60/V70 port, see v60.hpp.
/*
    FULLY TRUSTED
*/

uint32_t V60Cpu::opTB(int reg) /* TRUSTED */
{
	if (m_reg[reg] == 0)
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBGT(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	NORMALIZEFLAGS();
	if ((m_reg[reg] != 0) && !((_S ^ _OV) | _Z))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBLE(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	NORMALIZEFLAGS();
	if ((m_reg[reg] != 0) && ((_S ^ _OV) | _Z))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}


uint32_t V60Cpu::opDBGE(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	NORMALIZEFLAGS();
	if ((m_reg[reg] != 0) && !(_S ^ _OV))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBLT(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	NORMALIZEFLAGS();
	if ((m_reg[reg] != 0) && (_S ^ _OV))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBH(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && !(_CY | _Z))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBNH(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && (_CY | _Z))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}


uint32_t V60Cpu::opDBL(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && (_CY))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBNL(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && !(_CY))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBE(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && (_Z))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBNE(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && !(_Z))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBV(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && (_OV))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBNV(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && !(_OV))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBN(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && (_S))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBP(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if ((m_reg[reg] != 0) && !(_S))
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

uint32_t V60Cpu::opDBR(int reg) /* TRUSTED */
{
	m_reg[reg]--;

	if (m_reg[reg] != 0)
	{
		PC += (int16_t)OpRead16(PC + 2);
		return 0;
	}

	return 4;
}

const V60Cpu::op6_func V60Cpu::s_OpC6Table[8] = /* TRUSTED */
{
	&V60Cpu::opDBV,
	&V60Cpu::opDBL,
	&V60Cpu::opDBE,
	&V60Cpu::opDBNH,
	&V60Cpu::opDBN,
	&V60Cpu::opDBR,
	&V60Cpu::opDBLT,
	&V60Cpu::opDBLE
};

const V60Cpu::op6_func V60Cpu::s_OpC7Table[8] = /* TRUSTED */
{
	&V60Cpu::opDBNV,
	&V60Cpu::opDBNL,
	&V60Cpu::opDBNE,
	&V60Cpu::opDBH,
	&V60Cpu::opDBP,
	&V60Cpu::opTB,
	&V60Cpu::opDBGE,
	&V60Cpu::opDBGT
};


uint32_t V60Cpu::opC6() /* TRUSTED */
{
	uint8_t appb = OpRead8(PC + 1);
	return (this->*s_OpC6Table[appb >> 5])(appb & 0x1f);
}

uint32_t V60Cpu::opC7() /* TRUSTED */
{
	uint8_t appb = OpRead8(PC + 1);
	return (this->*s_OpC7Table[appb >> 5])(appb & 0x1f);
}
