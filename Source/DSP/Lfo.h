#pragma once

#include "../Parameters.h"
#include "Common.h"

#include <cmath>
#include <cstdint>

namespace tourbillon
{
//==============================================================================
/** The low-frequency oscillator that moves every circuit except the rotary speaker (whose rotors
    have their own inertia).

    The position is kept as a running count of cycles, not just a phase, so the two random shapes
    can be worked out from the cycle number. That makes them repeatable: with Stereo Phase at zero
    both sides get the same random values, a synced LFO gives the same pattern on every play-through
    of the song, and the Motion display can draw the very values the audio is using.

    Every shape passes through a 1.5 ms smoother, so the corners of Square, the resets of the Ramps
    and the steps of S&H don't click on a tremolo, while sweeps up to 12 Hz are untouched. */
struct Lfo
{
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        smoothCoef = msToCoef (1.5f, sr);
        reset();
    }

    void reset() noexcept
    {
        cycles = 0.0;
        smooth[0] = smooth[1] = 0.0f;
        primed = false;
    }

    /** Locks the position to the host's: `position` is in cycles (beats / beats per cycle). */
    void syncTo (double position) noexcept      { cycles = position; }
    double position() const noexcept            { return cycles; }

    /** Value of a shape at a running position in cycles, -1..1, before smoothing. */
    static float valueAt (int shape, double pos) noexcept
    {
        const double whole = std::floor (pos);
        const float p = (float) (pos - whole);
        switch (shape)
        {
            case triangle:   return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case square:     return std::tanh (5.0f * std::sin (2.0f * (float) kPi * p)) / std::tanh (5.0f);
            case rampUp:     return 2.0f * p - 1.0f;
            case rampDown:   return 1.0f - 2.0f * p;
            case sampleHold: return random ((int64_t) whole);
            case drift:
            {
                const float a = random ((int64_t) whole), b = random ((int64_t) whole + 1);
                return a + (b - a) * (0.5f - 0.5f * std::cos ((float) kPi * p));
            }
            default:         return std::sin (2.0f * (float) kPi * p);
        }
    }

    /** Advances by n samples at rateHz and writes both sides. The right side runs `spreadDeg` ahead. */
    void process (float* outL, float* outR, int n, int shape, double rateHz, float spreadDeg) noexcept
    {
        const double step = rateHz / sr, offset = (double) spreadDeg / 360.0;
        for (int i = 0; i < n; ++i)
        {
            cycles += step;
            const float l = valueAt (shape, cycles), r = valueAt (shape, cycles + offset);
            if (! primed) { smooth[0] = l; smooth[1] = r; primed = true; }
            smooth[0] += smoothCoef * (l - smooth[0]);
            smooth[1] += smoothCoef * (r - smooth[1]);
            outL[i] = smooth[0];
            outR[i] = smooth[1];
        }
        // keep the count small enough that a double still resolves a sample's worth of phase
        if (cycles > 1.0e6) cycles -= 1.0e6;
    }

private:
    /** -1..1 from a cycle number (SplitMix64), the same on every run. */
    static float random (int64_t index) noexcept
    {
        uint64_t z = (uint64_t) index + 0x9e3779b97f4a7c15ull;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        z ^= z >> 31;
        return (float) (z >> 40) * (2.0f / 16777216.0f) - 1.0f;
    }

    double sr = 48000.0, cycles = 0.0;
    float smoothCoef = 0.01f, smooth[2] {};
    bool primed = false;
};

//==============================================================================
/** Follows the level of the dry signal (or the sidechain key) for the Envelope control: 0 at
    -48 dBFS and below, 1 at -6 dBFS and above, with a quick attack and an unhurried release so a
    sweep opens on each note and settles between them. */
struct EnvelopeFollower
{
    void prepare (double sr)
    {
        att = msToCoef (3.0f, sr);
        rel = msToCoef (160.0f, sr);
        reset();
    }
    void reset() noexcept   { env = 0.0f; }

    void process (const float* l, const float* r, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (l[i]), std::abs (r[i]));
            env += (x > env ? att : rel) * (x - env);
            out[i] = std::clamp ((gainToDb (env) + 48.0f) / 42.0f, 0.0f, 1.0f);
        }
    }

    float env = 0.0f, att = 0.1f, rel = 0.001f;
};
} // namespace tourbillon
