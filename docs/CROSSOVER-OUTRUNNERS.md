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
KENYA** (its Kenya stage alone), with OutRunners' car **MAD POWER**, OutRun's Ferrari (section 7) or
Super Hang-On's bike (section 8). MAD POWER also drives OutRun's courses (section 9) and Super Hang-On's
and Hang-On's (section 10).

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
    manual box (the race setup's GEARBOX = MANUAL), and the accelerator;
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
- Hang-On's bike on OutRunners' courses (section 8.10), and MAD POWER on Super Hang-On's and Hang-On's
  courses (section 9.9; it drives OutRun's, section 9).
- The menu's MUSIC choice: OutRunners' courses play OutRunners' own radio.

## 7. OutRun's Ferrari on OutRunners' courses (`crossover/orvehicle.*`)

(`crossover/orvehicle` was `crossover/orferrari` until Super Hang-On's bike joined it, section 8: one
class, `ORVehicle`, with the vehicle chosen as in `crossover/hovehicle`.)

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
| Road edge | single road of `road_width` w <= `$FF`: wheels off from w + `$D4` to w + `$104`, edge w + `$EC` | the segment's half-width `+$12` (264 to 440 on the full course) | OutRun's w = half-width x 6/11 - 236 (440 -> 4: OutRunners' standard road is OutRun's narrowest, edge 236). Its narrower roads (264-412) would need w < 0, which OutRun's code does not take (its narrowest road); for them `ORVehicle::check_wheels` makes OutRun's comparison with the edge where it is |
| Limit | `set_ferrari_bounds`: w + `$1F4` | half-width + `$60` (`$A77C-$A79A`) | OutRunners' (the tighter), converted |
| Distance per km/h | `CAR_BASE_INC` 303 per tick in road position (16.16; the integer part indexes the path) | 136 per tick in `$8000` per segment | one OutRun path position = 272/303 = 0.898 of a segment at the same km/h |
| Curve | `road_type`, `road_curve`; path: one unit vector (4096) per position | segment heading (12-bit, `+0`), turning right as it grows (`$104F6`: the forward vector is (sin h, cos h) with x to the right) | see 7.3 |

The lateral scale and the distance scale together keep OutRun's handling law in car widths: OutRun's
curve push (`OFerrari::set_curve_adjust`) moves the car sideways by about 4.7 x curvature x km/h
OutRun units a tick, i.e. a drift angle of the curvature times a look-ahead of 21 car widths; with
these scales it is 21 car widths in OutRunners too (OutRunners' world is 1.57 times OutRun's in car
widths per km/h: both its speed and its curvature per distance scale by that).

### 7.3 The road for OutRun's car code (`ORVehicle::build_road`)

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
| `$1222F`, `$12235` | the tyre/skid parameter (`$700718`); `$12235` is the routine's second entry, called directly by the wheelspin (`$AB28`), off-road (`$B95D`) and goal-run (`$7552`) code | not run (`$12235` since section 8.9) |

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
board's output fell from RMS 3,700 to 0). The tyre parameter's routine has a second entry, `$12235`,
which the wheelspin, off-road (gravel) and goal-run code call directly; it was found with the bike
(section 8.9) and is now skipped too, so MAD POWER's gravel sound no longer plays when the Ferrari
leaves the road. Its crash sounds are commands queued (`$12124`, queue
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
  (the board muted) is RMS about 1,280 in a race (its engine), 0 in a MAD POWER race. (That race stayed
  on the road: off it, MAD POWER's gravel sound still played until `$12235` was hooked, section 8.9.)
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


## 8. Super Hang-On's bike on OutRunners' courses (`crossover/orvehicle.*`, `crossover/bike.*`)

VEHICLE: SUPER HANG ON BIKE with TRACK: OUTRUNNERS FULL COURSE or OUTRUNNERS KENYA. The bike is the
one that rides OutRun's and Hang-On's courses: Super Hang-On's own tasks rebuilt in C and checked
against MAME (`crossover/shophys`: speed, centrifugal push, steering, lean, off-road, knockback, the
crash with its rider and shadows, the sound task) run in OutRun's car slot by `crossover/bike`, with
its sprites drawn by OutRun's sprite code and its sound from Super Hang-On's sound board. OutRunners
runs unmodified and keeps the world, through the hooks of section 7.4 (shared code, `ORVehicle` with
`vehicle` = `SH_BIKE`), plus the widths of section 8.4. Player 1, the left screen.

### 8.1 The run-up and the rate

The run-up is section 7.1's (MAD POWER, automatic). In a race OutRunners' car sees the wheel centred,
no accelerator and the player's brake (its camera dips as the bike brakes). At `$A39C` (once per
OutRunners tick, 30 Hz) `CrossoverBike::tick` runs two Super Hang-On frames, as it does for each of
OutRun's 30 Hz ticks: Super Hang-On runs its tasks at 60 Hz. The program's 30 fps mode gives the same
bike to the 1/60 s (8.8). The supercharger is GEAR1 or GEAR2, as on the other courses.

### 8.2 Units, with their sources

Super Hang-On's units are OutRun's (`crossover/bike.hpp`, from the code: lateral X, track position,
16.16 km/h; the distance per km/h is the same within 0.4 %, Super Hang-On's 151 per frame at 60 Hz
against OutRun's 303 per tick at 30 Hz). So the bike takes the Ferrari's conversions of OutRun's world
into OutRunners' (7.2); only what belongs to the bike itself changes.

| Quantity | Super Hang-On | OutRunners | Conversion / rule |
|---|---|---|---|
| Speed | `$260078`, 16.16 km/h | `$209074` = km/h x 136 | x 136, as the Ferrari. OutRunners' speedometer shows the bike's own value: 241 at top speed, 279 with the supercharger (Super Hang-On's speedometer shows its value x 279/240: 280 and 324), as Hang-On's speedometer does |
| Lateral | `$260022`, + = left, OutRun's units | `$209038`, + = right | x (-11/6), as the Ferrari |
| Road edge (off-road) | the bike's X against 300, its centre line (`sub_07F30`) | the segment's half-width `+$12` | the bike's centre line against OutRunners' asphalt edge: off-road when \|X\| > half-width x 6/11 (440 -> 240; 264 -> 144). `CrossoverBike::set_road_edge` |
| Limit | 500 from its road's centre (`$85B6`, `sub_07F0E`) | half-width + `$60` | OutRunners' (the tighter: 536 -> 292), converted, as the Ferrari |
| Road's width | Super Hang-On's road is one road | one road, its width changing | no carry with the width (OutRun's carry, `$C1B2`, is for its split roads), as the Ferrari |

### 8.3 The road for Super Hang-On's code

- **Curve push:** Super Hang-On's road builder (`sub_01236`, verified as `sho_road_build_sh`) over the
  path of 7.3 (OutRunners' segment headings, one OutRun position = 272/303 of a segment ahead of the
  car), as on OutRun's and Hang-On's courses it runs over their paths (the same format). The centrifugal
  task reads its table as on its own roads. The path is built at the car's place each tick; the
  builder reads it from the bike's own track position, which its speed task moves on in the tick's
  first frame, so the second frame reads it one or two positions further on, as on the other courses.
- **Curve type and radius** (`$2600AC`/`$2600B0`; the distance correction in its speed task, the demo
  rider): from the headings, as for the Ferrari (7.3).
- **Road heights** for the crash objects (`$261200`): OutRun's flat road of 7.3, as on Hang-On's course.

### 8.4 Collisions (design decisions)

OutRunners' tests decide that something was hit and what; Super Hang-On's rules decide how close
counts, and what follows. The car's own collision box is in the list `$124A4` builds every tick
(`500[R25]` -> its entry: `+6` segment, `+$A`/`+$C` along the road, `+$E`/`+$10` the lateral span,
lateral -/+ MAD POWER's 44 from `$12928`). The tests read the span when they start, so the program
sets it at each test's entry, keeping its centre:

| Test (entry) | Kind | Bike's half-width | Why |
|---|---|---|---|
| `$1254F`, `$12554`, `$1261B`, `$126F8` (roadside objects' footprints) | 3, 0 | 0 | Super Hang-On's scenery test is the bike's centre line against the object (on OutRun's courses: against OutRun's hit area, never wider than the object as drawn). OutRunners keeps the car's width in the car's box and the object's in its footprint, so the footprint stands for the object |
| `$127DB` (the race's cars), `$12896` (objects with their own position: Kenya's elephants) | 2, 1 | 10 x BIKE SIZE, x 11/6: 18 at full size, 15 at 80 % | Super Hang-On hits a vehicle within its half-width + 10 (`$74D4`); the 10 is the bike's part and follows BIKE SIZE, as on OutRun's courses (`CrossoverBike::traffic_half_width`) |
| `$B79B` (off-road flag `$20907C`, from `$A3EA` with the car's half-width in R3) | | 0 | the bike's centre line, as its own off-road rule; the flag only feeds OutRunners' handling and wheel sounds, which the bike replaces |

The length of the box along the road stays MAD POWER's (`+$A0`). The answers:

| Kind | The bike |
|---|---|
| 0 (scenery) | Super Hang-On's crash: `CrossoverBike::scenery_hit` (via OutRun's collision flag, as on OutRun's and Hang-On's courses) counts a hit for every frame of contact while not crashing, Super Hang-On's own rule, so there is no gate |
| 1, 2 (an elephant, another car) | Super Hang-On's knockback away from it (`$7510`: 14 a frame, kept within 40; the knock also cuts the throttle and brakes, its speed task's `$260088` rule), on both Super Hang-On frames of the tick; its contact sound `$89` when a contact with that object or car starts (`$7540`; OutRunners flags every tick of an overlap) |
| 3 (minor contact) | Super Hang-On's off-road for 30 of its frames (OutRunners' 15 ticks, `$7FAA`): `CrossoverBike::force_offroad` |

OutRunners' bounce and its crash sounds do not run, as for the Ferrari.

### 8.5 Time up and the goal

- **Time up** (car state 3): Super Hang-On's rule: its timer at 0 cuts the throttle (`$616C`), and below
  80 km/h its speed task always accelerates (`$61B8`), so the bike slows to about 80 km/h and holds it.
  OutRunners' time-up state lasts 180 ticks (6 s, `$73E3`) whatever the speed, then shows GAME OVER;
  the bike is still rolling then, as it would be in Super Hang-On.
- **Goal run** (state 4): OutRunners' script moves the car, as for the Ferrari (7.4): the bike's speed
  and position are the script's, with Super Hang-On's engine and the throttle held while the script
  speeds up (design decision, the Ferrari's). The script's speed on Kenya is 329 km/h.

### 8.6 Drawing

- **Place:** the overlay of 7.6: the bike's frame in OutRun's sprite layer (its crash, rider and shadows
  included) with its anchor line (y 222, `CrossoverBike::setup_sprite`; the Ferrari's is 221, each the
  sprite's own y in OutRun's screen) on MAD POWER's bottom line, x 160 on its middle.
- **Size (design decision):** BIKE SIZE applies, as on OutRun's courses. Like OutRun's, OutRunners'
  world is sized for a car: MAD POWER (88 x 51 pixels at scale 1) is the Ferrari's size, and the bike at
  80 % is 25 x 56. On Super Hang-On's and Hang-On's courses, sized for bikes, it stays full size. The size
  is then scaled by MAD POWER's projection scale, as the Ferrari's.
- **Off-road dust:** OutRun's smoke from the bike's off-road state, as on OutRun's courses (Super
  Hang-On's own dust task is not rebuilt; on Hang-On's course Hang-On's draws it).
- **Colours:** OutRun's palette with the bike's six palettes from Super Hang-On's ROM (as on OutRun's
  courses), converted per tick as in 7.6.

### 8.7 Sound

OutRunners' board keeps its radio and the world's sounds, without MAD POWER's engine, tyres and gravel
(7.7, 8.9). Super Hang-On's sound board plays the bike: its sound task's packet every Super Hang-On
frame (engine, squeal, off-road rumble, the supercharger), its crash sounds and `$89`, as on the other
courses. OutRun's sound board is silent (the bike's engine pitch is 0, as on OutRun's courses).

### 8.8 Checked

Harness runs (`-headless -script`), Linux build, 60 fps unless noted; the bike at BIKE SIZE 80 %.
`CB_ORF_TRACE` writes the bike's off-road, crash, knock and lean frame after the Ferrari's columns.

- **Acceleration and top speed** (full throttle from a standstill, steered along the bike's road):
  on OutRunners' pre-stage and on OutRun's stage 1 (no traffic) the bike reaches 50, 100, 150, 200,
  220, 230, 235, 240 and 241 km/h at 0.83, 1.87, 3.27, 5.37, 6.60, 7.40, 7.83, 8.37 and 8.47 s, the same
  in both to the 1/60 s; with the supercharger held, 250, 260, 270, 275 and 279 at 8.60, 8.77, 8.90, 9.00
  and 9.07 s in both. Top speed 241 (its speed task's sawtooth 241.1-242.0), 279 supercharged. The 30 fps
  mode gives the same times on the pre-stage.
- **Curves** (Kenya, steered towards the centre line by the harness, above 200 km/h): the bike stays
  within 137 of the centre line in the radius-98 curves, 352 in radius 73 and 416 in radius 59 (the
  road's half-width is 440). It left the road only when an elephant knocked it off.
- **Branch choice:** full course, kept left of the centre line: the first checkpoint takes WEST
  (decision 1, course 1) at lateral -341; kept right: EAST (decision 0, course 0) at +199.
- **Crash:** steered off the road on Kenya and back, the bike hits a tree at 87 km/h: Super Hang-On's
  crash (the bike slides, the rider is thrown, both lie on the ground), 78 ticks (2.6 s), then the bike is
  back on the road at X 17.
- **Elephants:** on Kenya, 11 ticks of contact with two elephants: four contacts starting (four `$89`),
  the knock reaching -38, the bike pushed from lateral 221 to OutRunners' limit (535) while its speed
  fell from 238 to 152 km/h.
- **Off-road and box widths:** off-road from X -252 (the edge is 240); OutRunners' box for the car
  (`$208514`) read -44..44, set to 0..0 at the roadside tests and -15..15 at the car test.
- **Timer and game over:** Kenya, time up at 206 km/h; 82 km/h 1.3 s later, then 79-81 km/h; GAME OVER
  6 s after the time-up, the map, the program's menu.
- **Goal:** Kenya with the time held (`CB_ORF_FREEZE_TIME`): the goal, CONGRATULATIONS with the total
  time (1'09"02), the goal run (the bike carried at the script's 329 km/h, then stopped), the ending,
  the name entry, the menu.
- **Sound:** with OutRunners' music stopped mid-race (`$1F`) and Super Hang-On's board muted
  (`CB_XAUDIO_MUTE_SH`, for testing), OutRunners' board is silent in a bike race (RMS 0 in every 2 s
  window, off-road included); Super Hang-On's board alone (`CB_OR_MUTE_BOARD`) is RMS 1,440-2,230 in a
  race.
- **Menu:** VEHICLE cycles SUPER HANG ON BIKE (OUTRUNNERS FULL COURSE kept), HANG ON BIKE (the track
  becomes HANG ON), OUTRUNNERS MAD POWER (back to OUTRUNNERS FULL COURSE), OUTRUN FERRARI, SUPER HANG
  ON BIKE; a saved bike with an OutRunners track is kept at start-up.
- **Unchanged:** `tools/s32vtest` replays: 214 of 214 screens exact; `tools/orbtest` session 1 (8,000
  frames): RAM identical at every frame, 2,015,481 I/O accesses identical, 16 of 16 snapshots; a MAD
  POWER race on Kenya (3,000 frames, seven snapshots and the WAV) byte-identical before and after. The
  Ferrari on Kenya and on the full course (3,300 frames, nine snapshots each and the WAV): byte-identical
  after the move to `ORVehicle`; with the `$12235` hook (8.9) the snapshots stay identical and the WAVs
  differ from 25.2 s and 25.7 s on, after the scripts steer off the road at 25.0 s: MAD POWER's gravel
  sound is gone (and the board's later output differs with it).
  The bike on OutRun's stage 1 and on Hang-On's course: byte-identical before and after.
- The Windows build compiles; under Wine a bike snapshot at frame 900 is identical to the Linux build's.
- Speed: a 3,300-frame Kenya script with the bike runs in 15.8 s (15.3 s with the Ferrari), run-up
  included.

### 8.9 Found on the way: MAD POWER's tyre parameter has two entries

The routine that writes MAD POWER's tyre/skid parameter (`$700718`) starts at `$1222F` with a test of
the off-road flag and falls into `$12235`, which the wheelspin (`$AB28`), off-road (`$B95D`) and
goal-run (`$7552`) code also call directly with the sound's number in R0 (`$1224A` writes it; 298
writes of 2 and 4 in a bike race off the road, found by logging the V70's writes to the shared RAM).
Section 7's hook at `$1222F` caught only the first entry, so MAD POWER's gravel sound played on
OutRunners' board when the Ferrari (and the bike) left the road. `$12235` is now hooked for both
vehicles (to its `rsr`, `$12251`).

### 8.10 Not done / known issues

- **Hang-On's bike** stays on Hang-On's course. It exists only as Hang-On's own program on its emulated
  board (`crossover/homode`): its speed, centrifugal, bike and crash tasks (`$8566`, `$879A`, `$AF10`,
  slots 4-7) work on Hang-On's RAM, its road and its sound packet, and nothing of it has been rebuilt as
  code that can run outside that board (only Hang-On's road builder is, in `shophys/sho_road.c`). Every
  vehicle the project puts on another game's world is code of its own (OutRun's in Cannonball, Super
  Hang-On's rebuilt and checked against MAME); a second emulated board driven as a vehicle beside
  OutRunners' would be a new design. It needs Hang-On's bike tasks rebuilt in C and checked against
  MAME as Super Hang-On's were, and its sprites converted.
- OutRunners' own pictures of its car stay MAD POWER (the ending, the map's and HUD's car icons); the
  HUD's gearbox shows AUTO.
- The crash objects stand on OutRun's flat road, not on OutRunners' hills (as the Ferrari's).
- The roadside footprints stand for the objects' outlines; they were not compared with the objects as
  drawn (on OutRun's courses the bike's hit area is clipped to the drawn outline).
- Super Hang-On's own dust is not drawn (OutRun's off-road smoke instead, as on OutRun's courses).
- Two players, linked cabinets: player 1 only.


## 9. MAD POWER on OutRun's courses (`crossover/madpower.*`, `crossover/orcar/*`)

VEHICLE: OUTRUNNERS MAD POWER with TRACK: OUTRUN FULL ROUTE or any single OutRun stage. The design of
Super Hang-On's bike on OutRun's courses (`crossover/bike`): OutRun's world keeps running in Cannonball
(road and camera, traffic, roadside objects and the collision tests, checkpoints and forks, timer,
score, goal and bonus, OutRun's sound board for the world); the vehicle in OutRun's car slot
(`OInitEngine::update_engine`) is OutRunners' own car code, transcribed and proven against its board
(`docs/OUTRUNNERS-RE.md` section 13): the handling, the sprite frame and the sound parameters
(`orcar::ORCar`), and the car's sprite object (`orcar::CarSprite`, section 13.8 there). It runs once per
OutRun tick, which is OutRunners' car rate (30 Hz) at both of the program's frame rates. The ROMs are
the full `orunnersj` set (the main program, the sprite ROMs and the sound ROMs, loaded at the first
race with MAD POWER, as the board's are for OutRunners' courses).

### 9.1 Where it plugs in (Cannonball's OutRun code, each marked `crossover`)

| Place | With MAD POWER |
|---|---|
| `OInitEngine::init` | a race (`GS_INIT_GAME`) with MAD POWER chosen: `CrossoverMadPower::reset` (OutRunners' state 0 at the first tick, the sound board powered on); the attract mode keeps the Ferrari |
| `OInitEngine::update_engine` | `madpower.tick()` in place of `OFerrari::move` ... `set_ferrari_bounds` (as the bike); OutRun's L/H gear text is not drawn |
| `OFerrari::setup_ferrari_sprite` | a scenery hit (`olevelobjs.collision_sprite`) goes to MAD POWER; the car's entry is MAD POWER's |
| `OFerrari` passengers, shadow, engine pitch, curve squeal, off-road sounds | not used (MAD POWER's own sprite and sound board) |
| `OTraffic::check_collision` | OutRun's test (the Ferrari's 0.6875 x the car's width: MAD POWER has the Ferrari's width, 9.2); the hit goes to MAD POWER; OutRun's bookkeeping (`collision_traffic`, the time trial's count) kept, its REBOUND sound not played |
| `OSprites::do_sprite` | the car's entry becomes a foreign entry (bank 7, address `$FFFF`) |
| `hwsprites::render_impl` | at a foreign entry, `CrossoverMadPower::blit` draws the car's image (widescreen offset, hi-res doubled) |
| `Video`, renderers | `XCAR_COLOURS` (4096) more colours after `EXT_COLOURS + 1`: MAD POWER's |
| `sdl2/audio.cpp` | `madpower.mix` adds its sound board |

### 9.2 Units and the road (`MadRoad`, `CrossoverMadPower::tick`)

| Quantity | OutRun | OutRunners | Conversion (section 7.2 the other way) |
|---|---|---|---|
| Lateral | `car_x_pos`, + = left; the Ferrari's half-width 24 | `$209038`, + = right; MAD POWER's 44 | OutRunners' lateral = -(X - the centre of the car's road) x 11/6, kept as MAD POWER's own (converted back to X each tick; recomputed from X only when the car's road changes, OutRun's road-width carry moves it or OutRun's limit holds it) |
| Speed | `car_increment`, 16.16 km/h | `$209074`, the advance a tick, `$8000` a segment | km/h = speed / 136, OutRun's speedometer shows it (as OutRunners' does) |
| Distance | road position 16.16, the path's index | segments of `$8000` | one segment = 303/272 OutRun positions; the car's advance (segments crossed + its place in the segment, the place as a fraction of the segment's length at its lateral position, which is OutRunners' own inside-line rule) moves OutRun's road position (fractions carried) |
| Road edge | asphalt half-width `$EC` around a road's centre (`OFerrari::check_wheels`), `road_width` + `$EC` for one wide road | the segment's half-width `+$12` | half-width x 11/6 (433 on a road of OutRun's narrowest width); the road the car is on as for the bike (`CrossoverBike::compute_road_frame`: split roads, the two roads drawn as one) |
| Heading | the path: a unit vector per position, its angle atan2(x, y) grows in right curves | the segment's heading (12-bit), grows in a right turn | each segment's heading = the car's heading + the path's turn (4096 a turn) from the car to the segment's start (interpolated between positions); on OutRun's mirrored road at a fork (`rd_split_state` set, X < 0: OutRun mirrors its own curve push there) the turns are the other way |
| Pitch, heights | OutRun's car has no slope force; its height sections only draw the road | `+2` pitch (the slope's force: sin(pitch) / 2; + = down), `+C` heights (+ = up, -128 x sin(pitch) a segment) | the pitch at the car from OutRun's height sections (9.10, design decision), for the car's segment and those after it; heights down the ring from it |
| Surface | | the course node's zone lists | the pre-stage's (node 0): its lists end at once, so beyond the asphalt's edges the wheel kind is 7, and no zones on it |

Inputs: the controls as crossover/ormode puts OutRun's onto OutRunners' cabinet (the wheel over the
ADC's travel, `$208100` = `$FF` - ADC; the pedals 0-`$FF`); the race setup's GEARBOX = MANUAL
(`crossover.or_manual`) gives MAD POWER's 6-speed manual box (GEAR2 up, GEAR1 down; their new presses
each tick), AUTOMATIC its automatic. One player alone: rank 0, the car ahead is itself (no catch-up).

### 9.3 The start, time up and the goal (design decisions)

- OutRunners' state 0 runs at the first tick of the race (a tick of its own: the sound board must see
  the engine's start value `$A0` before the running `$20`); MAD POWER's place on the line is OutRun's
  (its X), its own grid position is not used.
- OutRunners' start line (state 1: brake held, the wheel centred, the engine revs with the
  accelerator) lasts while OutRun counts down, and ends at OutRun's start (`GS_INGAME`).
- Time up, the goal, the bonus and the ending are OutRun's: at time up OutRun stops calling the car;
  in the bonus run MAD POWER drives with OutRun's bonus inputs; after it, OutRun's ending runs with MAD
  POWER's image standing at the car's place (OutRun's passengers' animation is the Ferrari's).
- OutRun's limit for its car (`set_ferrari_bounds`) applies after the tick; MAD POWER's own (the
  half-width + `$60`) is the tighter.

### 9.4 Collisions (design decisions)

OutRun's tests decide that something was hit; MAD POWER's own responses (`$7F87`) follow, reported for
its next tick as OutRunners' roadside tests report theirs (they run after its car), every tick of an
overlap as OutRunners' tests flag them:

| OutRun | MAD POWER (kind `$20046E`) |
|---|---|
| a roadside object (`OLevelObjs`) | 0, scenery: the bounce by its speed, the crash frames ("Ouch!", "CRASH") |
| a traffic car or lorry (`OTraffic::check_collision`) | 2, a car of its race, of MAD POWER's own weight (272): its lateral position the car's own + the traffic's screen x x 11/6 (at the car's depth OutRun's screen x is in road units: `OTraffic::update_props`, x = xw1 x z / 512 + the road's centre, z about 512), its lateral speed its lane change (4 a tick towards `xw2`, x 11/6), its speed its km/h x 136 |

MAD POWER's width is the Ferrari's at the lateral scale, so OutRun's tests are unchanged. Its off-road
is its own (a side beyond the edge: OutRun's "one wheel off"), with its drag, shaker, gravel sound and
smoke kind; OutRun's wheel state stays "on the road", so OutRun draws no dust and plays no off-road
sound for it.

### 9.5 Drawing

- **The entries:** the car's sprite object (`orcar::CarSprite`, `$99B8` for the player's car: body,
  wheels, the wheels' smoke, the dust when the speed jumps, the crash's dust; its `$14374` objects that
  bring the wheels back after a frame without them), drawn from the car record of the last tick as on
  the board, at OutRunners' scale 1 (`$100`).
- **Rendering:** `orcar::S32Sprites`, MAME's `draw_one_sprite` for these entries, with the palette RAM
  as the game loads it at power-on (`$153904`: entries `$000-$9FF`; `$3153C`: `$F00-$FFF`; `$11881`) and
  the indirect palettes as it copies them into sprite RAM (`$107020` to `$401000`, `$10BE0`), and the
  mixer's colour for a sprite pixel with the registers the game sets in a race (sprite groups from bits
  12-13, palette base 0, no colour offsets, the transparency mask `$0FFF`). MAD POWER's palettes are the
  same on every course (palette RAM compared at six points of a full-course race); the per-course
  palette lists (`$31B14`, `$318CC`, `$31DDC`) load other entries.
- **Place and size (design decisions):** OutRun's camera is fixed to its car: MAD POWER's point is where
  OutRunners' projection puts it on its start line (the list's offset (160, 222) and R16 = -12, R15 =
  0), in the middle of OutRun's screen, at scale 1 (MAD POWER on its start line: 88 x 51 pixels, the
  Ferrari's 90 x 42); a crash's jump raises it by its height (world units are pixels at scale 1). Its
  frame's hill index (`$7B3F`) is OutRun's own decision for its car (`road_y[$1E8] - road_y[$1F0]`:
  above `$12` uphill, index 0; `$12` level, 1; below, downhill, 2). OutRunners' index is not the road's
  pitch: it is the camera's pitch less the road's at the car ((camera - `$B0` - road) x 16 / `$300`),
  the road as the camera sees it; the camera here is OutRun's, and OutRun's decision is the same thing
  for OutRun's camera (the screen slope of the road at the car), so it stays with the slope of 9.10
  (the record's pitch passed to `$7B3F` is 0, the camera's pitch carries OutRun's decision).
- **In the list:** the image is drawn at the Ferrari's entry (`priority` `$1FD`), so nearer objects are
  drawn over it, as OutRun draws them over the Ferrari.
- **Not drawn:** the player marker OutRunners shows on its start line, the wheels flying off in a crash
  (`$14374` draws them with the generic `$ED50`; their timing is kept), the objects `$14460` switch off
  for MAD POWER.

### 9.6 Sound

OutRunners' sound board (`crossover/orsound`, a second instance beside the music player's), driven as
its main CPU drives it for player 1, no music: the engine's parameters (`$700713` the car's engine,
`$700714` the box, `$700710` = 1 and `$700712` = `$A0` at the start; then every tick `$700710` = the
engine speed >> 8, `$700712` = `$20`), the tyre/skid sound's number (`$700718`, each write the car's
code makes: wheelspin, locked wheels, slides, the gravel off the road, the shift smoke) and the
commands the car queues (the crash's two, the bounce's `$31`), one a 60 Hz frame to `$700700` as the
frame interrupt `$3F8DA` sends them. Each car tick's writes reach the board at the start of the next
audio frame. The mix is crossover/ormode's (MAME's left speaker, x 0.17). These are all the main CPU's
writes to the car's parameters in a race (`ortrace`, every write to `$700700-$7008FF` from power-on to
a race: `$700700` commands, `$700710-$700714`, `$700718`, and player 2's engine off). The program's
OutRun sound board plays the world (music, traffic, voices); OutRun's engine and tyre sounds are off.

### 9.7 Checked

Harness runs (`-headless -script`), Linux build. `CB_MP_TRACE=<file>` writes a line per tick (frame,
OutRun state, car state, km/h, X, lateral, half-width, road position, road control, road width, split
state, mirror, heading, off-road, gear, engine speed, frame, sub-frame, jump); `CB_MP_DEBUG` logs the
hits and the sound board; `CB_MP_MUTE_BOARD` mutes MAD POWER's board (testing).

The acceleration and top-speed checks below were made with the road level (before 9.10; now
`CB_MP_LEVEL=1`, which gives byte for byte the program of those checks: stage 1, four snapshots and the
WAV). With the slope, OutRun's stage 1 is level for its first 129 ticks (its first hill, height section
211, begins at frame 779).

- **Acceleration, automatic** (OutRun's stage 1, no traffic, full throttle from the start, steered
  along its road): against MAD POWER on OutRunners' pre-stage (the board under `tools/ortrace`, the
  accelerator held from the start line), its speed is identical at every tick for the first 124 ticks
  (to 148 km/h); there the pre-stage's first slope begins (the segment's pitch 8, then rising), which
  OutRun's level road does not have. With the pre-stage made level on the board (every ring segment's
  pitch written 0 each frame, the lateral held at 0), identical at every tick for 501 ticks (16.7 s,
  to 414 km/h): 50, 100, 150, 200, 250, 300 and 330 km/h at 1.53, 3.10, 4.40, 5.70, 7.00, 8.40 and 9.53 s
  in both. At tick 502 the board's car touches a roadside object at the line it was held on (OutRunners'
  minor contact: its drag x 5.6).
