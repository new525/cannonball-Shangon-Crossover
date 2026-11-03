/***************************************************************************
    OutRunners' radio: the in-race radio object of OutRunners' main program (MAME set orunnersj),
    transcribed from the V70 code (docs/OUTRUNNERS-RE.md section 14) and proven identical to it
    on the emulated board, tick by tick (tools/orradiotest).

    What is transcribed (player 1, one tick = 1/30 s, the order of OutRunners' tick):
      - the radio object: its init ($191E0), the wait for the start countdown ($1923F) and its tick
        ($1924C): the song (<< / >> / START, the wrap-around, << and >> together for Jingle Bell),
        DJ/MUSIC (Mega Radio Station: the station of the route node, the voices of the DJ: the
        introduction, the station's name, the remarks chosen at random), a button in the DJ's
        mode back to the song;
      - the title objects it creates ($1958E): "` Splash Wave" sliding in from the right,
        held 2 s, sliding out to the left; the replacement of a title by the next one;
      - the voice clock of $18C45 ($208F30, $208F34): a voice ends when its lip-sync script does
        (one byte every 3 ticks), and the DJ waits for it before the next;
      - the sound command queue ($12124: $208800, 32 entries) and its drain, one command a tick to
        the sound board's $700700 ($3F8DA, in the frame interrupt at the end of the tick);
      - the random numbers ($120B1, state $208F00).
    Not transcribed: the cabinet's LCD (F10-F1F, F40-F48: the titles and the DJ's mouth on the
    radio panel's display), the lamps.

    The model reads no memory: what the original reads from the rest of the game is passed in
    (In, Race); the ROM's tables are transcribed below with their addresses.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

namespace orradio
{

// $19698: the songs (index $209014 0-9) and, index 10, Jingle Bell: the sound command (the
// entry's top byte) and the title record (16-bit x offset, then the text; '`' is the note)
struct Song { uint8_t cmd; uint32_t rec; int16_t x; const char* text; };
extern const Song SONGS[11];
extern const Song MEGA_RADIO;                // $197B8: "Mega Radio Station" (no command)
const int SONG_COUNT = 10;                   // $19339: cmp.b #A
const int JINGLE_BELL = 10;
extern const uint8_t START_SONG[8];          // $1968D: the song a race starts with, by car
extern const uint8_t NODE_STATION[31];       // $196C4: Mega Radio Station's command, by route node
// $197E2: the DJ's voices: the command and the lip-sync script ($208F30 walks it; its length
// is the number of bytes before the negative one that ends it)
struct Voice { uint8_t cmd; uint32_t script; uint8_t len; };
extern const Voice VOICES[0x31];

// The inputs of one tick (each read where the original reads it)
struct In
{
    uint16_t edges = 0;      // $208118: new presses this tick: bit 6 START, 8 DJ/MUSIC, 9 <<, 10 >>
    bool     over = false;   // $209181 | $209182: the goal, GAME OVER
    uint8_t  node = 0;       // $2080A1: the route node being driven
    uint8_t  stage = 0;      // $2080A4: the stage number (0-4)
    bool     countdown = false;  // 10[$200400] != 0: the start countdown is running
    // $208F0F: set by the branch's slow-down object ($1488A) before the radio's tick, else
    // unchanged (the model keeps it: cleared by the radio when the node changes)
    bool     f0f_set = false;
};

// The race the radio introduces (read by its init and its remarks)
struct Race
{
    uint8_t car = 0;                 // $209060: the player's car (MAD POWER = 0): the first song
    uint8_t entries = 1;             // $20FC1F (1F[$208308]): the cars introduced
    uint8_t order[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };  // 80[R20]: car by place
    uint8_t own_place = 0;           // 9[R20] = $20E030[car number]
    uint8_t place_of_first = 0;      // $20E030[$20FC20]: remark $1950A
};

// A title object ($1958E)
struct Title
{
    bool     active = true;          // [obj] bit 0
    uint8_t  c = 0;                  // C[obj]: 0 start, 1 in, 2 held, 3 out, 4 out fast
    uint8_t  d = 0;                  // D[obj]: frames drawn (the colour: $197CD[(d >> 1) & 7])
    uint16_t timer = 0x3C;           // 10[obj]: ticks held
    int16_t  speed = 0;              // 12[obj]
    int16_t  x = 0;                  // 20[obj] (low half; the high half is y = $C8)
    int16_t  xoff = 0;               // 30[obj]: the record's x offset
    const Song* song = nullptr;      // 18[obj]: the record
    bool     drawn = false;          // drawn this tick, at screen x = x + xoff, y = 200
    int      id = 0;                 // the model's name for the object
    int16_t  screen_x() const { return (int16_t)(x + xoff); }
    static const int Y = 0xC8;       // $195C2: 200
};

class Radio
{
public:
    // $191E0: the object is created as the race starts (the start countdown then holds it at $1923F)
    void init(const Race& race, uint8_t node);
    // One tick of the radio object, then its title objects
    void tick(const In& in);
    // Then, after the rest of the tick's objects, $18C45's voice clock (its $208F30 part)
    void voice_clock();
    // $3F8DA: the frame interrupt at the end of the tick sends one queued command; -1 if none
    int drain();
    // Commands queued by others ($12124 from the rest of the game; tools/orradiotest)
    void queue_external(uint8_t c) { queue(c); queued.pop_back(); q_src[(q_write - 1) & 0x1F] = 1; }
    bool drained_external = false;   // the command drain() returned was queued by queue_external

    // ---- outputs ----
    std::vector<uint8_t> queued;     // the commands queued this tick ($12124), in order
    std::vector<uint8_t> volume;     // the values written to $700918 this tick ($12256)
    std::vector<Title> titles;       // the title objects, in the object list's order

    // ---- state (player 1's addresses) ----
    enum Routine { R_INIT, R_WAIT, R_RUN, R_OVER };   // 4[R20]: $191E0, $1923F, $1924C, $19276
    Routine routine = R_INIT;
    uint8_t  c = 0, d = 0, b = 0;    // C, D, B[R20]: the radio's state, the voices' state, a voice to say
    uint8_t  a = 0;                  // A[R20]: the node the radio knows
    uint8_t  timer = 0;              // 10[R20]
    uint16_t edges = 0;              // 2C[R20]
    uint32_t seq = 0;                // 38[R20]: a run of remarks in progress ($198D8's lists; their ROM address)
    uint8_t  order[8] = {};          // 80[R20]
    uint8_t  left = 0, at = 0;       // 88, 89[R20]: the introduction's cars left, its place
    uint8_t  own_place = 0;          // 9[R20]
    uint8_t  place_of_first = 0;
    int      title = -1;             // 18[R20]: the latest title object (index in titles), -1 none
    uint8_t  song = 0;               // $209014
    bool     f0f = false;            // $208F0F
    uint32_t f30 = 0;                // $208F30: the voice's script position (0: none)
    uint8_t  f34 = 3;                // $208F34: ticks to the script's next byte
    bool     f35 = true;             // $208F35 ($FF in a race): the voice clock runs
    uint32_t rng = 0;                // $208F00
    uint8_t  q[32] = {};             // $208802: the queue, read index $208800, write index $208801
    uint8_t  q_read = 0, q_write = 0;
    uint8_t  q_src[32] = {};         // (the model's) 1: queued by queue_external

    uint16_t rand();                 // $120B1
    // tools/orradiotest: $208F30 set by the rest of the game (its script's end, $208F34)
    void set_voice(uint32_t script_pos, uint32_t script_end, uint8_t clock);
    uint32_t voice_end() const { return f30_end; }

private:
    Race     race_;
    uint8_t  node_ = 0;
    int      title_ids = 0;
    uint32_t f30_end = 0;            // the end of the voice's script (its terminating byte)
    void queue(uint8_t c);           // $1214A
    void say(int voice);             // $1966E
    void new_title(const Song* s);   // $19561 / $19576
    uint8_t song_title(int index);   // $19551: the song's title; its command
    void run(const In& in);          // $19276
    void tail(const In& in);         // $19414
    uint8_t r_order(uint8_t i) const;
    void remark(uint32_t routine);   // $1950A-$19548
    void title_tick(Title& t);
};

} // namespace orradio
