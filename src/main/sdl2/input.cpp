/***************************************************************************
    SDL Based Input Handling.

    Populates keys array with user input.
    If porting to a non-SDL platform, you would need to replace this class.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>
#include <cstring>
#include <cstdlib> // abs
#include "sdl2/input.hpp"

Input input;

Input::Input(void)
{
    stick      = NULL;
    controller = NULL;
    haptic     = NULL;

    gamepad = false;
    rumble_supported = false;

    memset(keys, 0, sizeof(keys));
    memset(keys_old, 0, sizeof(keys_old));
    memset(keys_pressed, 0, sizeof(keys_pressed));
}

Input::~Input(void)
{
}

void Input::init(int pad_id, int* key_config, int* pad_config, int analog, int* axis, bool* invert, int* analog_settings)
{
    this->pad_id      = pad_id;
    this->key_config  = key_config;
    this->pad_config  = pad_config;
    this->analog      = analog;
    this->axis        = axis;
    this->invert      = invert;
    this->wheel_zone  = analog_settings[0];
    this->wheel_dead  = analog_settings[1];
    motor_limits[0] = motor_limits[1] = motor_limits[2] = 0;
}

// Crossover: built-in button defaults for wheels SDL has no game controller mapping for, by USB id.
// Only entries still at -1 (not set in config.xml or the menu) take a default.
//
// Logitech PRO Racing Wheel (the direct-drive one; 046d:c272 Xbox/PC, 046d:c268 PlayStation/PC): the
// button numbers are those of its HID report in order, as documented with hardware captures by the
// Linux driver project github.com/mescon/logitech-rs50-linux-driver (docs/BUTTON_MAPPING.md, "sequential
// indices that match Windows DirectInput"): 0 A, 1 X, 2 B, 3 Y, 4 right paddle, 5 left paddle, 6 RT,
// 7 LT, 8 View, 9 Menu, 10 RSB, 11 LSB, 27 the G button; the d-pad is a hat. Not checked on the
// hardware here. The axes are not in the profile: their order through SDL could not be established
// without the wheel, so WHEEL SETUP finds them by asking the player to move each.
struct WheelProfile
{
    int vendor, product;
    const char* name;
    int gear1, gear2, start, coin, menu, view;      // padconfig 2-7
};

static const WheelProfile WHEEL_PROFILES[] =
{
    //                                                      gear1 (down)  gear2 (up)   start    coin     menu   view
    { 0x046d, 0xc272, "LOGITECH PRO RACING WHEEL",          5 /* L paddle */, 4 /* R paddle */, 9 /* Menu */, 8 /* View */, 2 /* B */, 3 /* Y */ },
    { 0x046d, 0xc268, "LOGITECH PRO RACING WHEEL",          5, 4, 9, 8, 2, 3 },
};

// The joystick to use: config's pad_id, or with -1 the first that SDL calls a wheel, else the first
int Input::choose_pad() const
{
    const int count = SDL_NumJoysticks();
    if (pad_id >= 0) return pad_id < count ? pad_id : -1;
    for (int i = 0; i < count; i++)
        if (SDL_JoystickGetDeviceType(i) == SDL_JOYSTICK_TYPE_WHEEL) return i;
    return count > 0 ? 0 : -1;
}

void Input::apply_profile()
{
    profile = "";
    if (controller != NULL) return;     // SDL's own mapping gives the defaults
    for (const WheelProfile& p : WHEEL_PROFILES)
    {
        if (p.vendor != pad_vid || p.product != pad_pid) continue;
        profile = p.name;
        const int defaults[6] = { p.gear1, p.gear2, p.start, p.coin, p.menu, p.view };
        for (int i = 0; i < 6; i++)
            if (pad_config[2 + i] == -1) pad_config[2 + i] = defaults[i];
        return;
    }
}

