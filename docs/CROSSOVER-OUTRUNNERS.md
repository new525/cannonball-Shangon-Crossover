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
in a race).

OutRunners' courses are in the program too, on an emulated copy of the whole board (section 6):
**OUTRUNNERS FULL COURSE** (the game's own map, your choice at each checkpoint) and **OUTRUNNERS
KENYA** (its Kenya stage alone), with OutRunners' car **MAD POWER** or OutRun's Ferrari (section 7).

Sets: MAME `orunners`, `orunnersu`, `orunnersj`. Reference: MAME 0.264. The music needs only the three
sound ROMs, which are the same in all three sets; the courses need every file of `orunnersj`
(`roms/orunners/roms.txt`).

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

## 5. OutRunners' courses and cars: the first investigation

This was the first look, before the board in section 6 was built. What was established (MAME 0.264):

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

## 6. OutRunners' courses on the emulated board (`crossover/orboard.*`, `crossover/ormode.*`)

**The board.** The whole System Multi 32, as MAME 0.264's `segas32.cpp` (sega_multi32_analog)
describes it:
- the V70 (`crossover/v60`, MAME's V60/V70 interpreter and disassembler: Farfetch'd, R. Belmont);
- System 32 video for both screens (`crossover/s32video`, from `segas32_v.cpp`, Aaron Giles);
- the two 315-5296 I/O chips, the MSM6253 ADC, the 93C46 EEPROM and the comm board
  (`crossover/s32io`);
- the interrupt controller and its timers, and the sound board of section 1;
- MAME's scheduler for this machine (`crossover/orboard.cpp` lists each rule with the MAME code it
  comes from).

**Checked against MAME 0.264** (`tools/v60test`, `tools/s32vtest`, `tools/orbtest`):
- The V70: a 1.25-million-instruction trace from power-on is identical (PC, cycles, disassembly,
  every I/O access, 432 interrupts).
- The video: 214 screens replayed from MAME's own video writes are pixel-identical.
- The board, over two scripted sessions (8,000 and 7,000 frames: coin, START, car select, races
  with steering, shifts and brake, a checkpoint, player 2, test mode):
  - work RAM and shared RAM are identical at every frame;
  - every I/O access is identical to the attosecond (3.6 million);
  - all 30 snapshots are pixel-identical, and the EEPROM ends byte-identical;
  - the sound correlates 0.998 with MAME's WAV, at the same 12-sample lag as section 3.
- Speed: 5.2× real time drawing both screens, 11× drawing none.

**The game's code.** `docs/OUTRUNNERS-RE.md` documents the player car, road, course loading,
collisions, sprites and car models of `orunnersj`, each address confirmed by an experiment on the
board (`tools/ortrace`, `run_all.sh` reruns them).

**The mode** (`crossover/ormode`), with OutRunners' own car:
- The board is powered on and run unseen and unheard through a coin, START, the car select and the
  transmission screen, as fast as the game allows, following its own state rather than fixed frames
  (`docs/OUTRUNNERS-RE.md`, "Starting a race quickly"):
  - a coin during the boot, START as soon as the boot is over (`$208080` = 2, frame 679);
  - at the car select, the wheel turned left until its cursor (`$209060`) is on MAD POWER (car 0),
    then the accelerator to confirm; on the transmission screen, the wheel held right for the
    manual box (the program's separate-gear-buttons setting), and the accelerator;
  - for OUTRUNNERS KENYA, the route node and stage bytes `$2080A0-$2080A6` written before the race
    setup, so the race starts on the Kenya stage (course 41);
  - the entry countdown (`$20FB1A`, `$20FC1A`), which waits for a second player, shortened to 2;
  - the player takes over at the start line (substate `$208082` = 6), at frame 1389 of the board
    (2581 when the screens time out). On the test machine, START RACE to the start line takes
    about 1.9 s, the first ROM loading included.
- The player is player 1: the left screen and the left speaker. Only the left screen is drawn, and
  only the frames shown (at 30 fps, every second one); drawing changes nothing the game sees. The race ends when the game leaves
  its game state (`$208080` no longer 6); the program then returns to its menu.
- Controls: OutRun's steering range onto the wheel's full travel, the pedals onto 0-255; GEAR1/GEAR2
  shift down/up with a manual box.
- The EEPROM (settings, "OutRunner of OutRunners" records) is kept in `orunners_eeprom.bin` in the
  save folder. The run-up works from a fresh and from a used EEPROM.
- Level: × 0.17 of MAME's scale, which brings a race (music and the car's sounds) to OutRun's own
  races in the program. A design decision.
