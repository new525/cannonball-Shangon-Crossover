/*
 * Super Hang-On lateral handling (see shophys.h):
 *   slot 9  $0063CE  centrifugal push from the projected road curvature
 *   slot 34 $007AE6  player bike object: steering, knockback, off-road, hard-brake skid,
 *                    lean sprite selection, attract-mode demo driver
 * Comments give the original 68000 address of each step.
 */
#include "sho_internal.h"

#define V_X          0x260022u  /* .w player lateral position; road edge +/-300 */
#define V_STEER      0x260068u  /* .w -127..127, + = right                      */
#define V_THROTTLE   0x26006Cu
#define V_BRAKE      0x260070u
#define V_SPEED      0x260078u  /* .l 16.16 km/h                                */
#define V_KNOCK      0x260088u  /* .w knockback timer (signed, counts to 0)     */
#define V_OFFROAD    0x260098u  /* .w 0xFFFF when |X| > 300                     */
#define V_HARDBRAKE  0x260054u
#define V_LEANFRAME  0x260056u  /* .b lean sprite frame 0..3                    */
#define V_LEANFLIP   0x260057u  /* .b bit 0 = leaning left                      */
#define V_SPR_X      0x260058u
#define V_SPR_Y      0x26005Au
#define V_MODE       0x260000u
#define V_RETIRE     0x2600ECu
#define V_STAGE_DONE 0x260010u  /* .w nonzero after the first stage (start-line damping off) */
#define V_TRACK_POS  0x260030u
#define V_CURVE_NOW  0x2600ACu
#define V_CURVE_NEXT 0x2600A4u
#define W_INGAME     0x060408u
#define W_SC_ACTIVE  0x060412u
#define W_RNG        0x060402u

#define ROM_XLIM     0x85B6u    /* 500 */
#define ROM_KNOCK    0x85BCu    /* 16  */
#define ROM_STEERDIV 0x85BEu    /* 128 */
#define ROM_SKID     0x85C8u    /* 8   */
#define ROM_MAXSPEED 0x85A6u
#define ROM_BRAKE_T1 0x85CEu
#define ROM_DEMO_BRK_A 0x85D0u
#define ROM_DEMO_BRK_B 0x85D2u
#define ROM_LEAN_TBL 0xCA46u    /* lean thresholds 20, 44, 82, 32767 */
#define ROM_SPR_LEAN 0xCA4Eu    /* sprite pointers by lean frame     */
#define ROM_SPR_KNOCK 0xCA76u
#define ROM_SPR_SKID 0xCA96u

static uint16_t asr16(uint16_t v, int n) { return (uint16_t)(s16(v) >> n); }

/* ------------------------------------------------------------------ slot 9 */
void sho_centrifugal_task(sho_mem *m, uint32_t a5)
{
    uint32_t a0 = (0x260800u + (uint32_t)(int32_t)s16(rw(m, 0x2607FEu))) & 0xFFFFFFu; /* 63CE */
    uint16_t d1 = 0xA6;
    uint32_t d0;
    int16_t step;
    uint16_t x, mag;
    if (rb(m, W_SC_ACTIVE)) d1 = (uint16_t)(d1 - 0x1A);                 /* 63DE */
    d0 = (uint16_t)(rw(m, a0 + d1) - rw(m, a0 + 0x1B8));                 /* 63EA */
    d0 = divs_w((uint32_t)(int32_t)s16(d0), 0x40);                       /* 63F4 */
    if ((d0 & 0xFFFFu) == 0) return;                                     /* 63F8 */
    step = 1;
    d0 = (uint32_t)((int32_t)s16(rw(m, V_SPEED)) * (int32_t)s16(d0));    /* 63FC muls.w */
    d0 = divs_w(d0, rw(m, a5 + 0x1E));                                   /* 6402 */
    if (d0 & 0x8000u) step = -1;                                         /* 6406 bpl on quotient */
    x = (uint16_t)(d0 + (uint32_t)(int32_t)step + rw(m, V_X));           /* 640A-640C */
    mag = (x & 0x8000u) ? neg16(x) : x;
    if (s16(mag) > s16(rw(m, ROM_XLIM))) return;                         /* 6418 */
    ww(m, V_X, x);                                                       /* 6420 */
}

