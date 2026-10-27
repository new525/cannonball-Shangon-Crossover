/* Super Hang-On sound task ($005B98, slot 1). See sho_sound.c. */
#ifndef SHO_SOUND_H
#define SHO_SOUND_H
#include "shophys.h"
#define SHO_A5_SOUND  0x063840u   /* slot 1 */
#define SHO_PKT       0x060640u   /* .b[8] the sound packet sent each frame ($060640 = command slot) */
/* Writes $060641-$060643 and issues effect commands through m->sound_fn. */
void sho_sound_task(sho_mem *m, uint32_t a5);
#endif
