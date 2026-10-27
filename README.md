# OutRun / Super Hang-On / Hang-On Crossover

**A fork of [Cannonball](https://github.com/djyt/cannonball), Chris White's OutRun engine.**

This project takes Cannonball and adds Super Hang-On (1987) and Hang-On (1985) to it. You can
ride Super Hang-On's bike on OutRun's courses, and race to the music of all three games, each
tune played on an emulated copy of its own game's sound board.

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
- **Aaron Giles**: ymfm, the YM2151/YM2203 cores in `src/main/crossover/ymfm` (BSD-3-Clause,
  the copy shipped with MAME 0.264).
- **The MAME team**: the FD1089 decryption and the Sega PCM sample generation are derived from
  MAME (BSD-3-Clause). MAME 0.264 was also the reference that every reconstruction here was
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
| Race setup menu: vehicle, track, music | Done |
| Super Hang-On's courses (with either vehicle) | In progress |
| Hang-On's course (with either vehicle) | Planned |
| Bike versions of OutRun's start and ending scenes | Planned |

---

## What this fork changes

All new code is in `src/main/crossover/`. Cannonball's own files are changed only where the
crossover has to hook in (main loop, menus, music, sprites, traffic, collisions, audio output);
`git diff` against upstream Cannonball shows every change.

- **The bike** (`crossover/bike.*`, `crossover/shophys/`): Super Hang-On's speed, steering,
  lean, centrifugal push, off-road, knockback, crash sequence and engine-sound tasks, rebuilt in C
  from the game's 68000 code. Each one was compared with MAME 0.264 frame by frame with no
  differences. OutRun's collision detection decides when the bike hits something; Super Hang-On's
  code decides what happens next.
- **Sound boards** (`crossover/soundboard.*`, `crossover/xaudio.*`): Super Hang-On's board
  (Z80, YM2151, Sega PCM) and Hang-On's board (Z80, YM2203, Sega PCM), running each game's own Z80
  sound program. Given the same commands as MAME, they make the same sound-chip register writes.
- **Graphics**: Super Hang-On's bike, rider and crash sprites are drawn through OutRun's sprite
  hardware emulation, decoded from Super Hang-On's ROMs at start-up.
- **Forks**: the bike can take either road. On the road OutRun draws mirrored, the bike's
  curve push is mirrored too, as OutRun does for the Ferrari.
- **Menu**: *PLAY GAME* opens a race setup screen (vehicle, track, music).
- **Test harness** (`crossover/harness.*`): headless, scripted runs with traces, screenshots and
  WAV output, used to check behaviour without a person at the controls.

Super Hang-On and Hang-On ROMs are optional. Without them, Cannonball runs as normal with the
Ferrari, and each missing set only removes that game's part.

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
```

### 3. Play

1. Run `cannonball.exe`.
2. Choose **PLAY GAME**. The race setup screen opens:
   - **VEHICLE**: OutRun's Ferrari or Super Hang-On's bike
   - **TRACK**: OutRun's full route (pick a road at each fork) or any single OutRun stage
   - **MUSIC**: any of OutRun's, Super Hang-On's or Hang-On's race tracks (it previews as you
     choose), or *CHOOSE ON THE RADIO* for OutRun's usual radio screen
3. Choose **START RACE**.

Up/down moves through the menu and left/right changes a setting. Your choices are saved in
`config.xml`.

### Controls (default keyboard)

| Key | Ferrari | Bike |
|---|---|---|
| Arrow keys | Steer (and menus) | Steer (and menus) |
| Z | Accelerate | Accelerate |
| X | Brake | Brake |
| Space | Gear shift | Supercharger (hold it at top speed, as in Super Hang-On) |
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

- The TRACK option lists OutRun's courses only, for now.
- OutRun's ending shows the Ferrari even when you ride the bike.
- In widescreen mode, crash objects near the screen edges vanish early, because Super Hang-On's
  sprite code clips to its own 320-pixel screen.

Bug reports are welcome in [Issues](../../issues).

---

## Licence

Cannonball's licence ([`docs/license.txt`](docs/license.txt)) applies to this fork: it is
non-commercial, redistributions may not be sold, and modified versions must be distributed with
their full source code. Each release zip therefore includes this program's source and the source
of the libraries it ships with (SDL2, the Boost headers used, winpthreads).

The Z80 core is MIT-licensed, ymfm is BSD-3-Clause, and the MAME-derived parts are BSD-3-Clause.
The licence texts are in the release zip's `licenses` folder and next to the code.

OutRun, Super Hang-On and Hang-On are trademarks of SEGA. This project is not affiliated with
SEGA.
