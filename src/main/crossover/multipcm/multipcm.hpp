/***************************************************************************
    Sega/Yamaha MultiPCM: Yamaha YMW-258-F "GEW8", aka Sega 315-5560,
    standalone.

    Transcribed from MAME 0.264 src/devices/sound/gew.cpp, gew.h,
    multipcm.cpp and multipcm.h.
    license:BSD-3-Clause
    copyright-holders:Miguel Angel Horna
    (by Miguel Angel Horna (ElSemi) for Model 2 Emulator and MAME;
    information by R. Belmont and the YMF278B (OPL4) manual.)

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.

    MAME splits the chip into gew_pcm_device (the GEW engine, shared with
    GEW7) and multipcm_device (the register interface, 28 voices, clock
    divider 224). Only the MultiPCM variant is used here, so both are merged
    into one class without virtual dispatch. Lines marked "PORT:" in
    multipcm.cpp are the only places where the code differs from MAME.

    How to drive it:

    - Memory. read_byte must return what MAME's device_rom_interface
      <22, 0, 0, ENDIANNESS_BIG> read_byte() returns, i.e. a byte from the
      chip's own address map. The chip masks the address to 22 bits before
      calling read_byte (as MAME's memory cache does with its addrmask), so
      read_byte only ever sees 0x000000-0x3fffff. Unmapped addresses must
      return 0 (MAME's default unmap value). For System Multi 32
      (segas32.cpp multipcm_map): 0x000000-0x0fffff = region "sega"
      offset 0; 0x100000-0x17ffff = 0x80000 window "multipcmbanklo";
      0x180000-0x1fffff = 0x80000 window "multipcmbankhi"; Z80 port
      0xb0-0xbf (multipcm_bank_w) selects hi = (data >> 3) & 7,
      lo = data & 7, each entry n at region offset n * 0x80000.

    - Timing. MAME's write path calls m_stream->update() before every slot
      register write, and rom_bank_pre_change() calls it before a bank
      switch. This class has no stream: the host MUST call generate() up to
      the current emulated time before every write() and before changing
      what read_byte returns (bank switch).

    - Sample rate. MAME keeps m_rate = (float)clock / 224 as a float (all
      internal tables use that float), but stream_alloc() takes an int, so
      the stream runs at int(m_rate), truncated: sample_rate() returns that.
      At 10 MHz: m_rate = 44642.857f, stream = 44642 Hz.

    - Output. generate() writes (not adds) exactly the floats MAME's
      sound_stream_update stores: put_int_clamp(i, v, 32768), i.e.
      float(clamp(v, -32768, 32768)) * (1.0f / 32768.0f). Output 0 is
      `left` (MAME's smpl, from m_left_pan_table), output 1 is `right`
      (smpr). NOTE: segas32.cpp routes output 1 to "lspeaker" and output 0
      to "rspeaker", i.e. swapped relative to the names inside the chip.
***************************************************************************/
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

class MultiPcm
{
public:
    // chip's 22-bit byte address space (see above)
    std::function<uint8_t(uint32_t)> read_byte;

    // constructor + device_start + device_reset; tables built here
    void init(uint32_t clock);
    // multipcm_device::write (offset 0 = data, 1 = slot select, 2 = register select)
    void write(uint32_t offset, uint8_t data);
    // multipcm_device::read (always 0)
    uint8_t read();
    // int(m_rate): the rate MAME's stream actually runs at
    uint32_t sample_rate() const;
    // n samples; left = MAME output 0, right = MAME output 1
    void generate(int n, float *left, float *right);

private:
    struct sample_t
    {
        uint32_t m_start = 0;
        uint32_t m_loop = 0;
        uint32_t m_end = 0;
        uint8_t m_attack_reg = 0;
        uint8_t m_decay1_reg = 0;
        uint8_t m_decay2_reg = 0;
        uint8_t m_decay_level = 0;
        uint8_t m_release_reg = 0;
        uint8_t m_key_rate_scale = 0;
        uint8_t m_lfo_vibrato_reg = 0;
        uint8_t m_lfo_amplitude_reg = 0;
        uint8_t m_format = 0;
    };

    enum class state_t : uint8_t
    {
        ATTACK,
        DECAY1,
        DECAY2,
        RELEASE
    };