/* ------------------------------------------------------------------ slot 34 helpers */
static uint32_t rng(sho_mem *m)                                          /* sub_024CA */
{
    uint32_t d1 = rl(m, W_RNG), d0, lo, hi;
    if (d1 == 0) d1 = 0x2A6D365Au;
    d0 = d1;
    d1 = d1 * 41u;
    lo = d1 & 0xFFFFu; hi = d1 >> 16;
    d0 = (d0 & 0xFFFF0000u) | ((lo + hi) & 0xFFFFu);
    d1 = ((d0 & 0xFFFFu) << 16) | lo;
    wl(m, W_RNG, d1);
    return d0;
}

static void sprite_register(sho_mem *m, uint32_t a5)                     /* sub_07A72 */
{
    uint32_t a0 = (0x61C00u + (uint32_t)(int32_t)s16((uint16_t)((rw(m, a5 + 0x14) - 0x80u) << 5))) & 0xFFFFFFu;
    uint16_t n = rw(m, a0);
    ww(m, a0, (uint16_t)(n + 1));
    ww(m, a0 + 2 + (uint32_t)(int32_t)s16((uint16_t)(n * 2u)), (uint16_t)(a5 - 0x63800u));
}

static void clamp_x(sho_mem *m)                                          /* sub_07F0E */
{
    uint16_t d1 = rw(m, ROM_XLIM), d0 = rw(m, V_X);
    if (d0 & 0x8000u) { d0 = neg16(d0); d1 = neg16(d1); }
    if (s16(d0) > s16(rw(m, ROM_XLIM))) ww(m, V_X, d1);
}

static void anim_timer(sho_mem *m, uint32_t a5)                          /* sub_07E5E */
{
    uint16_t t = (uint16_t)(rw(m, a5 + 0x22) - 1);
    ww(m, a5 + 0x22, t);
    if (t == 0) { ww(m, a5 + 0x22, rw(m, a5 + 0x20)); wb(m, a5 + 0x16, rb(m, a5 + 0x1F)); }
    else if (asr16(rw(m, a5 + 0x20), 1) == t) wb(m, a5 + 0x16, (uint8_t)(rb(m, a5 + 0x1F) + 1));
}

static void knockback(sho_mem *m)                                        /* sub_07E8A */
{
    uint16_t k = rw(m, V_KNOCK), d0 = 0xFFFF, d1 = rw(m, ROM_KNOCK);
    if (k == 0) return;
    if (k & 0x8000u) { d0 = 1; d1 = neg16(d1); }
    ww(m, V_KNOCK, (uint16_t)(k + d0));
    ww(m, V_X, (uint16_t)(rw(m, V_X) + d1));
    clamp_x(m);
}

static void steering(sho_mem *m, uint32_t a5)                            /* sub_07EB2 */
{
    int32_t d0 = s16((uint16_t)(rw(m, V_STEER) - rw(m, a5 + 0x34)));
    uint32_t v;
    if (d0 > 12) d0 = 12;
    else if (d0 <= -12) d0 = -12;                                        /* slew limit */
    ww(m, a5 + 0x34, (uint16_t)(rw(m, a5 + 0x34) + (uint16_t)d0));
    v = (uint32_t)(int32_t)s16(rw(m, a5 + 0x34));
    if (rw(m, V_OFFROAD) == 0 && s16(rw(m, V_SPEED)) <= 0x7F) {          /* < 128 km/h on road */
        v = (uint32_t)((int32_t)s16(v) * (int32_t)s16(rw(m, V_SPEED)));
        v = (uint32_t)((int32_t)v >> 7);
    }
    v <<= 4;
    v = divs_w(v, rw(m, ROM_STEERDIV));
    ww(m, V_X, (uint16_t)(rw(m, V_X) - (uint16_t)v));                   /* neg.w ; add.w */
    clamp_x(m);
}

