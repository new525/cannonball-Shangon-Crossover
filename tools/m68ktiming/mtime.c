/*
 * mtime: runs the 68000 timing test batches made by gen.py through the crossover's Musashi
 * (src/main/crossover/m68k) on a bus shaped like MAME's Hang-On sub CPU (19-bit address mask,
 * 16 KB of RAM at $7C000-$7FFFF), and prints the cycles Musashi charges for every instruction.
 *
 *   mtime valid                          one line per opcode word: "<hex> <1 if valid on a 68000>"
 *   mtime run <batches.bin> <out.txt> [ramdir]
 *       out.txt: "B <batch>" then "<pc> <cycles>" per instruction executed, then "E <status>";
 *       ramdir: writes <ramdir>/m_<batch>.ram (the 16 KB RAM after the batch) for checking
 *       against MAME's.
 *
 *   mtime irq <prog.bin> <start> <first irq> <period> <count> <out.tr> [max slice]
 *       interrupt latency: runs irqprog.py's program (Hang-On main CPU work RAM at $20C000) from
 *       cycle <start> and raises the level 4 autovector (held until acknowledged) at
 *       <first irq> + k * <period>, in time slices of random length up to [max slice] that end at
 *       each interrupt; out.tr lists "<cycle hex> <pc hex>:" per instruction, like MAME's trace.
 *
 * Batch file: records of [u32 magic 'MTB1'][u32 batch][u32 start pc][u32 end pc][16 KB RAM image],
 * big-endian. Every exception vector points at the handler stub at HND (see gen.py).
 * Accesses outside the test area are counted (MT_VERBOSE=1 lists them); the one expected is
 * JSR (A7), which pushes its return address over its own, already executed, opcode.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"

#define RAM_LO 0x7c000u
#define RAM_HI 0x80000u
#define HND    0x7ee80u
#define ABORT_FAULT 0x7eeaau  /* the abort routine's last instruction (gen.py) */
#define DATA_LO 0x7f000u      /* where test instructions may write */
#define SCAF_LO 0x7ee40u      /* scaffolding cells (continuation address, done flag) */
#define SCAF_HI 0x7ee60u

static unsigned char mem[0x80000];
static unsigned char* bigmem;   /* irq mode: 16 MB, 24-bit addresses */
static int irq_level;
static int violations;
static unsigned int first_violation, violation_pc, cur_pc;

static void violate(unsigned int a)
{
	if (getenv("MT_VERBOSE")) fprintf(stderr, "access %x at pc %x\n", a, cur_pc);
	if (!violations++) { first_violation = a; violation_pc = cur_pc; }
}

static unsigned int rd(unsigned int a, int size)
{
	unsigned int v = 0;
	int i;
	if (bigmem) {
		for (i = 0; i < size; i++) v = (v << 8) | bigmem[(a + i) & 0xffffff];
		return v;
	}
	a &= 0x7ffff;
	if (a < RAM_LO && a >= 0x400) violate(a | 0x80000000u);
	for (i = 0; i < size; i++) v = (v << 8) | mem[(a + i) & 0x7ffff];
	return v;
}

unsigned int xo_bus_read(int size, unsigned int address, int space)
{
	(void)space;
	return rd(address, size);
}

void xo_bus_write(int size, unsigned int address, unsigned int value)
{
	unsigned int a = address & 0x7ffff;
	int i;
	if (bigmem) {
		for (i = size - 1; i >= 0; i--, value >>= 8) bigmem[(address + i) & 0xffffff] = (unsigned char)value;
		return;
	}
	if (!(a >= DATA_LO || (a >= SCAF_LO && a < SCAF_HI))) violate(a);
	for (i = size - 1; i >= 0; i--, value >>= 8) mem[(a + i) & 0x7ffff] = (unsigned char)value;
}

int xo_m68k_int_ack(int level)
{
	(void)level;
	if (irq_level) { irq_level = 0; m68k_set_irq(0); }   /* HOLD_LINE */
	return M68K_INT_ACK_AUTOVECTOR;
}
static FILE* irq_trace;
void xo_m68k_reset_out(void) {}
void xo_m68k_instr_hook(unsigned int pc)
{
	cur_pc = pc;
	if (irq_trace) fprintf(irq_trace, "%llX %06X:\n", m68k_get_total_cycles(), pc & 0xffffff);
}

