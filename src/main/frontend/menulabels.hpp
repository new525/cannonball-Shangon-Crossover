#pragma once

// ------------------------------------------------------------------------------------------------
// Text Labels for menus
// ------------------------------------------------------------------------------------------------

// Back Labels
const static char* ENTRY_BACK = "BACK";

// Main Menu
const static char* ENTRY_PLAYGAME = "PLAY GAME";

// Crossover race setup
const static char* ENTRY_X_START   = "START RACE";
const static char* ENTRY_X_VEHICLE = "VEHICLE  ";
const static char* ENTRY_X_BIKESIZE = "BIKE SIZE  ";
const static char* ENTRY_X_TRACK   = "TRACK  ";
const static char* ENTRY_X_MUSIC   = "MUSIC  ";
const static char* ENTRY_X_GEARBOX = "GEARBOX  ";     // OutRunners' cars only
const static char* ENTRY_X_BALANCE = "BALANCE  ";
const static char* ENTRY_GAMEMODES = "GAME MODES";
const static char* ENTRY_SETTINGS = "SETTINGS";
const static char* ENTRY_ABOUT = "ABOUT";
const static char* ENTRY_EXIT = "EXIT";

// Main Menu (cabinet)
const static char* ENTRY_DIPS       = "DIP SWITCHES";
const static char* ENTRY_CABTESTS   = "HARDWARE TESTS";
const static char* ENTRY_EXSETTINGS = "EXTRA SETTINGS";

// Game Modes Menu
const static char* ENTRY_ENHANCED = "SET ENHANCED MODE";
const static char* ENTRY_ORIGINAL = "SET ORIGINAL MODE";
const static char* ENTRY_CONT = "CONTINUOUS MODE";
const static char* ENTRY_TIMETRIAL = "TIME TRIAL MODE";

// Time Trial Menu
const static char* ENTRY_START = "START TIME TRIAL";
const static char* ENTRY_LAPS = "NO OF LAPS ";

// Continuous Menu
const static char* ENTRY_START_CONT = "START CONTINUOUS MODE";

// Settings Menu
const static char* ENTRY_VIDEO = "VIDEO";
const static char* ENTRY_SOUND = "SOUND";
const static char* ENTRY_CONTROLS = "CONTROLS";
const static char* ENTRY_ENGINE = "GAME ENGINE";
const static char* ENTRY_SCORES = "CLEAR HISCORES";
const static char* ENTRY_SAVE = "SAVE AND RETURN";
const static char* ENTRY_ENHANCE = "ENHANCEMENTS";

// SMARTYPI Extra Settings
const static char* ENTRY_S_CAB = "CABINET TYPE ";
const static char* ENTRY_FREEPLAY = "FREEPLAY ";
const static char* ENTRY_S_MOTOR = "MOTOR TEST";
const static char* ENTRY_S_INPUTS = "INPUT TEST";
const static char* ENTRY_S_OUTPUTS = "OUTPUT TEST";
const static char* ENTRY_S_CRT = "CRT TEST";
const static char* ENTRY_TIMER = "TIMING FIXES ";
const static char* ENTRY_S_BUGS = "BUG FIXES ";
const static char* ENTRY_TRAFFICFIX = "TRAFFIC BRAKE FIX ";
const static char* ENTRY_HISCOREKEYS = "DIGITAL NAME ENTRY ";

// Bug Fixes Sub-Menu (each of fix_bugs' fixes on its own; ENTRY_S_BUGS is the master switch)
const static char* ENTRY_SUB_FIXES   = "SELECT BUG FIXES";
const static char* ENTRY_FIX_ENDPAL  = "ENDING PALETTE ";
const static char* ENTRY_FIX_TROPHY  = "ENDING TROPHY ";
const static char* ENTRY_FIX_LAPTIME = "LAP TIME HUNDREDTHS ";
const static char* ENTRY_FIX_MUSTILE = "MUSIC SCREEN TILE ";
const static char* ENTRY_FIX_MAPSEA  = "MAP SEA EDGE ";
const static char* ENTRY_FIX_STEER   = "STEERING FROM REST ";
const static char* ENTRY_FIX_SPREDGE = "SPRITE RIGHT EDGE ";
const static char* ENTRY_FIX_ROADLN  = "ROAD LINE ON MENUS ";

