# OutRunners in the crossover

OutRunners (Sega, 1992, System Multi 32) has covers of OutRun's music. Four of them are in the
music selection, played by OutRunners' own sound program on an emulated copy of the board's sound
side:

| Title in the program | OutRunners sound test (monitor A) |
|---|---|
| OUTRUNNERS MAGICAL SOUND SHOWER | `$01` |
| OUTRUNNERS PASSING BREEZE | `$02` |
| OUTRUNNERS SPLASH WAVE | `$03` |
| OUTRUNNERS LAST WAVE | `$12` |

These are the numbers its in-race radio uses for those songs (MAME 0.264, the radio cycled through
in a race). Only OutRunners' sound is used; its courses and cars are not in the program (section 5).

Sets: MAME `orunners`, `orunnersu`, `orunnersj`. Reference: MAME 0.264. Only the three sound ROMs are
needed (`roms/orunners/roms.txt`); they are the same in all three sets.

## 1. The board (`crossover/orsound.*`)

The sound side of System Multi 32, as MAME 0.264's `segas32.cpp` (BSD-3-Clause, Aaron Giles)
describes it:

- a Z80 at 8,053,975 Hz (32,215,900 / 4), interrupt mode 1:
  - ROM at `$0000-$9FFF`;
  - an 8 KB window at `$A000-$BFFF` into the 512 KB program ROM `epr-15550`, selected by port `$A0`
    (bank = data & `$3F`);
  - the MultiPCM at `$C000-$DFFF`;
  - RAM shared with the main V70 at `$E000-$FFFF` (main `$700000-$701FFF`);
- a YM3438 at the same clock (ports `$80-$83`);
- the sound interrupt controller: ports `$C0-$CF` acknowledge (an odd port clears the sources whose
  bits are zero), ports `$D0-$D7` hold the vector control and the mask; the YM3438 is source 0, the
  main CPU source 1;
- a MultiPCM (Sega 315-5560, Yamaha GEW8) at 10 MHz, so 44,642 samples per second, with 4 MB of
  samples (`mpr-15551`, `mpr-15552`): the first 1 MB direct, then two 512 KB windows selected by
  port `$B0` (high = (data >> 3) & 7, low = data & 7).

Cores: Z80 `crossover/z80` (MIT), YM3438 ymfm (BSD-3-Clause), MultiPCM `crossover/multipcm`,
transcribed from MAME 0.264's `gew.cpp` and `multipcm.cpp` (BSD-3-Clause, Miguel Angel Horna).

Timing follows MAME 0.264:
- time is counted in Z80 cycles; the YM3438's timers run on the same clock;
- MAME's Z80 makes each memory or port access at its own machine cycle, so a chip write is timed
  at the cycle where MAME makes it: `OUT (n),A` at 7 cycles into the instruction, `LD (nn),A` at
  10 (the only two instructions the sound program uses to write the chips);
- interrupts are taken between instructions, after any timer that expired during the instruction,
  and the 13 cycles of the acknowledge are counted;
- the YM3438's mode writes and interrupt checks made while the Z80 runs go through MAME's
  `synchronize`: they are queued and done in time order with the timer expiries;
- the main CPU's writes become visible to the Z80 at the start of the Z80's next timeslice, as in
  MAME, where the V70 runs first in each slice. Slices end at 1/60 s and at the YM3438's timer
  expiries;
- the two chips' outputs are generated as MAME's streams are: up to the sample index of the
  current time, rounded up, at their integer rates (55,930 and 44,642 Hz).

MAME's mix: the YM3438's output 1 (× 0.40) and the MultiPCM's output 1 (× 1.0) go to the left
speaker, the outputs 0 to the right. In a race and in sound test monitor A, the game puts player
1's sound on the left only, so the program plays the left speaker on both channels.

## 2. Standing in for the main CPU (`crossover/ormusic.*`)

The main program talks to the sound program only through the shared RAM, which the Z80 polls:

- **Commands:** a byte at main `$700700` for monitor A (at `$700800` for monitor B). The sound test
  sends `$1F` (stop) to A, `$1F` to B, then the song number to A, one frame apart. The program
  does the same.
- **Boot:** the main CPU releases the Z80 from reset 0.0888 s after power-on; commands are taken
  from 0.05 s after the release. The program powers the board on when the first song is chosen,
  runs it to 0.05 s after the release, then sends the commands.
- **Stop:** `$1F` alone fades the song out in about 0.5 s. The program sends it to A and B and
  pauses the board (its time stops) one second later.
- **Level:** MAME's ±1.0 scale is converted to 16 bits and multiplied by 0.26. Measured in the
  program at the radio screen (RMS, 16-bit, 9.5-24.5 s), OutRun's own three tracks average 2,494
  (Magical Sound Shower 2,843, Passing Breeze 2,162, Splash Wave 2,478); the three covers then
  measure 2,131, 2,542 and 2,815 (Last Wave 2,064). This is a design decision; absolute levels
  between different boards have no arcade reference. MAME's MultiPCM output is clamped at ±1.0 and
  the songs reach it, so the covers' peaks are at the clamp (8,519 after the gain).
