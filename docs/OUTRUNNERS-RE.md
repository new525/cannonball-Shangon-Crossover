# OutRunners: the program, for putting other vehicles on its courses

What putting other games' vehicles on OutRunners' courses, and its car MAD POWER on theirs, needs to
know about OutRunners' main program: the main loop, the player car's state, the road and how a course
is chosen, collisions, the car's sprites, the car models and the branch choice at a checkpoint, with
the code each was read from and the experiment that confirmed it.

Set: MAME `orunnersj`. Reference: MAME 0.264, and the emulated board `crossover/orboard` (identical to
MAME's work RAM at every frame over 8,000 frames, see `orboard.hpp`). Addresses are main-CPU (V70)
addresses. The V70 is little-endian: a word at `$209074` is `$209074` (low byte) + `$209075`. Code
excerpts are MAME's disassembly as `tools/ortrace` prints it (`8CC[PC]` is the absolute address
`$8CC`; `mov.w` is 32 bits, `mov.h` 16, `mov.b` 8; `X[Rn]` is `Rn + X`; `[A](Rn)` is `A + Rn × size`).
Numbers in the text are hexadecimal with `$`, decimal otherwise.

Frames are numbered as in `tools/orbtest` (MAME's Lua frame: frame f is the (f−1)-th VBLANK). The
experiments are the scripts in `tools/ortrace/scripts` (section 9); "e05" is `e05_*.scr`.

Status of each finding:
- **Confirmed**: read from the code and checked by an experiment (named).
- **Read**: read from the code; no experiment checked it. Interpretations so marked can be wrong.

The program ROM (`epr-15616/15617`, 256 KB) is mirrored four times over `$000000-$0FFFFF`; the data
ROM (`mpr-15538/15539`) is at `$100000-$1FFFFF`. All the code below is in the first 256 KB.

## 1. The main loop

### Two tasks, 30 Hz each (Confirmed: e01, e13)

Each player (monitor A = player 1, monitor B = player 2) has its own task: a loop with its own stack
(player 1's SP `$21FFxx`, player 2's `$21FBxx`). The VBLANK-start interrupt switches tasks at every
VBLANK, so each player's loop runs every other frame: **one tick of a player's game is 1/30 s**.
Player 1's tick runs between the VBLANK of an odd frame and the next (even) frame's VBLANK.

Interrupts: SBR = `$3F000` (`ldpr #3F000, #5` at `$0006`). Vector `$3F100` → `$3F500` (VBLANK start,
acknowledged with `not.b #1, D00007`), `$3F104` → `$3F794` (VBLANK end), `$3F10C` → `$3F7AC` (timer 0,
34 per frame in a race: it calls `$18B2A`).

```
03f5f7: test.b  28[R24]                   ; R24 = $20E000 (shared), R25 = the interrupted task's base
03f5fa: be      3F60C
03f5fc: mov.w   4[R24], R0
03f600: stpr    #1, 4[R24]                ; swap L0SP
03f605: ldpr    R0, #1
03f608: xch.w   SP, 0[R24]                ; swap the stacks: the other task resumes after the RETIS
```

Each task waits for the VBLANK in `$8F0` (`test.b 23[R24]` until the handler toggles it). Player 1's
loop (`$6B0-$717`, player 2's is the copy at `$719-$77C` with `R25 = $218000`):

```
0006b0: not.b   #0, 2B[R24]
0006b5: jsr     EB4B
0006bb: bsr     8F0                       ; wait for the VBLANK (the other task runs meanwhile)
0006be: mov.b   #0, 2B[R24]
0006c3: test.b  700[R24]
0006c7: bne     77F
0006ca: jsr     E88C                      ; car records $20FB00.. copied to $20FC00.. (Read)
0006d0: jsr     DC5F                      ; race order (Read)
0006d6: jsr     DDD9                      ; checkpoints: branch choice, time extension, countdown (section 7)
0006dc: jsr     E164
0006e2: inc.b   26[R24]
0006e5: movea.b 208000, R25               ; player 1's base
0006ec: bsr     C5A                       ; input edges (Read)
0006ef: movz.bw 80[R25], R0               ; game state $208080
0006f4: movea.b 1000[R25], R23            ; R23 = $209000
0006f9: bsr     848                       ; coin / START (Read)
0006fc: mov.w   8CC[PC](R0), R0
000704: jsr     [R0]                      ; state handler
000706: jsr     11F2C                     ; the object list
00070c: jsr     18C45
000712: test.b  24[R24]                   ; 0 (two tasks): loop
000715: be      6B0
```

Game states (`$208080`, table `$8CC`): 0 `$1440` (the boot), 2 `$1000` (attract, from frame 679 with
or without a coin: a run with no coin also changes 0 -> 2 at 679; a coin inserted during the boot, at
frame 300, counts, and START at 680 starts the game at 683), 6 `$30C0` game (from START at 701; also the car select), 3 `$16A4` (player 2's while player 1
races alone); 1 `$1698`, 4 `$1762`, 5 `$2018`, 7 `$E014`, 8 `$D9F` not identified. State 6 has substates
(`$208082`, table `$30CC`); in e04-e15 (fresh EEPROM, coin at 600, START at 700): 1 at 701, 2 at 765
(`$31B9`: the race init, clears `$2080A0-$2080A6`), 3 at 767 (car select, transmission), 4 at 2505, 5
at 2507 (`$334F`: race setup), `$17`-`$1A` and `$0B` from 2571 (start), 6 at 2581, 7 at 2645 (racing).

### The object list (`$11F2C`) (Confirmed: e13, e08, objects disabled one at a time)

Objects are 256-byte blocks in the player's half of the work RAM (`$200400`.. for player 1); `[obj]`
bit 0 = active, `4[obj]` = routine, `C[obj]` = the object's own state. `$11F2C` calls the active ones
in list order with `R20` = the object. Player 1's list in a race (frame 3201, e13):

| Object | Routine | What it is |
|---|---|---|
| `$200400` | `$715C` | the player car: inputs, physics, camera, road generation and projection (section 2) |
| `$200500` | `$16D6C` | the road: writes NBG3's row scroll / row select (video RAM, `$17075-$172C1`) |
| `$200600` | `$17790` | the stage: course index `$20060F`, course loads (section 3) |
| `$200B00` | `$C283` | HUD km/h digits (disabled: digits gone) |
| `$200C00` | `$C379` | HUD gear box ("AUTO" gone when disabled); the shift logic (`$C42F` writes the gear) |
| `$200D00` | `$C22D` | HUD speed bar (disabled: gone) |
| `$200E00` | `$C332` | HUD "km/h" label (disabled: gone) |
| `$201200` | `$C8CB` | HUD countdown digits (disabled: gone) |
| `$201600` | `$C6AA` | HUD lap time (disabled: gone) |
| `$201700` | `$C7F1` | HUD stage name (disabled: gone) |
| `$201800` | `$99B8` | the player car's sprites (section 5) |
| `$202300`, `$201100`.. | `$12C17` | roadside objects (Read: they write sprites through `$FE04`) |
| `$202F00`, `$203200`, `$203300` | `$12DF0` | roadside objects (Read) |
| others | `$12A5A` (`$200800`), `$12968`, `$139D8`, `$18E1E`, `$1924C`, `$CE2C`, `$14374`, `$14460`, `$13228`, `$12AE8` | not identified (`$200800`, `$202300`, `$202F00` disabled: no visible change at that moment) |

### The player car's tick (`$715C`, state `C[$200400]`) (Read; the variables are confirmed in section 2)

States (table `$7184`): 0 `$719C` init, 1 `$72F4` start line, 2 `$7325` driving, 3 `$73A0` time up
(e14b: at 9233, GAME OVER follows), 4 `$746C` (not identified), 5 `$819D` crash / bounce (section 4).
Every tick first: `$76AF` (race order of the cars around), `$A304` (inputs), `$124A4` (collision
boxes); then state 2:

```
007342: jsr     127DB                     ; car-to-car test (section 4)
007348: test1   #2, [R20]                 ; collision flagged -> $7F87 (section 4)
00734c: bne     7F87
00734f: mov.w   6C[308[R25]], 4[R23]
007358: movea.b 2000[R25], R11            ; the segment ring $20A000
00735d: add.h   40[R23], R11              ; + the car's segment
007361: jsr     A39C                      ; steering, speed, lateral, advance along the course
007367: jsr     A89E                      ; camera
00737a: bsr     7B74                      ; sliding (substate E0[R20])
00737d: jsr     79A0
007383: bsr     77BD                      ; publish the car to its record $20FB00 (section 2.8)
007386: jsr     FF8E                      ; road generator: course script -> segment ring
00738c: jsr     102CE                     ; projection of the segments -> $20B000 (for the road object)
007392: jsr     121E8
007398: br      7A7A                      ; sprite frame selection (section 5)
```

Player 2 differs only in its bases: `R25 = $218000`, `R23 = $219000`, objects at `$210400`.., its
car record `$20FB80`, its inputs ANALOG4/7/8 and `P1_B`, its sprite list for monitor B, and its tick
on the other frames. `R24 = $20E000` is shared (e07 prints the bases).

## 2. The player car's state

`R23 = $209000`, the car object `R20 = $200400`, `R25 = $208000` (player 1).

| Address | Size | What | Units | Written by | Status |
|---|---|---|---|---|---|
| `$209074` | u16 | speed | course units per tick; km/h shown = speed / 136 | `$ABF3` / `$ACCD` (`$AAC9`) | Confirmed e02, e03 |
| `$20042C` | u16 | copy of the speed (what the HUD and the other cars read) | | `$A47E` | Read |
| `$208F50` | u16 | km/h shown | speed / 136 | `$C2EE` | Confirmed e02 |
| `$209042` | u16 | segment counter (course position) | segments | `$A6B9` | Confirmed e03 |
| `$209034` | u32 | position within the segment | 0 .. segment length (`$8000` on a straight) | `$A6DF` | Confirmed e03 |
| `$209032` | u16 | `$209034 >> 8` | | `$A6EB` | Read |
| `$209040`, `$2090A4` | u16 | offset of the car's segment in the ring (`× $20`, `& $AFFF`) | | `$A6CF` | Read |
| `$209038` | s16 | lateral position from the centre line | world units; road half-width 440 on the pre-stage | `$A67E` | Confirmed e03, e12 |
| `$20042E` | s16 | lateral velocity per tick | | `$A5DB` | Read |
| `$200420`, `$200424`, `$200428` | s32 | world x, y, z | x = lateral on a straight; a segment is 128 long | `$A800`, `$A835`, `$A812` | Confirmed e03 (x) |
| `$209070` | u16 | engine speed (rpm) | approaches speed × 256 / gear ratio by 1/3 per tick | `$AD23` | Confirmed e09 |
| `$209060` | u8 | car (0 = MAD POWER .. 7) | | `$555D` (car select) | Confirmed e10 |
| `$209061` | u8 | transmission: 0 automatic, 1 manual | | `$55C5` | Confirmed e09 |
| `$209062` | u8 | gear (0-based) | | `$C42F` | Confirmed e09 |
| `$209063` | u8 | number of gears − 1 | | `$A29E` | Confirmed e09, e10 |
| `$209064` | u8 | ratio of the gear in use | | `$A2A7` | Confirmed e10 |
| `$208100`, `$208101`, `$208102` | u8 | steering (`$FF` full left, `$7F` centre, `$00` full right), accelerator, brake | | input code | Confirmed e11 |
| `$2004D0`, `$2004D1`, `$2004D2` | u8 | the same, as the car reads them: accelerator, brake, steering | | `$A304` | Confirmed e11 |
| `$200496` | s16 | steering angle | | `$A4D0` | Read |
| `$20907C` | u8 | `$FF` off-road (a side of the car beyond the edge) | | `$B83D` | Confirmed e06, e12 |
| `$209140-$209142` | u8 | wheel surface / smoke kinds (0 on the road) | | `$A3A0`, `$B7xx`, `$AB36`.. | Read |
| `$209065` | u8 | wheelspin or locked wheels (`$FF`) | | `$AB0A`, `$AB4D`, `$AC55` | Read |
| `$209067` | u8 | sliding (`$FF`) | | `$A537`, `$A511` | Read |
| `$20040C` | u8 | car state (above): 2 driving, 5 crash / bounce | | | Confirmed e06, e07 |
| `$200400` | u16 | flags: bit 0 active, bit 2 collision this tick, bit 3 crashing | | `$126DB`, `$8155`, `$733C` | Confirmed e06 |
| `$20046E` | u8 | collision kind (section 4) | | `$12603`, `$126E0`, `$1286A`, `$12913` | Confirmed e06, e07 |
| `$209058` | u32 | pointer to the car's parameter record | | `$A2AC` | Confirmed e10 |
| `$209050`, `$209054` | u32 | torque curve, gear table pointers | | `$A28C`, `$A2A3` | Confirmed e10 |
| `$20FB6A` | u16 | countdown (master) | 1/30 s | `$DECA` (−1 per tick), `$DEB2` (extensions) | Confirmed e14 |
| `$209012` | u16 | countdown (copy) | 1/30 s | | Confirmed e14, e14b |

### 2.1 Speed (Confirmed: e01, e02, e03)

The speed is integrated once per tick by `$AAC9` and written to `$209074`. The HUD value is

```
00c2db: movz.hw 2C[0[R23]], R18           ; the car's speed ($20042C)
00c2e9: divu.h  #88, R18                  ; / 136
00c2ee: mov.h   R18, F50[R25]             ; $208F50, the km/h shown
```

e02: speed `$A936` (43,318) at frame 3400, `$208F50` = 318; the screen drawn at 3400 shows 317, the
previous tick's value (`$A89D` / 136).

The speed is also the advance along the course per tick (`$A5F4-$A61D`: the speed × cos of the angle
between the car's lateral velocity and its speed, `$12318`, >> 14, added to `$209034`). e03 writes the
speed `$9000` in the middle of each tick (at `$A477`, before it is used) with no lateral velocity: the
segment counter goes up by 1 and `$209034` by `$1000` every tick, `$9000` in all, and 271 km/h
(`$9000` / 136) is shown.

### 2.2 Position along the course (Confirmed: e03)

The course is a chain of segments generated into a ring of 128 records of 32 bytes at `$20A000`
(section 3.1). The car's position is the segment counter `$209042` plus `$209034` within the
segment. A segment is `$8000` long at the centre line; on a curve its length depends on the lateral
position (the inside is shorter):

```
00a682: movea.b 20[R11], R12              ; next segment (R11 = the car's)
00a68b: movz.hw 0[R11], R17
00a68f: sub.h   0[R12], R17               ; heading change over the segment
00a698: movs.hw 100C00(R17), R17          ; sine table ($100C00; cosine $101400), 16384 = 1.0
00a6a0: mulu.w  R13, R17                  ; × lateral position
00a6a3: sha.w   #FA, R17                  ; >> 6
00a6a7: addc.w  #8000, R17                ; segment length at this lateral position
00a6ae: cmp.w   R17, R15                  ; R15 = $209034 + advance
00a6b1: blt     A6DF
00a6b3: sub.w   R17, R15                  ; into the next segment
00a6b6: mov.w   R12, R11
00a6b9: inc.h   42[R23]                   ; $209042
```

The world position (`$200420-$20042B`) is interpolated between the segment's and the next one's
(`$A7CC-$A83E`); e03: at lateral −300 on a straight, world x = −300 (`$FFFFFED4`), at +300, `$12C`.

### 2.3 Lateral position, road edges, off-road (Confirmed: e03, e06, e12)

`$A66E`: lateral = `$209038` + lateral velocity `$20042E` + collision push `$2004CE`, kept within
± (segment half-width + `$60`) (`$A77C-$A79A`). The segment's half-width is `+$12` of its record (440
for width code 5, section 3.1).

Off-road (`$B79B-$B83D`): the car's sides are lateral ± half-width of the car (parameter `+$1A`, 44 for
MAD POWER); `$20907C` = `$FF` when a side is beyond the segment's half-width. e12 (pre-stage, half-width
440, lateral written mid-tick): 395 on the road, 397 and 400 off-road: 397 + 44 > 440.

