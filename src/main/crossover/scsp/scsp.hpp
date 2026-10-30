/***************************************************************************
    Sega/Yamaha YMF292-F "SCSP" (Saturn Custom Sound Processor), standalone.

    Transcribed from MAME 0.264 src/devices/sound/scsp.cpp, scspdsp.cpp and
    scsplfo.ipp (BSD-3-Clause; copyright holders ElSemi, R. Belmont; thanks to
    kingshriek). Note: at the mame0264 tag there is no scsplfo.ipp; the LFO code
    (LFO_Init, PLFO_Step, ALFO_Step, LFO_ComputeStep) lives at the end of
    scsp.cpp and is transcribed from there into scsp.cpp here.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.

    How to drive it (what MAME's device_sound_interface / emu_timer did):

    - Streaming. MAME's read() and write() call m_stream->update() first, so
      every register access sees the chip state at the access time. This port
      has no stream: the host MUST call generate() up to the current time
      before every read(), write(), midi_in(), midi_out_r() and timer_fire().
      The sample rate is clock / 512 with integer division, as MAME's
      stream_alloc(2, 2, clock() / 512): 44100 Hz at 22,579,200 Hz, but
      44099 Hz at the 22,579,000 Hz that coolridr.cpp configures.

    - Output. generate() WRITES (does not add) one int32_t per channel and
      sample: exactly the integer MAME passes to write_stream_view::
      put_int_clamp(), after its clamp. MAME then stores
          float(v) * (1.0f / float(max))   with max = output_max()
      (32768 normally, 131072 with DAC18B set; note the clamp range is
      [-max, +max], so +32768 is possible). generate_float() produces that
      float. MAME applies MVOL as a stream output gain of MVOL / 15.0 when a
      downstream consumer reads the stream (set_output_gain in UpdateReg);
      output_gain() returns that factor; neither generate call applies it.

    - Memory. The chip's own space is 20 bits wide, 16-bit, big-endian
      (device_rom_interface<20, 1, 0, ENDIANNESS_BIG>). read_word/write_word
      receive byte addresses already masked as MAME's address space masks
      them: (addr & 0xFFFFF) & ~1. Bytes (8-bit PCM) are taken from the word
      at the even address, high byte first (big-endian). coolridr maps only
      0x000000-0x07FFFF (RAM, no mirror); MAME returns 0 for unmapped reads
      and drops unmapped writes, so the host callbacks should do the same.

    - Timers. Where MAME calls m_timerX->adjust(attotime::from_ticks(512,
      time)), this port calls timer_arm(which, period). The timer is one-shot
      and re-arming replaces a pending one (emu_timer::adjust semantics). When
      it expires the host calls timer_fire(which) (after generate()).
      As in MAME, timers are only armed when has_irq_cb is true.

    - Random numbers. MAME uses machine().rand(), a machine-wide LCG shared
      by every device (seed 0x9d14abd7). It is used for the LFO noise tables
      in init() and for SSCTL=1 noise. rand defaults to a private copy of that
      LCG; set it to a shared generator to reproduce MAME's global ordering.
***************************************************************************/
#pragma once

#include <cstdint>
#include <functional>

#include "crossover/scsp/scspdsp.hpp"

// The period MAME gives the emu_timer: attotime::from_ticks(512, hz) with
// hz = (clock / (1 << prescale)) / (255 - count)   (integer divisions).
struct ScspTimerPeriod
{
    uint32_t hz;             // MAME's local 'time'
    // attotime::from_ticks(512, hz) exactly as MAME computes it:
    //   attos_per_tick = 1e18 / hz (truncated); if 512 < hz: 512 * attos_per_tick,
    //   else seconds = 512 / hz, attoseconds = (512 % hz) * attos_per_tick.
    int32_t seconds;
    int64_t attoseconds;
    // The same period in output samples, exactly: samples_num / samples_den
    // = 512 * sample_rate / hz, with sample_rate = clock / 512 (integer),
    // reduced by the gcd. With clock = 22,579,200 it is pris * (255 - count)
    // whenever (clock / pris) is divisible by (255 - count).
    uint64_t samples_num;
    uint64_t samples_den;
    double samples;          // samples_num / samples_den
};

