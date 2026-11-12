#include "PluginEditor.h"

using namespace tourbillon;
using namespace tourbillon::ui;

namespace
{
    constexpr int headerH = 74, rowH = 300, guideH = 200, margin = 10, gap = 8, meterW = 262;
    constexpr int sourceId = 200, effectId = 400, userId = 1000;     // preset menu item ids

    float toDb (float gain)     { return gain > 1.0e-5f ? 20.0f * std::log10 (gain) : -100.0f; }

    /** The signal path of each type and voice, written under the type selector. */
    const char* const circuitPaths[numTypes][3] =
    {
        { "ONE 1024-STAGE BBD PER SIDE|DRY + WET", "TWO BBDS SWEPT IN OPPOSITION|ONE TO EACH SIDE", "THREE BBDS PER SIDE|FIXED SINES + SHIMMER" },
        { "BBD, 0.25 TO 12 MS|FEEDBACK LOOP", "SWEPT COPY|AGAINST A FIXED 4 MS COPY", "CLEAN DELAY, 0.25 TO 12 MS|FEEDBACK LOOP" },
        { "4 ALL-PASS STAGES|JFET SWEEP|REGEN", "8 ALL-PASS STAGES|JFET SWEEP|REGEN", "12 ALL-PASS STAGES|JFET SWEEP|REGEN" },
        { "LAMP|PHOTOCELLS|4 MISMATCHED STAGES", "LAMP|BRIGHT PHOTOCELLS|4 MISMATCHED STAGES", "LAMP|DARK PHOTOCELLS|4 MISMATCHED STAGES" },
        { "VALVE STAGE|BIAS SWEPT BY THE LFO", "LAMP|PHOTOCELL|LEVEL", "CROSSOVER|BASS AND TREBLE IN OPPOSITE PHASE" },
        { "BBD|PITCH ONLY", "CLEAN DELAY|PITCH ONLY", "TAPE TRANSPORT|WOW + FLUTTER" },
        { "AMP|800 HZ CROSSOVER|HORN + DRUM|TWO MICS", "AMP|800 HZ CROSSOVER|HORN + DRUM|TWO MICS", "AMP|800 HZ CROSSOVER|HORN + DRUM|TWO MICS" },
    };
}