### 2.4 Steering and lateral motion (Read)

`$A482-$A5DB`:
- steering angle = the car's steering table (parameter `+0`, 32 signed bytes) at steering >> 3, × 20 /
  16, then scaled by the speed: × speed / `$4000` up to `$2000`, × (speed + `$6000`) / `$10000` up to
  `$A000`, unscaled above (`$A49E-$A4D0`); to `$200496`;
- the road's curvature at the car (`8E`/`90[R20]`, the heading at the car this and last tick) gives a
  push outwards, divided by `$20906A` + `$100`;
- speed × steering angle is compared with the grip limit (parameter `+$20`): within it the lateral
  velocity takes the steering and the push; beyond it the car slides (`$209067` = `$FF`, slide amount
  `$2004B8` = excess × parameter `+$24` / −`$400000`), with tyre smoke;
- lateral velocity `$20042E` approaches its target by 1/4 or 1/12 per tick in other cases
  (`$A57C`, `$A5A3`).

### 2.5 Speed integration (`$AAC9`) (Read)

Inputs: `R7` = gravity along the slope (sine of the segment's pitch `+2`) minus drag (speed² >> 16, the
surface from `$B79B`, + `$1800`), `R8` = engine force = accelerator map (`$AE37`, signed bytes) × torque
(`$209300 + gear × $200`, indexed by speed >> 8) >> 6, `R9` = brake map (`$AF37`) × parameter `+$1C`
(negative), `R19` = the gear's ratio.

