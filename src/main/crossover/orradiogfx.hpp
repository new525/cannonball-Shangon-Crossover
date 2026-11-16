/***************************************************************************
    OutRunners' radio: its graphics, from the ROM files of orunnersj (docs/OUTRUNNERS-RE.md 14.6,
    14.8). Nothing here is drawn from memory of the game: every pixel and colour is read from the
    ROMs at the addresses the game's code uses.

      - The title's font: the title object ($1958E) draws each character as an 8 x 8 sprite of 16
        colours ($11C66 with the sprite header at $122B5C: bank 0, the character c's eight rows at
        the sprite ROM's 32-bit words $DA861 + 8c, one word a row, the leftmost pixel in the top
        four bits). Pixel 0 is transparent, and 15 in a row's first and last pixel (the sprite
        hardware's end marker); the letters use 1-4 (the fill, top to bottom) and 5 (the outline).
      - The title's palettes: the sprite palettes $80-$FF, loaded at power-on ($11881) from the
        data ROM's record at $153904 (palette RAM $8000 onwards, 16-bit entries xBGRBBBBGGGGRRRR):
        the entry i of palette p at $153908 + 2 (p + i). The title takes the eight of $197CD in
        turn (orradio::TITLE_PALETTE).
      - The LCD's characters of the game's own: 8 characters of 5 x 8 dots at $18DBA, sent to the
        display's character generator RAM at power-on ($189FD): 0 blank, 1-6 the DJ's face (left
        and right halves with the mouth shut, half open, open), 7 the note.
      - The program ROM itself, for the voices' scripts (orradio::Radio::rom).

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orradio
{

struct Gfx
{
    bool     ok = false;
    uint32_t font[128][8];              // character, row: 8 pixels of 4 bits, the leftmost in bits 31-28
    uint16_t pal[8][16];                // TITLE_PALETTE's palettes in its order, as palette RAM holds them (xBBBBBGGGGGRRRRR)
    uint8_t  cg[64];                    // the LCD's characters 0-7: 8 rows each, bits 4-0 = left to right
    std::vector<uint8_t> prog;          // the program ROM, $000000-$03FFFF

    // Reads the ROM files (found as crossover/rompath.hpp finds them, by their MAME names in dir
    // otherwise): epr-15616/15617 (program), mpr-15538/15539 (data), mpr-15540/15542/15544/15546
    // (sprites, bank 0). False, with the reason, if one cannot be read.
    bool load(const std::string& dir, std::string& error);

    // A title character's pixel (x, y in 0-7): 0 transparent, else the palette's entry
    int title_pixel(uint8_t c, int x, int y) const
    {
        const int v = (int)(font[c & 0x7F][y] >> (28 - 4 * x)) & 15;
        return (v == 15 && (x == 0 || x == 7)) ? 0 : v;
    }
    // A dot of the LCD's character `code` (x 0-5, y 0-7), for drawing the panel. Codes 0-15 are the
    // game's own characters (5 x 8, in columns 0-4), from the character generator RAM given (cg
    // above, or the emulated board's display). The display's own characters are not in the game's
    // ROMs: $20-$7D (ASCII in the game's texts) are drawn as the fill of the title font's letters
    // (its pixels 1-4, the font's columns 1-6), a stand-in. Other codes: blank (the game's other
    // texts use $7E, $7F, $A6-$DF and $FF, crossover/orlcd.hpp: none is in a title of the radio's).
    bool lcd_dot(const uint8_t* cgram, uint8_t code, int x, int y) const
    {
        if (code < 0x10) return x < 5 && ((cgram[(code & 7) * 8 + y] >> (4 - x)) & 1);
        if (code < 0x20 || code > 0x7D) return false;
        const int v = (int)(font[code][y] >> (28 - 4 * (x + 1))) & 15;
        return v >= 1 && v <= 4;
    }
    // A palette RAM word as the board's video output shows it (crossover/s32video: 5 bits << 3)
    static uint32_t rgb(uint16_t v)
    {
        return (uint32_t)(v & 0x1F) << 19 | (uint32_t)((v >> 5) & 0x1F) << 11 | (uint32_t)((v >> 10) & 0x1F) << 3;
    }
};

} // namespace orradio
