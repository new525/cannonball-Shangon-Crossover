# Hang-On in the crossover

How Hang-On's course is run (an emulated Hang-On board), how other games' vehicles are put on it,
and the Hang-On addresses the code relies on, with the code they were read from.

Set: MAME `hangon` (Rev A, unencrypted 68000s). Reference: MAME 0.264. Main-CPU addresses unless
noted; the second CPU sees the shared RAM at `$7C000` (the main CPU at `$C7C000`).

## 1. The board (`crossover/hoboard.*`, `crossover/hovideo.*`)

Transcribed from MAME 0.264's `segahang.cpp` (Hang-On), `segahang_v.cpp`, `segaic16.cpp`
(Hang-On tilemaps), `segaic16_road.cpp` (Hang-On road) and `sega16sp.cpp` (Hang-On sprites):

- two 68000s at 6.2937 MHz (Musashi, shared with the Super Hang-On board through `xo_bus`); 262 lines
  of 400 clocks; main IRQ4 at line 224, held until acknowledged; the second CPU's IRQ4 and RESET
  from the second 8255's port A (bits 6 and 5);
- two 8255s (sound latch in mode 2 with handshake, screen controls, tilemap modes, ADC channel);
- ADC0804 (74 clocks of CPU/6 per conversion) for steering, accelerator and brake;
- the 1/6000 s interleave of MAME's driver, with the main CPU's slice ended at writes to the first
  8255 as MAME does.

Musashi's 68000 timing was brought to MAME 0.264's (`tools/m68ktiming`):
- Instruction times: every legal opcode word with every addressing mode, plus data-dependent
  cases (shift counts, bit numbers, MUL/DIV operands, conditions, DBcc, MOVEM counts, CHK,
  TRAPV), were run on Hang-On's sub CPU in MAME and in Musashi. All 104,854 compared cases take
  the same number of cycles. The data-dependent MULS/DIVU/DIVS times follow the 68000 manual's
  MULS rule and J. Cwik's analysis of the division microcode.
- Interrupts: where MAME's core samples the interrupt level in each instruction
  (`m68k/m68kipl.h`, generated from MAME's microcode), the autovector acknowledge time and STOP.
  In a test program, 2,998 interrupts started their handlers at the same cycle as in MAME, and
  every one of 20,292,334 instructions started at the same cycle.
- An instruction's length, which those rules need, is measured against the slice's starting
  count, so a bus access in the middle of the instruction that ends the slice (the main CPU's
  8255 writes here) does not distort it.

### Checked against MAME 0.264

A scripted game (coin at frame 600, START at 700, full accelerator from 760), RAM dumped every
frame by both (`tools/hotest`, MAME through a Lua script), stacks and the write-only `$20C460`
excluded:

- main work RAM `$20C000-$20FFFF`: identical in every frame up to 1,631 except frame 22;
- shared RAM `$C7C000-$C7FFFF`: different in frames 1-15, identical otherwise up to 1,620;
- of frames 1,632-2,000, 62 differ in the main RAM. They come in runs of at most 4 frames, each
  followed by identical frames, not as a lasting drift.

Before the 68000 timing work the runs drifted apart for good from frame 1,602 (main RAM
identical in 1,614 of 2,000 frames, now 1,937; shared RAM 1,596, now 1,952). The main CPU's work
for a frame ends within a few cycles of VBLANK, so a small timing difference moves a task into
the next frame; what remains is not explained yet. The game logic is the original program; only
the timing of the board differs.

Screenshots at frames 100, 400, 650, 720, 800, 1000, 1300 and 1600 were pixel-identical with
MAME's. That comparison was made before the 68000 timing work and has not been repeated.

Hang-On's ending (the photo collage after the goal and bonus) was compared by eye with MAME's,
reached in both by the same shortcut (the demo rider steering in a game, the timer held, the stage
index set to 3 at the start): the same screens.

## 2. Hang-On's course in the program (`crossover/homode.*`)

The board is reset and run without display or sound through a coin (frame 601) and START (701),
then the player takes over on the start line. Hang-On's race music (`$9D`) plays unless another
track is chosen. The mode ends when `$20C408` (in a game) drops, after the ranking screen.

Rankings: Hang-On resets them at every power-on (`sub_05A7C`, from the boot code at `$1016`: 99
entries of 12 bytes at `$20D800`, the value at `$20C03C`; `sub_07E8A` from `$101A`: the lap-time
records at `$20DF90`, 16 bytes). They are saved to `hiscores_hangon.bin` when the mode ends and put
back at frame 60 of the next run-up.

## 3. Vehicles on Hang-On's course (`crossover/hovehicle.*`)

- **Hang-On's bike** (VEHICLE: HANG ON BIKE, Hang-On's course only): the game as it is.
- **OutRun's Ferrari**: the design of the Ferrari on Super Hang-On's courses (`crossover/shferrari`),
  on Hang-On's program.