- **Acceleration, manual** (shifts up at ticks 40, 80, 120, 160 and 200 after the start, both): identical
  for 458 ticks (15.3 s, to 464 km/h); 50 to 330 km/h at 1.50, 2.60, 3.70, 4.77, 5.83, 6.87 and 7.57 s.
- The program's 30 fps mode gives the same speeds at every tick.
- **Top speed** on OutRun's stage 1: 451 km/h automatic (its gear's limit is 467), 471 km/h manual (480);
  OutRun's speedometer shows them.
- **The full route** (OUTRUN FULL ROUTE, normal traffic, the time held, steered at its road's centre):
  the five stages, the forks (on the mirrored road at the first, frame 3367: split state 2, the car on
  the left road), 36 ticks of traffic contact and 5 of scenery contact (30 crashes of MAD POWER's own:
  "CRASH" and the spin for cars, "Ouch!" for the rocks), the goal at frame 17757, the bonus, the ending. Snapshots: at speed (stages 1-4),
  in curves, at the fork, a traffic contact, a scenery contact, the goal, the steering frames left and
  right (sub-frames 0 and 8).
- **Sprites:** `orcar::S32Sprites` against the board's screen (a race frame's sprite RAM dumped, the
  car's entries rendered, compared with the board's screen): 3,484 of 3,484 pixels identical; a crash
  frame (body and crash dust): 5,914 of 5,925, the other 11 under the HUD band at the top of the screen.
