# Cool Riders in the crossover

Cool Riders (Sega, 1995, System H1) has a cover of Hang-On's theme: *HANG ON -THEME OF LOVE-*,
song `$10` of its sound test (the radio title is in program ROM at `$4614B`). It is in the music
selection as **HANG ON THEME OF LOVE** (the big font has no hyphen), played by Cool Riders' own
sound program on an emulated copy of the board's sound side. Only Cool Riders' sound is used; its
courses and bikes are not in the program (section 5).

Set: MAME `coolridr`. Reference: MAME 0.264. Only 13 of the set's files are needed
(`roms/coolridr/roms.txt`); the ten 4 MB sprite ROMs are not.

## 1. The board (`crossover/crsound.*`)

The sound side of System H1, as MAME 0.264's `coolridr.cpp` describes it (that driver is
LGPL-2.1+ and was used as documentation only; no code was copied from it):

- an SH-1 (SH7032, 16 MHz, program `ep17662.12`) that streams sample data: its DMA unit at
  `$04000000` copies blocks from the main SH-2's program ROM and data ROMs into the sound RAMs. It
  takes the main CPU's commands from a ring in the shared RAM;
- a 68000 (16 MHz) whose program the SH-1 copies into sound RAM and releases from reset; it drives
  the SCSPs;
- two SCSPs (YMF292-F, 22.579 MHz, so 44,099 samples per second), each with 512 KB of sample RAM.
  SCSP 1 interrupts the 68000, and both interrupt the SH-1 (level 14);
- a level-12 interrupt to the SH-1 at line 384 of the 57 Hz, 512-line screen; a 315-5649 I/O
  chip.

Cores: the SH-1 is `crossover/sh/shcpu.*`, a transcription of MAME 0.264's SH-2 family
interpreter (BSD-3-Clause). The SCSPs are `crossover/scsp/*`, transcribed from MAME 0.264's
`scsp.cpp` and `scspdsp.cpp` (BSD-3-Clause). The 68000 is Musashi, shared with the other boards
through `xo_bus`.

Scheduling follows MAME's `device_scheduler`:
- the CPUs run in MAME's order (68000, then SH-1; a CPU held in reset runs last, as MAME's
  `rebuild_execute_list` puts suspended CPUs at the end, and its time advances by whole cycles);
- timeslices end at the next timer (scanlines, the SCSP timers, the 50 Hz stream update) or when
  an input line changes (MAME's synchronised `set_input_line`), or at the 60 Hz quantum;
- in a timer callback the current time is the timer's expiry (`device_scheduler::time`);
- the scanline timer counts from the start of VBLANK, as MAME's screen does
  (`screen_device::time_until_pos`): line 0 is 128 lines after power-on and line 384, the SH-1's
  level-12 interrupt, one frame after it;
- time and cycle arithmetic is MAME's, in attoseconds.

The board runs its own timeslices until its time reaches what the caller asks for, and may run
past it by up to a scanline; no timer is set at the caller's time, so the caller's step (1/57 s
in `crtest`, 1/60 s in the program) does not change the run. With both steps the SCSP writes are
identical; the output differs in a few noise samples, since the two chips draw from one random
generator in the order their samples are generated, as in MAME.

The main SH-2 is not emulated.

Musashi makes all of an instruction's bus accesses at its start and finishes every instruction;
MAME's 68000 makes each access at its own bus cycle and can stop between bus cycles. The board
therefore:
- times the 68000's writes from MAME's microcode pattern: a word write starts 8 cycles before the
  instruction ends (it is followed by the final prefetch), e.g. `move_w_imm16_dad_df` writes
  after 8 of its 16 cycles; a long write's two words start 12 and 8 cycles before the end;
- when a write changes an input line (which ends the timeslice at that bus cycle in MAME), ends
  the 68000's slice at that bus cycle and charges the rest of the instruction to its next slice.

This matters for the tempo: the sound program re-arms SCSP timer A with
`move.w #$ce,($418,A5)`, and the timer counts from that write.

## 2. Standing in for the main CPU (`crossover/crmusic.*`)

Cool Riders' main program talks to the sound board only through the shared RAM (main
`$03F00000` = SH-1 `$05300000`):

- **Boot:** in its first 50 frames the main program writes 142 values: a handshake (words
  `$040`-`$0B0`, "SEGA" at `$08C`), the ring's first eight entries (the stop sequence for both
  sides) and heartbeat words. They were recorded in MAME 0.264 with write taps, and the program
  replays them with their recorded times. Replaying only these, followed by a song command, plays
  the song. The per-frame settings block (`$1F0`-`$2DC`) and the later heartbeat writes are not
  needed.
- **Commands:** 32-bit entries in a ring of 32 at `$400`-`$47C`, then the write index at byte
  `$501`, which wraps from 31 to 0.
  - Format `ae10ss00` = play song `ss` on side 0 (player 1; both speakers).
  - Before each song the sound test sends the stop sequence `be1a2800`, `ae720000`,
    `ae700000`, `be507d00`, and the program does the same.
  - The stop sequence alone fades the song out over about a second.
- **Boot time (measured):** commands written at 1.1, 1.2, 1.3 or 1.4 s after power-on all
  started the song between 1.25 and 1.5 s. The program sends the first command at 1.25 s, booting
  the board at 3× speed with the output muted (about 0.4 s). After that the board is kept; when no
  song is playing it is paused (its time stops) once the fade is over.
- **NVRAM:** the SH-1's 512-byte backup RAM is left at zero. The output was sample-identical with
  zeros and with MAME's NVRAM file.
