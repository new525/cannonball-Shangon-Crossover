OutRun / Super Hang-On / Hang-On crossover - version 0.6.1 (Windows, 64-bit)
==============================================================================

A modified build of Cannonball (the OutRun engine by Chris White) that lets
Super Hang-On's bike ride OutRun's courses, OutRun's Ferrari drive Super
Hang-On's own courses (on an emulated Super Hang-On board), and either of
them, or Hang-On's own bike, race Hang-On's course (on an emulated Hang-On
board), with all three games' race music, Cool Riders' cover of Hang-On's
theme and OutRunners' covers of OutRun's music. Non-commercial; see
"Licences" below.

No game ROMs are included. You need your own copies.

Project page and source: https://github.com/new525/cannonball-Shangon-Crossover
Based on Cannonball by Chris White: https://github.com/djyt/cannonball


Changes in the next version
---------------------------
- OutRunners' covers of Magical Sound Shower, Passing Breeze, Splash Wave
  and Last Wave join the music selection, played by OutRunners' own sound
  program on an emulated copy of its sound board (Z80, YM3438, MultiPCM).
  Only 3 files of MAME's "orunners" set are needed (roms\orunners\roms.txt).
  Each note of Magical Sound Shower starts within 1.6 ms of MAME's.
- Cool Riders' HANG ON THEME OF LOVE is now as loud as OutRun's own tracks
  (it was about a third of their level).
- The race setup menu's minicar no longer jumps to the wrong row when a
  setting's text is too long for the line: a long music title is shown
  without the "MUSIC" label (the same for the sound test's track names).
- OutRunners' courses and cars were looked into and not done; see
  docs\CROSSOVER-OUTRUNNERS.md in the source.


Changes in 0.6.1
----------------
- Cool Riders' board closer to MAME: the scanline interrupt's phase, the
  time seen in timer callbacks, the order of a CPU held in reset and the
  68000 stopping at the bus cycle that changes an interrupt line now follow
  MAME's scheduler. Each note of the song now starts within a fraction of a
  millisecond of MAME's (median 0.03 ms; before, 2-4 ms early).
- The 68000 core measured an instruction's length wrongly when a bus access
  in its middle ended the time slice, which could delay an interrupt by one
  instruction on all three emulated boards.
- Super Hang-On's and Hang-On's sound boards ran 0.008 percent fast (the
  Z80's last instruction of each frame was added to the next frame); their
  sound was 9 ms ahead after two minutes.
- The sound boards' conversion to the output rate now holds a steady backlog
  at any sample rate (at 22,050 Hz the music would have lagged more and
  more), and a FRAME RATE change in the menu no longer breaks their sound.
- The MUSIC choice is saved by its title: adding or removing a ROM set no
  longer makes a different track play. A saved vehicle or track whose ROM set
  is gone falls back to the Ferrari / OutRun's route at start-up.
- CLEAR HISCORES now also clears Super Hang-On's and Hang-On's rankings, and
  reports "SCORES CLEARED" when it clears anything (it always said "NO SAVED
  SCORES FOUND").
- config.xml now ships with the <crossover> section (ROM folders included).
- The build sets C++17 in CMake (the code needed it; the compiler's default
  happened to provide it).


Changes in 0.6.0
----------------
- Cool Riders' cover of Hang-On's theme, "HANG ON -THEME OF LOVE-", is in
  the music list (HANG ON THEME OF LOVE). It plays on an emulated copy of
  the sound side of Cool Riders' System H1 board (the SH-1 that streams the
  samples, the 68000 and two Yamaha SCSPs), running Cool Riders' own sound
  program. Compared with MAME 0.264 over the whole song: 5,542 notes started
  against MAME's 5,541, no tempo drift, notes within about 4 ms of MAME's.
  Needs 13 files of MAME's "coolridr" set in roms\coolridr\ (the ten 4 MB
  sprite ROMs are not needed). The first time it is chosen, the board takes
  about half a second to start.
- The 68000 core (Musashi) now takes the same number of cycles as MAME
  0.264's 68000 on every instruction tested (104,854 cases) and takes
  interrupts at the same points. The emulated Hang-On board's RAM now
  matches MAME's in 1,937 of 2,000 frames of a scripted game (1,614 before).


Changes in 0.5.0
----------------
- Hang-On's course is in the TRACK option (HANG ON). Like Super Hang-On's
  courses it runs on an emulated copy of its own game's board: both 68000
  CPUs, the two 8255s, the ADC, road, tilemaps, sprites and sound board,
  so the course, scenery, rival riders, timer, checkpoints, score, ending
  and rankings are Hang-On's own. The program puts in a coin and presses
  START for you and hands over at the start line; when the game ends you
  are back in the menu.
- Checked against MAME 0.264 over a scripted game: the main CPU's RAM
  matched in every frame up to 1,601 except two (22 and 414), and
  screenshots at eight points up to frame 1,600 were pixel-identical. After
  that the two runs drift apart: the game's work for a frame ends within a
  few CPU cycles of the vertical blank, and the 68000 core used here is not
  cycle-exact like MAME's, so a task can land one frame later. The game's
  logic is the original program throughout.
- VEHICLE on Hang-On's course: OutRun's Ferrari (as on Super Hang-On's
  courses), Super Hang-On's bike (as on OutRun's courses: its own physics,
  crash, supercharger and engine sound, drawn full size), or the new
  HANG ON BIKE, Hang-On's own bike, which is offered for its own course only.