static void offroad(sho_mem *m, uint32_t a5)                             /* sub_07F30 */
{
    uint16_t d1 = rw(m, V_X);
    int off;
    ww(m, V_OFFROAD, 0);
    if (m->offroad_fn) off = m->offroad_fn(m->offroad_ctx, (int16_t)d1);   /* crossover hook */
    else {
        if (d1 & 0x8000u) d1 = neg16(d1);
        off = s16(d1) > 0x12C;
    }
    if (off) {
        ww(m, V_OFFROAD, 0xFFFF);
        ww(m, a5 + 0x24, (uint16_t)(rw(m, a5 + 0x24) + 1));
    }
}

static void wobble(sho_mem *m, uint32_t a5)                              /* sub_07FA6 */
{
    if (rw(m, V_RETIRE)) return;
    if (rw(m, a5 + 0x24)) {
        uint16_t d0 = (uint16_t)rng(m), n = (uint16_t)(rw(m, a5 + 0x28) + 1);
        ww(m, a5 + 0x28, n);
        d0 &= 1;
        if (!(n & 2)) d0 = neg16(d0);
        ww(m, a5 + 0xA, (uint16_t)(rw(m, a5 + 0xA) + d0));
        if (n & 1) ww(m, a5 + 0xC, (uint16_t)(rw(m, a5 + 0xC) + ((n & 2) ? 0xFFFFu : 1u)));
        ww(m, a5 + 0x24, 0);
    } else {
        uint16_t a = rw(m, a5 + 0xA);
        ww(m, a5 + 0xC, 0xDF);
        if (a == 0x160) return;
        /* cmpi.w #$160 ; bmi : N flag of the 16-bit difference */
        ww(m, a5 + 0xA, (uint16_t)(a + ((((uint16_t)(a - 0x160)) & 0x8000u) ? 1u : 0xFFFFu)));
    }
}

static void skid_push(sho_mem *m, uint32_t a5)                           /* sub_07F52 */
{
    uint16_t d1 = rw(m, a5 + 0x3A), d0, lim;
    if (s16(d1) <= 0xF) return;
    d0 = rw(m, ROM_SKID);
    if (d1 == 0x1F || d1 == 0x1B || d1 == 0x17) d0 = (uint16_t)(d0 + 0xC);
    if (rb(m, a5) & 1) d0 = neg16(d0);
    d0 = (uint16_t)(d0 + rw(m, V_X));
    lim = rw(m, ROM_XLIM);
    if (s16(d0) > s16(lim)) d0 = lim;
    else if (s16(d0) <= s16(neg16(lim))) d0 = neg16(lim);
    ww(m, V_X, d0);
}

