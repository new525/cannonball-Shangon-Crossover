/***************************************************************************
    Cool Riders' music on its own sound board. See crmusic.hpp.
***************************************************************************/
#include "crossover/crmusic.hpp"
#include "crossover/crsound.hpp"
#include "crossover/romset.hpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// The main SH-2's writes to the shared RAM ($03F00000 + offset) in its first 50 frames after
// power-on, as MAME 0.264 recorded them (coolridr, write taps): attoseconds after power-on,
// offset, data, mask. They are the boot handshake (words $040-$0B0, "SEGA" at $08C), the ring's
// first eight entries (the stop sequence for both sides) with the write index at $501, and the
// heartbeat words $004/$080 the program writes each frame.
static const struct { int64_t atto; uint32_t off, data, mask; } BOOT_WRITES[] =
{
    { 46888535713910606LL, 0x040, 0x00000000U, 0x0000ff00U },
    { 46888678571053462LL, 0x048, 0x00000000U, 0xff000000U },
    { 46888714285339176LL, 0x08c, 0x20000000U, 0xff000000U },
    { 46888785713910604LL, 0x08c, 0x00200000U, 0x00ff0000U },
    { 46888857142482032LL, 0x08c, 0x00002000U, 0x0000ff00U },
    { 46888928571053460LL, 0x08c, 0x00000020U, 0x000000ffU },
    { 121787964284739982LL, 0x0ac, 0x00000000U, 0x0000ff00U },
    { 121787999999025696LL, 0x0ac, 0x00000000U, 0x000000ffU },
    { 121788285713311408LL, 0x0b0, 0x13000000U, 0xff000000U },
    { 121788357141882836LL, 0x0ac, 0x00000001U, 0x000000ffU },
    { 121788464284739978LL, 0x0ac, 0x00000001U, 0x000000ffU },
    { 121788821427597118LL, 0x0b0, 0x00000000U, 0x00ff0000U },
    { 121788892856168546LL, 0x0ac, 0x00000002U, 0x000000ffU },
    { 121788999999025688LL, 0x0ac, 0x00000002U, 0x000000ffU },
    { 121789357141882828LL, 0x0b0, 0x00000000U, 0x0000ff00U },
    { 121789428570454256LL, 0x0ac, 0x00000003U, 0x000000ffU },
    { 121789535713311398LL, 0x0ac, 0x00000003U, 0x000000ffU },
    { 121789892856168538LL, 0x0b0, 0x0000001fU, 0x000000ffU },
    { 121789964284739966LL, 0x0ac, 0x00000004U, 0x000000ffU },
    { 121790071427597108LL, 0x0ac, 0x00000004U, 0x000000ffU },
    { 121795499999025636LL, 0x004, 0x00000000U, 0xffffffffU },
    { 121795571427597064LL, 0x080, 0x01000000U, 0xff000000U },
    { 121797142856168480LL, 0x004, 0x00000000U, 0xffffffffU },
    { 121797214284739908LL, 0x080, 0x01000000U, 0xff000000U },
    { 121798785713311324LL, 0x004, 0x00000000U, 0xffffffffU },
    { 121798857141882752LL, 0x080, 0x01000000U, 0xff000000U },
    { 121800428570454168LL, 0x004, 0x00000000U, 0xffffffffU },
    { 121800499999025596LL, 0x080, 0x01000000U, 0xff000000U },
    { 121825107141882542LL, 0x080, 0x00010000U, 0x00ff0000U },
    { 121849999999025200LL, 0x080, 0x00010000U, 0x00ff0000U },
    { 121874892856167858LL, 0x080, 0x00010000U, 0x00ff0000U },
    { 121899785713310516LL, 0x080, 0x00010000U, 0x00ff0000U },
    { 121902249999024782LL, 0x400, 0xbe1a2800U, 0xffffffffU },
    { 121902428570453352LL, 0x500, 0x00010000U, 0x00ff0000U },
    { 121903607141881914LL, 0x404, 0xae720000U, 0xffffffffU },
    { 121903785713310484LL, 0x500, 0x00020000U, 0x00ff0000U },
    { 121904964284739046LL, 0x408, 0xae700000U, 0xffffffffU },
    { 121905142856167616LL, 0x500, 0x00030000U, 0x00ff0000U },
    { 121906321427596178LL, 0x40c, 0xbe507d00U, 0xffffffffU },
    { 121906499999024748LL, 0x500, 0x00040000U, 0x00ff0000U },
    { 122144571427594272LL, 0x410, 0xbe1a2801U, 0xffffffffU },
    { 122144749999022842LL, 0x500, 0x00050000U, 0x00ff0000U },
    { 122145928570451404LL, 0x414, 0xae720001U, 0xffffffffU },
    { 122146107141879974LL, 0x500, 0x00060000U, 0x00ff0000U },
    { 122147285713308536LL, 0x418, 0xae700001U, 0xffffffffU },
    { 122147464284737106LL, 0x500, 0x00070000U, 0x00ff0000U },
    { 122148642856165668LL, 0x41c, 0xbe507d01U, 0xffffffffU },
    { 122148821427594238LL, 0x500, 0x00080000U, 0x00ff0000U },
    { 122431357141877692LL, 0x004, 0x00000000U, 0xffffffffU },
    { 122431428570449120LL, 0x080, 0x01000000U, 0xff000000U },
    { 122809857141874664LL, 0x004, 0x00000000U, 0xffffffffU },
    { 122809928570446092LL, 0x080, 0x01000000U, 0xff000000U },
    { 140353714284591456LL, 0x004, 0x00000000U, 0xffffffffU },
    { 140353785713162884LL, 0x080, 0x01000000U, 0xff000000U },
    { 157897642855879676LL, 0x004, 0x00000000U, 0xffffffffU },
    { 157897714284451104LL, 0x080, 0x01000000U, 0xff000000U },
    { 175441535712882182LL, 0x004, 0x00000000U, 0xffffffffU },
    { 175441607141453610LL, 0x080, 0x01000000U, 0xff000000U },
    { 192985285712741832LL, 0x004, 0x00000000U, 0xffffffffU },
    { 192985357141313260LL, 0x080, 0x01000000U, 0xff000000U },
    { 210529249998315766LL, 0x004, 0x00000000U, 0xffffffffU },
    { 210529321426887194LL, 0x080, 0x01000000U, 0xff000000U },
    { 228072999998175416LL, 0x004, 0x00000000U, 0xffffffffU },
    { 228073071426746844LL, 0x080, 0x01000000U, 0xff000000U },
    { 245616892855177922LL, 0x004, 0x00000000U, 0xffffffffU },
    { 245616964283749350LL, 0x080, 0x01000000U, 0xff000000U },
    { 263160821426466142LL, 0x004, 0x00000000U, 0xffffffffU },
    { 263160892855037570LL, 0x080, 0x01000000U, 0xff000000U },
    { 280704678569182934LL, 0x004, 0x00000000U, 0xffffffffU },
    { 280704749997754362LL, 0x080, 0x01000000U, 0xff000000U },
    { 298248464283328298LL, 0x004, 0x00000000U, 0xffffffffU },
    { 298248535711899726LL, 0x080, 0x01000000U, 0xff000000U },
    { 315792321426045090LL, 0x004, 0x00000000U, 0xffffffffU },
    { 315792392854616518LL, 0x080, 0x01000000U, 0xff000000U },
    { 333336249997333310LL, 0x004, 0x00000000U, 0xffffffffU },
    { 333336321425904738LL, 0x080, 0x01000000U, 0xff000000U },
    { 350880142854335816LL, 0x004, 0x00000000U, 0xffffffffU },
    { 350880214282907244LL, 0x080, 0x01000000U, 0xff000000U },
    { 368423892854195466LL, 0x004, 0x00000000U, 0xffffffffU },
    { 368423964282766894LL, 0x080, 0x01000000U, 0xff000000U },
    { 385967785711197972LL, 0x004, 0x00000000U, 0xffffffffU },
    { 385967857139769400LL, 0x080, 0x01000000U, 0xff000000U },
    { 403511714282486192LL, 0x004, 0x00000000U, 0xffffffffU },
    { 403511785711057620LL, 0x080, 0x01000000U, 0xff000000U },
    { 421055464282345842LL, 0x004, 0x00000000U, 0xffffffffU },
    { 421055535710917270LL, 0x080, 0x01000000U, 0xff000000U },
    { 438599357139348348LL, 0x004, 0x00000000U, 0xffffffffU },
    { 438599428567919776LL, 0x080, 0x01000000U, 0xff000000U },
    { 456143249996350854LL, 0x004, 0x00000000U, 0xffffffffU },
    { 456143321424922282LL, 0x080, 0x01000000U, 0xff000000U },
    { 473687035710496218LL, 0x004, 0x00000000U, 0xffffffffU },
    { 473687107139067646LL, 0x080, 0x01000000U, 0xff000000U },
    { 491230892853213010LL, 0x004, 0x00000000U, 0xffffffffU },
    { 491230964281784438LL, 0x080, 0x01000000U, 0xff000000U },
    { 508774785710215516LL, 0x004, 0x00000000U, 0xffffffffU },
    { 508774857138786944LL, 0x080, 0x01000000U, 0xff000000U },
    { 526318678567218022LL, 0x004, 0x00000000U, 0xffffffffU },
    { 526318749995789450LL, 0x080, 0x01000000U, 0xff000000U },
    { 543862607138506242LL, 0x004, 0x00000000U, 0xffffffffU },
    { 543862678567077670LL, 0x080, 0x01000000U, 0xff000000U },
    { 561406392852651606LL, 0x004, 0x00000000U, 0xffffffffU },
    { 561406464281223034LL, 0x080, 0x01000000U, 0xff000000U },
    { 578950321423939826LL, 0x004, 0x00000000U, 0xffffffffU },
    { 578950392852511254LL, 0x080, 0x01000000U, 0xff000000U },
    { 596494071423799476LL, 0x004, 0x00000000U, 0xffffffffU },
    { 596494142852370904LL, 0x080, 0x01000000U, 0xff000000U },
    { 614037964280801982LL, 0x004, 0x00000000U, 0xffffffffU },
    { 614038035709373410LL, 0x080, 0x01000000U, 0xff000000U },
    { 631581857137804488LL, 0x004, 0x00000000U, 0xffffffffU },
    { 631581928566375916LL, 0x080, 0x01000000U, 0xff000000U },
    { 649125642851949852LL, 0x004, 0x00000000U, 0xffffffffU },
    { 649125714280521280LL, 0x080, 0x01000000U, 0xff000000U },
    { 666669499994666644LL, 0x004, 0x00000000U, 0xffffffffU },
    { 666669571423238072LL, 0x080, 0x01000000U, 0xff000000U },
    { 684213428565954864LL, 0x004, 0x00000000U, 0xffffffffU },
    { 684213499994526292LL, 0x080, 0x01000000U, 0xff000000U },
    { 701757249994385942LL, 0x004, 0x00000000U, 0xffffffffU },
    { 701757321422957370LL, 0x080, 0x01000000U, 0xff000000U },
    { 719301178565674162LL, 0x004, 0x00000000U, 0xffffffffU },
    { 719301249994245590LL, 0x080, 0x01000000U, 0xff000000U },
    { 720382392851379798LL, 0x08c, 0x53000000U, 0xff000000U },
    { 720382428565665512LL, 0x08c, 0x00450000U, 0x00ff0000U },
    { 720382535708522654LL, 0x08c, 0x00004700U, 0x0000ff00U },
    { 720382642851379796LL, 0x08c, 0x00000041U, 0x000000ffU },
    { 720382857137094080LL, 0x048, 0x01000000U, 0xff000000U },
    { 720384035708522642LL, 0x084, 0x00000000U, 0x00ff0000U },
    { 720384071422808356LL, 0x084, 0x00000000U, 0xff000000U },
    { 720384178565665498LL, 0x080, 0x00000001U, 0x000000ffU },
    { 736844964279819526LL, 0x004, 0x00000000U, 0xffffffffU },
    { 736845035708390954LL, 0x080, 0x01000000U, 0xff000000U },
    { 754388892851107746LL, 0x004, 0x00000000U, 0xffffffffU },
    { 754388964279679174LL, 0x080, 0x01000000U, 0xff000000U },
    { 771932714279538824LL, 0x004, 0x00000000U, 0xffffffffU },
    { 771932785708110252LL, 0x080, 0x01000000U, 0xff000000U },
    { 789476642850827044LL, 0x004, 0x00000000U, 0xffffffffU },
    { 789476714279398472LL, 0x080, 0x01000000U, 0xff000000U },
    { 807020464279258122LL, 0x004, 0x00000000U, 0xffffffffU },
    { 807020535707829550LL, 0x080, 0x01000000U, 0xff000000U },
    { 824564249993403486LL, 0x004, 0x00000000U, 0xffffffffU },
    { 824564321421974914LL, 0x080, 0x01000000U, 0xff000000U },
    { 842108214278977420LL, 0x004, 0x00000000U, 0xffffffffU },
    { 842108285707548848LL, 0x080, 0x01000000U, 0xff000000U },
};

