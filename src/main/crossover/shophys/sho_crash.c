/*
 * Super Hang-On crash sequence and sprite-entry builder, C reconstruction.
 * Source: main-CPU program of shangon1, decrypted (opcode view for code, data view for tables).
 *
 *   $009FE6  crash task (slot 3): start (sub_0A22E), scripted steps (sub_0A11C), projection
 *            (sub_0A1A6), outward slide (sub_0A1F4) and clamp (sub_0A20A), walk back, end ($A084)
 *   $00A2EC  shadow task (slots 4 and 6): follows the object in the slot before it
 *   $00A33A  rider task (slot 5): its own script (sub_0A3A6)
 *   $002994  sprite renderer: object slot -> 12-byte entry at $61400 + index*12
 *
 * Verified against MAME 0.264 captures of all four speed classes (see verify_crash.c).
 * The per-priority draw list (sub_07A72) is not modelled: an object is drawn every frame its
 * task runs with $260094 set, which is exactly when sub_07A72 is called.
 */
#include "sho_internal.h"
#include <string.h>
#include "sho_crash.h"

static void sound(sho_mem *m, uint8_t code)                                  /* bsr $28B0 */
{
    if (m->sound_fn) m->sound_fn(m->sound_ctx, code);
}

/* sub_0A11C: load the next script step (16-byte entries) */
static void crash_step(sho_mem *m, uint32_t a5)
{
    uint32_t a0 = rl(m, a5 + 0x26) + (uint32_t)rb(m, a5 + 0x22) * 0x10;
    int16_t  x  = s16(rw(m, SHO_V_X));
    wl(m, a5 + 0x1A, rl(m, a0));                                 /* definition */
    if (x >= 0) {                                                /* bmi skips the flag update */
        uint8_t f = (uint8_t)(rb(m, a0 + 0xA) ^ 0x80);
        wb(m, a5, (uint8_t)((rb(m, a5) & 0xFE) | f));
    }
    uint32_t d0 = rb(m, a0 + 6);
    ww(m, a5 + 0x14, (uint16_t)(d0 + 0x80));                     /* depth */
    d0 = divu_w(d0 * 0x6F, 0xBF);
    uint16_t z = (uint16_t)((d0 & 0xFFFF) + 0xC);
    if (s16(z) > 0x6F) z = 0x6F;
    ww(m, a5 + 0xE, z);                                          /* zoom */
    wb(m, a5 + 0x25, rb(m, a0 + 7));                             /* height above the road */
    uint16_t lat = rw(m, a0 + 8);
    if (x >= 0) lat = neg16(lat);
    ww(m, a5 + 0x1E, lat);                                       /* lateral, away from the side */
    if ((int16_t)rb(m, a5 + 0x22) < s16(rw(m, a5 + 0x2C))) {
        wb(m, a5 + 0x23, rb(m, a0 + 4));                         /* duration */
        wb(m, a5 + 0x22, (uint8_t)(rb(m, a5 + 0x22) + 1));
    }
}

/* sub_0A1A6: screen position from lateral, depth and height */
static void crash_project(sho_mem *m, uint32_t a5)
{
    uint16_t d1 = (uint16_t)(rw(m, a5 + 0x14) - 0x80);
    uint32_t d0 = divs_w((uint32_t)((int32_t)s16(rw(m, a5 + 0x1E)) * s16(d1)), 0xE0);
    ww(m, a5 + 0xA, (uint16_t)(d0 + 0x160));
    ww(m, a5 + 0xC, sho_crash_road_y(m, d1));
    uint16_t y = rw(m, a5 + 0xC);
    uint32_t d2 = divs_w((uint32_t)((int32_t)s16(rw(m, a5 + 0x24)) * s16(d1)), 0xC0);
    ww(m, a5 + 0xC, (uint16_t)(y - (uint16_t)d2));
}

