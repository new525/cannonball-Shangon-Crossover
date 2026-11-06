OutRun / Super Hang-On / Hang-On crossover - version 0.10.0 (Windows, 64-bit)
==============================================================================

A modified build of Cannonball (the OutRun engine by Chris White) that lets
Super Hang-On's bike ride OutRun's courses, OutRun's Ferrari drive Super
Hang-On's own courses (on an emulated Super Hang-On board), and either of
them, or Hang-On's own bike, race Hang-On's course (on an emulated Hang-On
board), with all three games' race music, Cool Riders' cover of Hang-On's
theme and OutRunners' radio (its songs and DJ). Non-commercial; see
"Licences" below.

No game ROMs are included. You need your own copies.

Project page and source: https://github.com/new525/cannonball-Shangon-Crossover
Based on Cannonball by Chris White: https://github.com/djyt/cannonball


Changes since 0.10.0 (not yet released)
---------------------------------------
- Force feedback for steering wheels, on SDL2 haptics (Windows and Linux;
  it replaces Cannonball's DirectX 8 support). The base is each vehicle's
  own arcade cabinet motor: OutRun's deluxe cabinet motor for the Ferrari
  (as before), OutRunners' wheel vibration motor for MAD POWER; the bikes'
  ride-on cabinets have none. On top, CannonBall-DX's effects (sand, tyre
  slip, off-road rumble and pull, gear shift, traffic contact, crash
  impacts and loads, start-line steering and rev shake, engine shake) and
  its speed-dependent centering spring, on every vehicle and course. OFF by
  default: SETTINGS > CONTROLS > FORCE FEEDBACK, FFB STRENGTH and SPRING;
  each effect's strength is in config.xml under <haptic> (0-100, DX's
  defaults; <invert> for a wheel that pulls the wrong way). The device is
  the controls' wheel, or FF_TARGET_VIDPID (e.g. 046d:c24f) chooses one.
  Untested on real hardware: docs\FORCE-FEEDBACK.md says what to check.
