/***************************************************************************
    OUTRUNNERS RADIO on the other games' courses. See radio.hpp.
***************************************************************************/
#include "crossover/radio.hpp"
#include "crossover/orlcd.hpp"
#include "crossover/romset.hpp"
#include "crossover/xaudio.hpp"
#include "sdl2/input.hpp"
#include "video.hpp"
#include "globals.hpp"
#include "roms.hpp"
#include "frontend/config.hpp"
#include "main.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

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

// OutRunners' own title font, palettes and LCD characters: read from its ROM files the first time
// they are needed (four of the 2 MB sprite ROMs among them), if the whole set is there
void XRadio::load_gfx()
{
    if (gfx_tried) return;
    gfx_tried = true;
    if (!or_roms.full) return;
    std::string err;
    if (!gfx.load(config.crossover.outrunners_rompath, err))
        std::cerr << "OUTRUNNERS RADIO: " << err << " (the title is drawn in OutRun's font, no panel)" << std::endl;
    colours_set = false;
}

void XRadio::start()
{
    OutRunnersMusic& m = xaudio.outrunners();
    if (!m.ready) return;
    m.start();                                  // the race setup's $1F ($12277)
    load_gfx();
    radio = orradio::Radio();
    radio.rom = gfx.ok ? gfx.prog.data() : nullptr;     // the voices' scripts: the DJ's mouth
    radio.init(host_race(), 0);                 // the start of OutRunners' map: node 0
    radio.rng = 0;                              // $208F00 as an OutRunners race starts (tools/ortrace)
    on = true;
    ticks = 0;
    static const char* logname = getenv("CB_RADIO_LOG");
    if (logname && !log) log = fopen(logname, "w");
    if (log) fprintf(log, "frame %d start\n", cannonball::frame);
    std::memset(logged_lcd, 0, sizeof logged_lcd);
}

void XRadio::stop()
{
    if (!on) return;
    on = false;
    shown = false;
    radio.titles.clear();
    if (log) { fprintf(log, "frame %d tick %d stop\n", cannonball::frame, ticks); fflush(log); }
}

void XRadio::board_panel(const OrLcd* lcd)
{
    if (lcd) load_gfx();
    board_lcd = lcd && gfx.ok ? lcd : nullptr;
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
        // P2_A's new presses, as $208118's bits 8-10 (DJ/MUSIC, <<, >>); START, its bit 6
        if (input.has_pressed(Input::UP))    in.edges |= 0x100;
        if (input.has_pressed(Input::DOWN))  in.edges |= 0x400;
        if (input.has_pressed(Input::START)) in.edges |= 0x040;
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
        // the LCD while a title scrolls on it (the codes 0-7, the game's own characters, as digits)
        if (radio.lcd_title() && std::memcmp(logged_lcd, radio.lcd, sizeof logged_lcd))
        {
            std::memcpy(logged_lcd, radio.lcd, sizeof logged_lcd);
            fprintf(log, "frame %d tick %d lcd |", cannonball::frame, ticks);
            for (uint8_t ch : radio.lcd) fputc(ch < 0x10 ? '0' + (ch & 7) : ch, log);
            fprintf(log, "|\n");
        }
    }
}

// ------------------------------------------------------------------------------------------------
// The title and the panel
// ------------------------------------------------------------------------------------------------

// The radio's frame-buffer values (RADIO_COLOURS, globals.hpp): OutRunners' eight title palettes
// (16 entries each, in $197CD's order), the panel's three colours, OutRun's text palette 5
static const int COL_TITLE = 0x00, COL_PANEL = 0x80, COL_OUTRUN = 0x88;
// The panel's colours (a design decision: the cabinet's are not in the ROMs): border, background, dots
static const uint32_t PANEL_RGB[3] = { 0x101810, 0x90A870, 0x202818 };

// OutRun's radio-screen font (OHud::blit_text_big): letter n = the text tiles $A0 + 2n (top) and
// $A1 + 2n (bottom); the note $7A, $7B over $7C, $7D; text palette 5 (the codes' bits 9-11)
static const uint16_t LETTERS = 0x0A0, NOTE = 0x07A;
static const int PALETTE = 5;
// OutRun's text palettes: written to palette RAM entries 0-63 from the ROM at $16ED8
// (OTiles::setup_palette_hud)
static const uint32_t PAL_ROM = 0x16ED8;