- **Sound:** MAD POWER's board's part of the output (the WAV with and without it, `CB_MP_MUTE_BOARD`,
  the difference): RMS 286-735 over 2 s windows as the car accelerates; the board's own engine writes
  from a race replayed into a standalone board give 890-1,210 while accelerating, the same order.
- **Unchanged:** `tools/orcartest` `run_all.sh` (with the new comparisons, `docs/OUTRUNNERS-RE.md`
  13.8): 26 runs, 0 differences; `tools/orbtest` session 1 (3,000 frames): RAM identical at every frame,
  798,342 I/O accesses identical, 6 of 6 snapshots; `tools/s32vtest` replays: 214 of 214 screens exact;
  the Ferrari and Super Hang-On's bike on OutRun's stage 1 (1,500 frames, four snapshots, the WAV) and
  on OutRunners' Kenya (3,300 frames, nine snapshots, the WAV), and MAD POWER on OutRunners' Kenya:
  byte-identical before and after.
- The Windows build compiles.

### 9.8 Found on the way

- MAD POWER's palette entries are loaded once, at power-on, and are the same on every course; the
  palette lists loaded by course are the scenery's.
- Its body is an 8 bpp sprite with indirect palettes (sixteen sub-palettes); the indirect tables are in
  sprite RAM, copied from the ROM at power-on.
