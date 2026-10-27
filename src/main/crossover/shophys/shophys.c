/*
 * Super Hang-On player speed / track-progress task ($00610A). See shophys.h.
 * Comments give the original 68000 address of each step.
 */
#include "sho_internal.h"

/* ---- ROM constants (addresses in the program ROM, data view) ---- */
#define ROM_MAXSPEED   0x85A6u  /* .l 0x01170000: 279.0 km/h, 16.16              */
#define ROM_ACCEL_KMH  0x85AAu  /* .w 80: below this, always accelerate          */
#define ROM_DIST       0x85AEu  /* .l 151: distance per km/h per frame            */
#define ROM_BRAKE      0x85B2u  /* .l 0xC666: deceleration step                   */
#define ROM_OFFROAD_DV 0x85CCu  /* .w 3: throttle divisor when off-road          */
#define ROM_BRAKE_T1   0x85CEu  /* .w 128, 160, 192, 224 brake tiers              */
#define ROM_DISTCAP    0x85DEu  /* .l[course] course distance cap                 */

/* ---- shared game state ($26xxxx) ---- */
#define V_MODE        0x260000u /* .w game mode (1, 2 = racing ...)               */
#define V_TRK_LIST    0x260002u /* .l pointer to track event list (8-byte entries) */
#define V_TRK_IDX     0x260006u /* .w byte offset into track event list            */
#define V_COURSE      0x260012u /* .w course 0..3                                  */
#define V_PLAYER_X    0x260022u /* .w player lateral position                      */
#define V_TRACK_POS   0x260030u /* .l track position (hi word compared to events)  */
#define V_TRK_EVENT   0x26004Cu /* .w value of last track event reached            */
#define V_HARD_BRAKE  0x260054u /* .w set when brake >= top tier                   */
#define V_STEER       0x260068u /* .w steering -127..127 (+ = right)  [not used here]*/
#define V_THROTTLE    0x26006Cu /* .w 0..127                                       */
#define V_BRAKE       0x260070u /* .w 0..~256                                      */
#define V_DIST_FRAME  0x260074u /* .l distance this frame                          */
#define V_SPEED       0x260078u /* .l speed, 16.16 km/h                            */
#define V_FLAG_88     0x260088u /* .w throttle cut + 3 brake steps (meaning TBD)   */
#define V_ODO_STEP    0x26008Cu /* .w odometer increment this frame                */
#define V_ODOMETER    0x260090u /* .w odometer                                     */
#define V_CRASH       0x260094u /* .w crash: speed decays 31/32 per frame          */
#define V_OFFROAD     0x260098u /* .w off-road: throttle / 3                       */
#define V_CURVE_DIR   0x2600ACu /* .w curve type (>1 = curve; 3 vs 2 = direction)  */
#define V_CURVE_RAD   0x2600B0u /* .w curve radius                                 */
#define V_GOAL        0x2600EAu /* .w goal sequence active                         */
#define V_TRK_BLOCK1  0x260424u /* .b track events suspended (set elsewhere)        */
#define V_TRK_BLOCK2  0x260434u /* .b set when track position hi >= 2000           */
#define V_FRAME_TGL   0x2607FEu /* .w bit 10 toggled every task run                 */

/* ---- main work RAM ($06xxxx) ---- */
#define W_SC_INPUT    0x060051u /* .b input bits; bit 5 = Supercharger button       */
#define W_ATTRACT     0x060408u /* .w nonzero when a game is being played           */
#define W_SC_ACTIVE   0x060412u /* .b 0xFF while supercharging                      */
#define W_COURSE_DIST 0x060444u /* .l distance along course, capped per course      */
#define W_TIMER       0x060500u /* .b race timer, BCD; 0 = time over                */

/* ---- task-local (a5 relative) ---- */
#define T_TARGET      0x1Eu     /* .w target speed from throttle                     */
#define T_ODO_REM     0x22u     /* .w odometer remainder (speed mod 34 accumulated)  */
#define T_SC_STEP     0x24u     /* .l supercharger accel/decel step                  */

static sho_path distance_and_track(sho_mem *m, uint32_t a5, sho_path path);