- Joins on Hang-On's course (design choices): Hang-On's road edge (260 units
  from the centre; Super Hang-On's is 300) with OutRun's wheel rules for the
  Ferrari and as the bike's off-road limit; the curve push from each
  vehicle's own road builder run over Hang-On's road path; rivals for the
  Ferrari as on Super Hang-On's courses, for the bike Super Hang-On's
  knockback; roadside objects use Hang-On's own test and start the
  vehicle's own crash; the bike's dust is drawn by Hang-On's dust code,
  which is the counterpart of Super Hang-On's.
- Hang-On's speedometer shows the speed value itself: Super Hang-On's bike
  reads 241 at top speed and 279 with the supercharger (280 and 324 on Super
  Hang-On's own speedometer).
- Hang-On's rankings and lap records are kept between sessions in
  hiscores_hangon.bin (save folder), as Super Hang-On's are.
- Widescreen on Hang-On's course: the road is now drawn correctly far to
  the side of the screen (it was cut off when the vehicle was far to the
  right in a curve).

Changes in 0.4.0
----------------
- OutRun's Ferrari drives Super Hang-On's four courses (VEHICLE: OUTRUN FERRARI,
  TRACK: AFRICA, ASIA, AMERICA or EUROPE). Super Hang-On's program runs the
  world: road, scenery, rival riders, timer, checkpoints, score, rankings.
  Cannonball's own OutRun code drives the car: speed and gears, steering,
  curve push, off-road, skids and spins, the crash with its flips and
  thrown passengers, smoke and shadow, and the engine and tyre sounds from
  OutRun's sound program (Super Hang-On's bike engine is silenced). The car
  is drawn with OutRun's sprites and colours at the bike's place in Super
  Hang-On's depth-sorted sprite list.
- How the two games are joined (design choices, see "What is reconstructed"):
  OutRun's wheel rules on Super Hang-On's road edge; OutRun's curve push
  from OutRun's road builder run over Super Hang-On's road; OutRun's
  distance per speed unit; rivals count as OutRun traffic (hit within 0.6875
  of a rival's width, OutRun's skid, speed cut and spin chance for cars);
  roadside objects use Super Hang-On's own test and start OutRun's crash.
- Super Hang-On's speedometer shows its speed value times 279/240, so the
  Ferrari's top speed of 293 reads 340 km/h there. The car moves exactly as
  fast as in OutRun.
- On Super Hang-On's courses OutRun's pedal handling now runs at OutRun's
  30 Hz rate at every frame-rate setting, as it does on OutRun's courses
  (at 60 fps it ran twice as often before).

Changes in 0.3.1
----------------
- Widescreen on Super Hang-On's courses, done the way Cannonball widens
  OutRun (the widescreen setting, which GAME MODES > ENHANCED turns on):
  road, scenery, background and riders fill the wider screen; the score
  and timer text stays in the middle. Super Hang-On drops sprites that
  leave its own 320-pixel screen; in widescreen that limit moves out to the
  wider edges.
- Super Hang-On's high scores now last between sessions. The game itself
  resets its rankings at every power-on; the program keeps them in
  hiscores_superhangon.bin (save folder) and puts them back after the
  game has started.

Changes in 0.3.0
----------------
- Super Hang-On's four courses are in the TRACK option: AFRICA (Beginner,
  6 stages), ASIA (Junior, 10), AMERICA (Senior, 14) and EUROPE (Expert,
  18). They run on an emulated copy of Super Hang-On's own board (both
  68000 CPUs, memory mapper, I/O, road, tilemaps, sprites and sound board),
  so the course, scenery, rival riders, timing and rankings are Super
  Hang-On's own. The program goes through the game's class and music
  screens for you and hands over at the start line; when the game ends you
  are back in the menu. The menu key leaves at any time.
