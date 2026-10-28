/***************************************************************************
    Musashi callbacks for the emulated boards. See xobus.hpp.
***************************************************************************/
#include "crossover/xobus.hpp"

XoBus* xo_bus = nullptr;

extern "C" unsigned int xo_bus_read(int size, unsigned int address, int space)
{
    return xo_bus ? xo_bus->bus_read(address, size, space) : 0xffffffffu;
}
extern "C" void xo_bus_write(int size, unsigned int address, unsigned int value)
{
    if (xo_bus) xo_bus->bus_write(address, size, value);
}
extern "C" int  xo_m68k_int_ack(int level)          { return xo_bus ? xo_bus->int_ack(level) : -1; }
extern "C" void xo_m68k_reset_out(void)             { if (xo_bus) xo_bus->reset_out(); }
extern "C" void xo_m68k_instr_hook(unsigned int pc) { if (xo_bus) xo_bus->instr_hook(pc); }