sho_path sho_speed_task(sho_mem *m, uint32_t a5)
{
    uint32_t d0, d1, d2;

    if (rw(m, V_MODE) == 1) {                                     /* 610A */
        ww(m, V_FRAME_TGL, rw(m, V_FRAME_TGL) ^ 0x400);           /* 63A4 */
        return SHO_PATH_MODE1;
    }
    if (rw(m, V_GOAL))                                            /* 6116 */
        return SHO_PATH_GOAL;   /* calls sub_0B430 and may stop the bike: not reconstructed yet */

    if (rw(m, V_CRASH)) {                                         /* 6142: speed.w *= 31/32 */
        uint16_t s = rw(m, V_SPEED);
        uint16_t t = (uint16_t)((s << 5) - s);
        ww(m, V_SPEED, (uint16_t)(s16(t) >> 5));
        return distance_and_track(m, a5, SHO_PATH_DECAY);
    }

    d0 = rw(m, V_THROTTLE);                                       /* 6164 / 6172 */
    if (rb(m, W_TIMER) == 0) { ww(m, V_THROTTLE, 0); d0 = 0; }    /* 616C: time over */
    if (rw(m, V_FLAG_88)) d0 = 0;                                 /* 6178 */
    if (rw(m, V_OFFROAD)) d0 = divu_w(d0 & 0xFFFFu, rw(m, ROM_OFFROAD_DV));   /* 618A */

    d1 = (uint32_t)rw(m, ROM_MAXSPEED) * (d0 & 0xFFFFu);          /* 6192: 279 * throttle */
    d1 = divu_w(d1, 0x7F);                                        /* 619A */
    ww(m, a5 + T_TARGET, (uint16_t)d1);                           /* 619E: target km/h */

    d1 = rl(m, ROM_MAXSPEED) - rl(m, V_SPEED);                    /* 61A2 */
    d1 = divs_w(d1, 0x400);
    d1 = (uint32_t)(int32_t)s16(d1);                              /* ext.l */
    d1 = d1 * 4u;                                                 /* add.l d1,d1 x2 */

    if (s16(rw(m, V_SPEED)) >= s16(rw(m, ROM_ACCEL_KMH))) {       /* 61B8: >= 80 km/h */
        int16_t spd = s16(rw(m, V_SPEED)), tgt = s16(rw(m, a5 + T_TARGET));
        int steps;
        if (spd > tgt)       d1 = (uint32_t)0 - rl(m, ROM_BRAKE) * 2u;   /* 61D4 */
        else if (spd == tgt) d1 = 0;                                      /* 61D0 */
        d2 = (uint32_t)0 - rl(m, ROM_BRAKE);                              /* 61DE */
        if (rw(m, V_FLAG_88)) steps = 3;                                  /* 61E6 */
        else {
            int16_t br = s16(rw(m, V_BRAKE));                             /* 61EE */
            if      (br < s16(rw(m, ROM_BRAKE_T1)))     steps = 0;
            else if (br < s16(rw(m, ROM_BRAKE_T1 + 2))) steps = 1;
            else if (br < s16(rw(m, ROM_BRAKE_T1 + 4))) steps = 2;
            else if (br < s16(rw(m, ROM_BRAKE_T1 + 6))) steps = 3;
            else { ww(m, V_HARD_BRAKE, 1); steps = 4; }                   /* 6214 */
        }
        d1 += (uint32_t)steps * d2;                                       /* 621C-6222 */
    }
    d1 += rl(m, V_SPEED);                                         /* 6224 */
    if ((int32_t)d1 < 0) d1 = 0;                                  /* 622E */
    wl(m, V_SPEED, d1);

    /* 6236: Supercharger */
    if (rw(m, W_ATTRACT) && rw(m, V_MODE) == 2
        && (rw(m, V_OFFROAD) | rw(m, V_CRASH) | rw(m, V_GOAL)) == 0
        && (rb(m, W_SC_INPUT) & 0x20) && s16(rw(m, V_SPEED)) > 0xF0) {
        wb(m, W_SC_ACTIVE, 0xFF);                                 /* 6272 */
        wl(m, V_SPEED, rl(m, V_SPEED) + rl(m, a5 + T_SC_STEP));
        if (s16(rw(m, V_SPEED)) > 0x117) ww(m, V_SPEED, 0x117);   /* 628E: word write keeps fraction */
    } else {
        wb(m, W_SC_ACTIVE, 0);                                    /* 6298 */
        if (s16(rw(m, V_SPEED)) > 0xF1)
            wl(m, V_SPEED, rl(m, V_SPEED) - rl(m, a5 + T_SC_STEP));
    }
    return distance_and_track(m, a5, SHO_PATH_RUN);
}