```
00aac9: add.w   R7, R9
00aacc: add.w   R8, R9
00aacf: sha.w   #FA, R9                   ; force = (R7 + R8 + R9) >> 6
00aad3: mov.h   R9, 94[R20]
00aad8: mov.w   R19, R18
00aadb: mulu.w  #FFFF, R18
00aae2: shl.w   #F8, R18                  ; top speed in this gear = ratio × $FFFF / 256
...
00ac5a: movz.hw 74[R23], R0
00ac5e: add.w   C4[R20], R0               ; + collision speed change (section 4)
00ac68: mov.w   R0, R1
00ac6b: add.w   R9, R1                    ; target = speed + force
00ac6e: cmp.w   R18, R1                   ; above the gear's top speed: approach it by 1/30
...                                       ; else approach the target by 1/2 (rounded away from 0)
00accd: mov.h   R0, 74[R23]
```

Wheelspin: a force ≥ parameter `+$C` sets `$209065` and limits the force to parameter `+$14`; it ends
when the force is below `+$10` (`$AAEC-$AB80`). A force ≤ −`$380` while moving (hard braking) also sets
`$209065` (`$AC4B` → `$AB07`). Above the gear's top speed the speed approaches it by 1/30 per tick
(`$AC73-$AC94`). Engine speed `$209070`: speed × 256 / ratio, approached by 1/3 per tick (1/10 when
spinning); at standstill the accelerator × 256 (`$ACE7`). The torque table `$209300` is built per gear
at the start by `$A2B6` from the curve's points (pairs of 16-bit speed, torque), by linear
interpolation.