    struct envelope_gen_t
    {
        int32_t m_volume = 0;
        state_t m_state = state_t::ATTACK;
        uint8_t m_reverb = 0;
        int32_t step = 0;
        //step vals
        int32_t m_attack_rate = 0;  // Attack
        int32_t m_decay1_rate = 0;  // Decay1
        int32_t m_decay2_rate = 0;  // Decay2
        int32_t m_release_rate = 0; // Release
        int32_t m_decay_level = 0;  // Decay level
    };

    struct lfo_t
    {
        uint16_t m_phase = 0;
        uint32_t m_phase_step = 0;
        int32_t *m_table = nullptr;
        int32_t *m_scale = nullptr;
    };

    struct slot_t
    {
        uint8_t m_regs[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        bool m_playing = false;
        sample_t m_sample;
        uint32_t m_offset = 0;
        uint8_t m_octave = 0;
        uint16_t m_pitch = 0;
        uint32_t m_step = 0;
        bool m_reverse = false;
        uint32_t m_pan = 0;
        uint32_t m_total_level = 0;
        uint32_t m_dest_total_level = 0;
        int32_t m_total_level_step = 0;
        int32_t m_prev_sample = 0;
        envelope_gen_t m_envelope_gen;
        // PORT: MAME has no initializer here, but make_unique<slot_t[]>()
        // value-initializes, which zeroes it; "= 0" gives the same value.
        uint8_t m_lfo_frequency = 0;
        lfo_t m_pitch_lfo; // Pitch lfo
        uint8_t m_vibrato = 0;
        lfo_t m_amplitude_lfo; // AM lfo
        uint8_t m_tremolo = 0;
    };

    uint32_t value_to_fixed(const uint32_t bits, const float value);

    void retrigger_sample(slot_t &slot);
    void update_step(slot_t &slot);

    static constexpr uint32_t TL_SHIFT = 12;
    static constexpr uint32_t EG_SHIFT = 16;
    static constexpr uint32_t LFO_SHIFT = 8;

    // Internal LFO functions
    void lfo_init();
    void lfo_compute_step(lfo_t &lfo, uint32_t lfo_frequency, uint32_t LFOS, int32_t amplitude_lfo);
    int32_t pitch_lfo_step(lfo_t &lfo);
    int32_t amplitude_lfo_step(lfo_t &lfo);

    // Internal envelope functions
    void envelope_generator_init(const double (&rates)[64], double attack_decay_ratio);
    int32_t envelope_generator_update(slot_t &slot);
    void envelope_generator_calc(slot_t &slot);
    uint32_t get_rate(uint32_t *steps, int32_t rate, uint32_t val);

    // multipcm_device
    void init_sample(sample_t &sample, uint32_t index);
    void write_slot(slot_t &slot, int32_t reg, uint8_t data);

    // PORT: device_rom_interface::read_byte, with the cache's 22-bit mask
    uint8_t rom_read(uint32_t addr) { return read_byte(addr & 0x3fffff); }

    static const double BASE_TIMES[64];

    static const float LFO_FREQ[8];
    static const float PHASE_SCALE_LIMIT[8];
    static const float AMPLITUDE_SCALE_LIMIT[8];

    static const int32_t VALUE_TO_CHANNEL[32];

    // internal state
    std::unique_ptr<slot_t[]> m_slots;
    float m_rate = 0;
    uint32_t m_voices = 28;         // multipcm_device: voices = 28
    uint32_t m_clock_divider = 224; // multipcm_device: clock_divider = 224
    uint32_t m_clock = 0;           // PORT: device_t::clock()

    std::unique_ptr<uint32_t[]> m_attack_step;
    std::unique_ptr<uint32_t[]> m_decay_release_step;   // Envelope step tables
    std::unique_ptr<uint32_t[]> m_freq_step_table;      // Frequency step table

    std::unique_ptr<int32_t[]> m_left_pan_table;
    std::unique_ptr<int32_t[]> m_right_pan_table;
    std::unique_ptr<int32_t[]> m_linear_to_exp_volume;
    std::unique_ptr<int32_t[]> m_total_level_steps;

    std::unique_ptr<int32_t[]> m_pitch_table;
    std::unique_ptr<int32_t[]> m_pitch_scale_tables[8];
    std::unique_ptr<int32_t[]> m_amplitude_table;
    std::unique_ptr<int32_t[]> m_amplitude_scale_tables[8];

    // multipcm_device internal state
    uint32_t m_cur_slot = 0;
    uint32_t m_address = 0;
};