- **Super Hang-On's bike**: it runs in OutRun's car slot exactly as on OutRun's courses
  (`crossover/bike`, Super Hang-On's reconstructed tasks, crash and sound board), with Hang-On's
  world in place of OutRun's.

The main CPU is hooked where Hang-On reaches its own bike:

| Hook | Hang-On | Ferrari | Super Hang-On bike |
|---|---|---|---|
| `$119A` dispatcher `jsr (a0)` (a5 = `$20C800` + 64·slot) | slot 9 speed `$8566` | OutRun's speed and distance; Hang-On's tail (below) | Super Hang-On's speed task (its own distance); Hang-On's tail |
| | slot 10 centrifugal `$879A` | skipped (OutRun's curve push) | skipped (Super Hang-On's centrifugal task) |
| | slot 34 bike `$AF10` | OutRun's car logic, 30 Hz | Super Hang-On's bike tasks, 60 Hz |
| | slots 4-7 crash `$89F2`/`$8AD0` | skipped (OutRun's crash) | skipped (Super Hang-On's crash) |
| | slot 2 sound `$8140` | runs; packet bytes 1-3 (`$20C641-$20C643`, the bike engine) cleared after it | same |
| `$4026` / `$4038` after `sub_04110` writes the bike's sprite entry | 10 bytes at `$20D400` + 10·rank, word 3 = data address | tagged; OutRun's sprites drawn there | same |
| `$A922` rival allowance (`cmp.w d0,d1`) | hit if \|dx\| ≤ allowance + 10 | widened to 11/8 (OutRun's rule for cars) | unchanged |
| `$A928` → `$A95A` rival contact | push via `$C7E088` | OutRun's skid, speed cut, spin chance | Super Hang-On's knockback |
| `$A978` → `$A97C` contact sound `$89` | Hang-On's board | OutRun's REBOUND | Super Hang-On's `$89` |
| `$B76A` → `$B76E` dust draw (task 35) | | skipped (OutRun's smoke) | runs (Super Hang-On's dust is its counterpart) |
| `$3F2E` sound queue, in the slot-2 task | `$8A-$8D` squeal / off-road | dropped | dropped (the bike's own come from its board) |

Racing = `$20C408` ≠ 0 and mode `$20C420` = 1 (start line), 2 (race) or 3 (goal).

What Hang-On reads about its vehicle is written back each frame: X `$C7E050` (kept within ±500,
Hang-On's own limit), off-road `$C7E098`, and for Super Hang-On's bike its state at the same
offsets (`$C7E054` hard brake, `$C7E056/57` lean, `$C7E058/5A` sprite position, `$C7E088` knock,
`$C7E094` crash, `$C7E0E6` squeal).

Speed task (`$8566`), as replaced:
- mode 1: nothing (the original returns at once);
- goal (`$C7E0EA` ≠ 0 and position `$C7E030` > `$1C`): speed 0, `$C7E0EC` and `$C7E0E2` = `$FFFF`
  every frame (`$8588-$8596`);
- otherwise the vehicle's speed to `$C7E078` (16.16 km/h: the HUD shows its integer part, and
  scenery and rivals approach by it) and its distance to the tail `$86F2-$8730`: `$C7E074` = distance,
  `$C7E030` += distance, odometer step = speed / 29 with the remainder in `$22(a5)`, added to
  `$C7E090` (the road's rumble-strip phase, `sub_0184A`).

Bike task (`$AF10`), as replaced: the start-line blink (`$B3F6`) and, after the goal, the exit:
`$C7E0EA` counts down by 4 per frame while the vehicle is drawn, then `$C7E0EE` = `$FFFF` (the game
flow's mode 3 waits for it at `$73EE`) and `$C7E0EA`/`$C7E0EC` are cleared (`$B380-$B3B0`).

Drawing: the bike's object stays at `$A`/`$C` = `$160`/`$DF`, depth `$14` = `$E0`; in Hang-On depth
is the screen line, and the draw list (`sub_0AD3A`: `$C7E500` count, `$C7E502` depth/slot pairs) is
sorted by task 36 (`$984A`). During OutRun's crash the depth is the screen line of the thrown car.

Joins (design decisions):
- **Road edge:** Hang-On's is 260 (`sub_0B57C`; Super Hang-On's 300). The Ferrari gets OutRun's
  wheel rules around it (OutRun road half-width `$18`); Super Hang-On's bike gets 260 as its
  off-road limit.
- **Curve push:** the Ferrari's from OutRun's road builder over Hang-On's path; the bike's from
  Super Hang-On's builder over Hang-On's path. The path is the second CPU's pointer `$7F0F0`
  (main `$C7F0F0`), in the format all three games share.
- **Road heights** for OutRun's crash, smoke and incline frames: OutRun's own flat-road heights
  (`$2400` per entry, measured on OutRun's first stage). Hang-On's road is flat.
- **Rivals:** for the Ferrari as on Super Hang-On's courses. Rival speed for the speed cut:
  `S - 1.6·d0`, `S` = `$10FA4` + (`$20C450` & 6)·8 + stage·2, `d0` = `$36(a5)` + min(`$32(a5)`/5,
  `$26`), or 10 once knocked (`$AB3C-$ABA6`).
- **Scenery:** Hang-On's own test (`$9E68-$9EAA`) decides; a hit starts the vehicle's crash.
- **Speedometer:** Hang-On's shows the speed value itself. Super Hang-On's bike tops out at 241
  without the supercharger and 279 with it; Super Hang-On's own speedometer would show those as
  280 and 324.

## 4. Tools

- `tools/hotest` (`build.sh <dir> [video]`): headless board runs with RAM dumps per frame, for
  comparison with MAME (Lua RAM dumps of the same layout), and screenshots.
- `tools/m68ktiming` (`run.sh <work dir> <MAME home>`): the 68000 instruction-timing comparison
  with MAME 0.264 (generated tests on Hang-On's sub CPU); `run_irq.sh` does the interrupt timing.
- `tools/hotest/hoexp.cpp` (built alongside): exploration runs: executed-instruction maps of both
  CPUs, the task slots per frame, the demo rider steering in a game (`autopilot`), the timer held
  (`freeze`), RAM pokes and dumps.