- **Level:** MAME's ±1.0 scale is converted to 16 bits and multiplied by 5.7, which brings the
  cover to the average level of OutRun's own three tracks as the program plays them (RMS 2,494,
  16-bit, measured at the radio screen over 9.5-24.5 s; the cover measures 2,487). Until 0.6.1 it
  was doubled, matching Hang-On's own theme on Hang-On's board, which is about a third of
  OutRun's level. This is a design decision; absolute levels between different boards have no
  arcade reference.
- **Resampling:** from 44,099 Hz to the output rate, by linear interpolation, as for the other
  boards (`crossover/resample.hpp`).

The song kept playing over a 7-minute test run.

## 3. Checked against MAME 0.264

**Reference recording:** MAME's sound test, song No. 15 (HANG ON), with all of the main CPU's
shared-RAM writes recorded. `tools/crtest` replays those writes into the board, and the outputs
are compared.

- **SCSP register writes:** the first 3,120 are identical (address, data, mask), with times
  within 1 µs. At 1.118 s the 68000 serves SCSP timer C's interrupt before timer B's, where MAME
  served B first; from there the order differs.
- **The song (43-224 s, 30,778 key-ons in MAME, 30,775 here, every one of MAME's matched to one
  here on the same chip within 50 ms):**
  - timing of each key-on against MAME's: median +0.03 ms, 10th percentile −0.10 ms, 90th
    +0.25 ms, over the whole song, with no drift (the median of each 5 s stays within
    ±0.05 ms);
  - loudness: within 0-4 % per 10 s;
  - spectrogram correlation: 0.88-0.93 per 2 s window (mean 0.907).
  - History: with the 68000's writes timed at the instruction's start the song drifted from
    −19 ms to −80 ms over 44-214 s; timed at the bus cycle, a steady −4 ms (key-ons median
    −2.2 ms); with the scanline phase, callback time, reset order and bus-cycle slice end as
    MAME's, the figures above.
- **Known difference, cause not found:** the 68000 signals the SH-1 by pulsing an SCSP interrupt
  (writes to `$10042C`, then `$10042E` 2 µs later) until the SH-1's handler has answered.
  - The bursts of pulses come at the same times as in MAME (16,788 bursts against 16,317 over
    the run) but are longer: 280,604 pulses against MAME's 84,954; in MAME most bursts are 3-6
    pulses, here 3-16.
  - The first burst matches exactly (12 pulses, the SH-1's reads of the interrupt-status
    register 2.1 and 5.1 µs after each pulse against MAME's 2.0 and 4.4 µs). Later ones differ
    in length, so the SH-1 is elsewhere in its program when they come. MAME has the main SH-2
    and its timers in the same scheduler, which end timeslices at times this board has no
    knowledge of, and the 68000 and SH-1 interleave differently as a result.
  - MAME's SH-1 could not be traced: in MAME 0.264 here, the `trace` command and read taps on
    the SH-1's program ROM crash MAME. Read taps on its I/O registers work.
- **In the program:** the board's output in the music selection matched `crtest`'s output for the
  same command (correlation 0.9996 at 48 kHz from 1 s after the song starts; 0.987 over the
  first 6 s, while the resampler builds up its 16-sample margin).
  - At 30 fps the song's envelope matched the 60 fps run (correlation 0.944 over 8 s at 10 ms).
  - On Hang-On's course, frame 600 was pixel-identical with this track and with Hang-On's own
    music.
  - The Windows build under Wine gave the same output as the Linux build.
- **CPU:** about 15 % of one core of the 2.7 GHz Xeon used for the tests while the song plays: a
  127 s headless run took 32.1 s with this track against 14.9 s with Hang-On's. While paused it
  costs nothing.

## 4. Tools

- `tools/crtest` (`build.sh <dir>`): `crtest <coolridr dir> <share log> <seconds> <prefix>
  [nvram]` runs the board from a log of main-CPU writes and writes `<prefix>.wav` (44,099 Hz,
  MAME's final clamp) and `<prefix>.scsp` (the 68000's SCSP writes).
  - `CRT_TRACE68=<s>` traces the 68000 for 0.04 s from that time.
  - `CRT_DMALOG=<file>` logs the sound DMA.

## 5. Cool Riders' courses and bikes

These were investigated and not done. In short:

- **The world:** there is no road chip and no polygon hardware.
  - Every frame the SH-2 sends 100-200 objects per screen to a blitter (about 20 zoomed road
    strips with per-line tables, then scenery, rivals, bike and HUD).
  - The game is real 3D in SH-2 software, at 28.5 Hz on 496×384 screens.
  - It shares neither road hardware nor units with OutRun, Super Hang-On or Hang-On.
- **Courses:** fully decoded. There are 51 lists of 8-byte records (points, road type, Δpitch,
  Δyaw) integrated at a fixed step. The reconstruction reproduced 534 of 534 road points sampled
  from RAM. Converting them to the other games' path format is plausible, but scale and the 3-way
  forks are design decisions.
- **Running Cool Riders' courses with the other vehicles** would need an emulated H1 main board:
  - the SH-2 plus its watchdog interval timer and interrupt controller;
  - most of all the blitter and RLE sprite decoder, which exist only in MAME's LGPL driver, whose
    own notes call parts of them guesses.
  - MAME 0.264's SH-2 interpreter does not boot the game: `sh7604_device` declares its own
    `m_test_irq`, hiding the base class's, so the watchdog interrupt is never taken. So there
    is no instruction-level reference without patching MAME.
  - Estimate: months of hobby time.
- **Cool Riders' bikes on the other courses** would need the bike's physics rebuilt from compiled
  SH-2 C code tangled with the 3D camera and course, and an RLE decoder for its frames. It is
  smaller in code but harder to verify.