static void demo_driver(sho_mem *m, uint32_t a5)                         /* $7B06-$7CB0 */
{
    int16_t nxt = s16(rw(m, V_CURVE_NEXT));
    uint16_t cur = rw(m, V_CURVE_NOW), d0, d2, d3;
    int step = 0, have = 0;
    if (nxt > 1)       d0 = rw(m, V_CURVE_NEXT) == 3 ? 0xB4 : neg16(0xB4);
    else if (nxt == 1) d0 = cur == 2 ? 0xB4 : neg16(0xB4);
    else               d0 = cur == 3 ? 0xB4 : (cur == 2 ? neg16(0xB4) : 0);
    ww(m, a5 + 0x2E, d0);
    d2 = (uint16_t)(rw(m, a5 + 0x2E) - rw(m, V_X)); d3 = d2;               /* 7B6A */
    if (d2 & 0x8000u) d2 = neg16(d2);
    if (s16(d2) > 6) d2 = 6;
    if (!(d3 & 0x8000u)) {
        int16_t d1 = s16((uint16_t)(rw(m, V_X) - rw(m, a5 + 0x32)));
        if (!(d1 == 0 && d2 == 0)) {
            if (rw(m, W_INGAME) == 0 && rw(m, V_OFFROAD) != 0) { /* 7BA6: no change */ }
            else if (d1 < 1) { step = -1; have = 1; }
            else if (d1 > 1) { step = 1; have = 1; }
        }
    } else {
        int16_t d1 = s16((uint16_t)(rw(m, a5 + 0x32) - rw(m, V_X)));
        if (!(d1 == 0 && d2 == 0)) {
            if (d1 < 1) { step = 1; have = 1; }
            else if (d1 > 1) { step = -1; have = 1; }
        }
    }
    if (have) {
        int32_t v = step * (s16(d2) + 1) + s16(rw(m, a5 + 0x30));
        if (v > 127) v = 127; else if (v < -127) v = -127;
        ww(m, a5 + 0x30, (uint16_t)v);
    }
    ww(m, a5 + 0x32, rw(m, V_X));                                          /* 7BF8 */
    ww(m, V_STEER, rw(m, a5 + 0x30));
    ww(m, V_BRAKE, 0);                                                     /* 7C30 */
    if (rb(m, 0x2600BCu)) { wb(m, 0x2600BCu, 0); ww(m, V_BRAKE, rw(m, ROM_DEMO_BRK_B)); }
    if (rw(m, V_OFFROAD)) ww(m, V_BRAKE, rw(m, ROM_DEMO_BRK_B));
    if (rw(m, 0x2600A8u)) {
        uint16_t n = (uint16_t)(rw(m, a5 + 0x2A) + 1);
        ww(m, a5 + 0x2A, n);
        if (n == 1) {
            d0 = (uint16_t)(0x96 - rw(m, 0x2600A8u));
            if (!(d0 & 0x8000u)) ww(m, a5 + 0x2C, d0);                     /* bmi on result */
        } else if (rw(m, a5 + 0x2C)) {
            uint16_t c = rw(m, a5 + 0x2C);
            if (s16(c) <= 0xA || (rb(m, a5 + 0x2D) & 8)) ww(m, V_BRAKE, rw(m, ROM_DEMO_BRK_A));
            ww(m, a5 + 0x2C, (uint16_t)(c - 1));
        }
    } else {
        ww(m, a5 + 0x2A, 0);
    }
    ww(m, V_THROTTLE, 0x7F);                                               /* 7CB0 */
}