// Video Menu
const static char* ENTRY_FPS = "FRAME RATE ";
const static char* ENTRY_FULLSCREEN = "FULL SCREEN ";
const static char* ENTRY_DISPLAY = "DISPLAY ";
const static char* ENTRY_WIDESCREEN = "WIDESCREEN ";
const static char* ENTRY_HIRES = "HIRES ";
const static char* ENTRY_SCALE = "WINDOW SCALE ";
const static char* ENTRY_SCANLINES = "SCANLINES ";

// Sound Menu
const static char* ENTRY_MUTE = "SOUND ";
const static char* ENTRY_SNDDEVICE = "OUTPUT DEVICE ";
const static char* ENTRY_BGM = "BGM VOL ";
const static char* ENTRY_SFX = "SFX VOL ";
const static char* ENTRY_ADVERTISE = "ATTRACT SOUND ";
const static char* ENTRY_PREVIEWSND = "PREVIEW MUSIC ";
const static char* ENTRY_FIXSAMPLES = "FIX SAMPLES ";
const static char* ENTRY_MUSICTEST = "MUSIC TEST";

// Controls Menu
const static char* ENTRY_GEAR = "GEAR ";
const static char* ENTRY_CONFIGUREGP = "CONFIGURE GAMEPAD";
const static char* ENTRY_REDEFKEY = "REDEFINE KEYS";
const static char* ENTRY_DSTEER = "DIGITAL STEER SPEED ";
const static char* ENTRY_DPEDAL = "DIGITAL PEDAL SPEED ";
// Crossover: force feedback (ffb/ffb)
const static char* ENTRY_FFB = "FORCE FEEDBACK ";
const static char* ENTRY_FFB_STRENGTH = "FFB STRENGTH ";
const static char* ENTRY_FFB_SPRING = "SPRING ";

// GamePad Menu
const static char* ENTRY_ANALOG = "ANALOG ";
const static char* ENTRY_RUMBLE = "RUMBLE STRENGTH ";
const static char* ENTRY_REDEFJOY = "REDEFINE GAMEPAD";

// Game Engine Menu
const static char* ENTRY_TRACKS = "TRACKS ";
const static char* ENTRY_TIME = "TIME ";
const static char* ENTRY_TRAFFIC = "TRAFFIC ";
const static char* ENTRY_SUB_ENHANCEMENTS = "ENHANCEMENTS";
const static char* ENTRY_SUB_HANDLING = "CAR SETUP";

// Game Engine: Enhancements Sub-Menu
const static char* ENTRY_ATTRACT = "NEW ATTRACT ";
const static char* ENTRY_PROTOTYPE = "PROTOTYPE STAGE 1 ";
const static char* ENTRY_OBJECTS = "OBJECTS ";

// Game Engine: Car Setup Sub-Menu
const static char* ENTRY_GRIP    = "GRIPPY TYRES ";
const static char* ENTRY_OFFROAD = "OFFROAD TYRES ";
const static char* ENTRY_BUMPER  = "STRONG BUMPER ";
const static char* ENTRY_TURBO   = "FASTER CAR ";
const static char* ENTRY_COLOR   = "COLOR ";

const static char* COLOR_LABELS[8] = { "RED", "BLUE", "YELLOW", "GREEN", "CYAN", "BLACK", "WHITE", "SILVER" };

// Music Test Menu
const static char* ENTRY_MUSIC1 = "PLAY TRACK";
const static char* ENTRY_MUSIC2 = "TRACK - ";
const static char* ENTRY_MUSIC3 = "LAST WAVE";

const static char* DIP_DIFFICULTY[4] = { "EASY", "NORMAL", "HARD", "HARDEST" };
const static char* GEAR_LABELS[4] = {"MANUAL", "MANUAL CABINET", "MANUAL 2 BUTTONS", "AUTOMATIC"};
const static char* FPS_LABELS[3] = { "30 FPS", "ORIGINAL", "60 FPS" };
const static char* ANALOG_LABELS[3] = { "OFF", "ON", "ON WHEEL ONLY" };
const static char* VIDEO_LABELS[3] = { "OFF", "ON", "STRETCH" };
const static char* RUMBLE_LABELS[5] = { "OFF", "LOW", "MED", "HIGH", "FULL" };
const static char* CAB_LABELS[3] = { "MOVING", "UPRIGHT", "MINI" };