static int run_irq(int argc, char** argv)
{
	FILE* f = fopen(argv[2], "rb");
	unsigned long long start = strtoull(argv[3], 0, 0), t0 = strtoull(argv[4], 0, 0);
	unsigned long long period = strtoull(argv[5], 0, 0);
	int count = atoi(argv[6]), maxslice = argc > 8 ? atoi(argv[8]) : 2000, k;
	unsigned int seed = 12345;
	bigmem = calloc(1, 0x1000000);
	if (!f || !bigmem || fread(bigmem + 0x20c000, 1, 0x4000, f) != 0x4000) { fprintf(stderr, "prog\n"); return 1; }
	fclose(f);
	irq_trace = fopen(argv[7], "w");
	/* reset vectors, and the level 4 autovector to the handler */
	bigmem[1] = 0x20; bigmem[2] = 0xff; bigmem[5] = 0x20; bigmem[6] = 0xc0;
	bigmem[28*4+1] = 0x20; bigmem[28*4+2] = 0xc8;
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	{ FILE* t = irq_trace; irq_trace = 0; m68k_execute(1); irq_trace = t; }   /* the reset sequence */
	m68k_set_reg(M68K_REG_SR, 0x2700);
	m68k_set_reg(M68K_REG_PC, 0x20c000);
	m68k_set_total_cycles(start);
	for (k = 0; k < count; k++) {
		unsigned long long t = t0 + k * period;
		while (m68k_get_total_cycles() < t) {
			unsigned long long left = t - m68k_get_total_cycles();
			seed = seed * 1103515245u + 12345u;
			int n = 1 + (int)((seed >> 8) % (unsigned)maxslice);
			m68k_execute(n < (long long)left ? n : (int)left);
		}
		irq_level = 4;
		m68k_set_irq_ago(4, (int)(m68k_get_total_cycles() - t));
	}
	fclose(irq_trace);
	return 0;
}

static unsigned int be32(const unsigned char* p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int main(int argc, char** argv)
{
	if (argc >= 2 && !strcmp(argv[1], "valid")) {
		unsigned int op;
		for (op = 0; op < 0x10000; op++)
			printf("%04x %d\n", op, m68k_is_valid_instruction(op, M68K_CPU_TYPE_68000) ? 1 : 0);
		return 0;
	}
	if (argc >= 8 && !strcmp(argv[1], "irq"))
		return run_irq(argc, argv);
	if (argc < 4 || strcmp(argv[1], "run")) {
		fprintf(stderr, "usage: mtime valid | mtime run batches.bin out.txt [ramdir] | mtime irq prog start t0 period count out [slice]\n");
		return 2;
	}
	FILE* in = fopen(argv[2], "rb");
	FILE* out = fopen(argv[3], "w");
	const char* ramdir = argc > 4 ? argv[4] : NULL;
	if (!in || !out) { perror("open"); return 1; }
	unsigned int v;
	memset(mem, 0, sizeof mem);
	/* reset vectors, then every other vector to the handler stub */
	mem[0] = 0x00; mem[1] = 0x07; mem[2] = 0xfc; mem[3] = 0x00;
	mem[4] = 0x00; mem[5] = 0x07; mem[6] = 0xc0; mem[7] = 0x00;
	for (v = 2; v < 256; v++) { mem[v*4] = 0; mem[v*4+1] = HND >> 16; mem[v*4+2] = (HND >> 8) & 0xff; mem[v*4+3] = HND & 0xff; }
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	m68k_execute(1);   /* eat the reset cycles */

	unsigned char hdr[16];
	while (fread(hdr, 1, 16, in) == 16) {
		unsigned int batch = be32(hdr + 4), start = be32(hdr + 8), end = be32(hdr + 12);
		if (be32(hdr) != 0x4d544231) { fprintf(stderr, "bad magic\n"); return 1; }
		if (fread(mem + RAM_LO, 1, RAM_HI - RAM_LO, in) != RAM_HI - RAM_LO) { fprintf(stderr, "short\n"); return 1; }
		m68k_set_reg(M68K_REG_SR, 0x2700);
		m68k_set_reg(M68K_REG_PC, start);
		violations = 0;
		fprintf(out, "B %u\n", batch);
		int n = 0, status = 0;
		for (;;) {
			unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC) & 0x7ffff;
			if (pc == end) break;
			if (pc == ABORT_FAULT) { status = 3; break; }
			if (++n > 200000) { status = 1; break; }
			int stop = rd(pc, 2) == 0x4e72;
			int c = m68k_execute(1);
			fprintf(out, "%x %d\n", pc, c);
			if (stop) { status = 2; break; }
		}
		fprintf(out, "E %d viol %d %x pc %x\n", status, violations, first_violation, violation_pc);
		if (violations) fprintf(stderr, "batch %u: %d accesses outside the test area, first %x by the instruction at %x\n", batch, violations, first_violation, violation_pc);
		if (ramdir) {
			char fn[512];
			snprintf(fn, sizeof fn, "%s/m_%u.ram", ramdir, batch);
			FILE* r = fopen(fn, "wb");
			if (r) { fwrite(mem + RAM_LO, 1, RAM_HI - RAM_LO, r); fclose(r); }
		}
	}
	fclose(out);
	return 0;
}
