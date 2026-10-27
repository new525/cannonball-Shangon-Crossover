/*
 * Musashi memory and callback glue for the crossover's emulated board (crossover/shboard.cpp).
 * Space: 0 = data, 1 = program (instruction stream, immediates, PC-relative operands): the
 * FD1089B-encrypted main CPU decrypts the two with different keys.
 */
#include "m68k.h"

extern unsigned int xo_bus_read(int size, unsigned int address, int space);
extern void         xo_bus_write(int size, unsigned int address, unsigned int value);

unsigned int m68k_read_memory_8(unsigned int a)       { return xo_bus_read(1, a, 0); }
unsigned int m68k_read_memory_16(unsigned int a)      { return xo_bus_read(2, a, 0); }
unsigned int m68k_read_memory_32(unsigned int a)      { return xo_bus_read(4, a, 0); }
unsigned int m68k_read_immediate_16(unsigned int a)   { return xo_bus_read(2, a, 1); }
unsigned int m68k_read_immediate_32(unsigned int a)   { return xo_bus_read(4, a, 1); }
unsigned int m68k_read_pcrelative_8(unsigned int a)   { return xo_bus_read(1, a, 1); }
unsigned int m68k_read_pcrelative_16(unsigned int a)  { return xo_bus_read(2, a, 1); }
unsigned int m68k_read_pcrelative_32(unsigned int a)  { return xo_bus_read(4, a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a){ return xo_bus_read(2, a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a){ return xo_bus_read(4, a, 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { xo_bus_write(1, a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { xo_bus_write(2, a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { xo_bus_write(4, a, v); }
