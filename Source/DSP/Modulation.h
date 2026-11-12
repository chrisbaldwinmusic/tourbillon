#pragma once

#include "../Parameters.h"
#include "BucketBrigade.h"
#include "Common.h"

namespace tourbillon
{
//==============================================================================
/** A modulated delay line read with cubic (Hermite) interpolation, for the clean voices. */
struct ModDelay
{
    void prepare (int maxSamples)
    {
        int n = 16;
        while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }

    void reset() noexcept               { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept        { buf[(size_t) w] = x; w = (w + 1) & mask; }

    /** The signal `d` samples ago (d = 1 is the sample pushed last). */
    float read (float d) const noexcept
    {
        d = std::clamp (d, 2.0f, (float) (mask - 4));
        const int i = (int) d;
        const float f = d - (float) i;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    float at (int k) const noexcept     { return buf[(size_t) ((w - k) & mask)]; }
    std::vector<float> buf;
    int mask = 0, w = 0;
};

/** First-order all-pass, the building block of the phaser and the vibe. */
struct AllPass
{
    float z = 0.0f;
    static float coef (double sr, double hz) noexcept
    {
        const double t = std::tan (kPi * std::clamp (hz, 5.0, 0.45 * sr) / sr);
        return (float) ((t - 1.0) / (t + 1.0));
    }
    inline float process (float x, float a) noexcept
    {
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

//==============================================================================
/** The seven circuits. Each one turns the dry signal and the LFO into the wet signal; the
    processor then blends wet and dry with Mix, so "dry plus wet at equal level" (Mix 50 %) is the
    classic pedal sound for the chorus, flanger, phaser and vibe, and the wet signal alone (Mix
    100 %) is the classic sound for the tremolo, vibrato and rotary speaker.

    The sweep position used by most of them is the same: Manual sets where the sweep sits, Depth
    how far the LFO moves it either side, and Envelope pushes it up (or down) with the playing. */
struct Modulator
{
    struct Params
    {
        int type = chorus, voice = 0;
        float depth = 0.5f, manual = 0.5f, feedback = 0.0f, character = 0.4f;   // 0..1, feedback -0.95..0.95
        float envAmt = 0.0f;                                                     // -1..1
        float spreadDeg = 90.0f;
        double rateHz = 0.6;
    };

    /** What the circuit is doing now, for the Motion display. */
    struct State
    {
        float position[2] {};           // sweep position 0..1 (tremolo: gain; vibrato: pitch in cents)
        float delayMs[2] {};            // chorus, flanger, vibrato
        float breakHz[2] {};            // phaser: the all-pass corner; vibe: the photocell resistance in ohms
        float hornHz = 0.0f, drumHz = 0.0f, hornAngle = 0.0f, drumAngle = 0.0f;
    } state;

    // the vibe's four phase-shift capacitors, in the order the signal meets them
    static constexpr double vibeCaps[4] = { 15.0e-9, 220.0e-9, 470.0e-12, 4.7e-9 };
    static constexpr double vibeRmax = 1.0e6;
    static double vibeRmin (int voice) noexcept     { return voice == 1 ? 12.0e3 : (voice == 2 ? 50.0e3 : 25.0e3); }
    static int phaserStages (int voice) noexcept    { return voice == 1 ? 8 : (voice == 2 ? 12 : 4); }
    static double phaserHz (float pos) noexcept     { return 70.0 * std::pow (4800.0 / 70.0, std::pow ((double) pos, 1.35)); }
    static float flangerMs (float pos) noexcept     { return 0.25f * std::pow (48.0f, pos); }
    static constexpr float tzfReferenceMs = 4.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const BucketBrigade::Chip chorusChip { 512, 10000.0, 200000.0, false };
        const BucketBrigade::Chip flangerChip { 512, 10000.0, 400000.0, false };
        for (auto& ch : chorusBbd) for (auto& b : ch) b.prepare (sr, 0.06, chorusChip);
        for (auto& b : flangerBbd) b.prepare (sr, 0.03, flangerChip);
        for (auto& d : line) d.prepare ((int) (0.06 * sr) + 8);
        hornLine.prepare ((int) (0.01 * sr) + 8);
        drumLine.prepare ((int) (0.01 * sr) + 8);
        for (auto& f : xoverLo) f.setLowPass (sr, 800.0, 0.7071);
        for (auto& f : xoverHi) f.setHighPass (sr, 800.0, 0.7071);
        hornCab.setPeak (sr, 2500.0, 0.7, 3.0);
        drumCab.setLowPass (sr, 3500.0, 0.6);
        lampUp = msToCoef (12.0f, sr);
        tremLp.reset();
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : chorusBbd) for (auto& b : ch) b.reset();
        for (auto& b : flangerBbd) b.reset();
        for (auto& d : line) d.reset();
        hornLine.reset(); drumLine.reset();
        for (auto& ch : stages) for (auto& s : ch) s.z = 0.0f;
        for (auto& f : xoverLo) f.reset();
        for (auto& f : xoverHi) f.reset();
        hornCab.reset(); drumCab.reset(); tremLp.reset();
        fbState[0] = fbState[1] = 0.0f;
        lamp[0] = lamp[1] = 0.0f;
        logR[0] = logR[1] = (float) std::log (vibeRmax);
        ensPhase = vibPhase = 0.0;
        wow = wowTarget = 0.0f; wowCount = 0; wowSeed = 0x1234567u;
        primedRotor = false;
    }

    void set (const Params& np) noexcept     { p = np; }

    void process (const float* inL, const float* inR, float* outL, float* outR,
                  const float* lfoL, const float* lfoR, const float* env, int n) noexcept
    {
        const float* in[2] { inL, inR };
        float* out[2] { outL, outR };
        const float* lfo[2] { lfoL, lfoR };

        switch (p.type)
        {
            case flanger:   runFlanger (in, out, lfo, env, n); break;
            case phaser:    runPhaser (in, out, lfo, env, n); break;
            case vibe:      runVibe (in, out, lfo, env, n); break;
            case tremolo:   runTremolo (in, out, lfo, env, n); break;
            case vibrato:   runVibrato (in, out, lfo, env, n); break;
            case rotary:    runRotary (in, out, env, n); break;
            default:        runChorus (in, out, lfo, env, n); break;
        }
    }

private:
    float sweep (float lfoValue, float envValue) const noexcept
    {
        return std::clamp (p.manual + 0.5f * p.depth * lfoValue + p.envAmt * envValue, 0.0f, 1.0f);
    }

    float ms (float samples) const noexcept     { return samples * 1000.0f / (float) sr; }
    float samples (float ms) const noexcept     { return ms * 0.001f * (float) sr; }

    //==========================================================================
    /** Chorus. Classic is one bucket-brigade line per side, swept by the LFO and mixed with the dry
        signal, as in the compact chorus pedals. Dimension feeds two lines from the mono sum and sweeps
        them in opposite directions, one to each side: the subtle widener of the rack units. Ensemble
        runs three lines per side on sines a third of a cycle apart, plus a faster shimmer, as the
        string machines did; it uses its own sines, so the Shape control does not apply. */
    void runChorus (const float* const* in, float* const* out, const float* const* lfo, const float* env, int n) noexcept
    {
        for (auto& ch : chorusBbd) for (auto& b : ch) b.setCharacter (p.character);
        const float fb = 0.6f * p.feedback;

        if (p.voice == 1)       // Dimension
        {
            const float centre = 5.0f + 7.0f * p.manual, swing = 1.2f * p.depth;
            for (int i = 0; i < n; ++i)
            {
                const float mono = 0.5f * (in[0][i] + in[1][i]);
                const float e = p.envAmt * env[i] * 3.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& b = chorusBbd[ch][0];
                    const float d = std::clamp (centre + e + (ch == 0 ? swing : -swing) * lfo[0][i], 2.6f, 50.0f);
                    const float ds = samples (d);
                    const float y = b.read (ds - b.latencySamples (ds));
                    b.write (mono + fb * fbVoice[ch]);
                    fbVoice[ch] = y;
                    out[ch][i] = y;
                    state.delayMs[ch] = d;
                    state.position[ch] = (d - 2.6f) / 18.0f;
                }
            }
            return;
        }

        if (p.voice == 2)       // Ensemble
        {
            const float centre = 6.0f + 10.0f * p.manual, slow = 3.0f * p.depth, fast = 0.35f * p.depth;
            const double slowStep = p.rateHz / sr, fastStep = 6.1 / sr;
            const float offset = p.spreadDeg / 360.0f;
            for (int i = 0; i < n; ++i)
            {
                ensPhase += slowStep; if (ensPhase >= 1.0) ensPhase -= 1.0;
                vibPhase += fastStep; if (vibPhase >= 1.0) vibPhase -= 1.0;
                const float e = p.envAmt * env[i] * 4.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    float sum = 0.0f;
                    for (int v = 0; v < 3; ++v)
                    {
                        const float ph = (float) ensPhase + (float) v / 3.0f + (float) ch * offset;
                        const float fph = (float) vibPhase + (float) v / 3.0f + (float) ch * 0.25f;
                        const float d = std::clamp (centre + e + slow * std::sin (2.0f * (float) kPi * ph)
                                                               + fast * std::sin (2.0f * (float) kPi * fph), 2.6f, 50.0f);
                        auto& b = chorusBbd[ch][v];
                        const float ds = samples (d);
                        sum += b.read (ds - b.latencySamples (ds));
                        b.write (in[ch][i]);
                        if (v == 0) { state.delayMs[ch] = d; state.position[ch] = (d - 2.6f) / 22.0f; }
                    }
                    out[ch][i] = 0.6f * sum;
                }
            }
            return;
        }

        // Classic
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& b = chorusBbd[ch][0];
            const float centre = 4.0f + 16.0f * p.manual;
            const float swing = p.depth * std::min (centre - 2.7f, 5.0f);
            for (int i = 0; i < n; ++i)
            {
                const float d = std::clamp (centre + swing * lfo[ch][i] + p.envAmt * env[i] * 0.5f * centre, 2.6f, 50.0f);
                const float ds = samples (d);
                const float y = b.read (ds - b.latencySamples (ds));
                b.write (in[ch][i] + fb * fbVoice[ch]);
                fbVoice[ch] = y;
                out[ch][i] = y;
                state.delayMs[ch] = d;
                state.position[ch] = (d - 2.6f) / 22.0f;
            }
        }
    }

