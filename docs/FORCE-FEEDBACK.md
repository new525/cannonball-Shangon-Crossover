# Force feedback (steering wheels)

Cannonball's force feedback (DirectX 8, Windows only, the deluxe cabinet's motor commands as a
constant force of a fixed duration) is replaced by SDL2 haptics on Windows and Linux, driven by an
effects layer that takes the vehicle's state once per 30 Hz tick: the vehicle's own arcade motor
output as the base, plus CannonBall-DX's synthesised effects (its `ooutputs_base.cpp` and
`ffeedback_base.cpp`, commit 09edd62), each an option, and DX's speed-dependent centering spring.

Files: `src/main/ffb/ffbdevice.*` (the device), `src/main/ffb/ffb.*` (the effects), the vehicles'
state builders (`OOutputs::ffb_tick`, `CrossoverBike::ffb_tick`, `CrossoverMadPower::ffb_tick`,
`SHMode::ffb_tick`, `HOMode::ffb_tick`, `ORMode::ffb_tick`).

## 1. The device (`ffb/ffbdevice`)

Three SDL haptic effects created once and updated in place on the steering axis
(`SDL_HAPTIC_STEERING_AXIS` for a joystick SDL reports as a wheel, cartesian with the sign in the
direction otherwise, as DX does): a constant force (signed, -100..100 %), a spring about the centre
(0..100 % of the full coefficient, the device's own autocenter switched off) and a sine vibration
(magnitude and period). The master strength scales the three. Device choice, in order: the
joystick whose vendor:product matches `FF_TARGET_VIDPID` in the environment (`046d:c24f`), the
`pad_id` joystick the program reads its controls from, any joystick of the wheel type, any
joystick; each only if it has a haptic interface with a constant-force effect. The gamepad rumble
path (`Input::set_rumble`) is untouched; with force feedback on, `OOutputs` runs in its force
feedback mode, so the rumble output is never set (as in Cannonball).

Testing double: `CB_FFB_LOG=<file>` appends every backend call with the engine frame number
(`f<frame> constant -35`, `spring 32`, `periodic 11 45`, `gain 50`, `stop`), and the device counts
as present without hardware; `CB_FFB_LOG_STATE=1` adds the vehicle state given to the effects
each tick. Without a device and without the switch every call returns at once; with
`haptic enabled` 0 nothing is opened and nothing runs.

## 2. The effects (`ffb/ffb`)

Levels are percentages of the device's full force before the master strength (`strength`, the
menu's FFB STRENGTH). All of DX's cadences are on ticks (1/30 s) instead of milliseconds.

**The arcade base.** The vehicle's own cabinet motor output, as its game's code drives it:
- OutRun's Ferrari: the deluxe cabinet's motor commands (`OOutputs::do_motors`, unchanged: the
  bank direction and speed the game writes to its motor register, 0-15 with 8 the centre, the
  wheel's position in place of the cabinet's bank) as a constant force linear in the motor speed
  ((command - 8) / 7 of full, scaled by `effects.motor`). This is what Cannonball's force feedback
  did, with a linear scale instead of the old 70-85 % band. Outside a race (attract mode, the music
  selection) the motor's stationary centering runs as it always did.
- OutRunners' MAD POWER: the cabinet's wheel vibration motor, one bit. On the emulated board it is
  the I/O chip's port D bit 4 (MAME's `MA_Steering_Wheel_motor`), read from the board's output
  latch; on OutRun's courses it is the transcribed car's shaker byte (`$20E0D0` + player: 0 on the
  road, the car's `$B970` value off it or in a minor contact, `$10` in a crash), which a board run
  with `CB_OR_MOTOR_LOG=<file>` shows to be exactly what the game writes to that bit (one frame
  later, the latch's write). Rendered as a sine while the bit is on, at `effects.motor_vibration`.
- Super Hang-On's and Hang-On's bikes: their ride-on cabinets have no motor; no base.

**DX's effects**, each with its strength under `haptic/effects` (0 = off), DX's defaults:
`sand` (a loose surface's taps, from `OLevelObjs::rough_surface`: a sand or stone strip at the car's
depth across the car, or the desert's debris; MAD POWER: a side in one of its course's surface
zones), `tyre_slip` (a sine while the tyres slide on the road; the spring drops to `spring.sliding`
%), `offroad_rumble_one_wheel` / `offroad_rumble_full` (the motor's off-road table normalised to
100 % at its peak, scaled by a speed envelope, as DX; the Ferrari's from its motor command, the
other vehicles from the same table), `offroad_pull_one_wheel` / `offroad_pull_full` (a constant pull
away from the road), `gear_shift` (a kick in the shift's direction for two ticks, a rebound on the
third; the player's shifts, not the automatic box's), `traffic_skid` (a yank in the skid's
direction held through the skid), the crash choreography (`crash_bump`, `crash_spin_impact`,
`crash_spin`, `crash_flip_impact`, `crash_flip`, `crash_flip_landing`: DX's impacts, side loads
every 4 ticks, the flip's rotation loads and the landing), `start_steering` (the load as the
Ferrari drives onto the start line, from its sprite's x), `start_rev_shake` (a sine with the
accelerator during the countdown, ramped), `engine` (a sine while driving, its magnitude and
period from the revs in eight steps, `engine_period` the idle period in ms).

Where a synthesised effect stands in for a motor pattern of the same event (the scenery crash, the
traffic skid, the start line's steering load), the motor base is muted for that event while the
effect's strength is above 0; at 0 the motor's own pattern plays instead. Off-road, the motor's
rumble pattern is the off-road rumble effect (scaled by its own strengths rather than `motor`).

One deliberate difference from DX: a traffic contact's spin (OutRun counts it with
`crash_counter` at `crash_state` 0) gets the spin's side loads; DX left it silent after the impact.

**The spring** (`centering_strength`, the menu's SPRING, the reference; `spring.*` percentages of
it): `low_speed` in menus, attract mode, on the start line, off-road and up to `speed_start` km/h,
rising to `high_speed` at `speed_full` km/h (OutRun's speed scale, 0-294), `sliding` of the current
level while the tyres slide, and the per-state levels during a traffic skid and the crashes'
phases (`traffic_skid`, `crash_bump`, `crash_spin`, `crash_recovery`, `crash_flip_start`,
`crash_flip_airborne`, `crash_flip_transition`, `crash_flip_landing`, `crash_flip_recovery`).

`invert` turns the constant forces round for a wheel whose driver reads the sign the other way.
The sign convention is DX's: a positive level is what their wheel felt as a pull to the left
(their code's comment), so the left wheels off the road pull left, away from the road.

Menus and the boards' run-ups: the spring at its low-speed level and nothing else (`ffb::idle`,
called by `main` on any frame no vehicle drove). A pause stops every force. Losing the window's
focus stops every force until it is back. The exit closes the device.

## 3. What is driven, per vehicle and course

| Vehicle, course | Base | Effects from the vehicle's own state | Not read |
|---|---|---|---|
| Ferrari, OutRun's courses (`OOutputs::ffb_tick`) | deluxe motor commands | all of them: wheels off by side, slip (`is_slipping`, the start's wheelspin), sand/debris, traffic skid (`skid_counter`), crash kind (flip / low-speed bump / spin, DX's rule), start steering and rev shake, revs, manual shifts | |
| Ferrari, Super Hang-On's, Hang-On's, OutRunners' courses (`shferrari`, `hovehicle`, `orvehicle`) | deluxe motor commands (OutRun's car logic runs there with the board's world faked in) | the same, from OutRun's variables; the rivals' contacts as the skid, the boards' scenery as the crash; the surface flag only on OutRun's courses | sand (the boards have no scenery strips of that kind) |
| Super Hang-On's bike, OutRun's courses (`bike.cpp`) | none | speed spring, off-road (whole bike, `$260098`), the traffic knockback (`$260088`) as the skid, the crash (`$260094`) as a tumble to the side of the road the bike is on, sand/debris from OutRun's scenery, rev shake from the throttle | the lean, the rear-wheel skid |
| Super Hang-On's bike on Hang-On's and OutRunners' courses | none | the same, from its own state | as above |
| Super Hang-On's bike at home (`shmode`, the board's RAM) | none | the same from `$260000`: mode 1 start, 2 race | as above |
| Hang-On's bike at home (`homode`, `$C7E000`, mode `$20C420`) | none | the same (Hang-On's RAM has Super Hang-On's offsets) | as above |
| MAD POWER, OutRun's courses (`madpower.cpp`) | the shaker byte as the wheel motor | speed spring, off-road by side (`$20907C`, the wheel kinds' bits), sliding / wheelspin / the shift's wheelspin as the slip, a side in a surface zone or OutRun's strips as sand, the manual box's shifts, a scenery hit (kind 0) as a tumble, a traffic hit (kind 2) as the skid, both through its state 5 | |
| MAD POWER, Super Hang-On's and Hang-On's courses | the shaker byte | the same (the boards' rivals as traffic) | sand |
| MAD POWER at home (`ormode`, the board's RAM) | port D bit 4 | the same from `$2004xx`/`$2090xx` | |

The bikes and MAD POWER have no start-line steering load (the Ferrari's intro animation); their
countdown rev shake runs when their tick runs on the start line (the boards' own vehicles and the
visiting ones do; on OutRun's courses the bike's and MAD POWER's ticks only start with the race,
so there it does not).

## 4. Tests (headless, no device: the log double)

Runs with `CB_FFB_LOG` and `CB_FFB_LOG_STATE`, checked against the harness trace / contact log of
the same run (scripts and logs in the round's scratchpad `dxport/ffbtest`):
- the Ferrari on OutRun stage 1: the intro load easing 27 → 0, the countdown shake ramping 1 → 20,
  the race's start wheelspin (periodic 11, spring 19), the spring 32 → 56 with speed, the engine
  sine's period 88 → 39 ms in steps, the motor's steer resistance with `steerto`, one wheel then
  both off the road (pull 35 / 21 plus the rumble), a crash's spin loads (±70 every 4 ticks) and
  a flip (spring 26 → 6 airborne), the recovery spring 39; on the full route with `CB_CHASE` a
  traffic contact at the frame the contact log gives (yank 70, spring 28) and its spin;
- the bike on OutRun: off-road pull and rumble, the tumble; MAD POWER on OutRun: as above;
- each vehicle on a board course (the Ferrari on Super Hang-On's, Hang-On's and Kenya; Super
  Hang-On's bike on Hang-On's; the three boards' own vehicles): the start phase with the pedal
  shake, the race with motor commands (the Ferrari), slips, off-road by side, rival contacts as
  skids, crashes (the Ferrari's bump/flip, the bikes' and MAD POWER's tumble), the motor bit for
  MAD POWER;
- the menu: the spring at 32 and nothing else;
- MAD POWER on Kenya with `CB_OR_MOTOR_LOG`: the port D bit against the shaker byte, 3,369 frames,
  equal but for the single-frame latch delay at each edge.

Identity with `haptic enabled` 0 (the default): WAV and snapshots byte for byte against a6aa2b8
for the Ferrari, the bike and MAD POWER on OutRun stage 1, MAD POWER on Kenya, the Ferrari on Super
Hang-On's and Hang-On's courses.

## 5. Not testable here: a wheel

No haptic device exists on the test machine. What a user with a wheel should check first:
1. the program prints `Force feedback: <device> (wheel), spring yes, periodic yes` at start with
   `haptic enabled` 1 (else which device SDL picked: use `FF_TARGET_VIDPID`);
2. in the menu the wheel centres gently (SPRING 80 x 40 %); with FFB STRENGTH stepped the force
   changes at once;
3. the direction: off the road the wheel should pull away from the road and a left-hand bend's
   motor resistance should push right; if reversed, `invert` 1;
4. the levels: `motor` (the Ferrari's base) against the spring at speed (both centre: set `motor`
   to 0 for DX's feel), `motor_vibration` for MAD POWER's buzz, the crash loads at 70 of a 50
   master;
5. Linux: the wheel's kernel driver must expose FF_CONSTANT and FF_SPRING (Logitech wheels through
   hid-logitech-hidpp do); SDL's evdev haptic backend is used, DX's raw evdev path was not kept.
