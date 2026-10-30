/***************************************************************************
    Sega/Yamaha YMF292-F "SCSP" (Saturn Custom Sound Processor), standalone.

    Transcribed from MAME 0.264 src/devices/sound/scsp.cpp, scspdsp.cpp and
    scsplfo.ipp (BSD-3-Clause; copyright holders ElSemi, R. Belmont; thanks to
    kingshriek). At the mame0264 tag the LFO code is part of scsp.cpp (there is
    no scsplfo.ipp); it is transcribed at the end of this file.

    Original credits from scsp.cpp:
        By ElSemi
        MAME/M1 conversion and cleanup by R. Belmont
        Additional code and bugfixes by kingshriek

    This chip has 32 voices.  Each voice can play a sample or be part of
    an FM construct.  Unlike traditional Yamaha FM chips, the base waveform
    for the FM still comes from the wavetable RAM.

    Statements are kept in MAME's order. Differences from MAME are marked
    "PORT:" and are limited to the framework glue (stream, timers, devcb,
    address space, logging, save states); see scsp.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/scsp/scsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

// TODO : Envelope/LFO times are based on 44100Hz case?

#define SCSP_FM_DELAY    0    // delay in number of slots processed before samples are written to the FM ring buffer
                // driver code indicates should be 4, but sounds distorted then
                // PORT: only the SCSP_FM_DELAY == 0 paths of MAME are transcribed.

#define SHIFT   12
#define LFO_SHIFT   8
#define FIX(v)  ((uint32_t) ((float) (1 << SHIFT) * (v)))


#define EG_SHIFT    16

namespace {

// PORT: two's-complement wrapping 32-bit multiply. MAME writes 'step * PLFO_Step()'
// as a plain int multiply, which can exceed 32 bits for high octaves with deep
// pitch LFO; compiled MAME (imul) wraps. This yields the same bits without UB.
inline int32_t mul_wrap(int32_t a, int32_t b)
{
    return int32_t(uint32_t(a) * uint32_t(b));
}

}

/*
    SCSP features 32 programmable slots
    that can generate FM and PCM (from ROM/RAM) sound
*/

//SLOT PARAMETERS
#define KEYONEX(slot)   ((slot->udata.data[0x0] >> 0x0) & 0x1000)
#define KEYONB(slot)    ((slot->udata.data[0x0] >> 0x0) & 0x0800)
#define SBCTL(slot)     ((slot->udata.data[0x0] >> 0x9) & 0x0003)
#define SSCTL(slot)     ((slot->udata.data[0x0] >> 0x7) & 0x0003)
#define LPCTL(slot)     ((slot->udata.data[0x0] >> 0x5) & 0x0003)
#define PCM8B(slot)     ((slot->udata.data[0x0] >> 0x0) & 0x0010)

#define SA(slot)        (((slot->udata.data[0x0] & 0xF) << 16) | (slot->udata.data[0x1]))

#define LSA(slot)       (slot->udata.data[0x2])

#define LEA(slot)       (slot->udata.data[0x3])

#define D2R(slot)       ((slot->udata.data[0x4] >> 0xB) & 0x001F)
#define D1R(slot)       ((slot->udata.data[0x4] >> 0x6) & 0x001F)
#define EGHOLD(slot)    ((slot->udata.data[0x4] >> 0x0) & 0x0020)
#define AR(slot)        ((slot->udata.data[0x4] >> 0x0) & 0x001F)

#define LPSLNK(slot)    ((slot->udata.data[0x5] >> 0x0) & 0x4000)
#define KRS(slot)       ((slot->udata.data[0x5] >> 0xA) & 0x000F)
#define DL(slot)        ((slot->udata.data[0x5] >> 0x5) & 0x001F)
#define RR(slot)        ((slot->udata.data[0x5] >> 0x0) & 0x001F)

#define STWINH(slot)    ((slot->udata.data[0x6] >> 0x0) & 0x0200)
#define SDIR(slot)      ((slot->udata.data[0x6] >> 0x0) & 0x0100)
#define TL(slot)        ((slot->udata.data[0x6] >> 0x0) & 0x00FF)

#define MDL(slot)       ((slot->udata.data[0x7] >> 0xC) & 0x000F)
#define MDXSL(slot)     ((slot->udata.data[0x7] >> 0x6) & 0x003F)
#define MDYSL(slot)     ((slot->udata.data[0x7] >> 0x0) & 0x003F)

#define OCT(slot)       ((slot->udata.data[0x8] >> 0xB) & 0x000F)
#define FNS(slot)       ((slot->udata.data[0x8] >> 0x0) & 0x03FF)

#define LFORE(slot)     ((slot->udata.data[0x9] >> 0x0) & 0x8000)
#define LFOF(slot)      ((slot->udata.data[0x9] >> 0xA) & 0x001F)
#define PLFOWS(slot)    ((slot->udata.data[0x9] >> 0x8) & 0x0003)
#define PLFOS(slot)     ((slot->udata.data[0x9] >> 0x5) & 0x0007)
#define ALFOWS(slot)    ((slot->udata.data[0x9] >> 0x3) & 0x0003)
#define ALFOS(slot)     ((slot->udata.data[0x9] >> 0x0) & 0x0007)

#define ISEL(slot)      ((slot->udata.data[0xA] >> 0x3) & 0x000F)
#define IMXL(slot)      ((slot->udata.data[0xA] >> 0x0) & 0x0007)

#define DISDL(slot)     ((slot->udata.data[0xB] >> 0xD) & 0x0007)
#define DIPAN(slot)     ((slot->udata.data[0xB] >> 0x8) & 0x001F)
#define EFSDL(slot)     ((slot->udata.data[0xB] >> 0x5) & 0x0007)
#define EFPAN(slot)     ((slot->udata.data[0xB] >> 0x0) & 0x001F)

//Envelope times in ms
static const double ARTimes[64] = {100000/*infinity*/,100000/*infinity*/,8100.0,6900.0,6000.0,4800.0,4000.0,3400.0,3000.0,2400.0,2000.0,1700.0,1500.0,
                    1200.0,1000.0,860.0,760.0,600.0,500.0,430.0,380.0,300.0,250.0,220.0,190.0,150.0,130.0,110.0,95.0,
                    76.0,63.0,55.0,47.0,38.0,31.0,27.0,24.0,19.0,15.0,13.0,12.0,9.4,7.9,6.8,6.0,4.7,3.8,3.4,3.0,2.4,
                    2.0,1.8,1.6,1.3,1.1,0.93,0.85,0.65,0.53,0.44,0.40,0.35,0.0,0.0};
static const double DRTimes[64] = {100000/*infinity*/,100000/*infinity*/,118200.0,101300.0,88600.0,70900.0,59100.0,50700.0,44300.0,35500.0,29600.0,25300.0,22200.0,17700.0,
                    14800.0,12700.0,11100.0,8900.0,7400.0,6300.0,5500.0,4400.0,3700.0,3200.0,2800.0,2200.0,1800.0,1600.0,1400.0,1100.0,
                    920.0,790.0,690.0,550.0,460.0,390.0,340.0,270.0,230.0,200.0,170.0,140.0,110.0,98.0,85.0,68.0,57.0,49.0,43.0,34.0,
                    28.0,25.0,22.0,18.0,14.0,12.0,11.0,8.5,7.1,6.1,5.4,4.3,3.6,3.1};

