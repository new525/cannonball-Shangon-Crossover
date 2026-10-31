/*
 * verify_crash: checks sho_crash.c against MAME captures, frame by frame.
 *
 * Captures (MAME Lua RAM dumps, one record per frame: u32 frame + bytes):
 *   <p>_slots.bin  $638C0-$639BF (slots 3-6)     <p>_vars.bin  $260000-$2600FF
 *   <p>_y.bin      $261200-$2613FF               <p>_tbl.bin   $61400-$616FF
 *   <p>_clip.bin   $260440-$26045F               <p>_buf.bin   $2607FC-$2607FF
 * For each consecutive pair (f, f+1) the model starts from frame f's state, takes the inputs
 * that other tasks provide at f+1 (crash counter; the speed injected by the capture's breakpoint
 * at the crash start, given on the command line), runs one frame of the four crash tasks and the
 * renderer, and compares: all 256 slot bytes, the crash variables, and the sprite entry of every
 * drawn object.
 *
 * usage: verify_crash <rom_data.bin> <capture prefix> <injected speed (hex) or -1> [verbose]
 */
#include "sho_crash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t frame; uint8_t *d; } rec;
typedef struct { rec *r; int n; int len; } stream;

static stream load(const char *pfx, const char *suf, int len)
{
    char name[512]; snprintf(name, sizeof name, "%s_%s.bin", pfx, suf);
    FILE *f = fopen(name, "rb"); stream s = { 0, 0, len };
    if (!f) { fprintf(stderr, "cannot open %s\n", name); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    s.n = (int)(sz / (4 + len)); s.r = calloc(s.n, sizeof(rec));
    for (int i = 0; i < s.n; i++) {
        uint8_t h[4]; if (fread(h, 1, 4, f) != 4) exit(1);
        s.r[i].frame = (uint32_t)h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3];
        s.r[i].d = malloc(len); if (fread(s.r[i].d, 1, len, f) != (size_t)len) exit(1);
    }
    fclose(f); return s;
}
static uint8_t *at(stream *s, uint32_t frame)
{
    for (int i = 0; i < s->n; i++) if (s->r[i].frame == frame) return s->r[i].d;
    return NULL;
}

