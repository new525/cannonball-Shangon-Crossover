# OutRun / Super Hang-On / Hang-On Crossover

**A fork of [Cannonball](https://github.com/djyt/cannonball), Chris White's OutRun engine.**

This project takes Cannonball and adds Super Hang-On (1987) and Hang-On (1985) to it. You can
ride Super Hang-On's bike on OutRun's courses, drive OutRun's Ferrari on Super Hang-On's courses,
race Hang-On's course with the Ferrari or either game's bike, and race to the music of all three
games, plus Cool Riders' cover of Hang-On's theme. Super Hang-On's and Hang-On's courses run on
emulated copies of their own boards, and each tune plays on an emulated copy of its own game's
sound board.

It is a hobby project. It isn't affiliated with or endorsed by Chris White or SEGA.

---

## Credit where it's due

- **Chris White ([djyt](https://github.com/djyt))** wrote Cannonball, rebuilding OutRun's 68000
  and Z80 code in C++. That work is the foundation of everything here: the OutRun engine,
  renderer, menus, sound and input code all come from his project. See his
  [Cannonball wiki](https://github.com/djyt/cannonball/wiki) and
  [Reassembler blog](http://reassembler.blogspot.com/). His original README is kept in
  [`docs/README-cannonball.md`](docs/README-cannonball.md).
- **Nicolas Allemand**: the Z80 core in `src/main/crossover/z80` (MIT).
- **Karl Stenerud**: Musashi, the 68000 core in `src/main/crossover/m68k` (MIT), which runs the
  emulated Super Hang-On and Hang-On boards. It bundles John R. Hauser's SoftFloat (its licence notice is in
  `m68k/softfloat/softfloat.c`).
- **Aaron Giles**: ymfm, the YM2151/YM2203 cores in `src/main/crossover/ymfm` (BSD-3-Clause,
  the copy shipped with MAME 0.264).
- **The MAME team**: the FD1089 decryption, the Sega PCM sample generation, the two boards'
  video (tilemaps, road, sprites, mixing), the SH-1 core in `src/main/crossover/sh` (MAME's
  SH-2 family interpreter: Juergen Buchmueller, R. Belmont, David Haywood, Angelo Salese) and the
  SCSP in `src/main/crossover/scsp` (ElSemi, R. Belmont, kingshriek) are derived from MAME
  (BSD-3-Clause). MAME 0.264 was also the reference that every reconstruction here was
  checked against.

---

## Goals

1. **Either game's vehicle on either game's tracks, with arcade accuracy.** Super Hang-On's bike
   on OutRun's courses, OutRun's Ferrari on Super Hang-On's courses, and Hang-On's course too.
2. **All three games' race music**, played by each game's own sound program.
3. **Arcade accuracy from the original code.** Where this fork adds behaviour from Super Hang-On
   or Hang-On, it reconstructs that game's own code and compares the result with MAME frame by
   frame. Where the two games have to be joined together (for example, which of OutRun's two
   roads the bike is on at a fork), the choice is written down as a design decision rather than
   passed off as original behaviour.

### Status

| Feature | Status |
|---|---|
| Super Hang-On's bike on OutRun's courses (physics, lean, crash, knockback, engine sound) | Done |
| Super Hang-On and Hang-On race music on emulated sound boards | Done |
| Race setup menu: vehicle, bike size, track, music | Done |
| Super Hang-On's four courses with the bike, on an emulated Super Hang-On board | Done |
| OutRun's Ferrari on Super Hang-On's courses (handling, crash, skids, smoke, engine sound) | Done |
| Hang-On's course on an emulated Hang-On board, with the Ferrari, either game's bike | Done |
| Cool Riders' cover of Hang-On's theme, on an emulated Cool Riders sound board | Done |
| Bike versions of OutRun's start and ending scenes | Planned |

---

## What this fork changes

All new code is in `src/main/crossover/`. Cannonball's own files are changed only where the
crossover has to hook in (main loop, menus, music, sprites, traffic, collisions, audio output);
`git diff` against upstream Cannonball shows every change.

- **The bike** (`crossover/bike.*`, `crossover/shophys/`): Super Hang-On's speed, steering,
  lean, centrifugal push, off-road, knockback, crash sequence and engine-sound tasks, rebuilt in C
  from the game's 68000 code. Each one was compared with MAME 0.264 frame by frame with no
  differences.
- **Collisions**: OutRun decides when a car or roadside object is level with the bike. Super
  Hang-On's rules decide how close counts as a hit: the bike's centre line has to reach a
  roadside object's outline (OutRun's own hit area for that object, but never wider than the
  object as drawn), or come within 10 pixels of a car's outline. Those 10 pixels shrink with
  BIKE SIZE. Super Hang-On's code then decides what happens next.
- **Sound boards** (`crossover/soundboard.*`, `crossover/xaudio.*`): Super Hang-On's board
  (Z80, YM2151, Sega PCM) and Hang-On's board (Z80, YM2203, Sega PCM), running each game's own Z80
  sound program. Given the same commands as MAME, they make the same sound-chip register writes.
- **Graphics**: Super Hang-On's bike, rider and crash sprites are drawn through OutRun's sprite
  hardware emulation, decoded from Super Hang-On's ROMs at start-up.
- **Forks**: the bike can take either road. OutRun draws the right-hand road as a mirror image
  of the left-hand one, so the bike's curve push is mirrored there, as OutRun does for the
  Ferrari.
- **Super Hang-On's courses** (`crossover/shboard.*`, `crossover/shvideo.*`, `crossover/shmode.*`):
  Super Hang-On runs on an emulated copy of its own board: both 68000 CPUs (Musashi), the
  315-5195 memory mapper, the I/O, the road chip, tilemaps and System 16B sprites, and its sound
  board. Compared with MAME 0.264 over a scripted game, the main CPU's RAM matched in 1,208 of 1,210
  frames (frames 10 and 11 differed, then matched again) and the second CPU's in 1,205 (frames 8,
  10, 11, 802 and 803), and screenshots taken during play were pixel-identical. The program goes through the game's own class and music
  screens for you, then hands over at the start line. In widescreen (as Cannonball widens
  OutRun) the road, scenery and riders fill the wider screen and the text stays in the middle.
  Super Hang-On resets its rankings at every power-on; the program keeps them between sessions
  in `hiscores_superhangon.bin`, as Cannonball keeps OutRun's in `hiscores.xml`.
- **The Ferrari on Super Hang-On's courses** (`crossover/shferrari.*`): the bike's mirror image.
  Super Hang-On's program runs the world (road, scenery, rivals, timer, checkpoints, score,
  rankings), and Cannonball's own OutRun code drives the car: speed and gears, steering, curve
  push, off-road, skids, the crash with its flips and thrown passengers, smoke, shadow, and the
  engine and tyre sounds from OutRun's sound program. Where Super Hang-On's main CPU reaches its
  own bike's code, the program runs the Ferrari's instead and hands the results back (position on
  the road, speed, distance). The car and everything that belongs to it are drawn with OutRun's
  sprite code and colours at the bike's place in Super Hang-On's depth-sorted sprite list, so
  objects nearer to the camera still pass in front of it. The joins, as design decisions:
  - Road edges: OutRun's wheel rules are applied to Super Hang-On's road edge (300 units from
    the centre), so one wheel leaves the road 24 units before it and both 24 units after it,
    and the car can wander as far past the edge as OutRun allows (564 units from the centre).
  - Curve push: OutRun's own push, from OutRun's road builder run over Super Hang-On's road path
    (as the bike uses Super Hang-On's builder on OutRun's path).
  - Distance: OutRun's (303 per km/h per 30 Hz tick, with its correction for the line taken
    through a curve); Super Hang-On's own course logic then takes it from there.
  - Rivals: they count as OutRun traffic. A rival is hit within 0.6875 of its width either side
    of the car's centre (OutRun's rule for cars; the allowance Super Hang-On uses is measured to
    be half the rival's drawn width). The response is OutRun's: a skid away from the rival, speed
    cut to the rival's speed less 80, and a chance of a spin, using OutRun's value for cars
    rather than lorries.
  - Roadside objects: Super Hang-On's own test decides (the car's centre line against the
    object's drawn outline). OutRun's hit areas are per-object data that Super Hang-On's objects
    don't have. A hit starts OutRun's crash.
  - Speedometer: Super Hang-On's shows its speed value multiplied by 279/240 (its 240 reads
    280 km/h), so the Ferrari's top speed of 293 shows as 340 km/h there. The motion matches
    OutRun's (distance per speed unit is the same in both games, within 0.4%).
  - Off-road dust and wheel smoke use OutRun's first-stage colours. There is no start-line rev
    effect, because in OutRun that comes with its own start signal.
