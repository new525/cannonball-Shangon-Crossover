/***************************************************************************
    Force feedback device on SDL2 Haptics. See ffbdevice.hpp.

    Three effects are created once and updated in place (SDL_HapticUpdateEffect, then
    SDL_HapticRunEffect; a level of 0 stops the effect instead). A wheel (SDL_JOYSTICK_TYPE_WHEEL)
    takes SDL_HAPTIC_STEERING_AXIS directions with a signed constant level; any other joystick a
    cartesian direction with the sign in the direction, as CannonBall-DX's backend does.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "ffb/ffbdevice.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <SDL.h>

#include "main.hpp"
#include "crossover/harness.hpp"

namespace ffbdev
{
    static SDL_Joystick* joystick = nullptr;
    static SDL_Haptic*   haptic   = nullptr;
    static unsigned int  caps     = 0;
    static bool          is_wheel = false;
    static int           fx_constant = -1, fx_spring = -1, fx_periodic = -1;
    static bool          supported = false;
    static int           gain = 100;
    static int           cur_constant = 0, cur_spring = 0, cur_periodic = 0, cur_period = 0;
    static FILE*         log = nullptr;
    static std::string   status_text = "NOT TRIED";     // status()
    static std::vector<std::string> steps;              // what the last init() did, for report()

    static void step(const std::string& s)
    {
        steps.push_back(s);
        std::cout << "Force feedback: " << s << std::endl;
    }

    static std::string hex4(unsigned v)
    {
        char b[8];
        snprintf(b, sizeof b, "%04x", v & 0xFFFF);
        return b;
    }

    // "name" vvvv:pppp
    static std::string describe(int index)
    {
        const char* n = SDL_JoystickNameForIndex(index);
        return std::string("joystick ") + std::to_string(index) + " \"" + (n ? n : "?") + "\" " +
               hex4(SDL_JoystickGetDeviceVendor(index)) + ":" + hex4(SDL_JoystickGetDeviceProduct(index));
    }

    static std::string caps_text(unsigned int c)
    {
        static const struct { unsigned int bit; const char* name; } NAMES[] = {
            { SDL_HAPTIC_CONSTANT, "constant" }, { SDL_HAPTIC_SINE, "sine" }, { SDL_HAPTIC_LEFTRIGHT, "leftright" },
            { SDL_HAPTIC_TRIANGLE, "triangle" }, { SDL_HAPTIC_SAWTOOTHUP, "sawtoothup" },
            { SDL_HAPTIC_SAWTOOTHDOWN, "sawtoothdown" }, { SDL_HAPTIC_RAMP, "ramp" }, { SDL_HAPTIC_SPRING, "spring" },
            { SDL_HAPTIC_DAMPER, "damper" }, { SDL_HAPTIC_INERTIA, "inertia" }, { SDL_HAPTIC_FRICTION, "friction" },
            { SDL_HAPTIC_CUSTOM, "custom" }, { SDL_HAPTIC_GAIN, "gain" }, { SDL_HAPTIC_AUTOCENTER, "autocenter" },
            { SDL_HAPTIC_STATUS, "status" }, { SDL_HAPTIC_PAUSE, "pause" },
        };
        std::string s;
        for (const auto& n : NAMES)
            if (c & n.bit) s += (s.empty() ? "" : " ") + std::string(n.name);
        return s.empty() ? "none" : s;
    }

    static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static void logf(const char* fmt, ...)
    {
        if (!log) return;
        fprintf(log, "f%d ", harness.active ? harness.frame : cannonball::frame);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(log, fmt, ap);
        va_end(ap);
        fputc('\n', log);
        fflush(log);
    }

    static SDL_HapticDirection direction(int sign)
    {
        SDL_HapticDirection d;
        memset(&d, 0, sizeof d);
        d.type = is_wheel ? SDL_HAPTIC_STEERING_AXIS : SDL_HAPTIC_CARTESIAN;
        d.dir[0] = is_wheel ? 1 : sign;
        return d;
    }

    static bool read_target(Uint16& vid, Uint16& pid)
    {
        const char* env = getenv("FF_TARGET_VIDPID");
        unsigned v = 0, p = 0;
        if (!env || !*env || sscanf(env, "%x:%x", &v, &p) != 2) return false;
        vid = (Uint16) v;
        pid = (Uint16) p;
        return vid != 0 || pid != 0;
    }

    // try one joystick: a haptic interface with a constant force
    static bool try_open(int index)
    {
        const std::string who = describe(index);
        SDL_Joystick* j = SDL_JoystickOpen(index);
        if (!j) { step(who + ": cannot open the joystick: " + SDL_GetError()); return false; }
        SDL_ClearError();
        SDL_Haptic* h = SDL_HapticOpenFromJoystick(j);
        if (!h)
        {
            step(who + ": no force feedback interface: " + SDL_GetError());
            SDL_JoystickClose(j);
            return false;
        }
        unsigned int c = SDL_HapticQuery(h);
        if (!(c & SDL_HAPTIC_CONSTANT))
        {
            step(who + ": no constant force (it has: " + caps_text(c) + ")");
            SDL_HapticClose(h);
            SDL_JoystickClose(j);
            return false;
        }
        joystick = j;
        haptic = h;
        caps = c;
        is_wheel = SDL_JoystickGetType(j) == SDL_JOYSTICK_TYPE_WHEEL;
        step(who + ": opened (" + caps_text(c) + ")");
        return true;
    }

    // SDL's list of haptic devices, for one whose joystick side has no force feedback interface
    // (read by SDL through a driver that has none); a wheel if a wheel joystick has its name
    static bool try_open_haptic(int index)
    {
        const char* n = SDL_HapticName(index);
        const std::string who = std::string("haptic device ") + std::to_string(index) + " \"" + (n ? n : "?") + "\"";
        SDL_ClearError();
        SDL_Haptic* h = SDL_HapticOpen(index);
        if (!h) { step(who + ": cannot open: " + SDL_GetError()); return false; }
        unsigned int c = SDL_HapticQuery(h);
        if (!(c & SDL_HAPTIC_CONSTANT))
        {
            step(who + ": no constant force (it has: " + caps_text(c) + ")");
            SDL_HapticClose(h);
            return false;
        }
        joystick = nullptr;
        haptic = h;
        caps = c;
        is_wheel = false;
        for (int i = 0; i < SDL_NumJoysticks(); i++)
        {
            const char* jn = SDL_JoystickNameForIndex(i);
            if (n && jn && SDL_JoystickGetDeviceType(i) == SDL_JOYSTICK_TYPE_WHEEL && strcmp(n, jn) == 0)
                is_wheel = true;
        }
        step(who + ": opened (" + caps_text(c) + ")");
        return true;
    }

    static bool select_device(int pad_id)
    {
        const int count = SDL_NumJoysticks();
        Uint16 vid = 0, pid = 0;
        if (read_target(vid, pid))
        {
            for (int i = 0; i < count; i++)
                if ((!vid || SDL_JoystickGetDeviceVendor(i) == vid) && (!pid || SDL_JoystickGetDeviceProduct(i) == pid) && try_open(i))
                    return true;
            step("no device matches FF_TARGET_VIDPID");
        }
        if (count == 0) step("SDL reports no joysticks");
        if (pad_id >= 0 && pad_id < count && try_open(pad_id))
            return true;
        for (int i = 0; i < count; i++)
            if (i != pad_id && SDL_JoystickGetDeviceType(i) == SDL_JOYSTICK_TYPE_WHEEL && try_open(i))
                return true;
        for (int i = 0; i < count; i++)
            if (i != pad_id && SDL_JoystickGetDeviceType(i) != SDL_JOYSTICK_TYPE_WHEEL && try_open(i))
                return true;
        const int haptics = SDL_NumHaptics();
        for (int i = 0; i < haptics; i++)
            if (try_open_haptic(i))
                return true;
        return false;
    }

    static bool create_effects()
    {
        SDL_HapticEffect e;
        memset(&e, 0, sizeof e);
        e.type = SDL_HAPTIC_CONSTANT;
        e.constant.direction = direction(1);
        e.constant.length = SDL_HAPTIC_INFINITY;
        e.constant.level = 0;
        fx_constant = SDL_HapticNewEffect(haptic, &e);
        if (fx_constant < 0 && is_wheel)
        {
            // the steering-axis direction refused (a driver or an SDL without it): the same force as
            // a cartesian one along the device's first axis, as for a device that is not a wheel
            step(std::string("the constant force along the steering axis was refused (") + SDL_GetError() +
                 "): trying a cartesian direction");
            is_wheel = false;
            e.constant.direction = direction(1);
            fx_constant = SDL_HapticNewEffect(haptic, &e);
        }
        if (fx_constant < 0)
        {
            step(std::string("the constant force effect could not be created: ") + SDL_GetError());
            return false;
        }
        if (caps & SDL_HAPTIC_SPRING)
        {
            memset(&e, 0, sizeof e);
            e.type = SDL_HAPTIC_SPRING;
            e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;    // a condition effect: always the steering axis
            e.condition.direction.dir[0] = 1;
            e.condition.length = SDL_HAPTIC_INFINITY;
            e.condition.left_sat[0] = e.condition.right_sat[0] = 0xFFFF;
            fx_spring = SDL_HapticNewEffect(haptic, &e);
            if (fx_spring < 0)
                step(std::string("the spring effect could not be created: ") + SDL_GetError());
        }
        if (caps & SDL_HAPTIC_SINE)
        {
            memset(&e, 0, sizeof e);
            e.type = SDL_HAPTIC_SINE;
            e.periodic.direction = direction(1);
            e.periodic.length = SDL_HAPTIC_INFINITY;
            e.periodic.period = 45;
            e.periodic.magnitude = 0;
            fx_periodic = SDL_HapticNewEffect(haptic, &e);
            if (fx_periodic < 0)
                step(std::string("the periodic effect could not be created: ") + SDL_GetError());
        }
        return true;
    }

    bool init(int pad_id)
    {
        if (supported) return true;
        if (const char* path = getenv("CB_FFB_LOG"))
        {
            if (!log) log = fopen(path, "w");
            if (log) { supported = true; logf("init pad %d (log double, no device)", pad_id); }
        }
        if (log) { status_text = "LOG ONLY"; return true; }

        steps.clear();
        if (!select_device(pad_id))
        {
            status_text = SDL_NumJoysticks() == 0 ? "NO JOYSTICK FOUND" : "NO FFB ON DEVICE";
            return false;
        }
        if (caps & SDL_HAPTIC_AUTOCENTER) SDL_HapticSetAutocenter(haptic, 0);    // the spring effect centres instead
        if (caps & SDL_HAPTIC_GAIN) SDL_HapticSetGain(haptic, 100);
        if (!create_effects())
        {
            close();
            status_text = "FFB EFFECT REFUSED";
            return false;
        }
        const char* name = joystick ? SDL_JoystickName(joystick) : SDL_HapticName(SDL_HapticIndex(haptic));
        step(std::string("using ") + (name ? name : "device") + (is_wheel ? " (wheel)" : "") +
             ", spring " + (fx_spring >= 0 ? "yes" : "no") + ", periodic " + (fx_periodic >= 0 ? "yes" : "no"));
        status_text = name ? name : "DEVICE";
        supported = true;
        return true;
    }

    const char* status() { return status_text.c_str(); }

    std::string report()
    {
        std::ostringstream o;
        SDL_version v;
        SDL_GetVersion(&v);
        o << "SDL " << (int) v.major << "." << (int) v.minor << "." << (int) v.patch << " on " << SDL_GetPlatform() << "\n";
        const char* env = getenv("FF_TARGET_VIDPID");
        o << "FF_TARGET_VIDPID: " << (env && *env ? env : "(not set)") << "\n\n";

        const int count = SDL_NumJoysticks();
        o << "Joysticks: " << count << "\n";
        for (int i = 0; i < count; i++)
        {
            static const char* TYPES[] = { "unknown", "game controller", "wheel", "arcade stick", "flight stick",
                                           "dance pad", "guitar", "drum kit", "arcade pad", "throttle" };
            const int type = (int) SDL_JoystickGetDeviceType(i);
            char guid[40];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
            o << " " << describe(i) << "\n";
            o << "   type: " << (type >= 0 && type < 10 ? TYPES[type] : "?")
              << "; SDL game controller mapping: " << (SDL_IsGameController(i) ? "yes" : "no") << "; GUID " << guid << "\n";
            SDL_Joystick* j = SDL_JoystickOpen(i);
            if (!j) { o << "   cannot open: " << SDL_GetError() << "\n"; continue; }
            o << "   axes " << SDL_JoystickNumAxes(j) << ", buttons " << SDL_JoystickNumButtons(j)
              << ", hats " << SDL_JoystickNumHats(j) << "\n   axis values now:";
            for (int a = 0; a < SDL_JoystickNumAxes(j) && a < 16; a++) o << " " << a << "=" << SDL_JoystickGetAxis(j, a);
            // (hold a button while the report is written to learn its number)
            o << "\n   buttons held now:";
            bool any = false;
            for (int b = 0; b < SDL_JoystickNumButtons(j); b++)
                if (SDL_JoystickGetButton(j, b)) { o << " " << b; any = true; }
            if (!any) o << " none";
            for (int h = 0; h < SDL_JoystickNumHats(j); h++) o << "; hat " << h << "=" << (int) SDL_JoystickGetHat(j, h);
            o << "\n";
            SDL_ClearError();
            const int is_haptic = SDL_JoystickIsHaptic(j);
            if (is_haptic != 1)
                o << "   force feedback interface: no" << (*SDL_GetError() ? std::string(" (") + SDL_GetError() + ")" : "") << "\n";
            else if (SDL_Haptic* h = SDL_HapticOpenFromJoystick(j))
            {
                o << "   force feedback interface: yes; effects: " << caps_text(SDL_HapticQuery(h))
                  << "; axes " << SDL_HapticNumAxes(h) << "; effects at once " << SDL_HapticNumEffectsPlaying(h) << "\n";
                SDL_HapticClose(h);
            }
            else
                o << "   force feedback interface: yes, but it cannot be opened: " << SDL_GetError() << "\n";
            SDL_JoystickClose(j);
        }

        const int haptics = SDL_NumHaptics();
        o << "\nSDL haptic devices: " << haptics << "\n";
        for (int i = 0; i < haptics; i++)
        {
            const char* n = SDL_HapticName(i);
            o << " " << i << " \"" << (n ? n : "?") << "\"" << (SDL_HapticOpened(i) ? " (open)" : "") << "\n";
        }

        o << "\nLast attempt to open a force feedback device (" << status_text << "):\n";
        if (steps.empty()) o << " (none yet: turn FORCE FEEDBACK on)\n";
        for (const std::string& s : steps) o << " " << s << "\n";
        return o.str();
    }

    void close()
    {
        if (log)
        {
            logf("close");
            fclose(log);
            log = nullptr;
        }
        if (haptic)
        {
            if (fx_periodic >= 0) { SDL_HapticStopEffect(haptic, fx_periodic); SDL_HapticDestroyEffect(haptic, fx_periodic); }
            if (fx_spring >= 0)   { SDL_HapticStopEffect(haptic, fx_spring);   SDL_HapticDestroyEffect(haptic, fx_spring); }
            if (fx_constant >= 0) { SDL_HapticStopEffect(haptic, fx_constant); SDL_HapticDestroyEffect(haptic, fx_constant); }
            SDL_HapticClose(haptic);
        }
        if (joystick) SDL_JoystickClose(joystick);
        haptic = nullptr;
        joystick = nullptr;
        caps = 0;
        is_wheel = false;
        fx_constant = fx_spring = fx_periodic = -1;
        supported = false;
        cur_constant = cur_spring = cur_periodic = cur_period = 0;
    }

    bool is_supported() { return supported; }

    int instance_id() { return joystick ? SDL_JoystickInstanceID(joystick) : -1; }

    void set_gain(int percent)
    {
        gain = clamp(percent, 0, 100);
        logf("gain %d", gain);
        // the running effects at the new gain
        const int c = cur_constant, s = cur_spring, p = cur_periodic, t = cur_period;
        if (c) { cur_constant = -1000; set_constant(c); }
        if (s) { cur_spring = -1000; set_spring(s); }
        if (p) { cur_periodic = -1000; set_periodic(p, t); }
    }

    static int scaled(int percent) { return clamp(percent, -100, 100) * 0x7FFF * gain / (100 * 100); }

    void set_constant(int level)
    {
        level = clamp(level, -100, 100);
        if (level == cur_constant) return;
        cur_constant = level;
        logf("constant %d", level);
        if (!haptic || fx_constant < 0) return;
        const int value = scaled(level);
        if (value == 0)
        {
            SDL_HapticStopEffect(haptic, fx_constant);
            return;
        }
        SDL_HapticEffect e;
        memset(&e, 0, sizeof e);
        e.type = SDL_HAPTIC_CONSTANT;
        e.constant.direction = direction(value < 0 ? -1 : 1);
        e.constant.length = SDL_HAPTIC_INFINITY;
        e.constant.level = (Sint16)(is_wheel ? value : abs(value));
        if (SDL_HapticUpdateEffect(haptic, fx_constant, &e) == 0)
            SDL_HapticRunEffect(haptic, fx_constant, 1);
    }

    void set_spring(int strength)
    {
        strength = clamp(strength, 0, 100);
        if (strength == cur_spring) return;
        cur_spring = strength;
        logf("spring %d", strength);
        if (!haptic || fx_spring < 0) return;
        const int value = scaled(strength);
        if (value == 0)
        {
            SDL_HapticStopEffect(haptic, fx_spring);
            return;
        }
        SDL_HapticEffect e;
        memset(&e, 0, sizeof e);
        e.type = SDL_HAPTIC_SPRING;
        e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.condition.direction.dir[0] = 1;
        e.condition.length = SDL_HAPTIC_INFINITY;
        e.condition.left_sat[0] = e.condition.right_sat[0] = 0xFFFF;
        e.condition.left_coeff[0] = e.condition.right_coeff[0] = (Sint16) value;
        if (SDL_HapticUpdateEffect(haptic, fx_spring, &e) == 0)
            SDL_HapticRunEffect(haptic, fx_spring, 1);
    }

    void set_periodic(int magnitude, int period_ms)
    {
        magnitude = clamp(magnitude, 0, 100);
        period_ms = clamp(period_ms, 1, 10000);
        if (magnitude == 0) period_ms = 0;
        if (magnitude == cur_periodic && period_ms == cur_period) return;
        cur_periodic = magnitude;
        cur_period = period_ms;
        logf("periodic %d %d", magnitude, period_ms);
        if (!haptic || fx_periodic < 0) return;
        const int value = scaled(magnitude);
        if (value == 0)
        {
            SDL_HapticStopEffect(haptic, fx_periodic);
            return;
        }
        SDL_HapticEffect e;
        memset(&e, 0, sizeof e);
        e.type = SDL_HAPTIC_SINE;
        e.periodic.direction = direction(1);
        e.periodic.length = SDL_HAPTIC_INFINITY;
        e.periodic.period = (Uint16) period_ms;
        e.periodic.magnitude = (Sint16) value;
        if (SDL_HapticUpdateEffect(haptic, fx_periodic, &e) == 0)
            SDL_HapticRunEffect(haptic, fx_periodic, 1);
    }

    bool logging() { return log != nullptr; }
    void log_line(const char* text) { logf("%s", text); }

    void stop()
    {
        if (!supported) return;
        if (cur_constant == 0 && cur_spring == 0 && cur_periodic == 0) return;
        logf("stop");
        cur_constant = cur_spring = cur_periodic = cur_period = 0;
        if (!haptic) return;
        if (fx_constant >= 0) SDL_HapticStopEffect(haptic, fx_constant);
        if (fx_spring >= 0)   SDL_HapticStopEffect(haptic, fx_spring);
        if (fx_periodic >= 0) SDL_HapticStopEffect(haptic, fx_periodic);
    }
}
