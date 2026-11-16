# BALANCE: a vehicle on another game's course

Each game's courses are made for its own vehicle: OutRun's for the Ferrari, Super Hang-On's for its
bike, Hang-On's for its bike, OutRunners' for MAD POWER. A vehicle from another game keeps its own
handling, sounds and speedometer; the race setup's BALANCE pulls what it does to the course towards
what the course's own vehicle does, by a factor per aspect:

    factor = full ^ strength

where full is the factor that matches the course's own vehicle (worked out per vehicle and course
from measurements, documented with each table below) and strength is 0 (OFF: the vehicles as they
are), 0.5 (HALF: the geometric midpoint) or 1 (FULL). On a vehicle's own course every factor is 1.

The three aspects, one part each:

- **[Pace](#pace)**: the distance the vehicle covers in the course's world per unit of its own speed
  (`src/main/crossover/balance_pace.cpp`).
- **[Curve push](#curve-push)**: the curve push the vehicle feels from the course's curves
  (`src/main/crossover/balance_push.cpp`).
- **[Contact](#contact)**: the speed it loses to other vehicles
  (`src/main/crossover/balance_contact.cpp`).

The common code (the strength, `blend`, which course is a vehicle's own) is in
`src/main/crossover/balance.hpp` and `balance.cpp`. Each part was written as a document of its own
(`docs/balance-pace.md`, `balance-push.md`, `balance-contact.md` up to release 0.11.0) and keeps its
own section numbers: "section 3" inside a part is that part's section 3; from outside they are cited
as Pace 3, Curve push 3, Contact 3.

## Pace

Pace is the distance a vehicle covers in the course's world per unit of its own speed. Before BALANCE
every vehicle covered the same road distance per km/h on every course, so a course built for a
293 km/h Ferrari was driven at 451 km/h by MAD POWER and at 241 km/h by Super Hang-On's bike. With
BALANCE the course's world takes the visiting vehicle's speed times a pace factor:

    factor = full ^ strength        (OFF 1, HALF the geometric midpoint, FULL full)
    full   = the course's own vehicle's distance per second at its top speed
             / the visiting vehicle's distance per second at its top speed on that course before BALANCE

With FULL every vehicle at its top speed covers the course at the course's own vehicle's pace. The
vehicle keeps its own speed for its physics, engine and tyre sounds, curve push input and speedometer;
only the distance and the speed the world moves by are scaled. The supercharger and MAD POWER's manual
box keep their gain on top (the factor multiplies whatever speed the vehicle reaches).

The code is in `src/main/crossover/balance_pace.cpp` (the table, `pace_now`, `world_speed`,
`own_speed`, `pace_distance`, the test trace) and `balance_pace.hpp`.

### 1. Method

All numbers come from headless harness runs at BALANCE OFF with the trace `CB_PACE_TRACE=<file>`
(below), or from the games' code, cited by address. Configs: copies of the shared `rad.xml` (OutRun
stage 1, track 1; track 0 for the full route), `shor.xml` (track 16, Super Hang-On's Africa), `hoor.xml`
(track 20) and `ofull.xml` with track 21 (OutRunners' full course), with `<vehicle>` and `<balance>` set.
Script: START at frames 150 and 230, then the accelerator held from frame 420 with `steerto 0 road` (the
vehicle steered to the centre of its road), 3,800 to 9,000 frames (60 frames a second). The
supercharger runs hold GEAR1 from frame 420; MAD POWER's manual runs set `or_manual` and press GEAR2 at
frames 560, 700, 880, 1100 and 1400. MAD POWER's runs on OutRun's course use `CB_MP_LEVEL=1` (its
level road) for its level top speed.

From each trace: the top speed is the highest own speed held (within 1 km/h) for 60 frames; the pace
is the mean advance per second over the 2-frame pairs at top speed on a straight (curve type <= 1 on
OutRun, Super Hang-On and Hang-On, whose distance corrections for the line through a curve apply only
on curves; on OutRunners, whose segments are shorter on the inside of a curve, the pairs with the most
common advance per km/h). That is the pace at the centre line of a straight, the same for every
vehicle on a course, so the factor does not depend on the line taken.

Units: OutRun, Super Hang-On and Hang-On count path positions (the integer part of the 16.16 road
position indexes the course's path, in the format the three games share); OutRunners counts segments
(`$8000` on the centre line). The factors are ratios within one course, so they need no common unit;
for comparison, OutRunners' segments are also shown as OutRun positions at 303/272 a segment, which is
the crossover's own convention (the same distance per km/h in both games), not something either game
measures.

### 2. Each game's distance accounting (checked)

| Game | Code | Distance | Checked |
|---|---|---|---|
| OutRun | `OFerrari::update_road_pos` ($6ABA), `CAR_BASE_INC` = `$12F` | 303 x integer km/h per 30 Hz tick, 16.16 positions; x (R ± X/40)/R on a curve | 0.138660 positions per km/h per second at 293.115 km/h = 303 x 293 x 30 / 65536 / 293.115 |
| Super Hang-On | speed task `$610A`, `$62B2-$62F4`, `$85AE` = 151 | 151 x integer km/h per 60 Hz frame; the same curve correction | the bike 33.3170 per second at 241.0-242.0 km/h = 151 x 241 x 60 / 65536 |
| Hang-On | speed task `$8566`, `$86B2-$86F8`, `$10F5C` = 151 | as Super Hang-On (its own constant, the same value) | the bike 38.4320 per second at 278.984 km/h = 151 x 278 x 60 / 65536 |
| OutRunners | `$209074` advance per tick (docs/OUTRUNNERS-RE.md 2.1) | 136 per km/h per 30 Hz tick, `$8000` a segment | 0.124514 segments per km/h per second measured = 136 x 30 / 32768 (0.124512) |

The documented claims hold: Super Hang-On's and Hang-On's 151 a frame is 302 per 30 Hz tick against
OutRun's 303 (0.33% less, "within 0.4%"); OutRunners' 136 per km/h is by construction the distance the
crossover gives OutRun's km/h (one OutRun position = 272/303 of a segment). Two details the claims leave
out: OutRun, Super Hang-On and Hang-On move by the integer part of the speed (`mulu.w`), OutRunners by
the whole value; and the bike's top speed is a saw between 241.0 and 242.0 km/h (its speed task adds
towards 279 and takes the supercharger's step off above 241, `$6236-$62AC`), so its distance uses 241.

Hang-On's bike: its target speed is 279 x throttle / 127 (`$10F54` = `$117`), approached by
(279 - speed) / `$400` x 4 per frame (`$8608-$861A`), which stops at 279 - `$400`/65536 = 278.984 km/h
(measured: 278.984). It moves by 278 km/h and its speedometer shows 278.

### 3. Top speeds and paces before BALANCE (OFF)

| Course | Vehicle | Top speed (km/h) | Pace on a straight at top speed | Run |
|---|---|---|---|---|
| OutRun (positions/s) | **Ferrari** | 293.115 | **40.6398** | f_or |
| | Super Hang-On's bike | 241.0-242.0 | 33.3170 | b_or |
| | the bike, supercharger | 279.6 | 38.5703 | b_or_sc |
| | MAD POWER, automatic | 451.169 (level) | 62.5713 (0.138687 per km/h a second) | m_or_lv |
| | MAD POWER, manual | 470.875 (level) | 65.2592 | m_or_man |
| Super Hang-On (positions/s) | **its bike** | 241.0-242.0 | **33.3170** | b_sh |
| | its bike, supercharger | 279.9 | 38.5703 | b_sh_sc |
| | Ferrari | 293.115 | 40.6398 | f_sh |
| | MAD POWER | 451.169 (level, see below) | 62.5713 (0.13878 per km/h a second here) | m_sh |
| Hang-On (positions/s) | **its bike** | 278.984 | **38.4320** | h_ho |
| | Ferrari | 293.115 | 40.6398 | f_ho |
| | Super Hang-On's bike | 241.0-242.0 | 33.3154 | b_ho |
| | MAD POWER | 451.169 (level) | 62.5713 (0.13878 per km/h a second here) | m_ho |
| OutRunners (segments/s) | **MAD POWER**, automatic | 451.169 (level) | **56.1769** (62.58 OutRun positions) | m_orr |
| | Ferrari | 293.115 | 36.4902 (40.65) | f_orr |
| | Super Hang-On's bike | 241.0-242.0 | 30.0853 (33.51) | b_orr |

MAD POWER never holds its top speed on Super Hang-On's, Hang-On's or OutRunners' courses in these runs
(rival contacts, tight curves, hills; on its own course the highest it held was 442.1 km/h before the
2.5-minute time ran out). Its top speed is a property of its car code: 451.169 km/h on a level road,
measured on OutRun's first stage made level; the car code is the one proven tick for tick against
OutRunners' board, with the same acceleration on its level pre-stage (docs/CROSSOVER-OUTRUNNERS.md 9),
and on Super Hang-On's and Hang-On's level roads (section 10). Its pace on each course is that speed x
the distance per km/h measured on that course: 0.138687 (OutRun), 0.13878 (Super Hang-On, Hang-On; the
same code, 0.07% apart through the trace's pairing) and 0.124514 (OutRunners). Using 442.1 km/h on its
own course instead would make the OutRunners factors 2.0% smaller.

### 4. The factors

| Vehicle on course | full | HALF | full = |
|---|---|---|---|
| Super Hang-On's bike on OutRun's | 1.21979 | 1.10444 | 40.6398 / 33.3170 |
| MAD POWER on OutRun's | 0.64950 | 0.80591 | 40.6398 / 62.5713 |
| Ferrari on Super Hang-On's | 0.81981 | 0.90543 | 33.3170 / 40.6398 |
| MAD POWER on Super Hang-On's | 0.53246 | 0.72970 | 33.3170 / 62.5713 |
| Ferrari on Hang-On's | 0.94567 | 0.97246 | 38.4320 / 40.6398 |
| Super Hang-On's bike on Hang-On's | 1.15358 | 1.07405 | 38.4320 / 33.3154 |
| MAD POWER on Hang-On's | 0.61421 | 0.78372 | 38.4320 / 62.5713 |
| Ferrari on OutRunners' | 1.53951 | 1.24077 | 56.1769 / 36.4902 |
| Super Hang-On's bike on OutRunners' | 1.86725 | 1.36647 | 56.1769 / 30.0853 |

Nine visiting combinations (the brief's list of joins gives 2 + 2 + 3 + 2 on the other games' courses);
every other pair is 1 (the vehicle's own course; Hang-On's bike rides only its own).

What the factors mean in speeds the world moves at, at top speed: on OutRun's courses the bike's world
moves at 294 km/h (FULL) or 267 (HALF), MAD POWER's at 293 or 364; on Super Hang-On's the Ferrari's at
240 or 265, MAD POWER's at 240 or 329; on Hang-On's the Ferrari's at 277 or 285, the bike's at 279 or
259, MAD POWER's at 277 or 354; on OutRunners' the Ferrari's at 451 or 364, the bike's at 451 or 330.

### 5. Where the factor is applied

The vehicle and course in force: each join registers itself for its race (`balance::set_world` in
`SHFerrari::begin`, `HOVehicle::begin`, `ORVehicle::begin`, cleared in their `end`); without one the
course is OutRun's and the vehicle the one OutRun's engine runs (`bike.enabled`, `madpower.enabled`,
else the Ferrari). `pace_now()` is 1 at OFF and on the vehicle's own course, and every use below then
gives exactly what it gave before.

#### Inside the visiting vehicles (their position on the course)
- **The Ferrari** (Super Hang-On's, Hang-On's, OutRunners' courses): `OFerrari::update_road_pos` scales
  its advance (`pace_distance`, the fraction carried). On Super Hang-On's and Hang-On's courses that is
  the distance handed to the board (`owed`); OutRun's road builder for its curve push is then built at
  the board's position.
- **Super Hang-On's bike** (`CrossoverBike::tick`): after each Super Hang-On frame's speed task, the
  distance it moved `$260030` is scaled before it becomes `oroad.road_pos`; the next frame's road table
  (its curve push) is built at that position. On OutRun's courses that is OutRun's position, on Hang-On's
  the distance handed to the board, on OutRunners' the position its builder reads OutRunners' path from
  within the tick.
- **MAD POWER** (`CrossoverMadPower`): one OutRunners segment of its ring is 303/272 OutRun positions x
  the factor, both where the ring is built from the host's path (`build_segments`) and where its advance
  goes back into the host's position, so the car crosses its segments at its own speed while the course
  under it moves by the scaled distance, and the headings it sees are those of the road where it is.

#### OutRun's world (Super Hang-On's bike and MAD POWER on OutRun's courses)
`car_increment` stays the vehicle's own speed (the speedometer `OInitEngine::update_engine`, smoke,
crash, end sequence). The world's motion uses `balance::world_speed(car_increment)`:
- traffic approach `OTraffic::update_props` ($4F0C);
- the road's width change per tick `OInitEngine::update_road` ($B8BC);
- the granular position for road scrolling and scenery zoom `OInitEngine::set_granular_position`;
- the distance score `OFerrari::do_sound_score_slip` / `OStats::convert_speed_score` ($BD78). Its table
  has 21 entries (0-335 km/h); when BALANCE changes the speed it is held to the last one.

Scenery, checkpoints, forks and the goal follow OutRun's road position.

#### Super Hang-On's world (`crossover/shferrari`: the Ferrari, MAD POWER)
The board's speed `$260078` is written by the replaced speed task as the world speed. Read by the
rivals (`$7716`), the roadside objects (`$6F4C`, `$725E`, `$B63C`, their table `$C348`), the score
(`$5C4E`, table `$C308`), the road's stripes (the odometer in the track advance, `$6376`), the 120 km/h
tests (`$7756`) and a lamp output (`$1CE8`). Hooks, only when the factor is not 1:
- `$1566` (`move.w $260078,d0` before x 279/240 into the speedometer `$60530`): d0 = the vehicle's own
  speed, continue at `$156C`;
- `$1588` (`cmpi.w #$F0,$260078` / `blt`, the speedometer's colour above 240): decided on the own speed;
- `$6F58` (the roadside objects' speed held to 279, `$85A6`): held to 349 instead, the end of their
  table (speed / 2, 350 entries), so they keep up with MAD POWER's world speed at HALF (329 km/h).
In the race the own speed is `car_increment`; after it (the goal run) the board's speed / factor.

#### Hang-On's world (`crossover/hovehicle`: the Ferrari, Super Hang-On's bike, MAD POWER)
`$C7E078` is the HUD and how scenery and rivals approach (docs/CROSSOVER-HANGON.md 3). The replaced
speed task writes the world speed there, and the odometer (the rumble-strip phase, `$C7E090`) steps by
it. Read by the roadside objects (`$9ED2`, `$A54C`: table `$9FB0`, speed x 3/5), the rivals (`$AB80`),
the score (sound task `$81F8`, table `$8380`), the 120 km/h tests (`$9B54`, `$ABC6`) and `$8364` (speed
/ 8 to `$20C53C`; no reader found in a race). Hooks, only when the factor is not 1:
- `$11EA` (`move.w $C7E078,d0` before `bsr $5858`, the speedometer `$20C530`): d0 = the own speed,
  continue at `$11F0`;
- `$9EDE`, `$A558` (`move.w (a2,d0.w),d0`, the approach table): at 300 km/h and above, past the table's
  end, d0 = speed x 3/5 (the table's own rule, checked for all 300 entries), continue at `$9EE2` /
  `$A55C` (MAD POWER's world speed at HALF is 354).

#### OutRunners' world (`crossover/orvehicle`: the Ferrari, Super Hang-On's bike)
`$209074` is both the advance per tick and, through its copy `$20042C`, the HUD (/136) and what the other
cars read. At `$A477` the world speed x 136 is written. Hook `$C2EE` (`mov.h R18, F50[R25]`, after
`divu.h #88`): R18 = the own km/h, so `$208F50` (shown) is the vehicle's speed. In the goal run
OutRunners' script drives `$209074`; the vehicle's speed is taken back from it (`own_speed`). The
other cars' pacing and the camera (it pulls back with speed) follow the world speed.

### 6. Verification

Harness runs with the same scripts at HALF and FULL (the pace of each is the measurement of section 1;
MAD POWER's on Super Hang-On's and Hang-On's courses, which never hold top speed, is the measured
advance per own km/h x 451.169):

| Vehicle on course | HALF expected | HALF measured | FULL expected | FULL measured |
|---|---|---|---|---|
| bike on OutRun's | 36.7967 | 36.7966 | 40.6398 | 40.6397 |
| bike + supercharger on OutRun's | 42.5986 | 42.5986 | 47.0477 | 47.0476 |
| MAD POWER on OutRun's (level) | 50.4270 | 50.3852 | 40.6398 | 40.6269 |
| MAD POWER manual on OutRun's (level) | 52.5934 | 52.5842 | 42.3859 | 42.3952 |
| Ferrari on Super Hang-On's | 36.7967 | 36.7953 | 33.3170 | 33.3169 |
| MAD POWER on Super Hang-On's | 45.6584 | 45.6633 | 33.3170 | 33.3202 |
| Ferrari on Hang-On's | 39.5205 | 39.5186 | 38.4320 | 38.4318 |
| bike on Hang-On's | 35.7824 | 35.7832 | 38.4320 | 38.4329 |
| MAD POWER on Hang-On's | 49.0382 | 49.0434 | 38.4320 | 38.4360 |
| Ferrari on OutRunners' (segments) | 45.2759 | 45.2825 | 56.1769 | 56.1803 |
| bike on OutRunners' (segments) | 41.1108 | 41.1101 | 56.1769 | 56.1731 |

Every measured pace is within 0.1% of the expected one (the largest difference: MAD POWER at HALF on
OutRun's, -0.08%; its straight pairs include its own lateral movement).

Stages:
- OutRun's full route (track 0): stage 1, its fork and stage 2 (`ostats.cur_stage` 0 to 1) for the bike
  at FULL (frame 5049), MAD POWER at HALF (4765; the end of stage 2 at 7859) and FULL (5181), and the
  Ferrari at OFF (5119). Stage 1 alone (track 1): the bike at FULL completes it at frame 3627.
- Super Hang-On's Africa: the first checkpoint (the course position back to the start of the next
  section) for the Ferrari at HALF (4802), MAD POWER at HALF (3919) and FULL (4863), and the Ferrari at
  FULL (5188, with `CB_SHF_FREEZE_TIME`: without it the Ferrari crashed in a tight curve and ran out of
  time 122 positions short).
- Hang-On: the first lap section for the Ferrari at HALF (4610) and FULL (5103, timer held), the bike at
  HALF (4338) and FULL (4519), MAD POWER at HALF (4071) and FULL (4567).
- OutRunners' full course: the first checkpoint (stage 0 to 1, `$2080A2`) for the Ferrari at HALF
  (3067) and FULL (2795), the bike at HALF (2485) and FULL (1715, and stage 2 at 4697).

Speedometers with the factor in force (snapshots and the trace): Super Hang-On's shows 340 for the
Ferrari at 293 km/h at FULL (its x 279/240) while `$260078` holds 240; Hang-On's shows 293 for the
Ferrari at FULL and 421 for MAD POWER at HALF; OutRunners' `$208F50` equals the Ferrari's own km/h on every
frame of a FULL run (1,753 frames moving) while `$209074` / 136 is 1.54 times it.

Identity: the WAV and three snapshots of a 50-second run (frames 1-3,000; the full route 3,500) are
byte-identical to commit 855d672's build, run from the same working directory with no rankings files:
- BALANCE OFF, every visitor: the Ferrari, the bike and MAD POWER on OutRun's stage 1 and full route,
  the Ferrari and MAD POWER on Super Hang-On's course, the Ferrari, the bike and MAD POWER on Hang-On's,
  the Ferrari and the bike on OutRunners';
- every vehicle on its own course at OFF, HALF and FULL: the Ferrari (stage 1 and the full route),
  Super Hang-On's bike, Hang-On's bike, MAD POWER on OutRunners' full course.
The same comparison for the bike on OutRun's and the Ferrari on Super Hang-On's at HALF differs, as it
should.

### 7. Notes and limits

- **Curve push.** Pace changes how long a vehicle spends in a curve per unit of its own speed. The
  Ferrari's and the bikes' push per tick comes from their own speed and the road's curvature at their
  position, so their total drift through a given curve scales by 1/factor (the Ferrari at FULL on
  Super Hang-On's Africa, 1.22 times the time in each curve, slid off a radius-56 curve the harness
  steering took at OFF). MAD POWER's comes from the heading change it crosses per tick, which scales
  with the factor, so its drift through a curve stays as it was. The push factors should be measured
  with pace in force.
- **Handling against the world.** At FULL on OutRunners' course the Ferrari meets the course at 451
  km/h's pace with its 293 km/h steering; the harness's simple steering crashed it twice in 36 s.
- **Found on the way (unchanged at OFF):** MAD POWER above 299 km/h on Hang-On's course reads past the
  objects' approach table (`$9FB0`, garbage values); above 279 on Super Hang-On's course its roadside
  objects are held to 279's approach and `$725E`/`$B63C` read past their 350-entry table above 349; on
  OutRun's full route above 335 km/h `OStats::convert_speed_score` reads past its 21 entries. BALANCE OFF
  keeps all of these as they were; at HALF and FULL the world speeds stay in range or are handled as
  above.
- The 120 km/h tests of Super Hang-On (`$7756`) and Hang-On (`$9B54`, `$ABC6`) see the world speed.

### 8. Test switches

- `CB_PACE_TRACE=<file>`: per rendered frame `frame host pos world_kmh own_kmh ...` (host 0 OutRun,
  1 Super Hang-On, 2 Hang-On, 3 OutRunners); then X, curve type and radius (and OutRun's stage) for
  hosts 0-2, the hill frame index `$2004D9`, the stage `$2080A2` and the shown km/h `$208F50` for 3.
- Harness `steerto <x> road`: steer relative to the road the vehicle is on, for the Ferrari too (OutRun's
  two roads), and for Super Hang-On's and Hang-On's own bikes and OutRunners' own car on their boards.

## Curve push

How the race setup's BALANCE scales the curve push a vehicle feels on another game's course, how the
factors were measured, and what was checked. The factor table is in `src/main/crossover/balance_push.cpp`.

Target (decided with the user): each vehicle feels a host's curves as hard as it feels its own game's
curves. FULL makes its demand on the host its demand on its own courses; HALF the geometric midpoint; OFF
leaves it as it was. This is measured with BALANCE's pace (the part Pace above) in force.

### 1. The push and the steering of each vehicle

Every vehicle keeps its own curve push, computed by its own game's code from a road built over the
host's path. What each one does per tick of its own code, in its own lateral units:

| Vehicle | Push (per tick) | Steering at full lock, top speed, on a straight | Tick | Top speed |
|---|---|---|---|---|
| OutRun's Ferrari | `OFerrari::set_curve_adjust` ($BF6E): ((road_x[170] - road_x[511]) >> 6) x speed / 220 x 2 | `OFerrari::set_ferrari_x` ($C1B2): steering_adjust 127, slewed 64 a tick, (s >> 3) + (s >> 5) = **18** | 30 Hz | 293 |
| Super Hang-On's bike | `$63CE` (`sho_centrifugal_task`): (T[$A6] - T[$1B8]) / 64 x speed / 110, plus 1 in the quotient's direction; with the supercharger T[$8C] | `$7EB2`: 127, slewed 12 a frame, << 4 / 128 = **15** | 60 Hz | 241 (279 with the supercharger) |
| Hang-On's bike | `$879A`: (T[$4C] - T[$DE], table at $C7E800) / 64 x speed / 220 ($20CA9E), plus 1 in the quotient's direction | `$B4F0`: as Super Hang-On's ($10F6C = 128), **15** | 60 Hz | 278 |
| MAD POWER | `$A4D5-$A50E`: the change of the road's heading at the car x 2, / (1 + catch-up / 256) | `$A487-$A4D0`: 32 at full lock above speed $A000; at its top speed its slide (`$A534-$A547`) takes 6 back: **26** | 30 Hz | 451 km/h automatic (61336) |

Hang-On's centrifugal and steering code was read with Musashi's disassembler from the `hangon` main ROM
(the other three are the transcriptions already in the program: Cannonball's `oferrari.cpp`,
`shophys/shophys_lat.c`, `orcar/orcar.cpp`).

The steering values were measured on a straight at top speed with the wheel at full lock, logged per tick:

- Ferrari, OutRun stage 1 at 293: X moved 17 or 18 a tick (18 from the steering; that straight's road
  narrows by about 1 every other tick, which OutRun's road-width carry adds), after one tick of slew.
- Super Hang-On's bike: 15 a frame after 10 frames of slew, on OutRun's stage 1 (241) and on its own
  board (Africa, 241); Hang-On's bike on its own board: 15 a frame (278).
- MAD POWER at 61,744-61,914: lateral velocity 26 a tick from the first tick (steering 32, slide -6). Its
  slide at full lock is -6 at every logged speed from 59,500 to 62,500, so 26 at 61,336.

The Ferrari also steers less into a curve above 147 km/h (`set_ferrari_x`, the road_curve term); the
demand below uses the straight-line steering, as defined, so that reduction is not in it.

### 2. Demand

demand = the push at the vehicle's top speed (without the supercharger; MAD POWER automatic) / its
steering's full-lock lateral speed, both per tick of its own code: the fraction of its steering a curve
takes to hold a line. Above 1 the vehicle cannot hold its line at top speed with full lock.

**D**, a vehicle's demand on a course: the demand averaged along the course's curves. The demand per tick
is averaged in bins of 8 positions (OutRun, Super Hang-On, Hang-On: path positions; OutRunners: segments);
the curve bins are those whose mean |demand| is 0.03 or more; D is the mean over them, i.e. the demand
summed over the curves divided by their length (the summed-over-curves method of the earlier host-vehicle
target, divided by the curves' length so that different courses compare).

#### Instrumentation

`CB_PUSH_LOG=<file>` writes one line per tick of each vehicle's push (`crossover/balance_push.cpp`,
`bike.cpp`, `madpower.cpp`): the frame, the course, the position on it (OutRun's road position; Super
Hang-On's `$260030`, Hang-On's `$C7E030`; OutRunners' segment count `$209042`), the speed, the lateral
position, the curve value the code reads (the road table difference; MAD POWER's heading term), the push
its code gave, the push applied after BALANCE and the steering (for the Ferrari also OutRun's curve type
and radius and its wheels). A course's own vehicle on its emulated board is read from the board's RAM
after each frame and its push computed as its code computes it. `CB_PUSH_HOLD=1` holds a board's own
vehicle at the road's centre and its timer, so that it covers its course at top speed (its push depends
on where it is along the course and its speed, not on its lateral position); `=2` holds the timer only.
Steering: the harness's `steerto 0 road` (from the pace work).

For the Ferrari and the bikes the push at top speed is computed from the logged curve value with the
code's own arithmetic at the top speed (the curve value depends only on the position), so every tick
counts whatever the vehicle's speed. MAD POWER's push is its heading change per tick, proportional to its
advance: its logged push x 61,336 / its speed, over the ticks it races (state 2) at half its top speed
or more.

#### Runs (BALANCE OFF, accelerator held)

- OutRun's 15 single stages (tracks 1-15, laps folded together), 7,000 frames each, no traffic, infinite
  time: the Ferrari, Super Hang-On's bike with and without the supercharger, MAD POWER.
- Super Hang-On's four courses (16-19), 24,000 frames: its bike (held; also with the supercharger), the
  Ferrari and MAD POWER (time held, `CB_SHF_FREEZE_TIME`).
- Hang-On's course (20): its bike (held), the Ferrari, MAD POWER, Super Hang-On's bike.
- OutRunners' Kenya (22) and full course (21): MAD POWER (Kenya held and steered, the full course held),
  the Ferrari, Super Hang-On's bike (time held).

### 3. Demand on the own courses and on the hosts

| Vehicle on course | D | curve bins | curves | per curve p10 / p50 / p90 | course by course |
|---|---|---|---|---|---|
| **Ferrari, own (OutRun's 15 stages)** | **0.792** | 2,479 | 126 | 0.51 / 0.89 / 1.34 | 0.53 - 1.03 (stages) |
| **Super Hang-On's bike, own (its four courses)** | **0.713** | 3,329 | 216 | 0.47 / 0.72 / 1.15 | 0.704 - 0.724 |
| **MAD POWER, own (OutRunners' Kenya + full course)** | **1.029** | 929 | 84 | 0.53 / 1.10 / 1.40 | Kenya 1.048, full 1.023 |
| Super Hang-On's bike on OutRun | 0.721 | 2,175 | 129 | 0.46 / 0.81 / 1.05 | 0.49 - 0.86 (stages) |
| MAD POWER on OutRun | 1.271 | 2,047 | 132 | 0.86 / 1.43 / 1.81 | 0.83 - 1.55 (stages) |
| Ferrari on Super Hang-On | 0.846 | 3,012 | 181 | 0.52 / 0.92 / 1.39 | 0.81 - 0.92 |
| MAD POWER on Super Hang-On | 1.296 | 2,874 | 278 | 0.87 / 1.37 / 2.03 | 1.22 - 1.37 |
| Ferrari on Hang-On | 0.935 | 872 | 38 | 0.57 / 1.07 / 1.39 | |
| MAD POWER on Hang-On | 1.244 | 851 | 55 | 0.82 / 1.36 / 1.92 | |
| Super Hang-On's bike on Hang-On | 0.718 | 948 | 39 | 0.52 / 0.78 / 1.10 | |
| Ferrari on OutRunners | 0.594 | 881 | 44 | 0.22 / 0.56 / 1.27 | Kenya 0.693, full 0.571 |
| Super Hang-On's bike on OutRunners | 0.562 | 933 | 62 | 0.32 / 0.58 / 0.70 | Kenya 0.560, full 0.563 |

MAD POWER's own data: 4,936 ticks at half its top speed or more in 929 curve bins (Kenya 1,321 ticks,
225 bins, 21 curves; the full course 3,615 ticks, 704 bins, 63 curves). It loses speed often on its own
courses (rivals and Kenya's animals at the centre line, where it was held), so this is the thinnest of the
three; Kenya and the full course agree within 2.5%. The full course's way through the map differs between
runs, which does not matter here (no curve-by-curve matching is needed).

### 4. Full factors

r = D_own(V) / D_host(V, H). For the Ferrari and the bikes the full push factor is r. For MAD POWER it is
r / pace(MAD POWER, H) (section 5).

| Guest on course | r | full push factor | r from the courses' extremes (own course or stage / host course or stage) |
|---|---|---|---|
| Super Hang-On's bike on OutRun | **0.990** | 0.990 | 0.82 - 1.47 |
| MAD POWER on OutRun | **0.810** | 0.810 / 0.64950 = 1.247 | 0.66 - 1.27 |
| Ferrari on Super Hang-On | **0.936** | 0.936 | 0.57 - 1.28 |
| MAD POWER on Super Hang-On | **0.795** | 0.795 / 0.53246 = 1.493 | 0.75 - 0.86 |
| Ferrari on Hang-On | **0.847** | 0.847 | 0.57 - 1.10 |
| MAD POWER on Hang-On | **0.827** | 0.827 / 0.61421 = 1.346 | 0.82 - 0.84 |
| Super Hang-On's bike on Hang-On | **0.993** | 0.993 | 0.98 - 1.01 |
| Ferrari on OutRunners | **1.334** | 1.334 | 0.76 - 1.80 (Kenya 1.14, full course 1.39) |
| Super Hang-On's bike on OutRunners | **1.268** | 1.268 | 1.25 - 1.29 |

The spread of the extremes is mostly the Ferrari's own stages (0.53 to 1.03 from OutRun's gentlest to its
twistiest stage) and OutRun's stages for the guests. One factor per pair: each course is taken whole.

What it means: Super Hang-On's bike meets OutRun's and Hang-On's curves, on average, as hard as its own
(r 0.99), so the push barely changes; OutRunners' long gentle arcs push it and the Ferrari less than their
own courses do (r 1.27 and 1.33); MAD POWER meets every other game's curves harder than its own (r 0.80-0.83),
and the Ferrari Super Hang-On's and Hang-On's (0.94, 0.85).

### 5. With pace

BALANCE's pace scales the distance the host's world covers per unit of the vehicle's own speed.

- **The Ferrari and the bikes**: their push per tick comes from their own speed and the road table at
  their position, so pace does not change their per-tick push at a given point of a curve. Measured
  (the push their own code gave at HALF and FULL, before the push factor, on the OFF run's curve bins,
  over OFF): 0.995-1.004 in every pair. Pace changes the time they spend in each curve (by 1 / pace).
- **MAD POWER**: its push is the heading change it crosses per tick. With pace, one of its segments
  covers pace x 303/272 positions of the host's road, so it crosses pace times the heading per tick.
  Measured, its own push over OFF on the same bins: FULL 0.659, 0.540, 0.621 on OutRun's, Super
  Hang-On's and Hang-On's courses (pace 0.650, 0.532, 0.614), HALF 0.813, 0.734, 0.787 (pace^0.5 0.806,
  0.730, 0.784). Its demand with pace at strength s is D_off x pace^s; the push factor r / pace gives
  blend(r / pace) x pace^s = r^s at every strength, HALF included.

#### The Ferrari at FULL on Africa's radius-56 curve

The curve (Africa's first section, positions 1,437-1,525, Super Hang-On radius 56): the Ferrari's demand
there is 1.16 a tick on average (1.44 at most) at OFF, HALF and FULL alike: above 1, so at top speed it
cannot hold its line with full lock at any setting, and the distance it slides out grows with the time it
spends in the curve. At OFF it already left the road there (64 ticks with wheels off, down to 139 km/h, X
to 376); at FULL pace (0.82) the curve takes 1.22 times as long, which is what made it slide off at FULL.
With the push factor (0.936 at FULL) its applied demand there is 1.09: still above 1. In these runs: OFF 96
ticks in the curve, wheels off 64; FULL 115 ticks, wheels off 49, X 74-329; at HALF the harness's steering
crashed it there. Per-tick demand, as defined, does not see the time in a curve; where the demand is above
1 the slide grows with it.

### 6. Where the push is scaled

The push each vehicle's code gives is multiplied by `balance::blend(full)` after its code computes it,
keeping the fraction rounded away for the next tick (`balance::push_scaled`), so the average is exact and
the shape (where in the curve it pushes, how it builds and fades) is the vehicle's own:

- **Ferrari** (OutRun's courses and Super Hang-On's, Hang-On's, OutRunners'):
  `balance::ferrari_curve_adjust` calls `OFerrari::set_curve_adjust` and scales the change it made to the
  car's X. Its x_diff is local to that function; the road_x it reads (the road builder's output) and
  road_curve (which `set_ferrari_x` and the distance correction use) are not touched.
- **Super Hang-On's bike** (`bike.cpp`): the change its centrifugal task made to X is scaled, with the
  task's own rule kept (no push when it would take X past 500). The road table it reads is also the
  speed task's (its distance correction) and is left alone.
- **MAD POWER** (`orcar.cpp`, `$A50E`, through `ORCar::push_fn` set by `madpower.cpp`): the push term
  before the steering and slide are added to it. The heading it comes from also places the car in the
  world (`$A7CC`) and is left alone.

Scaling the output rather than the curve value it is fed keeps each code's own thresholds as they are
(the Ferrari's >> 6, the bikes' / 64 and their +1 step). Nothing else of the vehicle changes: speed,
acceleration, steering, sprites, sounds.

### 7. Checks

#### Demand after the change (pace and push at the same strength)

The guest's runs at HALF and FULL; its demand from the push it was given (the log's applied push), on
the OFF run's curve bins, over its OFF demand there. Target: r^s. (The Ferrari and the bikes: ticks at
97% of top speed or more, and a bike within 50 of its ±500 limit, where its task gives no push, left out.)

| Guest on course | FULL measured | FULL target | HALF measured | HALF target |
|---|---|---|---|---|
| Super Hang-On's bike on OutRun | 0.990 | 0.990 | 0.997 | 0.995 |
| MAD POWER on OutRun | 0.821 | 0.810 | 0.907 | 0.900 |
| Ferrari on Super Hang-On | 0.933 | 0.936 | 0.966 | 0.967 |
| MAD POWER on Super Hang-On | 0.807 | 0.795 | 0.896 | 0.891 |
| Ferrari on Hang-On | 0.843 | 0.847 | 0.908 | 0.920 |
| MAD POWER on Hang-On | 0.836 | 0.827 | 0.914 | 0.910 |
| Super Hang-On's bike on Hang-On | 0.994 | 0.993 | 0.998 | 0.997 |
| Ferrari on OutRunners | 1.328 | 1.334 | 1.153 | 1.155 |
| Super Hang-On's bike on OutRunners | 1.274 | 1.268 | 1.127 | 1.126 |

Every pair is within 1.5% of its target at FULL and HALF (stated tolerance 2%). MAD POWER lands 1-1.5% above
its target at FULL because its push grows slightly more than the pace (0.659 against 0.650 on OutRun's). HALF
runs: OutRun's stages 1-5, Africa, Hang-On's course, Kenya. Bins compared: 17 (the Ferrari on OutRunners at
HALF, where the harness's steering crashed it) to 2,311.

#### The user's case

Super Hang-On's bike with the supercharger held, on OutRun's stage 1 (7,000 frames), against the same bike on
its own four courses; D at 279 km/h, and the share of the curves' length where the demand is above 1 (where it
cannot hold its line with full lock):

| | D | curve length above 1 | above 1.2 |
|---|---|---|---|
| own courses, supercharger | 1.079 | 48.8% | 30.1% |
| own courses, no supercharger | 0.713 | 21.7% | 9.3% |
| OutRun's 15 stages, supercharger, OFF | 0.968 | 58.7% | 19.2% |
| OutRun stage 1, supercharger, OFF | 0.985 | 55.0% | 14.2% |
| the same, HALF (applied/given push 0.9950) | 0.980 | | |
| the same, FULL (applied/given push 0.9900) | 0.975 | | |

Where the bike rode on stage 1 (from frame 1,500): mean distance from its road's centre 187 (OFF), 168 (HALF),
161 (FULL); past 300, Super Hang-On's own road edge, 31.2%, 28.0% and 24.9% of the frames. The push per tick
barely changes (r 0.99); what does change is the pace: the bike covers OutRun's curves 1.22 times faster at FULL,
so it spends less time sliding out in each.

So, measured this way, the bike is not pushed harder on OutRun's courses than on its own on average (0.97 against
1.08 with the supercharger, 0.72 against 0.71 without). OutRun's curves are more uniform: more of their length
is just above 1 with the supercharger (58.7% against 48.8%), while Super Hang-On's have more gentle stretches and
more very sharp ones (above 1.2: 30.1% against 19.2%). An earlier report of this work gave per-tick medians (0.93
on OutRun against 0.60 on its own courses without the supercharger); those counted ticks, which crowd where the
bike slows down off the road in OutRun's sharp curves, so they overstated OutRun's share. Per length of curve, as
here, the averages are equal.

#### OFF and the vehicles' own courses

Thirteen runs of 3,000 frames, WAV and two snapshots (frames 2,400 and 3,000), each in its own working directory
with the same ranking files, built from commit c3e54ba and from this branch: the Ferrari, the bike with the
supercharger and MAD POWER on OutRun's stage 1; the Ferrari, MAD POWER and Super Hang-On's bike on Africa; the
Ferrari, MAD POWER, Super Hang-On's bike and Hang-On's bike on Hang-On's course; the Ferrari, Super Hang-On's
bike and MAD POWER on Kenya.

- BALANCE OFF: all 39 files byte-identical to c3e54ba's.
- Each vehicle on its own course (the Ferrari on stage 1, the bike on Africa, Hang-On's bike, MAD POWER on
  Kenya) at HALF and at FULL: byte-identical to c3e54ba's at OFF.
- Sensitivity: four guests at FULL (the bike with the supercharger and MAD POWER on stage 1, the Ferrari on
  Africa, the bike on Kenya) differ in all their files from c3e54ba's FULL (pace alone), as they should.

### 8. Earlier target (superseded)

The first version matched each visitor to the host's own vehicle (full = D_host vehicle / D_visitor on the same
curves): 1.253 for the bike on OutRun, 0.706 MAD POWER on OutRun, 0.759 / 0.557 Ferrari / MAD POWER on Super
Hang-On, 0.777 / 0.576 / 1.018 on Hang-On, 1.363 / 1.900 on OutRunners. It raised the bike's push on OutRun's
courses, because OutRun's courses push its Ferrari harder relative to its steering than they push the bike; the
user chose the own-game target above instead.

## Contact

What a contact with another vehicle costs each game's own vehicle on its own course, what it costs each
visiting vehicle, and the rule BALANCE uses to bring the visiting vehicle in line (`balance::contact`,
`src/main/crossover/balance_contact.cpp`). Scenery crashes are not part of this aspect.

### 1. Each game's response, in the code

| Vehicle | Where | What a contact does to the speed |
|---|---|---|
| OutRun's Ferrari | `OTraffic::check_collision` (OutRun `$50DE`, `src/main/engine/otraffic.cpp`) | Every tick of an overlap the speed is set to the other car's speed − 80 (not below 0); a skid away from it (`SKID_RESET` 20, within 30). If the skid is still over 6 at `OInitEngine::init_crash_bonus` (`$984E`), a spin starts when `random & mask == mask`: mask 1 for cars, 3 for lorries (the traffic's property table), so 1 in 2 or 1 in 4. The spin is OutRun's crash code (`OCrash`), which slows the car on its own and often stops it. |
| Super Hang-On's bike | `$7510` (knock ±14 a frame of contact, ROM `$85B8`, kept within ±40, `$85BA`); `sub_07E8A` (knock counts back to 0 by 1 a frame, pushing X 16 a frame, `$85BC`); the speed task `$6178` (no throttle while the knock is not 0), `$61D4` (−2 × brake above the target) and `$61E6` (3 more brake steps), brake `$85B2` = `$C666` (0.775 km/h a frame) | While the knock runs the speed falls 5 × 0.775 = 3.875 km/h a frame, but only from 80 km/h (`$61B8`, `$85AA`); below that the task accelerates. One frame of contact gives a knock of 13–14 frames, two 26–27, three or more 39–40. `crossover/shophys/shophys.c`, `shophys_lat.c` |
| Hang-On's bike | `$A928`–`$A954` (knock ±`[$10F66]` = 14 within `[$10F68]` = 40, the same code as Super Hang-On's); speed task `$85DA` (no throttle while the knock is not 0), `$8644` (−2 × brake), `$8656` → `$8698` (3 brake steps); `[$10F60]` = `$C666`, `[$10F58]` = 80 | The same speed rule as Super Hang-On's (disassembled for this from `epr-6918a`, `6916a`, `6917a`, `6915a`). |
| OutRunners' MAD POWER | `$8029` (an object with its own position: Kenya's elephants), `$8074` (a car of the race), then state 5 `$819D` (`docs/OUTRUNNERS-RE.md` 2.7, 4.3; `crossover/orcar/orcar.cpp` `ORCar::collision`, `crash_tick`) | A speed change `$2004C4` from the speed difference (a rear hit counts double) × the response table × the weights, decayed each tick of state 5 (24–40 ticks in the runs) with the accelerator held at 0. In a one-player race the only other vehicles are Kenya's elephants (`docs/OUTRUNNERS-RE.md` "Traffic"). |

How the visiting vehicles get their own response (unchanged by this work, only scaled):

| Visiting vehicle, course | Join | Response |
|---|---|---|
| Super Hang-On's bike, OutRun's | `otraffic.cpp` (bike branch) → `CrossoverBike::traffic_hit` | its knockback; OutRun tests once a 30 Hz tick, so a tick of contact gives 14 on both Super Hang-On frames of it |
| MAD POWER, OutRun's | `otraffic.cpp` → `CrossoverMadPower::traffic_hit` | `$8074`, the traffic as a car of its race, of its weight |
| Ferrari, Super Hang-On's / Hang-On's | `shferrari.cpp` / `hovehicle.cpp`, the rivals' hits | OutRun's cut to the rival's speed − 80, skid, spin chance of a car |
| MAD POWER, Super Hang-On's / Hang-On's | `CrossoverMadPower::rival_hit` | `$8074`, the rival as a car of its race |
| Super Hang-On's bike, Hang-On's | `hovehicle.cpp` → `traffic_hit` | its knockback, once an OutRun tick |
| Ferrari, OutRunners' | `orvehicle.cpp` `ferrari_tick` | OutRun's cut to the elephant's (or car's) speed − 80 |
| Super Hang-On's bike, OutRunners' | `orvehicle.cpp` `bike_tick` → `traffic_hit` | its knockback, once an OutRunners tick |

There are nine visiting pairs (OutRun's courses: the bike, MAD POWER; Super Hang-On's: the Ferrari, MAD
POWER; Hang-On's: the Ferrari, MAD POWER, the bike; OutRunners': the Ferrari, the bike). Hang-On's bike
only rides its own course.

### 2. Method

**Trace.** `CB_CONTACT_LOG=<file>` writes the vehicle's speed every frame (its own km/h: OutRun's
`car_increment`, Super Hang-On's `$260078`, Hang-On's `$C7E078`, OutRunners' `$209074` / 136) with the
knock, MAD POWER's state, the Ferrari's spin and crash state, and a line for each contact with the other
vehicle's speed and the factor applied (the format is in `balance_contact.cpp`).

**Contacts on demand** (testing switches, harness and `balance_contact.cpp`):
- `CB_STEER_REL` (with `CB_STEER_P`, `CB_STEER_D`): `steerto` relative to the centre of the vehicle's road,
  damped; `CB_CHASE` steers at the nearest of OutRun's traffic ahead. Used on OutRun's full route, where
  the traffic is OutRun's own.
- `CB_RAILS=<X>` on Super Hang-On's and Hang-On's courses: the vehicle is put back at X as soon as no
  response runs (the knockback's push, the skid and the bounce happen as they are), and each rival coming up
  to the contact window has its lateral position `$34` set in the vehicle's path at the next offset of
  `CB_RAILS_OFFS` (0, 8, 16, 24, 32, 40: across the hit span), so that the board's own test and the
  vehicle's own response decide the contact. On OutRunners' Kenya the vehicle is held at X and the
  elephants run their own way (five runs at X = −90, −45, 0, 45, 90). The timer is held.

**Measures** (`ana3.py`, `verify.py` in the scratchpad's `bc/run`): for each contact the speed just
before (v0) and when the response is over (v1: the knock back to 0, state 5 over, the cut, a spin over).
Speed lost = (v0 − v1) / v0, and / top speed. Time back = the response, then the vehicle's own
acceleration from v1 to v0 (read off its runs with the accelerator held on a level road: the 80th
percentile per 4 km/h). Time lost against not having hit = the distance short over the response and the
recovery, over the top speed. Clean contacts only: at ≥ 85 % of top speed (MAD POWER ≥ 60 %: it seldom
gets nearer between contacts), no second contact before v1, not knocked into the scenery.

Top speeds (accelerator held, level road): Ferrari 293.1, Super Hang-On's bike 242.0, Hang-On's bike
276.6, MAD POWER 451.2 (automatic box).

### 3. What a contact costs the course's own vehicle

| Vehicle, course, contact | n | v0 | Speed lost, % of v0 | % of top | Time back | Time lost |
|---|---|---|---|---|---|---|
| Ferrari, OutRun's full route, traffic (no spin) | 14 | 279 | 56.1 (45–66) | 53.4 | 6.2 s | 1.35 s |
| the same, the contacts that spun (6 of 20) | 6 | 275 | 81.1 (4 to a stop) | 76.0 | 10.7 s | 5.1 s (stopped: 6.7–7.2 s) |
| Super Hang-On's bike, its course, rivals | 10 | 239 | 60.1 (42–66) | 59.3 | 7.2 s | 1.74 s |
| Hang-On's bike, its course, rivals | 4 | 267 | 58.0 (56–60) | 55.9 | 12.6 s | 2.49 s |
| MAD POWER, Kenya, elephants (≥ 60 % of top) | 22 | 363 | 77.7 (52–100) | 63.3 | 11.4 s | 4.65 s |
| the same, ≥ 85 % of top | 7 | 414 | 89.6 (75–100) | 82.0 | 18.8 s | 6.38 s |

- Ferrari: v1 is the other's speed − 80 to the km/h in every contact (traffic at 166–228 km/h, OutRun's
  cruise 200 + a random −32..31, `OTraffic::spawn_car` `$4BAC`).
- Bikes: 39 knock frames lose 154.6 km/h (241 → 87 on Super Hang-On's course; 276 → 121 on Hang-On's),
  26 frames 100.6. Contacts across the hit span gave 39 frames mostly and 26 at its edge; the mean on
  Super Hang-On's course is 145 km/h, on Hang-On's 155. The knock leaves the bike above the speed task's
  80 km/h floor.
- MAD POWER: against the elephants (152–227 km/h) it keeps about 1.385 × the elephant's speed − 0.52 × its
  own (least squares over 24 contacts at 221–451 km/h, rms 9.7 km/h, 3.6 points of speed lost): at its top
  speed an elephant takes almost all of it.

### 4. What a contact costs a visiting vehicle (BALANCE OFF)

| Visiting vehicle, course | n | v0 | Other | Speed lost, % of v0 | Time back | Time lost |
|---|---|---|---|---|---|---|
| Super Hang-On's bike, OutRun's | 6 | 240 | 180 | 62.9 | 7.5 s | 1.87 s |
| MAD POWER, OutRun's | 31 | 341 | 188 | 46.6 | 6.3 s | 2.27 s |
| Ferrari, Super Hang-On's | 7 | 276 | 158 | 70.9 | 7.0 s | 2.02 s |
| MAD POWER, Super Hang-On's | 48 | 323 | 169 | 51.2 | 5.7 s | 2.40 s |
| Ferrari, Hang-On's | 9 | 280 | 194 | 52.2 | 6.3 s | 1.28 s |
| MAD POWER, Hang-On's | 59 | 316 | 197 | 41.9 | 4.8 s | 1.88 s |
| Super Hang-On's bike, Hang-On's | 12 | 238 | 182 | 65.0 | 7.0 s | 1.82 s |
| Ferrari, OutRunners' (Kenya) | 8 | 284 | 207 | 54.6 | 6.9 s | 1.34 s |
| Super Hang-On's bike, OutRunners' (Kenya) | 9 | 241 | 196 | 65.1 | 7.6 s | 1.87 s |

(The Ferrari's contacts that spun are left out here as in section 3: on Super Hang-On's and Hang-On's
courses about half of its contacts spun, the chance of OutRun's cars. MAD POWER's rows include runs where
only every third rival was put in its path, so that it met some near its top speed.)

What each visiting vehicle's own response keeps, as measured (these are the models the rule uses):
- the Ferrari: the other's speed − 80, exact (it is the value its code sets; when it meets two rivals
  within a contact, the last one's);
- Super Hang-On's bike: v0 − 157 km/h on OutRun's, Hang-On's and OutRunners' courses (a tick of contact
  is two frames of 14, so 40 frames of knock: 241 → 84); a contact at the edge gives a shorter knock;
- MAD POWER against a car of its weight: 0.711 × the other's speed + 0.121 × its own + 4.5 km/h (least
  squares over its 362 contacts at OFF on OutRun's, Super Hang-On's and Hang-On's courses, met at
  120–450 km/h; rms 12 km/h, 4.3 points of speed lost). An earlier model, the other's speed less a constant
  per course, held at the speeds met on the first runs (about 300–340 km/h) but not near its top speed,
  which it reaches more often with pace in place.

### 5. The rule

**Which quantity.** The fraction of speed lost in the contact, not the time lost. The contact rule is what
decides the speed lost; the time to get it back is the vehicle's own acceleration, which BALANCE keeps as
its feel (and pace, another aspect, already maps its speed onto the course). Matching time would mean
inflating or shrinking a vehicle's losses to make up for how it accelerates. With the same fraction of
speed lost, a contact costs each vehicle the same share of its pace; the time lost then differs only by
how each vehicle accelerates (tables in sections 3, 4 and 7).

**One factor per pair does not fit.** OutRun's rule depends on the other vehicle's speed (its traffic runs
at 166–231 km/h, so the Ferrari loses 48–70 % at top speed), the bikes' does not (a fixed 145–157 km/h),
MAD POWER's depends on both speeds, and so does the Ferrari's as a visitor. A single factor for the bike
on OutRun's course would miss OutRun's own loss by up to 11 points either way.

**The rule.** With FULL, a visiting vehicle keeps the fraction of its speed that the course's own vehicle
would keep in the same contact: the same other vehicle, met at the same fraction of the vehicle's top
speed. Each game's rule is written as the speed it keeps (`kept_host`, `kept_own` in
`balance_contact.cpp`):

| Course's own vehicle | Speed kept at v (km/h) against another at u | Source |
|---|---|---|
| OutRun's Ferrari | max(0, u − 80) | `$50DE`, exact |
| Super Hang-On's bike | max(78, v − 145) | knock code, measured mean |
| Hang-On's bike | max(78, v − 155) | the same |
| MAD POWER (elephants) | max(0, 1.385 u − 0.52 v) | fitted, section 3 |

| Visiting vehicle's own response | Speed kept | |
|---|---|---|
| Ferrari | its cut, as computed | exact |
| Super Hang-On's bike | max(0, v − 157) (145 on its own course) | measured |
| MAD POWER (a car of its weight) | 0.711 u + 0.121 v + 4.5 | fitted, section 4 |

For a contact at v0 (km/h) with another at u:

    p          = v0 / top(visitor)
    lost_host  = 1 − kept_host(p × top(host's vehicle), u) / (p × top(host's vehicle))
    lost_own   = 1 − kept_own(v0, u) / v0
    full       = lost_host / lost_own            (1 when lost_own < 2 % or lost_host ≤ 0; kept within 0.2–5)
    applied    = full ^ strength                  (balance::blend: OFF 1, HALF its square root, FULL full)

The visiting vehicle's own response keeps its shape and only the speed it takes off is multiplied by the
applied factor (`contact_step`: speed after = before − (before − own after) × applied, tick by tick through
the response):
- the Ferrari: its cut (`contact_cut`); the same speed through every tick of the overlap, as OutRun sets
  the same speed each tick (if another vehicle is met within the contact, its cut is scaled from the speed
  the contact started at, as OutRun's last cut wins). The skid, the spin chance and the spin itself are untouched (a spin is
  OutRun's crash code and slows the car its own way);
- the bike: each frame of its knockback (`bike.cpp`, around the speed task); scaled, the knockback brakes
  at its own rate for all its frames, also below the speed task's 80 km/h floor (otherwise a factor above
  1 could not take more than the floor allows). A knock of 26 or 28 frames (a contact at the edge) still
  loses less than one of 40, as in its own game;
- MAD POWER: each tick of state 5 after a car's contact (`madpower.cpp`); scenery bounces are untouched.

The lateral push, what counts as a contact, the crashes, the spin chance and the sounds are unchanged. On a
vehicle's own course, and with OFF, the factor is 1 and every response returns exactly its own value.

**Full factors for a typical contact at top speed** (`balance::contact(v, h)`: the other at OutRun's
traffic cruise 200, Super Hang-On's rivals 136, Hang-On's 190, Kenya's elephants 203, the means met in
the runs). The factor applied in play is the per-contact one above.

| Visiting vehicle | Course | Course's vehicle loses | Visitor's own loss | FULL | HALF |
|---|---|---|---|---|---|
| Super Hang-On's bike | OutRun | 59.0 % | 64.9 % | 0.910 | 0.954 |
| MAD POWER | OutRun | 59.0 % | 55.4 % | 1.066 | 1.032 |
| Ferrari | Super Hang-On | 59.9 % | 80.9 % | 0.741 | 0.861 |
| MAD POWER | Super Hang-On | 59.9 % | 65.5 % | 0.915 | 0.957 |
| Ferrari | Hang-On | 56.0 % | 62.5 % | 0.897 | 0.947 |
| MAD POWER | Hang-On | 56.0 % | 57.0 % | 0.984 | 0.992 |
| Super Hang-On's bike | Hang-On | 56.0 % | 64.9 % | 0.864 | 0.929 |
| Ferrari | OutRunners | 89.7 % | 58.0 % | 1.546 | 1.243 |
| Super Hang-On's bike | OutRunners | 89.7 % | 64.9 % | 1.382 | 1.176 |

### 6. With pace in place

BALANCE's pace (`balance_pace.cpp`, commit `c3e54ba`) makes the course's world move by the visiting
vehicle's speed × its pace factor, while the vehicle keeps its own speed. For contact:
- **Speed lost** is in the vehicle's own units (`car_increment`, the bike's `$260078` in its own memory,
  MAD POWER's `$209074` in its own state): pace does not touch it, nor the responses' code.
- **The other vehicle's speed** is the world's, as before pace: OutRun's `traffic_speed`, the rivals'
  `rival_speed` on Super Hang-On's and Hang-On's courses, the elephants' `+$2C` on OutRunners'. The
  Ferrari's own cut is still to that speed − 80 (its code, unchanged), MAD POWER's bounce still compares its
  own speed with it, the bike's knockback does not use it.
- **The rule** meets the course's own vehicle at the same fraction of its top speed. With pace at FULL that
  is the same speed in the world (the pace factor is the course's own vehicle's distance at its top speed
  over the visitor's), so the host's rule is evaluated where the visitor really is in the course's world;
  with HALF pace the two differ by the square root of the pace factor, as all of HALF is a midpoint.
- **Time lost** (distance short over the top speed) is the same whether measured in the vehicle's units or
  the world's: both distance and top speed take the same pace factor.
- **What changes** is how contacts come about: the world's closing speeds, so how often the visitor meets
  the others, at what speed (MAD POWER on the boards now meets rivals near its top speed, which brought the
  model change of section 4), and for the bike how many ticks a contact lasts. The rule is per contact, so
  the verification below was run again with pace in place.

### 7. Verification

Runs as in section 2, with pace in place (commit `c3e54ba`), at HALF and FULL for each pair (rails runs of
9,000–15,000 frames, Kenya at X = −90, 45, 90, OutRun's full route 12,000 frames); OFF is the runs of
section 4 (pace is 1 at OFF). For each clean contact the expected loss is the visitor's own model × the
applied factor; at FULL that is the course's own vehicle's loss in that contact. Tolerance on the mean: 2
points for the Ferrari and the bike, whose responses the models give to within a point; 6 points for MAD
POWER, whose own response scatters by 3–7 points sd around its model at OFF.

| Pair | OFF: lost (model) | HALF: lost / expected | FULL: lost / target | FULL time lost (OFF) |
|---|---|---|---|---|
| bike, OutRun | 62.9 % (65.3), n 6 | 62.8 / 62.8, n 5 | 64.1 / 63.8, n 3 | 1.44 s (1.87) |
| MAD POWER, OutRun | 46.7 (46.8), n 31 | 50.9 / 51.7, n 10 | 56.0 / 58.8, n 10 | 3.10 s (2.27) |
| Ferrari, Super Hang-On | 70.9 (71.6), n 7 | 67.1 / 67.9, n 5 | 61.3 / 61.8, n 6 | 1.53 s (2.02) |
| MAD POWER, Super Hang-On | 51.2 (49.2), n 48 | 57.4 / 59.8, n 13 | 67.4 / 62.0, n 16 | 4.10 s (2.40) |
| Ferrari, Hang-On | 52.2 (52.9), n 9 | 53.8 / 54.4, n 7 | 56.1 / 56.6, n 4 | 1.39 s (1.28) |
| MAD POWER, Hang-On | 41.9 (42.0), n 59 | 51.0 / 53.6, n 19 | 61.3 / 61.5, n 26 | 3.65 s (1.88) |
| bike, Hang-On | 65.0 (66.1), n 12 | 63.2 / 62.6, n 9 | 58.9 / 56.6, n 6 | 1.72 s (1.82) |
| Ferrari, OutRunners | 54.6 (55.3), n 8 | 71.4 / 72.1, n 3 | 88.6 / 89.2, n 3 | 3.15 s (1.34) |
| bike, OutRunners | 65.1 (65.0), n 9 | 77.8 / 77.9, n 9 | 92.1 / 92.1, n 2 | 3.03 s (1.87) |

(The contacts of each setting are different ones: the runs part after the first contact. The time lost at
FULL also reflects that the visitor meets the others at other speeds with pace in place; MAD POWER's FULL
runs met them near its top speed, 380–413 km/h, against 316–341 at OFF.)

Before pace was merged the same check gave, at FULL, Ferrari on OutRunners' 87.4 / 88.0 (n 12), the bike
there 91.1 / 91.2 (n 5), the Ferrari on Super Hang-On's 63.0 / 63.5 (n 15) and on Hang-On's 59.4 / 60.0
(n 15): the Ferrari's cut and the bike's knockback are scaled to within a point in every case, with or
without pace.

All means are within the tolerance. The largest: MAD POWER on Super Hang-On's course loses 5.4 points more
than its target at FULL (sd 5.6; its own response scatters most there, 6.6 points sd at OFF), and on OutRun's
2.8 points less; its bounce is scaled tick by tick, and the drag in the bounce changes with the speed, so a
factor does not carry over exactly.

**Identity.** Compared with commit `c3e54ba` (pace merged) built with the same test instrumentation (the
harness, the testing functions of `balance_contact.cpp` in a file of their own, the `ORMode` trace call; its
vehicle code untouched), each run in a fresh working directory, the WAV and four snapshots byte for byte:
- BALANCE OFF, the nine visiting pairs (OutRun's full route with `CB_CHASE`, rails on Super Hang-On's and
  Hang-On's courses, Kenya at X = −90; 5,000–6,000 frames): identical, 9 of 9;
- each vehicle on its own course at OFF, HALF and FULL (the Ferrari on OutRun's full route, the bikes on
  rails, MAD POWER on Kenya at X = 45): identical, 12 of 12.

Every one of these runs has contacts (counted in the same runs with `CB_CONTACT_LOG`): 1 to 21 a run; 11
for the Ferrari at home, 3 and 11 for the bikes at home, 2 for MAD POWER at home. (The same 21 checks also
passed against `855d672` before the rebase onto pace.)

### 8. Testing switches

| Switch | What |
|---|---|
| `CB_CONTACT_LOG=<file>` | the trace of section 2 |
| `CB_STEER_REL`, `CB_STEER_P`, `CB_STEER_D` | harness `steerto` relative to the road's centre, damped (gains) |
| `CB_CHASE` | steer at the nearest of OutRun's traffic ahead (OutRunners' race cars when there are any) |
| `CB_RAILS=<X>`, `CB_RAILS_OFFS=<list>` | section 2 |
