/***************************************************************************
    OUTRUNNERS RADIO on the other games' courses. See radio.hpp.
***************************************************************************/
#include "crossover/radio.hpp"
#include "crossover/xaudio.hpp"
#include "sdl2/input.hpp"
#include "video.hpp"
#include "globals.hpp"
#include "roms.hpp"
#include "frontend/config.hpp"
#include "main.hpp"

#include <cstdlib>

XRadio xradio;

// The race the radio introduces on the other games' courses: OutRunners' single-player race with
// MAD POWER (tools/ortrace: $20FC1F = 1, $20FC20 = 0, $20E030 = 0, 80[R20] = 0, $FF x 7)
static orradio::Race host_race()
{
    orradio::Race r;
    r.car = 0;                                  // MAD POWER: Splash Wave
    r.entries = 1;
    r.order[0] = 0;
    for (int i = 1; i < 8; i++) r.order[i] = 0xFF;
    r.own_place = 0;
    r.place_of_first = 0;
    return r;
}

void XRadio::start()
{
    OutRunnersMusic& m = xaudio.outrunners();
    if (!m.ready) return;
    m.start();                                  // the race setup's $1F ($12277)
    radio = orradio::Radio();
    radio.init(host_race(), 0);                 // the start of OutRunners' map: node 0
    radio.rng = 0;                              // $208F00 as an OutRunners race starts (tools/ortrace)
    on = true;
    ticks = 0;
    static const char* logname = getenv("CB_RADIO_LOG");
    if (logname && !log) log = fopen(logname, "w");
    if (log) fprintf(log, "frame %d start\n", cannonball::frame);
}

void XRadio::stop()
{
    if (!on) return;
    on = false;
    shown = false;
    radio.titles.clear();
    if (log) { fprintf(log, "frame %d tick %d stop\n", cannonball::frame, ticks); fflush(log); }
}

void XRadio::tick(bool buttons, bool show)
{
    shown = on && show;
    if (!on) return;
    OutRunnersMusic& m = xaudio.outrunners();
    orradio::In in;                             // node 0, stage 0, no countdown, never over (the
                                                // music's own stop rules end it)
    if (buttons)
    {
        // P2_A's new presses, as $208118's bits 8-10 (DJ/MUSIC, <<, >>)
        if (input.has_pressed(Input::UP))   in.edges |= 0x100;
        if (input.has_pressed(Input::DOWN)) in.edges |= 0x400;
        if (input.analog)                       // LEFT / RIGHT steer with digital steering
        {
            if (input.has_pressed(Input::LEFT))  in.edges |= 0x200;
            if (input.has_pressed(Input::RIGHT)) in.edges |= 0x400;
        }
    }
    radio.tick(in);
    radio.voice_clock();
    for (uint8_t v : radio.volume) m.shared(0x0918, v);
    int c = radio.drain();
    if (c >= 0) m.command((uint8_t) c);
    ticks++;
    if (log)
    {
        if (in.edges) fprintf(log, "frame %d tick %d buttons %03x\n", cannonball::frame, ticks, in.edges);
        if (c >= 0) fprintf(log, "frame %d tick %d command %02x (song %d, C %d, D %d)\n", cannonball::frame, ticks, c, radio.song, radio.c, radio.d);
    }
}

// ------------------------------------------------------------------------------------------------
// The title
// ------------------------------------------------------------------------------------------------

// OutRun's radio-screen font (OHud::blit_text_big): letter n = the text tiles $A0 + 2n (top) and
// $A1 + 2n (bottom); the note $7A, $7B over $7C, $7D; text palette 5 (the codes' bits 9-11)
static const uint16_t LETTERS = 0x0A0, NOTE = 0x07A;
static const int PALETTE = 5;
// OutRun's text palettes: written to palette RAM entries 0-63 from the ROM at $16ED8
// (OTiles::setup_palette_hud)
static const uint32_t PAL_ROM = 0x16ED8;

void XRadio::colours()
{
    uint32_t rgb[RADIO_COLOURS];
    for (int i = 0; i < (int) RADIO_COLOURS; i++)
    {
        uint32_t a = roms.rom0.read16(PAL_ROM + 2 * (PALETTE * 8 + i));
        // as Video::own_rgb
        uint32_t r = ((a & 0x000f) << 1) | ((a >> 12) & 1);
        uint32_t g = ((a & 0x00f0) >> 3) | ((a >> 13) & 1);
        uint32_t b = ((a & 0x0f00) >> 7) | ((a >> 14) & 1);
        rgb[i] = (r * 8) << 16 | (g * 8) << 8 | (b * 8);
    }
    video.set_radio_colours(rgb);
    colours_set = true;
}

void XRadio::draw(uint16_t* pixels, int w, int h)
{
    if (!shown || radio.titles.empty()) return;
    if (!colours_set) colours();
    const int scale = h / S16_HEIGHT > 1 ? 2 : 1;
    // OutRunners' 320-pixel screen, centred in the frame; nothing outside it (as OutRun's text layer)
    const int x0 = (w - S16_WIDTH * scale) / 2, y0 = (h - S16_HEIGHT * scale) / 2;
    const int top = orradio::Title::Y - 4;      // 16 pixels high, centred on OutRunners' 8-pixel row
    auto glyph = [&](uint16_t tile, int sx, int sy)
    {
        const uint32_t* rows = video.tile_layer->tile_data(tile);
        for (int y = 0; y < 8; y++)
        {
            uint32_t p = rows[y];
            for (int x = 0; x < 8; x++)
            {
                uint32_t c = (p >> (28 - 4 * x)) & 7;
                int px = sx + x;
                if (!c || px < 0 || px >= S16_WIDTH) continue;
                uint16_t v = (uint16_t)(EXT_COLOURS + 1 + XCAR_COLOURS + c);
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                    {
                        int fx = x0 + px * scale + dx, fy = y0 + (sy + y) * scale + dy;
                        if (fx >= 0 && fx < w && fy >= 0 && fy < h) pixels[fy * w + fx] = v;
                    }
            }
        }
    };
    for (const orradio::Title& t : radio.titles)
    {
        if (!t.active || !t.drawn) continue;
        const char* s = t.song->text;
        int x = t.screen_x();
        for (int i = 0; s[i]; i++, x += 8)
        {
            char c = s[i];
            if (c == '`')                       // the note, over the note and the space after it
            {
                glyph(NOTE, x, top); glyph(NOTE + 1, x + 8, top);
                glyph(NOTE + 2, x, top + 8); glyph(NOTE + 3, x + 8, top + 8);
                if (s[i + 1] == ' ') { i++; x += 8; }
                continue;
            }
            if (c >= 'a' && c <= 'z') c = (char)(c - 0x20);
            if (c < 'A' || c > 'Z') continue;   // (the titles have letters and spaces only)
            uint16_t n = (uint16_t)(LETTERS + 2 * (c - 'A'));
            glyph(n, x, top);
            glyph((uint16_t)(n + 1), x, top + 8);
        }
    }
}
