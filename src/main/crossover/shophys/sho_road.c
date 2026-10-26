/*
 * Road lateral-offset ("road X") builders for the shared Super Scaler road path format.
 *
 *   sho_road_build_sh()     Super Hang-On second CPU sub_01236 ($1236): fixed depth layout from
 *                           the 33-entry table at sub-ROM $19A6; writes indices 191 (near) .. 0.
 *   ho_road_frame()         Hang-On (1985) second CPU $0A98: depth from the projected distance
 *                           (quotient, no x4); 28 $3210 markers; writes indices 111 .. 0; then shear,
 *                           far-slice interpolation and the heading word; ho_road_hpos() = IRQ $0E96.
 *   sho_road_build_outrun() OutRun second CPU $15B0 (Cannonball ORoad::setup_x_data): depth from the
 *                           projected distance; 128 $3210 markers first; writes indices 447 .. 0.
 *
 * Path format (both games): one (x, y) word pair per track position, |(x,y)| = 4096;
 * the builder starts at byte offset position*4. Output values are the road's projected lateral
 * offset per depth slice; the hardware H-scroll is $64C - value/64 (Super Hang-On sub_010DA),
 * so 64 units = 1 screen pixel. Both verified byte-exact against MAME 0.264 (see verify_road.c).
 *
 * To give a Super Hang-On bike a road on another course, call sho_road_build_sh() with that
 * course's path: the result is exactly the table the Super Hang-On physics expects.
 */
#include "sho_road.h"

static int16_t s16(int32_t v) { return (int16_t)(uint16_t)(uint32_t)v; }

/* 68000 DIVS.W returning the quotient word; *ovf set (and dn returned) on overflow */
static int32_t divs_q(int32_t dn, int16_t src, int *ovf) {
    int32_t q = dn / src;
    if (q < -32768 || q > 32767) { if (ovf) *ovf = 1; return dn; }
    if (ovf) *ovf = 0;
    return q;
}

