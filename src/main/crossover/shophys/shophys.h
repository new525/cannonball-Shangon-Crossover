/*
 * Super Hang-On (Sega, 1987) - player vehicle tasks, C reconstruction.
 * Source: main-CPU program of shangon1 (FD1089B 317-0034), decrypted.
 *
 * The original keeps game state in the second CPU's RAM, seen by the main CPU at $260000+
 * through the 315-5195 mapper, plus main work RAM at $060000-$067FFF. This port keeps that
 * layout: every variable is accessed at its original address, so the port can be checked
 * byte for byte against MAME (see verify.c) while the rest of the game is mapped out.
 *
 * Tasks run once per frame in this slot order: 8 (speed), 9 (centrifugal), ..., 34 (bike).
 * Verified frame-exact against MAME 0.264 by breakpoint snapshots at task entry and exit.
 */
#ifndef SHOPHYS_H
#define SHOPHYS_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const uint8_t *rom;  size_t rom_len;   /* main-CPU program ROM, data (not opcode) view   */
    uint8_t *work;                          /* $060000-$067FFF (0x8000 bytes)                 */
    uint8_t *shared;     size_t shared_len; /* $260000.. (>= 0x800; 0x1000 incl. road table) */
    /* Crossover hook (NULL = original behaviour, as verified): decides "off road" for the bike
     * task's sub_07F30 from the bike's X instead of Super Hang-On's fixed |X| > 300. Returns 0/1. */
    int  (*offroad_fn)(void *ctx, int16_t x);
    void  *offroad_ctx;
} sho_mem;

/* Task slot base addresses (a5) */
#define SHO_A5_SPEED       0x063A00u   /* slot 8,  entry $00610A */
#define SHO_A5_CENTRIFUGAL 0x063A40u   /* slot 9,  entry $0063CE */
#define SHO_A5_BIKE        0x064080u   /* slot 34, entry $007AE6 */

typedef enum {
    SHO_PATH_RUN = 0,      /* speed: normal integration                                   */
    SHO_PATH_DECAY,        /* speed: $260094 set (crash): speed *= 31/32                  */
    SHO_PATH_MODE1,        /* $260000 == 1                                                */
    SHO_PATH_GOAL,         /* speed: $2600EA set - goal sequence, NOT reconstructed       */
    SHO_PATH_GAME,         /* bike: in a game                                             */
    SHO_PATH_DEMO,         /* bike: attract-mode demo driver                              */
    SHO_PATH_RETIRE        /* bike: $2600EC set - bike object removed                     */
} sho_path;

sho_path sho_speed_task(sho_mem *m, uint32_t a5);        /* $00610A */
void     sho_centrifugal_task(sho_mem *m, uint32_t a5);  /* $0063CE */
sho_path sho_bike_task(sho_mem *m, uint32_t a5);         /* $007AE6 */

/* Input tasks; raw = the byte read from the ADC at $143021 (verified 1,403-1,404 frames each) */
#define SHO_A5_STEER       0x063880u   /* slot 2,  entry $005F4C */
#define SHO_A5_THROTTLE    0x063A80u   /* slot 10, entry $006014 */
#define SHO_A5_BRAKE       0x063C40u   /* slot 33, entry $006086 */
void sho_steering_task(sho_mem *m, uint32_t a5, uint8_t raw);   /* -> $260068 (+ = right) */
void sho_throttle_task(sho_mem *m, uint8_t raw);                /* -> $26006C (0..127)    */
void sho_brake_task(sho_mem *m, uint8_t raw);                   /* -> $260070             */

#endif
