#pragma once

#include "PluginProcessor.h"
#include "UI/Guide.h"
#include "UI/Motion.h"
#include "UI/Widgets.h"

class TourbillonEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit TourbillonEditor (TourbillonProcessor&);
    ~TourbillonEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // listens to every control so the Guide can explain whatever is under the pointer
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int baseW = 1280, fullH = 904, compactH = 696;   // with / without the Guide strip

private:
    using Section = tourbillon::ui::Section;
    using Knob = tourbillon::ui::Knob;

    /** Everything is laid out at base size inside this and scaled to the window. */
    struct Content final : juce::Component
    {
        void paint (juce::Graphics&) override;
        juce::String circuitText;
    };

    /** The LFO's Rate control: Hz when free-running, a note length when synced. */
    struct RateCell final : juce::Component
    {
        RateCell (tourbillon::ui::APVTS& s, juce::Colour accent)
            : free (s, "rate", "RATE", accent), synced (s, "div", "RATE", accent)
        {
            addChildComponent (free);
            addChildComponent (synced);
        }
        void setSynced (bool on)    { free.setVisible (! on); synced.setVisible (on); }
        void resized() override     { free.setBounds (getLocalBounds()); synced.setBounds (getLocalBounds()); }
        Knob free, synced;
    };

    void timerCallback() override;
    void layoutRows();
    void showType (int type, int voice);
    void updateHover (const juce::MouseEvent&);
    int baseH() const   { return showGuide ? fullH : compactH; }
    void applyWindowSize (int width);
    void setGuideShown (bool shown);

    // presets live as small XML files in ~/Library/Audio/Presets/Channel Audio/Tourbillon
    static juce::File presetFolder();
    void refreshPresets();
    void presetChosen();
    void savePreset();
    void loadPreset (const juce::File&);
    void resetToInit();
    void tapTempo();

    Knob* knob (Section&, const char* paramId, const char* caption);
    juce::ComboBox* combo (Section&, const char* paramId, const char* caption, int width = 96);
    void headerButton (Section&, const char* paramId, const char* caption, int width);
    tourbillon::ui::Readout* readout (Section&, const char* caption);

    TourbillonProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    tourbillon::ui::TourbillonLookAndFeel lnf;
    Content content;

    juce::OwnedArray<juce::Component> owned;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachments;

    Section input, circuit, output;
    tourbillon::ui::MotionPanel motionPanel;
    tourbillon::ui::MeterPanel meterPanel;
    tourbillon::ui::GuidePanel guidePanel;
    RateCell* rateCell = nullptr;
    Knob *tempoKnob = nullptr, *depthKnob = nullptr, *manualKnob = nullptr, *feedbackKnob = nullptr, *characterKnob = nullptr;
    Knob *spreadKnob = nullptr, *envKnob = nullptr;
    juce::ComboBox *voiceBox = nullptr, *shapeBox = nullptr;
    tourbillon::ui::Readout *rateRead = nullptr, *peakRead = nullptr;
    std::unique_ptr<tourbillon::ui::ChoiceButtons> typeButtons;
    std::unique_ptr<tourbillon::ui::LedButton> bypassButton;
    tourbillon::ui::TextToggle guideButton { "GUIDE", tourbillon::ui::col::gold };
    tourbillon::ui::TextToggle aButton { "A", tourbillon::ui::col::ivory }, bButton { "B", tourbillon::ui::col::ivory };
    tourbillon::ui::TextToggle undoButton { "UNDO", tourbillon::ui::col::ivory }, redoButton { "REDO", tourbillon::ui::col::ivory };
    tourbillon::ui::TextToggle tapButton { "TAP", tourbillon::ui::col::ivory };
    juce::Array<double> taps;       // times of the recent tap-tempo clicks, in milliseconds
    juce::ComboBox presetBox;
    juce::Array<juce::File> presetFiles;
    std::unique_ptr<juce::FileChooser> chooser;

    bool showGuide = true;
    int ticks = 0, shownType = -1, shownVoice = -1;
    bool synced = false, syncKnown = false;
    float inHold[2] { -100.0f, -100.0f }, outHold[2] { -100.0f, -100.0f };
    float vuDisp = -40.0f, wetHold = -100.0f, keyHold = -100.0f;
    float peakHold = -100.0f;
    int peakHoldTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TourbillonEditor)
};
