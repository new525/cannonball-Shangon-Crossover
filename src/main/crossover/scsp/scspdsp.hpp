/***************************************************************************
    SCSP (Yamaha YMF292-F) effect DSP.

    Transcribed from MAME 0.264 src/devices/sound/scspdsp.cpp and scspdsp.h
    (BSD-3-Clause; copyright holders ElSemi, R. Belmont; thanks to kingshriek).

    The only change from MAME is how the DSP reaches the chip's memory: MAME
    stores an address_space pointer; here the owning Scsp installs a context
    pointer and two thunks that behave like address_space::read_word /
    write_word on the SCSP's 20-bit, 16-bit wide, big-endian space.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>

//the DSP Context
struct ScspDsp
{
//Config
    // MAME: address_space *space;
    void *mem_ctx = nullptr;
    uint16_t (*mem_read_word)(void *ctx, uint32_t byte_addr) = nullptr;
    void (*mem_write_word)(void *ctx, uint32_t byte_addr, uint16_t data) = nullptr;

    uint32_t RBP; //Ring buf pointer
    uint32_t RBL; //Delay ram (Ring buffer) size in words

//context

    int16_t COEF[64];     //16 bit signed
    uint16_t MADRS[32];   //offsets (in words), 16 bit
    uint16_t MPRO[128*4]; //128 steps 64 bit
    int32_t TEMP[128];    //TEMP regs,24 bit signed
    int32_t MEMS[32]; //MEMS regs,24 bit signed
    uint32_t DEC;

//input
    int32_t MIXS[16]; //MIXS, 24 bit signed
    int16_t EXTS[2];  //External inputs (CDDA)    16 bit signed

//output
    int16_t EFREG[16];    //EFREG, 16 bit signed

    bool Stopped;
    int LastStep;

    void Init();
    void SetSample(int32_t sample, int32_t SEL, int32_t MXL);
    void Step();
    void Start();
};