- From CannonBall-SE and CannonBall-DX, rewritten for this program:
  - Quick taps of the gear, view, coin, start and menu buttons are no
    longer lost: the game reads its controls 30 times a second, and at 60
    fps a press released again before the next read was never seen. MAD
    POWER's manual box on OutRun's, Super Hang-On's and Hang-On's courses
    counts such a tap too.
  - New option DIGITAL NAME ENTRY (SETTINGS > GAME ENGINE > ENHANCEMENTS;
    <digital_entry> in config.xml under <scores>), OFF by default, which is
    the arcade: the wheel scrolls the letters (the keys through the
    simulated wheel, slowly) and the accelerator confirms. ON: each press
    of left or right moves one letter and START confirms as well as the
    accelerator. A real wheel works as before either way.
  - Three more Ferrari colours, BLACK, WHITE and SILVER (CannonBall-DX's
    palettes), after CYAN in SETTINGS > GAME ENGINE > HANDLING > COLOR.
  - SETTINGS > SOUND > OUTPUT DEVICE chooses the sound output (DEFAULT, or
    one of the devices SDL reports; <playback_device> in config.xml). A
    device that is no longer there falls back to the default.
  - SETTINGS > VIDEO > DISPLAY chooses the display the window opens on,
    full screen or windowed (<display> in config.xml, 0 = the first). A
    window dragged to another display is remembered for the next start.
  - SETTINGS > GAME ENGINE > ENHANCEMENTS: BUG FIXES is now on the PC menu
    too, and SELECT BUG FIXES turns its fixes on and off one by one
    (<fixes> in config.xml; all on by default, as BUG FIXES alone was):
    ENDING PALETTE (the man's colours at the start of three endings), LAP
    TIME HUNDREDTHS (at a checkpoint), MUSIC SCREEN TILE, MAP SEA EDGE,
    STEERING FROM REST, SPRITE RIGHT EDGE, ROAD LINE ON MENUS. ENDING
    TROPHY (off by default) keeps the gold of the trophy handed over in
    endings A and C, which the ending palette fix recoloured (found by
    CannonBall-DX).
- Fixes from a code review:
  - After a MAD POWER race on Super Hang-On's or Hang-On's course, or the
    menu key during any MAD POWER race, its sound board ran on in the menu
    and its unheard output piled up in memory (about 50 MB a minute).
  - On Hang-On's course, sprites reaching the right edge of the screen put
    a few pixels on the left edge of the next line.
  - At 30 fps with the sound at 22,050 Hz the other games' sound boards
    lost one sample a frame (they ran 0.14% slow, with a click).
  - A ROM file of the other games' sets found by its name but with the
    wrong CRC32 was used with only a console warning; it is refused with a
    message now, since the reconstructed bike reads its tables from it.
  - The music choice saved by title was lost at the next save when its ROM
    set was missing.
  - MAD POWER's score on OutRun's courses read past a table above 335 km/h.
  - BALANCE's rounding carries are cleared at each race start, so a second
    race is the same as the first.
  - CLEAR HISCORES also clears OutRunners' rankings (its EEPROM file).
  - The version is shown on the ABOUT page and printed at start-up.


Changes in 0.10.0
-----------------
- New race setup option BALANCE (HALF by default, FULL, OFF). Each game's
  courses are made for its own vehicle; BALANCE brings a vehicle from
  another game in line with the course's own one, while it keeps its own
  handling, acceleration, sounds and speedometer. FULL matches, HALF goes
  half way (the geometric mean), OFF leaves everything as in 0.9.0. On a
  vehicle's own course it changes nothing. Three things are balanced, each
  from measurements (docs/balance-pace.md, balance-push.md,
  balance-contact.md):
  - Pace: the course moves by the vehicle's speed times a factor, so at
    FULL every vehicle at its top speed covers the course as fast as the
    course's own vehicle. MAD POWER on OutRun's roads then goes at the
    Ferrari's pace (293 km/h; 363 at HALF) while its speedometer still
    shows its own 451; the Ferrari on OutRunners' courses goes at MAD
    POWER's. The supercharger and MAD POWER's manual box keep their extra
    speed.
  - Curve push: a vehicle feels another game's curves as hard as it feels
    its own game's (the share of its full-lock steering a curve takes at
    top speed, averaged along the curves). Super Hang-On's bike on OutRun's
    courses comes out the same on average (factor 0.99); MAD POWER is
    pushed about 0.8 as hard as before on OutRun's, Super Hang-On's and
    Hang-On's courses, the Ferrari 0.94 / 0.85 on Super Hang-On's /
    Hang-On's and 1.33 on OutRunners'.
  - Contact: bumping into another vehicle costs the share of speed the
    course's own vehicle would lose in the same contact, while each vehicle
    still reacts its own way (the Ferrari's skid and spin, the bike's
    knockback, MAD POWER's bounce). On OutRunners' Kenya an elephant stops
    you almost dead, as it does MAD POWER.

Fixes and features taken from CannonBall-SE and CannonBall-DX (forks of
Cannonball under the same licence), rewritten for this program:
- ROMs can stay in MAME's zip files: every .zip in a ROM folder is read as
  well as the loose files, and files are recognised the same way (CRC32
  first, then name; loose files are used first). A clone set's zip works
  next to its parent's (shangon1.zip with shangon.zip, orunnersj.zip with
  orunners.zip), and so does a merged set. A ROM path in config.xml may
  also name a single .zip. The optional fixed PCM ROM opr-10188.71f, which
  only MAME's enhanced sets have, can be added as a loose file next to
  outrun.zip.
- OutRun's sampled sounds and drums (the Sega PCM chip) play at the chip's
  real rate, 31,250 Hz (as in MAME); they were 2.4% sharp. Low-pitched
  samples are no longer flattened by a rounding error (up to 3% at 44,100
  Hz).
- FIX SAMPLES (fix_samples, on by default) no longer stops the program
  starting when opr-10188.71f is missing: it warns and uses the original
  opr-10188.71. Of the corrected ROMs, Sega/M2's official one (MAME's
  outruneh, CRC C2DE09B2) is now used before CannonBall's patched one
  (outruneha, 37598616). The menu shows which one is loaded.
