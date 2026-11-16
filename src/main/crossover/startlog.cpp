/***************************************************************************
    Start-up and race-start measurement (testing). See startlog.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/startlog.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define PSAPI_VERSION 2                 // GetProcessMemoryInfo from kernel32 (K32GetProcessMemoryInfo)
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
typedef std::chrono::steady_clock Clock;
const Clock::time_point t0 = Clock::now();
Clock::time_point last = t0;
bool race_pending = false;

// Resident memory now and its peak, in KiB
void memory(long& rss, long& peak)
{
    rss = peak = 0;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc))
    {
        rss  = (long)(pmc.WorkingSetSize / 1024);
        peak = (long)(pmc.PeakWorkingSetSize / 1024);
    }
#elif defined(__linux__)
    if (FILE* f = fopen("/proc/self/status", "r"))
    {
        char line[256];
        while (fgets(line, sizeof line, f))
        {
            if (!strncmp(line, "VmRSS:", 6)) rss = atol(line + 6);
            else if (!strncmp(line, "VmHWM:", 6)) peak = atol(line + 6);
        }
        fclose(f);
    }
#endif
}
}

bool startlog::on()
{
    static const bool set = getenv("CB_STARTUP_LOG") != nullptr;
    return set;
}

void startlog::mark(const char* stage)
{
    if (!on()) return;
    const Clock::time_point now = Clock::now();
    const double total = std::chrono::duration<double, std::milli>(now - t0).count();
    const double step  = std::chrono::duration<double, std::milli>(now - last).count();
    long rss, peak;
    memory(rss, peak);
    fprintf(stderr, "startup: %9.1f %8.1f rss %6.1f peak %6.1f  %s\n", total, step, rss / 1024.0, peak / 1024.0, stage);
    last = Clock::now();
}

void startlog::race_begin()
{
    if (!on()) return;
    mark("START RACE chosen");
    race_pending = true;
}

void startlog::race_frame()
{
    if (!race_pending) return;
    race_pending = false;
    mark("first race frame");
}