uint16_t sho_crash_road_y(sho_mem *m, uint16_t depth)       /* from the sub CPU's table at $261200 */
{
    uint16_t t = rw(m, SHO_V_ROADY + (uint32_t)(int32_t)s16((uint16_t)(depth * 2)));
    return (uint16_t)(0x80 - ((uint16_t)(s16(t) >> 4) - 0x60));
}

/* sub_0A1F4 + sub_0A20A: slide outward 8 per frame, clamped at the ROM limit ($85B6) */
static void crash_slide(sho_mem *m)
{
    uint16_t x = rw(m, SHO_V_X);
    ww(m, SHO_V_X, (uint16_t)(x + (s16(x) >= 0 ? 8 : 0xFFF8)));
    int16_t d0 = s16(rw(m, SHO_V_X)), lim = s16(rw(m, 0x85B6)), d1 = lim;
    if (d0 < 0) { d0 = (int16_t)-d0; d1 = (int16_t)-d1; }
    if (d0 >= lim) ww(m, SHO_V_X, (uint16_t)d1);
}

/* 8 * (depth - $80) / $BF: the screen shift of an object on the road when X moves by 8 */
static uint16_t walk_shift(sho_mem *m, uint32_t a5)
{
    uint32_t d0 = (uint32_t)8 * (uint16_t)(rw(m, a5 + 0x14) - 0x80);
    return (uint16_t)divu_w(d0, 0xBF);
}

/* sub_0A22E */
static void crash_start(sho_mem *m, uint32_t a5)
{
    ww(m, 0x2600D4, rw(m, SHO_V_X));
    sound(m, 0x8B);
    sound(m, 0x8D);
    ww(m, 0x26005C, 0);
    ww(m, 0x26005E, 0);
    wb(m, 0x063A40, rb(m, 0x063A40) & 0x7F);                     /* centrifugal task off */
    wb(m, 0x064080, rb(m, 0x064080) & 0x7F);                     /* bike task off */
    int16_t spd = s16(rw(m, SHO_V_SPEED));
    uint16_t cls = spd <= 0x73 ? 0 : spd <= 0x96 ? 1 : spd <= 0xB9 ? 2 : 3;
    ww(m, SHO_V_CRASH_CLASS, cls);
    uint32_t a0 = rl(m, 0xA4A8 + cls * 4);
    ww(m, a5 + 0x2C, rw(m, a0));
    wl(m, a5 + 0x26, a0 + 2);
    wl(m, a5 + 0x1A, rl(m, a0 + 2));
    ww(m, SHO_V_CRASH, 0xFFFF);
}

/* sub_0A2B8: sounds at given steps of each class's script ($A490: 3 x (step, code)) */
static void crash_sounds(sho_mem *m, uint32_t a5)
{
    uint32_t a0 = 0xA490 + rw(m, SHO_V_CRASH_CLASS) * 6;
    uint8_t step = rb(m, a5 + 0x22);
    for (int k = 0; k < 3; k++, a0 += 2)
        if (rb(m, a0) == step) { sound(m, rb(m, a0 + 1)); return; }
}

/* $A084: crash over */
static void crash_end(sho_mem *m, uint32_t a5)
{
    wb(m, a5 + 0x22, 0);
    wb(m, a5 + 0xA2, 0);                                         /* the rider's step counter */
    ww(m, SHO_V_CRASH, 0);
    wb(m, 0x063A40, rb(m, 0x063A40) | 0x80);
    wb(m, 0x064080, rb(m, 0x064080) | 0x80);
    ww(m, a5 + 0x20, rw(m, SHO_V_CRASH_COUNT));
    ww(m, a5 + 0xA, 0x160);
    ww(m, a5 + 0xC, 0xE0);
    ww(m, a5 + 0xE, 0x6F);
    wl(m, a5 + 0x10, 0);
    ww(m, a5 + 0x14, 0);
    wl(m, a5 + 0x22, 0);
    ww(m, a5 + 0x1E, 0);
    for (uint32_t o = 0x40; o <= 0xC0; o += 0x40) {
        ww(m, a5 + o + 0xA, 0x160);
        ww(m, a5 + o + 0xC, 0xE0);
        ww(m, a5 + o + 0x1E, 0);
    }
    ww(m, SHO_V_KNOCK, 0);
}