- High-score entry: with TIMING FIXES on, choosing ED no longer leaves the
  table on screen for hours (its timer wrapped round to 9999). A new high score is now saved when the
  entry's time runs out, not only when the entry is finished.
- New option TRAFFIC BRAKE FIX (SETTINGS > GAME ENGINE > ENHANCEMENTS;
  <fix_traffic> in config.xml), OFF by default, which is the original game:
  a traffic car could brake sharply as you drove alongside it with nothing
  ahead of it. PARTIAL: a new traffic car no longer inherits the previous
  car's "car ahead" speed (CannonBall-SE). FULL: also, it only slows to the
  car ahead's speed when other traffic is close (CannonBall-DX).
- Code clean-up (undefined behaviour in the tilemap scrolling and the
  traffic slope frames), with the same results as before.


Changes in 0.9.0
----------------
- OUTRUNNERS RADIO replaces the four OutRunners songs in the music
  selection: OutRunners' own in-race radio, ported from its code, with its
  ten songs and its DJ (Mega Radio Station), changed mid-race with the d-pad
  as with OutRunners' DJ/MUSIC, << and >> buttons (see Music). Its title
  slides in at the bottom of the screen as in OutRunners. A saved choice of
  one of the old four songs becomes OUTRUNNERS RADIO. On OutRunners' own
  courses the same d-pad directions press its radio buttons.
- New race setup option GEARBOX (AUTOMATIC / MANUAL), shown while
  OUTRUNNERS MAD POWER is the vehicle: MANUAL gives its 6-speed box (GEAR1
  down, GEAR2 up) whatever the gear button setting. It used to follow the
  separate gear buttons setting.
- On Super Hang-On's and Hang-On's courses, the chosen music track now
  stops when the game stops its own music (before Super Hang-On's ranking
  music, and in Hang-On's last seconds before GAME OVER); it used to play on
  over the game's own music.
- MAD POWER on OutRun's, Super Hang-On's and Hang-On's courses now has its
  wheelspin, sliding, gravel and crash sounds: only its engine reached its
  sound board before. Its engine was already OutRunners' own, at OutRunners'
  own level (quieter than the music, as in OutRunners).


Changes in 0.8.1
----------------
- MAD POWER now feels the hills: on OutRun's courses and on Super Hang-On's,
  it slows down climbing and speeds up going down, with OutRunners' own slope
  code. The slope comes from each game's own hill data, scaled so that a hill
  that looks as steep in both games has the same steepness for MAD POWER.
  Hang-On's road is flat, so nothing changes there.
- Sound boards: switching between music tracks (e.g. Cool Riders' track and
  the OutRunners songs) no longer distorts a track played again after it had
  stopped.


Changes in 0.8.0
----------------
- OutRunners' courses, on an emulated copy of its whole board (System Multi
  32), with OutRunners' car MAD POWER: OUTRUNNERS FULL COURSE (the game's
  own map; at each checkpoint, the side of the road you are on picks the
  way) and OUTRUNNERS KENYA (the Kenya stage alone). The board matches MAME
  exactly in long test sessions. They need every file of MAME's orunnersj
  set (roms\orunners\roms.txt).
- With the separate gear buttons setting, MAD POWER has its 6-speed manual
  box (GEAR1 down, GEAR2 up); otherwise its automatic.
- Starting one of OutRunners' races skips its attract mode and car select
  (the board plays them unseen, as fast as the game allows: about two
  seconds from START RACE to the start line).
