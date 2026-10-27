/***************************************************************************
    Cannonball Main Entry Point.
    
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <cstring>
#include <iostream>

// SDL Library
#include <SDL.h>

// SDL Specific Code
#include "sdl2/timer.hpp"
#include "sdl2/input.hpp"

#include "video.hpp"

#include "romloader.hpp"
#include "trackloader.hpp"
#include "stdint.hpp"
#include "main.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "frontend/menu.hpp"

#include "engine/oinputs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/omusic.hpp"

// Direct X Haptic Support.
// Fine to include on non-windows builds as dummy functions used.
#include "directx/ffeedback.hpp"

// Crossover test harness (headless scripted runs)
#include "crossover/harness.hpp"
#include "crossover/romset.hpp"
#include "crossover/bike.hpp"
#include "crossover/xaudio.hpp"

// ------------------------------------------------------------------------------------------------
// Initialize Shared Variables
// ------------------------------------------------------------------------------------------------
using namespace cannonball;

int    cannonball::state       = STATE_BOOT;
double cannonball::frame_ms    = 0;
int    cannonball::frame       = 0;
bool   cannonball::tick_frame  = true;
int    cannonball::fps_counter = 0;

// ------------------------------------------------------------------------------------------------
// Main Variables and Pointers
// ------------------------------------------------------------------------------------------------
Audio cannonball::audio;
Menu* menu;
bool pause_engine;


// ------------------------------------------------------------------------------------------------

static void quit_func(int code)
{
    harness.close();
    audio.stop_audio();
    input.close_joy();
    forcefeedback::close();
    delete menu;
    video.disable();    // crossover: release the window/GL context while SDL is still up (was in ~Video, after SDL_Quit)
    SDL_Quit();
    exit(code);
}

static void process_events(void)
{
    SDL_Event event;

    // Grab all events from the queue.
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_KEYDOWN:
                // Handle key presses.
                if (event.key.keysym.sym == SDLK_ESCAPE)
                    state = STATE_QUIT;
                else
                    input.handle_key_down(&event.key.keysym);
                break;

            case SDL_KEYUP:
                input.handle_key_up(&event.key.keysym);
                break;

            case SDL_JOYAXISMOTION:
                input.handle_joy_axis(&event.jaxis);
                break;

            case SDL_JOYBUTTONDOWN:
                input.handle_joy_down(&event.jbutton);
                break;

            case SDL_JOYBUTTONUP:
                input.handle_joy_up(&event.jbutton);
                break;

            case SDL_CONTROLLERAXISMOTION:
                input.handle_controller_axis(&event.caxis);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                input.handle_controller_down(&event.cbutton);
                break;

            case SDL_CONTROLLERBUTTONUP:
                input.handle_controller_up(&event.cbutton);
                break;

            case SDL_JOYHATMOTION:
                input.handle_joy_hat(&event.jhat);
                break;

            case SDL_JOYDEVICEADDED:
                input.open_joy();
                break;

            case SDL_JOYDEVICEREMOVED:
                input.close_joy();
                break;

            case SDL_QUIT:
                // Handle quit requests (like Ctrl-c).
                state = STATE_QUIT;
                break;
        }
    }
}

static void tick()
{
    frame++;

    // Non standard FPS: Determine whether to tick certain logic for the current frame.
    if (config.fps == 60)
        tick_frame = frame & 1;
    else if (config.fps == 120)
        tick_frame = (frame & 3) == 1;

    process_events();

    if (tick_frame)
    {
        oinputs.tick();           // Do Controls
        oinputs.do_gear();        // Digital Gear
    }
     
    switch (state)
    {
        case STATE_GAME:
        {
            if (tick_frame)
            {
                if (input.has_pressed(Input::TIMER)) outrun.freeze_timer = !outrun.freeze_timer;
                if (input.has_pressed(Input::PAUSE)) pause_engine = !pause_engine;
                if (input.has_pressed(Input::MENU))  state = STATE_INIT_MENU;
            }

            if (!pause_engine || input.has_pressed(Input::STEP))
            {
                outrun.tick(tick_frame);
                if (tick_frame) input.frame_done();
                osoundint.tick();
            }
            else
            {                
                if (tick_frame) input.frame_done();
            }
        }
        break;

        case STATE_INIT_GAME:
            if (config.engine.jap && !roms.load_japanese_roms())
            {
                state = STATE_QUIT;
            }
            else
            {
                tick_frame = true;
                pause_engine = false;
                outrun.init();
                state = STATE_GAME;
            }
            break;

        case STATE_MENU:
            menu->tick();
            input.frame_done();
            osoundint.tick();
            break;

        case STATE_INIT_MENU:
            oinputs.init();
            outrun.outputs->init();
            menu->init();
            state = STATE_MENU;
            break;
    }

    // Map OutRun outputs to CannonBall devices (SmartyPi Interface / Controller Rumble)
    outrun.outputs->writeDigitalToConsole();
    if (tick_frame)
    {
         input.set_rumble(outrun.outputs->is_set(OOutputs::D_MOTOR), config.controls.rumble);
    }
}

static void main_loop()
{
    // FPS Counter (If Enabled)
    Timer fps_count;
    int frame = 0;
    fps_count.start();

    // General Frame Timing
    bool vsync = config.video.vsync == 1 && video.supports_vsync();
    Timer frame_time;
    int t;                              // Actual timing of tick in ms as measured by SDL (ms)
    double deltatime  = 0;              // Time we want an entire frame to take (ms)
    int deltaintegral = 0;              // Integer version of above

    while (state != STATE_QUIT)
    {
        frame_time.start();
        if (harness.active) harness.before_tick();
        // Tick Engine
        tick();

        // Draw SDL Video
        video.prepare_frame();
        video.render_frame();
        harness.after_render(video.pixels, config.s16_width, config.s16_height);

        // Fill SDL Audio Buffer For Callback
        audio.tick();
        
        // Calculate Timings. Cap Frame Rate. Note this might be trumped by V-Sync
        if (!vsync && !harness.headless)
        {
            deltatime += (frame_ms * audio.adjust_speed());
            deltaintegral = (int)deltatime;
            t = frame_time.get_ticks();
            
            if (t < deltatime)
                SDL_Delay((Uint32)(deltatime - t));

            deltatime -= deltaintegral;
        }

        if (config.video.fps_count)
        {
            frame++;
            // One second has elapsed
            if (fps_count.get_ticks() >= 1000)
            {
                fps_counter = frame;
                frame       = 0;
                fps_count.start();
            }
        }
    }

    quit_func(0);
}

// Very (very) simple command line parser.
// Returns true if everything is ok to proceed with launching th engine.
static bool parse_command_line(int argc, char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-cfgfile") == 0 && i+1 < argc)
        {
            config.set_config_file(argv[i+1]);
        }
        else if (strcmp(argv[i], "-file") == 0 && i+1 < argc)
        {
            if (!trackloader.set_layout_track(argv[i+1]))
                return false;
        }
        else if (harness.parse_arg(i, argc, argv))
        {
        }
        else if (strcmp(argv[i], "-help") == 0)
        {
            std::cout << "Command Line Options:\n\n" <<
                         "-cfgfile: Location and name of config.xml\n" <<
                         "-file   : LayOut Editor track data to load\n" <<
                         "-headless, -script <file>, -snapdir <dir>, -wav <file>: test harness (see crossover/harness.cpp)\n" << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    // Parse command line arguments (config file location, LayOut data) 
    bool ok = parse_command_line(argc, argv);

    if (ok)
    {
        config.load(); // Load config.XML file
        ok = harness.load() && roms.load_revb_roms(config.sound.fix_samples);
    }
    if (!ok)
    {
        quit_func(1);
        return 0;
    }

    // Load gamecontrollerdb.txt mappings
    if (SDL_GameControllerAddMappingsFromFile((config.data.res_path + "gamecontrollerdb.txt").c_str()) == -1)
        std::cout << "Unable to load controller mapping" << std::endl;

    // Initialize timer and video systems
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) == -1)
    {
        std::cerr << "SDL Initialization Failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Load patched widescreen tilemaps
    if (!omusic.load_widescreen_map(config.data.res_path))
        std::cout << "Unable to load widescreen tilemaps" << std::endl;

    // Initialize SDL Video
    config.set_fps(config.video.fps);
    if (!video.init(&roms, &config.video))
        quit_func(1);

    // Crossover: other games' ROMs and the bike
    if (sh_roms.load(config.crossover.shangon_rompath))
        bike.init();
    else
        std::cout << "Super Hang-On ROMs not loaded: " << sh_roms.error << std::endl;
    bike.enabled = config.crossover.vehicle == 1 && bike.ready;
    if (!ho_roms.load(config.crossover.hangon_rompath))
        std::cout << "Hang-On ROMs not loaded: " << ho_roms.error << std::endl;
    if (!xaudio.init(config.sound.rate, config.fps))
        std::cout << "No crossover sound boards started" << std::endl;
    xaudio.sh_vehicle = bike.enabled && xaudio.ready(CrossoverAudio::SHANGON);

    // The other games' race music joins the music selection (titles from the games' own data;
    // see FINDINGS_physics_track_sound.md: Super Hang-On's sound test list, Hang-On's race music $9D)
    auto add_track = [](int type, int cmd, const char* title) {
        music_t m; m.type = type; m.cmd = cmd; m.title = title; config.sound.music.push_back(m);
    };
    if (xaudio.ready(CrossoverAudio::SHANGON))
    {
        add_track(music_t::IS_SHANGON, 0x9D, "OUTRIDE A CRISIS");
        add_track(music_t::IS_SHANGON, 0x82, "SPRINTER");
        add_track(music_t::IS_SHANGON, 0x96, "WINNING RUN");
        add_track(music_t::IS_SHANGON, 0x81, "HARD ROAD");
    }
    if (xaudio.ready(CrossoverAudio::HANGON))
        add_track(music_t::IS_HANGON, 0x9D, "HANG ON");

    // Initialize SDL Audio
    audio.init();

    state = config.menu.enabled ? STATE_INIT_MENU : STATE_INIT_GAME;

    // Initalize SDL Controls
    input.init(config.controls.pad_id,
               config.controls.keyconfig, config.controls.padconfig, 
               config.controls.analog,    config.controls.axis, config.controls.invert, config.controls.asettings);

    if (config.controls.haptic) 
        config.controls.haptic = forcefeedback::init(config.controls.max_force, config.controls.min_force, config.controls.force_duration);
        
    // Populate menus
    menu = new Menu();
    menu->populate();
    main_loop();  // Loop until we quit the app

    // Never Reached
    return 0;
}
