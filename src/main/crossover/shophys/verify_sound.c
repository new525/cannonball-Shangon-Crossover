/* verify_sound: sho_sound.c against MAME captures (drive.lua dumps, one record per frame):
 *   vars.bin $260000-$2600FF, w400.bin $060400-$06041F, slots.bin $063800-$063FFF, pkt.bin $060640-$06066F
 * For each frame pair (f, f+1): state of the end of f -> model slot-1 task -> compare the packet bytes
 * 1-3 and the task's own slot with frame f+1, and list the effect commands it issues.
 * usage: verify_sound <rom_data.bin> <dir> [verbose] */
#include "sho_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int n, len; uint32_t *fr; uint8_t *d; } st;
static st load(const char *dir, const char *name, int len) {
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name); FILE *f = fopen(p, "rb"); st s = {0, len, 0, 0};
    if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); s.n = (int)(sz / (4 + len));
    s.fr = malloc(s.n * 4); s.d = malloc((size_t)s.n * len);
    for (int i = 0; i < s.n; i++) { uint8_t h[4]; if (fread(h, 1, 4, f) != 4 || fread(s.d + (size_t)i * len, 1, len, f) != (size_t)len) exit(1);
        s.fr[i] = (uint32_t)h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3]; }
    fclose(f); return s;
}
static uint8_t work[0x8000], shared[0x1000];
static int snd[16], nsnd;
static void cb(void *c, uint8_t v) { (void)c; if (nsnd < 16) snd[nsnd++] = v; }
int main(int argc, char **argv) {
    FILE *rf = fopen(argv[1], "rb"); static uint8_t rom[0x40000]; size_t rl = fread(rom, 1, sizeof rom, rf); fclose(rf);
    st V = load(argv[2], "vars.bin", 256), W = load(argv[2], "w400.bin", 32), S = load(argv[2], "slots.bin", 0x800), P = load(argv[2], "pkt.bin", 0x30);
    int verbose = argc > 3;
    sho_mem m; memset(&m, 0, sizeof m); m.rom = rom; m.rom_len = rl; m.work = work; m.shared = shared; m.shared_len = sizeof shared; m.sound_fn = cb;
    long pairs = 0, pkt_bad = 0, slot_bad = 0, nsounds = 0;
    for (int i = 0; i + 1 < V.n; i++) {
        if (V.fr[i + 1] != V.fr[i] + 1) continue;
        memset(work, 0, sizeof work); memset(shared, 0, sizeof shared);
        memcpy(shared, V.d + (size_t)i * 256, 256);
        memcpy(work + 0x400, W.d + (size_t)i * 32, 32);
        memcpy(work + 0x3800, S.d + (size_t)i * 0x800, 0x800);
        memcpy(work + 0x640, P.d + (size_t)i * 0x30, 0x30);
        nsnd = 0;
        sho_sound_task(&m, SHO_A5_SOUND);
        const uint8_t *p1 = P.d + (size_t)(i + 1) * 0x30, *s1 = S.d + (size_t)(i + 1) * 0x800 + 0x40;
        for (int k = 1; k <= 3; k++) if (work[0x640 + k] != p1[k]) { pkt_bad++; if (verbose) printf("frame %u slot %d model %02x capture %02x\n", V.fr[i + 1], k, work[0x640 + k], p1[k]); }
        for (int k = 8; k < 0x10; k++) if (work[0x3840 + k] != s1[k]) { slot_bad++; if (verbose) printf("frame %u task +%02x model %02x capture %02x\n", V.fr[i + 1], k, work[0x3840 + k], s1[k]); }
        if (nsnd) { nsounds += nsnd; printf("frame %u sounds:", V.fr[i + 1]); for (int k = 0; k < nsnd; k++) printf(" %02x", snd[k]); printf("\n"); }
        pairs++;
    }
    printf("%ld frame pairs: packet byte mismatches %ld, task-slot mismatches %ld, %ld effect commands\n", pairs, pkt_bad, slot_bad, nsounds);
    return (pkt_bad || slot_bad) ? 1 : 0;
}