bool Input::wheel_needs_setup() const
{
    return gamepad && wheel_device && controller == NULL && analog != 0 &&
           (axis[0] == -1 || (analog == 1 && (axis[1] == -1 || axis[2] == -1)));
}

void Input::capture_begin()
{
    for (int a = 0; a < CAPTURE_AXES; a++)
        capture_rest[a] = stick != NULL && a < SDL_JoystickNumAxes(stick) ? SDL_JoystickGetAxis(stick, a) : 0;
}

int Input::capture_axis(int travel, unsigned skip, int& direction)
{
    if (stick == NULL) return -1;
    int best = -1, best_move = 0;
    for (int a = 0; a < CAPTURE_AXES && a < SDL_JoystickNumAxes(stick); a++)
    {
        if (skip & (1u << a)) continue;
        const int move = SDL_JoystickGetAxis(stick, a) - capture_rest[a];
        if (std::abs(move) >= travel && std::abs(move) > std::abs(best_move)) { best = a; best_move = move; }
    }
    direction = best_move < 0 ? -1 : 1;
    return best;
}

bool Input::capture_settled()
{
    if (stick == NULL) return true;
    for (int a = 0; a < CAPTURE_AXES && a < SDL_JoystickNumAxes(stick); a++)
        if (std::abs(SDL_JoystickGetAxis(stick, a) - capture_rest[a]) > 0x1000) return false;
    return true;
}

void Input::close_rumble()
{
    if (haptic != NULL)
    {
        SDL_HapticClose(haptic);
        haptic = NULL;
    }
    rumble_supported = false;
}

void Input::open_joy()
{
    // Crossover: the joystick is chosen (pad_id -1: a wheel if there is one) and described, and a
    // joystick already open is closed first (this runs again for every device SDL announces)
    close_joy();
    pad_open = choose_pad();
    gamepad = pad_open >= 0;
    wheel_device = false;
    pad_vid = pad_pid = 0;
    pad_name_text.clear();
    profile = "";
    if (gamepad)
    {
        const int pad_id = pad_open;
        stick = SDL_JoystickOpen(pad_id);
        if (stick == NULL) { gamepad = false; pad_open = -1; }
    }
    if (gamepad)
    {
        const int pad_id = pad_open;
        wheel_device = SDL_JoystickGetDeviceType(pad_id) == SDL_JOYSTICK_TYPE_WHEEL;
        pad_vid = SDL_JoystickGetDeviceVendor(pad_id);
        pad_pid = SDL_JoystickGetDeviceProduct(pad_id);
        const char* n = SDL_JoystickNameForIndex(pad_id);
        pad_name_text = n ? n : "";

        // If this is a recognized Game Controller, let's pull some useful default information
        if (SDL_IsGameController(pad_id))
        {
            controller = SDL_GameControllerOpen(pad_id);

            bind_axis(SDL_CONTROLLER_AXIS_LEFTX, 0);                // Analog: Default Steering Axis
            bind_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);         // Analog: Default Accelerate Axis
            bind_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 2);          // Analog: Default Brake Axis
            bind_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);    // Digital Controls. Map 'Right Shoulder' to Accelerate
            bind_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1);     // Digital Controls. Map 'Left Shoulder' to Brake
            bind_button(SDL_CONTROLLER_BUTTON_A, 2);                // Digital Controls. Map 'A' to Gear 1
            bind_button(SDL_CONTROLLER_BUTTON_B, 3);                // Digital Controls. Map 'B' to Gear 2 (has to be enabled)
            bind_button(SDL_CONTROLLER_BUTTON_START, 4);            // Digital Controls. Map 'START'
            bind_button(SDL_CONTROLLER_BUTTON_Y, 5);                // Digital Controls. Map 'Y' to Coin
            bind_button(SDL_CONTROLLER_BUTTON_BACK, 6);             // Digital Controls. Map 'Back' to Menu Button
            bind_button(SDL_CONTROLLER_BUTTON_X, 7);                // Digital Controls. Map 'X' to Change View
            bind_button(SDL_CONTROLLER_BUTTON_DPAD_UP, 8);          // Digital Controls. Map D-Pad
            bind_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 9);
            bind_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 10);
            bind_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 11);
        }

        apply_profile();

        // The simple rumble, on this joystick's own haptic interface (it was SDL's haptic device of
        // the same number, which need not be this joystick). Not on a wheel: its motor is force
        // feedback's (ffb/), and a second handle on it here only got in that one's way.
        rumble_supported = false;
        if (!wheel_device && stick != NULL && SDL_JoystickIsHaptic(stick) == 1)
            haptic = SDL_HapticOpenFromJoystick(stick);
        if (haptic)
        {
            if (SDL_HapticRumbleSupported(haptic))
                rumble_supported = SDL_HapticRumbleInit(haptic) != -1;
        }
    }

    reset_axis_config();
    wheel = a_wheel = CENTRE;
}