- Checked in the program: Kenya and the full course start with MAD POWER, automatic and manual
  (three upshifts reach 4th of 6), a race reaches 324 km/h, the timer runs out to GAME OVER and the
  program returns to its menu.
- **Kenya's goal.** Started on Kenya directly, the game counted Kenya's goal as the race's first
  checkpoint marker: the finish time was never written and the goal run waited for it for ever (the car
  kept driving at the goal run's speed with the timer at 0). The mode now writes, on the frame the car
  reaches the goal (`$209181` = `$FF`), the checkpoint bookkeeping normal play has on reaching Kenya
  (by the West and the lower branches; `tools/ortrace`, e15 at frame 16420 against the start):
  `$208330` = 5, `$208338-$20833C` = 1, `$208342` = 5, `$208344-$208348` = 2, 6, 12, 20, 30 (the
  nodes), `$208349` = 5, `$20834A-$20834E` = 1, `$20835A-$20835E` = 1, `$208480` = 5, and in the
  player's record `$20FB60` = 5, `$20FB61` = 5, `$20FB63` = 5, `$20FB65` = 4, `$20FB67` = 1. The goal
  is then the sixth marker: the finish time is written (`$DFD1` to `$208410`), the goal run, map,
  CONGRATULATIONS and name entry follow and the game returns to its attract mode (checked on the board
  with `tools/ortrace` and MAD POWER, and in the program with the Ferrari). Written at the start instead, the same
  bytes make the game show EXTEND TIME; written at the goal, a MAD POWER race up to the goal is
  unchanged (section 7.8). The earlier checkpoints' times stay 0 on the results.

**Not done yet:**
- The bikes on OutRunners' courses, and MAD POWER on the other games' courses.
- The menu's MUSIC choice: OutRunners' courses play OutRunners' own radio.

## 7. OutRun's Ferrari on OutRunners' courses (`crossover/orferrari.*`)

VEHICLE: OUTRUN FERRARI with TRACK: OUTRUNNERS FULL COURSE or OUTRUNNERS KENYA. The design of the
Ferrari on Hang-On's and Super Hang-On's courses (`docs/CROSSOVER-HANGON.md` section 3,
`crossover/shferrari`): OutRunners runs unmodified on the board and keeps the world (road, camera,
scenery, wildlife, checkpoints and the West/East and branch choices, timer, goal, game over); the
vehicle is Cannonball's own OutRun code (`OFerrari`, `OCrash`, `OSmoke`, `OSprites` and OutRun's sound
board) at OutRun's 30 Hz car rate, fed from OutRunners' road and writing its speed and lateral
position where OutRunners' car code takes its own. Player 1, the left screen. The game's code the
hooks rely on is in `docs/OUTRUNNERS-RE.md`; what was found for this is its section 11.

### 7.1 The run-up and the car OutRunners keeps

