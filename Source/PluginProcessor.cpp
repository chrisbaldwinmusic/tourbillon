#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tourbillon;

namespace
{
    void pushPeak (std::atomic<float>& dest, float v) noexcept
    {
        if (v > dest.load (std::memory_order_relaxed))
            dest.store (v, std::memory_order_relaxed);
    }

    float blockPeak (const float* x, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i) p = std::max (p, std::abs (x[i]));
        return p;
    }
}

//==============================================================================
TourbillonProcessor::TourbillonProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undo, "TOURBILLON", createParameterLayout())
{
    static const char* smoothedIds[numSmoothed] =
        { "wetLo", "wetHi", "depth", "manual", "feedback", "character", "width", "rate", "spread", "envAmt", "mix" };

    for (int i = 0; i < numSmoothed; ++i)
    {
        smSrc[i] = apvts.getRawParameterValue (smoothedIds[i]);
        jassert (smSrc[i] != nullptr);
    }

    auto p = [this] (const char* id) { auto* v = apvts.getRawParameterValue (id); jassert (v != nullptr); return v; };
    pType = p ("type");         pVoice = p ("voice");       pBypass = p ("bypass");     pInGain = p ("inGain");
    pOutGain = p ("outGain");   pBpm = p ("bpm");           pSync = p ("sync");         pDiv = p ("div");
    pShape = p ("shape");       pEnvExt = p ("envExt");     pWetOnly = p ("wetOnly");

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

bool TourbillonProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    const auto out = layouts.getMainOutputChannelSet();
    if ((out != mono && out != stereo) || out != layouts.getMainInputChannelSet())
        return false;

    const auto key = layouts.getChannelSet (true, 1);
    return key.isDisabled() || key == mono || key == stereo;
}

//==============================================================================
double TourbillonProcessor::currentBpm() const noexcept
{
    const float host = meters.hostBpm.load (std::memory_order_relaxed);
    return host > 1.0f ? (double) host : (double) pBpm->load();
}

double TourbillonProcessor::lfoRateHz() const noexcept
{
    if (pSync->load() > 0.5f)
        return currentBpm() / 60.0 / (double) divisionBeats[juce::jlimit (0, numDivisions - 1, (int) pDiv->load())];
    return (double) apvts.getRawParameterValue ("rate")->load();
}

double TourbillonProcessor::getTailLengthSeconds() const
{
    // the delay-based circuits ring on briefly with feedback; nothing else outlasts the input
    return 0.1;
}

//==============================================================================
void TourbillonProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    chunk = 32 * juce::jmax (1, juce::roundToInt (sr / 48000.0));
    fillSamples = (int) (0.03 * sr);

    lfo.prepare (sr);
    follower.prepare (sr);
    circuit.prepare (sr);
    loudness.prepare (sr);
    wetHp.reset(); wetLp.reset();

    for (auto* f : { &bypassFade, &circuitFade, &wetHpFade, &wetLpFade })
        f->prepare (sr);
    gainsPrimed = false;
    hostCycles = -1.0;

    inGain.reset (sr, 0.02);
    outGain.reset (sr, 0.02);
    inGain.setCurrentAndTargetValue (dbToGain (pInGain->load()));
    outGain.setCurrentAndTargetValue (dbToGain (pOutGain->load()));

    smCoef = 1.0f - (float) std::exp (-(double) chunk / (0.008 * sr));
    smPrimed = false;

    for (auto* v : { &monoR, &dryL, &dryR, &wetL, &wetR, &tmpL, &tmpR, &lfoL, &lfoR, &envBuf })
        v->assign ((size_t) chunk, 0.0f);

    vuState = 0.0f;
    vuCoef = msToCoef (150.0f, sr);

    setLatencySamples (0);      // nothing looks ahead or oversamples, so the dry signal is never delayed
    prepared = true;
}

