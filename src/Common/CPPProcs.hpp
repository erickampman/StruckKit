//
//  CPPProcs.hpp
//
//  Created by Eric Kampman on 4/11/25.
//

#ifndef CPPProcs_h
#define CPPProcs_h

#include <cmath>
#include <algorithm>

#include "pm_midi.h"


const double kTwoPi = 2.0 * M_PI;

template <typename T>
T clamp(T input, T low, T high) {
	return std::min(std::max(input, low), high);
}

static inline double pow2(double x) {
	return x * x;
}

static inline double pow3(double x) {
	return x * x * x;
}

static inline double noteToHz(int noteNumber)
{
	return 440. * exp2((noteNumber - 69)/12.);
}

static inline double panValue(double x)
{
	x = clamp(x, -1., 1.);
	return cos(M_PI_2 * (.5 * x + .5));
}

// Keeps x only when its magnitude lies in (1e-15, 1e15); anything else becomes 0. That flushes
// denormals and tiny values at the low end, infinities at the high end, and NaN, for which
// both comparisons are false. Zero maps to zero either way.
static inline float convertBadValuesToZero(float x) {
	const float magnitude = std::fabs(x);
	const bool inRange = magnitude > 1e-15 && magnitude < 1e15;
	return inRange ? x : 0.0f;
}

static inline double squared(double x) {
	return x * x;
}

static inline double noteToHertzWithTuneAndPitch(int noteNumber, float pitch, float cents) {
	
	double baseFreq = noteToHz(noteNumber);
	double mult = std::exp2f(pitch/12.0 + cents/1200.0);
	double ret = baseFreq * mult;

	return ret;
}

static inline double frequencyWithPitchAndCents(double baseFreq, float pitch, float cents) {
	double mult = std::exp2f(pitch/12.0 + cents/1200.0);
	return baseFreq * mult;
}

// cycles per sample is proportional to frequency, so is the same as above.
static inline double adjustCyclesPerSampleWithCents(double cps, double cents) {
	double mult = std::exp2f(cents/1200.0);
	return cps * mult;
}

static inline double adjustCyclesPerSampleWithCents(double cps, float cents) {
	double mult = std::exp2f(cents/1200.0);
	return cps * mult;
}

static float wrap01(float value) {
    return value - std::floor(value);
}

inline void zeroBuffer(float* buffer, size_t count) {
    std::fill_n(buffer, count, 0.0f);
}

#endif /* CPPProcs_h */