// The stop sequence Cool Riders' sound test sends before each song (side 0)
static const uint32_t STOP_SEQ[4] = { 0xbe1a2800, 0xae720000, 0xae700000, 0xbe507d00 };

// Measured with tools/crtest: a command written at 1.1 s after power-on starts the song at
// about 1.2 s; 1.25 s leaves a margin. The fade after the stop sequence is down to the board's
// idle level about 1.5 s after it.
static const double CMD_TIME = 1.25, FADE_TIME = 1.5;   // seconds
static const int    BOOT_SPEED = 3;           // board frames per output frame while booting

// Level: MAME's mix (+-1.0 full scale) to 16-bit, times 5.7: the cover's average level then
// matches OutRun's own three tracks as the program plays them (RMS 2,494 on average, 16-bit
// scale, measured at the radio screen: Magical Sound Shower 2,843, Passing Breeze 2,162, Splash
// Wave 2,478; the cover measured 872 at x 2, so x 5.7). A design decision: there is no arcade
// reference for levels between different games' boards.
static const float GAIN = 32767.0f * 5.7f;

CoolRidersMusic::CoolRidersMusic() {}
CoolRidersMusic::~CoolRidersMusic() {}

bool CoolRidersMusic::init(int rate)
{
    ready = false;
    if (!cr_roms.loaded()) return false;
    out_rate = rate;
    board.reset(new CRSound());
    if (!board->init(cr_roms["maincpu"].data, cr_roms["gfx_data"].data, cr_roms["sub"].data)) { board.reset(); return false; }
    state = OFF;
    ready = true;
    return true;
}

