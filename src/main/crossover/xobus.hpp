/***************************************************************************
    The Musashi 68000 core has one global state; the emulated boards (shboard, hoboard) take
    turns with it. The board running a frame installs itself as xo_bus, and Musashi's memory,
    interrupt-acknowledge, RESET and instruction-hook callbacks go to it.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

class XoBus
{
public:
    virtual ~XoBus() {}
    // size 1/2/4; space 0 = data, 1 = program (instructions, immediates), 2 = disassembler
    virtual uint32_t bus_read(uint32_t address, int size, int space) = 0;
    virtual void     bus_write(uint32_t address, int size, uint32_t value) = 0;
    virtual int      int_ack(int level) = 0;
    virtual void     reset_out() = 0;
    virtual void     instr_hook(uint32_t pc) = 0;
};

extern XoBus* xo_bus;
