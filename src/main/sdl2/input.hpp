/***************************************************************************
    SDL Based Input Handling.

    Populates keys array with user input.
    If porting to a non-SDL platform, you would need to replace this class.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include <string>
#include <SDL.h>

class Input
{
public:
    enum presses
    {
        LEFT  = 0,
        RIGHT = 1,
        UP    = 2,
        DOWN  = 3,
        ACCEL = 4,
        BRAKE = 5,
        GEAR1 = 6,
        GEAR2 = 7,

        START = 8,
        COIN  = 9,
        VIEWPOINT = 10,
        
        PAUSE = 11,
        STEP  = 12,
        TIMER = 13,
        MENU = 14,     
    };

    bool keys[15];
    bool keys_old[15];
    bool keys_pressed[15];  // a press since the last frame_done(), kept even if released again

    enum limits
    {
        SW_LEFT   = 0,
        SW_CENTRE = 1,   
        SW_RIGHT  = 2,   
    };
    bool motor_limits[3];

    // Has gamepad been found?
    bool gamepad;

    // Gamepad supports rumble?
    int rumble_supported;

    // Use analog controls
    int analog;

    // Latch last key press for redefines
    int key_press;

    // Latch last joystick button press for redefines
    int16_t joy_button;

    // Analog Controls
    int wheel, a_wheel;
    int a_accel;
    int a_brake;
    int a_motor;

    Input(void);
    ~Input(void);

    void init(int, int*, int*, const int, int*, bool*, int*);
    void open_joy();
    void close_joy();

    void handle_key_up(SDL_Keysym*);
    void handle_key_down(SDL_Keysym*);
    void handle_joy_axis(SDL_JoyAxisEvent*);
    void handle_joy_down(SDL_JoyButtonEvent*);
    void handle_joy_up(SDL_JoyButtonEvent*);
    void handle_joy_hat(SDL_JoyHatEvent*);
    void handle_controller_axis(SDL_ControllerAxisEvent*);
    void handle_controller_down(SDL_ControllerButtonEvent*);
    void handle_controller_up(SDL_ControllerButtonEvent*);
    void frame_done();
    bool is_pressed(presses p);
    bool is_pressed_clear(presses p);
    bool has_pressed(presses p);
    void reset_axis_config();
    int get_axis_config();
    void set_rumble(bool, float strength = 1.0f);

    // Crossover: steering wheels
    // The joystick in use: its index among SDL's (-1 none), whether SDL calls it a wheel, its name and
    // USB ids, and the name of the built-in profile whose button defaults it took ("" none)
    int  pad_index() const { return pad_open; }
    bool is_wheel() const { return wheel_device; }
    const char* pad_name() const { return pad_name_text.c_str(); }
    int  pad_vendor() const { return pad_vid; }
    int  pad_product() const { return pad_pid; }
    const char* profile_name() const { return profile; }
    // A wheel whose steering or pedal axes are not chosen yet (the menu then runs WHEEL SETUP)
    bool wheel_needs_setup() const;
    // WHEEL SETUP: capture_begin() notes where every axis rests; capture_axis() then returns the axis
    // that has moved from there by at least `travel` (of 65,535 for the whole range), or -1, with the
    // direction it moved in (+1 / -1). Axes in `skip` (a bit each) are left out. capture_settled():
    // every axis is back where it rested.
    void capture_begin();
    int  capture_axis(int travel, unsigned skip, int& direction);
    bool capture_settled();
    // the dead zone in the centre of the steering (of 128 for the whole range), changed in the menu
    void set_wheel_dead(int dead) { wheel_dead = dead; }
    // the rumble's haptic handle closed (force feedback takes the device)
    void close_rumble();
    // an event of the joystick in use (SDL sends those of every open joystick, and force feedback
    // may have another one open)
    bool ours(SDL_JoystickID which) const { return stick != NULL && which == SDL_JoystickInstanceID(stick); }
    // a wheel read as a plain joystick: WHEEL SETUP chooses its axes (REDEFINE GAMEPAD its buttons)
    bool wheel_by_setup() const { return gamepad && wheel_device && controller == NULL; }

private:
    static const int CENTRE = 0x80;

    // SDL Joystick / Keypad
    SDL_Joystick *stick;
    SDL_GameController* controller;
    SDL_Haptic* haptic;

    // Configurations for keyboard and joypad
    int pad_id;
    int* pad_config;
    int* key_config;
    int* axis;
    bool* invert;

    int wheel_zone;
    int wheel_dead;

    // Last axis used
    int axis_last , axis_counter, axis_config;

    // Crossover: the joystick in use and the wheel setup's resting values
    int pad_open = -1;
    bool wheel_device = false;
    int pad_vid = 0, pad_pid = 0;
    std::string pad_name_text;
    const char* profile = "";
    static const int CAPTURE_AXES = 16;
    int capture_rest[CAPTURE_AXES];
    int choose_pad() const;
    void apply_profile();

    void bind_axis(SDL_GameControllerAxis ax, int offset);
    void bind_button(SDL_GameControllerButton button, int offset);
    void handle_key(const int, const bool);
    void handle_joy(const uint8_t, const bool);
    void set_key_state(const int, const bool);
    void handle_axis(const uint8_t axis, const int16_t value);
    void store_last_axis(const uint8_t axis, const int16_t value);
    int scale_trigger(const int);
};

extern Input input;