void CoolRidersMusic::send(const uint32_t* cmds, int n)
{
    // as the main program does: the entry, then the write index (byte $501), 2 us apart
    CrTime t = board->now();
    const CrTime gap(0, 2000000000000LL);
    for (int i = 0; i < n; i++)
    {
        board->main_write(t, 0x400 + 4u * ring, cmds[i], 0xffffffffu); t = t + gap;
        ring = (uint8_t)((ring + 1) & 31);
        board->main_write(t, 0x500, (uint32_t) ring << 16, 0x00ff0000u); t = t + gap;
    }
}

void CoolRidersMusic::send_play(uint8_t song)
{
    uint32_t c[5] = { STOP_SEQ[0], STOP_SEQ[1], STOP_SEQ[2], STOP_SEQ[3], 0xae100000u | (uint32_t) song << 8 };
    send(c, 5);
}

void CoolRidersMusic::send_stop()
{
    send(STOP_SEQ, 4);
}

void CoolRidersMusic::play(uint8_t song)
{
    if (!ready) return;
    if (getenv("CB_XAUDIO_DEBUG")) fprintf(stderr, "crmusic: play %02x (state %d)\n", song, (int) state);
    switch (state)
    {
        case OFF:
            board->power_on(nullptr);
            for (const auto& w : BOOT_WRITES) board->main_write(CrTime(0, w.atto), w.off, w.data, w.mask);
            next_time = CrTime(); ring = 8; out.clear();
            pending_song = song;
            state = BOOTING;
            break;
        case BOOTING:
            pending_song = song;
            break;
        case RUNNING: case STOPPING: case PAUSED:
            send_play(song);
            state = RUNNING;
            break;
    }
}