void Input::bind_axis(SDL_GameControllerAxis ax, int offset)
{
    if (axis[offset] == -1) axis[offset] = ax;
}

void Input::bind_button(SDL_GameControllerButton button, int offset)
{
    if (pad_config[offset] == -1) pad_config[offset] = button;
}

void Input::close_joy()
{
    if (controller != NULL)
    {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }

    if (stick != NULL)
    {
        SDL_JoystickClose(stick);
        stick = NULL;
    }

    if (haptic != NULL)
    {
        SDL_HapticClose(haptic);
        haptic = NULL;
        rumble_supported = false;
    }

    gamepad = false;
}

// Detect whether a key press change has occurred.
// The game reads its inputs 30 times a second (frame_done() runs on the tick frames only), so at
// 60 or 120 fps a press and its release can both arrive between two reads, and the old test
// (keys && !keys_old) never saw them: a quick tap of the gear, view, coin, start or menu button was
// lost. set_key_state() latches every press in keys_pressed until the next frame_done(), as
// CannonBall-DX does. The old edge test is kept as well, for the test harness, which writes keys[]
// directly and so never sets the latch: its scripted presses behave exactly as before.
bool Input::has_pressed(presses p)
{
    return keys_pressed[p] || (keys[p] && !keys_old[p]);
}

// Detect whether key is still pressed
bool Input::is_pressed(presses p)
{
    return keys[p];
}

// Detect whether pressed and clear the press
bool Input::is_pressed_clear(presses p)
{
    bool pressed = keys[p];
    keys[p] = false;
    keys_pressed[p] = false;
    return pressed;
}

// Denote that a frame has been done by copying key presses into previous array and clearing the
// press latches
void Input::frame_done()
{
    memcpy(&keys_old, &keys, sizeof(keys));
    memset(keys_pressed, 0, sizeof(keys_pressed));
}

// Every change from SDL goes through here: a press (not a repeat) sets the latch for has_pressed()
void Input::set_key_state(const int p, const bool is_pressed)
{
    if (is_pressed && !keys[p])
        keys_pressed[p] = true;
    keys[p] = is_pressed;
}

void Input::handle_key_down(SDL_Keysym* keysym)
{
    key_press = keysym->sym;
    handle_key(key_press, true);
}