- OutRun's Ferrari on OutRunners' courses (VEHICLE: OUTRUN FERRARI with
  OUTRUNNERS FULL COURSE or OUTRUNNERS KENYA). OutRunners' program runs the
  world: road, camera, scenery and wildlife, checkpoints (your side of the
  road picks WEST/EAST and the branches), timer, goal, game over.
  Cannonball's own OutRun code drives the car: speed and gears, steering,
  curve push, off-road, skids and spins, the crash with its flips and thrown
  passengers, smoke and shadow, and the engine and tyre sounds from OutRun's
  sound program (MAD POWER's engine is silenced). The car is drawn with
  OutRun's sprites and colours at MAD POWER's place in OutRunners' sprite
  list. Top speed 293 km/h, which OutRunners' speedometer shows; the
  acceleration is OutRun's own to the 1/30 s.
- OUTRUNNERS KENYA: reaching the goal now ends the race with the goal run,
  results and name entry, as at the end of the full course (the race used to
  go on at the goal run's speed for ever).
- Super Hang-On's bike on OutRunners' courses (VEHICLE: SUPER HANG ON BIKE
  with OUTRUNNERS FULL COURSE or OUTRUNNERS KENYA): the same bike as on
  OutRun's and Hang-On's courses (Super Hang-On's own speed, steering, lean,
  centrifugal push, off-road, supercharger, knockback and crash), with its
  engine and effects from Super Hang-On's sound board, in OutRunners' world
  as the Ferrari is. Top speed 241 (279 with the supercharger), which
  OutRunners' speedometer shows; the acceleration is the same as on OutRun's
  first stage to the 1/60 s. BIKE SIZE applies on OutRunners' courses too.
- On OutRunners' courses MAD POWER's gravel and wheelspin sounds no longer
  play under the Ferrari (or the bike) when it leaves the road.
- OutRunners' MAD POWER on OutRun's courses (VEHICLE: OUTRUNNERS MAD POWER
  with OutRun's full route or any single OutRun stage). As Super Hang-On's
  bike rides them, MAD POWER drives with its own code: OutRunners' handling,
  sprite frames, crash and sound parameters, rebuilt in C++ and checked
  against its emulated board tick by tick (no differences). OutRun's world
  (road, traffic, scenery, checkpoints and forks, timer, goal) follows it.
  It is drawn with OutRunners' own sprites and colours, and its engine,
  tyres and crashes come from OutRunners' sound board (no OutRunners music).
  Its acceleration on OutRun's first stage is the same at every 1/30 s as on
  OutRunners' first course (made level for the comparison); it tops out at
  451 km/h (automatic) or 471 km/h (its 6-speed manual box: separate gear
  buttons setting, GEAR1 down, GEAR2 up), which OutRun's speedometer shows.
  Needs every file of MAME's orunnersj set.
- MAD POWER on Super Hang-On's courses and Hang-On's course (VEHICLE:
  OUTRUNNERS MAD POWER with SUPER HANG ON AFRICA, ASIA, AMERICA, EUROPE or
  HANG ON): it takes the Ferrari's place on those boards, with the same code
  as on OutRun's courses. Its acceleration there is the same at every 1/30 s
  as on OutRun's first stage until it first touches something.
- Fixed: with the Ferrari, Super Hang-On's goal stopped at the bonus count
  (the game never went on to the ranking); it now goes on to the ranking,
  name entry and the menu (with MAD POWER too).


Changes in 0.7.0
----------------
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
   For its music, 3 files of MAME's "orunners" set (or orunnersu,
   orunnersj). For its courses and MAD POWER, every file of "orunnersj".
6. Run cannonball.exe. With a force-feedback wheel, turn on SETTINGS >
   CONTROLS > FORCE FEEDBACK (docs\FORCE-FEEDBACK.md).

The crossover ROM sets are optional: without Super Hang-On's ROMs you drive
the Ferrari, and each missing set only removes that game's part (its
courses, vehicle and music; for Cool Riders and OutRunners, their tracks).
Each folder can hold the loose files or MAME's zips as they are (outrun.zip
in roms\, shangon1.zip in roms\shangon1\, and so on); every .zip in the
folder is read. For a clone set, put its parent's zip next to it:
shangon.zip with shangon1.zip, orunners.zip with orunnersj.zip. ROM files
are recognised by CRC32 first, then by name, so the names don't have to
match exactly; loose files are used before files in zips.