void XRadio::colours()
{
    uint32_t rgb[RADIO_COLOURS] = {};
    if (gfx.ok)
        for (int p = 0; p < 8; p++)
            for (int i = 0; i < 16; i++) rgb[COL_TITLE + p * 16 + i] = orradio::Gfx::rgb(gfx.pal[p][i]);
    for (int i = 0; i < 3; i++) rgb[COL_PANEL + i] = PANEL_RGB[i];
    for (int i = 0; i < 8; i++)
    {
        uint32_t a = roms.rom0.read16(PAL_ROM + 2 * (PALETTE * 8 + i));
        // as Video::own_rgb
        uint32_t r = ((a & 0x000f) << 1) | ((a >> 12) & 1);
        uint32_t g = ((a & 0x00f0) >> 3) | ((a >> 13) & 1);
        uint32_t b = ((a & 0x0f00) >> 7) | ((a >> 14) & 1);
        rgb[COL_OUTRUN + i] = (r * 8) << 16 | (g * 8) << 8 | (b * 8);
    }
    video.set_radio_colours(rgb);
    colours_set = true;
}

void XRadio::draw(uint16_t* pixels, int w, int h)
{
    const bool title = shown && !radio.titles.empty();
    // the panel: the board's display (OutRunners' own courses), or the radio's while it scrolls a title
    const uint8_t* lcd = nullptr;
    const uint8_t* cg = nullptr;
    if (board_lcd) { lcd = board_lcd->ddram; cg = board_lcd->cgram; }
    else if (shown && gfx.ok && radio.lcd_title()) { lcd = radio.lcd; cg = gfx.cg; }
    if (!title && !lcd) return;
    if (!colours_set) colours();
    const int scale = h / S16_HEIGHT > 1 ? 2 : 1;
    // OutRunners' 320-pixel screen, centred in the frame; nothing outside it (as OutRun's text layer)
    const int x0 = (w - S16_WIDTH * scale) / 2, y0 = (h - S16_HEIGHT * scale) / 2;
    auto put = [&](int px, int py, int colour)
    {
        if (px < 0 || px >= S16_WIDTH || py < 0 || py >= S16_HEIGHT) return;
        const uint16_t v = (uint16_t)(EXT_COLOURS + 1 + XCAR_COLOURS + colour);
        for (int dy = 0; dy < scale; dy++)
            for (int dx = 0; dx < scale; dx++)
            {
                int fx = x0 + px * scale + dx, fy = y0 + py * scale + dy;
                if (fx >= 0 && fx < w && fy >= 0 && fy < h) pixels[fy * w + fx] = v;
            }
    };

    if (title && gfx.ok)
    {
        // OutRunners' own: 8 x 8 characters at (x + 8 i, 200), the whole title in this tick's palette
        for (const orradio::Title& t : radio.titles)
        {
            if (!t.active || !t.drawn) continue;
            const char* s = t.song->text;
            const int col = COL_TITLE + t.palette() * 16;
            int x = t.screen_x();
            for (int i = 0; s[i]; i++, x += 8)
                for (int y = 0; y < 8; y++)
                    for (int gx = 0; gx < 8; gx++)
                        if (int v = gfx.title_pixel((uint8_t) s[i], gx, y)) put(x + gx, orradio::Title::Y + y, col + v);
        }
    }
    else if (title)
    {
        // OutRun's lettering: 16 pixels high, centred on OutRunners' 8-pixel row
        const int top = orradio::Title::Y - 4;
        auto glyph = [&](uint16_t tile, int sx, int sy)
        {
            const uint32_t* rows = video.tile_layer->tile_data(tile);
            for (int y = 0; y < 8; y++)
            {
                uint32_t p = rows[y];
                for (int x = 0; x < 8; x++)
                    if (uint32_t c = (p >> (28 - 4 * x)) & 7) put(sx + x, sy + y, COL_OUTRUN + (int) c);
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

    if (lcd)
    {
        for (int y = 0; y < PANEL_H; y++)
            for (int x = 0; x < PANEL_W; x++)
                put(PANEL_X + x, PANEL_Y + y, COL_PANEL + ((x == 0 || y == 0 || x == PANEL_W - 1 || y == PANEL_H - 1) ? 0 : 1));
        for (int i = 0; i < orradio::LCD_CHARS; i++)
            for (int y = 0; y < CELL_H; y++)
                for (int x = 0; x < CELL_W - 1; x++)
                    if (gfx.lcd_dot(cg, lcd[i], x, y))
                        put(PANEL_X + MARGIN + i * CELL_W + x, PANEL_Y + MARGIN + y, COL_PANEL + 2);
    }
}
