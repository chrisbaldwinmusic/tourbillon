// Offline checks for Tourbillon: runs audio through the processor and checks the results.
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <CoreFoundation/CoreFoundation.h>
#include <iostream>

using namespace tourbillon;

namespace
{
int failures = 0;
const double sr = 48000.0;
constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

void setParam (TourbillonProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float value (TourbillonProcessor& p, const char* id)    { return p.apvts.getRawParameterValue (id)->load(); }

void prepare (TourbillonProcessor& p, int channels = 2, double rate = sr, int block = 512)
{
    p.setPlayConfigDetails (channels, channels, rate, block);
    p.prepareToPlay (rate, block);
}

void run (TourbillonProcessor& p, juce::AudioBuffer<float>& audio, int blockSize)
{
    juce::MidiBuffer midi;
    for (int pos = 0; pos < audio.getNumSamples(); pos += blockSize)
    {
        const int n = juce::jmin (blockSize, audio.getNumSamples() - pos);
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), audio.getNumChannels(), pos, n);
        p.processBlock (view, midi);
    }
}

juce::AudioBuffer<float> tone (int channels, int n, double freq, float gainDb, double rate = sr)
{
    juce::AudioBuffer<float> b (channels, n);
    const float g = juce::Decibels::decibelsToGain (gainDb);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample (ch, i, g * (float) std::sin (twoPi * freq * i / rate));
    return b;
}

juce::AudioBuffer<float> constant (int channels, int n, float v)
{
    juce::AudioBuffer<float> b (channels, n);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill (b.getWritePointer (ch), v, n);
    return b;
}

juce::AudioBuffer<float> noise (int channels, int n, float gainDb, int seed = 1)
{
    juce::AudioBuffer<float> b (channels, n);
    juce::Random rng (seed);
    const float g = juce::Decibels::decibelsToGain (gainDb);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample (ch, i, g * (rng.nextFloat() * 2.0f - 1.0f));
    return b;
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}

float rmsDb (const juce::AudioBuffer<float>& b, int from, int to, int ch = 0)
{
    return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, from, juce::jmin (to, b.getNumSamples()) - from), -200.0f);
}

/** Times (in samples, interpolated) at which the signal crosses `level` going upwards. */
std::vector<double> upCrossings (const float* x, int from, int to, float level = 0.0f)
{
    std::vector<double> t;
    for (int i = from + 1; i < to; ++i)
        if (x[i - 1] < level && x[i] >= level)
            t.push_back ((double) (i - 1) + (double) ((level - x[i - 1]) / (x[i] - x[i - 1])));
    return t;
}

/** The circuit with nothing in the way: wet only, no filters, no width change. */
void plain (TourbillonProcessor& p, int type, int voice)
{
    for (auto* param : p.getParameters())
        param->setValueNotifyingHost (param->getDefaultValue());
    setParam (p, "type", (float) type);
    setParam (p, "voice", (float) voice);
    setParam (p, "mix", 100.0f);
}