- The sprite object draws the record the car published the tick before (the race's car array is
  copied from the records at the start of each frame, before the car's tick).

### 9.9 Not done / known issues

- The wheels flying off in a crash are not drawn (their timing is: the wheels come back when they would).
- The start-line marker is not drawn.
- OutRun's ending runs with MAD POWER's image standing still and the Ferrari's passengers' animation;
  there is no MAD POWER version of OutRun's start and ending scenes. The attract mode shows the Ferrari.
- OutRun's HUD shows no gear for MAD POWER's manual box.
- MAD POWER's engine level from its board, against OutRunners' own race, was compared only roughly
  (9.7).
- The slope on OutRun's courses is a reading of OutRun's 2D hills (9.10): their steepest values give
  climbs steeper than any on OutRunners' own course (9.10.4).

### 9.10 The slope (`CrossoverMadPower::outrun_pitch`, `ORoad::xo_slope`)

OutRunners' handling feels the road's pitch: the force on the car is sin(pitch) / 2 less the drag
(`$A400-$A40C`, 16384 = 1.0), and the ring's world height falls 128 x sin(pitch) a segment. On its
pre-stage (ring dumped at frame 3200 under `tools/ortrace`) the pitch at the car is 240 and rising to 480,
the height falls 45 a segment at 240: a positive pitch is a descent, and the board's car there is faster
than on the same course made level (273 against 257 km/h at frame 3300, 430 frames after its start), as the force says.
`tools/orcartest` (unchanged, 0 differences) runs the car on OutRunners' own courses with their hills
(full course, Kenya): that is the model on real slopes.

The adapter now gives the car's segment and the segments after it the pitch at the car (12-bit, + = down)
and the heights down the ring from it; `CB_MP_LEVEL=1` gives the level road of before (testing).

**9.10.1 The scale (design decision).** A road at a slope, seen from a camera on the road behind it, moves
the far road by (focal length x tan(slope)) pixels. A hill that looks as steep in two games moves the far
road as far; so tan(pitch) = (the host's shift in pixels) / OutRunners' focal length:
- **OutRunners:** 192 pixels. Its projection (`$102CE`) scales a point by the table `$19A340` at its depth z
  (`$10437-$1044B`: screen = coordinate x T[z] >> 12), and T[z] = 192 x 4096 / z (T[256] = `$C00`,
  T[1024] = `$300`, T[2048] = `$180`).
- **OutRun:** its road heights `road_y` are 1/16 pixel above the bottom line, 512 entries from the car to the
  horizon, each the last plus the section's change per entry (`ORoad::set_y_2044`: road_y = total << 4 >> 16,
  the level change `(horizon_base + horizon_offset) << 4`, `$2400` on stage 1: the road's 72 pixels). A change
  of c per entry on the whole road (a road at a slope) moves its far end 512 x c / 65536 = c / 128 pixels:
  tan(pitch) = c / 128 / 192.
- **Super Hang-On** (section 10): 64 pixels (10.1).

**9.10.2 OutRun's hills (design decisions).** OutRun's height sections (`ORoad::setup_road_y`, data read by
`init_height_seg`) are 2D: each section's values (from entry 6 on; entries 0-5 are 0) change the road's
per-entry height change from one stretch of the road ahead to the next (`read_next_height`: c(k+1) = value
<< 4 + c(k)), and the road at the car is drawn level with OutRun's camera. As the car nears a value's
stretch, that stretch's change shrinks by `height_final` (value x (`height_start` - `$100`) >> 4, 0 to
about 16 x value) and is 0 when the car reaches it: the camera has turned into the slope. So, read as a
road under a camera that rides it:
- the slope at the car is the values reached in this section (each 16 x value x 255 / 256) plus
  `height_final` of the one being reached, plus any tilt of the road at the car itself (`c0` - level: the
  hold of sections 2);
- values held at a fixed distance ahead (the delay sections, `ctrl2` 1, and 3 from entry 6: their
  stretch does not come nearer) change the road ahead, not the car's slope; a horizon move (`ctrl2` 4,
  Gateway) is OutRun's camera, not a slope;