Race setup menu
---------------
Main menu > PLAY GAME opens the race setup:
    START RACE
    VEHICLE   OutRun's Ferrari, Super Hang-On's bike, Hang-On's bike
              (HANG ON BIKE: Hang-On's course only), or OutRunners' MAD
              POWER (every course)
    GEARBOX   OutRunners' MAD POWER only: AUTOMATIC, or MANUAL (its 6-speed
              box: GEAR1 down, GEAR2 up)
    BIKE SIZE 80 percent (default), 50 to 90 percent, or FULL SIZE
              (OutRun's and OutRunners' courses; the bike is full size on
              Super Hang-On's and Hang-On's)
    TRACK     OutRun's full route (you choose at each fork), or any single
              OutRun stage (1, 2A-2B, 3A-3C, 4A-4D, 5A-5E; letters follow
              the order of Cannonball's time-trial course map). A single
              stage is raced with the time-trial rules; laps and traffic are
              set under GAME MODES > TIME TRIAL. Or one of Super Hang-On's
              courses: AFRICA, ASIA, AMERICA, EUROPE (the Ferrari, Super
              Hang-On's bike or MAD POWER), or Hang-On's course: HANG ON
              (any of the four vehicles), or
              OUTRUNNERS FULL COURSE / OUTRUNNERS KENYA (MAD POWER, the
              Ferrari or Super Hang-On's bike; not Hang-On's bike).
    MUSIC     any of the tracks below (Music), previewed as you choose, or
              "CHOOSE ON THE RADIO" for OutRun's in-car radio screen.
              OUTRUNNERS RADIO: OutRunners' radio, changed with the d-pad
              in the race (see Music). OutRunners' own courses always play
              OutRunners' own radio (its buttons on the same d-pad).
    BALANCE   HALF (default), FULL or OFF: how far a vehicle on another
              game's course is brought in line with the course's own
              vehicle (see Changes since 0.9.0). No effect on a vehicle's
              own course.
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
Riders' cover of it ("HANG ON THEME OF LOVE"), then "OUTRUNNERS RADIO". Each
one plays on an emulated copy of its own game's sound board. The covers'
levels are matched to OutRun's own tracks (a design choice: there is no
arcade reference for levels between different games' boards).

OUTRUNNERS RADIO is OutRunners' in-race radio, ported from OutRunners' own
code. It starts on Splash Wave (the song OutRunners gives its car MAD POWER;
the other vehicles count as MAD POWER). Its ten songs, in OutRunners' order:
Magical Sound Shower, Passing Breeze, Splash Wave, Picture the Rivers, Blow
Your Cool, Looking for the Rainbow, Speed King, Adventure, Sonic Control,
Last Wave (after the last comes the first again). In a race the d-pad (or
the up/down keys) is OutRunners' radio buttons:
    UP     DJ/MUSIC: Mega Radio Station (the DJ talks over the station);
           in Mega Radio Station any radio button goes back to the song
    DOWN   >>: the next song
    LEFT   <<: the song before   } only with analogue steering (a wheel
    RIGHT  >>: the next song     } or a stick); otherwise they steer
    << and >> together (LEFT with RIGHT or DOWN): Jingle Bell
Each change shows the song's title at the bottom of the screen for about
three seconds, as OutRunners does, in the lettering of OutRun's radio
screen. OutRunners' map (its stations by place, the slow-down at its
branches) is not on the other games' courses: there Mega Radio Station
plays OutRunners' first station. The radio stops when the music would
(GAME OVER, the end of a Super Hang-On or Hang-On race, leaving the race).


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
- The Ferrari on OutRunners' courses: MAD POWER (automatic) is chosen in the
  run-up and the Ferrari takes its place. Speed: OutRun's km/h x 136
  (OutRunners' unit; its speedometer shows OutRun's km/h). Lateral position:
  OutRun's x 11/6, the ratio of the two cars' half-widths (24 and 44), so
  MAD POWER's collision box and off-road width are the Ferrari's; OutRun's
  wheel rules at OutRunners' road edge. Curve push: OutRun's road builder
  over a path made from OutRunners' segment headings, one OutRun path
  position being 272/303 of a segment (the distance both games cover per
  km/h). Incline frames from OutRunners' own hill frame for its car.
  Scenery hits start OutRun's crash; Kenya's elephants (and other cars) give
  OutRun's car contact (skid, speed cut, spin chance of cars); a minor
  contact OutRun's off-road for the 15 ticks OutRunners uses. The Ferrari is
  drawn at OutRun's size scaled as OutRunners' camera scales MAD POWER (it
  flies in at the start and pulls back with speed). After the goal,
  OutRunners' scripted goal run carries the Ferrari. Details in
  docs\CROSSOVER-OUTRUNNERS.md (section 7) in the source.