#define MEM4B()     ((m_udata.data[0] >> 0x0) & 0x0200)
// DAC18B() is the inline member Scsp::DAC18B() (same expression)
#define MVOL()      ((m_udata.data[0] >> 0x0) & 0x000F)
#define RBL()       ((m_udata.data[1] >> 0x7) & 0x0003)
#define RBP()       ((m_udata.data[1] >> 0x0) & 0x003F)
#define MOFULL()    ((m_udata.data[2] >> 0x0) & 0x1000)
#define MOEMPTY()   ((m_udata.data[2] >> 0x0) & 0x0800)
#define MIOVF()     ((m_udata.data[2] >> 0x0) & 0x0400)
#define MIFULL()    ((m_udata.data[2] >> 0x0) & 0x0200)
#define MIEMPTY()   ((m_udata.data[2] >> 0x0) & 0x0100)

#define SCILV0()    ((m_udata.data[0x24/2] >> 0x0) & 0xff)
#define SCILV1()    ((m_udata.data[0x26/2] >> 0x0) & 0xff)
#define SCILV2()    ((m_udata.data[0x28/2] >> 0x0) & 0xff)

#define SCIEX0  0
#define SCIEX1  1
#define SCIEX2  2
#define SCIMID  3
#define SCIDMA  4
#define SCIIRQ  5
#define SCITMA  6
#define SCITMB  7

#define USEDSP

static const float SDLT[8] = {-1000000.0f,-36.0f,-30.0f,-24.0f,-18.0f,-12.0f,-6.0f,0.0f};

//-------------------------------------------------
//  PORT: framework glue
//-------------------------------------------------

Scsp::Scsp()
{
    // MAME's constructor: everything below starts zeroed
    std::memset(m_udata.data, 0, sizeof(m_udata.data));
    std::memset(static_cast<void *>(m_Slots), 0, sizeof(m_Slots));
    std::fill(std::begin(m_RINGBUF), std::end(m_RINGBUF), 0);
    m_BUFPTR = 0;
    m_IrqTimA = m_IrqTimBC = m_IrqMidi = 0;
    m_MidiOutW = m_MidiOutR = 0;
    std::fill(std::begin(m_MidiStack), std::end(m_MidiStack), 0);
    m_MidiW = m_MidiR = 0;
    std::fill(std::begin(m_EG_TABLE), std::end(m_EG_TABLE), 0);
    std::fill(std::begin(m_LPANTABLE), std::end(m_LPANTABLE), 0);
    std::fill(std::begin(m_RPANTABLE), std::end(m_RPANTABLE), 0);
    std::fill(std::begin(m_TimPris), std::end(m_TimPris), 0);
    m_TimCnt[0] = m_TimCnt[1] = m_TimCnt[2] = 0;
    m_dma = {};
    m_mcieb = 0;
    m_mcipd = 0;
    std::fill(std::begin(m_ARTABLE), std::end(m_ARTABLE), 0);
    std::fill(std::begin(m_DRTABLE), std::end(m_DRTABLE), 0);
    m_DSP.Init();
    m_RBUFDST = nullptr;
    std::fill(std::begin(m_PLFO_TRI), std::end(m_PLFO_TRI), 0);
    std::fill(std::begin(m_PLFO_SQR), std::end(m_PLFO_SQR), 0);
    std::fill(std::begin(m_PLFO_SAW), std::end(m_PLFO_SAW), 0);
    std::fill(std::begin(m_PLFO_NOI), std::end(m_PLFO_NOI), 0);
    std::fill(std::begin(m_ALFO_TRI), std::end(m_ALFO_TRI), 0);
    std::fill(std::begin(m_ALFO_SQR), std::end(m_ALFO_SQR), 0);
    std::fill(std::begin(m_ALFO_SAW), std::end(m_ALFO_SAW), 0);
    std::fill(std::begin(m_ALFO_NOI), std::end(m_ALFO_NOI), 0);
    std::memset(m_PSCALES, 0, sizeof(m_PSCALES));
    std::memset(m_ASCALES, 0, sizeof(m_ASCALES));
}

uint32_t Scsp::machine_rand()
{
    if (rand)
        return rand();
    // running_machine::rand()
    m_rand_seed = 1664525 * m_rand_seed + 1013904223;

    // return rotated by 16 bits; the low bits have a short period
    // and are frequently used
    return (m_rand_seed >> 16) | (m_rand_seed << 16);
}

// device_rom_interface<20, 1, 0, ENDIANNESS_BIG>: memory_access_cache::read_word
// does read_native(address & ~1), read_native masks with the 20-bit address mask
uint16_t Scsp::mem_read_word(uint32_t addr)
{
    return read_word ? read_word((addr & 0xFFFFF) & ~1u) : 0;
}

// read_byte on a big-endian 16-bit bus: the even byte is the high half of the word
uint8_t Scsp::mem_read_byte(uint32_t addr)
{
    uint16_t const w = mem_read_word(addr);
    return (addr & 1) ? uint8_t(w & 0xff) : uint8_t(w >> 8);
}

void Scsp::mem_write_word(uint32_t addr, uint16_t data)
{
    if (write_word)
        write_word((addr & 0xFFFFF) & ~1u, data);
}

uint16_t Scsp::dsp_read_thunk(void *ctx, uint32_t addr)
{
    return static_cast<Scsp *>(ctx)->mem_read_word(addr);
}

void Scsp::dsp_write_thunk(void *ctx, uint32_t addr, uint16_t data)
{
    static_cast<Scsp *>(ctx)->mem_write_word(addr, data);
}

// m_irq_cb(offset, data): a no-op while unset
void Scsp::irq(uint32_t level, int state)
{
    if (has_irq_cb && irq_cb)
        irq_cb(level, state);
}

// m_main_irq_cb(state): a no-op while unset
void Scsp::main_irq(int state)
{
    if (main_irq_cb)
        main_irq_cb(state);
}

// m_timerX->adjust(attotime::from_ticks(512, time))
void Scsp::arm_timer(int which, uint32_t time)
{
    ScspTimerPeriod p;
    p.hz = time;
    int64_t const attos_per_tick = int64_t(1000000000000000000LL / int64_t(time));
    if (512 < time)
    {
        p.seconds = 0;
        p.attoseconds = 512 * attos_per_tick;
    }
    else
    {
        p.seconds = int32_t(512 / time);
        p.attoseconds = int64_t(512 % time) * attos_per_tick;
    }
    uint64_t num = 512ull * uint64_t(m_clock / 512);
    uint64_t den = time;
    uint64_t a = num, b = den;
    while (b) { uint64_t t = a % b; a = b; b = t; }
    if (a) { num /= a; den /= a; }
    p.samples_num = num;
    p.samples_den = den;
    p.samples = double(num) / double(den);
    if (timer_arm)
        timer_arm(which, p);
}

