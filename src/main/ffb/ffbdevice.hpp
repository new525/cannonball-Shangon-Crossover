/***************************************************************************
    Force feedback device: the backend-neutral interface the effects layer (crossover/ffb) talks to.

    The implementation is SDL2 Haptics (ffbdevice.cpp), on Windows and Linux alike, with three
    effects on the steering axis: a constant force (signed), a spring about the centre and a
    periodic (sine) vibration. It replaces Cannonball's DirectX 8 backend (directx/ffeedback,
    Windows only, one constant force of a fixed duration).

    Levels are percentages: a constant force -100..100 (negative pushes the wheel left, as
    Cannonball's motor commands below MOTOR_CENTRE did), a spring 0..100 of the device's full
    coefficient, a periodic magnitude 0..100 with its period in milliseconds. The master gain
    (set_gain) scales the three.

    Testing without a device: with CB_FFB_LOG=<file> in the environment every call is appended to
    the file with the engine frame number, and the device counts as present, so the effects layer
    can be checked in headless harness runs. Without a device and without the switch every call
    returns at once.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

namespace ffbdev
{
    // Open a device. Order: a joystick whose vendor:product matches FF_TARGET_VIDPID (environment,
    // "046d:c24f" or "0x046d:0xc24f"), then the joystick the program reads its controls from
    // (pad_id), then any joystick SDL reports as a wheel, then any joystick, each only if it has
    // a haptic interface with a constant-force effect. Returns false when none is found.
    bool init(int pad_id);
    void close();
    bool is_supported();
    // the SDL joystick instance id of the device, or -1: the gamepad rumble path leaves that one alone
    int  instance_id();

    void set_gain(int percent);                 // master, 0..100 (scales everything below)
    void set_constant(int level);               // -100..100; 0 stops the effect
    void set_spring(int strength);              // 0..100 of the full coefficient; 0 stops the effect
    void set_periodic(int magnitude, int period_ms);    // 0 stops the effect
    void stop();                                // every effect off (focus lost, a pause, the menu)

    // testing: a line in the CB_FFB_LOG file (the vehicle's state, written by the effects layer when
    // CB_FFB_LOG_STATE is also set); nothing without the log
    bool logging();
    void log_line(const char* text);
}