- Super Hang-On's bike on OutRunners' courses: as the Ferrari, MAD POWER
  (automatic) is chosen in the run-up. Super Hang-On's units are OutRun's,
  so the bike takes the Ferrari's conversions (speed x 136, lateral x 11/6,
  272/303 of a segment per OutRun path position); OutRunners' speedometer
  shows the bike's own speed value (241 at top speed; Super Hang-On's own
  speedometer would show 280). Super Hang-On's tasks run two frames per
  OutRunners tick, as they run per OutRun tick. The bike is off the road
  when its centre line passes OutRunners' asphalt edge (Super Hang-On's own
  rule, which uses 300 on its road), and OutRunners' limit for its car holds
  it. Curve push: Super Hang-On's road builder over the same path made from
  OutRunners' headings. Collisions: OutRunners' tests decide, with the
  bike's widths written into OutRunners' collision box for each test: its
  centre line against roadside objects, 10 units (x BIKE SIZE) beyond a
  car's or an elephant's width; scenery starts Super Hang-On's crash, an
  elephant or a car its knockback and contact sound, a minor contact its
  off-road for half a second. At time up Super Hang-On's rule cuts the
  throttle and holds 80 km/h; OutRunners shows GAME OVER 6 s later, as
  always. BIKE SIZE applies, as on OutRun's courses (both worlds are sized
  for a car). After the goal, OutRunners' scripted goal run carries the
  bike. Off-road dust is OutRun's smoke, as on OutRun's courses. Details in
  docs\CROSSOVER-OUTRUNNERS.md (section 8) in the source.