- Checked against MAME 0.264 over a scripted game: the game's RAM matched
  in 1,208 of 1,210 frames (the other two differed for one frame each, then
  matched again), and screenshots taken during play were pixel-identical.
- MUSIC on these courses: Super Hang-On's tracks are chosen on its own
  music screen; another game's track replaces Super Hang-On's race music;
  CHOOSE ON THE RADIO shows Super Hang-On's own music screen (brake to
  choose, START to confirm).
- These courses need the bike for now (the Ferrari arrived in 0.4.0).

Changes in 0.2.3
----------------
- BIKE SIZE now defaults to 80 percent. All sizes are still selectable.
- Collisions use Super Hang-On's rules for the bike. OutRun still decides
  when a car or roadside object is level with the bike; Super Hang-On's
  rules decide how close counts as a hit. A roadside object hits when the
  bike's centre line reaches its outline (OutRun's own hit area for the
  object, but never wider than the object as drawn: OutRun's areas reach
  up to 18 pixels past poles and signs). A car hits when the bike's centre
  comes within 10 pixels of its outline (OutRun allowed 0.19 x the car's
  width). The 10 pixels shrink with BIKE SIZE, e.g. 8 at 80 percent.

Changes in 0.2.2
----------------
- The bike is now drawn at 60 percent of Super Hang-On's size by default,
  so it covers less of the road ahead. The new race setup option BIKE SIZE
  goes from 50 percent up to FULL SIZE (the original) in steps of 10. The
  crashing bike and rider are scaled to match. This applies on OutRun's
  courses only; on Super Hang-On's own courses (once added) the bike will
  always be full size. Only the drawing changes: the riding is the same at
  every size.

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
4. Put the Cool Riders ROMs in         roms\coolridr\     (list: roms\coolridr\roms.txt)
   13 files of MAME's "coolridr" set; only its music is used.
5. Put the OutRunners ROMs in          roms\orunners\     (list: roms\orunners\roms.txt)
   3 files of MAME's "orunners" set (or orunnersu, orunnersj); only its
   music is used.
6. Run cannonball.exe.

The crossover ROM sets are optional: without Super Hang-On's ROMs you drive
the Ferrari, and each missing set only removes that game's part (its
courses, vehicle and music; for Cool Riders and OutRunners, their tracks).
ROM files are recognised by CRC32 first, then by name, so the names don't
have to match exactly.


