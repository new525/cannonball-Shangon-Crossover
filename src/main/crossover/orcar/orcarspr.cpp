/***************************************************************************
    OutRunners' player car: its sprite ($99B8) and the System 32 sprite renderer. See orcarspr.hpp.
    Comments give the address of each instruction transcribed.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/orcar/orcarspr.hpp"

#include <algorithm>
#include <cstring>

namespace orcar
{

static const uint32_t ANIM = 0x34138;               // frame definitions: rate, then a piece list per sub-frame
static const uint32_t PAL_TABLE = 0x32BA4;          // palette index -> the pieces' palette words
static const uint32_t IMAGES = 0x122B74;            // image definitions, at 4 x the image's number
static const uint32_t WHEEL_TABLES = 0x18D25C;      // per car: wheel and smoke positions, 16 bytes per body image
static const uint32_t IMG_BASE = 0xA038, FRAME_FIRST = 0xA048, WHEEL1_FRAME = 0xA058, WHEEL2_FRAME = 0xA068;
static const uint32_t WHEEL1_MASK = 0xA078, WHEEL2_MASK = 0xA080, WHEEL1_PAL = 0xA088, WHEEL2_PAL = 0xA098;
static const uint32_t WHEEL1_LIMIT = 0xA0A8, WHEEL2_LIMIT = 0xA0B0, WHEELS_SHOWN = 0xA0B8;
static const uint32_t SMOKE1_FRAMES = 0xA198, SMOKE2_FRAMES = 0xA1B8, SMOKE_PAL = 0xA178, SMOKE_LEN = 0xA1D8;
static const uint16_t UNDER_FRAME = 0x44, UNDER_PAL = 0x58;     // the dust under the car ($9C85)

static inline uint16_t lo16(uint32_t v) { return (uint16_t) v; }

Record Record::publish(const State& s)
{
    Record r;                                                       // $77BD
    r.frame = s.anim_frame;                                         // 7877
    r.pal = s.pal_index;                                            // 787D
    r.sub = s.anim_step;                                            // 7883
    r.wheel[0] = s.wheel[0];                                        // 7889
    r.wheel[3] = s.squat;                                           // 7890
    r.wheel[1] = s.wheel[1];                                        // 7897
    r.wheel[2] = s.wheel[2];                                        // 789E
    r.speed = s.speed_copy;                                         // 78A5
    return r;
}

// $97FC-$99B3, the fields of the car's sprite object that $99B8 reads for the player's car
void CarSprite::init(uint8_t c)
{
    *this = CarSprite(rom);
    car = c;                                                        // 9830
    img_base = r16(IMG_BASE + 2u * c);                              // 9834
    frame_base = r16(FRAME_FIRST + 2u * c);                         // 983F
    uint32_t t = r32(WHEEL_TABLES + 4u * c);                        // 98A2
    wheel[0].table = t;                                             // 98AA
    wheel[0].frame = r16(WHEEL1_FRAME + 2u * c);                    // 984A
    wheel[0].pal = r16(WHEEL1_PAL + 2u * c);                        // 9855
    wheel[0].mask = r8_(WHEEL1_MASK + c);                           // 986B
    wheel[0].limit = r8_(WHEEL1_LIMIT + c);                         // 9860
    wheel[1].table = t + 4;                                         // 98AF
    wheel[1].frame = r16(WHEEL2_FRAME + 2u * c);                    // 9876
    wheel[1].pal = r16(WHEEL2_PAL + 2u * c);                        // 9881
    wheel[1].mask = r8_(WHEEL2_MASK + c);                           // 9897
    wheel[1].limit = r8_(WHEEL2_LIMIT + c);                         // 988C
    smoke[0].frames = SMOKE1_FRAMES;                                // 98B6
    smoke[0].pos = t + 8;                                           // 98C0
    smoke[0].which = 2;                                             // 98C7: &52[the car's entry]
    smoke[0].mask = 2;                                              // 98CE
    smoke[1].frames = SMOKE2_FRAMES;                                // 98D4
    smoke[1].pos = t + 0xC;                                         // 98DE
    smoke[1].which = 1;                                             // 98E5: &51[the car's entry]
    smoke[1].mask = 1;                                              // 98EC
    // 9924-9962: the two $14374 objects: +9 the car (the second + 8), +A $FF (the second 0)
    fly[0].idx = c; fly[0].a = 0xFF;
    fly[1].idx = (uint8_t)(c + 8); fly[1].a = 0;
}

// The $14374 objects' tick: 0 $1438C (the animation $1467A[+9]), 1 $143B0 (wait for +2E), 2 $143DB (the
// animation, $F579; at its end +2E := +A)
void CarSprite::after()
{
    for (Fly& f : fly)
    {
        switch (f.state)
        {
        case 0:                                                     // 1438C
        {
            f.state++;
            f.frame = r16(0x1467A + 2u * f.idx);                    // 14398 (143A0: its palette, 3A)
            uint8_t b = r8_(r32(ANIM + 4u * f.frame));              // 143AA jsr F5D2
            f.rate = b; f.count = b; f.step = 0;
        }
            [[fallthrough]];
        case 1:                                                     // 143B0
            if (!wheels_off) break;                                 // 143B7
            f.state++;                                              // 143BD (143C0: 32, the position)
            f.step = 0; f.rate = 1; f.count = 1;                    // 143CC-143D6
            [[fallthrough]];
        case 2:                                                     // 143DB
            smoke[0].count = 0;                                     // 143E2 CC[parent]
            smoke[1].count = 0;                                     // 143E8 DC[parent]
            // 143EE-1443D: with +2E set, drawn at the car (jsr ED50): not drawn here
            // 14443 jsr F579
            if (f.rate != 0 && (int8_t)(--f.count) <= 0)
            {
                f.count = f.rate;
                f.step++;
                uint32_t p = r32(ANIM + 4u * f.frame);
                int32_t v = (int32_t) r32(p + 4 + 4u * f.step);
                if (v <= 0)
                {
                    uint32_t target = r32(0xF5B8 + 4u * (uint32_t)(-v));
                    if (target == 0xF5C4) { f.rate = 0; f.step--; }                 // stop
                    else if (target == 0xF5FC) f.step = 0;                          // loop
                    else if (target == 0xF5CD)                                      // another frame
                    {
                        f.frame = (uint16_t) r32(p + 8 + 4u * f.step);
                        uint8_t b = r8_(r32(ANIM + 4u * f.frame));
                        f.rate = b; f.count = b; f.step = 0;
                    }
                }
            }
            if (f.rate != 0) break;                                 // 14449
            f.state--;                                              // 14456
            wheels_off = f.a;                                       // 14459
            break;
        default:
            break;
        }
    }
}

// $9F0A: the first piece of a frame's sub-frame, with a palette index (R12 frame, R13 palette, R14 sub,
// R17/R18 x/y, R8 scale); $9F22 the next piece of the same lists
uint16_t CarSprite::piece_start(uint16_t frame, uint16_t pal, uint8_t sub, int16_t x, int16_t y, uint16_t scale,
                                uint16_t mon, std::vector<uint16_t>& out)
{
    uint32_t p = r32(ANIM + 4u * frame);                            // 9F0A
    p = r32(p + 4 + 4u * sub);                                      // 9F12
    piece_ptr = p + 2;                                              // 9F17
    pal_ptr = r32(PAL_TABLE + 4u * pal);                            // 9F1A
    return piece_next(x, y, scale, mon, out);
}

uint16_t CarSprite::piece_next(int16_t x, int16_t y, uint16_t scale, uint16_t mon, std::vector<uint16_t>& out)
{
    uint16_t w[8];
    uint32_t& p = piece_ptr;
    w[0] = r16(p); p += 2;                                          // 9F22-9F2A
    uint32_t q = IMAGES + 4u * r16(p); p += 2;                      // 9F2D-9F30 movea.w: the definition's address
                                                                    // (12 bytes; the images are numbered in threes)
    w[1] = r16(q); q += 2;                                          // 9F38-9F40
    uint16_t addr = r16(q); q += 2;                                 // 9F43
    uint32_t v = (uint32_t) r16(q) * scale; q += 2;                 // 9F46-9F49
    w[2] = (uint16_t)(((v >> 8) & 0x3FF) | r16(q)); q += 2;          // 9F4C-9F5D
    v = (uint32_t) r16(q) * scale; q += 2;                          // 9F60-9F63
    w[3] = (uint16_t)(((v >> 8) & 0x3FF) | r16(q) | mon); q += 2;    // 9F66-9F7B
    v = (uint32_t)(int32_t)(int16_t) r16(p) * scale; p += 2;        // 9F7E-9F81
    w[4] = lo16((uint32_t)((int32_t) v >> 8) + (uint16_t) y);       // 9F84-9F90
    v = (uint32_t)(int32_t)(int16_t) r16(p) * scale; p += 2;        // 9F93-9F96
    w[5] = lo16((uint32_t)((int32_t) v >> 8) + (uint16_t) x);       // 9F99-9FA5
    w[6] = addr;                                                    // 9FA8
    w[7] = r16(pal_ptr); pal_ptr += 2;                              // 9FB0-9FB8
    out.insert(out.end(), w, w + 8);
    return w[0];
}

// mulu.h R8, Rn / sha.h #F8, Rn / addc.h #0, Rn: an offset scaled, rounded with the last bit shifted out
static inline uint16_t scale_off(uint16_t v, uint16_t scale)
{
    uint16_t t = (uint16_t)((uint32_t) v * scale);
    return (uint16_t)(((int16_t) t >> 8) + ((t >> 7) & 1));
}

// $9EDD: a piece placed by a position table (wheels, smoke): the body's offsets plus the table's entry for
// the body's image (16 bytes a row), scaled, at the car's point
void CarSprite::wheel_piece(uint32_t table, uint16_t frame, uint16_t pal, uint8_t sub, int16_t bx, int16_t by,
                            uint16_t rowoff, const SpriteInput& in, std::vector<uint16_t>& out)
{
    uint16_t r17 = (uint16_t) bx, r18 = (uint16_t) by;              // 9EDD-9EE0
    uint32_t r11 = table + rowoff;                                  // 9EE3
    if (r32(r11) == 0) return;                                      // 9EE6-9EE8
    r17 = (uint16_t)(r17 + r16(r11));                               // 9EEA
    r18 = (uint16_t)(r18 + r16(r11 + 2));                           // 9EED
    r17 = scale_off(r17, in.scale);                                 // 9EF0-9EFA
    r18 = scale_off(r18, in.scale);                                 // 9EF3-9F01
    r17 = (uint16_t)(r17 + (uint16_t) in.x);                        // 9F04
    r18 = (uint16_t)(r18 + (uint16_t) in.y);                        // 9F07
    piece_start(frame, pal, sub, (int16_t) r17, (int16_t) r18, in.scale, in.monitor, out);
}

// $9E8A: a wheel's smoke, from the wheel kinds
void CarSprite::smoke_piece(Smoke& sm, const Record& rec, int16_t bx, int16_t by, uint16_t rowoff,
                            const SpriteInput& in, std::vector<uint16_t>& out)
{
    uint8_t r1 = sm.count;                                          // 9E8A
    if (r1 == 0 && (rec.wheel[0] & sm.mask) == 0)                   // 9E8E-9E99
    {
        sm.count = 0;                                               // 9E7F
        sm.kind = 0;                                                // 9E84
        return;
    }
    uint8_t r0 = sm.kind;                                           // 9E9B
    uint8_t r2 = rec.wheel[sm.which];                               // 9E9F
    if (r2 != 0 && r0 != r2)                                        // 9EA3-9EAA
    {
        r0 = r2;                                                    // 9EAC
        sm.kind = r2;                                               // 9EAF
        r1 = 0;                                                     // 9EB3
    }
    uint8_t sub = r1;                                               // 9EB6
    r1++;                                                           // 9EB9
    if (r1 >= r8_(SMOKE_LEN + r0)) r1 = 0;                          // 9EBB-9EC5
    sm.count = r1;                                                  // 9EC8
    uint16_t frame = r16(sm.frames + 2u * r0);                      // 9ED0
    uint16_t pal = r16(SMOKE_PAL + 2u * r0);                        // 9ED5
    wheel_piece(sm.pos, frame, pal, sub, bx, by, rowoff, in, out);  // 9ECC, 9EDD
}

// $99B8 from $9AE0, the player's own car (BD[R20] = 0)
void CarSprite::draw(const Record& rec, SpriteInput& in, std::vector<uint16_t>& out)
{
    acc += rec.speed;                                               // 9AE0-9AE4 add.w R13, 88[R20]
    uint8_t t = (uint8_t)((acc >> 16) & 1);                         // 9AE9-9AEE
    if (t != (uint8_t)(acc >> 24))                                  // 9AF1 cmp.b R13, 8B[R20]
    {
        acc = (acc & 0x00FFFFFFu) | (uint32_t) t << 24;             // 9AF8 mov.b R13, 8B[R20]
        acc &= 0xFFFF0000u;                                         // 9AFD mov.h #0, 88[R20]
    }
    uint16_t r13 = (uint16_t)(t + in.braking + rec.pal);            // 9B03-9B07
    body_flags = piece_start(rec.frame, r13, rec.sub, in.x, in.y, in.scale, in.monitor, out);   // 9B0B-9B2C
    if (wheels_off) goto t9c9b;                                     // 9B31
    {
        uint16_t r1 = (uint16_t)(rec.frame - frame_base);           // 9B37-9B3E
        uint8_t shown = r8_(WHEELS_SHOWN + r1);                     // 9B43
        if (shown == 0)
        {
            wheels_off = 0xFF;                                      // 9B4C
            flag137 = 0xFF;                                         // 9B58 (BD = 0)
            goto t9c9b;
        }
        flag137 = 0;                                                // 9B61
        Wheel* w0 = &wheel[0];                                      // 9B67 R27
        Wheel* w1 = &wheel[1];                                      // 9B6C R28
        bool swap = car != 6 ? !(body_flags & 0x40) : (shown == 2 && !(body_flags & 0x40));   // 9B71-9B88
        if (swap) std::swap(w0, w1);
        uint8_t c0 = 0, c1 = 0;
        if (rec.speed != 0)                                         // 9B8B
        {
            c0 = w0->count < w0->limit ? (uint8_t)(w0->count + 1) : 1;  // 9B90-9BA2
            c1 = w1->count < w1->limit ? (uint8_t)(w1->count + 1) : 1;  // 9BA5-9BAF
        }
        w0->count = c0;                                             // 9BBA
        w1->count = c1;                                             // 9BBE
        uint32_t p = r32(ANIM + 4u * rec.frame);                    // 9BC2
        p = r32(p + 4 + 4u * rec.sub) + 2;                          // 9BCA-9BD3
        uint16_t r3 = r16(p);                                       // 9BD6
        uint16_t im = (uint16_t)(r16(p + 2) - img_base);            // 9BD9-9BDC
        im = (uint16_t)(im / 3);                                    // 9BE1
        r3 = (uint16_t)((uint16_t)(((r3 & 0x40) >> 6) * 0x34) + im);    // 9BE4-9BF2
        row = (uint8_t) r3;                                         // 9BF8
        uint16_t r4 = (uint16_t)(r3 << 4);                          // 9BF5, 9BFD
        int16_t by = (int16_t) r16(p + 4), bx = (int16_t) r16(p + 6);   // 9C00-9C03
        if (!(in.hide & w0->mask))                                  // 9C06-9C0F
            wheel_piece(w0->table, (uint16_t)(w0->frame + r3), w0->pal, c0, bx, by, r4, in, out);   // 9C11-9C20
        if (!(in.hide & w1->mask))                                  // 9C23-9C2C
            wheel_piece(w1->table, (uint16_t)(w1->frame + r3), w1->pal, c1, bx, by, r4, in, out);   // 9C2E-9C40
        Smoke* s0 = &smoke[0];                                      // 9C43 R27
        Smoke* s1 = &smoke[1];                                      // 9C48 R28
        if (!(body_flags & 0x40)) std::swap(s0, s1);                // 9C4D-9C55
        smoke_piece(*s0, rec, bx, by, r4, in, out);                 // 9C58-9C5C
        smoke_piece(*s1, rec, bx, by, r4, in, out);                 // 9C5F-9C62
        if (bf != 0 || rec.wheel[3] != 0)                           // 9C65-9C71
        {
            uint8_t sub = bf;                                       // 9C73
            uint8_t n = (uint8_t)(bf + 1);                          // 9C76
            if (n >= 5) n = 0;                                      // 9C78-9C7D
            bf = n;                                                 // 9C80
            piece_start(UNDER_FRAME, UNDER_PAL, sub, in.x, in.y, in.scale, in.monitor, out);   // 9C85-9C95
            piece_next(in.x, in.y, in.scale, in.monitor, out);      // 9C98
        }
    }
t9c9b:
    if (in.marker) return;                                          // 9C9B-9CA9 -> 9CCC: the player marker (not drawn)
    if (dust_count == 0)                                            // 9D19
    {
        if (in.crash_dust == 0) return;                             // 9D22-9D27
        in.crash_dust = 0;                                          // 9D2B
        dust_step = 0;                                              // 9D31
        dust_count = 0x1E;                                          // 9D37
        dust_y = (uint16_t) in.y;                                   // 9D3E
        dust_frame = 0x2F;                                          // 9D43
        dust_pal = 0x4B;                                            // 9D4B
        if (in.crash_snd == 0) { dust_frame = 0x30; dust_pal = 0x4C; }  // 9D53-9D61
    }
    dust_count--;                                                   // 9D69
    piece_start(dust_frame, dust_pal, (uint8_t)(dust_step >> 1), in.x, (int16_t) dust_y, 0x100, in.monitor, out);   // 9D6D-9D8D
    dust_step = (uint8_t)((dust_step + 1) & 7);                     // 9D90-9D94
}

// ------------------------------------------------------------------------------------------------
// The renderer
// ------------------------------------------------------------------------------------------------
static inline uint16_t pal_convert(uint16_t v)     // xBGRBBBBGGGGRRRR -> xBBBBBGGGGGRRRRR (segas32_v.cpp)
{
    int r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
    int g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
    int b = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
    return (uint16_t)((v & 0x8000) | (b << 10) | (g << 5) | (r << 0));
}

S32Sprites::S32Sprites(const uint8_t* r, const uint32_t* s, size_t words) : rom(r), spr(s), spr_words(words)
{
    auto r16 = [&](uint32_t a) { return (uint16_t)(rom[a & 0x1FFFFF] | rom[(a + 1) & 0x1FFFFF] << 8); };
    // palette RAM: $11881's blocks at power-on (+0 the destination in palette RAM's xBGR half, +2 the
    // length in bytes, then the data); the car's colours are in $153904 ($000-$9FF) and $3153C ($F00-$FFF)
    std::memset(pal, 0, sizeof pal);
    for (uint32_t blk : { 0x153904u, 0x3153Cu })
    {
        uint32_t dst = (r16(blk) >> 1) & 0x0FFF, n = r16(blk + 2) / 2;
        for (uint32_t i = 0; i < n && dst + i < 0x1000; i++) pal[dst + i] = pal_convert(r16(blk + 4 + 2 * i));
    }
    // sprite RAM $401000-$4033FF: the indirect palettes, copied at power-on from the ROM at $107020 ($10BC4)
    for (uint32_t i = 0; i < 0x1200; i++) ind[i] = r16(0x107020 + 2 * i);
}

bool S32Sprites::colour(uint16_t pix, uint32_t& rgb) const
{
    // mixer register $4C = $BE4D in a race: sprite groups from bits 12-13 (sprpixmask $0FFF), shadows by bit 15;
    // the groups' palette bases 0, no colour offsets
    if (!(pix & 0x8000)) return false;                              // a shadow (sprshadowmask)
    uint16_t p = pix & 0x0FFF;
    if ((p & 0x7FFE) == 0x0FFE) return false;                       // sprshadow
    uint16_t c = pal[p];
    rgb = (uint32_t)(c & 0x1F) << 19 | (uint32_t)((c >> 5) & 0x1F) << 11 | (uint32_t)((c >> 10) & 0x1F) << 3;
    return true;
}

void S32Sprites::draw(const uint16_t* e, int n, int xoffs, int yoffs, uint16_t* img, int w, int h) const
{
    for (int i = 0; i < n; i++)
    {
        const uint16_t* d = e + 8 * i;
        if ((d[0] >> 14) != 0) continue;                            // only sprites
        draw_one(d, xoffs, yoffs, img, w, h);
        if ((d[0] & 0x2000) && (d[0] & 0x1000)) i += 2;            // an inline indirect palette
    }
}

// segas32_v.cpp draw_one_sprite (MAME 0.264), for the image (its cliprect: the image)
static inline int sext12(uint16_t v) { return (int16_t)(uint16_t)(v << 4) >> 4; }

void S32Sprites::draw_one(const uint16_t* data, int xoffs, int yoffs, uint16_t* img, int W, int H) const
{
    const int indirect = data[0] & 0x2000, indlocal = data[0] & 0x1000;
    const int shadow = data[0] & 0x0800;            // with the sprite control's shadows on (latched $0A bit 0: on in a race)
    const int bpp8 = data[0] & 0x0200;
    const int transp = (data[0] & 0x0100) ? 0 : (bpp8 ? 0xff : 0x0f);
    const int flipy = data[0] & 0x0080, flipx = data[0] & 0x0040;
    const int offsety = data[0] & 0x0020, offsetx = data[0] & 0x0010;
    const int adjusty = (data[0] >> 2) & 3, adjustx = data[0] & 3;
    const int srch = data[1] >> 8;
    const int srcw = bpp8 ? (data[1] & 0x3f) : ((data[1] >> 1) & 0x3f);
    int bank = ((data[3] & 0x2000) >> 13) | ((data[3] & 0x8000) >> 14);
    const int dsth = data[2] & 0x3ff, dstw = data[3] & 0x3ff;
    int ypos = sext12(data[4]), xpos = sext12(data[5]);
    uint32_t addr = data[6] | ((uint32_t)(data[2] & 0xf000) << 4);
    const int color = 0x8000 | (data[7] & (bpp8 ? 0x7f00 : 0x7ff0));
    if (srcw == 0 || srch == 0 || dstw == 0 || dsth == 0) return;
    // transparency_masks[$08 & 3][$0A & 3] with the sprite control as the game sets it in a race: $0FFF
    int transmask = 0x0fff;
    if (bpp8) transmask &= 0xfff0;
    uint16_t indtable[16] = {};
    if (indirect)
    {
        for (int x = 0; x < 16; x++)
        {
            uint16_t v;
            if (indlocal) v = data[8 + x];
            else
            {
                uint32_t wi = 8u * (data[7] & 0x1fff) + (uint32_t) x;
                v = (wi >= 0x800 && wi < 0x800 + 0x1200) ? ind[wi - 0x800] : 0;
            }
            indtable[x] = (uint16_t)((v & (bpp8 ? 0xfff0 : 0xffff)) | 0x8000);
        }
    }
    const uint8_t numbanks = (uint8_t)(spr_words >> 20);
    if (numbanks) bank %= numbanks;
    const uint32_t* spritedata = spr + ((size_t) bank << 20);
    const uint32_t addrmask = 0xfffff;
    const int hzoom = (((bpp8 ? 4 : 8) * srcw) << 16) / dstw;
    const int vzoom = (srch << 16) / dsth;
    if (offsetx) xpos += xoffs;
    switch (adjustx) { case 0: case 3: xpos -= (dstw - 1) / 2; break; case 1: xpos -= dstw - 1; break; default: break; }
    if (offsety) ypos += yoffs;
    switch (adjusty) { case 0: case 3: ypos -= (dsth - 1) / 2; break; case 1: ypos -= dsth - 1; break; default: break; }
    int xdelta = 1, ydelta = 1;
    if (flipx) { xpos += dstw - 1; xdelta = -1; }
    if (flipy) { ypos += dsth - 1; ydelta = -1; }
    int xtarget = xpos + xdelta * dstw;
    const int ytarget = ypos + ydelta * dsth;
    if (xdelta > 0 && xtarget > W - 1) { xtarget = W; if (xpos >= xtarget) return; }
    if (xdelta < 0 && xtarget < 0) { xtarget = -1; if (xpos <= xtarget) return; }
    int yacc = 0;
    auto put = [&](int x, uint16_t* dest, int pix, int trans) {
        if (x < 0 || x >= W || pix == trans) return;
        if (!indirect)
        {
            if (pix != 0) { if (!shadow) dest[x] = (uint16_t)(color | pix); else dest[x] = dest[x] ? (uint16_t)(dest[x] & 0x7fff) : 2; }
        }
        else
        {
            int indpix = bpp8 ? ((indtable[pix >> 4]) | (pix & 0x0f)) : indtable[pix];
            if ((indpix & transmask) != transmask)
            {
                if (!shadow) dest[x] = (uint16_t) indpix;
                else dest[x] = dest[x] ? (uint16_t)(dest[x] & 0x7fff) : 2;
            }
        }
    };
    for (int y = ypos; y != ytarget; y += ydelta)
    {
        if (y >= 0 && y < H)
        {
            uint16_t* const dest = img + (size_t) y * W;
            int xacc = 0;
            uint32_t curaddr = addr - 1;
            for (int x = xpos; x != xtarget; )
            {
                const uint32_t pixels = spritedata[++curaddr & addrmask];
                const int npix = bpp8 ? 4 : 8;
                int pix = 0;
                for (int k = 0; k < npix; k++)
                {
                    pix = bpp8 ? (int)((pixels >> (24 - 8 * k)) & 0xff) : (int)((pixels >> (28 - 4 * k)) & 0xf);
                    const int trans = (k == 0 || k == npix - 1) ? transp : 0;
                    while (xacc < 0x10000 && x != xtarget) { put(x, dest, pix, trans); x += xdelta; xacc += hzoom; }
                    xacc -= 0x10000;
                }
                if (transp != 0 && pix == (bpp8 ? 0xff : 0x0f)) break;
            }
        }
        yacc += vzoom;
        addr += (uint32_t)(srcw * (yacc >> 16));
        yacc &= 0xffff;
    }
}

} // namespace orcar
