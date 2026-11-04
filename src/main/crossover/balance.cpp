/***************************************************************************
    Balancing a vehicle against a course from another game: see balance.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include <cmath>

#include "frontend/config.hpp"
#include "crossover/balance.hpp"

namespace balance
{
    double strength()
    {
        const int b = config.crossover.balance;
        return b <= 0 ? 0.0 : b == 1 ? 0.5 : 1.0;
    }

    double blend(double full)
    {
        const double s = strength();
        if (s == 0.0 || full == 1.0) return 1.0;
        return std::pow(full, s);
    }

    bool native(Vehicle v, Host h)
    {
        return (v == FERRARI && h == OUTRUN) || (v == SH_BIKE && h == SUPER_HANG_ON) ||
               (v == HO_BIKE && h == HANG_ON) || (v == MAD_POWER && h == OUTRUNNERS);
    }

    // Until each aspect's table is in (balance_pace.cpp, balance_push.cpp, balance_contact.cpp)
    double contact(Vehicle, Host) { return 1.0; }
}
