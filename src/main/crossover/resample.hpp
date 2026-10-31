/***************************************************************************
    Board-rate to output-rate conversion for the emulated sound boards.

    A board makes its samples at its own rate in frames of 1/fps s; the program takes rate/fps
    output samples per frame. Linear interpolation at the nominal ratio, with the ratio pulled by
    at most 0.2 % towards keeping a small backlog of board samples: rate/fps is a whole number
    and a board's frame is not (22,050 Hz at 60 fps takes 367 samples for a 367.5-sample frame),
    so a fixed ratio would let the backlog grow or run dry.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>

struct FrameResampler
{
    std::vector<float> buf;         // board samples not yet converted, interleaved stereo
    double pos = 0;                 // read position in board samples

    void clear() { buf.clear(); pos = 0; }
    void push(float l, float r) { buf.push_back(l); buf.push_back(r); }
    size_t available() const { return buf.size() / 2; }

    // Adds samples output samples to mix (interleaved stereo), board samples per output sample
    // nominally ratio
    void pull(double ratio, int32_t* mix, int samples)
    {
        size_t n = buf.size() / 2;
        // the backlog this frame would leave at the nominal ratio, against the margin wanted
        double backlog = (double) n - pos - ratio * samples;
        double step = ratio;
        if (samples > 0)
        {
            // 5 % of the error per frame: the margin is built up over the first dozen frames at
            // the 0.2 % limit; a steady 0.5 sample per frame (22,050 Hz at 60 fps) is absorbed
            // with a backlog of 10 samples over the margin; the frame's jitter of 1 sample moves
            // the ratio by about 0.007 %
            double adjust = 0.05 * (backlog - MARGIN) / samples;
            adjust = std::max(-0.002 * ratio, std::min(0.002 * ratio, adjust));
            step = ratio + adjust;
        }
        for (int i = 0; i < samples; i++)
        {
            size_t k = (size_t) pos;
            double f = pos - (double) k;
            for (int c = 0; c < 2; c++)
            {
                double a = k < n ? buf[2 * k + c] : (n ? buf[2 * (n - 1) + c] : 0);
                double b = k + 1 < n ? buf[2 * (k + 1) + c] : a;
                mix[2 * i + c] += (int32_t)(a + (b - a) * f);
            }
            pos += step;
        }
        size_t used = std::min((size_t) pos, n);
        buf.erase(buf.begin(), buf.begin() + 2 * (long) used);
        pos -= (double) used;
    }

    static constexpr double MARGIN = 16;    // board samples kept back, against the frame's jitter
};
