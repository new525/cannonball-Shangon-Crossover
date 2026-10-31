// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// PORT: MAME 0.264 src/devices/cpu/v60/op5.hxx, verbatim except the class name
// (v60_device -> V60Cpu). Part of the crossover's V60/V70 port, see v60.hpp.
/*
 *  HALT: must add log
 */

uint32_t V60Cpu::opBRK()
{
/*
    uint32_t oldPSW = v60_update_psw_for_exception(0, 0);

    SP -=4;
    m_program->write_dword_unaligned(SP, EXCEPTION_CODE_AND_SIZE(0x0d00, 4));
    SP -=4;
    m_program->write_dword_unaligned(SP, oldPSW);
    SP -=4;
    m_program->write_dword_unaligned(SP, PC + 1);
    PC = GETINTVECT(13);
*/
	logerror("Skipping BRK opcode! PC=%x", PC);

	return 1;
}

uint32_t V60Cpu::opBRKV()
{
	uint32_t oldPSW = v60_update_psw_for_exception(0, 0);

	SP -=4;
	m_program->write_dword_unaligned(SP, PC);
	SP -=4;
	m_program->write_dword_unaligned(SP, EXCEPTION_CODE_AND_SIZE(0x1501, 4));
	SP -=4;
	m_program->write_dword_unaligned(SP, oldPSW);
	SP -=4;
	m_program->write_dword_unaligned(SP, PC + 1);
	PC = GETINTVECT(21);

	return 0;
}

uint32_t V60Cpu::opCLRTLBA()
{
	// @@@ TLB not yet supported
	logerror("Skipping CLRTLBA opcode! PC=%x\n", PC);
	return 1;
}

uint32_t V60Cpu::opDISPOSE()
{
	SP = FP;
	FP = m_program->read_dword_unaligned(SP);
	SP +=4;

	return 1;
}

uint32_t V60Cpu::opHALT()
{
	// @@@ It should wait for an interrupt to occur
	//logerror("HALT found: skipping");
	return 1;
}

uint32_t V60Cpu::opNOP() /* TRUSTED */
{
	return 1;
}

uint32_t V60Cpu::opRSR()
{
	PC = m_program->read_dword_unaligned(SP);
	SP +=4;

	return 0;
}

uint32_t V60Cpu::opTRAPFL()
{
	if ((TKCW & 0x1F0) & ((v60ReadPSW() & 0x1F00) >> 4))
	{
		// @@@ FPU exception
		fatalerror("Hit TRAPFL! PC=%x\n", PC);
	}

	return 1;
}