- **Hang-On's course** (`crossover/hoboard.*`, `crossover/hovideo.*`, `crossover/homode.*`):
  Hang-On runs on an emulated copy of its own board: both 68000 CPUs, the two 8255s, the ADC,
  the road, tilemaps, sprites and its sound board. Compared with MAME 0.264 over a scripted game,
  the main CPU's RAM matched in every frame up to 1,631 except frame 22, and in 1,937 of 2,000
  frames in all. From frame 1,632 short runs of frames (at most 4) differ, then match again: the
  game's work for a frame ends within a few cycles of the vertical blank, so a small timing
  difference can move a task into the next frame. The logic is the game's own program
  throughout. Hang-On's rankings and lap records are kept in `hiscores_hangon.bin`.
  The details, with every Hang-On address the code relies on, are in
  [`docs/CROSSOVER-HANGON.md`](docs/CROSSOVER-HANGON.md).
- **Vehicles on Hang-On's course** (`crossover/hovehicle.*`): Hang-On's own bike (the game as it
  is), OutRun's Ferrari (as on Super Hang-On's courses), or Super Hang-On's bike (as on OutRun's
  courses, in OutRun's car slot, with its own physics, crash, supercharger and engine sound). The
  joins, as design decisions:
  - Road edge: Hang-On's is 260 units from the centre (Super Hang-On's is 300). The Ferrari gets
    OutRun's wheel rules around it; the bike gets it as its off-road limit.
  - Curve push: each vehicle's own road builder (OutRun's for the Ferrari, Super Hang-On's for the
    bike) run over Hang-On's road path, which has the same format in all three games.
  - Rivals: for the Ferrari as on Super Hang-On's courses; for the bike, Super Hang-On's knockback
    (Hang-On's is the same).
  - Roadside objects: Hang-On's own test decides; a hit starts the vehicle's own crash.
  - The bike's dust is drawn by Hang-On's dust code, the counterpart of Super Hang-On's.
  - The Ferrari's crash, smoke and incline frames stand on OutRun's own flat-road heights.
  - Speedometer: Hang-On's shows the speed value itself, so Super Hang-On's bike reads 241 at top
    speed and 279 with the supercharger (280 and 324 on Super Hang-On's speedometer).
- **Cool Riders' cover of Hang-On's theme** (`crossover/crsound.*`, `crossover/crmusic.*`):
  *HANG ON -THEME OF LOVE-* from Cool Riders (1995), played by its own sound program on an
  emulated copy of the sound side of its System H1 board: the SH-1 that streams the samples, the
  68000 and the two Yamaha SCSPs. Cool Riders' main CPU isn't emulated; the program writes the
  sound commands as its main program does. Compared with MAME 0.264 over the whole song, the
  notes, loudness and tempo match: 5,542 notes started against MAME's 5,541, no tempo drift,
  and notes within about 4 ms of MAME's.
  The details are in [`docs/CROSSOVER-COOLRIDERS.md`](docs/CROSSOVER-COOLRIDERS.md), together
  with why Cool Riders' courses and bikes are not in (yet).
- **68000 timing**: Musashi now takes the same number of cycles as MAME 0.264's 68000 on every
  instruction tested (104,854 cases) and takes interrupts at the same points. This brought the
  emulated Hang-On board much closer to MAME (see *Hang-On's course* above).
- **Menu**: *PLAY GAME* opens a race setup screen (vehicle, bike size, track, music).
- **Test harness** (`crossover/harness.*`): headless, scripted runs with traces, screenshots and
  WAV output, used to check behaviour without a person at the controls.

Super Hang-On, Hang-On and Cool Riders ROMs are optional. Without them, Cannonball runs as normal
with the Ferrari, and each missing set only removes that game's part.

---

## Installing (Windows, 64-bit)

### 1. Download

Download the latest `superscaler-crossover-<version>-win64.zip` from this repository's
[Releases](../../releases) page and extract it to a folder of your choice.

### 2. Add the ROMs

No game ROMs are included. You need your own copies, as zip-extracted files from these MAME sets:

| Game | MAME set | Put the files in |
|---|---|---|
| OutRun | `outrun` (Revision B) | `roms\` |
| Super Hang-On | `shangon1` (mini ride-on, FD1089B 317-0034), including `317-0034.key` | `roms\shangon1\` |
| Hang-On | `hangon` (Rev A) | `roms\hangon\` |
| Cool Riders (music only) | `coolridr`: 13 of its files, without the ten 4 MB `mpr-176xx` sprite ROMs | `roms\coolridr\` |

Each folder has a `roms.txt` listing the exact files and their CRC32s. Files are recognised by
CRC32 first and by name second, so they don't need to be renamed.

The folder should end up like this:

```
cannonball.exe
config.xml
SDL2.dll
libwinpthread-1.dll
res\
roms\            epr-10380b.133, epr-10382b.118, ... (OutRun)
roms\shangon1\   epr-10636.133, ..., 317-0034.key   (Super Hang-On)
roms\hangon\     epr-6918a.ic22, ...                (Hang-On)
roms\coolridr\   ep17659.30, ..., ep17662.12        (Cool Riders)
```

### 3. Play

1. Run `cannonball.exe`.
2. Choose **PLAY GAME**. The race setup screen opens:
   - **VEHICLE**: OutRun's Ferrari, Super Hang-On's bike, or *HANG ON BIKE*, Hang-On's own bike,
     which is offered for Hang-On's course only
   - **BIKE SIZE**: 80 percent by default; anything from 50 percent up to *FULL SIZE* (as in
     Super Hang-On) in steps of 10. Super Hang-On's bike takes up much more of the screen than
     OutRun's Ferrari, and a smaller bike shows more of the road ahead. It applies on OutRun's
     courses only; on Super Hang-On's and Hang-On's courses the bike is always full size. The
     riding is the same at every size; the bike's reach in collisions with cars shrinks with it
     (see *Collisions* above).
   - **TRACK**: OutRun's full route (pick a road at each fork), any single OutRun stage (raced
     with the time-trial rules; laps and traffic as set under GAME MODES > TIME TRIAL), or one
     of Super Hang-On's courses: *SUPER HANG ON AFRICA* (Beginner, 6 stages), *ASIA* (Junior,
     10), *AMERICA* (Senior, 14) or *EUROPE* (Expert, 18), with either vehicle, or Hang-On's
     course, *HANG ON*, with any of the three.
   - **MUSIC**: any of OutRun's, Super Hang-On's or Hang-On's race tracks, or Cool Riders'
     *HANG ON THEME OF LOVE* (it previews as you choose; Cool Riders' board takes about half a
     second to start the first time), or *CHOOSE ON THE RADIO* for OutRun's usual radio screen.
     On Super Hang-On's courses, *CHOOSE ON THE RADIO* takes you to Super Hang-On's own music
     screen instead (choose with the brake, confirm with START); on Hang-On's course it plays
     Hang-On's own race music. The music chosen here also plays in the ENHANCED, ORIGINAL and
     CONTINUOUS game modes, which then skip the radio.
3. Choose **START RACE**.

Up/down moves through the menu and left/right changes a setting. Your choices are saved in
`config.xml` (section `<crossover>`, which also holds the three ROM folders; the music is saved by
its title, so it survives ROM sets coming and going). **CLEAR HISCORES** under SETTINGS clears
Super Hang-On's and Hang-On's rankings along with OutRun's. On Super Hang-On's and Hang-On's courses the game is that game's own, rankings
included; when it ends you're back in the menu. The menu key leaves at any time.

### Controls (default keyboard)

| Key | Ferrari | Bike |
|---|---|---|
| Arrow keys | Steer (and menus) | Steer (and menus) |
| Z | Accelerate | Accelerate |
| X | Brake | Brake |
| Space | Gear shift (on any course) | Supercharger (hold it at top speed, as in Super Hang-On) |
| 5 | Insert coin | Insert coin |
| 1 | Start | Start |

Keys can be changed under **SETTINGS > CONTROLS > REDEFINE KEYS**. Gamepads and analog wheels
work as in Cannonball.

---

## Building from source

The code is C++11 with SDL2 and the Boost headers, built with CMake, like Cannonball.

### Windows build, cross-compiled from Linux (how the releases are made)

Requirements: MinGW-w64 with POSIX threads (`x86_64-w64-mingw32-g++-posix`), the SDL2 MinGW
development package, and the Boost headers.

```
mkdir build-win && cd build-win
cmake ../cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw64-toolchain.cmake -DTARGET=mingw64.cmake \
      -DSDL2_ROOT=<SDL2-devel-mingw>/x86_64-w64-mingw32 -DBOOST_INC=<folder containing boost/>
make -j8
```

Put `SDL2.dll` and `libwinpthread-1.dll` next to `cannonball.exe`, with `res\` and `roms\` as
above.

### Linux build

Plain SDL2 rendering, without OpenGL. This is the build the automated tests use.

```
sudo apt install build-essential cmake libsdl2-dev libboost-dev
mkdir build-linux && cd build-linux
cmake ../cmake -DTARGET=linux-sdl.cmake
make -j8
```

### Other platforms

Cannonball's own instructions (Visual Studio, macOS, Raspberry Pi) are in
[`docs/README-cannonball.md`](docs/README-cannonball.md). They should still apply, but this fork
has only been built and tested on Windows (MinGW-w64) and Linux.

---

## Known issues

- OutRun's ending shows the Ferrari even when you ride the bike.
- In widescreen mode, crash objects near the screen edges vanish early, because Super Hang-On's
  sprite code clips to its own 320-pixel screen.
- On Super Hang-On's courses, the Ferrari's flying wreck and passengers are drawn at one depth in
  Super Hang-On's sprite order (the wreck's), so a roadside object between them and the camera
  can be drawn behind a passenger. They are also not hidden behind hill crests.
- On Hang-On's course the Ferrari's wreck, if thrown far up the road, stops at OutRun's flat-road
  horizon (screen line 151), below Hang-On's (line 128).
- The emulated Hang-On board's RAM differs from MAME's in short runs of frames after about 1,630
  frames of a game (see *Hang-On's course* above).
- Cool Riders' sound board: the 68000 has to repeat its interrupt to the SH-1 more often than in
  MAME before the SH-1 answers, and notes come up to about 4 ms earlier than in MAME. The cause
  hasn't been found (see [`docs/CROSSOVER-COOLRIDERS.md`](docs/CROSSOVER-COOLRIDERS.md)).

Bug reports are welcome in [Issues](../../issues).

---

## Licence

Cannonball's licence ([`docs/license.txt`](docs/license.txt)) applies to this fork: it is
non-commercial, redistributions may not be sold, and modified versions must be distributed with
their full source code. Each release zip therefore includes this program's source and the source
of the libraries it ships with (SDL2, the Boost headers used, winpthreads).

The Z80 core and Musashi are MIT-licensed, ymfm is BSD-3-Clause, and the MAME-derived parts are
BSD-3-Clause; SoftFloat (bundled with Musashi) has its own permissive notice.
The licence texts are in the release zip's `licenses` folder and next to the code.

OutRun, Super Hang-On, Hang-On and Cool Riders are trademarks of SEGA. This project is not affiliated with
SEGA.