int sho_crash_task(sho_mem *m, uint32_t a5)                                  /* $009FE6 */
{
    if (rw(m, SHO_V_CRASH_COUNT) == rw(m, a5 + 0x20)) return 0;
    if ((int8_t)rb(m, a5 + 0x22) <= 0) crash_start(m, a5);
    crash_sounds(m, a5);
    if ((int8_t)rw(m, a5 + 0x2C) > (int8_t)rb(m, a5 + 0x22)) {   /* script still running */
        crash_slide(m);
    } else if ((int8_t)rb(m, a5 + 0x23) < 0) {                   /* script done: walk back */
        int16_t x = s16(rw(m, SHO_V_X));
        if (x < 0) {
            ww(m, SHO_V_X, (uint16_t)(x + 8));
            ww(m, a5 + 0xA, (uint16_t)(rw(m, a5 + 0xA) + walk_shift(m, a5)));
            if (s16(rw(m, SHO_V_X)) >= 0) { crash_end(m, a5); return 0; }
        } else {
            ww(m, SHO_V_X, (uint16_t)(x - 8));
            ww(m, a5 + 0xA, (uint16_t)(rw(m, a5 + 0xA) - walk_shift(m, a5)));
            if (s16(rw(m, SHO_V_X)) < 0) { crash_end(m, a5); return 0; }
        }
        return 1;
    }
    uint8_t t = (uint8_t)(rb(m, a5 + 0x23) - 1);
    wb(m, a5 + 0x23, t);
    if ((int8_t)t < 0) crash_step(m, a5);
    crash_project(m, a5);
    return 1;
}

int sho_crash_shadow_task(sho_mem *m, uint32_t a5)                           /* $00A2EC */
{
    if (!rw(m, SHO_V_CRASH)) return 0;
    uint32_t a3 = a5 - 0x40;
    ww(m, a5 + 0xA, rw(m, a3 + 0xA));
    uint16_t d0 = rw(m, a3 + 0x14);
    ww(m, a5 + 0x14, (uint16_t)(d0 - 1));
    ww(m, a5 + 0xC, sho_crash_road_y(m, (uint16_t)(d0 - 0x80)));
    ww(m, a5 + 0xE, rw(m, a3 + 0xE));
    return 1;
}

int sho_crash_rider_task(sho_mem *m, uint32_t a5)                            /* $00A33A */
{
    if (!rw(m, SHO_V_CRASH)) return 0;
    if ((int8_t)rb(m, a5 + 0x22) <= 0) {                         /* sub_0A3A6 */
        uint32_t a0 = rl(m, 0xA4B8 + rw(m, SHO_V_CRASH_CLASS) * 4);
        ww(m, a5 + 0x2C, rw(m, a0));
        wl(m, a5 + 0x26, a0 + 2);
        wl(m, a5 + 0x1A, rl(m, a0 + 2));
        wb(m, a5 + 0x23, 0);
        ww(m, a5 + 0xE, 0x6F);
        ww(m, a5 + 0xC, 0xDF);
        ww(m, a5 + 0xA, 0x160);
    }
    if ((int8_t)rw(m, a5 + 0x2C) <= (int8_t)rb(m, a5 + 0x22) && (int8_t)rb(m, a5 + 0x23) < 0) {
        uint16_t d = walk_shift(m, a5);                          /* walks with the road */
        if (s16(rw(m, SHO_V_X)) < 0) ww(m, a5 + 0xA, (uint16_t)(rw(m, a5 + 0xA) + d));
        else                         ww(m, a5 + 0xA, (uint16_t)(rw(m, a5 + 0xA) - d));
        return 1;
    }
    uint8_t t = (uint8_t)(rb(m, a5 + 0x23) - 1);
    wb(m, a5 + 0x23, t);
    if ((int8_t)t < 0) crash_step(m, a5);
    crash_project(m, a5);
    return 1;
}

