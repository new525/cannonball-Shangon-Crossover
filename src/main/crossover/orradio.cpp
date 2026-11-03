/***************************************************************************
    OutRunners' radio. See orradio.hpp; docs/OUTRUNNERS-RE.md section 14 has the code.
***************************************************************************/
#include "crossover/orradio.hpp"

namespace orradio
{

// $19698: (command << 24 | title record); the records' x offset and text
const Song SONGS[11] = {
    { 0x01, 0x196E4,  72, "` Magical Sound Shower" },
    { 0x02, 0x196FE,  96, "` Passing Breeze" },
    { 0x03, 0x19712, 108, "` Splash Wave" },
    { 0x04, 0x19722,  80, "` Picture the Rivers" },
    { 0x05, 0x1973A,  96, "` Blow Your Cool" },
    { 0x06, 0x1974E,  60, "` Looking for the Rainbow" },
    { 0x07, 0x1976A, 112, "` Speed King" },
    { 0x08, 0x1977A, 116, "` Adventure" },
    { 0x0E, 0x19788, 100, "` Sonic Control" },
    { 0x12, 0x1979A, 116, "` Last Wave" },
    { 0x0D, 0x197A8, 108, "` Jingle Bell" },
};
const Song MEGA_RADIO = { 0x00, 0x197B8, 88, "Mega Radio Station" };

// $1968D: by car (0 MAD POWER .. 7)
const uint8_t START_SONG[8] = { 2, 0, 6, 3, 4, 8, 5, 7 };

// $196C4: by route node 0-30
const uint8_t NODE_STATION[31] = {
    0xF3, 0xEC, 0xF1, 0xE8, 0xF2, 0xED, 0xE9, 0xE7, 0xF0, 0xE1, 0xEA, 0xE4, 0xE6, 0xE5, 0xEF, 0xE2,
    0xE3, 0xE3, 0xE2, 0xEF, 0xE5, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
};

// $197DD: B[R20] at the end of the branch's announcement, by stage $2080A4: '0' '0' '0' '0' '.'; the
// bytes after them (stage 5, Kenya: not reached, as Kenya has no branch) are $197E2's first ones
static const uint8_t STAGE_VOICE[8] = { 0x30, 0x30, 0x30, 0x30, 0x2E, 0x64, 0x99, 0x01 };

// $197E2: entries 0-$30 (those the radio can say: 0, 1-8 the cars, 9-$10 the places, $11-$20 the
// remarks by place, $29 the station, $2A-$2F, $30 and $2E at the branch)
const Voice VOICES[0x31] = {
    { 0xB0, 0x19964, 114 }, { 0x76, 0x199D7, 15 }, { 0x7A, 0x199E7, 15 },
    { 0x75, 0x199F7, 15 }, { 0x79, 0x19A07, 18 }, { 0x74, 0x19A1A, 11 },
    { 0x78, 0x19A26, 19 }, { 0x77, 0x19A3A, 13 }, { 0x7B, 0x19A48, 17 },
    { 0x98, 0x19A5A, 17 }, { 0x99, 0x19A6C, 17 }, { 0x9A, 0x19A7E, 17 },
    { 0x9B, 0x19A6C, 17 }, { 0x9C, 0x19A5A, 17 }, { 0x9D, 0x19A90, 17 },
    { 0x9E, 0x19AA2, 17 }, { 0x9F, 0x19AB4, 17 }, { 0xA0, 0x19AC6, 32 },
    { 0xA1, 0x19AE7, 32 }, { 0xA2, 0x19B08, 32 }, { 0xA3, 0x19AE7, 32 },
    { 0xA4, 0x19AC6, 32 }, { 0xA5, 0x19B29, 32 }, { 0xA6, 0x19B4A, 32 },
    { 0xA7, 0x19B6B, 32 }, { 0xA8, 0x19B8C, 42 }, { 0xA9, 0x19BB7, 42 },
    { 0xAA, 0x19BE2, 42 }, { 0xAB, 0x19BB7, 42 }, { 0xAC, 0x19B8C, 42 },
    { 0xAD, 0x19C0D, 42 }, { 0xAE, 0x19C38, 42 }, { 0xAF, 0x19C63, 42 },
    { 0x90, 0x19C8E, 26 }, { 0x91, 0x19CA9, 26 }, { 0x92, 0x19CC4, 26 },
    { 0x93, 0x19CA9, 26 }, { 0x94, 0x19C8E, 26 }, { 0x95, 0x19CDF, 26 },
    { 0x96, 0x19CFA, 26 }, { 0x97, 0x19D15, 26 }, { 0x82, 0x19D30, 51 },
    { 0x83, 0x19D64, 18 }, { 0x86, 0x19D77,  9 }, { 0x88, 0x19D81, 26 },
    { 0x8A, 0x19D9C, 19 }, { 0x8B, 0x19DB0, 59 }, { 0x8C, 0x19DEC, 22 },
    { 0x8D, 0x19E03, 11 },
};

// $198D8: the runs of remarks, chosen when the random number & $3FF is below 14
static const uint32_t SEQ_START[14] = {
    0x19910, 0x19910, 0x19910, 0x19910, 0x19914, 0x19914, 0x19920, 0x19920,
    0x1992C, 0x1992C, 0x19938, 0x1993C, 0x19948, 0x19954,
};
// $19910-$19963: the runs (remark routines, 0 ends a run)
static const uint32_t SEQ_BASE = 0x19910;
static const uint32_t SEQ_WORDS[21] = {
    0x1950A, 0x1951D, 0x19528, 0, 0x1950A, 0x19528, 0, 0x1950A, 0x1951D, 0,
    0x19544, 0x1953D, 0x19536, 0, 0x19544, 0x1952F, 0, 0x19544, 0x19536, 0x1952F, 0,
};
static uint32_t seq_word(uint32_t addr) { return SEQ_WORDS[(addr - SEQ_BASE) >> 2]; }

// $120B1: the random number generator (state $208F00)
uint16_t Radio::rand()
{
    uint32_t s = rng;
    if ((s & 0xFFFF) == 0) s ^= 0x2A6D365B;               // test.h R1; xor.w #2A6D365B
    uint32_t m = s * 41;                                     // the adds: x4 + x, x8 + x
    uint16_t lo = (uint16_t)((m & 0xFFFF) + (m >> 16));      // mov.h R1, R0; rot; add.h R1, R0
    rng = ((uint32_t) lo << 16) | (m & 0xFFFF);              // mov.h R0, R1; rot: $208F00
    return lo;                                               // R0's low half (the caller masks it)
}

// $1214A: the queue's write index $208801, 32 entries
void Radio::queue(uint8_t cmd)
{
    q_src[q_write] = 0;
    q[q_write] = cmd;
    q_write = (uint8_t)((q_write + 1) & 0x1F);
    queued.push_back(cmd);
}

// $3F8DA (player 1's half of the frame interrupt): one command a tick
int Radio::drain()
{
    if (q_read == q_write) return -1;
    uint8_t cmd = q[q_read];
    drained_external = q_src[q_read] != 0;
    q_read = (uint8_t)((q_read + 1) & 0x1F);
    return cmd;
}

// $1966E: a voice: its command, and its script for the voice clock ($208F30)
void Radio::say(int v)
{
    if (v < 0 || v > 0x30) return;                           // (beyond the radio's voices: not reached)
    f30 = VOICES[v].script;
    f30_end = VOICES[v].script + VOICES[v].len;
    queue(VOICES[v].cmd);
}

// $19561 / $19576: a new title object ($11FAF: appended to the object list, so it runs this tick
// after the others); the one before it (18[R20]) is told to go ($19569: 10 := 0)
void Radio::new_title(const Song* s)
{
    if (title >= 0)
        for (Title& t : titles) if (t.id == title) t.timer = 0;
    Title t;
    t.song = s;
    t.timer = 0x3C;                                          // $19586
    t.id = ++title_ids;
    titles.push_back(t);
    title = t.id;
}

// $19551: the song's title; its command
uint8_t Radio::song_title(int index)
{
    new_title(&SONGS[index]);
    return SONGS[index].cmd;
}

// $191E0: the object's first tick
void Radio::init(const Race& race, uint8_t node)
{
    race_ = race;
    node_ = node;
    routine = R_INIT;
}

void Radio::tick(const In& in)
{
    queued.clear();
    volume.clear();
    if (in.f0f_set) f0f = true;

    switch (routine)
    {
        case R_INIT:
            // $191E0: 80[R20] := $FF x 8; the cars to introduce from 1F[$208308] (here given)
            for (int i = 0; i < 8; i++) order[i] = race_.order[i];
            left = race_.entries;                            // 88[R20]
            at = 0;
            own_place = race_.own_place;                     // 9[R20]
            place_of_first = race_.place_of_first;
            f0f = false;                                     // $19220
            song = START_SONG[race_.car & 7];                // $1922A: $209014
            a = node_;                                       // $19232
            routine = R_WAIT;
            // fall through
        case R_WAIT:
            if (in.countdown) break;                         // $1923F: 10[$200400] != 0
            routine = R_RUN;
            // fall through
        case R_RUN:
            edges = in.edges;                                // $1924C: 118[R25]
            if (in.over)                                     // $209181 | $209182
            {
                edges = 0;
                d = 5;
                routine = R_OVER;                            // $19270: from now $19276
            }
            // fall through
        case R_OVER:
            run(in);
            break;
    }

    // the title objects, after the radio in the object list
    for (Title& t : titles) if (t.active) title_tick(t);
}

// $19276 - $19550
void Radio::run(const In& in)
{
    // $19276: the title object 18[R20] gone
    if (title >= 0)
    {
        bool alive = false;
        for (const Title& t : titles) if (t.id == title && t.active) alive = true;
        if (!alive) title = -1;
    }
    // inactive title objects leave the object list
    for (size_t i = 0; i < titles.size();) { if (!titles[i].active) titles.erase(titles.begin() + i); else i++; }

    // $19289: the branch's slow-down flag is cleared once the node has changed
    if (f0f && in.node != a)
    {
        a = in.node;
        f0f = false;
    }

    switch (c)
    {
        case 0:                                              // $192D4: the song
            queue(song_title(song));
            c = 1;
            // fall through
        case 1:                                              // $192E4: the buttons
        {
            const uint16_t e = edges;
            if (e & 0x100)                                   // DJ/MUSIC: $192FD
            {
                c = 2;
                queue(0x1F);
                return;
            }
            if (e & 0x200)                                   // <<: $1930C
            {
                if (e & 0x400)                               // and >>: Jingle Bell ($1934F)
                {
                    queue(song_title(JINGLE_BELL));
                    return;
                }
                queue(0x1F);
                int s = (int) song - 1;
                if (s < 0) s = SONG_COUNT - 1;               // bp: >= 0
                song = (uint8_t) s;
                queue(song_title(song));
                return;
            }
            if ((e & 0x400) || (e & 0x40))                   // >> or START: $19329
            {
                queue(0x1F);
                int s = (int) song + 1;
                if (s >= SONG_COUNT) s = 0;                  // blt
                song = (uint8_t) s;
                queue(song_title(song));
            }
            return;
        }
        case 2:                                              // $1935C: Mega Radio Station
            new_title(&MEGA_RADIO);
            b = 0x29;
            if (f0f) { goto branch_e0; }
            queue(NODE_STATION[in.node]);
            c = 3;
            // fall through
        case 3:                                              // $19384
            if (!f0f) break;
            volume.push_back(0x3F);                          // $12256: $700918
            timer = 0x3C;
            c = 4;
            // fall through
        case 4:                                              // $1939E
            if (--timer != 0) break;
        branch_e0:                                           // $193A3
            queue(0xE0);
            timer = 0xB4;
            c = 5;
            // fall through
        case 5:                                              // $193B8
            if (--timer != 0) break;
            c = 6;
            // fall through
        case 6:                                              // $193C0
            if (!f0f) goto station;
            queue(0xEE);
            c = 7;
            // fall through
        case 7:                                              // $193D3
            if (f0f) break;
            volume.push_back(0x7F);
            timer = 0x78;
            c = 8;
            // fall through
        case 8:                                              // $193EC
            if (--timer != 0) break;
        station:                                             // $193F1
            b = STAGE_VOICE[in.stage & 7];
            queue(NODE_STATION[in.node]);
            c = 3;
            break;
        default:
            break;
    }
    tail(in);
}

// $19414: in Mega Radio Station's states
void Radio::tail(const In& in)
{
    (void) in;
    if ((edges >> 8) & 7)                                    // a button: back to the song
    {
        c = 0;
        d = 4;
        f30 = 0;
        queue(0x1F);
        return;
    }
    if (f30 != 0)                                            // a voice is being said
    {
        b = 0;
        return;
    }
    switch (d)
    {
        case 0:                                              // $19464: the DJ's greeting
            say(0);
            d = 1;
            break;
        case 1:                                              // $19471: the next car's place
        {
            int r = at;
            while (r < 10 && (r_order((uint8_t) r) & 0x80)) r++;
            at = (uint8_t) r;
            say(r + 9);
            d = 2;
            break;
        }
        case 2:                                              // $19493: its name
            say((r_order(at) + 1) & 0xFF);
            at++;
            if (--left == 0) d = 3; else d = 1;
            break;
        case 3:                                              // $194B5
            say(0x2A);
            d = 4;
            break;
        case 4:                                              // $194C2
        {
            if (b != 0)                                      // $19502
            {
                seq = 0;
                say(b);
                break;
            }
            if (c != 3) break;
            uint32_t p = seq;
            if (p == 0)
            {
                uint16_t r = (uint16_t)(rand() & 0x3FF);
                if (r >= 14) break;
                p = SEQ_START[r];
            }
            uint32_t routine_ = seq_word(p);
            p += 4;
            if (seq_word(p) == 0) p = 0;
            seq = p;
            remark(routine_);
            break;
        }
        default:                                             // 5: $19550
            break;
    }
}

// 80[R20](n): the list, then (past its 8 places) 88 and 89[R20], as the original reads them
uint8_t Radio::r_order(uint8_t i) const { return i < 8 ? order[i] : i == 8 ? left : i == 9 ? at : 0; }

// $1950A-$19548: the remarks
void Radio::remark(uint32_t r)
{
    switch (r)
    {
        case 0x1950A: say((place_of_first + 0x11) & 0xFF); break;
        case 0x1951D: say((own_place + 0x19) & 0xFF); break;
        case 0x19528: say(0x2A); break;
        case 0x1952F: say(0x2B); break;
        case 0x19536: say(0x2D); break;
        case 0x1953D: say(0x2F); break;
        case 0x19544: say(0x2C); break;
        default: break;
    }
}

// $1958E: a title object's tick
void Radio::title_tick(Title& t)
{
    t.drawn = false;
    switch (t.c)
    {
        case 0:                                              // $195B0
            t.xoff = t.song->x;
            t.x = 0xD2;
            t.speed = 0x14;
            t.c = 1;
            // fall through
        case 1:                                              // $195D5: in, slowing
            if (t.timer != 0)
            {
                t.x = (int16_t)(t.x - t.speed);
                t.speed = (int16_t)(t.speed - 1);
                if (t.x != 0) break;
            }
            t.c++;                                           // $195F3
            // fall through
        case 2:                                              // $195F6: held
            t.timer = (uint16_t)(t.timer - 1);
            if ((int16_t) t.timer > 0) break;
            if ((int16_t) t.timer < 0) { t.c = (uint8_t)(t.c + 2); goto out_fast; }
            t.c++;
            // fall through
        case 3:                                              // $19600: out, faster
            t.speed = (int16_t)(t.speed + 1);
            t.x = (int16_t)(t.x - t.speed);
            if (t.x <= -210) { t.active = false; return; }
            break;
        case 4:                                              // $19626: out, replaced
        out_fast:
            t.x = (int16_t)(t.x - 0x28);
            if (t.x <= -210) { t.active = false; return; }
            break;
        default:
            return;
    }
    t.d++;                                                   // $19647: the colour cycles
    t.drawn = true;
}

// $18C45 (its $208F30 part), after the objects: the voice's script, a byte every 3 ticks
void Radio::voice_clock()
{
    if (!f35) return;
    if (f30 == 0) { f34 = 3; return; }
    if (--f34 != 0) return;
    if (f30 >= f30_end) f30 = 0;                             // the negative byte: the voice is over
    else f30++;
    f34 = 3;
}

void Radio::set_voice(uint32_t script_pos, uint32_t script_end, uint8_t clock)
{
    f30 = script_pos;
    f30_end = script_end;
    f34 = clock;
}

} // namespace orradio