/* ------------------------------------------------------------------ slot 34 */
sho_path sho_bike_task(sho_mem *m, uint32_t a5)
{
    sho_path path = SHO_PATH_GAME;
    uint16_t d0, d3;
    uint32_t a0;

    if (rw(m, V_MODE) == 1) {                                              /* 7E38 */
        uint8_t c = (uint8_t)(rb(m, a5 + 0x38) - 1), b39;
        wb(m, a5 + 0x38, c);
        if (c == 0) { wb(m, a5 + 0x38, 0xC); wb(m, a5 + 0x39, (uint8_t)(rb(m, a5 + 0x39) + 1)); }
        b39 = rb(m, a5 + 0x39);
        if ((int8_t)b39 > 0xA || !(b39 & 1)) sprite_register(m, a5);
        return SHO_PATH_MODE1;
    }
    if (rw(m, V_RETIRE)) {                                                 /* 7E1E */
        wb(m, 0x064140u, (uint8_t)(rb(m, 0x064140u) | 0x80));
        wb(m, a5, (uint8_t)(rb(m, a5) & 0x7F));
        wl(m, V_SPEED, 0);
        sprite_register(m, a5);
        return SHO_PATH_RETIRE;
    }
    if (rw(m, W_INGAME) == 0) { demo_driver(m, a5); path = SHO_PATH_DEMO; }

    /* 7CB8: steering damped right after the start line of the first stage */
    if (rw(m, V_STAGE_DONE) == 0 && s16(rw(m, V_TRACK_POS)) < 0x20) {
        uint32_t v = (uint32_t)(int32_t)s16(rw(m, V_STEER));
        unsigned sh = (uint16_t)(0x20 - rw(m, V_TRACK_POS)) >> 2;
        sh &= 63;
        v = sh < 32 ? v >> sh : 0;                                         /* lsr.l: logical */
        ww(m, V_STEER, (uint16_t)v);
    }

    /* 7CF4: lean direction and frame from the previous frame's smoothed steering */
    d0 = rw(m, a5 + 0x34);
    if (d0 & 0x8000u) { wb(m, a5, (uint8_t)(rb(m, a5) | 1)); d0 = neg16(d0); }
    else wb(m, a5, (uint8_t)(rb(m, a5) & 0xFE));
    d3 = 0;
    while (s16(d0) > s16(rw(m, ROM_LEAN_TBL + 2u * d3))) d3++;

    if (rw(m, V_HARDBRAKE) && rb(m, V_LEANFRAME) == 3
        && s16(rw(m, ROM_MAXSPEED)) - 0x14 <= s16(rw(m, V_SPEED)))
        ww(m, a5 + 0x3A, 0x20);                                            /* 7D3C: skid starts */

    if (rw(m, a5 + 0x3A)) {                                                /* 7D48: skid frames */
        ww(m, a5 + 0x3A, (uint16_t)(rw(m, a5 + 0x3A) - 1));
        d3 = (uint16_t)((0x1F - rw(m, a5 + 0x3A)) & 0x1C);
        a0 = ROM_SPR_SKID;
    } else if (rw(m, V_KNOCK)) {                                           /* 7D62: knockback frames */
        uint16_t d2 = rw(m, V_KNOCK);
        ww(m, a5 + 0x24, (uint16_t)(rw(m, a5 + 0x24) + 1));
        a0 = ROM_SPR_KNOCK;
        /* 7D76-7D86: tst d2 / bpl / neg d2 / neg d3 / btst #0,(a5) / bpl / neg d3.
         * BTST does not set N, so the second bpl sees N from the previous instruction:
         * TST.W d2 when d2 >= 0, otherwise NEG.W d3. Reproduced exactly. */
        if (d2 & 0x8000u) {
            d2 = neg16(d2);
            d3 = neg16(d3);
            if (d3 & 0x8000u) d3 = neg16(d3);
        }
        d2 = asr16(d2, 2);
        if (s16(d2) > 1) d2 = 1;
        d3 = (uint16_t)((uint16_t)(d3 << 3) + d2 * 4u);
    } else {                                                               /* 7D9E */
        wb(m, V_LEANFRAME, (uint8_t)d3);
        wb(m, V_LEANFLIP, rb(m, a5));
        ww(m, V_SPR_X, rw(m, a5 + 0xA));
        ww(m, V_SPR_Y, rw(m, a5 + 0xC));
        d3 = (uint16_t)(d3 * 4u);
        a0 = ROM_SPR_LEAN;
        if (rw(m, V_RETIRE)) d3 = 0;
    }
    wl(m, a5 + 0x1A, rl(m, a0 + (uint32_t)(int32_t)s16(d3)));              /* 7DD0 */
    ww(m, a5 + 0x20, (uint16_t)(0xD - s16(asr16(rw(m, V_SPEED), 5))));    /* 7DD6: 13 - speed/32 */
    {
        uint8_t b = rb(m, a5 + 0x1E);
        if (s16(rw(m, V_BRAKE)) >= s16(rw(m, ROM_BRAKE_T1))) b = (uint8_t)(b + 2);
        wb(m, a5 + 0x1F, b);
    }
    anim_timer(m, a5);
    knockback(m);
    steering(m, a5);
    offroad(m, a5);
    wobble(m, a5);
    skid_push(m, a5);
    sprite_register(m, a5);
    return path;
}
