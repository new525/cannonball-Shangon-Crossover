#ifndef SHO_ROAD_H
#define SHO_ROAD_H
#include <stdint.h>

/* Reads one signed path word at a byte offset from the course's path start. */
typedef struct {
    int16_t (*word)(void *ctx, uint32_t byte_offset);
    void *ctx;
} sho_path_reader;

/* Super Hang-On builder: depth_end = the 33 words at sub-ROM $19A6. Writes table indices 191..0. */
void sho_road_build_sh(const sho_path_reader *p, uint32_t start_offset, const int16_t depth_end[33], uint16_t table[512]);
/* OutRun builder: writes 128 $3210 markers, then indices 447..0. */
void sho_road_build_outrun(const sho_path_reader *p, uint32_t start_offset, uint16_t table[512]);


/* Hang-On (1985) second CPU, sub_00A98 and helpers. One call per frame. */
#define HO_SLICES 112                 /* $7F100..$7F1DF, index 111 nearest, one slice per scanline 128+i */
#define HO_MARK   0x3210              /* "no road" marker, as in OutRun */
typedef struct {
    uint16_t road_x[HO_SLICES];       /* $7F100: road X per slice (sub_00ABA); persists between frames */
    uint16_t sheared[HO_SLICES];      /* $7F200: + bike lateral shear (sub_00D5C) */
    uint16_t far[96];                 /* $7F300: 6 farthest slices x16 (sub_00DB6) */
    uint16_t heading;                 /* $7E040 (sub_00C40) */
} ho_road_state;
/* start_offset = position*4 (16-bit, as $7F020); shear = $7E050 as read at $0D68. */
void ho_road_frame(const sho_path_reader *p, uint16_t start_offset, uint16_t shear, ho_road_state *s);
/* sub_00E96 (sub-CPU IRQ): H-scroll words for road RAM hpos[$90..$EF] from sheared[0..95]. */
void ho_road_hpos(const uint16_t sheared[HO_SLICES], uint16_t hpos_out[96]);

#endif