static uint32_t isqrt32(uint32_t n) {           /* floor(sqrt(n)), as sub_017F8 / $177A / Hang-On $0CCA */
    uint32_t r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

typedef struct { int16_t cx, cy; int32_t acc_x, acc_y; } proj_t;

static int setup(const sho_path_reader *p, uint32_t start, proj_t *s) {
    int16_t x = s16(p->word(p->ctx, start) + p->word(p->ctx, start + 4));
    int16_t y = s16(p->word(p->ctx, start + 2) + p->word(p->ctx, start + 6));
    uint32_t d0 = (uint32_t)((int32_t)x * x) + (uint32_t)((int32_t)y * y);
    uint32_t dist = d0 ? isqrt32(d0) : 0;
    if (!dist) return 0;
    s->cx = s16(divs_q((int32_t)((uint32_t)(int32_t)x << 14), (int16_t)dist, 0));
    s->cy = s16(divs_q((int32_t)((uint32_t)(int32_t)y << 14), (int16_t)dist, 0));
    s->acc_x = s->acc_y = 0;
    return 1;
}

/* one path sample: returns projected lateral (quotient word) and forward>>7 and the divisor */
static void sample(const sho_path_reader *p, uint32_t *off, proj_t *s, int16_t *lat_q, int32_t *fwd7, int16_t *den) {
    int16_t w0 = p->word(p->ctx, *off), w1 = p->word(p->ctx, *off + 2),
            w2 = p->word(p->ctx, *off + 4), w3 = p->word(p->ctx, *off + 6);
    int16_t a, b;
    int32_t lat, fwd;
    *off += 8;
    s->acc_x += (int32_t)w0 + w2; s->acc_y += (int32_t)w1 + w3;
    a = s16(s->acc_x >> 5); b = s16(s->acc_y >> 5);
    lat = (int32_t)a * s->cy - (int32_t)b * s->cx;
    fwd = ((int32_t)a * s->cx + (int32_t)b * s->cy) >> 7;
    *den = s16((fwd >> 7) + 0x410);
    *lat_q = s16(divs_q(lat, *den, 0));
    *fwd7 = fwd;
}

static int fill(uint16_t *table, int *d4, int16_t *prev_x, int16_t *prev_end, int16_t cur_x, int16_t cur_end) {
    int16_t steps = s16(cur_end - *prev_end), inc, v, e;
    if (steps < 0) return -1;
    if (steps == 0) return 0;                      /* skip sample; previous values kept */
    inc = s16(divs_q(s16(cur_x - *prev_x), steps, 0));
    v = *prev_x; e = *prev_end;
    for (;;) {
        v = s16(v + inc); e = s16(e + 1);
        if (v > 0x3200 || v < -0x3200) return -1;
        table[*d4 / 2] = (uint16_t)v;
        *d4 -= 2;
        if (*d4 < 0) return -1;
        if (e == cur_end) break;
    }
    *prev_x = cur_x; *prev_end = cur_end;
    return 0;
}

void sho_road_build_sh(const sho_path_reader *p, uint32_t start_offset, const int16_t depth_end[33], uint16_t table[512])
{
    proj_t s; int d4 = 0x17E; int16_t prev_x = 0, prev_end = 0; uint32_t off = start_offset;
    if (!setup(p, start_offset, &s)) return;
    for (int d6 = 0x20; d6 >= 0; d6--) {
        int16_t lat_q, den; int32_t fwd7;
        sample(p, &off, &s, &lat_q, &fwd7, &den);
        if (fill(table, &d4, &prev_x, &prev_end, lat_q, depth_end[d6]) < 0) return;
    }
}

void sho_road_build_outrun(const sho_path_reader *p, uint32_t start_offset, uint16_t table[512])
{
    proj_t s; int d4 = 0x37E; int16_t prev_x = 0, prev_end = 0; uint32_t off = start_offset;
    for (int k = 0; k < 0x80; k++) table[k] = 0x3210;               /* $15FC-$1608 */
    if (!setup(p, start_offset, &s)) return;
    for (int d6 = 0x20; d6 >= 0; d6--) {
        int16_t lat_q, den, cur_end; int32_t fwd7; int ovf;
        int32_t q;
        sample(p, &off, &s, &lat_q, &fwd7, &den);
        q = divs_q(fwd7, den, &ovf);
        cur_end = s16((int32_t)(uint16_t)q * 4);                      /* add.w d2,d2 x2 */
        if (fill(table, &d4, &prev_x, &prev_end, lat_q, cur_end) < 0) return;
    }
}

/* ---------------------------------------------------------------- Hang-On (1985) */

static uint16_t ho_heading(const sho_path_reader *p, uint32_t start)       /* sub_00C40 */
{
    int16_t d0 = 0, d1 = 0, d2, d3;
    uint16_t d4;
    int n, ovf;
    for (int k = 0; k < 4; k++) {
        d0 = s16(d0 + p->word(p->ctx, start + 4 * k));
        d1 = s16(d1 + p->word(p->ctx, start + 4 * k + 2));
    }
    d2 = d0 < 0 ? s16(-d0) : d0;                       /* neg.w: $8000 stays $8000 */
    d3 = d1 < 0 ? s16(-d1) : d1;
    n = ((uint16_t)(d2 - d3) & 0x8000) != 0;            /* cmp.w d3,d2 ; bpl */
    if (n) d4 = (uint16_t)divs_q((int32_t)0x100 * d0, d1, &ovf);
    else   d4 = (uint16_t)divs_q((int32_t)-0x100 * d1, d0, &ovf);
    d4 += (d0 > 0 || (d0 == 0 && d1 >= 0)) ? 0x200 : 0x600;
    if (n) d4 += ((int32_t)d0 * d1 >= 0) ? 0xFE00 : 0x200;
    return d4;
}

void ho_road_frame(const sho_path_reader *p, uint16_t start_offset, uint16_t shear, ho_road_state *s)
{
    /* sub_00ABA: road X */
    proj_t pj; int d4 = 0xDE; int16_t prev_x = 0, prev_end = 0; uint32_t off = start_offset;
    if (setup(p, start_offset, &pj)) {
        s->heading = ho_heading(p, start_offset);
        for (int k = 0; k < 28; k++) s->road_x[k] = HO_MARK;
        for (int d6 = 0x20; d6 >= 0; d6--) {
            int16_t lat_q, den, cur_end; int32_t fwd7;
            sample(p, &off, &pj, &lat_q, &fwd7, &den);
            cur_end = s16(divs_q(fwd7, den, 0));
            if (fill(s->road_x, &d4, &prev_x, &prev_end, lat_q, cur_end) < 0) break;
        }
    }
    /* sub_00D5C: shear */
    {
        uint16_t d0 = (uint16_t)(shear << 6);
        if (!d0) {
            for (int i = 0; i < HO_SLICES; i++) s->sheared[i] = s->road_x[i];
        } else {
            uint16_t inc = (uint16_t)divs_q((int16_t)d0, 0x70, 0), d1 = 0;
            for (int i = 0; i < HO_SLICES; i++) {
                uint16_t v = s->road_x[i];
                s->sheared[i] = (uint16_t)((v == HO_MARK ? 0 : d1) + v);
                d1 = (uint16_t)(d1 + inc);
            }
        }
    }
    /* sub_00DB6: far slices 0..5, 16 entries each */
    for (int i = 0; i < 6; i++) {
        uint16_t cur = s->sheared[i];
        uint16_t *o = s->far + 16 * i;
        if (cur == HO_MARK) { for (int k = 0; k < 16; k++) o[k] = HO_MARK; continue; }
        uint32_t a = (uint32_t)cur << 16;
        int32_t step = (int32_t)(((uint32_t)s->sheared[i + 1] << 16) - a) >> 4;
        o[0] = cur;
        for (int k = 1; k < 16; k++) { a += (uint32_t)step; o[k] = (uint16_t)(a >> 16); }
    }
}

void ho_road_hpos(const uint16_t sheared[HO_SLICES], uint16_t hpos_out[96])
{
    for (int i = 0; i < 96; i++) {
        int16_t v = (int16_t)sheared[i];
        uint16_t d0 = (uint16_t)((int16_t)(s16(-v) >> 6) + 0xF42);
        if (!((uint16_t)(d0 - 0x1000) & 0x8000)) d0 = (uint16_t)((d0 + 8) ^ 0x7F8);
        hpos_out[i] = d0;
    }
}
