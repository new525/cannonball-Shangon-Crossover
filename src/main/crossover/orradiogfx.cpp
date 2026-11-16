/***************************************************************************
    OutRunners' radio: its graphics, from the ROM files. See orradiogfx.hpp.
***************************************************************************/
#include "crossover/orradiogfx.hpp"
#include "crossover/orradio.hpp"
#include "crossover/rompath.hpp"

namespace orradio
{

static const uint32_t FONT_WORD   = 0xDA861;    // $11CE8: the sprite's address = $A861 + 8c, bank bits $D ($122B5C)
static const uint32_t PALETTES    = 0x153908;   // the record $153904's data: palette RAM entry 0
static const uint32_t LCD_CHARS_AT = 0x18DBA;   // $189FD

static bool read(const std::string& dir, const char* name, size_t size, std::vector<uint8_t>& d, std::string& error)
{
    if (read_rom(dir, name, d) && d.size() == size) return true;
    error = "cannot read " + rom_file(dir, name).where();
    return false;
}

bool Gfx::load(const std::string& dir, std::string& error)
{
    ok = false;
    std::vector<uint8_t> a, b;

    // The program: ROM_LOAD32_WORD, the 16-bit words of the two files alternate
    if (!read(dir, "epr-15616.ic37", 0x20000, a, error) || !read(dir, "epr-15617.ic40", 0x20000, b, error)) return false;
    prog.assign(0x40000, 0);
    for (uint32_t i = 0; i < 0x20000; i += 2)
    {
        prog[2 * i] = a[i];     prog[2 * i + 1] = a[i + 1];
        prog[2 * i + 2] = b[i]; prog[2 * i + 3] = b[i + 1];
    }
    for (int i = 0; i < 64; i++) cg[i] = prog[LCD_CHARS_AT + i] & 0x1F;

    // The data ROM ($100000 onwards), the same way: the palettes, as the game's palette RAM write
    // converts them (crossover/s32video paletteram_w: xBGRBBBBGGGGRRRR to xBBBBBGGGGGRRRRR)
    if (!read(dir, "mpr-15538.ic36", 0x80000, a, error) || !read(dir, "mpr-15539.ic39", 0x80000, b, error)) return false;
    auto data16 = [&](uint32_t addr)            // a little-endian word at a V70 address
    {
        const uint32_t o = addr - 0x100000;
        const std::vector<uint8_t>& f = (o & 2) ? b : a;
        const uint32_t fo = (o >> 2) * 2;
        return (uint16_t)(f[fo] | f[fo + 1] << 8);
    };
    for (int p = 0; p < 8; p++)
        for (int i = 0; i < 16; i++)
        {
            const uint16_t v = data16(PALETTES + 2 * (uint32_t)(TITLE_PALETTE[p] + i));
            const int r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
            const int g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
            const int bl = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
            pal[p][i] = (uint16_t)((v & 0x8000) | (bl << 10) | (g << 5) | r);
        }

    // The sprite ROMs' first half: ROM_LOAD64_WORD, the 16-bit words of the four files in turn; a
    // 32-bit sprite word is big-endian, so the files 0 and 2 hold the upper halves of the even and
    // odd words, 1 and 3 the lower halves
    static const char* const SPR[4] = { "mpr-15540.ic14", "mpr-15542.ic15", "mpr-15544.ic10", "mpr-15546.ic38" };
    for (int c = 0; c < 128; c++) for (int y = 0; y < 8; y++) font[c][y] = 0;
    for (int f = 0; f < 4; f++)
    {
        if (!read(dir, SPR[f], 0x200000, a, error)) return false;
        for (int c = 0; c < 128; c++)
            for (int y = 0; y < 8; y++)
            {
                const uint32_t w = FONT_WORD + 8 * (uint32_t) c + (uint32_t) y;     // the word's number in the bank
                if ((int)(w & 1) != (f >> 1)) continue;                             // this file's word lane
                const uint32_t fo = (w >> 1) * 2;
                const uint32_t half = (uint32_t)(a[fo] << 8 | a[fo + 1]);
                font[c][y] |= (f & 1) ? half : half << 16;
            }
    }
    ok = true;
    return true;
}

} // namespace orradio
