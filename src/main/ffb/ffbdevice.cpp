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
        SDL_Joystick* j = SDL_JoystickOpen(index);
        if (!j) return false;
        SDL_Haptic* h = SDL_HapticOpenFromJoystick(j);
        if (!h) { SDL_JoystickClose(j); return false; }
        unsigned int c = SDL_HapticQuery(h);
        if (!(c & SDL_HAPTIC_CONSTANT)) { SDL_HapticClose(h); SDL_JoystickClose(j); return false; }
        joystick = j;
        haptic = h;
        caps = c;
        is_wheel = SDL_JoystickGetType(j) == SDL_JOYSTICK_TYPE_WHEEL;
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
            std::cout << "Force feedback: no device matches FF_TARGET_VIDPID" << std::endl;
        }
        if (pad_id >= 0 && pad_id < count && try_open(pad_id))
            return true;
        for (int i = 0; i < count; i++)
            if (SDL_JoystickGetDeviceType(i) == SDL_JOYSTICK_TYPE_WHEEL && try_open(i))
                return true;
        for (int i = 0; i < count; i++)
            if (try_open(i))
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
        if (fx_constant < 0)
        {
            std::cout << "Force feedback: constant force effect unavailable: " << SDL_GetError() << std::endl;
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
                std::cout << "Force feedback: spring effect unavailable: " << SDL_GetError() << std::endl;
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
                std::cout << "Force feedback: periodic effect unavailable: " << SDL_GetError() << std::endl;
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
        if (log) return true;

        if (!select_device(pad_id))
            return false;
        if (caps & SDL_HAPTIC_AUTOCENTER) SDL_HapticSetAutocenter(haptic, 0);    // the spring effect centres instead
        if (caps & SDL_HAPTIC_GAIN) SDL_HapticSetGain(haptic, 100);
        if (!create_effects())
        {
            close();
            return false;
        }
        std::cout << "Force feedback: " << (SDL_JoystickName(joystick) ? SDL_JoystickName(joystick) : "device")
                  << (is_wheel ? " (wheel)" : "") << ", spring " << (fx_spring >= 0 ? "yes" : "no")
                  << ", periodic " << (fx_periodic >= 0 ? "yes" : "no") << std::endl;
        supported = true;
        return true;
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
