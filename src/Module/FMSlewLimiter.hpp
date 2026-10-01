//
//  FMSlewLimiter.hpp — stereo slew limiter for block-averaged filter-modulation inputs.
//  Vendored from PolygraphModular. Smooths the per-block coefficient changes so a rapidly
//  changing modulation signal can't make the IIR filter jump (and ring/blow up).
//

#ifndef STRUCKKIT_FMSLEWLIMITER_HPP
#define STRUCKKIT_FMSLEWLIMITER_HPP

#include <algorithm>

// Tracks previous block-averaged values independently per channel. Slewing always runs to keep
// state current; pass enabled=false to bypass (still tracking) for a future on/off parameter.
struct FMSlewLimiter {
    float prevL = 0.0f;
    float prevR = 0.0f;

    void process(float& outL, float& outR,
                 float inL, float inR,
                 float risePerBlock, float fallPerBlock,
                 bool enabled = true)
    {
        prevL = slewValue(prevL, inL, risePerBlock, fallPerBlock);
        prevR = slewValue(prevR, inR, risePerBlock, fallPerBlock);
        outL = enabled ? prevL : inL;
        outR = enabled ? prevR : inR;
    }

private:
    static float slewValue(float prev, float input, float rise, float fall) {
        float delta = input - prev;
        if (delta > 0.0f) return std::min(prev + rise, input);
        if (delta < 0.0f) return std::max(prev - fall, input);
        return input;
    }
};

#endif // STRUCKKIT_FMSLEWLIMITER_HPP