//==============================================================================
void TourbillonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int mainCh = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels());
    const int n = buffer.getNumSamples();

    if (mainCh < 1 || n == 0 || ! prepared)
        return;

    float hostBpm = 0.0f;
    hostCycles = -1.0;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto bpm = pos->getBpm())
                hostBpm = (float) *bpm;
            // a synced LFO follows the song position while the transport runs, so it lands the same way every pass
            if (const auto ppq = pos->getPpqPosition(); ppq && pos->getIsPlaying() && pSync->load() > 0.5f)
                hostCycles = *ppq / (double) divisionBeats[juce::jlimit (0, numDivisions - 1, (int) pDiv->load())];
        }
    meters.hostBpm.store (hostBpm, std::memory_order_relaxed);

    const bool mono = mainCh < 2;
    float* l = buffer.getWritePointer (0);
    float* r = mono ? nullptr : buffer.getWritePointer (1);

    // sidechain input, when the host has connected one: its channels follow the main input's
    const int keyCh = getBusCount (true) > 1 ? juce::jmin (getChannelCountOfBus (true, 1), buffer.getNumChannels() - mainCh) : 0;
    const float* kl = keyCh > 0 ? buffer.getReadPointer (mainCh) : nullptr;
    const float* kr = keyCh > 1 ? buffer.getReadPointer (mainCh + 1) : kl;
    meters.keyConnected.store (kl != nullptr, std::memory_order_relaxed);

    for (int pos = 0; pos < n; pos += chunk)
    {
        const int m = juce::jmin (chunk, n - pos);
        const float* ckl = kl != nullptr ? kl + pos : nullptr;
        const float* ckr = kl != nullptr ? kr + pos : nullptr;

        if (mono)
        {
            std::copy (l + pos, l + pos + m, monoR.begin());
            processChunk (l + pos, monoR.data(), ckl, ckr, m);
            for (int i = 0; i < m; ++i)     // a mono track hears both sides of the stereo effect
                l[pos + i] = 0.5f * (l[pos + i] + monoR[(size_t) i]);
        }
        else
        {
            processChunk (l + pos, r + pos, ckl, ckr, m);
        }
        if (hostCycles >= 0.0)
            hostCycles += lfoRateHz() * (double) m / sr;
    }

    meters.lufsS.store (loudness.shortTerm, std::memory_order_relaxed);
    meters.lufsM.store (loudness.momentary, std::memory_order_relaxed);
}

void TourbillonProcessor::processChunk (float* l, float* r, const float* kl, const float* kr, int n)
{
    const bool bypassed = pBypass->load() > 0.5f;

    for (int i = 0; i < numSmoothed; ++i)
    {
        const float target = smSrc[i]->load (std::memory_order_relaxed);
        sm[i] = smPrimed ? sm[i] + smCoef * (target - sm[i]) : target;
    }
    smPrimed = true;

    // ---- input ----------------------------------------------------------------
    inGain.setTargetValue (bypassed ? 1.0f : dbToGain (pInGain->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.getNextValue();
        l[i] *= g; r[i] *= g;
    }
    pushPeak (meters.inPeak[0], blockPeak (l, n));
    pushPeak (meters.inPeak[1], blockPeak (r, n));
    if (kl != nullptr)
        pushPeak (meters.keyPeak, std::max (blockPeak (kl, n), blockPeak (kr, n)));

    // ---- effect ---------------------------------------------------------------
    // Bypass crossfades to the untouched input. Once fully bypassed the circuit stops running,
    // and it starts again from silence.
    bool cold = false;
    if (bypassFade.begin (! bypassed, 0, cold))
    {
        if (cold)
        {
            circuit.reset(); follower.reset(); wetHp.reset(); wetLp.reset();
            gainsPrimed = false;
            bypassFade.hold = fillSamples;      // as for a change of type: let the delay lines fill first
        }
        std::copy (l, l + n, dryL.begin());
        std::copy (r, r + n, dryR.begin());
        processEffect (l, r, kl, kr, n);
        bypassFade.mix (l, r, dryL.data(), dryR.data(), n);
    }

    // ---- output ---------------------------------------------------------------
    outGain.setTargetValue (bypassed ? 1.0f : dbToGain (pOutGain->load()));
    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        l[i] *= g; r[i] *= g;
        pkL = std::max (pkL, std::abs (l[i]));
        pkR = std::max (pkR, std::abs (r[i]));
        vuState += vuCoef * (0.5f * (l[i] * l[i] + r[i] * r[i]) - vuState);
    }
    pushPeak (meters.outPeak[0], pkL);
    pushPeak (meters.outPeak[1], pkR);
    meters.vuRms.store (std::sqrt (vuState), std::memory_order_relaxed);
    loudness.process (l, r, n);
}

