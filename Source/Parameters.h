#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tourbillon
{
enum Type { chorus = 0, flanger, phaser, vibe, tremolo, vibrato, rotary, numTypes };

inline const char* const typeNames[numTypes] = { "Chorus", "Flanger", "Phaser", "Vibe", "Tremolo", "Vibrato", "Rotary" };

/** Each type has three voices. The parameter itself is just I / II / III, so a session keeps its
    voice when the type changes; the editor shows these names for the type in use. */
inline const char* const voiceNames[numTypes][3] =
{
    { "Classic",  "Dimension",    "Ensemble" },
    { "BBD",      "Through-Zero", "Clean" },
    { "4 Stage",  "8 Stage",      "12 Stage" },
    { "Classic",  "Bright",       "Deep" },
    { "Bias",     "Opto",         "Harmonic" },
    { "BBD",      "Clean",        "Tape" },
    { "Slow",     "Fast",         "Brake" },
};

enum Shape { sine = 0, triangle, square, rampUp, rampDown, sampleHold, drift, numShapes };

/** Tempo-synced LFO cycle lengths, in beats (a quarter note is one beat), slowest first. */
inline constexpr int numDivisions = 15;
inline constexpr float divisionBeats[numDivisions] =
{
    16.0f, 8.0f, 4.0f, 3.0f, 2.0f, 4.0f / 3.0f, 1.5f, 1.0f, 2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f
};

namespace format
{
    inline juce::String db (float v, int)       { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
    inline juce::String hz (float v, int)       { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                                                                     : juce::String (v / 1000.0f, 2) + " kHz"; }
    inline juce::String rate (float v, int)     { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }
    inline juce::String pct (float v, int)      { return juce::String (juce::roundToInt (v)) + " %"; }
    inline juce::String signedPct (float v, int){ return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %"; }
    inline juce::String deg (float v, int)      { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); }
    inline juce::String bpm (float v, int)      { return juce::String (v, 1) + " BPM"; }
    inline juce::String lowCut (float v, int n) { return v <= 20.5f ? juce::String ("OFF") : hz (v, n); }
    inline juce::String highCut (float v, int n){ return v >= 19900.0f ? juce::String ("OFF") : hz (v, n); }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    using Fmt = String (*) (float, int);

    auto addF = [&layout] (const char* id, const char* name, float lo, float hi, float def, Fmt fmt, float centre = 0.0f, float step = 0.0f)
    {
        NormalisableRange<float> range (lo, hi, step);
        if (centre > 0.0f) range.setSkewForCentre (centre);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
    };
    auto addB = [&layout] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto addC = [&layout] (const char* id, const char* name, StringArray choices, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def));
    };

    StringArray types;
    for (auto* t : typeNames) types.add (t);

    addC ("type", "Type", types, chorus);
    addC ("voice", "Voice", { "I", "II", "III" }, 0);
    addB ("bypass", "Bypass", false);

    // Input: level, tempo, and the tone of what the circuit adds (the dry signal is never filtered)
    addF ("inGain", "Input Gain", -24.0f, 24.0f, 0.0f, format::db);
    addF ("bpm", "Tempo", 40.0f, 240.0f, 120.0f, format::bpm, 0.0f, 0.1f);
    addF ("wetLo", "Wet Low Cut", 20.0f, 1000.0f, 20.0f, format::lowCut, 160.0f);
    addF ("wetHi", "Wet High Cut", 1000.0f, 20000.0f, 20000.0f, format::highCut, 6000.0f);

    // The circuit
    addF ("depth", "Depth", 0.0f, 100.0f, 50.0f, format::pct);
    addF ("manual", "Manual", 0.0f, 100.0f, 50.0f, format::pct);
    addF ("feedback", "Feedback", -95.0f, 95.0f, 0.0f, format::signedPct);
    addF ("character", "Character", 0.0f, 100.0f, 40.0f, format::pct);
    addF ("width", "Width", 0.0f, 150.0f, 100.0f, format::pct);

    // The LFO, and the envelope that can push the sweep
    addF ("rate", "Rate", 0.02f, 12.0f, 0.6f, format::rate, 1.0f);
    addB ("sync", "Sync", false);
    addC ("div", "Rate (note length)", { "4 bars", "2 bars", "1 bar", "1/2D", "1/2", "1/2T", "1/4D", "1/4",
                                         "1/4T", "1/8D", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 2);
    addC ("shape", "LFO Shape", { "Sine", "Triangle", "Square", "Ramp Up", "Ramp Down", "S&H", "Drift" }, sine);
    addF ("spread", "Stereo Phase", 0.0f, 180.0f, 90.0f, format::deg);
    addF ("envAmt", "Envelope", -100.0f, 100.0f, 0.0f, format::signedPct);
    addB ("envExt", "Envelope Ext Key", false);

    // Output
    addF ("mix", "Mix", 0.0f, 100.0f, 50.0f, format::pct);
    addB ("wetOnly", "Wet Only", false);
    addF ("outGain", "Output Trim", -24.0f, 24.0f, 0.0f, format::db);

    return layout;
}
} // namespace tourbillon
