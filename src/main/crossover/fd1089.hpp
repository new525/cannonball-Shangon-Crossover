/***************************************************************************
    FD1089B decryption (Super Hang-On, key 317-0034).

    Port of MAME's src/mame/sega/fd1089.cpp, FD1089B path only.
    Algorithm and tables: Nicola Salmoria, Andreas Naive, Charles MacDonald (MAME, BSD-3-Clause).
***************************************************************************/
#pragma once
#include <cstdint>
#include <cstddef>

// raw: big-endian 68000 image (interleaved ROMs). key: 0x2000 bytes (opcode half, then data half).
// Writes the instruction-fetch view to opcodes and the data-read view to data (both len bytes).
void fd1089b_decrypt(const uint8_t* raw, size_t len, const uint8_t* key, uint8_t* opcodes, uint8_t* data);