/** On entry l/r hold the dry signal (also in dryL/dryR); on exit they hold the finished mix. */
void TourbillonProcessor::processEffect (float* l, float* r, const float* kl, const float* kr, int n)
{
    const int type = juce::jlimit (0, numTypes - 1, (int) pType->load());
    const int voice = juce::jlimit (0, 2, (int) pVoice->load());

    // ---- envelope and LFO -------------------------------------------------------
    const bool keyed = kl != nullptr && pEnvExt->load() > 0.5f;
    follower.process (keyed ? kl : dryL.data(), keyed ? kr : dryR.data(), envBuf.data(), n);

    const double rate = pSync->load() > 0.5f ? lfoRateHz() : (double) sm[sRate];
    if (hostCycles >= 0.0) lfo.syncTo (hostCycles);
    lfo.process (lfoL.data(), lfoR.data(), n, juce::jlimit (0, numShapes - 1, (int) pShape->load()), rate, sm[sSpread]);

    // ---- the circuit --------------------------------------------------------------
    // A change of type or voice fades to the dry signal, swaps, and fades back. The rotary
    // speaker's voices are its speed switch, which is played live, so they never interrupt it.
    const int choice = type * 4 + (type == rotary ? 0 : voice);
    bool cold = false;
    circuitFade.begin (true, choice, cold);
    if (cold)
    {
        // the delay-based circuits start from an empty line: stay on the dry signal until it has
        // filled (30 ms covers the longest delay any of them uses), so the new circuit fades in
        // on real signal instead of the audio arriving all at once
        circuit.reset();
        circuitFade.hold = fillSamples;
    }

    Modulator::Params p;
    p.type = circuitFade.active / 4;
    p.voice = p.type == rotary ? voice : circuitFade.active % 4;
    p.depth = 0.01f * sm[sDepth];
    p.manual = 0.01f * sm[sManual];
    p.feedback = 0.01f * sm[sFeedback];
    p.character = 0.01f * sm[sCharacter];
    p.envAmt = 0.01f * sm[sEnvAmt];
    p.spreadDeg = sm[sSpread];
    p.rateHz = rate;
    circuit.set (p);
    circuit.process (l, r, wetL.data(), wetR.data(), lfoL.data(), lfoR.data(), envBuf.data(), n);
    circuitFade.mix (wetL.data(), wetR.data(), dryL.data(), dryR.data(), n);

    // ---- the tone of the wet signal ------------------------------------------------
    float* const wl = wetL.data();
    float* const wr = wetR.data();
    auto wetStage = [&] (Fade& f, bool want, Biquad& filter, auto&& design)
    {
        bool stageCold = false;
        if (! f.begin (want, 0, stageCold)) return;
        if (stageCold) filter.reset();

        const bool fading = ! f.steady();
        if (fading)
        {
            std::copy (wl, wl + n, tmpL.begin());
            std::copy (wr, wr + n, tmpR.begin());
        }
        design();
        for (int i = 0; i < n; ++i) { wl[i] = filter.process (0, wl[i]); wr[i] = filter.process (1, wr[i]); }
        if (fading) f.mix (wl, wr, tmpL.data(), tmpR.data(), n);
    };
    wetStage (wetHpFade, sm[sWetLo] > 20.5f,    wetHp, [&] { wetHp.setHighPass (sr, sm[sWetLo], 0.707); });
    wetStage (wetLpFade, sm[sWetHi] < 19900.0f, wetLp, [&] { wetLp.setLowPass (sr, sm[sWetHi], 0.707); });

    // ---- gains ramp across the chunk, so nothing zippers ------------------------
    struct Ramp { float from, step; float at (int i) const noexcept { return from + step * (float) (i + 1); } };
    auto ramp = [this, n] (float& state, float to)
    {
        if (! gainsPrimed) state = to;
        const Ramp rp { state, (to - state) / (float) n };
        state = to;
        return rp;
    };

    // Mix: below 50 % the wet signal is turned down, above it the dry one, and the sum is kept at
    // the level of either alone, so a phaser's notches are deepest, and its level unchanged, at 50 %.
    const float mix = 0.01f * sm[sMix];
    const bool wetOnly = pWetOnly->load() > 0.5f;
    float dg = wetOnly ? 0.0f : juce::jmin (1.0f, 2.0f * (1.0f - mix));
    float wg = wetOnly ? 1.0f : juce::jmin (1.0f, 2.0f * mix);
    const float norm = 1.0f / juce::jmax (1.0e-3f, dg + wg);
    dg *= norm; wg *= norm;

    const Ramp gDry   = ramp (dryGain, gainsPrimed ? dryGain + smCoef * (dg - dryGain) : dg);
    const Ramp gWet   = ramp (wetGain, gainsPrimed ? wetGain + smCoef * (wg - wetGain) : wg);
    const Ramp gWidth = ramp (widthGain, 0.01f * sm[sWidth]);
    gainsPrimed = true;

    float wetPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float mid = 0.5f * (wl[i] + wr[i]), side = 0.5f * (wl[i] - wr[i]) * gWidth.at (i);
        const float a = mid + side, b = mid - side;
        wetPk = std::max (wetPk, std::max (std::abs (a), std::abs (b)));
        const float d = gDry.at (i), w = gWet.at (i);
        l[i] = dryL[(size_t) i] * d + a * w;
        r[i] = dryR[(size_t) i] * d + b * w;
    }

    // ---- what the Motion display shows -----------------------------------------
    pushPeak (meters.wetPeak, wetPk);
    meters.lfoPosition.store (lfo.position(), std::memory_order_relaxed);
    meters.env.store (envBuf[(size_t) n - 1], std::memory_order_relaxed);
    const auto& s = circuit.state;
    for (int ch = 0; ch < 2; ++ch)
    {
        meters.position[ch].store (s.position[ch], std::memory_order_relaxed);
        meters.delayMs[ch].store (s.delayMs[ch], std::memory_order_relaxed);
        meters.breakHz[ch].store (s.breakHz[ch], std::memory_order_relaxed);
        meters.cell[ch].store (s.cell[ch], std::memory_order_relaxed);
    }
    meters.hornHz.store (s.hornHz, std::memory_order_relaxed);
    meters.drumHz.store (s.drumHz, std::memory_order_relaxed);
    meters.hornAngle.store (s.hornAngle, std::memory_order_relaxed);
    meters.drumAngle.store (s.drumAngle, std::memory_order_relaxed);
}