class Scsp
{
public:
    // Line states as MAME's CLEAR_LINE / ASSERT_LINE / HOLD_LINE.
    enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };

    Scsp();
    Scsp(const Scsp &) = delete;            // LFO table pointers point into this object
    Scsp &operator=(const Scsp &) = delete;

    // memory: the chip's own 16-bit address space (sample RAM). Words are
    // host-order uint16_t, big-endian addressing as MAME's space (word at byte
    // address a = ram[a >> 1]). Addresses arrive masked to 20 bits and even.
    std::function<uint16_t(uint32_t byte_addr)> read_word;
    std::function<void(uint32_t byte_addr, uint16_t v)> write_word;

    // interrupts: as MAME's callbacks. has_irq_cb mirrors !m_irq_cb.isunset():
    // when false, irq_cb is never called (MAME's unset devcb is a no-op), the
    // timers are never armed and SCIEB/SCIPD/SCIRE/SCILV writes have no effect
    // beyond storing the register.
    bool has_irq_cb = false;
    // devcb_write8 m_irq_cb(offset, data): level = offset (the SCILV-decoded
    // level 0-7), state = 0 clear, 1 assert, 2 hold (HOLD_LINE, used by the
    // DMA end interrupt).
    std::function<void(uint32_t level, int state)> irq_cb;
    // devcb_write_line m_main_irq_cb: called with 0 or 1 (unset = no-op).
    std::function<void(int state)> main_irq_cb;

    // timers: MAME arms emu_timers (timerA/B/C ->adjust(...)); the host
    // schedules them instead. which = 0 (A), 1 (B), 2 (C).
    std::function<void(int which, const ScspTimerPeriod &period)> timer_arm;
    void timer_fire(int which);   // the body of timerA_cb / timerB_cb / timerC_cb

    // machine().rand() replacement (see the header comment).
    std::function<uint32_t()> rand;

    // Constructor state + device_start (MAME's init()). Call once, after the
    // callbacks are set (it calls rand() 256 times for the LFO noise tables).
    void init(uint32_t clock);
    // MAME 0.264's scsp_device has no device_reset(): a machine soft reset
    // leaves the chip untouched. reset() therefore does nothing; call init()
    // again for a power-on state.
    void reset() {}

    uint32_t clock() const { return m_clock; }
    uint32_t sample_rate() const { return m_clock / 512; }

    // scsp_device::read / write (offset in words, as MAME's offs_t). MAME's
    // read() has no mem_mask; the parameter here is accepted and ignored.
    uint16_t read(uint32_t offset, uint16_t mem_mask = 0xffff);
    void     write(uint32_t offset, uint16_t data, uint16_t mem_mask = 0xffff);

    // MIDI I/O access (used for comms on Model 2/3)
    void midi_in(uint8_t data);
    uint16_t midi_out_r();

    // DoMasterSamples for n samples. WRITES left[i] / right[i] (see the header
    // comment for the scaling). ext0/ext1 are the stream's two inputs (EXTS0/1,
    // MAME's inputs[0/1].get(s), floats in MAME's +/-1.0 scale), or nullptr
    // for silence (unconnected inputs, as on coolridr).
    void generate(int samples, int32_t *left, int32_t *right,
                  const float *ext0 = nullptr, const float *ext1 = nullptr);
    // Same, but stores what MAME's put_int_clamp writes into the stream buffer.
    void generate_float(int samples, float *left, float *right,
                        const float *ext0 = nullptr, const float *ext1 = nullptr);

    // Divisor of the last generated samples: 131072 if DAC18B else 32768.
    int32_t output_max() const { return DAC18B() ? 131072 : 32768; }
    // MVOL / 15.0 as MAME's set_output_gain(0/1, MVOL() / 15.0).
    float output_gain() const { return m_output_gain; }

