/***************************************************************************
    OutRunners' radio panel: the LCD, as the game drives it (docs/OUTRUNNERS-RE.md 14.8).

    The cabinet's radio panel has a character display of one line of 16 characters. The game
    drives it through port G of each player's I/O chip (MAME: sw2_output; $C0000C player 1,
    $C8000C player 2): bits 0-1 are the DJ/MUSIC and << >> lamps, bits 4-7 a data nibble,
    bit 3 the strobe and bit 2 command/data, the last three inverted. What it sends is the
    protocol of an HD44780-type controller with a 4-bit bus ($18968, $18A0F-$18A92, $18B2A):
      - at power-on the nibbles 3, 3, 3 (8-bit bus, three times), 2 (4-bit bus), then the
        instruction $0C (display on, no cursor), $40 (character generator address 0) and 64
        data bytes: eight characters of the game's own, 5 x 8 dots (the ROM's $18DBA);
      - from then on, every frame, $80 (display address 0) and the 16 characters of the
        player's buffer ($208F10, copied to $20E3C0 by $18ACF): one nibble per timer
        interrupt (34 a frame); once a minute the first six nibbles again (3, 3, 3, 2, 0, C).
    The function set that the game sends leaves the controller at one line.

    OrLcd is the receiving end: the controller's instructions that the game uses and those
    that change what is displayed, its display RAM and its character generator RAM. A nibble
    is taken when the port's bit 3 goes from 0 to 1 (the strobe's falling edge).

    The dots of the display's own characters are not in the game's ROMs. The game's texts are
    ASCII ("SEGA Enterprises", "Speed:    km/h") with $7E or $7F three times on either side
    of CHECK POINT ($CE04, $CE18), the bytes $A6-$DF in the attract mode's texts ($1AC34) and
    $FF as the countdown's bar ($19128): they fit the HD44780's Japanese standard character
    set as its data sheet is remembered (arrows, half-width katakana, a full block; not
    checked against a source here). The codes 0-7 are the game's own characters, in cgram.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstring>

struct OrLcd
{
    static const int CHARS = 16;            // one line of 16 characters, display addresses 0-15
    uint8_t ddram[0x80];                    // display RAM (character codes)
    uint8_t cgram[0x40];                    // character generator RAM: 8 characters x 8 rows, bits 4-0 = left to right
    bool    display_on;
    uint32_t bytes;                         // instructions and data received (tests)

    OrLcd() { reset(); }

    // The controller's power-on state: 8-bit bus, display off, address 0, increment
    void reset()
    {
        std::memset(ddram, 0x20, sizeof ddram);
        std::memset(cgram, 0, sizeof cgram);
        display_on = false;
        eight_bit = true; have_high = false; high = 0;
        addr = 0; to_cgram = false; increment = true;
        port = 0x08;                        // the strobe idle
        bytes = 0;
    }

    // A write to the I/O chip's port G
    void port_w(uint8_t data)
    {
        const bool edge = !(port & 0x08) && (data & 0x08);
        port = data;
        if (!edge) return;
        const uint8_t nibble = (uint8_t)((~data >> 4) & 0x0F);
        const bool rs = !(data & 0x04);     // 1: data, 0: an instruction
        if (eight_bit)                      // only the bus's upper four lines are connected
            byte(rs, (uint8_t)(nibble << 4));
        else if (!have_high) { high = nibble; have_high = true; }
        else { have_high = false; byte(rs, (uint8_t)(high << 4 | nibble)); }
    }

private:
    bool    eight_bit, have_high, to_cgram, increment;
    uint8_t high, addr, port;

    void byte(bool rs, uint8_t v)
    {
        bytes++;
        if (rs)                             // data: to the RAM the last address instruction chose
        {
            if (to_cgram) cgram[addr & 0x3F] = (uint8_t)(v & 0x1F);
            else ddram[addr & 0x7F] = v;
            addr = (uint8_t)(increment ? addr + 1 : addr - 1);
            return;
        }
        if (v & 0x80)      { addr = (uint8_t)(v & 0x7F); to_cgram = false; }    // display address
        else if (v & 0x40) { addr = (uint8_t)(v & 0x3F); to_cgram = true; }     // character generator address
        else if (v & 0x20) { eight_bit = (v & 0x10) != 0; have_high = false; }  // function set
        else if (v & 0x10) { }                                                  // cursor / display shift: not sent
        else if (v & 0x08) display_on = (v & 0x04) != 0;                        // display on/off
        else if (v & 0x04) increment = (v & 0x02) != 0;                         // entry mode
        else if (v & 0x02) { addr = 0; to_cgram = false; }                      // home
        else if (v & 0x01) { std::memset(ddram, 0x20, sizeof ddram); addr = 0; to_cgram = false; increment = true; }  // clear
    }
};