The run-up is section 6's with MAD POWER and its automatic box (design decision): MAD POWER's
half-width (44, parameter `+$1A` and its collision box at `$12928`) is the Ferrari's at the lateral
scale below, so OutRunners' own collision tests and off-road flag use the Ferrari's width; its HUD
gearbox shows AUTO (OutRunners' HUD has no LOW/HIGH). OutRunners' car object keeps running; in a race
(car state `$20040C` 1-4) its inputs are the wheel centred and no accelerator, and the player's brake,
so that OutRunners' camera dips when the car brakes (`$7B74` tests the brake value `$2004B0`).

### 7.2 Units, with their sources

| Quantity | OutRun | OutRunners | Conversion |
|---|---|---|---|
| Speed | `car_increment`, 16.16 km/h | `$209074`: the advance per 30 Hz tick in course units, a segment `$8000` long on the centre line; the HUD shows speed / 136 (`$C2E9 divu.h #88`; e02, e03) | km/h x 136: OutRunners' speedometer shows OutRun's km/h, and the Ferrari covers OutRunners' course as an OutRunners car at that km/h would |
| Lateral | `car_x_pos`, + = left; the Ferrari's half-width 24 (`OFerrari::check_wheels`: one wheel off 24 inside the road's edge, both 24 outside) | `$209038`, world units, + = right (e05); MAD POWER's half-width 44 | x (-11/6): the cars' half-widths 24 and 44 |
| Road edge | single road of `road_width` w <= `$FF`: wheels off from w + `$D4` to w + `$104`, edge w + `$EC` | the segment's half-width `+$12` (264 to 440 on the full course) | OutRun's w = half-width x 6/11 - 236 (440 -> 4: OutRunners' standard road is OutRun's narrowest, edge 236). Its narrower roads (264-412) would need w < 0, which OutRun's code does not take (its narrowest road); for them `ORFerrari::check_wheels` makes OutRun's comparison with the edge where it is |
| Limit | `set_ferrari_bounds`: w + `$1F4` | half-width + `$60` (`$A77C-$A79A`) | OutRunners' (the tighter), converted |
| Distance per km/h | `CAR_BASE_INC` 303 per tick in road position (16.16; the integer part indexes the path) | 136 per tick in `$8000` per segment | one OutRun path position = 272/303 = 0.898 of a segment at the same km/h |
| Curve | `road_type`, `road_curve`; path: one unit vector (4096) per position | segment heading (12-bit, `+0`), turning right as it grows (`$104F6`: the forward vector is (sin h, cos h) with x to the right) | see 7.3 |

The lateral scale and the distance scale together keep OutRun's handling law in car widths: OutRun's
curve push (`OFerrari::set_curve_adjust`) moves the car sideways by about 4.7 x curvature x km/h
OutRun units a tick, i.e. a drift angle of the curvature times a look-ahead of 21 car widths; with
these scales it is 21 car widths in OutRunners too (OutRunners' world is 1.57 times OutRun's in car
widths per km/h: both its speed and its curvature per distance scale by that).

### 7.3 The road for OutRun's car code (`ORFerrari::build_road`)

- **Curve push:** a path in OutRun's format (`sho_road_build_outrun`'s reader): for OutRun position p =
  0-71 ahead of the car, the unit vector (4096 sin h, 4096 cos h) of OutRunners' heading at
  p x 272/303 segments ahead of the car's place in its segment (`$209034`), interpolated along each
  segment as OutRunners interpolates it at the car (`$A842-$A893`), from the segment ring (`$20A000`,
  the car's record at `$209040`; segments generated ahead: `$2090AA` - `$209042`). OutRun's road
  builder (`ORoad::setup_x_data`, verified as `sho_road_build_outrun`) is run over it every tick, and
  `OFerrari::set_curve_adjust` reads its slices 170 and 511 as on OutRun's roads. The sign was checked
  against OutRun's own data: on its first stage `ROAD_LEFT` comes with the path's angle (atan2(x, y))
  falling and `road_x[170] - road_x[511]` about -270, `ROAD_RIGHT` with it rising and +270.
- **Curve type and radius** (`OFerrari::set_ferrari_x` reduces steering into sharp curves, `OCrash`
  slides with the curve, the tyre squeal): from the heading change of the car's segment. In OutRun's
  own data the radius is the path's radius in positions (stage 1: radius 121 with 0.00821 rad per
  position, 115 with 0.00865: radius x curvature = 0.99), so OutRunners' change dh gives
  radius = 4096 x 303 / (2 pi x 272 x |dh|) = 726 / |dh|, `ROAD_RIGHT` for dh > 0.