void sho_crash_frame(sho_mem *m, uint8_t drawn[4])
{
    drawn[0] = (uint8_t)sho_crash_task(m, SHO_A5_CRASH);
    drawn[1] = (uint8_t)sho_crash_shadow_task(m, SHO_A5_CRASH_SHADOW);
    drawn[2] = (uint8_t)sho_crash_rider_task(m, SHO_A5_RIDER);
    drawn[3] = (uint8_t)sho_crash_shadow_task(m, SHO_A5_RIDER_SHADOW);
}

/* ------------------------------------------------------------------------------------------
 * sub_02994: object slot -> sprite entry. Register-level, since stale register halves
 * (the remainder left by DIVU in sub_02C14) reach the top-clipping arithmetic.
 * ---------------------------------------------------------------------------------------- */
static uint32_t scale_2c14(sho_mem *m, uint32_t a5, uint32_t d0, uint32_t *d1out)
{
    d0 = (d0 & 0xFFFF) << 5;
    uint32_t d1 = (rb(m, a5 + 0x19) & 0x3E) >> 1;
    d0 = (d0 & 0xFFFF0000u) | ((d0 + d1) & 0xFFFF);
    d1 += 0x20;
    *d1out = d1;
    return divu_w(d0, (uint16_t)d1);
}

