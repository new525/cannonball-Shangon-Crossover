/* Internal helpers: big-endian access at original addresses, 68000 arithmetic. */
#ifndef SHO_INTERNAL_H
#define SHO_INTERNAL_H
#include "shophys.h"
#include <stdio.h>
#include <stdlib.h>

static inline uint8_t *sho_loc(sho_mem *m, uint32_t a) {
    if (a >= 0x260000u && a < 0x260000u + m->shared_len) return m->shared + (a - 0x260000u);
    if (a >= 0x060000u && a < 0x068000u) return m->work + (a - 0x060000u);
    if (a < m->rom_len) return (uint8_t *)m->rom + a;     /* read-only by convention */
    fprintf(stderr, "shophys: unmapped address %06X\n", (unsigned)a);
    abort();
}
static inline uint8_t  rb(sho_mem *m, uint32_t a) { return *sho_loc(m, a); }
static inline uint16_t rw(sho_mem *m, uint32_t a) { uint8_t *p = sho_loc(m, a); return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t rl(sho_mem *m, uint32_t a) { return (uint32_t)rw(m, a) << 16 | rw(m, a + 2); }
static inline void wb(sho_mem *m, uint32_t a, uint8_t v)  { *sho_loc(m, a) = v; }
static inline void ww(sho_mem *m, uint32_t a, uint16_t v) { uint8_t *p = sho_loc(m, a); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wl(sho_mem *m, uint32_t a, uint32_t v) { ww(m, a, (uint16_t)(v >> 16)); ww(m, a + 2, (uint16_t)v); }

static inline int16_t s16(uint32_t v) { return (int16_t)(uint16_t)v; }
static inline uint16_t neg16(uint16_t v) { return (uint16_t)(0u - v); }

/* DIVU.W: Dn(32) / src(16) -> rem:quot; on overflow Dn is unchanged */
static inline uint32_t divu_w(uint32_t dn, uint16_t src) {
    uint32_t q = dn / src, r = dn % src;
    if (q > 0xFFFFu) return dn;
    return (r & 0xFFFFu) << 16 | q;
}
/* DIVS.W: signed, quotient truncates toward zero, remainder has the dividend's sign */
static inline uint32_t divs_w(uint32_t dn, uint16_t src) {
    int32_t a = (int32_t)dn, b = (int16_t)src;
    int32_t q = a / b, r = a % b;            /* C99 truncation toward zero, as on the 68000 */
    if (q < -32768 || q > 32767) return dn;
    return ((uint32_t)(uint16_t)r) << 16 | (uint16_t)q;
}
#endif