- a section begins on a level road (OutRun's picture: the road at the car and ahead level). The values of a
  section need not add up to 0 (stage 1: section 211 ends 576 down, 213 1,152 up): OutRun's camera is level
  again at the next section and its picture does not show the difference; the adapter brings the slope back
  to level over the length of the section's last value.
`ORoad::set_road_y` keeps, per frame, the change at the car and the next (`c0`, `c1`), the level change, the
section (`height_lookup_wrk`), its entry (`height_index`), the value and `height_final`
(`ORoad::xo_slope`; OutRun reads none of it). Sections met on the 15 stages: `ctrl2` 0 on all, 1 on stages
1, 2, 6, 8, 9, 12, 13 and 14 (the time-trial map's order), 3 on 7, 4 on 2 and 6.

**9.10.3 The hill frame.** Unchanged (9.5): OutRunners' hill index is the road as its camera sees it, and the
camera is OutRun's.

**9.10.4 Checked** (Linux build, harness runs, the pitch now the trace's last column):
- **The pitch met:** OutRun's 15 single stages (4,000 frames each, full throttle): -640 to +418 (-56 to +37
  degrees; - = up); the full route -549 to +418. OutRunners' own full course (the ring at every 150 frames of
  `tools/ortrace` e15's route, 17,152 segments): -360 to +500 (-32 to +44 degrees). OutRun's steepest, -546 to -640, are its
  biggest values (1,152 to 2,304 on stages 1, 2, 5 and 15) taken one after another.
- **Stage 1, no traffic, full throttle, against the level road** (same inputs): section 211 climbs from
  frame 779 to -418 (36.6 degrees up) at frame 869 and falls to +233 (20.5 degrees down) at 983. On the climb
  at 198.5 km/h the car gains 0.971 km/h a tick against 1.243 on the level at that speed (197.5 against 204.8
  km/h at frame 869, 284.7 against 294.0 at 1013); down section 134 at 398 km/h, +323 (28.4 degrees down),
  0.530 against 0.294 (406.7 against 402.5 km/h at 1425, from 3.1 behind at 1317).
- **The full route** (as 9.7, the slope on): the goal at frame 17895 (17757 level), 38 ticks of traffic
  contact and 1 of scenery, then the bonus, the ending and the ranking (frame 21000).
- **Unchanged:** see 10.7.

## 10. MAD POWER on Super Hang-On's and Hang-On's courses (`crossover/shferrari.*`, `crossover/hovehicle.*`)

VEHICLE: OUTRUNNERS MAD POWER with TRACK: SUPER HANG ON AFRICA / ASIA / AMERICA / EUROPE or HANG ON.
Those courses run on their own emulated boards (`crossover/shmode`, `crossover/homode`), where OutRun's
Ferrari is put by `crossover/shferrari` and `crossover/hovehicle` (README.md; `docs/CROSSOVER-HANGON.md`
section 3): the board runs the game unmodified and its world (road, scenery, rivals, timer, checkpoints,
score, goal, rankings); where its main CPU reaches its own bike, instruction hooks hand the vehicle's
part to Cannonball's OutRun code in OutRun's car slot, with the board's world faked into OutRun's
variables (`world_to_outrun`: road position, curve, road width, heights, timer). MAD POWER takes the
Ferrari's place there (`SHFerrari::MAD_POWER`, `HOVehicle::MAD_POWER`): its car slot is
`CrossoverMadPower::tick` exactly as on OutRun's courses (section 9), with the board's road in OutRun's
variables, its image into the board's frame, its sound on OutRunners' board. The hooks, the speed and
distance hand-over (OutRun's units: road position, 16.16 km/h, X), the start-line blink, the goal exit and
the bike's sound dropped are the Ferrari's, unchanged.

### 10.1 The road (design decisions)

| | Super Hang-On | Hang-On | MAD POWER |
|---|---|---|---|
| Path | the second CPU's `$0604F0` | the second CPU's `$7F0F0` | OutRun's path format in both (4 bytes a position, a unit vector): `CrossoverMadPower::begin_board` gives `MadRoad` the board's path in place of OutRun's (`path_angle`); the segments, headings and distance as in 9.2 |
| Road edge | 300 (`$7F30`: off the road at \|X\| > 300) | 260 (`$B57C`) | the half-width x 11/6 (550 and 477), from OutRun's road width set as for the Ferrari (`$40` + `$EC`, `$18` + `$EC`) by `compute_frame`; one road, centre 0 |
| Hills | a vertical path run by the second CPU (10.1.1) | none (10.1.2) | the pitch at the car from Super Hang-On's path (10.1.1); level on Hang-On's course. The hill index of the car's frame is OutRun's decision on the heights the Ferrari gets there (9.5, 9.10.3) |
| Off-road flag | `$260098` | `$C7E098` | written from the board's own rule for its bike: \|X\| beyond its road edge; MAD POWER's drag, gravel and dust are its own |

**10.1.1 Super Hang-On's hills.** Unlike OutRun's, they are a 3D path: the second CPU runs a hill (its object
at `$060420`; main `$260420`) when the main CPU sets its number (`$06004C`): state (`+4`) 1 takes the path
from the table `$1A4C` (`+$C`), state 2 runs it with the distance into it (`+8`, positions in the high word,
added `$060074` a frame) until its vectors end. The path is unit vectors of 4096, one a position, as the road's
curve path: x the height (+ = up), y the distance along ((0, 4096) level). Its camera takes its direction
from the two vectors at the vehicle (`$1650-$1674`, the sum of vectors p and p + 1) and projects the vectors
ahead, two a step for 32 steps (`$16BC-$1706`): height x `$4000` / (along + `$300`) (`$174A-$1778`; along in
1/32 of the vectors' units, `$300` = six positions), which `sub_01494` adds, / 16, to the level road (8 an
entry) in the height table `$061200` the main CPU reads (/ 16 again: pixels): 64 pixels per unit of tan far
ahead, the focal length. The pitch at the car: tan = height / along of the two vectors at the vehicle x 64 /
192 (9.10.1), - = up (`SHFerrari::hill_pitch`). Africa (10,500 frames): -344 to +462 (-30 to +41 degrees); its
first hill (21) runs vectors down to (-3331, 2384), 54 degrees in Super Hang-On's own projection: +280 here
(25 degrees down).

**10.1.2 Hang-On's road is flat.** Hang-On's second CPU (the road's processor: 351 instructions executed in a
race) projects one path, the curves (`$C7F0F0`, `$ABA-$BD0`, the same projection as Super
Hang-On's); it has no second, vertical path (Super Hang-On's `$1602-$178A` and its table `$1A4C` have no
counterpart), and its height table stays the level road. MAD POWER's pitch there is 0, and its runs are
byte-identical to before.

The board moves its vehicle itself once: Super Hang-On steers it to the centre after the goal
(`sub_0B430`, in the speed task as replaced for the Ferrari). On these boards a change of X between ticks
that MAD POWER did not make becomes its lateral position again (`last_x`).

### 10.2 Collisions (design decisions)

The board's tests decide, MAD POWER's responses (`$7F87`) follow at its next tick, as on OutRun's
courses (9.4):

| Board | MAD POWER |
|---|---|
| Roadside object: Super Hang-On's test `$6EFA` (\|object x - `$160`\| within its width at depths `$109-$13F`, while `$260094` = 0), Hang-On's `$9E68-$9EAA` (the same at screen lines `$D0-$E0`, while `$C7E094` = 0): `$260084` / `$C7E084` counts every frame of an overlap | kind 0, scenery (the bounce by its speed, "Ouch!", "CRASH"), every tick the count moved |
| Rival bike: Super Hang-On's `$750A` (depth >= `$120`), Hang-On's `$A922`: \|rival screen x - `$160`\| within its half-width + 10 | the Ferrari's rule (11/8 of the half-width: MAD POWER is the Ferrari's width, 9.4); kind 2, a car of its race of its own weight: its lateral position the rival's own (`$34`: the board's X units and centre, + = right as OutRunners' lateral, so x 11/6; the screen x is `$160` + the road's x at its depth + `$34` x depth / 224 in Super Hang-On, `sub_07A98`, / 112 in Hang-On, `$AEC2`; checked at contacts: Super Hang-On X 94, `$34` -66, 28 units apart, screen 23 pixels at depth `$BF`), its lateral speed `$34`'s change since the last frame (2 or 3 a frame as the rival steers, `$75A0-$765E` / `$A9D0-$AAC8`) x 2 for the 30 Hz tick, x 11/6; its speed in km/h (as the Ferrari's, `S - 1.6 x d0`) x 136 |