- **Hills:** OutRun's heights for its crash, smoke and incline frames are its own flat road (`$2400` per
  entry, as on Hang-On's course), except near the car: the incline frame (OFerrari reads
  `road_y[$1E8] - road_y[$1F0]`: `$12` level, `$13` up, below `$12` down) follows OutRunners' own
  decision for its car (`$7B3F`: `$2004D9` = 1 level, 0 pitched up against the camera, 2-5 pitched
  down; checked on Kenya's hills). Design decision.
- **Road position** (OutRun's first-stage steering limit near the start, `cur_stage` = `$2080A2`): the
  distance from the course's start in OutRun positions.

### 7.4 Hooks (V70, player 1's task `R25` = `$208000`, in a race)

| Hook | OutRunners | With the Ferrari |
|---|---|---|
| `$7348` `test1 #2, [R20]` | a collision flagged this tick (bit 2 of `$200400`, kind `$20046E`, argument `$200468`) -> bounce `$7F87` | the flag cleared; OutRun's rules (7.5) |
| `$A39C` the car's handling (states 1-4) | steering, speed, lateral, advance | OutRun's car logic, once per OutRunners tick (30 Hz): `OFerrari::move`, `set_curve_adjust`, `set_ferrari_x`, `do_skid`, the wheels, `set_ferrari_bounds`, `OCrash`, `OSmoke`, the shadow, `OSprites`; OutRun's game state `GS_START1` on the start line (state 1), `GS_INGAME` after; OutRun's time 0 in OutRunners' time-up state (3) |
| `$A477` `movz.hw 74[R23], R0` | the speed is used from here | `$209074` := the Ferrari's km/h x 136 |
| `$A5EE` `movs.hw R0, R0` | the advance = speed x cos(angle of the lateral velocity to it) | R0 := 0: the advance is the speed; OutRunners' own segment lengths at the car's lateral position (`$A682`) still make the inside line shorter (OutRun's own line correction, `update_road_pos`, is not used: it is OutRun's version of the same thing) |
| `$A66E` `mov.h 38[R23], R13` | lateral = `$209038` + lateral velocity `$20042E` + push `$2004CE`, then within half-width + `$60` | the Ferrari's lateral position; its change since the last tick in `$20042E` (OutRunners' camera lags it, `$7B74`: `$209106`), no push |
| `$9E48` (`$99B8` after the car's parts are written) | the car's entries from 10[`$2080C0`] up to R10, each also at +`$3000` | the body's entry becomes the overlay tag, the others get no source data (7.6) |
| `$14971`, `$ED6F` | the game-over animation (`$9A01` switches the car's sprite object to `$14900`, which splits the car into objects drawn by the generic `$ED50`) | the body part (`$149A1`) is tagged, the others hidden |
| `$121E8` | the engine's pitch and on/off to the sound board (`$700710`, `$700712`), every tick | not run; `$700712` = 0 (engine off) |
| `$1222F` | the tyre/skid parameter (`$700718`) | not run |

In the goal run (state 4, `$746C`) OutRunners' script drives the car (`$7602`: the speed approaches a
target, the wheel steers to a lateral target, then the car is stopped) and nothing is written: the
Ferrari is carried by it, with OutRun's engine held on the accelerator while the script speeds up
(as OutRun's own goal run holds it) and its speed and position the script's (design decision). The
script's target speed is the game's (445 km/h on the full course).

### 7.5 Collisions (design decisions)

OutRunners' own tests decide that something was hit and what (`$127DB`, `$12554`/`$1261B`, `$1254F`,
`$12896`, with MAD POWER's box); OutRun's rules decide what follows:

| Kind (`$20046E`) | What it is | OutRunners' answer | The Ferrari |
|---|---|---|---|
| 0 | roadside scenery | bounce, state 5 | OutRun's crash (`olevelobjs.collision_sprite`), gated as OutRun gates it (no new hit for 4 ticks, `OLevelObjs::sprite_normal`: OutRunners flags every tick of an overlap) |
| 1 | an object with its own position (Kenya's elephants) | bounce off its lateral, speed and lateral velocity (`$8029`) | OutRun's contact with a car: a skid away from it, the speed cut to its speed - 80 km/h, the spin chance of OutRun's cars (1, not lorries' 3); its speed is the word `2C` << 8 in the car's units, as `$805B` uses it (the elephants run at about 220 km/h) |
| 2 | another car of the race (`$200468`: its number) | bounce by weights and the response table (`$8074`) | the same as kind 1, the car's speed from its record (`+$4C`); not met in a one-player race |
| 3 | a minor contact (`$1254F`) | 15 ticks of off-road drag (`$7FAA`: `$209147` = 15, read at `$B7F0`) | OutRun's off-road (both wheels off) for 15 ticks |

The response's sound is OutRun's (REBOUND, the crash's own sounds); OutRunners' crash sounds (`$8166`,
`$81C5`, `$8250`) never come, as its response does not run.

### 7.6 Drawing

- **Place:** the tag stands where MAD POWER's body would be drawn: the middle of its body sprite and
  its bottom line, in list order (roadside objects nearer to the camera are drawn over the Ferrari).
  OutRun's sprite layer (`hwsprites::render_to`, OutRun's 320 x 224 screen, priority 8 as on Hang-On's
  course) is drawn there with the Ferrari's bottom middle (x 160, y 221: `OFerrari::setup_ferrari_sprite`)
  on that point. The Ferrari's crash, its thrown passengers and smoke are in the image, so they are drawn
  at the car's depth too.
- **Size (design decision):** OutRun's size, scaled by MAD POWER's projection scale (`34[R20]` of the
  car's sprite object, `$100` on the start line): OutRunners' camera flies in at the start and pulls
  back with speed (to about 0.85 near top speed), and the Ferrari follows it as MAD POWER does. At
  scale 1 the two cars are about the same size (MAD POWER's body 88 x 51 pixels, the Ferrari 90 x 42).
- **The overlay** (`crossover/s32video`, marked PORT): an entry with word 6 = `$0C5E` and no source data
  (MAME's renderer skips it) asks for an image (word 2: image and scale); `S32Video::draw_overlay` writes
  it into monitor A's back sprite buffer with the entry's palette word (its priority group) and pen 1,
  keeping each image pixel alongside; a later sprite pixel replaces it, a shadow sprite shadows it.
  The mixer takes the image's colour for such a pixel as the palette colour, so layer priorities,
  colour offsets (the fades), blending and shadows apply to it. Shadow pixels are System 32 shadows
  when the game has sprite shadows on, otherwise shadow pixels of the entry's group. With no overlay
  set nothing of this runs.
- **Colours:** OutRun's palette (`Video::own_rgb`, shadowed colours included) converted to System 32's
  15-bit format, per OutRun tick.
- **Game over:** the Ferrari stays in place of MAD POWER through the GAME OVER animation (its body part
  is tagged, the rest hidden).

### 7.7 Sound

OutRunners' board keeps its radio and the world's sounds. MAD POWER's engine and tyres are written to
the sound board as parameters (`$121E8`: `$700710` = engine speed `$209070` >> 8, `$700712` = `$20`,
from `$1219C`'s `$A0` at the start; `$1222F`: `$700718`), not as commands; with them not written and
`$700712` = 0 the engine is silent (`tools/ortrace` experiment with the music stopped by `$1F`: the
board's output fell from RMS 3,700 to 0). Its crash sounds are commands queued (`$12124`, queue
`$208800`) only by the bounce, which does not run. OutRun's sound board plays the Ferrari (engine,
tyres, crash, REBOUND) through `osoundint`, as on the other courses.

### 7.8 Checked

Harness runs (`-headless -script`), Linux build:
- **Acceleration and top speed:** from a standstill at full throttle (automatic), the Ferrari on
  OutRunners' pre-stage and OutRun's Ferrari on OutRun's stage 1 (no traffic) reach 50, 100, 150, 200,
  250, 270, 280, 285, 290 and 293 km/h at 1.43, 2.97, 4.17, 5.57, 7.20, 8.83, 11.00, 12.97, 16.27 and
  18.93 s, the same in both to the 1/30 s; top speed 293 km/h, which OutRunners' HUD shows.
- **Curves:** OutRunners' curves as OutRun radii: over the full course 65 % of the driving ticks are in
  curves, radius median 73 positions (minimum 24, rare); OutRun's own stages (their paths, measured the
  same way): 55 %, median 83, sharpest 35. Kenya's first curve (radius 98) gives `road_x[170] -
  road_x[511]` = -285 (OutRun stage 1, radius 121: -270). Steered towards the centre line by the
  harness (in proportion to the distance, full lock from 168 OutRun units off it), above 250 km/h on
  Kenya the car stays within 159 of the centre line in the radius-98 curves, 231 in radius 73 and 414
  in radius 59 (the road's half-width is 440). OutRun's full lock moves the car 18 OutRun units a tick,
  which balances the curve push at 293 km/h up to `road_x[170] - road_x[511]` of about 450: the
  sharper curves (radius 49 and below, up to 620) take the car wide unless the driver lifts, as OutRun's
  own sharpest curves do.
- **Branch choice:** full course, the car kept left of the centre line: the first checkpoint takes WEST
  (decision 1, course 1) with the lateral at -295; kept right: EAST (decision 0, course 0) at +254. Kept
  left with the time held, every checkpoint took the left/lower branch (courses 1, 5, 13, 25, 41) and the
  goal came on Kenya.
- **Collisions:** scenery hits start OutRun's crash (flips, thrown passengers, the car back on the road
  after); Kenya's elephants give OutRun's contact (skid, 273 -> 162 km/h).
- **Timer and goal:** Kenya, time running out: time up at 135 km/h, the Ferrari coasts to a stop in
  about 2 s (OutRun's rule with no accelerator), GAME OVER with the Ferrari, the map, the program's menu.
  Kenya with the time held: the goal, CONGRATULATIONS with the total time, the goal run, the map, the
  name entry, the menu.
- **Sound:** with OutRunners' music stopped mid-race (`$1F`), the board's output in a Ferrari race is 0
  (RMS 0-3 over 2 s windows), against 2,300-3,200 in a MAD POWER race (its engine); OutRun's sound alone
  (the board muted) is RMS about 1,280 in a race (its engine), 0 in a MAD POWER race.
- **Unchanged:** `tools/s32vtest` replays: 214 of 214 screens exact; `tools/orbtest`: session 1 and
  session 2 (with session 1's EEPROM) identical to MAME (RAM every frame, all I/O, all snapshots); a
  MAD POWER race on Kenya (3,000 frames, seven snapshots and the WAV) byte-identical before and after.
- The Windows build compiles; under Wine a Ferrari snapshot at frame 900 is identical to the Linux
  build's.
- Speed: a Kenya race with the Ferrari runs at about 5 x real time (headless, Xeon 2.7 GHz).

### 7.9 Not done / known issues

- OutRunners' own pictures of its car stay MAD POWER: the ending's picture, the map's and the HUD's
  car icons.
- The flying wreck and passengers are drawn at the car's depth in the sprite list and stand on OutRun's
  flat-road heights, not on OutRunners' hills.
- The HUD's gearbox shows AUTO, not OutRun's LOW/HIGH.
- Two players, linked cabinets: player 1 only.
- Kenya alone: the results show 0 for the four checkpoints before Kenya.

