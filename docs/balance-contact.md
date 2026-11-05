# BALANCE: contact with other vehicles

What a contact with another vehicle costs each game's own vehicle on its own course, what it costs each
visiting vehicle, and the rule BALANCE uses to bring the visiting vehicle in line (`balance::contact`,
`src/main/crossover/balance_contact.cpp`). Scenery crashes are not part of this aspect.

## 1. Each game's response, in the code

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

## 2. Method

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

## 3. What a contact costs the course's own vehicle

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

## 4. What a contact costs a visiting vehicle (BALANCE OFF)

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

## 5. The rule

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

## 6. With pace in place

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

## 7. Verification

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

## 8. Testing switches

| Switch | What |
|---|---|
| `CB_CONTACT_LOG=<file>` | the trace of section 2 |
| `CB_STEER_REL`, `CB_STEER_P`, `CB_STEER_D` | harness `steerto` relative to the road's centre, damped (gains) |
| `CB_CHASE` | steer at the nearest of OutRun's traffic ahead (OutRunners' race cars when there are any) |
| `CB_RAILS=<X>`, `CB_RAILS_OFFS=<list>` | section 2 |