### 2.6 Gears (Confirmed: e09, e10)

Gear tables (`$B2B8[2 × car + transmission]`): the number of gears − 1, then the ratios. MAD POWER:
automatic `$B2F8` = 0, 248 (one ratio); manual `$B2FA` = 5, 106, 146, 186, 221, 253, 255. e09: with
the manual box chosen (wheel right at the transmission screen), shift up (`P1_A` bit 0) each 100
frames: `$209062` goes 1, 2, 3, 4, 5 (`$C42F`). In 1st gear at speed `$3B10` the engine speed is `$8AEB`
(15,120 × 256 / 106 = 36,516 = `$8EA4`, being approached).

### 2.7 Crash and bounce (Confirmed: e06; the details Read)

See section 4 for what starts it. State 5 (`$819D`) lasts until `$200441` (the spin count) runs out; it
holds the inputs at accelerator 0, brake 0, steering `$80`, decays the lateral push `$2004CE` (× `$209128`
/ 256 per tick) and the speed change `$2004C4` (× `$20912C` / 256), and makes the car jump: `$2004C8` is
a height added to the world y (`$A830`), `$2004CC` its vertical speed (gravity `$20912A`, restitution
`$209126`). The normal driving code then runs (`br 734F`). Back to state 2 at `$8263`; flag bit 3 is
cleared at `$733C`. e06: hit at 3367, state 5 from 3369 to 3449 (40 ticks), the speed `$797F` → `$655B`
→ `$5980`, the lateral position pushed from 532 back to 406, 330.

### 2.8 The car record (`$20FB00`, player 2 `$20FB80`) (Read; 44 and 6A Confirmed)

`$77BD` publishes the car every tick into its record (`24[R25]`); `$E88C` copies the records to the
race's car array `$20FC00 + $80 × car number` (`20[R25]`; the car number is `$208001`, 0 for player
1, 1 for player 2). Everything about other cars (collisions, drawing, race order, the branch choice)
reads this array. Fields: `30/34/38` world x/y/z, `3C` sprite frame, `3E`, `4A` steering frame, `40`
position in segment >> 8, `42` and `46` segment counters, `44` lateral position (e05), `49` car, `4C`
speed, `4E` lateral velocity, `50-53` wheel kinds, `54` road pitch, `58` flags, `60` branch markers
passed (e05), `62` stage node, `6A` countdown (e14).

## 3. The road, the camera and the course

### 3.1 The pipeline (Read; the ends Confirmed: e03, e04)

1. **Course script → segment ring.** `$FF8E`/`$FFE4` run the course script (pointer `$2090A0`) and
   append segment records to the ring `$20A000` (128 × 32 bytes) until the ring holds the draw
   distance (`$2090AA` ≥ `$2090AC`).
2. **Car physics** (`$A39C`) moves the car along the ring (section 2.2).
3. **Camera** (`$A89E`): from the car's segment, lateral position and the segments' angles: camera
   position `$209020/24/28` (world), angles `$20902C` (pitch), `$20902E`, `$209030`.
