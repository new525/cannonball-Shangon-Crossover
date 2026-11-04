# BALANCE: pace

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

## 1. Method

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

## 2. Each game's distance accounting (checked)

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

## 3. Top speeds and paces before BALANCE (OFF)

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

## 4. The factors

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

## 5. Where the factor is applied

The vehicle and course in force: each join registers itself for its race (`balance::set_world` in
`SHFerrari::begin`, `HOVehicle::begin`, `ORVehicle::begin`, cleared in their `end`); without one the
course is OutRun's and the vehicle the one OutRun's engine runs (`bike.enabled`, `madpower.enabled`,
else the Ferrari). `pace_now()` is 1 at OFF and on the vehicle's own course, and every use below then
gives exactly what it gave before.

### Inside the visiting vehicles (their position on the course)
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

### OutRun's world (Super Hang-On's bike and MAD POWER on OutRun's courses)
`car_increment` stays the vehicle's own speed (the speedometer `OInitEngine::update_engine`, smoke,
crash, end sequence). The world's motion uses `balance::world_speed(car_increment)`:
- traffic approach `OTraffic::update_props` ($4F0C);
- the road's width change per tick `OInitEngine::update_road` ($B8BC);
- the granular position for road scrolling and scenery zoom `OInitEngine::set_granular_position`;
- the distance score `OFerrari::do_sound_score_slip` / `OStats::convert_speed_score` ($BD78). Its table
  has 21 entries (0-335 km/h); when BALANCE changes the speed it is held to the last one.

Scenery, checkpoints, forks and the goal follow OutRun's road position.

### Super Hang-On's world (`crossover/shferrari`: the Ferrari, MAD POWER)
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

### Hang-On's world (`crossover/hovehicle`: the Ferrari, Super Hang-On's bike, MAD POWER)
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

### OutRunners' world (`crossover/orvehicle`: the Ferrari, Super Hang-On's bike)
`$209074` is both the advance per tick and, through its copy `$20042C`, the HUD (/136) and what the other
cars read. At `$A477` the world speed x 136 is written. Hook `$C2EE` (`mov.h R18, F50[R25]`, after
`divu.h #88`): R18 = the own km/h, so `$208F50` (shown) is the vehicle's speed. In the goal run
OutRunners' script drives `$209074`; the vehicle's speed is taken back from it (`own_speed`). The
other cars' pacing and the camera (it pulls back with speed) follow the world speed.

## 6. Verification

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

## 7. Notes and limits

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

## 8. Test switches

- `CB_PACE_TRACE=<file>`: per rendered frame `frame host pos world_kmh own_kmh ...` (host 0 OutRun,
  1 Super Hang-On, 2 Hang-On, 3 OutRunners); then X, curve type and radius (and OutRun's stage) for
  hosts 0-2, the hill frame index `$2004D9`, the stage `$2080A2` and the shown km/h `$208F50` for 3.
- Harness `steerto <x> road`: steer relative to the road the vehicle is on, for the Ferrari too (OutRun's
  two roads), and for Super Hang-On's and Hang-On's own bikes and OutRunners' own car on their boards.
