// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
// PORT: MAME 0.264 src/devices/cpu/v60/op2.hxx, verbatim except the class name
// (v60_device -> V60Cpu). Part of the crossover's V60/V70 port, see v60.hpp.

#define F2END() \
	return 2 + m_amlength1 + m_amlength2;

#define F2LOADOPFLOAT(num)                              \
	if (m_flag##num)                                    \
		appf = u2f(m_reg[m_op##num]);               \
	else                                                    \
		appf = u2f(m_program->read_dword_unaligned(m_op##num));

#define F2STOREOPFLOAT(num)                              \
	if (m_flag##num)                                    \
		m_reg[m_op##num] = f2u(appf);               \
	else                                                    \
		m_program->write_dword_unaligned(m_op##num, f2u(appf));

void V60Cpu::F2DecodeFirstOperand(am_func DecodeOp1, uint8_t dim1)
{
	m_moddim = dim1;
	m_modm = m_instflags & 0x40;
	m_modadd = PC + 2;
	m_amlength1 = (this->*DecodeOp1)();
	m_op1 = m_amout;
	m_flag1 = m_amflag;
}

void V60Cpu::F2DecodeSecondOperand(am_func DecodeOp2, uint8_t dim2)
{
	m_moddim = dim2;
	m_modm = m_instflags & 0x20;
	m_modadd = PC + 2 + m_amlength1;
	m_amlength2 = (this->*DecodeOp2)();
	m_op2 = m_amout;
	m_flag2 = m_amflag;
}

void V60Cpu::F2WriteSecondOperand(uint8_t dim2)
{
	m_moddim = dim2;
	m_modm = m_instflags & 0x20;
	m_modadd = PC + 2 + m_amlength1;
	m_amlength2 = WriteAM();
}

uint32_t V60Cpu::opCVTWS()
{
	float val;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);

	// Convert to float
	val = (float)(int32_t)m_op1;
	m_modwritevalw = f2u(val);

	_OV = 0;
	_CY = (val < 0.0f);
	_S = ((m_modwritevalw & 0x80000000) != 0);
	_Z = (val == 0.0f);

	F2WriteSecondOperand(2);
	F2END();
}

uint32_t V60Cpu::opCVTSW()
{
	float val;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);

	// Convert to uint32_t
	val = u2f(m_op1);
	m_modwritevalw = (uint32_t)val;

	_OV = 0;
	_CY =(val < 0.0f);
	_S = ((m_modwritevalw & 0x80000000) != 0);
	_Z = (val == 0.0f);

	F2WriteSecondOperand(2);
	F2END();
}

uint32_t V60Cpu::opMOVFS()
{
	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	m_modwritevalw = m_op1;
	F2WriteSecondOperand(2);
	F2END();
}

uint32_t V60Cpu::opNEGFS()
{
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	F2DecodeSecondOperand(&V60Cpu::ReadAMAddress, 2);

	appf = -u2f(m_op1);

	_OV = 0;
	_CY = (appf < 0.0f);
	_S = ((f2u(appf) & 0x80000000) != 0);
	_Z = (appf == 0.0f);

	F2STOREOPFLOAT(2);
	F2END()
}

uint32_t V60Cpu::opABSFS()
{
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	F2DecodeSecondOperand(&V60Cpu::ReadAMAddress, 2);

	appf = u2f(m_op1);

	if(appf < 0)
		appf = -appf;

	_OV = 0;
	_CY = 0;
	_S = ((f2u(appf) & 0x80000000) != 0);
	_Z = (appf == 0.0f);

	F2STOREOPFLOAT(2);
	F2END()
}

uint32_t V60Cpu::opADDFS()
{
	uint32_t appw;
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	F2DecodeSecondOperand(&V60Cpu::ReadAMAddress, 2);

	F2LOADOPFLOAT(2);

	appf += u2f(m_op1);

	appw = f2u(appf);
	_OV = _CY = 0;
	_S = ((appw & 0x80000000) != 0);
	_Z = (appw == 0);

	F2STOREOPFLOAT(2);
	F2END()
}

uint32_t V60Cpu::opSUBFS()
{
	uint32_t appw;
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	F2DecodeSecondOperand(&V60Cpu::ReadAMAddress, 2);

	F2LOADOPFLOAT(2);

	appf -= u2f(m_op1);

	appw = f2u(appf);
	_OV = _CY = 0;
	_S = ((appw & 0x80000000) != 0);
	_Z = (appw == 0);

	F2STOREOPFLOAT(2);
	F2END()
}

uint32_t V60Cpu::opMULFS()
{
	uint32_t appw;
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	F2DecodeSecondOperand(&V60Cpu::ReadAMAddress, 2);

	F2LOADOPFLOAT(2);

	appf *= u2f(m_op1);

	appw = f2u(appf);
	_OV = _CY = 0;
	_S = ((appw & 0x80000000) != 0);
	_Z = (appw == 0);

	F2STOREOPFLOAT(2);
	F2END()
}

uint32_t V60Cpu::opDIVFS()
{
	uint32_t appw;
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	F2DecodeSecondOperand(&V60Cpu::ReadAMAddress, 2);

	F2LOADOPFLOAT(2);

	appf /= u2f(m_op1);

	appw = f2u(appf);
	_OV = _CY = 0;
	_S = ((appw & 0x80000000) != 0);
	_Z = (appw == 0);

	F2STOREOPFLOAT(2);
	F2END()
}

uint32_t V60Cpu::opSCLFS()
{
	uint32_t appw;
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 1);
	F2DecodeSecondOperand(&V60Cpu::ReadAMAddress, 2);

	F2LOADOPFLOAT(2);

	if ((int16_t)m_op1 < 0)
		appf /= 1 << -(int16_t)m_op1;
	else
		appf *= 1 << m_op1;

	appw = f2u(appf);
	_OV = _CY = 0;
	_S = ((appw & 0x80000000) != 0);
	_Z = (appw == 0);

	F2STOREOPFLOAT(2);
	F2END()
}

uint32_t V60Cpu::opCMPF()
{
	float appf;

	F2DecodeFirstOperand(&V60Cpu::ReadAM, 2);
	F2DecodeSecondOperand(&V60Cpu::ReadAM, 2);

	appf = u2f(m_op2) - u2f(m_op1);

	_Z = (appf == 0);
	_S = (appf < 0);
	_OV = 0;
	_CY = 0;

	F2END();
}

uint32_t V60Cpu::op5FUNHANDLED()
{
	fatalerror("Unhandled 5F opcode at %08x\n", PC);
}

uint32_t V60Cpu::op5CUNHANDLED()
{
	fatalerror("Unhandled 5C opcode at %08x\n", PC);
}

const V60Cpu::am_func V60Cpu::s_Op5FTable[32] =
{
	&V60Cpu::opCVTWS,
	&V60Cpu::opCVTSW,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED,
	&V60Cpu::op5FUNHANDLED
};

const V60Cpu::am_func V60Cpu::s_Op5CTable[32] =
{
	&V60Cpu::opCMPF,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::opMOVFS,
	&V60Cpu::opNEGFS,
	&V60Cpu::opABSFS,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,

	&V60Cpu::opSCLFS,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::opADDFS,
	&V60Cpu::opSUBFS,
	&V60Cpu::opMULFS,
	&V60Cpu::opDIVFS,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED,
	&V60Cpu::op5CUNHANDLED
};


uint32_t V60Cpu::op5F()
{
	m_instflags = OpRead8(PC + 1);
	return (this->*s_Op5FTable[m_instflags & 0x1F])();
}


uint32_t V60Cpu::op5C()
{
	m_instflags = OpRead8(PC + 1);
	return (this->*s_Op5CTable[m_instflags & 0x1F])();
}
