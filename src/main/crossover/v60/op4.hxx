// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// PORT: MAME 0.264 src/devices/cpu/v60/op4.hxx, verbatim except the class name
// (v60_device -> V60Cpu). Part of the crossover's V60/V70 port, see v60.hpp.
/*
    FULLY TRUSTED
*/

uint32_t V60Cpu::opBGT8() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if (!((_S ^ _OV) | _Z))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBGT16() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if (!((_S ^ _OV) | _Z))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}


uint32_t V60Cpu::opBGE8() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if (!(_S ^ _OV))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBGE16() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if (!(_S ^ _OV))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBLT8() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if ((_S ^ _OV))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBLT16() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if ((_S ^ _OV))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}


uint32_t V60Cpu::opBLE8() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if (((_S ^ _OV) | _Z))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBLE16() /* TRUSTED */
{
	NORMALIZEFLAGS();

	if (((_S ^ _OV) | _Z))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBH8() /* TRUSTED */
{
	if (!(_CY | _Z))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBH16() /* TRUSTED */
{
	if (!(_CY | _Z))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBNH8() /* TRUSTED */
{
	if ((_CY | _Z))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBNH16() /* TRUSTED */
{
	if ((_CY | _Z))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBNL8() /* TRUSTED */
{
	if (!(_CY))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBNL16() /* TRUSTED */
{
	if (!(_CY))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBL8() /* TRUSTED */
{
	if ((_CY))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBL16() /* TRUSTED */
{
	if ((_CY))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBNE8() /* TRUSTED */
{
	if (!(_Z))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBNE16() /* TRUSTED */
{
	if (!(_Z))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBE8() /* TRUSTED */
{
	if ((_Z))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBE16() /* TRUSTED */
{
	if ((_Z))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBNV8() /* TRUSTED */
{
	if (!(_OV))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBNV16() /* TRUSTED */
{
	if (!(_OV))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBV8() /* TRUSTED */
{
	if ((_OV))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBV16() /* TRUSTED */
{
	if ((_OV))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBP8() /* TRUSTED */
{
	if (!(_S))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBP16() /* TRUSTED */
{
	if (!(_S))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBN8() /* TRUSTED */
{
	if ((_S))
	{
		PC += (int8_t)OpRead8(PC + 1);
		return 0;
	}

	return 2;
}

uint32_t V60Cpu::opBN16() /* TRUSTED */
{
	if ((_S))
	{
		PC += (int16_t)OpRead16(PC + 1);
		return 0;
	}

	return 3;
}

uint32_t V60Cpu::opBR8() /* TRUSTED */
{
	PC += (int8_t)OpRead8(PC + 1);
	return 0;
}

uint32_t V60Cpu::opBR16() /* TRUSTED */
{
	PC += (int16_t)OpRead16(PC + 1);
	return 0;
}

uint32_t V60Cpu::opBSR() /* TRUSTED */
{
	// Save Next PC onto the stack
	SP -= 4;
	m_program->write_dword_unaligned(SP, PC + 3);

	// Jump to subroutine
	PC += (int16_t)OpRead16(PC + 1);
	return 0;
}