struct FixedTempo final : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        return info;
    }
};
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "shots");
    outDir.createDirectory();

    // TOURBILLON_ONLY=<text> runs just the sections whose heading contains the text
    auto want = [] (const char* section)
    {
        const char* only = std::getenv ("TOURBILLON_ONLY");
        const bool yes = only == nullptr || juce::String (section).containsIgnoreCase (only);
        if (yes) std::cout << section << std::endl;
        return yes;
    };

    if (want ("Bypass and dry path"))
    {
        TourbillonProcessor p;
        prepare (p);
        setParam (p, "type", flanger); setParam (p, "feedback", 80.0f);
        setParam (p, "bypass", 1.0f);
        const auto in = noise (2, 96000, -12.0f);
        auto out = in;
        run (p, out, 512);
        float err = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                err = std::max (err, std::abs (out.getSample (ch, i) - in.getSample (ch, i)));
        check (p.getLatencySamples() == 0 && err < 1.0e-6f, "bypass is the input exactly, with no latency");

        bool ok = true;
        for (int type = 0; type < numTypes; ++type)
        {
            TourbillonProcessor q;
            prepare (q);
            setParam (q, "type", (float) type); setParam (q, "mix", 0.0f);
            auto b = in;
            run (q, b, 512);
            float e = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 4800; i < b.getNumSamples(); ++i)
                    e = std::max (e, std::abs (b.getSample (ch, i) - in.getSample (ch, i)));
            if (e > 1.0e-5f) { ok = false; check (false, juce::String (typeNames[type]) + " at Mix 0 % changes the signal by " + juce::String (e)); }
        }
        check (ok, "at Mix 0 % every type passes the dry signal untouched");
    }

    if (want ("LFO rate and sync"))
    {
        // a tremolo on a constant input draws the LFO directly
        auto period = [] (TourbillonProcessor& p, int samples)
        {
            auto b = constant (2, samples, 0.5f);
            run (p, b, 256);
            const float* x = b.getReadPointer (0);
            float mean = 0.0f;
            for (int i = samples / 4; i < samples; ++i) mean += x[i];
            mean /= (float) (samples - samples / 4);
            const auto t = upCrossings (x, samples / 4, samples, mean);
            return t.size() < 3 ? 0.0 : (t.back() - t.front()) / (double) (t.size() - 1);
        };

        for (float hz : { 0.5f, 4.0f, 11.0f })
        {
            TourbillonProcessor p;
            prepare (p);
            plain (p, tremolo, 0);
            setParam (p, "character", 0.0f); setParam (p, "depth", 100.0f); setParam (p, "rate", hz);
            const double per = period (p, (int) sr * 8);
            check (std::abs (per / (sr / hz) - 1.0) < 0.01, "LFO runs at " + juce::String (hz) + " Hz (period " + juce::String (per, 1) + " samples)");
        }

        {
            TourbillonProcessor p;
            FixedTempo tempo;
            p.setPlayHead (&tempo);
            prepare (p);
            plain (p, tremolo, 0);
            setParam (p, "character", 0.0f); setParam (p, "depth", 100.0f);
            setParam (p, "sync", 1.0f); setParam (p, "div", 7.0f);     // a quarter note
            const double per = period (p, (int) sr * 4);
            check (std::abs (per - 24000.0) < 120.0, "synced to a quarter note at a host tempo of 120 BPM: 2 Hz");
            p.setPlayHead (nullptr);
        }

        {
            TourbillonProcessor p;
            prepare (p);
            plain (p, tremolo, 0);
            setParam (p, "character", 0.0f); setParam (p, "depth", 100.0f); setParam (p, "rate", 2.0f); setParam (p, "spread", 180.0f);
            auto b = constant (2, (int) sr * 2, 0.5f);
            run (p, b, 256);
            double sl = 0, sr2 = 0, slr = 0, ml = 0, mr = 0;
            const int from = (int) sr / 2, n = b.getNumSamples() - from;
            for (int i = from; i < b.getNumSamples(); ++i) { ml += b.getSample (0, i); mr += b.getSample (1, i); }
            ml /= n; mr /= n;
            for (int i = from; i < b.getNumSamples(); ++i)
            {
                const double a = b.getSample (0, i) - ml, c = b.getSample (1, i) - mr;
                sl += a * a; sr2 += c * c; slr += a * c;
            }
            check (slr / std::sqrt (sl * sr2) < -0.95, "Stereo Phase 180 turns the tremolo into an auto-panner");
        }
    }

    if (want ("Circuits"))
    {
        // Through-zero: with the sweep parked on the reference copy, the two cancel completely
        {
            TourbillonProcessor p;
            prepare (p);
            plain (p, flanger, 1);
            setParam (p, "depth", 0.0f); setParam (p, "manual", 50.0f);
            auto b = noise (2, (int) sr, -12.0f, 3);
            run (p, b, 512);
            check (rmsDb (b, 4800, b.getNumSamples()) < -80.0f, "Through-Zero cancels completely where the sweep meets the reference");
        }

        // Phaser: each first-order stage is -90 degrees at its corner, so 4 stages mixed with the dry
        // signal peak there (-360) and notch where the total is -180 and -540 degrees
        {
            const float pos = 0.3f;
            const double corner = Modulator::phaserHz (pos);
            auto level = [&] (double f)
            {
                TourbillonProcessor p;
                prepare (p);
                plain (p, phaser, 0);
                setParam (p, "depth", 0.0f); setParam (p, "manual", 100.0f * pos); setParam (p, "character", 0.0f); setParam (p, "mix", 50.0f);
                auto b = tone (2, (int) sr, f, -30.0f);
                run (p, b, 512);
                return rmsDb (b, 24000, 48000) + 33.01f;     // relative to the input's level
            };
            const double pi = juce::MathConstants<double>::pi;
            // the stages are integrated by the trapezoidal rule, which maps analog frequency f to the digital
            // frequency below; the notches sit where it puts them
            auto digital = [&] (double f) { return sr / pi * std::atan (pi * f / sr); };
            const float lowNotch = level (digital (corner * std::tan (pi / 8.0))), highNotch = level (digital (corner * std::tan (3.0 * pi / 8.0)));
            const float peakAt = level (digital (corner)), below = level (corner / 30.0), above = level (20000.0);
            check (lowNotch < -30.0f && highNotch < -30.0f && std::abs (peakAt) < 0.5f && std::abs (below) < 0.5f && std::abs (above) < 0.5f,
                   "4-stage phaser: two notches either side of the corner (" + juce::String (lowNotch, 1) + " / " + juce::String (highNotch, 1)
                   + " dB), full level at the corner and far from it");
        }

        // Vibrato: the pitch swing follows Depth in cents, and is the same at any rate
        for (float hz : { 1.5f, 6.0f })
        {
            TourbillonProcessor p;
            prepare (p);
            plain (p, vibrato, 1);
            setParam (p, "depth", 50.0f); setParam (p, "rate", hz);
            auto b = tone (2, (int) sr * 3, 1000.0, -12.0f);
            run (p, b, 512);
            const auto t = upCrossings (b.getReadPointer (0), (int) sr, b.getNumSamples());
            double lo = 1.0e9, hi = 0.0;
            for (size_t k = 1; k < t.size(); ++k) { lo = std::min (lo, t[k] - t[k - 1]); hi = std::max (hi, t[k] - t[k - 1]); }
            const double cents = 1200.0 * std::log2 (hi / lo) * 0.5;
            check (std::abs (cents - 50.0) < 8.0 && std::abs (rmsDb (b, 48000, b.getNumSamples()) + 15.01f) < 0.3f,
                   "vibrato at " + juce::String (hz) + " Hz swings " + juce::String (cents, 1) + " cents either way at Depth 50 %, level unchanged");
        }

        // Chorus voices make a stereo field from a mono source
        for (int voice = 0; voice < 3; ++voice)
        {
            TourbillonProcessor p;
            prepare (p);
            plain (p, chorus, voice);
            setParam (p, "depth", 80.0f); setParam (p, "rate", 1.0f);
            auto b = noise (1, (int) sr * 2, -12.0f, 9);
            juce::AudioBuffer<float> st (2, b.getNumSamples());
            st.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
            st.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
            run (p, st, 512);
            double a2 = 0, b2 = 0, ab = 0;
            for (int i = 24000; i < st.getNumSamples(); ++i)
            {
                const double a = st.getSample (0, i), c = st.getSample (1, i);
                a2 += a * a; b2 += c * c; ab += a * c;
            }
            const double corr = ab / std::sqrt (a2 * b2);
            check (allFinite (st) && corr < 0.9, juce::String ("Chorus ") + voiceNames[chorus][voice] + " widens a mono source (correlation " + juce::String (corr, 2) + ")");
        }

        // Rotary: the rotors have weight
        {
            TourbillonProcessor p;
            prepare (p);
            plain (p, rotary, 0);
            auto b = noise (2, (int) sr, -20.0f, 4);
            run (p, b, 512);
            const float slowHorn = p.meters.hornHz.load();
            setParam (p, "voice", 1.0f);
            auto c = noise (2, (int) sr, -20.0f, 5);
            run (p, c, 512);
            const float hornAt1 = p.meters.hornHz.load(), drumAt1 = p.meters.drumHz.load();
            auto d = noise (2, (int) sr * 5, -20.0f, 6);
            run (p, d, 512);
            const float hornAt6 = p.meters.hornHz.load(), drumAt6 = p.meters.drumHz.load();
            check (std::abs (slowHorn - 0.8f) < 0.01f && hornAt1 > 6.0f && drumAt1 < 4.5f && drumAt6 > 5.5f && std::abs (hornAt6 - 6.8f) < 0.1f,
                   "Slow to Fast: the horn arrives within a second, the drum takes several ("
                   + juce::String (hornAt1, 2) + " / " + juce::String (drumAt1, 2) + " Hz after 1 s)");
            setParam (p, "voice", 2.0f);
            auto e = noise (2, (int) sr * 8, -20.0f, 7);
            run (p, e, 512);
            check (p.meters.hornHz.load() < 0.05f && p.meters.drumHz.load() < 0.2f && allFinite (e), "Brake stops both rotors");
        }

        // Bucket brigade at a new sample rate: the filters are redesigned for it
        {
            TourbillonProcessor p;
            plain (p, vibrato, 0);
            setParam (p, "depth", 0.0f);
            float level[2] {};
            int k = 0;
            for (double rate : { 44100.0, 96000.0 })
            {
                prepare (p, 2, rate);
                auto b = tone (2, (int) rate, 1000.0, -12.0f, rate);
                run (p, b, 512);
                level[k++] = rmsDb (b, (int) rate / 2, (int) rate);
            }
            check (std::abs (level[0] - level[1]) < 0.5f && std::abs (level[0] + 15.01f) < 1.0f,
                   "BBD passes 1 kHz at the same level after a sample-rate change (" + juce::String (level[0], 2) + " / " + juce::String (level[1], 2) + " dB)");
        }
    }

    if (want ("Circuits against ngspice"))
    {
        juce::File ngspice (juce::SystemStats::getEnvironmentVariable ("NGSPICE", {}));
        if (! ngspice.existsAsFile())
            ngspice = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/ngspice-44.2/bin/ngspice");

        if (! ngspice.existsAsFile())
        {
            std::cout << "  SKIPPED: ngspice not found (set NGSPICE=/path/to/ngspice)" << std::endl;
        }
        else
        {
            // 192 kHz, half a second, three tones at once while the sweep moves; the comparison skips the first 50 ms
            const double rate = 192000.0, T = 1.0 / rate;
            const int total = (int) (0.5 * rate), from = (int) (0.05 * rate);
            const char* tones = "0.03*sin(2*pi*220*time)+0.03*sin(2*pi*1370*time)+0.03*sin(2*pi*3100*time)";
            auto input = [&] (double t)
            {
                return 0.03 * (std::sin (twoPi * 220.0 * t) + std::sin (twoPi * 1370.0 * t) + std::sin (twoPi * 3100.0 * t));
            };

            // runs a deck, whose output node is "out", with ngspice's steps no longer than an eighth of a
            // sample, and returns the output on the sample grid
            constexpr int fine = 8;
            auto simulate = [&] (const juce::String& circuit)
            {
                const auto deck = outDir.getChildFile ("circuit.cir"), data = outDir.getChildFile ("circuit.txt");
                data.deleteFile();
                const auto step = juce::String::formatted ("%.15e", T), maxStep = juce::String::formatted ("%.15e", T / fine);
                deck.replaceWithText ("Tourbillon circuit\n" + circuit
                                      + ".options reltol=1e-7 abstol=1e-15 vntol=1e-10 method=trap\n"
                                      + ".control\nset wr_singlescale\nset numdgt=15\ntran " + step + " 0.5 0 " + maxStep
                                      + (circuit.contains (".ic ") ? " uic" : "") + "\n"
                                      + "linearize v(out)\nwrdata circuit.txt v(out)\n.endc\n.end\n");
                juce::ChildProcess proc;
                // ngspice writes its data file relative to where it is run
                const juce::StringArray cmd { "/bin/sh", "-c", "cd " + outDir.getFullPathName().quoted() + " && SPICE_LIB_DIR="
                                              + ngspice.getParentDirectory().getSiblingFile ("share/ngspice").getFullPathName().quoted()
                                              + " " + ngspice.getFullPathName().quoted() + " -b circuit.cir > circuit.log 2>&1" };
                std::vector<double> ref;
                if (proc.start (cmd) && proc.waitForProcessToFinish (600000))
                {
                    juce::StringArray lines;
                    lines.addLines (data.loadFileAsString());
                    for (const auto& line : lines)
                    {
                        const auto parts = juce::StringArray::fromTokens (line.trim(), " \t", "");
                        if (parts.size() >= 2) ref.push_back (parts[parts.size() - 1].getDoubleValue());
                    }
                }
                return ref;
            };

            // The model is run twice: stepped as finely as ngspice, where any difference left means the
            // circuit is not the same, and at 192 kHz, which adds the trapezoidal rule's own error.
            // makeModel (rate) gives a function of (time, input) -> output.
            auto against = [&] (const char* name, const juce::String& circuit, auto&& makeModel)
            {
                const auto ref = simulate (circuit);
                if ((int) ref.size() < total) { check (false, juce::String ("ngspice produced a reference for the ") + name); return; }

                float diff[2] {};
                for (int pass = 0; pass < 2; ++pass)
                {
                    const int over = pass == 0 ? fine : 1;
                    auto model = makeModel (rate * over);
                    double err = 0.0, sig = 0.0;
                    for (int i = 0; i < total * over; ++i)
                    {
                        const double t = i * T / over, y = model (t, input (t));
                        if (i % over != 0 || i / over < from) continue;
                        const double r = ref[(size_t) (i / over)];
                        err += (y - r) * (y - r);
                        sig += r * r;
                    }
                    diff[pass] = (float) (10.0 * std::log10 (std::max (1.0e-30, err / sig)));
                }
                check (diff[0] < -80.0f, juce::String (name) + ": the same circuit as ngspice (" + juce::String (diff[0], 1)
                                         + " dB difference when stepped as finely)");
                check (diff[1] < -40.0f, juce::String (name) + ": " + juce::String (diff[1], 1)
                                         + " dB from the circuit when run at 192 kHz");
            };

            // Phaser: four stages, the gate bias swept at 1.5 Hz over 10 to 100 % of its range
            {
                auto pos = [] (double t) { return 0.1 + 0.9 * (0.5 + 0.5 * std::sin (twoPi * 1.5 * t)); };
                juce::String deck;
                deck << "Bin in 0 V=" << tones << "\n"
                     << "Bvg vg 0 V=" << PhaserStage::Vto << "+" << PhaserStage::biasSpan << "*(0.1+0.9*(0.5+0.5*sin(2*pi*1.5*time)))\n"
                     << ".model JX NJF(Vto=" << PhaserStage::Vto << " Beta=" << PhaserStage::beta << " Lambda=0)\n";
                for (int st = 1; st <= 4; ++st)
                {
                    const juce::String a = st == 1 ? "in" : "o" + juce::String (st - 1), b = st == 4 ? "out" : "o" + juce::String (st), n (st);
                    // the drain-gate network, idealised: the gate sits at its bias plus half the drain voltage
                    deck << "C" << n << " " << a << " p" << n << " " << PhaserStage::C << "\n"
                         << "R" << n << " p" << n << " 0 " << PhaserStage::Rfix << "\n"
                         << "Bg" << n << " g" << n << " 0 V=V(vg)+V(p" << n << ")/2\n"
                         << "J" << n << " p" << n << " g" << n << " 0 JX\n"
                         << "Bo" << n << " " << b << " 0 V=2*V(p" << n << ")-V(" << a << ")\n";
                }
                against ("phaser, 4 stages swept", deck, [&] (double r)
                {
                    std::array<PhaserStage, 4> stages;
                    for (auto& s : stages) s.prepare (r);
                    return [=] (double t, double x) mutable
                    {
                        const double G = PhaserStage::conductance (PhaserStage::gateBias (pos (t)));
                        for (auto& s : stages) x = s.process (x, G);
                        return x;
                    };
                });
            }

            // Vibe: the four stages, the photocells swept at 3 Hz between 5 and 95 % lit
            {
                auto cell = [] (double t) { return 0.05 + 0.9 * (0.5 + 0.5 * std::sin (twoPi * 3.0 * t)); };
                juce::String deck;
                deck << "Bin in 0 V=" << tones << "\n";
                for (int st = 0; st < 4; ++st)
                {
                    const auto& q = VibeStage::parts[st];
                    const juce::String a = st == 0 ? "in" : "o" + juce::String (st), b = st == 3 ? "out" : "o" + juce::String (st + 1), n (st + 1);
                    const juce::String ldr = "1/(" + juce::String::formatted ("%.15e", 1.0 / q.ldrMax) + "+" + juce::String::formatted ("%.15e", 1.0 / q.ldrMin - 1.0 / q.ldrMax)
                                           + "*(0.05+0.9*(0.5+0.5*sin(2*pi*3*time))))";
                    deck << "Ec" << n << " c" << n << " 0 " << a << " 0 " << -q.beta << "\n"
                         << "Ee" << n << " e" << n << " 0 " << a << " 0 " << q.alpha << "\n"
                         << "Cp" << n << " c" << n << " " << b << " " << juce::String (q.cp, 15) << "\n"
                         << "Cd" << n << " e" << n << " m" << n << " " << VibeStage::Cdc << "\n"
                         << "Br" << n << " m" << n << " " << b << " I=V(m" << n << "," << b << ")/(" << ldr << "+" << VibeStage::R6 << ")\n";
                }
                against ("vibe, 4 stages swept", deck, [&] (double r)
                {
                    std::array<VibeStage, 4> stages;
                    for (int st = 0; st < 4; ++st) stages[(size_t) st].prepare (r, st);
                    return [=] (double t, double x) mutable
                    {
                        for (int st = 0; st < 4; ++st) x = stages[(size_t) st].process (x, VibeStage::ldr (st, 0, cell (t)));
                        return x;
                    };
                });
            }

            // The lamp and photocell: filament temperature and carriers as two capacitors charged by their rates,
            // the lamp's voltage a 3 Hz sine from nearly off to full; the output is the carrier count
            {
                auto volts = [] (double t) { return 0.55 + 0.45 * std::sin (twoPi * 3.0 * t); };
                const double tau = 0.026, a4 = std::pow (LampCell::ambient, 4.0);
                juce::String deck;
                deck << "Bv v 0 V=0.55+0.45*sin(2*pi*3*time)\n"
                     << "Ct t 0 " << tau << "\n"
                     << "Bt 0 t I=V(v)*V(v)*pwr(V(t),-1.2)-" << juce::String::formatted ("%.15e", LampCell::radiate) << "*(pwr(V(t),4)-" << juce::String::formatted ("%.15e", a4) << ")\n"
                     << "Bl l 0 V=exp(-" << LampCell::wien << "*(1/V(t)-1))\n"
                     << "Cn out 0 1\n"
                     << "Bn 0 out I=" << juce::String::formatted ("%.15e", LampCell::genRate) << "*V(l)-V(out)/" << LampCell::lifetime << "-" << LampCell::pairRate << "*V(out)*V(out)\n"
                     << ".ic v(t)=" << LampCell::ambient << " v(out)=0\n";
                against ("lamp and photocell", deck, [&] (double r)
                {
                    LampCell lc;
                    lc.prepare (r);
                    lc.setThermalTime (tau);
                    return [=] (double t, double) mutable { lc.step (volts (t)); return lc.cell(); };
                });
            }
        }
    }

    if (want ("Click-free switching"))
    {
        TourbillonProcessor p;
        prepare (p);
        setParam (p, "mix", 50.0f);
        auto b = tone (2, (int) sr * 8, 220.0, -12.0f);
        juce::MidiBuffer midi;
        int pos = 0, step = 0;
        while (pos < b.getNumSamples())
        {
            if (pos % 12000 == 0)
            {
                setParam (p, "type", (float) ((step / 3) % numTypes));
                setParam (p, "voice", (float) (step % 3));
                ++step;
            }
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, pos, 480);
            p.processBlock (view, midi);
            pos += 480;
        }
        float worst = 0.0f;
        int worstAt = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 4800; i < b.getNumSamples(); ++i)
                if (const float d = std::abs (b.getSample (ch, i) - b.getSample (ch, i - 1)); d > worst) { worst = d; worstAt = i; }
        if (worst >= 0.03f)
        {
            const int at = worstAt / 12000;     // which switch it followed
            std::cout << "  worst step at sample " << worstAt << ", after switching to " << typeNames[(at / 3) % numTypes] << " "
                      << voiceNames[(at / 3) % numTypes][at % 3] << " (" << worstAt % 12000 << " samples later)" << std::endl;
        }
        // a 220 Hz sine at -12 dB moves at most 0.007 per sample; the circuits may add some, a click would add far more
        check (allFinite (b) && worst < 0.03f, "switching every type and voice while playing never clicks (largest step " + juce::String (worst, 4) + ")");
    }

    if (want ("Stress: every type / voice / rate / block size / layout"))
    {
        int runs = 0;
        bool ok = true;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int block : { 1, 37, 256, 2048 })
                for (int channels : { 1, 2 })
                    for (int combo = 0; combo < numTypes * 3; ++combo)
                    {
                        if (block == 1 && rate > 48000.0) continue;
                        if (block == 1 && combo % 2 == 1) continue;
                        TourbillonProcessor p;
                        prepare (p, channels, rate, block);
                        setParam (p, "type", (float) (combo / 3)); setParam (p, "voice", (float) (combo % 3));
                        setParam (p, "feedback", combo % 2 == 0 ? 95.0f : -95.0f);
                        setParam (p, "depth", 100.0f); setParam (p, "character", 100.0f); setParam (p, "envAmt", 100.0f);
                        setParam (p, "rate", 12.0f); setParam (p, "shape", (float) (combo % numShapes)); setParam (p, "width", 150.0f);
                        setParam (p, "wetLo", 300.0f); setParam (p, "wetHi", 3000.0f); setParam (p, "inGain", 12.0f);

                        auto b = noise (channels, (int) (rate * (block == 1 ? 0.25 : 1.0)), 0.0f, 50 + combo);
                        const int half = b.getNumSamples() / 2;
                        juce::AudioBuffer<float> first (b.getArrayOfWritePointers(), channels, 0, half);
                        run (p, first, block);
                        setParam (p, "manual", 0.0f); setParam (p, "rate", 0.02f); setParam (p, "sync", 1.0f); setParam (p, "bpm", 240.0f); setParam (p, "div", 14.0f);
                        juce::AudioBuffer<float> second (b.getArrayOfWritePointers(), channels, half, b.getNumSamples() - half);
                        run (p, second, block);
                        ++runs;
                        if (! allFinite (b) || b.getMagnitude (0, b.getNumSamples()) > 30.0f)
                        {
                            ok = false;
                            check (false, juce::String (rate) + " Hz, block " + juce::String (block) + ", " + juce::String (channels) + " ch, "
                                          + typeNames[combo / 3] + " " + voiceNames[combo / 3][combo % 3]
                                          + ": peak " + juce::String (b.getMagnitude (0, b.getNumSamples())));
                        }
                    }
        std::cout << "  " << runs << " runs" << std::endl;
        check (ok, "stress matrix completed with finite, bounded output");
    }

    if (want ("CPU"))
    {
        for (int combo = 0; combo < numTypes * 3; ++combo)
        {
            TourbillonProcessor p;
            prepare (p, 2, sr, 256);
            setParam (p, "type", (float) (combo / 3)); setParam (p, "voice", (float) (combo % 3));
            setParam (p, "depth", 70.0f); setParam (p, "rate", 1.0f);
            auto b = noise (2, (int) sr * 10, -12.0f, 5);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            run (p, b, 256);
            const double pct = (juce::Time::getMillisecondCounterHiRes() - start) / 100.0;
            std::cout << "  " << typeNames[combo / 3] << " " << voiceNames[combo / 3][combo % 3] << ": " << juce::String (pct, 2) << " % of one core at 48 kHz" << std::endl;
            check (pct < 10.0, "CPU use is reasonable");
        }
    }

    if (want ("Guide starting points"))
    {
        bool ok = true;
        for (auto* list : { &guide::sources, &guide::effects })
            for (auto& src : *list)
            {
                TourbillonProcessor p;
                prepare (p);
                for (auto& [id, v] : src.settings)
                    if (p.apvts.getParameter (id) == nullptr) { ok = false; check (false, juce::String (src.name) + " names an unknown control: " + id); }
                ui::applyStartingPoint (p, src);

                const auto in = noise (2, (int) sr * 2, -18.0f, 21);
                auto out = in;
                run (p, out, 512);
                juce::AudioBuffer<float> diff (2, in.getNumSamples());
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < in.getNumSamples(); ++i)
                        diff.setSample (ch, i, out.getSample (ch, i) - in.getSample (ch, i));
                const float change = rmsDb (diff, 24000, in.getNumSamples()) - rmsDb (in, 24000, in.getNumSamples());
                const float level = rmsDb (out, 24000, in.getNumSamples()) - rmsDb (in, 24000, in.getNumSamples());
                if (! allFinite (out) || change < -30.0f || level > 4.0f)
                {
                    ok = false;
                    check (false, juce::String (src.name) + ": change " + juce::String (change, 1) + " dB, level " + juce::String (level, 1) + " dB");
                }
            }

        TourbillonProcessor p;
        for (auto* base : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (base))
                if (guide::findHelp (r->paramID.toRawUTF8()) == nullptr) { ok = false; check (false, "no Guide entry for " + r->paramID); }
        for (auto* id : { "tap", "rate.hz", "motion", "meter.vu", "meter.in", "meter.out", "meter.lufs" })
            if (guide::findHelp (id) == nullptr) { ok = false; check (false, juce::String ("no Guide entry for ") + id); }
        check (ok, "every starting point and factory preset applies, changes the sound without jumping in level, and every control has a Guide entry");
    }

    if (want ("State"))
    {
        TourbillonProcessor p;
        prepare (p);
        setParam (p, "type", phaser); setParam (p, "depth", 33.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);

        TourbillonProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check ((int) value (q, "type") == phaser && std::abs (value (q, "depth") - 33.0f) < 0.01f, "settings survive a save and reload");

        auto saved = p.stampedState();
        check ((int) saved.getProperty (TourbillonProcessor::stateVersionId, 0) == TourbillonProcessor::stateVersion, "saved state carries its format version");

        // a state that does not mention a control resets it to its default
        auto partial = saved.createCopy();
        partial.removeChild (partial.getChildWithProperty ("id", "depth"), nullptr);
        setParam (q, "depth", 90.0f);
        q.loadState (partial);
        check (std::abs (value (q, "depth") - 50.0f) < 0.01f && (int) value (q, "type") == phaser, "a control missing from a state goes to its default");

        setParam (q, "depth", 10.0f);
        check (! q.loadState (juce::ValueTree ("SOMETHING_ELSE")) && std::abs (value (q, "depth") - 10.0f) < 0.01f, "data that is not a Tourbillon state is ignored");

        q.switchAB (1);
        setParam (q, "depth", 70.0f);
        q.switchAB (0);
        const float a = value (q, "depth");
        q.switchAB (1);
        check (std::abs (a - 10.0f) < 0.01f && std::abs (value (q, "depth") - 70.0f) < 0.01f, "A/B keeps two separate settings");
    }

    if (want ("Interface snapshots"))
    {
        // with audio running so the meters, the Motion display and the coach have something to show
        struct Shot { const char* name; const char* source; bool guide; };
        static const Shot shots[] = { { "guide", "Clean Guitar", true }, { "phaser", "Lead Guitar", true },
                                      { "rotary", "Organ", true }, { "tremolo", "Electric Piano", false } };

        for (auto& shot : shots)
        {
            TourbillonProcessor p;
            prepare (p);
            for (int i = 0; i < (int) guide::sources.size(); ++i)
                if (juce::String (guide::sources[(size_t) i].name) == shot.source)
                {
                    ui::applyStartingPoint (p, guide::sources[(size_t) i]);
                    p.apvts.state.setProperty ("guide_source", i, nullptr);
                }
            p.apvts.state.setProperty ("ui_guide", shot.guide, nullptr);
            p.apvts.state.setProperty ("ui_width", 1280, nullptr);

            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            for (int pass = 0; pass < 8; ++pass)
            {
                auto audio = noise (2, 12000, -16.0f, 11 + pass);
                run (p, audio, 512);
                CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.12, false);   // lets the editor timer run
            }

            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            const auto file = outDir.getChildFile (juce::String ("tourbillon-") + shot.name + ".png");
            file.deleteFile();
            juce::FileOutputStream os (file);
            juce::PNGImageFormat png;
            check (os.openedOk() && png.writeImageToStream (img, os), "rendered " + file.getFullPathName());
        }
    }

    std::cout << (failures == 0 ? "\nALL CHECKS PASSED" : "\nFAILURES: " + juce::String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