- **Resampling:** from each chip's rate to the output rate, by linear interpolation, as for the
  other boards (`crossover/resample.hpp`).

## 3. Checked against MAME 0.264

**Reference recordings:** MAME's sound test, song No. 1 (Magical Sound Shower, 258 s), and a race
with the radio cycled through its songs, with all of the main CPU's shared-RAM writes and the Z80's
port and MultiPCM writes recorded (MAME through a Lua script; a fresh EEPROM for each run).
`tools/ortest` replays the main CPU's writes into the board and writes its chip writes and its
output.

- **Z80:** traced instruction by instruction in both from the release of reset, the first 136,714
  instructions start at the same cycle and go the same way.
- **The song, replaying every main-CPU write:** all 6,989 MultiPCM key-ons of the 258 s match
  MAME's, each at the same time to the microsecond.
- **The song, replaying only the three sound-test commands** (as the program does): the same 6,989
  key-ons, each within 1.6 ms of MAME's (the first 59 s: 1,659 key-ons within 0.86 ms).
- **Output:** against MAME's WAV, waveform correlation 0.997-0.999 and RMS within 0.02 %, with a
  constant lag of 12 samples (the conversion to the output rate).
- **Known difference:** with every main-CPU write replayed, the full stream of chip writes differs
  from MAME's only around the moments a command becomes visible (10.0 s, entering the test mode;
  12.8 s in the race run): MAME's timeslices also end at the main V70's timers, which this board
  does not have.
- **In the program:**
  - preview in the race setup menu, the radio screen, a race on Super Hang-On's Africa and a race on
    Hang-On's course with Hang-On's bike all play the chosen song;
  - at 30 fps the song's envelope matched the 60 fps run (correlation 0.944 over 14 s at 10 ms) at
    the same level (RMS 2,109 in both);
  - the Windows build under Wine gave output identical to the Linux build's, sample for sample.
- **CPU:** about 2 % of one core of the 2.7 GHz Xeon used for the tests (`ortest` runs the 258 s
  song in 5.2 s).

## 4. Tools

- `tools/ortest` (`build.sh <dir>`): `ortest <romdir> <share log> <release s> <seconds> <prefix>`
  runs the board from a log of main-CPU writes and writes `<prefix>.wav` (44,100 Hz, MAME's left
  speaker on both channels) and `<prefix>.chips` (every port and MultiPCM write).
  - `ORT_TRACE=<from>,<to>` (seconds) traces the Z80 into `<prefix>.tr`.
  - `ORT_OPLOG` logs the opcode of each chip access.
  - `ORT_BOTH` puts MAME's right speaker on the right channel.

## 5. OutRunners' courses and cars

Investigated, not done. What was established (MAME 0.264):

- **The hardware:** a V70 at 20 MHz (40 MHz / 2) runs the game. There is no road chip: Multi 32 has
  System 32's video (four tilemap layers NBG0-NBG3 with zoom, row scroll and row select, a text
  layer, a bitmap layer and zoomed sprites) driving two screens, one per player.
- **How a race is drawn:** turning the layers off one at a time during a race (writing the layer
  disable bits of the video register `$31FF02` every frame):
  - the road is tilemap layer NBG3, drawn with per-line row scroll and row select (the video
    register `$31FF04` reads `$E00F` while driving: both enabled for NBG2 and NBG3);
  - the sky is NBG0; with NBG3 off, a water layer shows beneath it;
  - everything else (the cars, roadside objects, the start banner and crowds, and the HUD) is
    sprites: with all four tilemap layers and the bitmap off, they remain, and turning the text
    layer off left player 1's screen pixel-identical.

  So OutRunners' road gets its curves and hills from row scroll and row select tables that only the
  game's software can write, not from a road chip as in OutRun, Super Hang-On and Hang-On. The
  course data is in the V70 program's ROMs, in a format not yet known.
- **Running OutRunners' courses with the other vehicles** would follow the route taken for Super
  Hang-On and Hang-On: an emulated board running the game, with the vehicle put on its road. That
  needs:
  - the V70 (MAME's V60/V70 core: BSD-3-Clause, about 12,400 lines with its disassembler);
  - System 32's video and mixer for two screens (MAME's `segas32_v.cpp`: BSD-3-Clause, about
    2,600 lines; its own notes list open questions, mostly in other games);
  - the two 315-5296 I/O chips, the interrupt controller, the EEPROM and the link to this sound
    board;
  - then the reverse engineering of the V70 program: the player car's position, speed and
    collision state, and where to put another vehicle in its sprite list, before any vehicle
    could be swapped in. There is no disassembly or documentation of the game's code.
  - Estimate: months of hobby time; the emulation is mostly transcription, the game's code is the
    unknown.
- **OutRunners' cars on the other courses** would need the cars' handling (in the V70 program,
  unknown) and their sprite frames, which are in 16 MB of sprite ROM in System 32's sprite format
  (documented by MAME's BSD driver, so decoding is plausible). Smaller in code, but the handling has
  to be found in a program with no prior documentation.
- **Converting OutRunners' courses into OutRun's road format** (the route considered for Cool Riders' courses) would
  first need the course data found and decoded in the V70 program's ROMs; it was not looked for.