void Input::handle_key_up(SDL_Keysym* keysym)
{
    handle_key(keysym->sym, false);
}
void Input::handle_key(const int key, const bool is_pressed)
{
    // Redefinable Key Input
    if (key == key_config[0])  set_key_state(UP, is_pressed);
    if (key == key_config[1])  set_key_state(DOWN, is_pressed);
    if (key == key_config[2])  set_key_state(LEFT, is_pressed);
    if (key == key_config[3])  set_key_state(RIGHT, is_pressed);
    if (key == key_config[4])  set_key_state(ACCEL, is_pressed);
    if (key == key_config[5])  set_key_state(BRAKE, is_pressed);
    if (key == key_config[6])  set_key_state(GEAR1, is_pressed);
    if (key == key_config[7])  set_key_state(GEAR2, is_pressed);
    if (key == key_config[8])  set_key_state(START, is_pressed);
    if (key == key_config[9])  set_key_state(COIN, is_pressed);
    if (key == key_config[10]) set_key_state(MENU, is_pressed);
    if (key == key_config[11]) set_key_state(VIEWPOINT, is_pressed);

    // Function keys are not redefinable
    switch (key)
    {
        case SDLK_F1:
            set_key_state(PAUSE, is_pressed);
            break;

        case SDLK_F2:
            set_key_state(STEP, is_pressed);
            break;

        case SDLK_F3:
            set_key_state(TIMER, is_pressed);
            break;

        case SDLK_F5:
            set_key_state(MENU, is_pressed);
            break;
    }
}

void Input::handle_joy_axis(SDL_JoyAxisEvent* evt)
{
    if (controller != NULL || !ours(evt->which)) return;
    handle_axis(evt->axis, evt->value);
}

void Input::handle_controller_axis(SDL_ControllerAxisEvent* evt)
{
    if (!ours(evt->which)) return;
    handle_axis(evt->axis, evt->value);
}