private:
    enum SCSP_STATE { SCSP_ATTACK, SCSP_DECAY1, SCSP_DECAY2, SCSP_RELEASE };

    struct SCSP_EG_t
    {
        int volume; //
        SCSP_STATE state;
        int step;
        //step vals
        int AR;     //Attack
        int D1R;    //Decay1
        int D2R;    //Decay2
        int RR;     //Release

        int DL;     //Decay level
        uint8_t EGHOLD;
        uint8_t LPLINK;
    };

    struct SCSP_LFO_t
    {
        uint16_t phase;
        uint32_t phase_step;
        int *table;
        int *scale;
    };

    struct SCSP_SLOT
    {
        // MAME: union { u16 data[0x10]; u8 datab[0x20]; } udata; datab is only
        // used for aligned 16-bit accesses, so data[] alone is equivalent.
        struct { uint16_t data[0x10]; } udata;  //only 0x1a bytes used

        uint8_t Backwards;    //the wave is playing backwards
        uint8_t active;   //this slot is currently playing
        uint32_t cur_addr;    //current play address (24.8)
        uint32_t nxt_addr;    //next play address
        uint32_t step;        //pitch step (24.8)
        SCSP_EG_t EG;            //Envelope
        SCSP_LFO_t PLFO;     //Phase LFO
        SCSP_LFO_t ALFO;     //Amplitude LFO
        int slot;
        int16_t Prev;  //Previous sample (for interpolation)
    };

    uint32_t m_clock = 0;
    float m_output_gain = 1.0f;
    uint32_t m_rand_seed = 0x9d14abd7;

    struct { uint16_t data[0x30/2]; } m_udata;

    SCSP_SLOT m_Slots[32];
    int16_t m_RINGBUF[128];
    uint8_t m_BUFPTR;

    uint32_t m_IrqTimA;
    uint32_t m_IrqTimBC;
    uint32_t m_IrqMidi;

    uint8_t m_MidiOutW, m_MidiOutR;
    uint8_t m_MidiStack[32];
    uint8_t m_MidiW, m_MidiR;

    int32_t m_EG_TABLE[0x400];

    int m_LPANTABLE[0x10000];
    int m_RPANTABLE[0x10000];

    int m_TimPris[3];
    int m_TimCnt[3];

    // DMA stuff
    struct
    {
        uint32_t dmea;
        uint16_t drga;
        uint16_t dtlg;
        uint8_t dgate;
        uint8_t ddir;
    } m_dma;

    uint16_t m_mcieb;
    uint16_t m_mcipd;

    int m_ARTABLE[64], m_DRTABLE[64];

    ScspDsp m_DSP;

    int16_t *m_RBUFDST;   //this points to where the sample will be stored in the RingBuf

    //LFO
    int m_PLFO_TRI[256], m_PLFO_SQR[256], m_PLFO_SAW[256], m_PLFO_NOI[256];
    int m_ALFO_TRI[256], m_ALFO_SQR[256], m_ALFO_SAW[256], m_ALFO_NOI[256];
    int m_PSCALES[8][256];
    int m_ASCALES[8][256];

    // register field helpers (MAME macros on m_udata)
    int DAC18B() const { return (m_udata.data[0] >> 0x0) & 0x0100; }

    // memory helpers: MAME's read_byte / read_word (device_rom_interface cache)
    // and space().write_word, with the 20-bit address mask
    uint8_t  mem_read_byte(uint32_t addr);
    uint16_t mem_read_word(uint32_t addr);
    void     mem_write_word(uint32_t addr, uint16_t data);
    static uint16_t dsp_read_thunk(void *ctx, uint32_t addr);
    static void dsp_write_thunk(void *ctx, uint32_t addr, uint16_t data);
    uint32_t machine_rand();
    void irq(uint32_t level, int state);
    void main_irq(int state);
    void arm_timer(int which, uint32_t time);

    void exec_dma();       /*state DMA transfer function*/
    uint8_t DecodeSCI(uint8_t irq);
    void CheckPendingIRQ();
    void MainCheckPendingIRQ(uint16_t irq_type);
    void ResetInterrupts();
    void timerA_cb();
    void timerB_cb();
    void timerC_cb();
    int Get_AR(int base, int R);
    int Get_DR(int base, int R);
    void Compute_EG(SCSP_SLOT *slot);
    int EG_Update(SCSP_SLOT *slot);
    uint32_t Step(SCSP_SLOT *slot);
    void Compute_LFO(SCSP_SLOT *slot);
    void StartSlot(SCSP_SLOT *slot);
    void StopSlot(SCSP_SLOT *slot, int keyoff);
    void init_chip();
    void UpdateSlotReg(int s, int r);
    void UpdateReg(int reg);
    void UpdateSlotRegR(int slot, int reg);
    void UpdateRegR(int reg);
    void w16(uint32_t addr, uint16_t val);
    uint16_t r16(uint32_t addr);
    inline int32_t UpdateSlot(SCSP_SLOT *slot);
    void DoMasterSamples(int samples, int32_t *left, int32_t *right,
                         const float *ext0, const float *ext1);

    //LFO
    void LFO_Init();
    int32_t PLFO_Step(SCSP_LFO_t *LFO);
    int32_t ALFO_Step(SCSP_LFO_t *LFO);
    void LFO_ComputeStep(SCSP_LFO_t *LFO, uint32_t LFOF, uint32_t LFOWS, uint32_t LFOS, int ALFO);
};