//==============================================================================
TourbillonEditor::TourbillonEditor (TourbillonProcessor& p)
    : AudioProcessorEditor (&p), proc (p), apvts (p.apvts),
      input   ("INPUT",      col::ivory, 2),
      circuit ("MODULATION", col::teal,  6),
      output  ("OUTPUT",     col::ivory, 2),
      motionPanel (p),
      guidePanel (p, p.apvts)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    for (auto* s : { &input, &circuit, &output })
        content.addAndMakeVisible (s);
    content.addAndMakeVisible (motionPanel);
    content.addAndMakeVisible (meterPanel);
    content.addAndMakeVisible (guidePanel);

    // ---- INPUT ----
    knob (input, "inGain", "GAIN");
    tempoKnob = knob (input, "bpm", "TEMPO");
    knob (input, "wetLo", "WET LOW CUT");
    knob (input, "wetHi", "WET HIGH CUT");
    tapButton.setClickingTogglesState (false);
    tapButton.onClick = [this] { tapTempo(); };
    setHelpId (tapButton, "tap");
    input.addHeader (&tapButton, 40);

    // ---- MODULATION ----  (top row: the circuit; bottom row: what moves it)
    voiceBox = combo (circuit, "voice", "VOICE", 110);
    depthKnob = knob (circuit, "depth", "DEPTH");
    manualKnob = knob (circuit, "manual", "MANUAL");
    feedbackKnob = knob (circuit, "feedback", "FEEDBACK");
    characterKnob = knob (circuit, "character", "CHARACTER");
    knob (circuit, "width", "WIDTH");
    rateCell = new RateCell (apvts, circuit.accent);
    owned.add (rateCell); circuit.addCell (rateCell);
    shapeBox = combo (circuit, "shape", "SHAPE", 96);
    spreadKnob = knob (circuit, "spread", "STEREO PHASE");
    envKnob = knob (circuit, "envAmt", "ENVELOPE");
    rateRead = readout (circuit, "LFO Hz");
    setHelpId (*rateRead, "rate.hz");
    headerButton (circuit, "envExt", "EXT KEY", 62);
    headerButton (circuit, "sync", "SYNC", 48);

    // ---- OUTPUT ----
    knob (output, "mix", "MIX");
    knob (output, "outGain", "TRIM");
    {
        auto* cell = new CaptionCell (std::make_unique<LedButton> (apvts, "wetOnly", "SEND", col::ivory), "WET ONLY", 56, 26);
        owned.add (cell); output.addCell (cell);
    }
    peakRead = readout (output, "PEAK dBFS");
    setHelpId (*peakRead, "meter.out");

    // ---- header ----
    bypassButton = std::make_unique<LedButton> (apvts, "bypass", "BYPASS", col::red);
    content.addAndMakeVisible (*bypassButton);

    typeButtons = std::make_unique<ChoiceButtons> (apvts, "type", col::gold);
    typeButtons->setTextSize (10.5f, 0.1f);
    content.addAndMakeVisible (*typeButtons);

    // The Guide strip can fold away; the choice is saved with the session.
    showGuide = (bool) apvts.state.getProperty ("ui_guide", true);
    guideButton.onClick = [this] { setGuideShown (guideButton.getToggleState()); };
    guideButton.setToggleState (showGuide, juce::dontSendNotification);

    // A/B: two complete settings snapshots to flip between
    auto showAB = [this]
    {
        aButton.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
        bButton.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    };
    aButton.onClick = [this, showAB] { proc.switchAB (0); showAB(); };
    bButton.onClick = [this, showAB] { proc.switchAB (1); showAB(); };
    showAB();

    for (auto* b : { &undoButton, &redoButton }) b->setClickingTogglesState (false);
    undoButton.onClick = [this] { proc.undo.undo(); };
    redoButton.onClick = [this] { proc.undo.redo(); };

    presetBox.setTextWhenNothingSelected ("PRESETS");
    presetBox.onChange = [this] { presetChosen(); };
    refreshPresets();

    for (auto* c : std::initializer_list<juce::Component*> { &guideButton, &aButton, &bButton, &undoButton, &redoButton, &presetBox })
        content.addAndMakeVisible (c);

    content.addMouseListener (this, true);
    setResizable (true, true);

    // Open at a size that fits this display, leaving room for the host's own window chrome.
    int width = (int) apvts.state.getProperty ("ui_width", 0);
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto screen = display->userBounds;
        const double fit = juce::jmin (1.0, (double) (screen.getHeight() - 110) / (double) baseH(),
                                            (double) (screen.getWidth() - 40) / (double) baseW);
        const int fitWidth = juce::roundToInt (fit * baseW);
        width = width > 0 ? juce::jmin (width, fitWidth) : fitWidth;
    }
    applyWindowSize (width > 0 ? width : baseW);

    timerCallback();
    startTimerHz (30);
}

TourbillonEditor::~TourbillonEditor()
{
    stopTimer();
    content.removeMouseListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void TourbillonEditor::applyWindowSize (int width)
{
    const double aspect = (double) baseW / (double) baseH();
    width = juce::jlimit (baseW / 2, baseW * 2, width);
    getConstrainer()->setFixedAspectRatio (aspect);
    setResizeLimits (baseW / 2, juce::roundToInt (baseW / 2 / aspect), baseW * 2, juce::roundToInt (baseW * 2 / aspect));
    setSize (width, juce::roundToInt (width / aspect));
    resized();
}

void TourbillonEditor::setGuideShown (bool shown)
{
    showGuide = shown;
    apvts.state.setProperty ("ui_guide", shown, nullptr);
    guideButton.setToggleState (shown, juce::dontSendNotification);
    applyWindowSize (getWidth());
}

//==============================================================================
juce::File TourbillonEditor::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Audio/Presets/Channel Audio/Tourbillon");
}