void Input::handle_axis(const uint8_t ax, const int16_t value_in)
{
    const int16_t value = value_in;
    // Analog Controls
    if (analog)
    {
        int workingv = value;
        //std::cout << "ax: " << (int)ax << " value " << value << std::endl;
        store_last_axis(ax, value);

        // Steering
        // OutRun requires values between 0x48 and 0xb8.
        if (ax == axis[0])
        {
            // Crossover: a wheel that reads left as positive (WHEEL SETUP sets invert[0])
            // (mirrored about 0, so that the centre stays the centre)
            const int value = !invert[0] ? (int) value_in : value_in == -32768 ? 32767 : -(int) value_in;
            wheel = ((value + 0x8000) / 0x100); // Back up this value for cab diagnostics only

            int percentage_adjust = ((wheel_zone) << 8) / 100;
            int adjusted = value + ((value * percentage_adjust) >> 8);

            // Make 0 hard left, and 0x80 centre value.
            adjusted = ((adjusted + (1 << 15)) >> 9);
            adjusted += 0x40; // Centre

            if (adjusted < 0x40)
                adjusted = 0x40;
            else if (adjusted > 0xC0)
                adjusted = 0xC0;

            // Remove Dead Zone
            if (wheel_dead)
            {
                if (std::abs(CENTRE - adjusted) <= wheel_dead)
                    adjusted = CENTRE;
            }

            //std::cout << "wheel zone : " << wheel_zone << " : " << std::hex << " : " << (int) adjusted << std::endl;
            a_wheel = adjusted;
        }
        // Accelerator [Single Axis]
        else if (ax == axis[1])
        {
            a_accel = scale_trigger(invert[1] ? -workingv : workingv);
            if (a_accel > 0xff) a_accel = 0xff;
        }

        // Brake [Single Axis]
        else if (ax == axis[2])
        {
            a_brake = scale_trigger(invert[2] ? -workingv : workingv);
            if (a_brake > 0xff) a_brake = 0xff;
        }

        // Motor Banking (Moving Cabient Only with SmartyPi)
        else if (ax == axis[3])
        {
            //a_motor = (workingv + 0x8000) / 0x100;
            a_motor = scale_trigger(workingv);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Scale the trigger value to be between 0 and 0xFF
// 
// This is based on whether this is an SDL Controller or Joystick.
// Controllers: Trigger axis values range from 0 to SDL_JOYSTICK_AXIS_MAX (32767)
// Joysticks:   Undefined, but usually between -32768 to 32767
// ------------------------------------------------------------------------------------------------

int Input::scale_trigger(const int value)
{
    if (controller != NULL)
        return value / 0x80;
    else
        return (value + 0x8000) / 0x100;
}

// ------------------------------------------------------------------------------------------------
// Store the last analog axis to be pressed and depressed beyond the cap value for config purposes
// ------------------------------------------------------------------------------------------------
void Input::store_last_axis(const uint8_t ax, const int16_t value)
{
    const static int CAP = SDL_JOYSTICK_AXIS_MAX / 4;

    if (std::abs(value) > CAP)
        axis_last = ax;
    else if (ax == axis_last)
    {
        axis_last = -1;
        axis_counter = 0;
    }

    if (ax == axis_last)
    {
        if (value > CAP*2 && axis_counter == 0) axis_counter = 1;
        if (value < CAP*2 && axis_counter == 1) axis_counter = 2;
        if (axis_counter == 2)                  axis_config = ax; // Store the axis
    }
}

int Input::get_axis_config()
{
    if (axis_counter == 2)
    {
        int value = axis_config;
        reset_axis_config();
        return value;
    }
    return -1;
}

void Input::reset_axis_config()
{
    axis_config = -1;
    axis_last = -1;
    axis_counter = 0;
}

void Input::handle_joy_down(SDL_JoyButtonEvent* evt)
{
    if (controller != NULL || !ours(evt->which)) return;
    // Latch joystick button presses for redefines
    joy_button = evt->button;
    handle_joy(evt->button, true);
}

void Input::handle_joy_up(SDL_JoyButtonEvent* evt)
{
    if (controller != NULL || !ours(evt->which)) return;
    handle_joy(evt->button, false);
}

void Input::handle_controller_down(SDL_ControllerButtonEvent* evt)
{
    if (!ours(evt->which)) return;
    joy_button = evt->button;
    handle_joy(evt->button, true);
}

void Input::handle_controller_up(SDL_ControllerButtonEvent* evt)
{
    if (!ours(evt->which)) return;
    handle_joy(evt->button, false);
}

void Input::handle_joy(const uint8_t button, const bool is_pressed)
{	
    if (button == pad_config[0])   set_key_state(ACCEL, is_pressed);
    if (button == pad_config[1])   set_key_state(BRAKE, is_pressed);
    if (button == pad_config[2])   set_key_state(GEAR1, is_pressed);
    if (button == pad_config[3])   set_key_state(GEAR2, is_pressed);
    if (button == pad_config[4])   set_key_state(START, is_pressed);
    if (button == pad_config[5])   set_key_state(COIN, is_pressed);
    if (button == pad_config[6])   set_key_state(MENU, is_pressed);
    if (button == pad_config[7])   set_key_state(VIEWPOINT, is_pressed);
    if (button == pad_config[8])   set_key_state(UP, is_pressed);
    if (button == pad_config[9])   set_key_state(DOWN, is_pressed);
    if (button == pad_config[10])  set_key_state(LEFT, is_pressed);
    if (button == pad_config[11])  set_key_state(RIGHT, is_pressed);
   
    // Limit Input Switches
    if (button == pad_config[12])  motor_limits[SW_LEFT]   = is_pressed;
    if (button == pad_config[13])  motor_limits[SW_CENTRE] = is_pressed;
    if (button == pad_config[14])  motor_limits[SW_RIGHT]  = is_pressed;
}

void Input::handle_joy_hat(SDL_JoyHatEvent* evt)
{
    if (controller != NULL || !ours(evt->which)) return;

    set_key_state(UP,    evt->value == SDL_HAT_UP);
    set_key_state(DOWN,  evt->value == SDL_HAT_DOWN);
    set_key_state(LEFT,  evt->value == SDL_HAT_LEFT);
    set_key_state(RIGHT, evt->value == SDL_HAT_RIGHT);
}

void Input::set_rumble(bool enable, float strength)
{
    if (haptic == NULL || !rumble_supported || strength == 0) return;

    if (enable)
        SDL_HapticRumblePlay(haptic, strength, 1000 / 30);
    else
        SDL_HapticRumbleStop(haptic);
}
