/*
 * Musashi configuration for the crossover's emulated Sega OutRun-hardware board (Super Hang-On).
 * Musashi by Karl Stenerud (MIT licence, see readme.txt); upstream commit in VERSION.txt.
 * This file replaces upstream's m68kconf.h. m68k.h, m68k_in.c, m68kcpu.c, m68kcpu.h and m68kmake.c
 * carry the crossover's 68000 timing corrections (instruction cycles and interrupt timing, marked
 * "crossover"; measured against MAME 0.264's 68000 with tools/m68ktiming), m68kops.c/.h are
 * generated from them with m68kmake, and m68kipl.h by tools/m68ktiming/iplscan.py from MAME's
 * 68000 microcode.
 */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2

#define M68K_COMPILE_FOR_MAME      M68K_OPT_OFF

/* Only the 68000 is needed */
#define M68K_EMULATE_010            M68K_OPT_OFF
#define M68K_EMULATE_EC020          M68K_OPT_OFF
#define M68K_EMULATE_020            M68K_OPT_OFF
#define M68K_EMULATE_030            M68K_OPT_OFF
#define M68K_EMULATE_040            M68K_OPT_OFF

/* The FD1089B decrypts program-space fetches (instruction words, extension words, immediates,
 * PC-relative operands) with a different key from data reads */
#define M68K_SEPARATE_READS         M68K_OPT_ON

#define M68K_SIMULATE_PD_WRITES     M68K_OPT_OFF

/* Interrupt lines are level-held by the board and cleared by it, not by the CPU */
#define M68K_EMULATE_INT_ACK        M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A)    xo_m68k_int_ack(A)

#define M68K_EMULATE_BKPT_ACK       M68K_OPT_OFF
#define M68K_BKPT_ACK_CALLBACK()    0
#define M68K_EMULATE_TRACE          M68K_OPT_OFF

/* The main CPU's RESET instruction resets the second CPU (MAME m68k_reset_callback) */
#define M68K_EMULATE_RESET          M68K_OPT_SPECIFY_HANDLER
#define M68K_RESET_CALLBACK()       xo_m68k_reset_out()

#define M68K_CMPILD_HAS_CALLBACK    M68K_OPT_OFF
#define M68K_CMPILD_CALLBACK(v,r)
#define M68K_RTE_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_RTE_CALLBACK()
#define M68K_TAS_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_TAS_CALLBACK()         1
#define M68K_ILLG_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_ILLG_CALLBACK(opcode)  0
#define M68K_TRAP_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_TRAP_CALLBACK(trap)

#define M68K_EMULATE_FC             M68K_OPT_OFF
#define M68K_SET_FC_CALLBACK(A)
#define M68K_MONITOR_PC             M68K_OPT_OFF
#define M68K_SET_PC_CALLBACK(A)

/* Optional per-instruction hook for tracing (a cheap null check when unused) */
#define M68K_INSTRUCTION_HOOK       M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) xo_m68k_instr_hook(pc)

#define M68K_EMULATE_PREFETCH       M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  M68K_OPT_OFF

#define M68K_LOG_ENABLE             M68K_OPT_OFF
#define M68K_LOG_1010_1111          M68K_OPT_OFF
#define M68K_LOG_FILEHANDLE         stderr

#define M68K_EMULATE_PMMU           M68K_OPT_OFF
#define M68K_USE_64_BIT             M68K_OPT_ON

#ifdef __cplusplus
extern "C" {
#endif
int  xo_m68k_int_ack(int level);
void xo_m68k_reset_out(void);
void xo_m68k_instr_hook(unsigned int pc);
#ifdef __cplusplus
}
#endif

#endif /* M68KCONF__HEADER */