void TourbillonEditor::refreshPresets()
{
    presetFiles = presetFolder().findChildFiles (juce::File::findFiles, false, "*.tourbillon");
    presetFiles.sort();

    presetBox.clear (juce::dontSendNotification);
    presetBox.addItem ("Init (reset everything)", 1);

    // factory presets: the Guide's starting points, then the other jobs the unit can do
    presetBox.addSectionHeading ("Sources");
    for (int i = 0; i < (int) guide::sources.size(); ++i)
        presetBox.addItem (guide::sources[(size_t) i].name, sourceId + i);
    presetBox.addSectionHeading ("Effects");
    for (int i = 0; i < (int) guide::effects.size(); ++i)
        presetBox.addItem (guide::effects[(size_t) i].name, effectId + i);

    if (! presetFiles.isEmpty()) presetBox.addSectionHeading ("Yours");
    for (int i = 0; i < presetFiles.size(); ++i)
        presetBox.addItem (presetFiles[i].getFileNameWithoutExtension(), userId + i);
    presetBox.addSeparator();
    presetBox.addItem ("Save preset...", 2);
    presetBox.addItem ("Show presets folder", 3);
}

void TourbillonEditor::presetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id == 0) return;

    if (id >= userId)
    {
        loadPreset (presetFiles[id - userId]);
        return;     // leave the preset's name showing
    }
    if (id >= sourceId)
    {
        // like the Guide's button: Wet Only, the gains and the tempo are left as they are
        proc.undo.beginNewTransaction ("Preset");
        applyStartingPoint (proc, id >= effectId ? guide::effects[(size_t) (id - effectId)] : guide::sources[(size_t) (id - sourceId)]);
        return;
    }

    presetBox.setSelectedId (0, juce::dontSendNotification);
    if (id == 1) resetToInit();
    if (id == 2) savePreset();
    if (id == 3) { presetFolder().createDirectory(); presetFolder().revealToUser(); }
}

void TourbillonEditor::resetToInit()
{
    proc.undo.beginNewTransaction ("Init");
    for (auto* base : proc.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

/** Sets the Tempo control from the average gap between the last few clicks. */
void TourbillonEditor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();
    taps.add (now);
    while (taps.size() > 6) taps.remove (0);
    if (taps.size() < 2) return;

    const double bpm = 60000.0 * (double) (taps.size() - 1) / (taps.getLast() - taps.getFirst());
    if (auto* p = apvts.getParameter ("bpm"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (40.0, 240.0, bpm)));
        p->endChangeGesture();
    }
}

void TourbillonEditor::savePreset()
{
    presetFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save Tourbillon preset", presetFolder().getChildFile ("My Preset.tourbillon"), "*.tourbillon");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File()) return;
        file = file.withFileExtension ("tourbillon");

        // a preset is the sound only: window and Guide choices stay with the session
        auto tree = proc.stampedState();
        for (int i = tree.getNumProperties(); --i >= 0;)
            if (const auto name = tree.getPropertyName (i).toString(); name.startsWith ("ui_") || name.startsWith ("guide_"))
                tree.removeProperty (tree.getPropertyName (i), nullptr);

        if (auto xml = tree.createXml())
            xml->writeTo (file);

        refreshPresets();
        presetBox.setSelectedId (userId + presetFiles.indexOf (file), juce::dontSendNotification);
    });
}

void TourbillonEditor::loadPreset (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    for (int i = 0; i < apvts.state.getNumProperties(); ++i)    // keep this session's window and Guide choices
        if (const auto name = apvts.state.getPropertyName (i); name.toString().startsWith ("ui_") || name.toString().startsWith ("guide_"))
            tree.setProperty (name, apvts.state.getProperty (name), nullptr);

    proc.loadState (tree);
}

