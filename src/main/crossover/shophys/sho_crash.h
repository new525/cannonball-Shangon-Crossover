/* Super Hang-On crash sequence ($009FE6, $00A2EC, $00A33A) and sprite entries ($002994).
 * See sho_crash.c. */
#ifndef SHO_CRASH_H
#define SHO_CRASH_H
#include "shophys.h"

#define SHO_A5_CRASH         0x0638C0u  /* slot 3: the bike           ($009FE6) */
#define SHO_A5_CRASH_SHADOW  0x063900u  /* slot 4: its shadow         ($00A2EC) */
#define SHO_A5_RIDER         0x063940u  /* slot 5: the rider          ($00A33A) */
#define SHO_A5_RIDER_SHADOW  0x063980u  /* slot 6: the rider's shadow ($00A2EC) */

#define SHO_V_X             0x260022u  /* .w lateral position (+ = left)                       */
#define SHO_V_SPEED         0x260078u  /* .w km/h (high word of the 16.16 speed)                */
#define SHO_V_CRASH_COUNT   0x260084u  /* .w scenery hits; a change starts a crash               */
#define SHO_V_KNOCK         0x260088u  /* .w knockback (traffic)                                 */
#define SHO_V_CRASH         0x260094u  /* .w $FFFF while crashing                                */
#define SHO_V_CRASH_CLASS   0x2600C8u  /* .w speed class 0..3 at the moment of the crash         */
#define SHO_V_ROADY         0x261200u  /* .w[256] road height per depth (sub CPU), <<4           */

/* One frame of the four crash objects, in slot order. drawn[i] = the object was submitted for
 * drawing this frame. The crash task needs $260084 to differ from its slot's +$20 to start. */
void sho_crash_frame(sho_mem *m, uint8_t drawn[4]);
int  sho_crash_task(sho_mem *m, uint32_t a5);
int  sho_crash_shadow_task(sho_mem *m, uint32_t a5);
int  sho_crash_rider_task(sho_mem *m, uint32_t a5);
uint16_t sho_crash_road_y(sho_mem *m, uint16_t depth);

/* sub_02994: build the 12-byte sprite entry for an object slot (also sets its +$19 zoom and
 * the visible bit 5 of +0, as the original does). Returns 1 if visible.
 *   [0] bottom  [1] top  [2..3] x  [4..5] flip<<8 | pitch  [6..7] address  [8] bank
 *   [9] priority<<6 | palette  [10..11] vzoom<<5 | hzoom                                     */
int sho_sprite_entry(sho_mem *m, uint32_t a5, uint8_t e[12]);
#endif