    //==========================================================================
    /** Flanger. A short delay swept on an exponential scale from 0.25 to 12 ms, so the comb's
        notches move evenly in pitch. BBD is the pedal: a bucket-brigade chip with its filters and
        grit. Clean is the same sweep without the chip. Through-Zero is the tape trick: two copies,
        one held 4 ms late and one swept either side of it, subtracted, so as the sweep passes the
        fixed copy the two cancel completely; use it at Mix 100 %. */
    void runFlanger (const float* const* in, float* const* out, const float* const* lfo, const float* env, int n) noexcept
    {
        for (auto& b : flangerBbd) b.setCharacter (p.character);
        const float fb = p.feedback;

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < n; ++i)
            {
                const float pos = sweep (lfo[ch][i], env[i]);
                const float x = in[ch][i];
                float y = 0.0f, d = 0.0f;

                if (p.voice == 1)       // through-zero
                {
                    const float ref = tzfReferenceMs;
                    d = std::max (0.05f, 2.0f * ref * pos);
                    auto& l = line[ch];
                    y = 0.5f * (l.read (samples (ref)) - l.read (samples (d)));
                    l.push (x + fb * std::tanh (fbState[ch]));
                }
                else if (p.voice == 2)  // clean
                {
                    d = flangerMs (pos);
                    auto& l = line[ch];
                    y = l.read (samples (d));
                    l.push (x + fb * std::tanh (fbState[ch]));
                }
                else                    // bucket brigade
                {
                    d = flangerMs (pos);
                    auto& b = flangerBbd[ch];
                    const float ds = samples (d);
                    y = b.read (std::max (1.0f, ds - b.latencySamples (ds)));
                    b.write (x + fb * fbState[ch]);
                }
                fbState[ch] = y;
                out[ch][i] = y;
                state.position[ch] = pos;
                state.delayMs[ch] = d;
            }
        }
    }

    //==========================================================================
    /** Phaser: 4, 8 or 12 first-order all-pass stages whose corner frequency is swept together, as
        the JFETs or OTAs of a phaser pedal do. Mixed with the dry signal, every 4 stages make two
        notches. The JFETs' square-law response bunches the sweep towards the bottom, which is where
        a phaser's sweep sounds slowest. Feedback (Regen) takes the last stage back to the first;
        Character drives the stages, which compress a little as a real JFET does. */
    void runPhaser (const float* const* in, float* const* out, const float* const* lfo, const float* env, int n) noexcept
    {
        const int count = phaserStages (p.voice);
        const float k = 0.15f + 1.6f * p.character, fb = p.feedback;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < n; ++i)
            {
                const float pos = sweep (lfo[ch][i], env[i]);
                const double hz = phaserHz (pos);
                const float a = AllPass::coef (sr, hz);
                float s = in[ch][i] + fb * fbState[ch];
                s = std::tanh (k * s) / k;
                for (int st = 0; st < count; ++st) s = stages[ch][(size_t) st].process (s, a);
                fbState[ch] = s;
                out[ch][i] = s;
                state.position[ch] = pos;
                state.breakHz[ch] = (float) hz;
            }
        }
    }

    //==========================================================================
    /** Vibe: four phase-shift stages with very different capacitors (15 nF, 220 nF, 470 pF, 4.7 nF),
        each with a photocell lit by one incandescent lamp. The lamp heats faster than it cools and
        the photocells answer light quickly but darkness slowly, so the sweep is lopsided and the
        stages spread across the whole spectrum instead of moving together: the throb of the
        original. Manual sets the lamp's resting brightness; Character is the lamp's lag. Voices
        set the photocells: Bright reaches lower resistance, Deep stays darker. */
    void runVibe (const float* const* in, float* const* out, const float* const* lfo, const float* env, int n) noexcept
    {
        const float lag = 0.4f + 1.6f * p.character;
        const float lampDown = msToCoef (35.0f * lag, sr);
        const float cellFast = msToCoef (8.0f, sr), cellSlow = msToCoef (60.0f * lag, sr);
        const float lnMin = (float) std::log (vibeRmin (p.voice)), lnMax = (float) std::log (vibeRmax);

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < n; ++i)
            {
                const float drive = sweep (lfo[ch][i], env[i]);
                lamp[ch] += (drive > lamp[ch] ? lampUp : lampDown) * (drive - lamp[ch]);
                const float target = lnMax - (lnMax - lnMin) * std::pow (lamp[ch], 0.8f);
                logR[ch] += (target < logR[ch] ? cellFast : cellSlow) * (target - logR[ch]);
                const double r = std::exp ((double) logR[ch]);

                float s = in[ch][i];
                for (int st = 0; st < 4; ++st)
                    s = stages[ch][(size_t) st].process (s, AllPass::coef (sr, 1.0 / (2.0 * kPi * r * vibeCaps[st])));
                out[ch][i] = s;
                state.position[ch] = lamp[ch];
                state.breakHz[ch] = (float) r;
            }
        }
    }

    //==========================================================================
    /** Tremolo. Bias varies the gain of a valve stage, which also changes how hard it is driven
        (Character), so the loud half of each cycle is a little thicker. Opto lights a lamp onto a
        photocell: quick to brighten, slow to fade, which softens the off-beat and gives the throb
        of the old combos; Character sets the lag. Harmonic splits the signal at a crossover
        (Character sets it, 300 Hz to 1.5 kHz) and pulses the two bands in opposite phase, which
        sounds like a slow phaser as much as a tremolo. Depth, plus Envelope, sets how far the
        level dips. */
    void runTremolo (const float* const* in, float* const* out, const float* const* lfo, const float* env, int n) noexcept
    {
        const float kDrive = 0.2f + 2.5f * p.character;
        const float optoDown = msToCoef (6.0f + 60.0f * p.character, sr), optoUp = msToCoef (3.0f, sr);
        tremLp.setCutoff (sr, 300.0 + 1200.0 * p.character);

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < n; ++i)
            {
                const float d = std::clamp (p.depth + p.envAmt * env[i], 0.0f, 1.0f);
                const float u = 0.5f + 0.5f * lfo[ch][i];      // 1 = loudest point of the cycle
                const float x = in[ch][i];
                float y, g;

                if (p.voice == 1)       // opto
                {
                    lamp[ch] += (u > lamp[ch] ? optoUp : optoDown) * (u - lamp[ch]);
                    g = 1.0f - d * (1.0f - std::pow (lamp[ch], 0.7f));
                    y = x * g;
                }
                else if (p.voice == 2)  // harmonic
                {
                    const float lo = tremLp.lp (ch, x), hi = x - lo;
                    g = 1.0f - d * (1.0f - u);
                    y = lo * g + hi * (1.0f - d * u);
                }
                else                    // bias
                {
                    g = 1.0f - d * (1.0f - u);
                    const float v = x * g * kDrive;
                    y = std::tanh (v) / kDrive;
                }
                out[ch][i] = y;
                state.position[ch] = g;
            }
        }
    }

    //==========================================================================
    /** Vibrato: a delay swept with nothing of the dry signal, so only the pitch moves. Depth is
        the pitch swing in cents (up to a semitone) and stays the same at any Rate; Manual sets the
        delay it sweeps around. BBD and Clean are the chip and a clean line; Tape adds the drifting
        wow and the flutter of a worn transport, scaled by Character. */
    void runVibrato (const float* const* in, float* const* out, const float* const* lfo, const float* env, int n) noexcept
    {
        for (auto& ch : chorusBbd) ch[0].setCharacter (p.character);
        const float cents = 100.0f * p.depth;
        const float swingMs = 1000.0f * (std::pow (2.0f, cents / 1200.0f) - 1.0f) / (2.0f * (float) kPi * (float) std::max (0.05, p.rateHz));
        const float swing = std::min (swingMs, 12.0f);
        const float floorMs = p.voice == 0 ? 2.7f : 0.3f;
        const float centre = std::max (1.0f + 9.0f * p.manual, floorMs + swing + 0.2f);

        for (int i = 0; i < n; ++i)
        {
            float tape = 0.0f;
            if (p.voice == 2)
            {
                if (--wowCount <= 0)
                {
                    wowCount = (int) (0.35 * sr);
                    wowSeed ^= wowSeed << 13; wowSeed ^= wowSeed >> 17; wowSeed ^= wowSeed << 5;
                    wowTarget = (float) wowSeed * (2.0f / 4294967296.0f) - 1.0f;
                }
                wow += 1.5e-4f * (wowTarget - wow);
                vibPhase += 9.3 / sr; if (vibPhase >= 1.0) vibPhase -= 1.0;
                tape = p.character * (0.4f * wow + 0.04f * std::sin (2.0f * (float) kPi * (float) vibPhase));
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                const float d = std::clamp (centre + swing * lfo[ch][i] + tape + p.envAmt * env[i] * 2.0f, floorMs, 50.0f);
                const float ds = samples (d);
                float y;
                if (p.voice == 0)
                {
                    auto& b = chorusBbd[ch][0];
                    y = b.read (ds - b.latencySamples (ds));
                    b.write (in[ch][i]);
                }
                else
                {
                    y = line[ch].read (ds);
                    line[ch].push (in[ch][i]);
                }
                out[ch][i] = y;
                state.delayMs[ch] = d;
                state.position[ch] = lfo[ch][i] * cents;
            }
        }
    }

    //==========================================================================
    /** Rotary speaker. The input is summed to mono, as it is at a real cabinet, driven through
        the preamp (Character) and split at 800 Hz. The treble goes to a spinning horn and the bass
        to a spinning drum, each of which comes round past two microphones (Stereo Phase is the
        angle between them). As a rotor turns, its distance to each microphone changes, which bends
        the pitch (the Doppler shift), and its mouth points towards and away, which changes the
        level; Depth sets how close the microphones are, so how strong both are. The horn also
        reaches the microphone off the back of the cabinet. Manual balances drum against horn.

        The rotors have weight: the horn takes about a second to change speed and the drum several,
        so Slow to Fast is the famous swell rather than a switch. Brake stops both. */
    void runRotary (const float* const* in, float* const* out, const float* env, int n) noexcept
    {
        juce::ignoreUnused (env);
        const float hornTarget = p.voice == 1 ? 6.8f : (p.voice == 2 ? 0.0f : 0.8f);
        const float drumTarget = p.voice == 1 ? 5.9f : (p.voice == 2 ? 0.0f : 0.66f);
        if (! primedRotor) { state.hornHz = hornTarget; state.drumHz = drumTarget; primedRotor = true; }
        // time constants of the speed change: about three of them to arrive
        const float hornUp = (float) (1.0 / (0.35 * sr)), hornDown = (float) (1.0 / (0.7 * sr));
        const float drumUp = (float) (1.0 / (1.6 * sr)), drumDown = (float) (1.0 / (2.2 * sr));

        const float kDrive = 0.3f + 4.0f * p.character;
        const float closeness = 0.25f + 0.75f * p.depth;
        const float hornDoppler = samples (0.44f) * closeness, drumDoppler = samples (0.12f) * closeness;
        const float hornAm = 0.65f * closeness, drumAm = 0.35f * closeness;
        const float hornBase = samples (1.2f), drumBase = samples (1.0f), backPath = samples (0.7f);
        const float hornGain = std::min (1.0f, 2.0f * p.manual), drumGain = std::min (1.0f, 2.0f * (1.0f - p.manual));
        const float micAngle = 0.5f * p.spreadDeg * (float) kPi / 180.0f;
        const float twoPi = 2.0f * (float) kPi;

        for (int i = 0; i < n; ++i)
        {
            state.hornHz += (hornTarget > state.hornHz ? hornUp : hornDown) * (hornTarget - state.hornHz);
            state.drumHz += (drumTarget > state.drumHz ? drumUp : drumDown) * (drumTarget - state.drumHz);
            state.hornAngle += twoPi * state.hornHz / (float) sr;
            state.drumAngle += twoPi * state.drumHz / (float) sr;
            if (state.hornAngle > twoPi) state.hornAngle -= twoPi;
            if (state.drumAngle > twoPi) state.drumAngle -= twoPi;

            float x = 0.5f * (in[0][i] + in[1][i]);
            x = std::tanh (kDrive * x) / kDrive;
            hornLine.push (hornCab.process (0, xoverHi[1].process (0, xoverHi[0].process (0, x))));
            drumLine.push (drumCab.process (0, xoverLo[1].process (0, xoverLo[0].process (0, x))));

            for (int ch = 0; ch < 2; ++ch)
            {
                const float mic = ch == 0 ? -micAngle : micAngle;
                const float h = state.hornAngle - mic, d = state.drumAngle - mic;
                const float ch_ = std::cos (h), cd = std::cos (d);

                const float front = (1.0f - hornAm) + hornAm * (0.5f + 0.5f * ch_);
                const float back  = 0.3f * ((1.0f - hornAm) + hornAm * (0.5f - 0.5f * ch_));
                const float horn = front * hornLine.read (hornBase - hornDoppler * ch_)
                                 + back  * hornLine.read (hornBase + backPath + hornDoppler * ch_);
                const float drum = ((1.0f - drumAm) + drumAm * (0.5f + 0.5f * cd)) * drumLine.read (drumBase - drumDoppler * cd);
                out[ch][i] = hornGain * horn + drumGain * drum;
                state.position[ch] = 0.5f + 0.5f * ch_;
            }
        }
    }

    Params p;
    double sr = 48000.0;

    BucketBrigade chorusBbd[2][3], flangerBbd[2];
    ModDelay line[2], hornLine, drumLine;
    std::array<AllPass, 12> stages[2];
    Biquad xoverLo[2], xoverHi[2], hornCab, drumCab;
    OnePole tremLp;

    float fbState[2] {}, fbVoice[2] {};
    float lamp[2] {}, logR[2] {};
    float lampUp = 0.01f;
    double ensPhase = 0.0, vibPhase = 0.0;
    float wow = 0.0f, wowTarget = 0.0f;
    int wowCount = 0;
    uint32_t wowSeed = 0x1234567u;
    bool primedRotor = false;
};
} // namespace tourbillon