//==============================================================================
Knob* TourbillonEditor::knob (Section& s, const char* paramId, const char* caption)
{
    auto* k = new Knob (apvts, paramId, caption, s.accent);
    owned.add (k);
    s.addCell (k);
    return k;
}

juce::ComboBox* TourbillonEditor::combo (Section& s, const char* paramId, const char* caption, int width)
{
    auto box = makeCombo (apvts, paramId, comboAttachments);
    auto* raw = box.get();
    auto* cell = new CaptionCell (std::move (box), caption, width, 24);
    owned.add (cell);
    s.addCell (cell);
    return raw;
}

void TourbillonEditor::headerButton (Section& s, const char* paramId, const char* caption, int width)
{
    auto* b = new LedButton (apvts, paramId, caption, s.accent);
    owned.add (b);
    s.addHeader (b, width);
}

Readout* TourbillonEditor::readout (Section& s, const char* caption)
{
    auto* r = new Readout (caption, s.accent);
    owned.add (r);
    s.addCell (r);
    return r;
}

//==============================================================================
void TourbillonEditor::updateHover (const juce::MouseEvent& e)
{
    if (e.eventComponent == &meterPanel)
        guidePanel.setHovered (meterPanel.helpIdAt (e.getPosition()));
    else
        guidePanel.setHovered (findHelpId (e.eventComponent));
}

void TourbillonEditor::mouseEnter (const juce::MouseEvent& e)  { updateHover (e); }
void TourbillonEditor::mouseMove (const juce::MouseEvent& e)   { updateHover (e); }

void TourbillonEditor::mouseExit (const juce::MouseEvent& e)
{
    if (e.eventComponent == &meterPanel || findHelpId (e.eventComponent).isNotEmpty())
        guidePanel.setHovered ({});
}

//==============================================================================
/** Each type gives the shared knobs its own captions, and greys out the ones it does not use. */
void TourbillonEditor::showType (int type, int voice)
{
    shownType = type;
    shownVoice = voice;

    for (int v = 0; v < 3; ++v)
        voiceBox->changeItemText (v + 1, voiceNames[type][v]);
    voiceBox->setText (voiceNames[type][voice], juce::dontSendNotification);

    const auto& cap = guide::captions[type];
    auto caption = [] (Knob* k, const char* text, const char* fallback, bool used)
    {
        k->setCaption (text[0] != 0 ? text : fallback);
        k->setAlpha (used && text[0] != 0 ? 1.0f : 0.3f);
    };

    const bool ensemble = type == chorus && voice == 2, dimension = type == chorus && voice == 1;
    const char* characterCaption = cap.character;
    if (type == tremolo) characterCaption = voice == 1 ? "LAG" : (voice == 2 ? "CROSSOVER" : "DRIVE");
    if (type == vibrato && voice == 2) characterCaption = "WOW";

    caption (depthKnob,     cap.depth,    "DEPTH",    true);
    caption (manualKnob,    cap.manual,   "MANUAL",   true);
    caption (feedbackKnob,  cap.feedback, "FEEDBACK", ! ensemble);
    caption (characterKnob, characterCaption, "CHARACTER", ! (type == flanger && voice != 0) && ! (type == vibrato && voice == 1));
    caption (spreadKnob,    type == rotary ? "MIC ANGLE" : "STEREO PHASE", "", ! dimension);
    caption (envKnob,       "ENVELOPE", "", type != rotary);

    rateCell->setAlpha (type == rotary ? 0.3f : 1.0f);
    shapeBox->setEnabled (type != rotary && ! ensemble);
    shapeBox->getParentComponent()->setAlpha (type != rotary && ! ensemble ? 1.0f : 0.3f);

    content.circuitText = juce::StringArray::fromTokens (circuitPaths[type][voice], "|", "")
                              .joinIntoString (juce::String::fromUTF8 (" \xe2\x80\xba "));
    content.circuitText = "IN" + juce::String::fromUTF8 (" \xe2\x80\xba ") + content.circuitText + juce::String::fromUTF8 (" \xe2\x80\xba ") + "MIX" + juce::String::fromUTF8 (" \xe2\x80\xba ") + "OUT";
    content.repaint();
    guidePanel.setType (type, voice);
}