void Scsp::timer_fire(int which)
{
    switch (which)
    {
        case 0: timerA_cb(); break;
        case 1: timerB_cb(); break;
        case 2: timerC_cb(); break;
    }
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void Scsp::init(uint32_t clock)
{
    // PORT: constructor state, then device_start(): init(); stream_alloc(2, 2,
    // clock() / 512). Save state registration is not ported.
    m_clock = clock;
    m_rand_seed = 0x9d14abd7;
    m_output_gain = 1.0f;   // stream outputs start with gain 1.0

    std::memset(m_udata.data, 0, sizeof(m_udata.data));
    std::memset(static_cast<void *>(m_Slots), 0, sizeof(m_Slots));
    std::fill(std::begin(m_RINGBUF), std::end(m_RINGBUF), 0);
    m_BUFPTR = 0;
    std::fill(std::begin(m_MidiStack), std::end(m_MidiStack), 0);
    std::fill(std::begin(m_TimPris), std::end(m_TimPris), 0);
    m_dma = {};
    m_mcieb = 0;
    m_mcipd = 0;
    m_RBUFDST = nullptr;

    init_chip();
}

uint8_t Scsp::DecodeSCI(uint8_t irq)
{
    uint8_t SCI = 0;
    uint8_t v;
    v = (SCILV0() & (1 << irq)) ? 1 : 0;
    SCI |= v;
    v = (SCILV1() & (1 << irq)) ? 1 : 0;
    SCI |= v << 1;
    v = (SCILV2() & (1 << irq)) ? 1 : 0;
    SCI |= v << 2;
    return SCI;
}

void Scsp::CheckPendingIRQ()
{
    uint32_t pend = m_udata.data[0x20/2];
    uint32_t en = m_udata.data[0x1e/2];
    if (m_MidiW != m_MidiR)
    {
        m_udata.data[0x20/2] |= 8;
        pend |= 8;
    }
    if (!pend)
        return;
    if (pend & 0x40)
        if (en & 0x40)
        {
            irq(m_IrqTimA, ASSERT_LINE);
            return;
        }
    if (pend & 0x80)
        if (en & 0x80)
        {
            irq(m_IrqTimBC, ASSERT_LINE);
            return;
        }
    if (pend & 0x100)
        if (en & 0x100)
        {
            irq(m_IrqTimBC, ASSERT_LINE);
            return;
        }
    if (pend & 8)
        if (en & 8)
        {
            irq(m_IrqMidi, ASSERT_LINE);
            m_udata.data[0x20/2] &= ~8;
            return;
        }

    irq(0, CLEAR_LINE);
}

void Scsp::MainCheckPendingIRQ(uint16_t irq_type)
{
    m_mcipd |= irq_type;

    //machine().scheduler().synchronize(); // force resync

    if (m_mcipd & m_mcieb)
        main_irq(1);
    else
        main_irq(0);
}

void Scsp::ResetInterrupts()
{
    uint32_t reset = m_udata.data[0x22/2];

    if (reset & 0x40)
    {
        irq(m_IrqTimA, CLEAR_LINE);
    }
    if (reset & 0x180)
    {
        irq(m_IrqTimBC, CLEAR_LINE);
    }
    if (reset & 0x8)
    {
        irq(m_IrqMidi, CLEAR_LINE);
    }

    CheckPendingIRQ();
}

void Scsp::timerA_cb()
{
    m_TimCnt[0] = 0xFFFF;
    m_udata.data[0x20/2] |= 0x40;
    m_udata.data[0x18/2] &= 0xff00;
    m_udata.data[0x18/2] |= m_TimCnt[0] >> 8;

    CheckPendingIRQ();
    MainCheckPendingIRQ(0x40);
}

void Scsp::timerB_cb()
{
    m_TimCnt[1] = 0xFFFF;
    m_udata.data[0x20/2] |= 0x80;
    m_udata.data[0x1a/2] &= 0xff00;
    m_udata.data[0x1a/2] |= m_TimCnt[1] >> 8;

    CheckPendingIRQ();
}

void Scsp::timerC_cb()
{
    m_TimCnt[2] = 0xFFFF;
    m_udata.data[0x20/2] |= 0x100;
    m_udata.data[0x1c/2] &= 0xff00;
    m_udata.data[0x1c/2] |= m_TimCnt[2] >> 8;

    CheckPendingIRQ();
}

int Scsp::Get_AR(int base, int R)
{
    int Rate = base + (R << 1);
    return m_ARTABLE[std::clamp(Rate, 0, 63)];
}

int Scsp::Get_DR(int base, int R)
{
    int Rate = base + (R << 1);
    return m_DRTABLE[std::clamp(Rate, 0, 63)];
}

void Scsp::Compute_EG(SCSP_SLOT *slot)
{
    int octave = (OCT(slot) ^ 8) - 8;
    int rate;
    if (KRS(slot) != 0xf)
        rate = octave + 2 * KRS(slot) + ((FNS(slot) >> 9) & 1);
    else
        rate = 0; //rate = ((FNS(slot) >> 9) & 1);

    slot->EG.volume = 0x17F<<EG_SHIFT;
    slot->EG.AR = Get_AR(rate,AR(slot));
    slot->EG.D1R = Get_DR(rate,D1R(slot));
    slot->EG.D2R = Get_DR(rate,D2R(slot));
    slot->EG.RR = Get_DR(rate,RR(slot));
    slot->EG.DL = 0x1f - DL(slot);
    slot->EG.EGHOLD = EGHOLD(slot);
}

int Scsp::EG_Update(SCSP_SLOT *slot)
{
    switch (slot->EG.state)
    {
        case SCSP_ATTACK:
            slot->EG.volume += slot->EG.AR;
            if (slot->EG.volume >= (0x3ff<<EG_SHIFT))
            {
                if (!LPSLNK(slot))
                {
                    slot->EG.state = SCSP_DECAY1;
                    if (slot->EG.D1R >= (1024 << EG_SHIFT)) //Skip SCSP_DECAY1, go directly to SCSP_DECAY2
                        slot->EG.state = SCSP_DECAY2;
                }
                slot->EG.volume=0x3ff << EG_SHIFT;
            }
            if (slot->EG.EGHOLD)
                return 0x3ff << (SHIFT - 10);
            break;
        case SCSP_DECAY1:
            slot->EG.volume -= slot->EG.D1R;
            if (slot->EG.volume <= 0)
                slot->EG.volume = 0;
            if (slot->EG.volume >> (EG_SHIFT + 5) <= slot->EG.DL)
                slot->EG.state = SCSP_DECAY2;
            break;
        case SCSP_DECAY2:
            if (D2R(slot) == 0)
                return (slot->EG.volume >> EG_SHIFT) << (SHIFT - 10);
            slot->EG.volume -= slot->EG.D2R;
            if (slot->EG.volume <= 0)
                slot->EG.volume = 0;

            break;
        case SCSP_RELEASE:
            slot->EG.volume -= slot->EG.RR;
            if (slot->EG.volume <= 0)
            {
                slot->EG.volume = 0;
                StopSlot(slot, 0);
                //slot->EG.volume = 0x17F << EG_SHIFT;
                //slot->EG.state = SCSP_ATTACK;
            }
            break;
        default:
            return 1 << SHIFT;
    }
    return (slot->EG.volume >> EG_SHIFT) << (SHIFT - 10);
}

uint32_t Scsp::Step(SCSP_SLOT *slot)
{
    int octave = (OCT(slot) ^ 8) - 8 + SHIFT - 10;
    uint32_t Fn = FNS(slot) + (1 << 10);
    if (octave >= 0)
    {
        Fn <<= octave;
    }
    else
    {
        Fn >>= -octave;
    }

    return Fn;
}


void Scsp::Compute_LFO(SCSP_SLOT *slot)
{
    if (PLFOS(slot) != 0)
        LFO_ComputeStep(&(slot->PLFO), LFOF(slot), PLFOWS(slot), PLFOS(slot), 0);
    if (ALFOS(slot) != 0)
        LFO_ComputeStep(&(slot->ALFO), LFOF(slot), ALFOWS(slot), ALFOS(slot), 1);
}

void Scsp::StartSlot(SCSP_SLOT *slot)
{
    slot->active = 1;
    slot->cur_addr = 0;
    slot->nxt_addr = 1 << SHIFT;
    slot->step = Step(slot);
    Compute_EG(slot);
    slot->EG.state = SCSP_ATTACK;
    slot->EG.volume = 0x17F << EG_SHIFT;
    slot->Prev = 0;
    slot->Backwards = 0;

    Compute_LFO(slot);
}

void Scsp::StopSlot(SCSP_SLOT *slot,int keyoff)
{
    if (keyoff /*&& slot->EG.state!=SCSP_RELEASE*/)
    {
        slot->EG.state = SCSP_RELEASE;
    }
    else
    {
        slot->active = 0;
    }
    slot->udata.data[0] &= ~0x800;
}

// MAME: scsp_device::init()
void Scsp::init_chip()
{
    int i;

    m_DSP.Init();

    m_IrqTimA = m_IrqTimBC = m_IrqMidi = 0;
    m_MidiR=m_MidiW = 0;
    m_MidiOutR = m_MidiOutW = 0;

    // MAME: m_DSP.space = &this->space();
    m_DSP.mem_ctx = this;
    m_DSP.mem_read_word = &Scsp::dsp_read_thunk;
    m_DSP.mem_write_word = &Scsp::dsp_write_thunk;
    // MAME: m_timerA/B/C = timer_alloc(...): the host owns the timers

    for (i = 0; i < 0x400; ++i)
    {
        float envDB = ((float)(3 * (i - 0x3ff))) / 32.0f;
        float scale = (float)(1 << SHIFT);
        m_EG_TABLE[i] = (int32_t)(powf(10.0f, envDB / 20.0f) * scale);
    }

    for (i = 0; i < 0x10000; ++i)
    {
        int iTL  = (i >> 0x0) & 0xff;
        int iPAN = (i >> 0x8) & 0x1f;
        int iSDL = (i >> 0xD) & 0x07;
        float TL;
        float SegaDB = 0.0f;
        float fSDL;
        float PAN;
        float LPAN,RPAN;

        if (iTL & 0x01) SegaDB -= 0.4f;
        if (iTL & 0x02) SegaDB -= 0.8f;
        if (iTL & 0x04) SegaDB -= 1.5f;
        if (iTL & 0x08) SegaDB -= 3.0f;
        if (iTL & 0x10) SegaDB -= 6.0f;
        if (iTL & 0x20) SegaDB -= 12.0f;
        if (iTL & 0x40) SegaDB -= 24.0f;
        if (iTL & 0x80) SegaDB -= 48.0f;

        TL=powf(10.0f, SegaDB / 20.0f);

        SegaDB=0;
        if (iPAN & 0x1) SegaDB -= 3.0f;
        if (iPAN & 0x2) SegaDB -= 6.0f;
        if (iPAN & 0x4) SegaDB -= 12.0f;
        if (iPAN & 0x8) SegaDB -= 24.0f;

        if ((iPAN & 0xf) == 0xf) PAN = 0.0;
        else PAN=powf(10.0f, SegaDB / 20.0f);

        if (iPAN < 0x10)
        {
            LPAN = PAN;
            RPAN = 1.0;
        }
        else
        {
            RPAN = PAN;
            LPAN = 1.0;
        }

        if (iSDL)
            fSDL = powf(10.0f, (SDLT[iSDL]) / 20.0f);
        else
            fSDL = 0.0;

        m_LPANTABLE[i] = FIX((4.0f * LPAN * TL * fSDL));
        m_RPANTABLE[i] = FIX((4.0f * RPAN * TL * fSDL));
    }

    m_ARTABLE[0] = m_DRTABLE[0] = 0;    //Infinite time
    m_ARTABLE[1] = m_DRTABLE[1] = 0;    //Infinite time
    for (i = 2; i < 64; ++i)
    {
        double step, scale;
        double t = ARTimes[i];   //In ms
        if (t != 0.0)
        {
            step = (1023 * 1000.0) / (44100.0 * t);
            scale = (double) (1 << EG_SHIFT);
            m_ARTABLE[i] = (int) (step * scale);
        }
        else
            m_ARTABLE[i] = 1024 << EG_SHIFT;

        t = DRTimes[i];   //In ms
        step = (1023 * 1000.0) / (44100.0 * t);
        scale = (double) (1 << EG_SHIFT);
        m_DRTABLE[i] = (int) (step * scale);
    }

    // make sure all the slots are off
    for (i = 0; i < 32; ++i)
    {
        m_Slots[i].slot = i;
        m_Slots[i].active = 0;
        m_Slots[i].EG.state = SCSP_RELEASE;
    }

    LFO_Init();
    // no "pend"
    m_udata.data[0x20/2] = 0;
    m_TimCnt[0] = 0xffff;
    m_TimCnt[1] = 0xffff;
    m_TimCnt[2] = 0xffff;
}

void Scsp::UpdateSlotReg(int s,int r)
{
    SCSP_SLOT *slot = m_Slots + s;
    switch (r & 0x3f)
    {
        case 0:
        case 1:
            if (KEYONEX(slot))
            {
                for (int sl=0; sl < 32; ++sl)
                {
                    SCSP_SLOT *s2 = m_Slots + sl;
                    {
                        if (KEYONB(s2) && s2->EG.state == SCSP_RELEASE/*&& !s2->active*/)
                        {
                            StartSlot(s2);
                        }
                        if (!KEYONB(s2) /*&& s2->active*/)
                        {
                            StopSlot(s2, 1);
                        }
                    }
                }
                slot->udata.data[0] &= ~0x1000;
            }
            break;
        case 0x10:
        case 0x11:
            slot->step = Step(slot);
            break;
        case 0xA:
        case 0xB:
            slot->EG.RR = Get_DR(0, RR(slot));
            slot->EG.DL = 0x1f - DL(slot);
            break;
        case 0x12:
        case 0x13:
            Compute_LFO(slot);
            break;
    }
}

void Scsp::UpdateReg(int reg)
{
    switch (reg & 0x3f)
    {
        case 0x0:
            // MAME: set_output_gain(0/1, MVOL() / 15.0)
            m_output_gain = float(MVOL() / 15.0);
            break;
        case 0x2:
        case 0x3:
            {
                m_DSP.RBL = (8 * 1024) << RBL(); // 8 / 16 / 32 / 64 kwords
                m_DSP.RBP = RBP();
            }
            break;
        case 0x6:
        case 0x7:
            midi_in(m_udata.data[0x6/2] & 0xff);
            break;
        case 8:
        case 9:
            /* Only MSLC could be written.  */
            m_udata.data[0x8/2] &= 0xf800; /**< @todo Docs claims MSLC to be 0x7800, but Jikkyou Parodius doesn't agree. */
            break;
        case 0x12:
        case 0x13:
            m_dma.dmea = (m_udata.data[0x12/2] & 0xfffe) | (m_dma.dmea & 0xf0000);
            break;
        case 0x14:
        case 0x15:
            m_dma.dmea = ((m_udata.data[0x14/2] & 0xf000) << 4) | (m_dma.dmea & 0xfffe);
            m_dma.drga = (m_udata.data[0x14/2] & 0x0ffe);
            break;
        case 0x16:
        case 0x17:
            m_dma.dtlg = (m_udata.data[0x16/2] & 0x0ffe);
            m_dma.ddir = (m_udata.data[0x16/2] & 0x2000) >> 13;
            m_dma.dgate = (m_udata.data[0x16/2] & 0x4000) >> 14;
            if (m_udata.data[0x16/2] & 0x1000) // dexe
                exec_dma();
            break;
        case 0x18:
        case 0x19:
            if (has_irq_cb)
            {
                m_TimPris[0] = 1 << ((m_udata.data[0x18/2] >> 8) & 0x7);
                m_TimCnt[0] = (m_udata.data[0x18/2] & 0xff) << 8;

                if ((m_udata.data[0x18/2] & 0xff) != 255)
                {
                    uint32_t time = (m_clock / m_TimPris[0]) / (255 - (m_udata.data[0x18/2] & 0xff));
                    if (time)
                    {
                        arm_timer(0, time);
                    }
                }
            }
            break;
        case 0x1a:
        case 0x1b:
            if (has_irq_cb)
            {
                m_TimPris[1] = 1 << ((m_udata.data[0x1A/2] >> 8) & 0x7);
                m_TimCnt[1] = (m_udata.data[0x1A/2] & 0xff) << 8;

                if ((m_udata.data[0x1A/2] & 0xff) != 255)
                {
                    uint32_t time = (m_clock / m_TimPris[1]) / (255 - (m_udata.data[0x1A/2] & 0xff));
                    if (time)
                    {
                        arm_timer(1, time);
                    }
                }
            }
            break;
        case 0x1C:
        case 0x1D:
            if (has_irq_cb)
            {
                m_TimPris[2] = 1 << ((m_udata.data[0x1C/2] >> 8) & 0x7);
                m_TimCnt[2] = (m_udata.data[0x1C/2] & 0xff) << 8;

                if ((m_udata.data[0x1C/2] & 0xff) != 255)
                {
                    uint32_t time = (m_clock / m_TimPris[2]) / (255 - (m_udata.data[0x1C/2] & 0xff));
                    if (time)
                    {
                        arm_timer(2, time);
                    }
                }
            }
            break;
        case 0x1e: // SCIEB
        case 0x1f:
            if (has_irq_cb)
            {
                CheckPendingIRQ();

                // MAME: popmessage("SCSP SCIEB enabled ...") when SCIEB & 0x610
            }
            break;
        case 0x20: // SCIPD
        case 0x21:
            if (has_irq_cb)
            {
                // MAME: popmessage("SCSP SCIPD write ...") when SCIEB & SCIPD & 0x20
            }
            break;
        case 0x22:  //SCIRE
        case 0x23:

            if (has_irq_cb)
            {
                m_udata.data[0x20/2] &= ~m_udata.data[0x22/2];
                ResetInterrupts();

                // behavior from real hardware: if you SCIRE a timer that's expired,
                // it'll immediately pop up again in SCIPD.  ask Sakura Taisen on the Saturn...
                if (m_TimCnt[0] == 0xffff)
                {
                    m_udata.data[0x20/2] |= 0x40;
                }
                if (m_TimCnt[1] == 0xffff)
                {
                    m_udata.data[0x20/2] |= 0x80;
                }
                if (m_TimCnt[2] == 0xffff)
                {
                    m_udata.data[0x20/2] |= 0x100;
                }
            }
            break;
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
            if (has_irq_cb)
            {
                m_IrqTimA = DecodeSCI(SCITMA);
                m_IrqTimBC = DecodeSCI(SCITMB);
                m_IrqMidi = DecodeSCI(SCIMID);
            }
            break;
        case 0x2a:
        case 0x2b:
            m_mcieb = m_udata.data[0x2a/2];

            MainCheckPendingIRQ(0);
            // MAME: popmessage("SCSP MCIEB enabled ...") when m_mcieb & ~0x60
            break;
        case 0x2c:
        case 0x2d:
            if (m_udata.data[0x2c/2] & 0x20)
                MainCheckPendingIRQ(0x20);
            break;
        case 0x2e:
        case 0x2f:
            m_mcipd &= ~m_udata.data[0x2e/2];
            MainCheckPendingIRQ(0);
            break;

    }
}

void Scsp::UpdateSlotRegR(int slot,int reg)
{
    (void)slot;
    (void)reg;
}

void Scsp::UpdateRegR(int reg)
{
    switch (reg & 0x3f)
    {
        case 4:
        case 5:
            {
                uint16_t v = m_udata.data[0x4/2];
                v &= 0xff00;
                v |= m_MidiStack[m_MidiR];
                irq(m_IrqMidi, CLEAR_LINE);   // cancel the IRQ
                // MAME: logerror("Read %x from SCSP MIDI\n", v);
                if (m_MidiR != m_MidiW)
                {
                    ++m_MidiR;
                    m_MidiR &= 31;
                }
                m_udata.data[0x4/2] = v;
            }
            break;
        case 8:
        case 9:
            {
                // MSLC     |  CA   |SGC|EG
                // f e d c b a 9 8 7 6 5 4 3 2 1 0
                uint8_t MSLC = (m_udata.data[0x8/2] >> 11) & 0x1f;
                SCSP_SLOT *slot = m_Slots + MSLC;
                uint32_t SGC = (slot->EG.state) & 3;
                uint32_t CA = (slot->cur_addr >> (SHIFT + 12)) & 0xf;
                uint32_t EG = (0x1f - (slot->EG.volume >> (EG_SHIFT + 5))) & 0x1f;
                /* note: according to the manual MSLC is write only, CA, SGC and EG read only.  */
                m_udata.data[0x8/2] =  /*(MSLC << 11) |*/ (CA << 7) | (SGC << 5) | EG;
            }
            break;

        case 0x18:
        case 0x19:
            break;

        case 0x1a:
        case 0x1b:
            break;

        case 0x1c:
        case 0x1d:
            break;

        case 0x2a:
        case 0x2b:
            m_udata.data[0x2a/2] = m_mcieb;
            break;

        case 0x2c:
        case 0x2d:
            m_udata.data[0x2c/2] = m_mcipd;
            break;
    }
}

// PORT: MAME stores through u16 pointers into the byte views (datab + addr).
// Every caller passes an even addr (offset * 2, or DRGA which is & 0x0ffe and
// steps by 2), so data[addr >> 1] is the same word.
void Scsp::w16(uint32_t addr, uint16_t val)
{
    addr &= 0xffff;
    if (addr < 0x400)
    {
        int slot = addr / 0x20;
        addr &= 0x1f;
        m_Slots[slot].udata.data[addr >> 1] = val;
        UpdateSlotReg(slot, addr & 0x1f);
    }
    else if (addr < 0x600)
    {
        if (addr < 0x430)
        {
            m_udata.data[(addr & 0x3f) >> 1] = val;
            UpdateReg(addr & 0x3f);
        }
    }
    else if (addr < 0x700)
        m_RINGBUF[(addr - 0x600)/2] = val;
    else
    {
        //DSP
        if (addr < 0x780)  //COEF
            *((uint16_t *) (m_DSP.COEF + (addr - 0x700) / 2)) = val;
        else if (addr < 0x7c0)
            *((uint16_t *) (m_DSP.MADRS + (addr - 0x780) / 2)) = val;
        else if (addr < 0x800) // MADRS is mirrored twice
            *((uint16_t *) (m_DSP.MADRS + (addr - 0x7c0) / 2)) = val;
        else if (addr < 0xC00)
        {
            *((uint16_t *) (m_DSP.MPRO + (addr - 0x800) / 2)) = val;

            if (addr == 0xBF0)
            {
                m_DSP.Start();
            }
        }
    }
}

uint16_t Scsp::r16(uint32_t addr)
{
    uint16_t v = 0;
    addr &= 0xffff;
    if (addr < 0x400)
    {
        int slot = addr / 0x20;
        addr &= 0x1f;
        UpdateSlotRegR(slot, addr & 0x1f);
        v = m_Slots[slot].udata.data[addr >> 1];
    }
    else if (addr < 0x600)
    {
        if (addr < 0x430)
        {
            UpdateRegR(addr & 0x3f);
            v = m_udata.data[(addr & 0x3f) >> 1];
        }
    }
    else if (addr < 0x700)
        v = m_RINGBUF[(addr-0x600)/2];
    else
    {
        //DSP
        if (addr < 0x780)  //COEF
            v= *((uint16_t *) (m_DSP.COEF + (addr - 0x700) / 2));
        else if (addr < 0x7c0)
            v= *((uint16_t *) (m_DSP.MADRS + (addr - 0x780) / 2));
        else if (addr < 0x800)
            v= *((uint16_t *) (m_DSP.MADRS + (addr - 0x7c0) / 2));
        else if (addr < 0xC00)
            v= *((uint16_t *) (m_DSP.MPRO + (addr - 0x800) / 2));
        else if (addr < 0xE00)
        {
            if (addr & 2)
                v = m_DSP.TEMP[(addr >> 2) & 0x7f] & 0xffff;
            else
                v = m_DSP.TEMP[(addr >> 2) & 0x7f] >> 16;
        }
        else if (addr < 0xE80)
        {
            if (addr & 2)
                v = m_DSP.MEMS[(addr >> 2) & 0x1f] & 0xffff;
            else
                v = m_DSP.MEMS[(addr >> 2) & 0x1f] >> 16;
        }
        else if (addr < 0xEC0)
        {
            if (addr & 2)
                v = m_DSP.MIXS[(addr >> 2) & 0xf] & 0xffff;
            else
                v = m_DSP.MIXS[(addr >> 2) & 0xf] >> 16;
        }
        else if (addr < 0xEE0)
            v = *((uint16_t *) (m_DSP.EFREG + (addr - 0xec0) / 2));
        else
        {
            /**!
            @todo Kyuutenkai reads from 0xee0/0xee2, it's tied with EXTS register(s) also used for CD-Rom Player equalizer.
            This port is actually an external parallel port, directly connected from the CD Block device, hence code is a bit of an hack.
            (MAME's 68000 listing of the Kyuutenkai code is not reproduced here.)
            */
            // MAME: logerror("SCSP: Reading from EXTS register %08x\n", addr);
            if (addr < 0xEE4)
                v = *((uint16_t *) (m_DSP.EXTS + (addr - 0xee0) / 2));
        }
    }
    return v;
}


inline int32_t Scsp::UpdateSlot(SCSP_SLOT *slot)
{
    if (SSCTL(slot) == 3) // manual says cannot be used
    {
        // MAME: logerror("SCSP: Invaild SSCTL setting at slot %02x\n", slot->slot);
        return 0;
    }

    int32_t sample = 0; // NB: Shouldn't be necessary, but GCC 8.2.1 claims otherwise.
    int step = slot->step;
    uint32_t addr1, addr2, addr_select;                                   // current and next sample addresses
    uint32_t *addr[2]      = {&addr1, &addr2};                          // used for linear interpolation
    uint32_t *slot_addr[2] = {&(slot->cur_addr), &(slot->nxt_addr)};    //

    if (PLFOS(slot) != 0)
    {
        step = mul_wrap(step, PLFO_Step(&(slot->PLFO)));   // MAME: step = step * PLFO_Step(...)
        step >>= SHIFT;
    }

    if (PCM8B(slot))
    {
        addr1 = slot->cur_addr >> SHIFT;
        addr2 = slot->nxt_addr >> SHIFT;
    }
    else
    {
        addr1 = (slot->cur_addr >> (SHIFT - 1)) & ~1;
        addr2 = (slot->nxt_addr >> (SHIFT - 1)) & ~1;
    }

    if (MDL(slot) != 0 || MDXSL(slot) != 0 || MDYSL(slot) != 0)
    {
        int32_t smp = (m_RINGBUF[(m_BUFPTR + MDXSL(slot)) & 63] + m_RINGBUF[(m_BUFPTR + MDYSL(slot)) & 63]) / 2;

        smp <<= 0xA; // associate cycle with 1024
        smp >>= 0x1A - MDL(slot); // ex. for MDL=0xF, sample range corresponds to +/- 64 pi (32=2^5 cycles) so shift by 11 (16-5 == 0x1A-0xF)
        if (!PCM8B(slot)) smp <<= 1;

        addr1 += smp; addr2 += smp;
    }

    if (SSCTL(slot) == 0) // External DRAM data
    {
        if (PCM8B(slot)) //8 bit signed
        {
            int8_t p1 = mem_read_byte(SA(slot) + addr1);
            int8_t p2 = mem_read_byte(SA(slot) + addr2);
            int32_t s;
            int32_t fpart=slot->cur_addr & ((1 << SHIFT) - 1);
            s = (int) (p1 << 8) * ((1 << SHIFT) - fpart) + (int) (p2 << 8) * fpart;
            sample = (s >> SHIFT);
        }
        else    //16 bit signed (endianness?)
        {
            int16_t p1 = mem_read_word(SA(slot) + addr1);
            int16_t p2 = mem_read_word(SA(slot) + addr2);
            int32_t s;
            int32_t fpart = slot->cur_addr & ((1 << SHIFT) - 1);
            s = (int)(p1) * ((1 << SHIFT) - fpart) + (int)(p2) * fpart;
            sample = (s >> SHIFT);
        }
    }
    else if (SSCTL(slot) == 1)  // Internally generated data (Noise)
        sample = (int16_t)(machine_rand() & 0xffff); // Unknown algorithm
    else if (SSCTL(slot) >= 2)  // Internally generated data (All 0)
        sample = 0;

    if (SBCTL(slot) & 0x1)
        sample ^= 0x7FFF;
    if (SBCTL(slot) & 0x2)
        sample = (int16_t)(sample ^ 0x8000);

    if (slot->Backwards)
        slot->cur_addr -= step;
    else
        slot->cur_addr += step;
    slot->nxt_addr = slot->cur_addr + (1 << SHIFT);

    addr1 = slot->cur_addr >> SHIFT;
    addr2 = slot->nxt_addr >> SHIFT;

    if (addr1 >= LSA(slot) && !(slot->Backwards))
    {
        if (LPSLNK(slot) && slot->EG.state == SCSP_ATTACK)
            slot->EG.state = SCSP_DECAY1;
    }

    for (addr_select = 0; addr_select < 2; addr_select++)
    {
        int32_t rem_addr;
        switch (LPCTL(slot))
        {
        case 0: //no loop
            if (*addr[addr_select] >= LSA(slot) && *addr[addr_select] >= LEA(slot))
            {
                //slot->active=0;
                StopSlot(slot, 0);
            }
            break;
        case 1: //normal loop
            if (*addr[addr_select] >= LEA(slot))
            {
                rem_addr = *slot_addr[addr_select] - (LEA(slot) << SHIFT);
                *slot_addr[addr_select] = (LSA(slot) << SHIFT) + rem_addr;
            }
            break;
        case 2: //reverse loop
            if ((*addr[addr_select] >= LSA(slot)) && !(slot->Backwards))
            {
                rem_addr = *slot_addr[addr_select] - (LSA(slot) << SHIFT);
                *slot_addr[addr_select] = (LEA(slot) << SHIFT) - rem_addr;
                slot->Backwards = 1;
            }
            else if ((*addr[addr_select] < LSA(slot) || (*slot_addr[addr_select] & 0x80000000)) && slot->Backwards)
            {
                rem_addr = (LSA(slot) << SHIFT) - *slot_addr[addr_select];
                *slot_addr[addr_select] = (LEA(slot) << SHIFT) - rem_addr;
            }
            break;
        case 3: //ping-pong
            if (*addr[addr_select] >= LEA(slot)) //reached end, reverse till start
            {
                rem_addr = *slot_addr[addr_select] - (LEA(slot) << SHIFT);
                *slot_addr[addr_select] = (LEA(slot) << SHIFT) - rem_addr;
                slot->Backwards = 1;
            }
            else if ((*addr[addr_select] < LSA(slot) || (*slot_addr[addr_select] & 0x80000000)) && slot->Backwards)//reached start or negative
            {
                rem_addr = (LSA(slot) << SHIFT) - *slot_addr[addr_select];
                *slot_addr[addr_select] = (LSA(slot) << SHIFT) + rem_addr;
                slot->Backwards = 0;
            }
            break;
        }
    }

    if (!SDIR(slot))
    {
        if (ALFOS(slot) != 0)
        {
            sample = sample * ALFO_Step(&(slot->ALFO));
            sample >>= SHIFT;
        }

        if (slot->EG.state == SCSP_ATTACK)
            sample = (sample * EG_Update(slot)) >> SHIFT;
        else
            sample = (sample * m_EG_TABLE[EG_Update(slot) >> (SHIFT - 10)]) >> SHIFT;
    }

    if (!STWINH(slot))
    {
        if (!SDIR(slot))
        {
            uint16_t Enc = ((TL(slot)) << 0x0) | (0x7 << 0xd);
            *m_RBUFDST = (sample * m_LPANTABLE[Enc]) >> (SHIFT + 1);
        }
        else
        {
            uint16_t Enc = (0 << 0x0) | (0x7 << 0xd);
            *m_RBUFDST = (sample * m_LPANTABLE[Enc]) >> (SHIFT + 1);
        }
    }

    return sample;
}

void Scsp::DoMasterSamples(int samples, int32_t *left, int32_t *right,
                           const float *ext0, const float *ext1)
{
    const float *const inputs[2] = { ext0, ext1 };

    for (int s = 0; s < samples; ++s)
    {
        int32_t smpl = 0, smpr = 0;

        for (int sl = 0; sl < 32; ++sl)
        {
            m_RBUFDST = m_RINGBUF + m_BUFPTR;
            if (m_Slots[sl].active)
            {
                SCSP_SLOT *slot = m_Slots + sl;
                uint16_t Enc;

                int32_t sample = UpdateSlot(slot);

                Enc = ((TL(slot)) << 0x0) | ((IMXL(slot)) << 0xd);
                m_DSP.SetSample((sample*m_LPANTABLE[Enc]) >> (SHIFT-2), ISEL(slot), IMXL(slot));
                Enc = ((TL(slot)) << 0x0) | ((DIPAN(slot)) << 0x8) | ((DISDL(slot)) << 0xd);
                {
                    smpl += (sample * m_LPANTABLE[Enc]) >> SHIFT;
                    smpr += (sample * m_RPANTABLE[Enc]) >> SHIFT;
                }
            }

            ++m_BUFPTR;
            m_BUFPTR &= 63;
        }

        m_DSP.Step();

        for (int i = 0; i < 16; ++i)
        {
            SCSP_SLOT *slot = m_Slots + i;
            if (EFSDL(slot))
            {
                uint16_t Enc = ((EFPAN(slot)) << 0x8) | ((EFSDL(slot)) << 0xd);
                smpl += (m_DSP.EFREG[i] * m_LPANTABLE[Enc]) >> SHIFT;
                smpr += (m_DSP.EFREG[i] * m_RPANTABLE[Enc]) >> SHIFT;
            }
        }

        for (int i = 0; i < 2; ++i)
        {
            SCSP_SLOT *slot = m_Slots + i + 16; // 100217, 100237 EFSDL, EFPAN for EXTS0/1
            if (EFSDL(slot))
            {
                // MAME: m_DSP.EXTS[i] = s32(inputs[i].get(s) * 32768.0);
                float const in = inputs[i] ? inputs[i][s] : 0.0f;
                m_DSP.EXTS[i] = int32_t(in * 32768.0);
                uint16_t Enc = ((EFPAN(slot)) << 0x8) | ((EFSDL(slot)) << 0xd);
                smpl += (m_DSP.EXTS[i] * m_LPANTABLE[Enc]) >> SHIFT;
                smpr += (m_DSP.EXTS[i] * m_RPANTABLE[Enc]) >> SHIFT;
            }
        }

        // MAME: bufl/bufr.put_int_clamp(s, v, max): the clamped integer is output here
        if (DAC18B())
        {
            left[s]  = std::clamp<int32_t>(smpl, -131072, 131072);
            right[s] = std::clamp<int32_t>(smpr, -131072, 131072);
        }
        else
        {
            left[s]  = std::clamp<int32_t>(smpl >> 2, -32768, 32768);
            right[s] = std::clamp<int32_t>(smpr >> 2, -32768, 32768);
        }
    }
}

void Scsp::generate(int samples, int32_t *left, int32_t *right,
                    const float *ext0, const float *ext1)
{
    DoMasterSamples(samples, left, right, ext0, ext1);
}

void Scsp::generate_float(int samples, float *left, float *right,
                          const float *ext0, const float *ext1)
{
    int32_t bl[256], br[256];
    int done = 0;
    while (done < samples)
    {
        int const n = std::min(samples - done, 256);
        DoMasterSamples(n, bl, br, ext0 ? ext0 + done : nullptr, ext1 ? ext1 + done : nullptr);
        // write_stream_view::put_int: sample_t(sample) * (1.0f / sample_t(max)).
        // DAC18B cannot change inside DoMasterSamples (no register writes).
        float const k = 1.0f / float(output_max());
        for (int i = 0; i < n; i++)
        {
            left[done + i]  = float(bl[i]) * k;
            right[done + i] = float(br[i]) * k;
        }
        done += n;
    }
}

/* TODO: this needs to be timer-ized */
void Scsp::exec_dma()
{
    // PORT: MAME has 'static u16 tmp_dma[3]' (shared by every SCSP); it is
    // written and read back inside the same call, so a local is equivalent.
    uint16_t tmp_dma[3] = { 0, 0, 0 };
    int i;

    // MAME: logerror("SCSP: DMA transfer START ...")

    /* Copy the dma values in a temp storage for resuming later */
        /* (DMA *can't* overwrite its parameters).                  */
    if (!(m_dma.ddir))
    {
        for (i = 0; i < 3; i++)
            tmp_dma[i] = m_udata.data[(0x12 + (i * 2)) / 2];
    }

    /* note: we don't use space.read_word / write_word because it can happen that SH-2 enables the DMA instead of m68k. */
    /* TODO: don't know if params auto-updates, I guess not ... */
    if (m_dma.ddir)
    {
        if (m_dma.dgate)
        {
            // MAME: popmessage("Check: SCSP DMA DGATE enabled, contact MAME/MESSdev");
            for (i = 0; i < m_dma.dtlg; i += 2)
            {
                mem_write_word(m_dma.dmea, 0);   // MAME: this->space().write_word
                m_dma.dmea += 2;
            }
        }
        else
        {
            for (i = 0; i < m_dma.dtlg; i += 2)
            {
                uint16_t tmp;
                tmp = r16(m_dma.drga);
                mem_write_word(m_dma.dmea, tmp);   // MAME: this->space().write_word
                m_dma.dmea += 2;
                m_dma.drga += 2;
            }
        }
    }
    else
    {
        if (m_dma.dgate)
        {
            // MAME: popmessage("Check: SCSP DMA DGATE enabled, contact MAME/MESSdev");
            for (i = 0; i < m_dma.dtlg; i += 2)
            {
                w16(m_dma.drga, 0);
                m_dma.drga += 2;
            }
        }
        else
        {
            for (i = 0; i < m_dma.dtlg; i += 2)
            {
                uint16_t tmp = mem_read_word(m_dma.dmea);   // MAME: read_word (rom cache)
                w16(m_dma.drga, tmp);
                m_dma.dmea += 2;
                m_dma.drga += 2;
            }
        }
    }

    /*Resume the values*/
    if (!(m_dma.ddir))
    {
        for (i = 0; i < 3; i++)
            m_udata.data[(0x12 + (i * 2)) / 2] = tmp_dma[i];
    }

    /* Job done */
    m_udata.data[0x16/2] &= ~0x1000;
    /* request a dma end irq (TODO: make it inside the interface) */
    if (m_udata.data[0x1e/2] & 0x10)
    {
        // MAME: popmessage("SCSP DMA IRQ triggered, contact MAMEdev");
        irq(DecodeSCI(SCIDMA), HOLD_LINE);
    }
}


uint16_t Scsp::read(uint32_t offset, uint16_t mem_mask)
{
    (void)mem_mask;
    // MAME: m_stream->update(); (the host has called generate() up to now)
    return r16(offset * 2);
}

void Scsp::write(uint32_t offset, uint16_t data, uint16_t mem_mask)
{
    // MAME: m_stream->update(); (the host has called generate() up to now)

    uint16_t tmp = r16(offset * 2);
    tmp = (tmp & ~mem_mask) | (data & mem_mask);   // COMBINE_DATA(&tmp);
    w16(offset * 2, tmp);
}

void Scsp::midi_in(uint8_t data)
{
    //    printf("scsp_midi_in: %02x\n", data);

    m_MidiStack[m_MidiW++] = data;
    m_MidiW &= 31;

    CheckPendingIRQ();
}

uint16_t Scsp::midi_out_r()
{
    uint8_t val = m_MidiStack[m_MidiR++];
    m_MidiR &= 31;
    return val;
}

//LFO handling

#define LFIX(v) ((uint32_t) ((float) (1 << LFO_SHIFT) * (v)))

//Convert DB to multiply amplitude
#define DB(v)   LFIX(powf(10.0f, v / 20.0f))

//Convert cents to step increment
#define CENTS(v) LFIX(powf(2.0f, v / 1200.0f))


static const float LFOFreq[32] =
{
    0.17f,0.19f,0.23f,0.27f,0.34f,0.39f,0.45f,0.55f,0.68f,0.78f,0.92f,1.10f,1.39f,1.60f,1.87f,2.27f,
    2.87f,3.31f,3.92f,4.79f,6.15f,7.18f,8.60f,10.8f,14.4f,17.2f,21.5f,28.7f,43.1f,57.4f,86.1f,172.3f
};
static const float ASCALE[8] = {0.0f,0.4f,0.8f,1.5f,3.0f,6.0f,12.0f,24.0f};
static const float PSCALE[8] = {0.0f,7.0f,13.5f,27.0f,55.0f,112.0f,230.0f,494.0f};


void Scsp::LFO_Init()
{
    for (int i = 0; i < 256; ++i)
    {
        int a,p;
//      float TL;
        //Saw
        a = 255-i;
        if (i < 128)
            p = i;
        else
            p = i - 256;
        m_ALFO_SAW[i] = a;
        m_PLFO_SAW[i] = p;

        //Square
        if (i < 128)
        {
            a = 255;
            p = 127;
        }
        else
        {
            a = 0;
            p = -128;
        }
        m_ALFO_SQR[i] = a;
        m_PLFO_SQR[i] = p;

        //Tri
        if (i < 128)
            a = 255 - (i * 2);
        else
            a = (i * 2) - 256;
        if (i < 64)
            p = i * 2;
        else if (i < 128)
            p = 255 - i * 2;
        else if (i < 192)
            p = 256 - i * 2;
        else
            p = i * 2 - 511;
        m_ALFO_TRI[i] = a;
        m_PLFO_TRI[i] = p;

        //noise
        //a=lfo_noise[i];
        a = machine_rand() & 0xff;
        p = 128 - a;
        m_ALFO_NOI[i] = a;
        m_PLFO_NOI[i] = p;
    }

    for (int s = 0; s < 8; ++s)
    {
        float limit = PSCALE[s];
        for (int i = -128; i < 128; ++i)
        {
            m_PSCALES[s][i+128] = CENTS(((limit * (float) i) / 128.0f));
        }
        limit = -ASCALE[s];
        for (int i = 0; i < 256; ++i)
        {
            m_ASCALES[s][i] = DB(((limit * (float) i) / 256.0f));
        }
    }
}

int32_t Scsp::PLFO_Step(SCSP_LFO_t *LFO)
{
    int p;
    LFO->phase += LFO->phase_step;
#if LFO_SHIFT!=8
    LFO->phase &= (1 << (LFO_SHIFT + 8)) - 1;
#endif
    p=LFO->table[LFO->phase >> LFO_SHIFT];
    p=LFO->scale[p+128];
    return p << (SHIFT - LFO_SHIFT);
}

int32_t Scsp::ALFO_Step(SCSP_LFO_t *LFO)
{
    int p;
    LFO->phase += LFO->phase_step;
#if LFO_SHIFT!=8
    LFO->phase &= (1 << (LFO_SHIFT + 8)) - 1;
#endif
    p=LFO->table[LFO->phase >> LFO_SHIFT];
    p=LFO->scale[p];
    return p << (SHIFT - LFO_SHIFT);
}

void Scsp::LFO_ComputeStep(SCSP_LFO_t *LFO,uint32_t LFOF,uint32_t LFOWS,uint32_t LFOS,int ALFO)
{
    float step = (float) LFOFreq[LFOF] * 256.0f / 44100.0f;
    LFO->phase_step = (uint32_t) ((float) (1 << LFO_SHIFT) * step);
    if (ALFO)
    {
        switch (LFOWS)
        {
            case 0: LFO->table = m_ALFO_SAW; break;
            case 1: LFO->table = m_ALFO_SQR; break;
            case 2: LFO->table = m_ALFO_TRI; break;
            case 3: LFO->table = m_ALFO_NOI; break;
        }
        LFO->scale = m_ASCALES[LFOS];
    }
    else
    {
        switch (LFOWS)
        {
            case 0: LFO->table = m_PLFO_SAW; break;
            case 1: LFO->table = m_PLFO_SQR; break;
            case 2: LFO->table = m_PLFO_TRI; break;
            case 3: LFO->table = m_PLFO_NOI; break;
        }
        LFO->scale = m_PSCALES[LFOS];
    }
}
