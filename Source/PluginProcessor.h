#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/Common.h"
#include "DSP/Lfo.h"
#include "DSP/Modulation.h"
#include "Parameters.h"

class TourbillonProcessor final : public juce::AudioProcessor
{
public:
    TourbillonProcessor();
    ~TourbillonProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Tourbillon"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override  { return bypassParam; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Version of the saved-state format. Raise it whenever a saved session or preset would load
        differently (a parameter renamed, a range or choice list changed) and add a step to
        migrateState in PluginProcessor.cpp that converts the previous version. */
    static constexpr int stateVersion = 1;
    static constexpr const char* stateVersionId = "stateVersion";

    /** The settings as saved in sessions and presets, stamped with the format version. */
    juce::ValueTree stampedState();
    /** Restores settings saved by any version of Tourbillon. Returns false if the tree is not a Tourbillon state. */
    bool loadState (juce::ValueTree tree);

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

    /** A/B comparison: two complete settings snapshots held for the life of the plugin instance. */
    int  getABSlot() const noexcept     { return abSlot; }
    void switchAB (int slot);

    /** The tempo a synced LFO follows: the host's when it gives one, otherwise the Tempo control. */
    double currentBpm() const noexcept;
    /** The LFO's speed in Hz, after Sync. */
    double lfoRateHz() const noexcept;

    /** Written on the audio thread, read (and peak-reset) by the editor. */
    struct Meters
    {
        std::atomic<float> inPeak[2] { { 0.0f }, { 0.0f } }, outPeak[2] { { 0.0f }, { 0.0f } };
        std::atomic<float> vuRms { 0.0f };
        std::atomic<float> wetPeak { 0.0f };                            // level the circuit is producing
        std::atomic<float> lufsS { -100.0f }, lufsM { -100.0f };
        std::atomic<float> hostBpm { 0.0f };                            // 0 when the host gives no tempo
        std::atomic<float> keyPeak { 0.0f };                            // level arriving at the sidechain input
        std::atomic<bool>  keyConnected { false };                      // the host has given us a sidechain bus

        // what the circuit is doing now, for the Motion display
        std::atomic<double> lfoPosition { 0.0 };                        // running LFO position in cycles
        std::atomic<float> env { 0.0f };
        std::atomic<float> position[2] { { 0.0f }, { 0.0f } };
        std::atomic<float> delayMs[2] { { 0.0f }, { 0.0f } };
        std::atomic<float> breakHz[2] { { 0.0f }, { 0.0f } };
        std::atomic<float> hornHz { 0.0f }, drumHz { 0.0f }, hornAngle { 0.0f }, drumAngle { 0.0f };
    } meters;

private:
    // chunk-rate smoothed parameters
    enum Sm { sWetLo, sWetHi, sDepth, sManual, sFeedback, sCharacter, sWidth, sRate, sSpread, sEnvAmt, sMix, numSmoothed };

    void processChunk (float* l, float* r, const float* keyL, const float* keyR, int n);
    void processEffect (float* l, float* r, const float* keyL, const float* keyR, int n);

    std::atomic<float>* smSrc[numSmoothed] {};
    float sm[numSmoothed] {};
    float smCoef = 0.1f;
    bool smPrimed = false;

    std::atomic<float> *pType {}, *pVoice {}, *pBypass {}, *pInGain {}, *pOutGain {}, *pBpm {};
    std::atomic<float> *pSync {}, *pDiv {}, *pShape {}, *pEnvExt {}, *pWetOnly {};
    juce::AudioParameterBool* bypassParam = nullptr;

    double sr = 48000.0;
    int chunk = 32, fillSamples = 1440;
    bool prepared = false;
    double hostCycles = -1.0;       // LFO position the host's timeline asks for at the next chunk (-1: none)

    juce::SmoothedValue<float> inGain, outGain;
    tourbillon::Lfo lfo;
    tourbillon::EnvelopeFollower follower;
    tourbillon::Modulator circuit;
    tourbillon::Biquad wetHp, wetLp;
    tourbillon::LoudnessMeter loudness;

    // click-free switching
    tourbillon::Fade bypassFade, circuitFade, wetHpFade, wetLpFade;

    // gains that ramp across each chunk
    float dryGain = 1.0f, wetGain = 1.0f, widthGain = 1.0f;
    bool gainsPrimed = false;

    std::vector<float> monoR, dryL, dryR, wetL, wetR, tmpL, tmpR, lfoL, lfoR, envBuf;
    float vuState = 0.0f, vuCoef = 0.001f;

    juce::ValueTree abState[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TourbillonProcessor)
};
