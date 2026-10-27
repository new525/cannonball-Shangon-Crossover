/*
 * Super Hang-On sound task (slot 1, $005B98), C reconstruction: the player's part of the 8-byte
 * sound packet ($060641-$060643: mode / supercharger, engine pitch, throttle) and the effect
 * commands for tyre squeal, knockback and off-road ($8A-$8D).
 *
 * Not reconstructed: the distance score it also awards (bsr $26FA with $C308[speed / 16]) - the
 * crossover keeps the host game's scoring - and the mode-4 branch ($60406), unused in a race.
 * Verified against MAME 0.264 (verify_sound.c).
 */
#include "sho_internal.h"
#include "sho_sound.h"

static void sound(sho_mem *m, uint8_t code) { if (m->sound_fn) m->sound_fn(m->sound_ctx, code); }

void sho_sound_task(sho_mem *m, uint32_t a5)
{
    if (!rw(m, 0x060408)) { ww(m, 0x2600EA, 0); ww(m, 0x2600EC, 0); }      /* 5B98 */
    uint16_t mode = rw(m, 0x260000);
    if (mode == 4) return;                                                  /* 5BAE (not modelled) */
    if (mode) {                                                             /* 5BCC */
        uint8_t d0 = (uint8_t) mode;
        if (d0 == 2 && !rw(m, 0x260094))
            d0 = rb(m, 0x060412) ? 0x80 : 0x00;                             /* supercharger */
        wb(m, SHO_PKT + 1, d0);
        uint32_t p = 0;
        if (!rw(m, 0x2600EA)) {
            p = (uint32_t)(int32_t) s16(rw(m, 0x260078)) << 8;
            p = divu_w(p, rw(m, 0x85A6));
            if (s16((uint16_t) p) > 0xFF) p = (p & 0xFFFF0000u) | 0xFF;
        }
        wb(m, SHO_PKT + 2, (uint8_t) p);                                    /* engine pitch */
        wb(m, SHO_PKT + 3, 0);
        if (!rw(m, 0x2600EA)) wb(m, SHO_PKT + 3, rb(m, 0x26006D));          /* throttle */
    }
    if (rw(m, 0x260000) != 2) return;                                       /* 5C3A */

    /* (5C46-5C6A: distance score, not modelled) */

    /* knockback squeal: 5C6E */
    uint16_t knock = rw(m, 0x260088);
    if (!rw(m, a5 + 0xA)) {
        if (knock) { sound(m, 0x8A); ww(m, 0x2600E6, 0xFFFF); }
    } else if (!knock) { sound(m, 0x8B); ww(m, 0x2600E6, 0); }
    ww(m, a5 + 0xA, knock);

    /* cornering squeal: 5CAC */
    int squeal = 0;
    uint16_t curve = rw(m, 0x2600AC);
    if (curve != 1) {
        int16_t st = s16(rw(m, 0x260068));
        int16_t ast = st < 0 ? (int16_t)-st : st;
        if (ast >= 0x70) {
            int16_t x = s16(rw(m, 0x260022)), px = s16(rw(m, a5 + 0xC));
            if (x < px) { if (curve != 2 && st < 0) squeal = 1; }
            else if (x > px) { if (curve != 3 && st >= 0) squeal = 1; }
        }
    }
    ww(m, 0x2600E4, squeal ? 0xFFFF : 0);
    ww(m, 0x2600E8, (uint16_t)(rw(m, 0x260022) - rw(m, a5 + 0xC)));
    ww(m, a5 + 0xC, rw(m, 0x260022));
    if (!rw(m, 0x260088)) {                                                 /* 5D2A */
        if (!rw(m, a5 + 0xE)) {
            if (rw(m, 0x2600E4)) { sound(m, 0x8A); ww(m, 0x2600E6, 0xFFFF); }
        } else if (!rw(m, 0x2600E4)) { sound(m, 0x8B); ww(m, 0x2600E6, 0); }
    }
    ww(m, a5 + 0xE, rw(m, 0x2600E4));

    /* off-road rumble: 5D70 */
    uint16_t off = rw(m, 0x260098);
    if (!rw(m, a5 + 0x8)) { if (off) sound(m, 0x8C); }
    else if (!off) sound(m, 0x8D);
    ww(m, a5 + 0x8, off);
}