void CoolRidersMusic::stop()
{
    if (!ready) return;
    switch (state)
    {
        case BOOTING:
            pending_song = -1;              // the boot goes on; the board is paused after it
            break;
        case RUNNING:
            if (getenv("CB_XAUDIO_DEBUG")) fprintf(stderr, "crmusic: stop\n");
            send_stop();
            state = STOPPING;
            stop_until = board->now() + CrTime(0, (int64_t)(FADE_TIME * 1e18));
            break;
        default:
            break;
    }
}

// One frame of the board: 1/fps s on from where it stopped (the frame rate can change between
// frames: the menu's FRAME RATE setting)
void CoolRidersMusic::run_frame(int fps, bool output)
{
    next_time = next_time + CrTime(0, CrTime::ATTO / fps);
    board->run_until(next_time);
    if (output)
        for (size_t i = 0; i < board->out_l.size(); i++)
            out.push(board->out_l[i] * GAIN, board->out_r[i] * GAIN);
    board->out_l.clear(); board->out_r.clear();
}

void CoolRidersMusic::mix(int32_t* mix, int samples, int fps)
{
    if (!ready) return;
    if (fps <= 0) fps = 60;
    if (state == BOOTING)
    {
        // muted (the board is silent until a song starts), at BOOT_SPEED x
        for (int k = 0; k < BOOT_SPEED && state == BOOTING; k++)
        {
            run_frame(fps, false);
            if (board->now().sec() >= CMD_TIME)
            {
                if (pending_song >= 0) { send_play((uint8_t) pending_song); state = RUNNING; }
                else state = PAUSED;
                pending_song = -1;
            }
        }
        return;
    }
    if (state == RUNNING || state == STOPPING)
    {
        run_frame(fps, true);
        if (state == STOPPING && stop_until <= board->now()) state = PAUSED;   // the fade is over
    }
    // board rate (44099 Hz) -> output rate; what is left after a stop drains at the same rate
    if (out.available())
        out.pull((double) board->sample_rate() / out_rate, mix, samples);
}