static uint8_t work[0x8000], shared[0x2000];
static int sounds[64], nsounds;
static void snd(void *ctx, uint8_t c) { (void)ctx; if (nsounds < 64) sounds[nsounds++] = c; }

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s rom_data.bin prefix inj_speed [v]\n", argv[0]); return 2; }
    FILE *rf = fopen(argv[1], "rb"); static uint8_t rom[0x40000];
    size_t rl = fread(rom, 1, sizeof rom, rf); fclose(rf);
    int inj = (int)strtol(argv[3], NULL, 16), verbose = argc > 4;
    stream S = load(argv[2], "slots", 256), V = load(argv[2], "vars", 256), Y = load(argv[2], "y", 512),
           T = load(argv[2], "tbl", 768), C = load(argv[2], "clip", 32), B = load(argv[2], "buf", 4);
    sho_mem m; memset(&m, 0, sizeof m);
    m.rom = rom; m.rom_len = rl; m.work = work; m.shared = shared; m.shared_len = sizeof shared;
    m.sound_fn = snd;

    long frames = 0, slot_bad = 0, var_bad = 0, ent_ok = 0, ent_bad = 0, crash_frames = 0, starts = 0;
    static const uint32_t vars[] = { 0x22, 0x5C, 0x5E, 0x88, 0x94, 0xC8, 0xD4 };
    for (int i = 0; i + 1 < S.n; i++) {
        uint32_t f = S.r[i].frame, g = S.r[i + 1].frame;
        if (g != f + 1) continue;
        if (verbose > 1) printf("-- %u\n", g);
        uint8_t *v0 = at(&V, f), *v1 = at(&V, g), *y1 = at(&Y, g), *t1 = at(&T, g), *c1 = at(&C, g), *b1 = at(&B, g);
        if (!v0 || !v1 || !y1 || !t1 || !c1 || !b1) continue;
        memset(work, 0, sizeof work); memset(shared, 0, sizeof shared);
        memcpy(work + 0x38C0, S.r[i].d, 256);
        memcpy(shared, v0, 256);
        memcpy(shared + 0x1200, y1, 512);           /* sub CPU table, as seen during frame g */
        memcpy(shared + 0x440, c1, 32);
        memcpy(shared + 0x7FC, b1, 4);
        work[0x3A40] = 0x80; work[0x4080] = (v0[0x94] ? 0x00 : 0x80);
        /* the crash counter is incremented by the collision task, which runs after slot 3: the
           crash task sees the value of the end of frame f, which the copy of v0 already holds */
        int starting = !v0[0x94] && v1[0x94];
        if (starting && inj >= 0) { shared[0x78] = (uint8_t)(inj >> 8); shared[0x79] = (uint8_t)inj; }
        nsounds = 0;
        uint8_t drawn[4];
        sho_crash_frame(&m, drawn);
        static const uint32_t slots[4] = { SHO_A5_CRASH, SHO_A5_CRASH_SHADOW, SHO_A5_RIDER, SHO_A5_RIDER_SHADOW };
        uint8_t *s1 = S.r[i + 1].d;
        int bad_here = 0;
        for (int k = 0; k < 4; k++) {
            if (!drawn[k]) continue;
            uint32_t a5 = slots[k];
            work[a5 - 0x60000 + 6] = s1[k * 64 + 6]; work[a5 - 0x60000 + 7] = s1[k * 64 + 7];   /* list index */
            uint8_t e[12];
            int vis = sho_sprite_entry(&m, a5, e);
            int idx = (s1[k * 64 + 6] << 8 | s1[k * 64 + 7]);
            uint8_t *te = t1 + idx * 12;
            int n = vis ? 12 : 2;
            if (idx * 12 + 12 <= 768) {
                if (memcmp(e, te, n) == 0) ent_ok++;
                else {
                    ent_bad++; bad_here = 1;
                    if (verbose) {
                        printf("frame %u slot %d entry:", g, k + 3);
                        for (int b = 0; b < 12; b++) printf(" %02x", e[b]);
                        printf("\n          capture:"); for (int b = 0; b < 12; b++) printf(" %02x", te[b]); printf("\n");
                    }
                }
            }
        }
        for (int k = 0; k < 256; k++)
            if (work[0x38C0 + k] != s1[k]) {
                slot_bad++; bad_here = 1;
                if (verbose) printf("frame %u slot %d +%02x model %02x capture %02x\n", g, k / 64 + 3, k % 64, work[0x38C0 + k], s1[k]);
            }
        /* outside a crash, and on the frame it ends, other tasks (bike, centrifugal) move X */
        for (unsigned k = (v1[0x94] ? 0 : 1); k < sizeof vars / sizeof vars[0]; k++)
            if (memcmp(shared + vars[k], v1 + vars[k], 2)) {
                var_bad++; bad_here = 1;
                if (verbose) printf("frame %u var %06x model %02x%02x capture %02x%02x\n", g, 0x260000 + vars[k],
                                    shared[vars[k]], shared[vars[k] + 1], v1[vars[k]], v1[vars[k] + 1]);
            }
        if (verbose && nsounds) { printf("frame %u sounds:", g); for (int k = 0; k < nsounds; k++) printf(" %02x", sounds[k]); printf("\n"); }
        frames++;
        if (v1[0x94] || v0[0x94]) crash_frames++;
        if (starting) starts++;
        (void)bad_here;
    }
    printf("%s: %ld frame pairs (%ld in a crash, %ld starts); slot byte mismatches %ld, variable mismatches %ld, "
           "sprite entries %ld ok / %ld bad\n", argv[2], frames, crash_frames, starts, slot_bad, var_bad, ent_ok, ent_bad);
    return (slot_bad || var_bad || ent_bad) ? 1 : 0;
}