int sho_sprite_entry(sho_mem *m, uint32_t a5, uint8_t e[12])
{
    uint8_t f[0x14];                 /* frame -$14..-$01 */
    memset(f, 0, sizeof f);
    #define FB(o)  f[0x14 + (o)]
    #define FW(o, v) do { uint16_t v_ = (uint16_t)(v); f[0x14 + (o)] = (uint8_t)(v_ >> 8); f[0x15 + (o)] = (uint8_t)v_; } while (0)
    #define FR(o)  ((uint16_t)(f[0x14 + (o)] << 8 | f[0x15 + (o)]))
    uint32_t d0, d1, d2;
    int visible = 1;

    wb(m, a5, rb(m, a5) | 0x20);
    uint32_t a0 = 0xC904 + ((rw(m, a5 + 0xE) & 0x7F) << 1);
    wb(m, a5 + 0x19, rb(m, a0) & 0x3E);
    uint32_t a4 = rl(m, a5 + 0x1A) + rb(m, a0 + 1);

    d0 = scale_2c14(m, a5, rw(m, a4), &d1);           FW(-4, d0);   /* scaled height */
    d0 = (d0 & 0xFFFF0000u) | (uint16_t)(rw(m, a4 + 2) * 4);
    d0 = scale_2c14(m, a5, d0, &d1);                   FW(-2, d0);   /* scaled width (pixels) */
    d0 = (d0 & 0xFFFF0000u) | (uint16_t)(FR(-2) >> 1);
    d0 = (d0 & 0xFFFF0000u) | (uint16_t)(rw(m, a5 + 0xA) - (uint16_t)d0);   /* sub; neg */
    FW(-8, d0);                                                       /* left */
    d0 = (d0 & 0xFFFF0000u) | (uint16_t)(rw(m, a5 + 0xC) - FR(-4));
    FW(-6, d0);                                                       /* top */

    int16_t fr = (int8_t)rb(m, a5 + 0x18);
    d0 = (d0 & 0xFFFF0000u) | (uint16_t)(fr * 4);
    d1 = rl(m, a4 + 6 + (uint32_t)(int32_t)(int16_t)(fr * 4));
    if (rb(m, a5) & 1) {
        d0 = (d0 & 0xFFFF0000u) | (uint16_t)(rw(m, a4 + 2) - 1);
        d1 = (d1 & 0xFFFF0000u) | (uint16_t)(d1 + d0);
    }
    if (rb(m, a5) & 2) d1 = (d1 & 0xFFFF0000u) | (uint16_t)(d1 + rw(m, a4 + 4));
    FW(-0xE, d1);                                                     /* sprite address */
    d0 = (d0 & 0xFFFF0000u) | rw(m, a4 + 2);
    if (rb(m, a5) & 2) d0 = (d0 & 0xFFFF0000u) | neg16((uint16_t)d0);
    d0 = (d0 & 0xFFFF0000u) | ((uint16_t)d0 & 0xFF);
    FW(-0x10, d0);                                                    /* pitch */

    d0 = (d0 & 0xFFFF0000u) | FR(-8);
    if ((uint16_t)d0 >= 0x200) visible = 0;
    else {
        d2 = FR(-2);
        d1 = (d1 & 0xFFFF0000u) | (uint16_t)(d0 + d2);
        if ((uint16_t)d1 <= 0xC0) visible = 0;
        else FW(-0x12, d0);
    }
    if (visible) {
        uint32_t a2 = 0x260448 + (uint32_t)(int32_t)(int16_t)((int8_t)rb(m, 0x2607FC) * 2);
        d1 = (d1 & 0xFFFF0000u) | (uint16_t)(rw(m, a5 + 0x14) - 0x80);
        int horizon = s16(d1) < s16(rw(m, a2));
        uint16_t limit = horizon ? rw(m, a2 + 2) : 0xE0;
        d0 = (d0 & 0xFFFF0000u) | FR(-6);
        if (s16(d0) < 0) {
            FB(-0x13) = 0;
            d0 = (d0 & 0xFFFF0000u) | neg16((uint16_t)d0);
            d0 <<= 7;
            d1 = 0x0080;
            d1 = (d1 & 0xFF00) | (uint8_t)(0x80 - rb(m, a5 + 0x19));
            d0 = divu_w(d0, (uint16_t)d1);
            d0 = (uint32_t)(uint16_t)d0 * rw(m, a4 + 2);                /* sub_02B80 */
            if (rb(m, a5) & 2) d0 = (d0 & 0xFFFF0000u) | neg16((uint16_t)d0);
            FW(-0xE, FR(-0xE) + (uint16_t)d0);
        } else if (s16(d0) >= s16(limit)) visible = 0;
        else FB(-0x13) = (uint8_t)d0;
        if (visible) {
            d0 = (d0 & 0xFFFF0000u) | (uint16_t)(FR(-4) - 1 + FR(-6));
            if (s16(d0) < 0) visible = 0;
            else {
                FB(-0x14) = (uint8_t)d0;
                if (horizon) { if (s16(d0) - s16(rw(m, a2 + 2)) > 0) FB(-0x14) = rb(m, a2 + 3); }
                else if (s16(d0) - 0xDF > 0) FB(-0x14) = 0xDF;
            }
        }
    }
    if (visible) {
        FB(-0xB) = (uint8_t)(rb(m, a5 + 0x16) | (((rb(m, a5 + 0x17) & 3) << 6)));
        uint16_t h = (rb(m, a5 + 0x19) & 0x3E) >> 1;
        FW(-0xA, h | h << 5);
        FW(-0xE, FR(-0xE) - FR(-0x10));
    } else {
        wb(m, a5, rb(m, a5) & ~0x20);
        FW(-0x14, 0x00EF);
    }
    /* sub_02B96 */
    FW(-0x12, FR(-0x12) & 0xFFF);
    FB(-0xC) = rw(m, a4 + 6) & 0xF;
    if (rb(m, a5) & 1) FB(-0x10) |= 1;
    memcpy(e, f, 12);
    #undef FB
    #undef FW
    #undef FR
    return visible;
}