//==============================================================================
void TourbillonProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = stampedState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TourbillonProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        loadState (juce::ValueTree::fromXml (*xml));
}

juce::ValueTree TourbillonProcessor::stampedState()
{
    auto tree = apvts.copyState();
    tree.setProperty (stateVersionId, stateVersion, nullptr);
    tree.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);     // for reading by eye only
    return tree;
}

/** Brings a saved tree written by an older Tourbillon up to the current format, one version at a
    time. Each step is a case that fixes up what changed in that version (a renamed id, a new
    range, a choice list that grew), so a session from any earlier version loads as it sounded. */
static void migrateState (juce::ValueTree& tree, int from)
{
    for (int v = from; v < TourbillonProcessor::stateVersion; ++v)
    {
        switch (v)
        {
            // case 1: ... changes from version 1 to version 2 go here
            default: break;
        }
    }
    juce::ignoreUnused (tree);
}

bool TourbillonProcessor::loadState (juce::ValueTree tree)
{
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return false;

    // A state from a newer Tourbillon is loaded as far as it can be: controls this version has are
    // restored, anything it does not know about is ignored.
    const int version = (int) tree.getProperty (stateVersionId, 1);
    migrateState (tree, juce::jmin (version, stateVersion));

    // A control the saved state does not mention goes to its default, rather than keeping whatever
    // it was set to before the load, so a session or preset always sounds the same however it is opened.
    for (auto* base : getParameters())
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (! tree.getChildWithProperty ("id", param->paramID).isValid())
            {
                juce::ValueTree child ("PARAM");
                child.setProperty ("id", param->paramID, nullptr);
                child.setProperty ("value", param->convertFrom0to1 (param->getDefaultValue()), nullptr);
                tree.appendChild (child, nullptr);
            }

    tree.setProperty (stateVersionId, stateVersion, nullptr);
    apvts.replaceState (tree);
    return true;
}

void TourbillonProcessor::switchAB (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abSlot) return;

    abState[abSlot] = apvts.copyState();
    if (abState[slot].isValid())
        apvts.replaceState (abState[slot].createCopy());    // an unused slot simply starts as a copy of the other
    abSlot = slot;
}

juce::AudioProcessorEditor* TourbillonProcessor::createEditor()
{
    return new TourbillonEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TourbillonProcessor();
}
