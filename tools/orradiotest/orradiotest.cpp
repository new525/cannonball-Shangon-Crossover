// orradiotest: OutRunners' radio (crossover/orradio, the model) against its original on the emulated
// board (crossover/orboard), tick by tick.
//
// The board runs from power-on (fresh EEPROM) into a race with MAD POWER (tools/ortrace's race_mp.inc:
// coin, START, the car select, the automatic box, full accelerator from frame 2900) and the radio's
// buttons are pressed on player 1's port (P2_A: bit 0 DJ/MUSIC, 1 <<, 2 >>; START on SERVICE12_A) at
// the scenario's frames. Instruction hooks:
//   $11F3E / $11F41 with R20 = the radio object ($201000): the radio's tick. At its entry the model
//     takes what the original reads from the rest of the game (In: $208118, $209181/2, $2080A1,
//     $2080A4, 10[$200400], $208F0F) and runs its tick; at its return the radio's state and the
//     commands it queued (and its $700918 writes) are compared;
//   $1214A (the queue's write, R25 = $208000) outside the radio's tick: another part of the game
//     queues a command (the car's, the race's voices): given to the model's queue, so that the two
//     queues hold the same commands;
//   $3F8DA (the frame interrupt's drain, R25 = $208000): the command the board sends to $700700 now
//     and the model's drain() are compared;
//   $18C45 / $18C9F (after the tick's objects): the model's voice clock; then the title objects and
//     the voice clock ($208F30, $208F34) are compared;
//   $18D52 ($18C45's return): the LCD's buffer $208F10-$208F1F and the title's scroll ($208F44,
//     $208F48, $208F49) are compared. Before $18C45 the model is given what the rest of the game
//     wrote: its text between titles (the time, the speed: $18E1E) and its own titles ($208F40 set
//     by another title object).
// At every VBLANK the display that the port G writes drive (ORBoard::lcd, crossover/orlcd.hpp) is
// compared with the buffer of the frames before, and at the end its character generator RAM with the
// ROM's $18DBA. With -screen the left screen is drawn while a title of the radio's is up, and each
// frame is compared with the model's titles drawn from the ROMs (crossover/orradiogfx: the font, the
// palette of the tick, the position), against the model's last ticks.
// What the rest of the game changes in the radio's shared variables is taken from the board before
// the radio's tick and counted: the random state $208F00 (6 callers), $208F30 when another voice
// (the race's) was started.
//
// usage: orradiotest <orunnersj rom dir> [scenario] [-frames n] [-v] [-log file] [-screen]
//   scenarios: buttons (default): >> x3, START, << x3 (the wrap), << and >> together, DJ at the
//              branch (the slow-down's announcement), back, DJ again, ... to GAME OVER;
//              dj: DJ from the start to GAME OVER (the introduction, the remarks);
//              branch: DJ from the start, the car stopped in the branch's slow-down (the station's
//              announcement $EE, the wait for the branch);
//              two: DJ from the start in a two-player race (the introduction of two cars);
//              fast: presses on consecutive ticks.
#include "crossover/orboard.hpp"
#include "crossover/orradio.hpp"
#include "crossover/orradiogfx.hpp"
#include "crossover/v60/v60d.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace
{

ORBoard* B = nullptr;
orradio::Radio M;
bool model_on = false;
bool in_radio = false;
uint64_t frame_no = 0;                  // ortrace's frame numbering: frame f runs after the (f-1)-th VBLANK
bool verbose = false;
FILE* logf = nullptr;

const uint32_t OBJ = 0x201000;          // the radio object (player 1)

uint8_t rb(uint32_t a) { return B->read8(a); }
uint16_t rw(uint32_t a) { return (uint16_t)(rb(a) | rb(a + 1) << 8); }
uint32_t rl(uint32_t a) { return rw(a) | (uint32_t) rw(a + 2) << 16; }

int ticks = 0, diffs = 0, max_diffs = 30;
int rng_syncs = 0, voice_syncs = 0, f0f_sets = 0, inits = 0;
int sent_board = 0, sent_radio = 0, titles_checked = 0;
std::vector<uint8_t> board_queued, board_volume;
std::vector<uint32_t> radio_objs;       // the board's title objects created by the radio ($1957C in its tick)
int other_titles = 0;
std::map<uint32_t, uint32_t> cover;     // instruction -> count (the transcribed routines)
const uint8_t* ROM = nullptr;           // the program ROM by address
int lcd_ticks = 0, lcd_title_ticks = 0, lcd_game_text = 0, lcd_game_titles = 0, lcd_mouth_ticks = 0;

// -screen: the model's titles after each of its last ticks, and the frames compared
orradio::Gfx gfx;
bool screen_check = false;
struct Shown { const char* text; int x, palette; bool held; };
std::vector<std::vector<Shown>> shown_ticks;        // newest last
int scr_frames = 0, scr_exact = 0, scr_none = 0, scr_held = 0, scr_held_exact = 0;
long scr_ok = 0, scr_bad = 0;
int scr_palette[8] = {};
std::map<int, int> scr_lag;                         // ticks back -> frames
std::vector<uint8_t> lcd_hist[8];                   // $208F10-$208F1F at the last VBLANKs
int lcd_frames = 0, lcd_frame_other = 0;
std::map<int, int> lcd_lag;

void diff(const char* what, long b, long m)
{
    diffs++;
    if (diffs <= max_diffs) printf("DIFF frame %llu tick %d: %s board %lx model %lx\n", (unsigned long long) frame_no, ticks, what, b, m);
}

// $191E0's list of cars, from the race as the original reads it ($191ED-$19219)
orradio::Race race_from_board()
{
    orradio::Race r;
    r.car = rb(0x209060);
    uint32_t list = rl(0x208308) + 0x1F;
    r.entries = rb(list);
    for (int i = 0; i < 8; i++) r.order[i] = 0xFF;
    for (int k = 0; k < r.entries; k++)
    {
        uint8_t carno = rb(list + 1 + k);
        r.order[rb(0x20E030 + carno) & 7] = rb(0x20FC49 + carno * 0x80);
    }
    r.own_place = rb(0x20E030 + rb(0x208001));
    r.place_of_first = rb(0x20E030 + rb(rl(0x208308) + 0x20));
    return r;
}

uint32_t script_end(uint32_t p)
{
    while (rb(p) < 0x80) p++;
    return p;
}

void radio_entry()
{
    uint32_t routine = rl(OBJ + 4) & 0xFFFFFF;
    if (routine != 0x191E0 && routine != 0x1923F && routine != 0x1924C && routine != 0x19276)
    {
        model_on = false;                       // the race is over: the object is something else's
        return;
    }
    if (routine == 0x191E0)
    {
        M = orradio::Radio();
        M.init(race_from_board(), rb(0x2080A1));
        model_on = true;
        inits++;
    }
    if (!model_on) return;
    ticks++;
    orradio::In in;
    in.edges = rw(0x208118);
    in.over = (rb(0x209181) | rb(0x209182)) != 0;
    in.node = rb(0x2080A1);
    in.stage = rb(0x2080A4);
    in.countdown = rw(0x200410) != 0;
    bool f0f = rb(0x208F0F) != 0;
    if (f0f && !M.f0f) { in.f0f_set = true; f0f_sets++; }
    if (!f0f && M.f0f) diff("F0F cleared by another", 0, 1);
    // shared with the rest of the game
    uint32_t rng = rl(0x208F00);
    if (rng != M.rng) { rng_syncs++; M.rng = rng; }
    uint32_t f30 = rl(0x208F30);
    if (f30 != M.f30 || rb(0x208F34) != M.f34)
    {
        diff("$208F30 at the radio's tick", f30, M.f30);
        M.set_voice(f30, f30 ? script_end(f30) : 0, rb(0x208F34));
    }
    M.f35 = rb(0x208F35) != 0;
    M.rom = ROM;
    board_queued.clear();
    board_volume.clear();
    in_radio = true;
    M.tick(in);
}

void radio_return()
{
    in_radio = false;
    if (!model_on) return;
    const uint32_t o = OBJ;
    auto cmp = [](const char* w, long b, long m) { if (b != m) diff(w, b, m); };
    cmp("C", rb(o + 0xC), M.c);
    cmp("D", rb(o + 0xD), M.d);
    cmp("B", rb(o + 0xB), M.b);
    cmp("A", rb(o + 0xA), M.a);
    cmp("10", rb(o + 0x10), M.timer);
    cmp("2C", rw(o + 0x2C), M.edges);
    cmp("38", rl(o + 0x38), M.seq);
    for (int i = 0; i < 8; i++) cmp("80+i", rb(o + 0x80 + i), M.order[i]);
    cmp("88", rb(o + 0x88), M.left);
    cmp("89", rb(o + 0x89), M.at);
    cmp("9", rb(o + 0x9), M.own_place);
    cmp("$209014", rb(0x209014), M.song);
    cmp("$208F0F", rb(0x208F0F) != 0, M.f0f);
    cmp("18 (title)", rl(o + 0x18) != 0, M.title >= 0);
    cmp("$208F00", rl(0x208F00), M.rng);
    cmp("routine", rl(o + 4) & 0xFFFFFF, M.routine == orradio::Radio::R_INIT ? 0x191E0 : M.routine == orradio::Radio::R_WAIT ? 0x1923F :
                                        M.routine == orradio::Radio::R_RUN ? 0x1924C : 0x19276);
    if (board_queued != M.queued)
    {
        std::string b, m;
        for (uint8_t v : board_queued) { char t[8]; snprintf(t, sizeof t, "%02x ", v); b += t; }
        for (uint8_t v : M.queued) { char t[8]; snprintf(t, sizeof t, "%02x ", v); m += t; }
        diffs++;
        if (diffs <= max_diffs) printf("DIFF frame %llu: queued board [%s] model [%s]\n", (unsigned long long) frame_no, b.c_str(), m.c_str());
    }
    if (board_volume != M.volume) diff("$700918 writes", (long) board_volume.size(), (long) M.volume.size());
    if (verbose && !M.queued.empty())
    {
        printf("frame %llu: radio queues", (unsigned long long) frame_no);
        for (uint8_t v : M.queued) printf(" %02x", v);
        printf(" (C %d D %d song %d)\n", M.c, M.d, M.song);
    }
}

void after_voice_clock()
{
    if (!model_on) return;
    if (rl(0x208F30) != M.f30) diff("$208F30", rl(0x208F30), M.f30);
    if (rb(0x208F34) != M.f34) diff("$208F34", rb(0x208F34), M.f34);
    // the title objects: player 1's object slots ($200400 + $100 n, n < $7C) running $1958E
    struct T { int c, d, timer, speed, x, xoff; bool operator<(const T& o) const { return std::tie(x, c, timer, d) < std::tie(o.x, o.c, o.timer, o.d); }
               bool operator!=(const T& o) const { return c != o.c || d != o.d || timer != o.timer || speed != o.speed || x != o.x || xoff != o.xoff; } };
    std::vector<T> bt, mt;
    // (the game's other titles, $3482, $5307, $CBF8, are not the radio's)
    for (size_t i = 0; i < radio_objs.size();)
    {
        uint32_t a = radio_objs[i];
        if ((rb(a) & 1) && (rl(a + 4) & 0xFFFFFF) == 0x1958E)
        {
            bt.push_back({ rb(a + 0xC), rb(a + 0xD), rw(a + 0x10), (int16_t) rw(a + 0x12), (int16_t) rw(a + 0x20), (int16_t) rw(a + 0x30) });
            i++;
        }
        else radio_objs.erase(radio_objs.begin() + i);
    }
    for (const orradio::Title& t : M.titles)
        if (t.active) mt.push_back({ t.c, t.d, t.timer, t.speed, t.x, t.xoff });
    std::sort(bt.begin(), bt.end()); std::sort(mt.begin(), mt.end());
    titles_checked += (int) bt.size();
    bool same = bt.size() == mt.size();
    for (size_t i = 0; same && i < bt.size(); i++) if (bt[i] != mt[i]) same = false;
    if (!same)
    {
        diffs++;
        if (diffs <= max_diffs)
        {
            printf("DIFF frame %llu: titles board", (unsigned long long) frame_no);
            for (auto& t : bt) printf(" [C%d D%d t%d s%d x%d+%d]", t.c, t.d, t.timer, t.speed, t.x, t.xoff);
            printf(" model");
            for (auto& t : mt) printf(" [C%d D%d t%d s%d x%d+%d]", t.c, t.d, t.timer, t.speed, t.x, t.xoff);
            printf("\n");
        }
    }
}

// Before $18C45: what the rest of the game wrote to the LCD's variables since the model's last tick
void before_lcd()
{
    if (!model_on) return;
    const uint32_t f40 = rl(0x208F40);
    const char* bt = f40 ? (const char*) ROM + f40 : nullptr;
    if (bt && (!M.f40 || strcmp(M.f40, bt) != 0)) { M.f40 = bt; lcd_game_titles++; }     // another title object's
    else if (!bt && M.f40) diff("$208F40 not set", 0, 1);
    const bool title = rl(0x208F44) != 0;
    if (title != M.lcd_title()) diff("$208F44 before $18C45", title, M.lcd_title());
    uint8_t buf[orradio::LCD_CHARS];
    for (int i = 0; i < orradio::LCD_CHARS; i++) buf[i] = rb(0x208F10 + i);
    if (memcmp(buf, M.lcd, sizeof buf))
    {
        if (M.lcd_title()) diff("the LCD written during a title", 0, 0);
        memcpy(M.lcd, buf, sizeof buf);
        lcd_game_text++;
    }
}

// $18D52: $18C45 returns
void after_lcd()
{
    if (!model_on) return;
    lcd_ticks++;
    if (M.lcd_title()) lcd_title_ticks++;
    if (M.f30) lcd_mouth_ticks++;
    for (int i = 0; i < orradio::LCD_CHARS; i++)
        if (rb(0x208F10 + i) != M.lcd[i]) { diff("LCD character", rb(0x208F10 + i), M.lcd[i]); break; }
    if ((rl(0x208F44) != 0) != M.lcd_title()) diff("$208F44", rl(0x208F44), M.lcd_title());
    if (M.lcd_title())
    {
        if (rb(0x208F48) != M.f48) diff("$208F48", rb(0x208F48), M.f48);
        if (rb(0x208F49) != M.f49) diff("$208F49", rb(0x208F49), M.f49);
        if (M.f44_spaces && rl(0x208F44) != M.f44_spaces) diff("$208F44 (spaces)", rl(0x208F44), M.f44_spaces);
    }
    // the model's titles as drawn this tick, for -screen
    std::vector<Shown> v;
    for (const orradio::Title& t : M.titles)
        if (t.active && t.drawn) v.push_back({ t.song->text, t.screen_x(), t.palette(), t.c == 2 });
    shown_ticks.push_back(v);
    if (shown_ticks.size() > 6) shown_ticks.erase(shown_ticks.begin());
}

// At a VBLANK: the display the port G writes drive against the buffer of the frames before
void lcd_frame()
{
    std::vector<uint8_t> now(orradio::LCD_CHARS);
    for (int i = 0; i < orradio::LCD_CHARS; i++) now[i] = rb(0x208F10 + i);
    for (int k = 7; k > 0; k--) lcd_hist[k] = lcd_hist[k - 1];
    lcd_hist[0] = now;
    if (!model_on) return;                          // (the buffers are kept from power-on, compared in a race)
    lcd_frames++;
    for (int k = 0; k < 8; k++)
        if (lcd_hist[k].size() == now.size() && !memcmp(lcd_hist[k].data(), B->lcd[0].ddram, now.size())) { lcd_lag[k]++; return; }
    lcd_frame_other++;
    if (verbose) printf("frame %llu: the display is none of the last 8 frames' buffers\n", (unsigned long long) frame_no);
}

// At a VBLANK with -screen: the frame just drawn against the model's titles of its last ticks
void screen_frame()
{
    const uint32_t* px = B->screen_pixels(0);
    const int w = B->screen_width(0);
    int best = -1; long best_ok = -1, best_bad = 0; int best_pal = 0, best_x0 = 0, best_x1 = 0;
    for (int k = (int) shown_ticks.size() - 1; k >= 0; k--)
    {
        long ok = 0, bad = 0;
        int bx0 = 9999, bx1 = -1;                   // the columns of the pixels that differ
        for (const Shown& t : shown_ticks[k])
            for (int i = 0; t.text[i]; i++)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                    {
                        const int v = gfx.title_pixel((uint8_t) t.text[i], x, y);
                        const int sx = t.x + 8 * i + x;
                        if (!v || sx < 0 || sx >= w) continue;
                        const uint32_t want = orradio::Gfx::rgb(gfx.pal[t.palette][v]);
                        if ((px[(size_t)(orradio::Title::Y + y) * S32Video::SCREEN_W_MAX + sx] & 0xFFFFFF) == want) ok++;
                        else { bad++; bx0 = std::min(bx0, sx); bx1 = std::max(bx1, sx); }
                    }
        if (ok + bad == 0) continue;
        if (best < 0 || ok * (best_ok + best_bad) > best_ok * (ok + bad))
            { best = k; best_ok = ok; best_bad = bad; best_pal = shown_ticks[k].empty() ? 0 : shown_ticks[k].back().palette; best_x0 = bx0; best_x1 = bx1; }
    }
    if (best < 0) return;
    scr_frames++;
    if (best_ok * 2 < best_ok + best_bad) { scr_none++; if (verbose) printf("frame %llu: no tick's titles on the screen (best %ld of %ld pixels)\n", (unsigned long long) frame_no, best_ok, best_ok + best_bad); return; }
    scr_ok += best_ok; scr_bad += best_bad;
    if (!best_bad) scr_exact++;
    if (shown_ticks[best].size() == 1 && shown_ticks[best][0].held)     // one title, held in the centre
    {
        scr_held++;
        if (!best_bad) scr_held_exact++;
        else if (verbose) printf("frame %llu: a held title with %ld of %ld pixels different, x %d-%d\n", (unsigned long long) frame_no, best_bad, best_ok + best_bad, best_x0, best_x1);
    }
    scr_palette[best_pal & 7]++;
    scr_lag[(int) shown_ticks.size() - 1 - best]++;
}

void drain()
{
    // the board's: what $3F8EE writes to $700700 now (nothing when the queue is empty)
    int b = -1;
    uint8_t r = rb(0x208800), w = rb(0x208801);
    if (r != w) b = rb(0x208802 + r);
    if (!model_on) return;
    int m = M.drain();
    if (b != m) diff("$700700", b, m);
    if (b >= 0)
    {
        sent_board++;
        if (!M.drained_external) sent_radio++;
        if (logf) fprintf(logf, "%llu %02x %s\n", (unsigned long long) frame_no, b, M.drained_external ? "game" : "radio");
    }
}

void hook(void*, uint32_t pc)
{
    auto it = cover.find(pc);
    if (it != cover.end()) it->second++;
    switch (pc)
    {
        case 0x11F3E:
            if (B->main_reg(20) == OBJ) radio_entry();
            break;
        case 0x11F41:
            if (B->main_reg(20) == OBJ && in_radio) radio_return();
            break;
        case 0x1214A:
            if (B->main_reg(25) != 0x208000) break;
            if (in_radio) board_queued.push_back((uint8_t) B->main_reg(0));
            else if (model_on) M.queue_external((uint8_t) B->main_reg(0));
            break;
        case 0x12262:
            if (in_radio) board_volume.push_back((uint8_t) B->main_reg(0));
            break;
        case 0x1957C:                                   // a title object: R21
            if (in_radio) radio_objs.push_back(B->main_reg(21));
            else other_titles++;
            break;
        case 0x19686:                                   // $1966E's F30 written: another part of the game's voice
            if (!in_radio && model_on && B->main_reg(25) == 0x208000)
            {
                voice_syncs++;
                M.set_voice(rl(0x208F30), script_end(rl(0x208F30)), rb(0x208F34));
            }
            break;
        case 0x3F8DA:
            if (B->main_reg(25) == 0x208000) drain();
            break;
        case 0x18C45:
            if (B->main_reg(25) == 0x208000 && model_on) { before_lcd(); M.voice_clock(); }
            break;
        case 0x18D52:
            if (B->main_reg(25) == 0x208000) after_lcd();
            break;
        case 0x18C9F:
            if (B->main_reg(25) == 0x208000) after_voice_clock();
            break;
        default:
            break;
    }
}

// The transcribed routines (code only; the jump tables between them are left out)
const uint32_t RANGES[][2] = {
    { 0x191E0, 0x191ED }, { 0x191ED, 0x192AF }, { 0x192D4, 0x1935C }, { 0x1935C, 0x1944A },
    { 0x19464, 0x19551 }, { 0x19551, 0x1958E }, { 0x1958E, 0x19599 }, { 0x195B0, 0x19668 },
    { 0x1966E, 0x1968D }, { 0x18C45, 0x18CDC }, { 0x18CEC, 0x18D53 }, { 0x120B1, 0x120F4 }, { 0x1214A, 0x12160 }, { 0x12256, 0x12266 },
};

struct Press { int frame; int len; uint8_t p2; bool start; };

std::vector<Press> scenario(const std::string& s)
{
    const uint8_t DJ = 1, PREV = 2, NEXT = 4;
    std::vector<Press> v;
    auto add = [&](int f, uint8_t m, bool start = false, int len = 6) { v.push_back({ f, len, m, start }); };
    if (s == "buttons")
    {
        add(3100, NEXT); add(3160, NEXT); add(3220, NEXT);              // 2 -> 3, 4, 5
        add(3280, 0, true);                                             // START: 6
        add(3340, PREV); add(3400, PREV); add(3460, PREV); add(3520, PREV); add(3580, PREV); add(3640, PREV); add(3700, PREV); // 5 .. 0, 9 (the wrap), 8
        add(3760, NEXT); add(3790, NEXT);                               // 9, 0 (the wrap)
        add(3830, PREV | NEXT);                                         // Jingle Bell
        add(3900, DJ);                                                  // DJ in the branch's slow-down ($208F0F from 3865)
        add(4700, NEXT);                                                // back to the song
        add(4800, DJ);                                                  // the station's name ($29)
        add(6400, DJ);                                                  // back
        add(6460, DJ);                                                  // DJ again, to GAME OVER
    }
    else if (s == "dj")
        add(2950, DJ);
    else if (s == "two")
        add(2950, DJ);                                                  // player 2 in the race too (main)
    else if (s == "branch")
        add(2950, DJ);                                                  // and the car stopped at the branch (main)
    else if (s == "fast")
    {
        for (int i = 0; i < 12; i++) add(3000 + 4 * i, i % 3 == 2 ? PREV : NEXT, false, 2);
        add(3100, DJ, false, 2); add(3104, DJ, false, 2); add(3108, DJ, false, 2); add(3112, NEXT, false, 2);
        add(3200, DJ, false, 40);                                       // held: one press
    }
    return v;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: orradiotest <romdir> [buttons|dj|branch|two|fast] [-frames n] [-v] [-log file] [-diffs n] [-screen]\n");
        return 2;
    }
    std::string sc = argc >= 3 && argv[2][0] != '-' ? argv[2] : "buttons";
    int last = 9800;
    for (int i = 2; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "-frames" && i + 1 < argc) last = atoi(argv[++i]);
        else if (a == "-v") verbose = true;
        else if (a == "-log" && i + 1 < argc) logf = fopen(argv[++i], "w");
        else if (a == "-diffs" && i + 1 < argc) max_diffs = atoi(argv[++i]);
        else if (a == "-screen") screen_check = true;
    }
    ORBoard::Roms roms; std::string err;
    if (!ORBoard::load_roms(argv[1], roms, err)) { fprintf(stderr, "ROMs: %s\n", err.c_str()); return 1; }
    static ORBoard board;
    B = &board;
    std::vector<uint8_t> rom(roms.maincpu.begin(), roms.maincpu.end());
    ROM = rom.data();
    if (!gfx.load(argv[1], err)) { fprintf(stderr, "ROMs: %s\n", err.c_str()); return 1; }
    if (!board.init(std::move(roms))) { fprintf(stderr, "board init failed\n"); return 1; }
    board.power_on(nullptr);
    board.draw_mask = 0;

    // the hooks: the radio's and the coverage of the transcribed routines
    V60Disassembler dis;
    std::vector<uint32_t> pcs = { 0x11F3E, 0x11F41, 0x1214A, 0x12262, 0x3F8DA, 0x18C45, 0x18C9F, 0x18D52, 0x1957C, 0x19686 };
    for (auto& r : RANGES)
        for (uint32_t pc = r[0]; pc < r[1];)
        {
            uint32_t res = 0;
            dis.disassemble(pc, [&](uint32_t a) { return rom[a & 0x1FFFFF]; }, &res);
            uint32_t len = res & V60Disassembler::LENGTHMASK;
            cover[pc] = 0;
            pcs.push_back(pc);
            pc += len ? len : 1;
        }
    board.set_pc_hooks(pcs.data(), (int) pcs.size(), hook, nullptr);

    std::vector<Press> presses = scenario(sc);
    board.on_frame = [&](uint64_t n)
    {
        frame_no = n + 1;
        int f = (int) frame_no;
        lcd_frame();
        if (screen_check)
        {
            // the screen is drawn while one of the radio's titles is up (and two frames more)
            static int draw_for = 0;
            bool up = false;
            for (const orradio::Title& t : M.titles) if (t.active) up = true;
            if (board.draw_mask && model_on) screen_frame();
            if (up && model_on) draw_for = 3; else if (draw_for) draw_for--;
            board.draw_mask = draw_for ? 1 : 0;
            if (!draw_for) shown_ticks.clear();
        }
        ORBoard::Inputs& in = board.in;
        // tools/ortrace/scripts/race_mp.inc
        if (f == 600) in.service12_a &= ~0x04;
        if (f == 606) in.service12_a |= 0x04;
        if (f == 700) in.service12_a &= ~0x10;
        if (f == 760) in.service12_a |= 0x10;
        if (f == 900 || f == 1050) in.analog[0] = 0;
        if (f == 920 || f == 1070) in.analog[0] = 128;
        if (f == 2900) in.analog[1] = 255;
        if (sc == "two")                            // player 2: coin and START with player 1's
        {
            if (f == 600) in.service12_b &= ~0x04;
            if (f == 606) in.service12_b |= 0x04;
            if (f == 700) in.service12_b &= ~0x10;
            if (f == 760) in.service12_b |= 0x10;
        }
        if (sc == "branch")                         // stopped in the branch's slow-down, then on
        {
            if (f == 3880) { in.analog[1] = 0; in.analog[2] = 255; }
            if (f == 4900) { in.analog[1] = 255; in.analog[2] = 0; }
        }
        uint8_t p2 = 0;
        bool start = false;
        for (const Press& p : presses)
            if (f >= p.frame && f < p.frame + p.len) { p2 |= p.p2; start |= p.start; }
        in.p2_a = (uint8_t)(0xFF & ~p2);
        if (f > 760) in.service12_a = start ? (uint8_t)(in.service12_a & ~0x10) : (uint8_t)(in.service12_a | 0x10);
    };
    frame_no = 1;
    while ((int) frame_no < last) board.run_frame();

    int covered = 0;
    for (auto& e : cover) if (e.second) covered++;
    printf("scenario %s, frames to %d: %d radio ticks (inits %d), %d commands sent to $700700 (%d by the radio), "
           "%d title-object ticks compared; synced from the game: $208F00 %d times, $208F30 %d times (the game's voices); $208F0F set %d times; the game's own title objects %d\n",
           sc.c_str(), last, ticks, inits, sent_board, sent_radio, titles_checked, rng_syncs, voice_syncs, f0f_sets, other_titles);
    printf("LCD: %d ticks compared ($208F10-$208F1F, the scroll), %d with a title scrolling, %d with a voice's script running; "
           "given from the game: its text %d times, its own titles %d\n",
           lcd_ticks, lcd_title_ticks, lcd_mouth_ticks, lcd_game_text, lcd_game_titles);
    printf("LCD display (port G): %d frames;", lcd_frames);
    for (auto& e : lcd_lag) printf(" the buffer of %d frames before: %d;", e.first, e.second);
    printf(" neither: %d; character generator RAM %s the ROM's $18DBA\n", lcd_frame_other,
           memcmp(board.lcd[0].cgram, ROM + 0x18DBA, 64) ? "DIFFERS from" : "=");
    if (memcmp(board.lcd[0].cgram, ROM + 0x18DBA, 64) || memcmp(board.lcd[0].cgram, gfx.cg, 64)) diffs++;
    if (screen_check)
    {
        printf("screen: %d frames with a title compared: %d exact, %ld of %ld title pixels as the model's (font, palette, position); "
               "one title held in the centre: %d frames, %d exact; "
               "%d frames matching no tick; frames by palette of $197CD:", scr_frames, scr_exact, scr_ok, scr_ok + scr_bad, scr_held, scr_held_exact, scr_none);
        for (int i = 0; i < 8; i++) printf(" %d", scr_palette[i]);
        printf("; by the model's tick shown (ticks back):");
        for (auto& e : scr_lag) printf(" %d: %d", e.first, e.second);
        printf("\n");
    }
    printf("coverage: %d of %d instructions of the transcribed routines executed\n", covered, (int) cover.size());
    if (verbose)
        for (auto& e : cover) if (!e.second) printf("  not executed: %06x\n", e.first);
    printf("%d differences\n", diffs);
    if (logf) fclose(logf);
    return diffs ? 1 : 0;
}
