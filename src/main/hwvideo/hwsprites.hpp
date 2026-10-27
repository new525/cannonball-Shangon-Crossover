#pragma once

#include "stdint.hpp"

class video;

class hwsprites
{
public:
    hwsprites();
    ~hwsprites();
    void init(const uint8_t*);
    void reset();
    void set_x_clip(bool);
    void swap();
    uint8_t read(const uint16_t adr);
    void write(const uint16_t adr, const uint16_t data);
    void render(const uint8_t);
    void render_to(uint16_t* target, int width, int height, int x_off, const uint8_t priority);
    // Crossover: banks 4-7 hold converted graphics from the other games (OutRun's ROM fills 0-3)
    uint32_t* bank_ptr(int bank) { return sprites + 0x10000 * (bank & 7); }

private:
    void render_impl(const uint8_t priority, uint16_t* target, const int s16_width, const int s16_height,
                     const int s16_x_off, const bool hires, const uint16_t x1, const uint16_t x2);
    // Clip values.
    uint16_t x1, x2;

    // 128 sprites, 16 bytes each (0x400)
    static const uint16_t SPRITE_RAM_SIZE = 128 * 8;
    static const uint32_t SPRITES_ROM_LENGTH = 0x100000 >> 2;  // OutRun sprite ROMs (4 banks)
    static const uint32_t SPRITES_LENGTH = 0x200000 >> 2;      // 8 banks: 4 more for the crossover
    static const uint16_t COLOR_BASE = 0x800;

    uint32_t sprites[SPRITES_LENGTH]; // Converted sprites
    
    // Two halves of RAM
    uint16_t ram[SPRITE_RAM_SIZE];
    uint16_t ramBuff[SPRITE_RAM_SIZE];
};