void TourbillonEditor::layoutRows()
{
    auto layoutRow = [] (juce::Rectangle<int> area, std::initializer_list<Section*> all)
    {
        int cols = 0;
        for (auto* s : all) cols += s->preferredCols();

        const float fixed = (float) ((int) all.size() * 2 * Section::pad + ((int) all.size() - 1) * gap);
        const float cellW = ((float) area.getWidth() - fixed) / (float) cols;
        float x = (float) area.getX();

        for (auto* s : all)
        {
            const float w = cellW * (float) s->preferredCols() + 2.0f * (float) Section::pad;
            s->setBounds (juce::roundToInt (x), area.getY(), juce::roundToInt (x + w) - juce::roundToInt (x), area.getHeight());
            x += w + (float) gap;
        }
    };

    auto area = juce::Rectangle<int> (baseW, baseH()).reduced (margin);
    area.removeFromTop (headerH);

    layoutRow (area.removeFromTop (rowH), { &input, &circuit, &output });
    area.removeFromTop (gap);

    auto rowB = area.removeFromTop (rowH);
    meterPanel.setBounds (rowB.removeFromRight (meterW));
    rowB.removeFromRight (gap);
    motionPanel.setBounds (rowB);

    guidePanel.setVisible (showGuide);
    if (showGuide)
    {
        area.removeFromTop (gap);
        guidePanel.setBounds (area.removeFromTop (guideH));
    }
}

void TourbillonEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseW));
    content.setBounds (0, 0, baseW, baseH());
    apvts.state.setProperty ("ui_width", getWidth(), nullptr);

    presetBox.setBounds (276, 17, 160, 24);
    typeButtons->setBounds (448, 10, 516, 38);

    int x = baseW - margin;
    auto place = [&x] (juce::Component& c, int w, int gapAfter = 6) { x -= w; c.setBounds (x, 17, w, 24); x -= gapAfter; };
    place (*bypassButton, 70);
    place (guideButton, 54, 12);
    place (bButton, 26, 2);
    place (aButton, 26, 12);
    place (redoButton, 44, 2);
    place (undoButton, 44);
    layoutRows();
}

void TourbillonEditor::paint (juce::Graphics& g)   { g.fillAll (col::faceplate); }

void TourbillonEditor::Content::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff18241d), 0.0f, 0.0f, juce::Colour (0xff09100c), 0.0f, r.getBottom(), false));
    g.fillAll();

    g.setColour (juce::Colours::white.withAlpha (0.012f));
    for (int y = 0; y < getHeight(); y += 3)
        g.drawHorizontalLine (y, 0.0f, r.getRight());

    g.setColour (col::cream);
    g.setFont (font (26.0f, true, 0.26f));
    g.drawText ("TOURBILLON", margin + 6, 12, 260, 34, juce::Justification::centredLeft, false);
    g.setColour (col::gold);
    g.setFont (font (10.0f, true, 0.34f));
    g.drawText ("BOUTIQUE MODULATION", margin + 8, 46, 260, 14, juce::Justification::centredLeft, false);

    // signal path, under the type selector
    g.setColour (col::creamDim);
    g.setFont (font (8.5f, true, 0.06f));
    g.drawFittedText (circuitText, 448, 52, 516, 12, juce::Justification::centred, 1, 0.7f);

    for (auto p : { juce::Point<float> (7.0f, 7.0f), { r.getRight() - 7.0f, 7.0f }, { 7.0f, r.getBottom() - 7.0f }, { r.getRight() - 7.0f, r.getBottom() - 7.0f } })
    {
        g.setColour (juce::Colour (0xff3d5a48));
        g.fillEllipse (p.x - 3.5f, p.y - 3.5f, 7.0f, 7.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (p.x - 2.2f, p.y - 2.2f, p.x + 2.2f, p.y + 2.2f, 1.0f);
    }
}