- MAD POWER on OutRun's courses: OutRun's road becomes the road OutRunners'
  car code reads: one OutRunners segment is 303/272 of an OutRun road
  position (the same distance per km/h in both games), each segment's
  heading the direction of OutRun's road path there (turned the other way on
  the mirrored road of a fork), the road's half-width OutRun's asphalt edge
  x 11/6 (the cars' half-widths 24 and 44: MAD POWER is the Ferrari's width,
  so OutRun's collision tests are unchanged), with the surface of
  OutRunners' first course. Slopes: OutRun's hills are drawn in 2D (each
  section bends the road ahead, and OutRun's camera turns into each bend as
  the car gets there); read as a road under a camera that rides it, they
  give the slope at the car, and a hill that looks as steep in both games
  gets the same steepness (the far road moved as far on screen, against
  OutRunners' focal length of 192 pixels). Speed: km/h =
  OutRunners' speed / 136. Collisions: OutRun's tests decide; a roadside
  object gives MAD POWER's own scenery bounce, a traffic car its response to
  a car of its race of its own weight. Its off-road (drag, shaking, gravel
  sound, dust) is its own. OutRun's camera stays fixed to the car: MAD POWER
  is drawn at OutRunners' scale 1 (its size on OutRunners' start line, the
  Ferrari's size), at the place OutRunners' projection gives it there, in
  the Ferrari's place in OutRun's sprite list; its hill frame follows
  OutRun's own uphill/level/downhill decision. OutRunners' start line holds
  while OutRun counts down; time up, the goal, the bonus run and the ending
  are OutRun's. Its sound board plays its engine, tyres and crashes; OutRun's
  board plays the world. Details in docs\CROSSOVER-OUTRUNNERS.md (section 9)
  in the source.
- MAD POWER on Super Hang-On's and Hang-On's courses: the board's own road
  path gives the road as OutRun's does (same format); the road's half-width
  is the board's road edge (300 on Super Hang-On's, 260 on Hang-On's) x
  11/6. Super Hang-On's hills are a real 3D path its second CPU runs: the
  slope at the vehicle comes from it, scaled as on OutRun's courses (its
  focal length 64 pixels against OutRunners' 192). Hang-On's road is flat.
  The boards' own tests decide collisions: a roadside object
  gives MAD POWER's scenery bounce; a rival bike is hit by the Ferrari's rule
  (MAD POWER is the Ferrari's width) and answered as a car of its race, at
  the rival's own position, sideways speed and speed. It is drawn with its
  own sprites and colours at the bike's place in the board's sprite list.
  Its sound board plays its engine, tyres and crashes; the board's bike
  sounds are silenced as for the Ferrari, and its engine stops when the race
  ends. Time up, the goal and the ending are the board's (Super Hang-On's
  ending shows its own rider). Details in docs\CROSSOVER-OUTRUNNERS.md
  (section 10) in the source.


Not done yet
------------
- Hang-On's bike on OutRunners' courses: it exists only as Hang-On's own
  program on its emulated board, and its tasks have not been rebuilt as code
  that can run in another game's world, as Super Hang-On's were.
- MAD POWER on OutRun's, Super Hang-On's and Hang-On's courses: the wheels
  flying off in a crash and the start line's player marker are not drawn.
- MAD POWER on OutRun's courses: OutRun's ending shows MAD POWER standing
  with the Ferrari's passengers' animation, and the attract mode shows the
  Ferrari; OutRun's HUD shows no gear for its manual box. OutRun's steepest
  hill values give climbs steeper than any on OutRunners' own course (up to
  56 degrees against 32).
- With the Ferrari or the bike on OutRunners' courses: OutRunners' own
  pictures of the car stay MAD POWER (the ending's picture, the map's and
  HUD's car icons); the HUD's gearbox shows AUTO; the flying wreck and
  passengers (or the bike's rider) are drawn at the car's depth and on
  OutRun's flat-road heights, not OutRunners' hills.
- The bike on OutRunners' courses: Super Hang-On's own dust is not drawn
  (OutRun's smoke instead), and OutRunners' roadside hit areas have not been
  compared with the objects as drawn.
- OUTRUNNERS KENYA: the results show 0 for the four checkpoints before Kenya.
- On OutRunners' courses the MUSIC choice is not used: OutRunners plays its
  own radio (the d-pad works its buttons).
- OUTRUNNERS RADIO on the other games' courses: the title does not cycle
  through OutRunners' eight colours (it is in OutRun's radio-screen colours),
  and OutRunners' radio-panel display (its LCD) is not shown. OutRunners'
  START (also "next song") is not mapped.
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
  superscaler-crossover-0.10.0-src.zip    this program (git tag v0.10.0,
                                          branch "crossover")
  SDL2-2.30.8.tar.gz                      SDL2 (zlib licence)
  boost-1.83-headers-used.tar.gz          the Boost headers compiled in
                                          (Boost Software License 1.0)
  mingw-w64-v11.0.1-winpthreads.tar.gz    libwinpthread-1.dll's source
                                          (upstream v11.0.1)
Other components: Z80 core by Nicolas Allemand (MIT), 68000 core Musashi by
Karl Stenerud (MIT; it bundles John R. Hauser's SoftFloat), ymfm by Aaron Giles
(BSD-3-Clause, as shipped in MAME 0.264), miniz by Rich Geldreich (MIT;
reads the ROM zips), and parts derived from MAME
(BSD-3-Clause): the FD1089 decryption, the Sega PCM sample generation, the
Super Hang-On and Hang-On boards' video (tilemaps, road, sprites and
mixing, transcribed from MAME's drivers), the SH-1 core (MAME's SH-2 family
interpreter), the SCSP sound chip, the MultiPCM sound chip (Miguel Angel
Horna), and OutRunners' System Multi 32 board: the V60/V70 core (Farfetch'd,
R. Belmont), the System 32 video and the board (Aaron Giles), the 315-5296
I/O chip (hap), the MSM6253 ADC (AJR), the serial EEPROM (Aaron Giles), the
comm board (Ariane Fugmann) and parts of MAME's memory and scheduling code
(Aaron Giles, Olivier Galibert, Nicola Salmoria, Alex W. Jackson, Paul
Priest, Vas Crabb).
Licence texts are in the licenses folder.

OutRun, Super Hang-On, Hang-On, Cool Riders and OutRunners are trademarks of
SEGA. This project is not affiliated with SEGA.