Race setup menu
---------------
Main menu > PLAY GAME opens the race setup:
    START RACE
    VEHICLE   OutRun's Ferrari, Super Hang-On's bike, or Hang-On's bike
              (HANG ON BIKE: Hang-On's course only)
    BIKE SIZE 80 percent (default), 50 to 90 percent, or FULL SIZE
              (OutRun's courses; the bike is full size on the others)
    TRACK     OutRun's full route (you choose at each fork), or any single
              OutRun stage (1, 2A-2B, 3A-3C, 4A-4D, 5A-5E; letters follow
              the order of Cannonball's time-trial course map). A single
              stage is raced with the time-trial rules; laps and traffic are
              set under GAME MODES > TIME TRIAL. Or one of Super Hang-On's
              courses: AFRICA, ASIA, AMERICA, EUROPE (either vehicle), or
              Hang-On's course: HANG ON (any of the three vehicles).
    MUSIC     any of the tracks below (Music), previewed as you choose, or
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
Winning Run and Hard Road, then Hang-On's race music ("HANG ON"), then Cool
Riders' cover of it ("HANG ON THEME OF LOVE"), then OutRunners' covers of
Magical Sound Shower, Passing Breeze, Splash Wave and Last Wave. Each one
plays on an emulated copy of its own game's sound board. The covers' levels
are matched to OutRun's own tracks (a design choice: there is no arcade
reference for levels between different games' boards).


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
- OutRun decides when an object is level with the bike; Super Hang-On's
  rules decide how close counts as a hit, and its code what happens next.
- The crashed bike and rider are placed on OutRun's road at the depth
  matching Super Hang-On's, so they follow OutRun's hills.
- Scoring is OutRun's.
- The Ferrari on Super Hang-On's courses: Super Hang-On's road edge (300
  units from the centre) takes the place of OutRun's, with OutRun's wheel
  rules around it (one wheel off 24 units before it, both 24 units after;
  the car can go as far past it as OutRun allows, 564 units from the
  centre). The curve push is OutRun's, from OutRun's road builder run over
  Super Hang-On's road path. Rivals are OutRun traffic: the allowance Super
  Hang-On uses for them was measured to be half the rival's drawn width, and
  the Ferrari hits within 11/8 of it (OutRun's 0.6875 of a car's width);
  OutRun's response follows, with the spin chance OutRun gives cars (1)
  rather than lorries (3). Roadside objects use Super Hang-On's own test
  (OutRun's hit areas are per-object data its objects don't have). The
  car's flying wreck and passengers are drawn at the wreck's depth in Super
  Hang-On's sprite order. Smoke colours are OutRun's first stage's.
- On Hang-On's course: as above, with Hang-On's road edge (260 units); the
  Ferrari's crash, smoke and incline frames stand on OutRun's own flat-road
  heights (Hang-On's road is flat). The addresses and hooks are listed in
  docs\CROSSOVER-HANGON.md in the source.


Not done yet
------------
- Bike versions of OutRun's start and ending scenes (the ending shows the
  Ferrari).
- In widescreen mode, crash objects near the screen edges are hidden early.
  Super Hang-On's sprite code clips to its own 320-pixel screen.
- On Super Hang-On's courses the Ferrari's wreck and passengers are not
  hidden behind hill crests, and share one depth in the sprite order.
- On Hang-On's course the Ferrari's wreck, when thrown far up the road,
  stops at OutRun's flat-road horizon (line 151), below Hang-On's (line 128).
- The emulated Hang-On board's RAM differs from MAME's in short runs of
  frames after about 1,630 frames of a game; the logic is the game's own.
- Cool Riders' sound board: its 68000 has to repeat its interrupt to the
  SH-1 more often than in MAME before the SH-1 answers. It doesn't affect
  the music; the likely cause is that MAME's scheduler also runs the game's
  main CPU, which this board leaves out.


Licences
--------
Cannonball: licenses\cannonball-license.txt. Redistributions may not be sold,
and modified redistributions must include complete source code, so the
source folder contains:
  superscaler-crossover-0.6.1-src.zip     this program (git tag v0.6.1,
                                          branch "crossover")
  SDL2-2.30.8.tar.gz                      SDL2 (zlib licence)
  boost-1.83-headers-used.tar.gz          the Boost headers compiled in
                                          (Boost Software License 1.0)
  mingw-w64-v11.0.1-winpthreads.tar.gz    libwinpthread-1.dll's source
                                          (upstream v11.0.1)
Other components: Z80 core by Nicolas Allemand (MIT), 68000 core Musashi by
Karl Stenerud (MIT; it bundles John R. Hauser's SoftFloat), ymfm by Aaron Giles
(BSD-3-Clause, as shipped in MAME 0.264), and parts derived from MAME
(BSD-3-Clause): the FD1089 decryption, the Sega PCM sample generation, the
Super Hang-On and Hang-On boards' video (tilemaps, road, sprites and
mixing, transcribed from MAME's drivers), the SH-1 core (MAME's SH-2 family
interpreter), the SCSP sound chip, the MultiPCM sound chip (Miguel Angel
Horna) and the System Multi 32 sound board's wiring (MAME's segas32 driver,
Aaron Giles).
Licence texts are in the licenses folder.

OutRun, Super Hang-On, Hang-On, Cool Riders and OutRunners are trademarks of
SEGA. This project is not affiliated with SEGA.