4. **Projection** (`$102CE`): the rotation matrix at `$2090DC`.. and the ring's segments through the
   camera into the table at `$20B000` (16 bytes per segment; in the tick measured it wrote
   `$20B100-$20B566` and `$2090EC-$2090FC`).
5. **Road object** (`$16D6C`: `$16F6C`, `$170F8`, `$17257`): reads that table and the car record's
   segment counter (`46[20[R25]]`) and writes NBG3's row scroll and row select in video RAM
   (`$17075-$172C1`), and `$20BB00`.

So the road follows whatever is in the car's course position (`$209042`, `$209034`), lateral position
(`$209038`) and speed (`$209074`): e03 drove the car by writing those in the middle of the tick, and the
road, camera and scenery followed (`e03_left.png`, `e03_right.png`).

Segment record (`$20A000 + $20 × n`, Confirmed in part by a dump at frame 3000 on the pre-stage):

| Offset | Size | What |
|---|---|---|
| `+0` | u16 | heading (12-bit angle, `& $FFF`) |
| `+2` | u16 | pitch (12-bit) |
| `+4`, `+8`, `+C` | s32 | world x, z, y (× 256): z advanced `$80` per segment on the straight |
| `+10` | u16 | road width (`$370` = 880 for width code 5) |
| `+12` | u16 | half-width (440) |
| `+14` | u8 | width code |
| `+15` | u8 | (from the script's 4th byte) |

### 3.2 The course script (Read; the format checked against the pre-stage and Kenya scripts)

Per route node, a pointer at `$3CC00 + 4 × node` (`$FF3E`): pre-stage (node 0) `$3CCA0`, Kenya (node 30)
`$3EA5C`. Read by `$10149`: a signed 16-bit count; when positive, 4 bytes follow (heading change per
segment, pitch change per segment (signed, × 256), width code (× 176 = target width), a byte to `+15`)
and `count` segments are generated with them; when zero or negative, a command (table `$101AC`):
- −4 (`$1029C`), the stage header: stage number, node (to `$2080A2`, `$2080A3`). Every node's script
  starts with it; read from the ROM for nodes 0-30: stage 0 for node 0, 1 for 1-2, 2 for 3-6, 3 for
  7-12, 4 for 13-20, 5 for 21-30, and the node itself;
- −2 (`$101F5`), the branch (section 7): then the next node for choice 0 and for choice 1 (a byte
  each), and the two scripts that follow (32-bit pointers);
- others not identified.

Pre-stage (`$3CCA0`): `-4: 0, 0 | 50: 0, 0, 5, 0 | 60: 8, 0, 5, 0 | 180: −2, 0, 5, 0 | ...`; Kenya
(`$3EA5C`): `-4: 5, 30 | 100: 0, 0, 5, 0 | 180: 0, −6, 5, 0 | ...`.

### 3.3 The route and the course index (Confirmed: e04, e15)

The route is a graph of 31 nodes; a node's course index is `$17DCC[node]`:

```
node:   0   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30
course: FF  0  1  2  3  4  5  6  7  9 10 12 13 14 15 17 19 20 22 24 25 26 27 29 31 33 34 36 38 40 41
```

Variables (`$2080A0-$2080A6`, cleared by the race init at `$31B9`):

| Byte | What | Set by |
|---|---|---|
| `$2080A0` | start node | 0 in a game; the attract demo (`$32EA`) |
| `$2080A1` | node of the course being generated | the branch (`$10247`) |
| `$2080A2`, `$2080A3` | stage number, node, from the script's stage header | `$1029C` |
| `$2080A4`, `$2080A6` | stage number, node, a while after the stage header | `$CB02`, `$CB0E` |
| `$2080A5` | stage number at the branch | `$10252` |
| `$20060F` | course index (`$17DCC[node]`) | `$17804` (start), `$17910` (branch) |

e15 (normal play, kept left of centre, countdown held): nodes 2, 6, 12, 20, 30 and courses 1, 5, 13,
25, 41, stage numbers 1-5; on Kenya `$2080A1/A3/A6` = 30 and `$2080A2/A4/A5` = 5; the goal at ~22000.

### 3.4 Starting a race on a given course (Confirmed: e04, e04b, e15)

At the start (frame 2571) the stage object's state 0 (`$177A8`) computes the course index from the
start node `$2080A0` and `$FF39` loads that node's script:

```
0177f5: movz.bw A0[R25], R0               ; $2080A0
0177fa: tb      R0, 17804[PC]             ; node 0: $FF (pre-stage)
0177fe: not.b   17DCC[PC](R0), R0
017804: not.b   R0, F[R20]                ; $20060F = $17DCC[node]
...
00ff39: movz.bw A0[R25], R0
00ff3e: mov.w   3CC00(R0), R11            ; the node's course script
00ff46: mov.w   R11, A0[R23]              ; $2090A0
```

**To start on Kenya:** after the race init has cleared the bytes (substate 2, frame 767 from a fresh
EEPROM) and before the start (frame 2571; i.e. while `$208080` = 6 and `$208082` = 3 or 4), write
`$2080A0`, `$2080A1`, `$2080A3`, `$2080A6` = 30 and `$2080A2`, `$2080A4`, `$2080A5` = 5 (as normal play
has them on Kenya, e15). e04 writes them at frame 2500 (car select / transmission screen): course 41 is
loaded at 2573, the race starts on the savanna with "FINAL STAGE" and the scenery of e15's Kenya
(`e04_2700.png`, `e04_3100.png`: driving at 105 km/h, 3.84 s into the race). For another course, use
its node and its stage number (the first command of the node's script, section 3.2: nodes 1-2 → 1,
3-6 → 2, 7-12 → 3, 13-20 → 4, 21-30 → 5).

e04b writes only `$2080A0`: Kenya's road is loaded and the script's header sets `$2080A2`/`A3` (then
`A4`/`A6`), but `$2080A1` and `$2080A5` stay 0; the HUD shows "PRE-STAGE" and the trees are the wrong
colour (`e04b_3100.png`), so the other bytes are needed. Which of them the HUD and the scenery read
was not narrowed down.

The countdown is `$20FB6A`; `$209012` is only a copy: holding `$20FB6A` keeps 40 s and the race going
(e14, still driving at 9000), holding `$209012` alone does not (e14b: time up at 9233, GAME OVER).

## 4. Collisions

### 4.1 The boxes (`$124A4`) (Read; sizes from the table)

Every tick the car builds a list at `$208510` with one entry per car of the race (from the car array)
and its own at `$2085B0` (`500[R25]` points to it): car record pointer, segment counter, segment
counter + 4, then a box in road coordinates: along the road `pos .. pos + $A0` where `pos` = position
in segment >> 8 (`$80` per segment), across `lateral − w .. lateral + w`. The box per car is at `$12928
+ 8 × car` (`+$A0, 0, −w, +w`); `w` per car: 44, 44, 40, 40, 40, 52, 15, 42 (MAD POWER 44, the same as
its parameter `+$1A`; car 6's table entry is 15 against 30 in its parameter record).

### 4.2 The tests (Confirmed: e06 scenery, e07 cars; others Read)

All tests compare the car's box with the other box by the signs of the four differences (overlap when
both axes straddle). A hit sets bit 2 of `$200400`, the kind in `$20046E` and an argument:

| Test | Other | Kind `$20046E` | Argument |
|---|---|---|---|
| `$127DB` (state 2, every tick) | every other car of the list in the same segment window | 2 | `$200468` = car number |
| `$12554` / `$1261B` (called by roadside objects) | the object's footprint (`60[obj]`: 4 halfwords), at its segment range `56/58[obj]` repeated every `10[obj]` segments, lateral `3F[obj]` × half-width | 0 (crash) | `$209120/$209122` = offsets / 2 |
| `$1254F` | the same | 3 (minor: flag cleared, `$209147` = 15) | |
| `$12896` | an object with its own position (`1C[obj]`) | 1 | `$200468` = the object |

e06: on Kenya with no steering, off the road at 3348, the footprint test at `$126DB` hits at 3367 (kind
0). e07: both players in a race, player 1 steering right, player 2 left: kind 2 at 2985 (`$1286A`),
player 1's car in state 5, "Ouch!" on both screens (`e07_left.png`, `e07_right.png`).

### 4.3 What happens (`$7F87`) (Read; the state change Confirmed e06, e07)

```
007f87: movz.bw 6E[R20], R0
007f8b: mov.w   7F94[PC](R0), R0          ; 0 $7FB3, 1 $8029, 2 $8074, 3 $7FA4
007f90: jmp     [R0]
```

- **Kind 2 (car)** `$8074`: relative speed (the other's − own, rear hits doubled) × the other's weight /
  own weight (`$82A4[car]`: 272, 256, 240, 240, 288, 384, 160, 256; MAD POWER 272), + a per-rank offset
  (`$8292`), indexes a response table (`$209130` → `$82DE`, rows of 16 bytes up to `$1F0`): speed change
  `$2004C4` = (`+$E`) × ..., lateral push `$2004CE` = (lateral difference + 2 × lateral velocity
  difference) × (`+8`) × weights, vertical speed (the jump of state 5) `$2004CC` = (`+$A`).
- **Kind 0 (scenery)** `$7FB3`: the same table indexed by the car's own speed (`$2004C0`); speed change
  from `+4`, push from `+6` by the side of the car.
- **Kind 1** `$8029`: against the object's own lateral position, speed and lateral velocity.
- **Kind 3** `$7FA4`: no response.

Then (`$8149`): state 5, flags bit 3, a sound (`$209134/$209135`), the crash frames (`$8170-$8192`).

For another vehicle the choice is: let these run (the vehicle then gets OutRunners' crash and push
through `$2004C4`/`$2004CE`/`$2004CC` and state 5), or catch the flag at `$7348` (bit 2 of `$200400`,
kind `$20046E`) and apply the vehicle's own rules.

## 5. The player car's sprites

### 5.1 Who draws it (Confirmed: e08)

Object `$201800` (routine `$99B8`) draws the car of the record at `80[$201800]` = `$20FC00` (player 1's
entry in the car array). With its active bit cleared (`$201800` = 0) the car is not drawn and the rest
of the screen is unchanged (`e08_ref.png`, `e08_off.png`). In a two-player race each player's list has
one such object per car (`$201800` → `$20FC00`, `$201D00` → `$20FC80`); the player's own car is the one
whose `80[obj]` is `$20FC00 + $80 × $208001`.

### 5.2 Drawing (Read)

`$99B8` transforms the car record's world position (`30/34/38`) by the camera (`$209020-$209028`,
matrix `DC[R23]`) into screen x, y and depth (`$9A0F-$9A86`); it is drawn when 64 < depth < `$3000` and
on screen. The scale is `$19A340[depth]`, the depth bucket `$1A0340[depth]`. Each sprite is written
twice, at `R10` and `R10 + $3000` (`$9F22-$9FB8`: 16 bytes, System 32's sprite format), at the list
pointer `$2080C0` (the car's at `$4042B0` and `$4072B0` at frame 3201; the limit `$2080C4` =
`$40C800`) and linked into its depth bucket
(`5000[R25]`, `$9E48-$9E79`): the car sits among the other sprites by its depth (Read). Parts: the
body (`$9F0A` from frame `3C[rec]`, sub-frame `4A[rec]`), the wheels (`$9EDD` twice), smoke (`$9E8A`
twice), and a further part (`$9C85-$9C98`) (Read).

### 5.3 Frame selection (`$7A7A`) (Read)

In the car object, copied to the record by `$77BD` (`38[R20]` → `3C`, `3A` → `3E`, `40` → `4A`):

```
007a95: mov.h   94D8[PC](R9), R0          ; base frame per car: 940 + 116 × car
007a9d: bsr     7B3F                      ; hill: (camera pitch $20902C − $B0 − road pitch) × 16 / $300, 0..5
007aa0: add.h   R1, R0
007aa3: mov.h   R0, 38[R20]
007aa7: movs.hw 96[R20], R0               ; steering angle
007aac: mov.b   9528[PC](R0), R0          ; -> steering frame
007ab4: mov.b   DB[R20], R1               ; moved one step per tick towards it
...
007ac6: mov.b   R1, 40[R20]
```

Wheelspin uses `$956C[car]`, sliding `$957C`/`$967C` (by direction and `$2004B8`), the crash
`$8170-$8192` (frames from the response table, spin counts `41/42[R20]`). Frame definitions:
`$34138[frame]`.

## 6. The car models

### 6.1 Where the parameters are (Confirmed: e10)

`$A278` at the start of a race:

```
00a278: movz.bw 60[R23], R0               ; car
00a27c: mov.w   B038(R0), R12             ; parameter record
00a284: movz.bw 61[R23], R1               ; transmission
00a288: movea.h [R1](R0), R1              ; 2 × car + transmission
00a28c: mov.w   B32C[PC](R1), 50[R23]     ; torque curve
00a296: mov.w   B2B8[PC](R1), R1          ; gear table
00a29e: mov.b   [R1+], 63[R23]            ; gears − 1
00a2a3: mov.w   R1, 54[R23]
00a2a7: mov.b   [R1], 64[R23]             ; first ratio
00a2ac: mov.w   R12, 58[R23]
00a2b0: jmp     B764
```

Parameter records (`$B038[car]` = `$B058 + 40 × car`), signed 16-bit unless noted:

| Offset | Use (Read) | MAD POWER | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|---|
| `+0` (32-bit) | steering table (32 bytes) | `$B198` | `$B1B8` | `$B1D8` | `$B1F8` | `$B218` | `$B238` | `$B258` | `$B278` |
| `+4` (32-bit) | not identified (all cars) | `$B298` | | | | | | | |
| `+8` | a multiplier in `$B79B` (surface / off-road; `$B7FA`, `$B842`) | 90 | 88 | 70 | 70 | 78 | 56 | 64 | 12 |
| `+C` | wheelspin threshold (force) | 448 | 384 | 432 | 432 | 1024 | 416 | 768 | 512 |
| `+10` | wheelspin end | 416 | 368 | 400 | 400 | 944 | 384 | 752 | 448 |
| `+14` | force while spinning | 288 | 288 | 320 | 320 | 512 | 288 | 736 | 288 |
| `+18` | not identified | 40 | 40 | 60 | 60 | 76 | 56 | 8 | 60 |
| `+1A` | half-width (off-road) | 44 | 44 | 40 | 40 | 40 | 52 | 30 | 42 |
| `+1C` (32-bit) | brake force factor | −400 | −400 | −500 | −500 | −990 | −200 | −700 | −400 |
| `+20` (32-bit) | grip limit (speed × steering) | `$12C000` | `$138000` | `$11E000` | `$11E000` | `$F0000` | `$104000` | `$B6000` | `$BD000` |
| `+24` | slide factor | 37 | 37 | 32 | 32 | 36 | 34 | 24 | 33 |

Torque curves: MAD POWER automatic `$B36C`, manual `$B38C`. Weights for collisions: `$82A4[car]`.
Sprite base frames: `$94D8[car]`. Car names: the pointers at `$1F38[car]`.

### 6.2 The handling, for a transcription (Read)

All per tick (30 Hz), in this order (player car state 2):

| Routine | Inputs | Outputs |
|---|---|---|
| `$A304` | `$208100-$208102` | `$2004D0-$2004D2` |
| `$A39C` (to `$A89D`) | the above, `$209074`, `$209038`, `$209034`, ring segment `R11`, parameters `$209058` | steering `$200496`, lateral velocity `$20042E`, lateral `$209038`, course position, world position, `$2004B4`, `$20903A` |
| `$B79B` (from `$A3EA`) | lateral, car half-width, segment half-widths | drag term, `$20907C`, `$209140-$209142` |
| `$AAC9` (from `$A467`) | forces, gear ratio | speed `$209074`, `$2004C0`, rpm `$209070`, wheelspin `$209065` |
| `$12318` | lateral velocity, speed | angle for the advance |
| `$104F6` | rotation of the lateral offset by the heading | world x/z |
| `$A89E` (to `$AAC8`), `$AD28` | car position | camera |
| `$7B74` (to `$7D27`) | lateral velocity, `$2004B2/B4` | `$2004E2`, `$209100-$209106` (body roll / sliding, Read) |
| `$79A0` | `$209100-$209112` | `$2080B8` |
| `$A2B6` (at the start) | torque curve points | torque table `$209300` |
| `$C379` / `$C42F` | shift buttons | gear `$209062`, ratio |
| `$7F87`, `$819D` | collision kind | crash / bounce (section 4) |

Tables used: sine `$100C00`, cosine `$101400` (12-bit angle, 16384 = 1.0), accelerator map `$AE37`,
brake map `$AF37`, steering-frame `$9528`. The listing of every executed instruction of a race is
produced by `ortrace list` from a coverage file (e13); the routines above can be disassembled whole
with `ortrace dasm` (their jump tables are data and print as junk).

## 7. The branch choice at a checkpoint (Confirmed: e05, e15)

The course script has a branch command (−2, `$101F5`, section 3.2). When the road generator consumes
it (the car is then on the checkpoint line: e05, frame 3959), it increments the car record's `$20FB60`.
`$DDD9`, at the start of the player's next passes, takes the choice as the sign bit of the car's
lateral position as published in the tick of the marker (`$20FB44` by `$77BD`, copied to the car array),
then records it (e05: `$208349` at frame 3963):

```
00de64: movz.hw 44[R15], R5               ; the car's lateral position
00de68: shl.h   #F1, R5                   ; >> 15: 1 = left of centre
00de6c: mov.b   R5, 67[R11]
...
00dfe8: inc.b   349[R25]                  ; $208349: decisions taken
00dfec: mov.b   R3, 34A[R25](R1)          ; $20834A + n: 1 = left (West / the lower branch)
```

and `$10216` then continues the script at the chosen branch: `$2080A1` = the branch's node for that
choice, `$20060F` its course (`$10247`, `$17910`), the script pointer the branch's pointer for it.

e05: lateral held at +200 until frame 3940 and −200 after → left (`$20834A` = 1, course 1); until 3960
→ right (0, course 0): the marker was consumed at 3959 and the sign of that tick decided. e15: left
of centre at every checkpoint gives West and the lower branches to Kenya.

To pass another vehicle's steering through: write its lateral position's sign into `$209038` in the
tick of the marker (or keep `$209038` the vehicle's own lateral position, as e03 does).

## 8. Putting another vehicle on OutRunners' road (design notes)

What the experiments support:
- **Moving the vehicle:** write the vehicle's speed into `$209074` at `$A477` and its lateral position
  into `$209038` at `$A66E` (with `$20042E` and `$2004CE` cleared), every tick of player 1 (e03). The
  game then advances along its course by the speed, keeps the road, camera, scenery, checkpoints and
  branch working. Speed in OutRunners' units: km/h × 136 per tick at 30 Hz; lateral in world units
  (road half-width 440).
- **Off-road:** `$20907C` uses MAD POWER's half-width (44, parameter `+$1A`); another vehicle's width
  can be put in its parameter record or its own rule used with the segment half-width (`+$12`).
- **Drawing:** suppress the car by clearing `$201800`'s active bit (e08) and draw the vehicle in its
  place (screen position: the same transform as `$99B8`).
- **Collisions:** catch bit 2 of `$200400` / `$20046E` at `$7348` (section 4).
- **The start:** section 3.4.

## 9. Tools (`tools/ortrace`)

- `build.sh <dir>`: builds `ortrace` (the board of `crossover/orboard` with the V70's instruction hook;
  nothing in the board is changed).
- `ortrace run <romdir> <script> [-eeprom f] [-draw mask] [-o out]`: runs the board from power-on with a
  script of commands per frame: inputs, RAM writes (also at an instruction: `pcw16 pc addr value`),
  prints, screenshots, RAM dumps (also at an instruction), write watches with the writing PC (work RAM;
  `vwatch` for video and sprite RAM), breakpoints with registers, executed-instruction coverage,
  instruction traces, call counts. The commands are listed in `ortrace.cpp`.
- `ortrace dasm <romdir> <start> <end>`: disassembly of the program ROM.
- `ortrace list <romdir> <cov> [lo hi]`: listing of the executed instructions of a coverage file with
  call targets and counts.
- `calltree.py <trace> [depth] [frame]`, `objs.py <trace> <frame>`: a call tree, and the object list
  with each object's routine and instruction count, from a trace with registers.
- `run_all.sh <ortrace> <romdir> <out dir>`: runs every experiment (`scripts/e*.scr`, with
  `race_mp.inc`: player 1 into a race with MAD POWER, automatic) and writes `<name>.txt` and the
  screenshots into `<out dir>` (about 5 minutes in all on a 2.7 GHz Xeon; e15, the longest, 23,000
  frames). The results quoted above are theirs.

## 10. Not established

- The units of the camera and projection (`$A89E`, `$102CE`) and the projected table's format: only
  their addresses and where they are read.
- The meaning of parameters `+4` and `+18`, the response table's columns beyond those named, the
  objects marked "not identified", course script commands other than −2 and −4.
- Traffic: in a single-player race no other car was met; kind 1 collisions were not provoked.
- The draw priority of the car in the sprite list is read from the code, not checked.
- How the attract demo chooses its course (substate 9, `$32D4`, table `$3740`).
- Linked cabinets (more cars through the comm board): not looked at.

## Starting a race quickly (crossover/ormode)

Found while shortening the program's run-up (tools/ortrace, frames from power-on, fresh EEPROM):
- The car select's cursor is `$209060` (car index, set 6 frames into substate 3: 2 = EASY HANDLING);
  `$201080` and the other car copies are only written as the race starts. The screen takes no input
  for about its first 105 frames; a step comes about 20 frames after the wheel turns.
- The accelerator confirms the car and then the transmission (the screen's own instruction). On the
  transmission screen the wheel held right through the confirmation gives the manual box
  (`$209061` = 1); centred, the automatic.
- The game then waits for other players to enter until its entry countdown runs out: 16-bit, 1/30 s,
  at `$20FB1A` and `$20FC1A` (`$20041C` is a copy: writing it has no effect). Writing 2 to both at
  frame 1250 started the race at 1451 instead of about 2010.
- With these, a race (car chosen, transmission confirmed) starts at frame 1389 (1381 from a used
  EEPROM), against 2581 with the time-outs.
