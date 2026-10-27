OutRun / Super Hang-On / Hang-On crossover - version 0.2.2 (Windows, 64-bit)
==============================================================================

A modified build of Cannonball (the OutRun engine by Chris White) that lets
Super Hang-On's bike ride OutRun's courses, and adds Super Hang-On's and
Hang-On's race music. Non-commercial; see "Licences" below.

No game ROMs are included. You need your own copies.

Project page and source: https://github.com/new525/cannonball-Shangon-Crossover
Based on Cannonball by Chris White: https://github.com/djyt/cannonball


Changes in 0.2.2
----------------
- New race setup option BIKE SIZE: full size, or 90 down to 50 percent.
  Super Hang-On's bike takes up much more of the screen than OutRun's
  Ferrari, so a smaller bike shows more of the road ahead. The crashing
  bike and rider are scaled to match. Only the drawing changes: the
  riding and the collisions are the same at every size.

Changes in 0.2.1
----------------
- Forks: on the right-hand road, which OutRun draws as a mirror image of
  the left-hand one, the bike was pushed toward the inside of each curve.
  Its curve push is now mirrored there, as OutRun does for the Ferrari.


Setting up
----------
1. Put the OutRun Revision B ROMs in   roms\              (list: roms\roms.txt)
2. Put the Super Hang-On ROMs in       roms\shangon1\     (list: roms\shangon1\roms.txt)
   These are the files of MAME's "shangon1" set, including 317-0034.key.
3. Put the Hang-On ROMs in             roms\hangon\       (list: roms\hangon\roms.txt)
   These are the files of MAME's "hangon" set (Rev A).
4. Run cannonball.exe.

The crossover ROM sets are optional: without Super Hang-On's ROMs you drive
the Ferrari, and each missing set only removes that game's music.
ROM files are recognised by CRC32 first, then by name, so the names don't
have to match exactly.


Race setup menu
---------------
Main menu > PLAY GAME opens the race setup:
    START RACE
    VEHICLE   OutRun's Ferrari or Super Hang-On's bike
    BIKE SIZE full size, or 90 down to 50 percent (see above)
    TRACK     OutRun's full route (you choose at each fork), or any single
              OutRun stage (1, 2A-2B, 3A-3C, 4A-4D, 5A-5E; letters follow
              the order of Cannonball's time-trial course map). A single
              stage is raced with the time-trial rules; laps and traffic are
              set under GAME MODES > TIME TRIAL.
    MUSIC     any of the eight tracks, previewed as you choose, or
              "CHOOSE ON THE RADIO" for OutRun's in-car radio screen.
Up/down moves, left/right (or select) changes a setting. Your choices are
saved in config.xml (section <crossover>).


Playing with the bike
---------------------
- Steering, accelerator and brake work as for the Ferrari.
- The gear button is the bike's supercharger (Super Hang-On's turbo
  button); hold it at top speed.
- Hitting roadside objects starts Super Hang-On's crash. Hitting cars knocks
  you sideways the way Super Hang-On does.
- Forks: the bike can take either road.


Music
-----
On the music selection screen, steer (or use left/right) through the list:
OutRun's three tracks, then Super Hang-On's Outride a Crisis, Sprinter,
Winning Run and Hard Road, then Hang-On's race music ("HANG ON"). Each one
plays on an emulated copy of its own game's sound board.


What is reconstructed, and how it was checked
---------------------------------------------
- Bike physics (speed, steering, lean, centrifugal push, off-road, knockback),
  the crash sequence and the bike's sound data are C reconstructions of
  Super Hang-On's own code. Each was compared with MAME 0.264 frame by
  frame and showed no differences.
- Sound boards: each game's own Z80 sound program runs on an emulated Z80
  with the YM2151/YM2203 cores MAME 0.264 uses (ymfm) and MAME's Sega PCM.
  Fed the same command stream as MAME, both boards make the same
  sound-chip register writes as MAME. The rest differ only because of
  sub-millisecond timing.
- Units: Super Hang-On's and OutRun's speed, position and steering units
  were matched from both games' code.

Design choices (not original behaviour of either game):
- OutRun's collision detection decides when the bike hits something;
  Super Hang-On's code decides what happens.
- The crashed bike and rider are placed on OutRun's road at the depth
  matching Super Hang-On's, so they follow OutRun's hills.
- Scoring is OutRun's.


Not done yet
------------
- Super Hang-On's courses and the Hang-On course (the TRACK option lists
  OutRun's courses only).
- Bike versions of OutRun's start and ending scenes (the ending shows the
  Ferrari).
- In widescreen mode, crash objects near the screen edges are hidden early.
  Super Hang-On's sprite code clips to its own 320-pixel screen.


Licences
--------
Cannonball: licenses\cannonball-license.txt. Redistributions may not be sold,
and modified redistributions must include complete source code, so the
source folder contains:
  superscaler-crossover-0.2.2-src.zip     this program (git tag v0.2.2,
                                          branch "crossover")
  SDL2-2.30.8.tar.gz                      SDL2 (zlib licence)
  boost-1.83-headers-used.tar.gz          the Boost headers compiled in
                                          (Boost Software License 1.0)
  mingw-w64-v11.0.1-winpthreads.tar.gz    libwinpthread-1.dll's source
                                          (upstream v11.0.1)
Other components: Z80 core by Nicolas Allemand (MIT), ymfm by Aaron Giles
(BSD-3-Clause, as shipped in MAME 0.264), and two parts derived from MAME
(BSD-3-Clause): the FD1089 decryption and the Sega PCM sample generation.
Licence texts are in the licenses folder.

OutRun, Super Hang-On and Hang-On are trademarks of SEGA. This project is
not affiliated with SEGA.
