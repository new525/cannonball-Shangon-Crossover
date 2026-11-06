#include "renderbase.hpp"
#include "frontend/config.hpp"
#include <iostream>
#include <cmath>

RenderBase::RenderBase()
{
    surface       = NULL;
    screen_pixels = NULL;

    orig_width  = 0;
    orig_height = 0;
}

// Setup screen size (of the display the window opens on)
bool RenderBase::sdl_screen_size()
{
    static int queried = -1;    // the display orig_* came from (queried once, as before, per display)
    if (orig_width == 0 || orig_height == 0 || queried != display_index())
    {
	    SDL_DisplayMode info;
	    SDL_GetCurrentDisplayMode(display_index(), &info);

        orig_width  = info.w;
        orig_height = info.h;
        queried     = display_index();
    }

    scn_width  = orig_width;
    scn_height = orig_height;

    return true;
}

int RenderBase::display_index()
{
    const int n = SDL_GetNumVideoDisplays();
    if (config.video.display > 0 && config.video.display >= n)
    {
        static bool said = false;
        if (!said)
            std::cout << "Display " << config.video.display + 1 << " not found (" << n << " reported by SDL); using the first" << std::endl;
        said = true;
        return 0;
    }
    return config.video.display < 0 ? 0 : config.video.display;
}

bool RenderBase::moved = false;

void RenderBase::remember_display(SDL_Window* window)
{
    if (!window) return;
    const int d = SDL_GetWindowDisplayIndex(window);
    if (d < 0 || d >= SDL_GetNumVideoDisplays() || d == config.video.display) return;
    if (d != display_index())
        std::cout << "Window moved to display " << d + 1 << std::endl;
    config.video.display = d;   // also settles an out-of-range value on the display used instead
    moved = true;
}

// See: SDL_PixelFormat
#define CURRENT_RGB() (r << Rshift) | (g << Gshift) | (b << Bshift);

void RenderBase::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;

    uint32_t r = r1 * 8;
    uint32_t g = g1 * 8;
    uint32_t b = b1 * 8;

    rgb[adr] = CURRENT_RGB();

    // Create shadow colours at end of RGB array
    r = r1 * shadow_multi / 31;
    g = g1 * shadow_multi / 31;
    b = b1 * shadow_multi / 31;
        
    rgb[adr + S16_PALETTE_ENTRIES] = CURRENT_RGB(); // Add to the end of the array

    // Highlight colour code would be added here, but unused.
}

void RenderBase::set_rgb(uint32_t index, uint32_t c)
{
    uint32_t r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    rgb[index] = CURRENT_RGB();
}

void RenderBase::set_shadow_intensity(float f)
{
    shadow_multi = (int) std::round(255.0f * f);
}