//==============================================================================
void TourbillonEditor::timerCallback()
{
    auto& m = proc.meters;
    auto take = [] (std::atomic<float>& a) { return a.exchange (0.0f, std::memory_order_relaxed); };
    auto fall = [] (float& disp, float target, float rate) { disp = target > disp ? target : juce::jmax (target, disp - rate); };
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    for (int ch = 0; ch < 2; ++ch)
    {
        fall (inHold[ch],  toDb (take (m.inPeak[ch])),  1.6f);
        fall (outHold[ch], toDb (take (m.outPeak[ch])), 1.6f);
        meterPanel.inDb[ch] = inHold[ch];
        meterPanel.outDb[ch] = outHold[ch];
    }

    const float outPk = juce::jmax (outHold[0], outHold[1]);
    if (outPk >= peakHold || ++peakHoldTicks > 60) { peakHold = outPk; peakHoldTicks = 0; }
    peakRead->set (peakHold < -90.0f ? juce::String ("-inf") : juce::String (peakHold, 1));

    fall (wetHold, toDb (take (m.wetPeak)), 1.2f);
    fall (keyHold, toDb (take (m.keyPeak)), 1.6f);

    // ---- controls that change with the settings ----
    const bool nowSynced = raw ("sync") > 0.5f;
    if (! syncKnown || nowSynced != synced)
    {
        syncKnown = true; synced = nowSynced;
        rateCell->setSynced (synced);
    }

    const int type = juce::jlimit (0, numTypes - 1, (int) raw ("type"));
    const int voice = juce::jlimit (0, 2, (int) raw ("voice"));
    if (type != shownType || voice != shownVoice)
        showType (type, voice);

    const double hz = type == rotary ? (double) m.hornHz.load (std::memory_order_relaxed) : proc.lfoRateHz();
    rateRead->set (juce::String (hz, hz < 1.0 ? 2 : 1));

    const float hostBpm = m.hostBpm.load (std::memory_order_relaxed);
    tempoKnob->setAlpha (hostBpm > 1.0f ? 0.35f : 1.0f);        // the host's tempo wins when there is one
    tapButton.setAlpha (hostBpm > 1.0f ? 0.35f : 1.0f);

    // ---- meters ----
    const float vuTarget = juce::jlimit (-40.0f, 6.0f, toDb (m.vuRms.load (std::memory_order_relaxed)) + 3.0f + 18.0f);
    vuDisp += 0.3f * (vuTarget - vuDisp);
    meterPanel.vuDb = vuDisp;
    meterPanel.lufsS = m.lufsS.load (std::memory_order_relaxed);
    meterPanel.lufsM = m.lufsM.load (std::memory_order_relaxed);
    meterPanel.repaint();
    motionPanel.update();

    if (++ticks % 6 == 0)
        guidePanel.refreshValue();

    // group edits into undo steps: a new step begins whenever the mouse is released
    if (ticks % 10 == 0 && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        proc.undo.beginNewTransaction();

    if (ticks % 12 == 0)
    {
        Coach::Readings r;
        r.inPeakDb = juce::jmax (inHold[0], inHold[1]);
        r.outPeakDb = peakHold;
        r.vu = vuDisp;
        r.wetPeakDb = wetHold;
        r.keyPeakDb = keyHold;
        r.hostBpm = hostBpm;
        r.rateHz = (float) hz;
        r.env = m.env.load (std::memory_order_relaxed);
        r.keyConnected = m.keyConnected.load (std::memory_order_relaxed);
        guidePanel.setCoach (Coach::advise (apvts, r));
    }
}
