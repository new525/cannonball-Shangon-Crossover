/*
 * Super Hang-On input tasks (main CPU), reconstructed from the decrypted program.
 * Verified against MAME 0.264 with the raw ADC byte logged at each read instruction
 * ($5F76, $6044, $60B8) and the results at each task's exit: steering 1,403/1,403,
 * throttle 1,404/1,404, brake 1,404/1,404 frames.
 */
#include "sho_internal.h"

void sho_steering_task(sho_mem *m, uint32_t a5, uint8_t raw)           /* $005F4C */
{
    uint16_t d0 = raw, acc;
    int16_t a;
    int32_t q;
    if (d0 > 0xC8) d0 = 0xC8;
    else if (d0 < 0x38) d0 = 0x38;
    if (d0 >= 0x6C && d0 <= 0x94) d0 = 0x80;                            /* dead zone */
    acc = (uint16_t)(rw(m, a5 + 0xA) + (uint16_t)(d0 - rw(m, a5 + 0x8)));
    ww(m, a5 + 0x8, d0);
    ww(m, a5 + 0xA, acc);
    a = s16(acc);
    if (a <= 2 && a >= -2) return;                                       /* jitter: keep $260068 */
    ww(m, a5 + 0xA, 0);
    q = ((int32_t)d0 - 0x80) * 256 / 0x90;                               /* asl.l #8 ; divs.w #$90 */
    if (q > 0x7F) q = 0x7F; else if (q < -0x7F) q = -0x7F;
    ww(m, 0x260068u, (uint16_t)(-q));
    if (rw(m, 0x060408u) == 0) {                                         /* $5FEA: not in a game */
        int16_t s = s16(rw(m, 0x260068u));
        if (s > 0x60 || s < -0x60) wb(m, 0x060500u, 1);
    }
}

void sho_throttle_task(sho_mem *m, uint8_t raw)                          /* $006014 */
{
    uint16_t d0 = raw;
    if (d0 > 0x93) d0 = 0x93; else if (d0 < 8) d0 = 8;
    d0 = (uint16_t)(((uint16_t)((d0 - 8) << 5)) * 5u);
    d0 = (uint16_t)(d0 / 0x8C);
    if (d0 > 0x7F) d0 = 0x7F;
    ww(m, 0x26006Cu, d0);
}

void sho_brake_task(sho_mem *m, uint8_t raw)                             /* $006086 */
{
    uint16_t d0 = raw > 0x9D ? 0x9D : raw;
    ww(m, 0x260070u, (uint16_t)((d0 << 8) / 0x9E));
}
