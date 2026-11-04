# BALANCE: curve push

How the race setup's BALANCE scales the curve push a vehicle feels on another game's course, how the
factors were measured, and what was checked. The factor table is in `src/main/crossover/balance_push.cpp`.

Target (decided with the user): each vehicle feels a host's curves as hard as it feels its own game's
curves. FULL makes its demand on the host its demand on its own courses; HALF the geometric midpoint; OFF
leaves it as it was. This is measured with BALANCE's pace (`docs/balance-pace.md`) in force.

## 1. The push and the steering of each vehicle

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

## 2. Demand

demand = the push at the vehicle's top speed (without the supercharger; MAD POWER automatic) / its
steering's full-lock lateral speed, both per tick of its own code: the fraction of its steering a curve
takes to hold a line. Above 1 the vehicle cannot hold its line at top speed with full lock.

**D**, a vehicle's demand on a course: the demand averaged along the course's curves. The demand per tick
is averaged in bins of 8 positions (OutRun, Super Hang-On, Hang-On: path positions; OutRunners: segments);
the curve bins are those whose mean |demand| is 0.03 or more; D is the mean over them, i.e. the demand
summed over the curves divided by their length (the summed-over-curves method of the earlier host-vehicle
target, divided by the curves' length so that different courses compare).

### Instrumentation

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

### Runs (BALANCE OFF, accelerator held)

- OutRun's 15 single stages (tracks 1-15, laps folded together), 7,000 frames each, no traffic, infinite
  time: the Ferrari, Super Hang-On's bike with and without the supercharger, MAD POWER.
- Super Hang-On's four courses (16-19), 24,000 frames: its bike (held; also with the supercharger), the
  Ferrari and MAD POWER (time held, `CB_SHF_FREEZE_TIME`).
- Hang-On's course (20): its bike (held), the Ferrari, MAD POWER, Super Hang-On's bike.
- OutRunners' Kenya (22) and full course (21): MAD POWER (Kenya held and steered, the full course held),
  the Ferrari, Super Hang-On's bike (time held).

## 3. Demand on the own courses and on the hosts

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

## 4. Full factors

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

## 5. With pace

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

### The Ferrari at FULL on Africa's radius-56 curve

The curve (Africa's first section, positions 1,437-1,525, Super Hang-On radius 56): the Ferrari's demand
there is 1.16 a tick on average (1.44 at most) at OFF, HALF and FULL alike: above 1, so at top speed it
cannot hold its line with full lock at any setting, and the distance it slides out grows with the time it
spends in the curve. At OFF it already left the road there (64 ticks with wheels off, down to 139 km/h, X
to 376); at FULL pace (0.82) the curve takes 1.22 times as long, which is what made it slide off at FULL.
With the push factor (0.936 at FULL) its applied demand there is 1.09: still above 1. In these runs: OFF 96
ticks in the curve, wheels off 64; FULL 115 ticks, wheels off 49, X 74-329; at HALF the harness's steering
crashed it there. Per-tick demand, as defined, does not see the time in a curve; where the demand is above
1 the slide grows with it.

## 6. Where the push is scaled

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

## 7. Checks

### Demand after the change (pace and push at the same strength)

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

### The user's case

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

### OFF and the vehicles' own courses

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

## 8. Earlier target (superseded)

The first version matched each visitor to the host's own vehicle (full = D_host vehicle / D_visitor on the same
curves): 1.253 for the bike on OutRun, 0.706 MAD POWER on OutRun, 0.759 / 0.557 Ferrari / MAD POWER on Super
Hang-On, 0.777 / 0.576 / 1.018 on Hang-On, 1.363 / 1.900 on OutRunners. It raised the bike's push on OutRun's
courses, because OutRun's courses push its Ferrari harder relative to its steering than they push the bike; the
user chose the own-game target above instead.