The board's knock-back (`$260088`) and contact sound (`$89`) are not used, as for the Ferrari (MAD
POWER's crash sounds come from its board).

### 10.3 Drawing and colours

- **Place:** the image (`CrossoverMadPower::render`: OutRunners' entries for the car, scale 1) at the place
  it has on OutRun's screen (as the Ferrari's sprites there), in the board's frame at the bike's entry of
  its depth-sorted sprite list (the Ferrari's tag), with that entry's priority: nearer objects pass in
  front of it.
- **Colours:** the board videos' overlay (`SHVideo` / `HOVideo::overlay`) takes a third kind of pixel:
  `$4000` + a palette entry of MAD POWER's (`$000-$FFF`), which the frame gets as `EXT_COLOURS + 1` +
  the entry: MAD POWER's colours, loaded with its ROMs (`Video::set_car_colours`, section 9.5), after the
  board's (`Video::set_external`); a shadow pixel is the board's own shadow (`$1000`, as the Ferrari's).
  `Video::rgb_of` (screenshots) knows them too.
- The "Ouch!" and "CRASH" signs are drawn over the car, as OutRunners draws them (they cover most of it
  for the first frames of a contact).

### 10.4 Sound (design decisions)

OutRunners' board as on OutRun's courses (9.6). The board's bike engine (the sound packet's player bytes)
and the bike's effects (`$8A-$8D`) are dropped as for the Ferrari, and its contact sound `$89` too; OutRun's
engine stays silent (volume and pitch 0; OutRun's car sound code is not run). When the race ends (the
board leaves its race modes) or the vehicle leaves the course after the goal, MAD POWER's engine is
switched off (`$700712` = 0, as OutRunners does for a car that is not racing).

### 10.5 The goal and time up (design decisions)

- **Time up:** the timer at 0 gives MAD POWER no accelerator (the Ferrari's rule there); the board's game
  over, ranking and name entry follow, then the menu.
- **Super Hang-On:** after the goal line the board steers the vehicle to the centre and takes it off the
  course (`$6124`, `$7E1E`); its goal run, ending and name entry follow with Super Hang-On's own rider.
- **Hang-On:** past the goal line Hang-On stops its vehicle (speed 0 every frame, `$8588`): MAD POWER's
  speed is set 0 there each tick (`hold`), as the Ferrari's; then the exit, the ending and the name entry.

### 10.6 Found on the way: Super Hang-On's goal with another vehicle

With the Ferrari (and so MAD POWER) Super Hang-On's goal stopped at its bonus count. The bike's crash
objects (slots 3-6) were skipped only while racing (modes 1 and 2); in the goal sequence (mode 3) they ran
again, saw the scenery count `$260084` differ from their copy (`$9FE6`: Super Hang-On's scenery test had
counted the vehicle's hits while they were off), and started a crash; its end switches the bike's task back
on (`$A09C`), and that task, finding the vehicle retired, clears the speed (`$7E2C`) that the goal run
(slot 35, `$B50A`: `$53`) needs to bring the rider to the end of its run (`$B588`: `$26` up to `$1FF0`),
so `$6041E` never came. The crash slots now stay skipped for the rest of the game while they hold the
tasks seen in them in the race (`SHFerrari::on_task`). The Ferrari reaches the ranking and the menu too
now; before the goal nothing changed (the regression runs below).

### 10.7 Checked

Harness runs (`-headless -script`), Linux build, widescreen, 60 fps, `CB_MP_TRACE` per tick (section 9.7).

- The acceleration checks below were made with the road level (`CB_MP_LEVEL=1` now, 9.7); on Hang-On's
  course they hold with the slope too (level there).
- **Acceleration, automatic** (full throttle from the start line, steered to the centre of the road),
  against MAD POWER on OutRun's stage 1 (no traffic, 9.7): identical speed, gear and engine speed at every
  tick from the start, through curves, until the first contact: Super Hang-On's Africa 639 ticks (21.3 s,
  to 435.8 km/h; then a rival), Hang-On's course 322 ticks (10.7 s, to 353.2 km/h; then a rival). 50, 100,
  150, 200, 250, 300 and 330 km/h at 1.53, 3.10, 4.40, 5.70, 7.00, 8.40 and 9.53 s, 400 km/h at 14.80 s,
  as on OutRun's stage 1.
- **Acceleration, manual** (shifts up at ticks 40, 80, 120, 160 and 200 after the start, as in 9.7):
  identical for 536 ticks on Africa (17.9 s, to 468.7 km/h; then a rival) and 389 ticks on Hang-On's course
  (13.0 s, to 451.8 km/h; then it ran wide off the asphalt in a curve, where OutRun's stage 1 is straight);
  50 to 330 km/h at 1.50, 2.60, 3.70, 4.77, 5.83, 6.87 and 7.57 s.
- The program's 30 fps mode gives the same speeds at every tick (the whole runs, 686 and 685 ticks).
- **To the goal** (normal time, steered at the road's centre): Africa's six stages, goal at frame 9894,
  45 ticks of rival contact and 1 of scenery contact (17 crashes), then Super Hang-On's goal run, ending,
  ranking and name entry, then the menu; Hang-On's five stages, goal at frame 16890, 76 ticks of rival
  contact and 9 of scenery contact (27 crashes), then the ending, ranking and name entry, then the menu.
  Top speeds 437 and 433 km/h (Super Hang-On's speedometer shows its speed x 279/240, as for the
  Ferrari; Hang-On's shows the speed).
- **Game over** (the brake held from 15 s): time up, game over, ranking, the menu, on both boards.
- Snapshots on both boards: the start, at speed, curves, rival contacts ("CRASH", "Ouch!"), scenery
  contacts, the goal, the ranking, the menu.
- **Sound:** MAD POWER's board's part of the output (with and without it, `CB_MP_MUTE_BOARD`): RMS 249-746
  over 2 s windows as the car accelerates on both boards (on OutRun's courses 286-735); 0 once the car has
  stopped (its engine speed follows the accelerator to 0, as on OutRunners' board).
- **The slope on Africa** (full throttle, against the level road, same inputs): on the climb at frame 1798,
  -329 (28.9 degrees up), the car gains 0.970 km/h a tick at 280.7 km/h against 1.184 on the level; down the
  first hill at frame 1586, +328, 0.309 against 0.103 at 437.7 km/h. To the goal with the slope on: frame
  10188 (9894 level), 82 ticks of rival contact, then the goal run, the ranking and the menu.
- **Unchanged by the slope** (job 5, against the program before it, the audio fix `bf6da4d` included): the
  Ferrari and Super Hang-On's bike on OutRun's stage 1 (1,500 frames) and full route (6,000 frames, four
  snapshots), on Super Hang-On's Africa, on Hang-On's course (with Hang-On's bike) and on OutRunners'
  Kenya; MAD POWER on Kenya and on Hang-On's course: byte-identical (snapshots and the WAV). MAD POWER on
  OutRun's stage 1 with `CB_MP_LEVEL=1`: byte-identical to the program before. `tools/orcartest`
  `run_all.sh` 0 differences (the same counts as below), `tools/orbtest` session 1 identical, `tools/s32vtest`
  214 of 214, the Windows build compiles.
- **Unchanged (section 10's own checks):** the Ferrari and Super Hang-On's bike on Super Hang-On's Africa, and the Ferrari, Super
  Hang-On's bike and Hang-On's bike on Hang-On's course (3,000 frames each, seven snapshots, the WAV), MAD
  POWER on OutRun's stage 1 and on OutRunners' Kenya: byte-identical before and after (Hang-On's bike is
  offered on its own course only); `tools/orcartest` `run_all.sh`: 26 runs, 0 differences (83,811 /
  83,784 car and gear-box ticks re-synced, 76,099 / 76,072 free-running, 166,114 sprite-object draws);
  `tools/orbtest` session 1 (3,000 frames): RAM identical at every frame, 798,342 I/O accesses identical,
  6 of 6 snapshots; `tools/s32vtest` replays: 214 of 214 screens exact.
- The Windows build compiles.

### 10.8 Not done / known issues

- As on OutRun's courses (9.9): the wheels flying off and the start-line marker are not drawn; no gear on
  the HUD (Super Hang-On's and Hang-On's have none).
- Super Hang-On's goal run and ending show its own rider and bike (as with the Ferrari).
- The boards' worlds were made for bikes (Super Hang-On's: 279 km/h with its supercharger); at MAD POWER's
  speeds (up to 451 km/h automatic) their scenery and rivals approach by their own rules, checked only by
  the runs of 10.7.