static sho_path distance_and_track(sho_mem *m, uint32_t a5, sho_path path)
{
    uint32_t d0 = rl(m, ROM_DIST), d1;                            /* 62B2 */

    if (s16(rw(m, V_CURVE_DIR)) > 1) {                            /* 62B8: on a curve */
        d1 = (uint32_t)(int32_t)s16(rw(m, V_PLAYER_X));
        if (rw(m, V_CURVE_DIR) != 3) d1 = (uint32_t)0 - d1;
        d1 = divs_w(d1, 0x28);                                    /* x / 40 */
        d1 = (d1 & 0xFFFF0000u) | (uint16_t)(d1 + rw(m, V_CURVE_RAD));
        d0 = (uint32_t)((int32_t)s16(d1) * (int32_t)s16(d0));     /* muls.w */
        d0 = divs_w(d0, rw(m, V_CURVE_RAD));                      /* * (R +/- x/40) / R */
    }
    d0 = (d0 & 0xFFFFu) * (uint32_t)rw(m, V_SPEED);               /* 62E8: mulu.w */
    sho_track_advance(m, a5, d0);
    return path;
}

/* sub_01456: the rumble-strip pattern for the road chip ($261A00, 191 words) from the odometer */
static void road_stripes(sho_mem *m)
{
    uint32_t a0 = 0xE7C8u + (uint32_t)(rw(m, V_ODOMETER) & 0x1F) * 0x180u, a1 = 0x261A00u;
    if (m->shared_len < 0x1A00u + 0xBFu * 2u) return;            /* not in this memory image */
    for (int k = 0; k <= 0xBE; k++, a0 += 2, a1 += 2)
        ww(m, a1, rw(m, 0x1492u + (rw(m, a0) & 2u)));
}

void sho_track_advance(sho_mem *m, uint32_t a5, uint32_t d0)
{
    uint32_t a0, cap;
    wl(m, V_DIST_FRAME, d0);
    wl(m, V_TRACK_POS, rl(m, V_TRACK_POS) + d0);                  /* 62F4 */
    wb(m, V_TRK_BLOCK2, s16(rw(m, V_TRACK_POS)) < 0x7D0 ? 0 : 0xFF);   /* 62FA-630C */

    cap = rl(m, ROM_DISTCAP + 4u * rw(m, V_COURSE));              /* 6312 */
    d0 += rl(m, W_COURSE_DIST);
    if ((int32_t)d0 > (int32_t)cap) d0 = cap;
    wl(m, W_COURSE_DIST, d0);

    ww(m, V_FRAME_TGL, rw(m, V_FRAME_TGL) ^ 0x400);               /* 6338 */
    road_stripes(m);                                              /* 6340: bsr sub_01456 */

    if (rb(m, V_TRK_BLOCK2) == 0 && rb(m, V_TRK_BLOCK1) == 0) {   /* 6344: track events */
        a0 = (rl(m, V_TRK_LIST) + (uint32_t)(int32_t)s16(rw(m, V_TRK_IDX))) & (uint32_t)0xFFFFFF;
        if (s16(rw(m, a0)) <= s16(rw(m, V_TRACK_POS))) {
            ww(m, V_TRK_EVENT, rw(m, a0 + 2));
            ww(m, V_TRK_IDX, (uint16_t)(rw(m, V_TRK_IDX) + 8));
        }
    }

    {                                                             /* 6376: odometer */
        uint32_t q = divu_w((uint32_t)(int32_t)s16(rw(m, V_SPEED)), 0x22);
        uint16_t quo = (uint16_t)q, rem = (uint16_t)(q >> 16);
        uint16_t acc = (uint16_t)(rw(m, a5 + T_ODO_REM) + rem);
        if (s16(acc) > 0x22) { acc = (uint16_t)(acc - 0x22); quo++; }
        ww(m, a5 + T_ODO_REM, acc);
        ww(m, V_ODO_STEP, quo);
        ww(m, V_ODOMETER, (uint16_t)(rw(m, V_ODOMETER) + quo));
    }
}
