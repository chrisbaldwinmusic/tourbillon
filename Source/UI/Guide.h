#pragma once

#include "GuideData.h"
#include "Widgets.h"

namespace tourbillon::ui
{
//==============================================================================
/** Live feedback: looks at the meters and settings and says the single most useful thing. */
struct Coach
{
    enum Level { ok, note, warn };

    struct Advice
    {
        Level level = ok;
        juce::String text { "Play some audio and I'll comment on what the circuit is doing." };
        bool operator!= (const Advice& o) const   { return level != o.level || text != o.text; }
    };

    struct Readings
    {
        float inPeakDb = -100, outPeakDb = -100, vu = -40, wetPeakDb = -100, keyPeakDb = -100;
        float hostBpm = 0, rateHz = 0, env = 0;
        bool keyConnected = false;
    };

    static Advice advise (APVTS& s, const Readings& m)
    {
        auto val = [&s] (const char* id) { return s.getRawParameterValue (id)->load(); };
        auto on  = [&val] (const char* id) { return val (id) > 0.5f; };
        const int type = juce::jlimit (0, numTypes - 1, (int) val ("type"));
        const int voice = juce::jlimit (0, 2, (int) val ("voice"));
        const float mix = val ("mix");

        if (on ("bypass"))
            return { note, "Bypassed. Do you miss the movement, or is the part clearer without it? Match levels with Output Trim before deciding." };
        if (m.inPeakDb < -80.0f)
            return { ok, "Waiting for audio. Play the track and I'll comment on what the circuit is doing." };

        if (on ("envExt") && std::abs (val ("envAmt")) > 0.5f)
        {
            if (! m.keyConnected)
                return { warn, "Envelope Ext Key is on, but the host has not connected a sidechain input, so the envelope is following this track. Route a key track to the plugin's sidechain." };
            if (m.keyPeakDb < -70.0f)
                return { note, "Envelope Ext Key is on but nothing is arriving at the sidechain input yet. Check the key track is routed and playing." };
        }

        if (m.outPeakDb > 0.0f)
            return { warn, "Output is peaking at +" + juce::String (m.outPeakDb, 1) + " dBFS. Pull Output Trim down." };

        const bool wetType = type == tremolo || type == vibrato || type == rotary;
        if (wetType && ! on ("wetOnly") && mix < 80.0f)
            return { note, juce::String (typeNames[type]) + " is usually heard on its own, at Mix 100 %. At "
                           + juce::String (juce::roundToInt (mix)) + " % the dry signal fills in the movement"
                           + (type == vibrato ? ", which turns the vibrato into a chorus." : ".") };
        if (! wetType && (type != flanger || voice != 1) && mix > 85.0f && ! on ("wetOnly"))
            return { note, juce::String (typeNames[type]) + " needs the dry signal to make its effect. At Mix 100 % you hear only the wet side"
                           + (type == phaser || type == vibe ? ", which is a gentle vibrato. Try 50 %." : ", which is a vibrato. Try 50 %.") };
        if (type == flanger && voice == 1 && mix < 90.0f && ! on ("wetOnly"))
            return { note, "Through-Zero makes its own reference copy, so it is complete on its own. Set Mix to 100 % for the full cancellation as the sweep passes zero." };

        if (on ("sync") && m.hostBpm < 1.0f && type != rotary)
            return { note, "Sync is on but there is no host tempo, so the LFO is following the Tempo knob (" + juce::String (val ("bpm"), 1) + " BPM). Set it to the tempo of the music." };

        if ((type == flanger || type == phaser) && std::abs (val ("feedback")) > 80.0f)
            return { note, "Feedback above 80 % makes the circuit ring and whistle. Great for a moment; for a part that plays all the way through, 30 to 60 % is easier to live with." };

        if (val ("depth") < 1.0f && std::abs (val ("envAmt")) < 1.0f && type != rotary)
            return { note, "Depth and Envelope are both at zero, so nothing is moving: the circuit is a fixed filter or delay. That can be the point; turn Depth up for movement." };

        if (std::abs (val ("envAmt")) > 1.0f && type != rotary && m.env < 0.05f)
            return { note, "Envelope is set, but the signal is too quiet for it to move anything. Raise Input Gain, or the level of the key." };

        if (type == rotary && voice == 2)
            return { note, "Brake: the rotors are stopping. The sound freezes wherever the horn comes to rest; switch to Slow or Fast to start them again." };

        if (val ("width") > 125.0f)
            return { note, "Width is above 125 %. Check the mix in mono; an over-wide effect can thin out or vanish." };

        if (m.vu < -14.0f && (type == chorus || type == flanger || type == vibrato || type == rotary))
            return { note, "Level through the unit is low. Raise Input Gain towards 0 VU; the chips and the rotary's amplifier respond to level." };

        if (type == chorus && val ("wetLo") < 60.0f && m.vu > -10.0f)
            return { note, "Chorus with no Wet Low Cut moves the low end too. On bass, kick-heavy or full-mix material, raise Wet Low Cut to 150 Hz or more." };

        static const char* fine[numTypes] = {
            "Looks healthy. Bypass now and then: a good chorus is missed when it goes, not noticed while it is on.",
            "Looks healthy. Watch the comb sweep in the Motion display; slow the Rate until it rises and falls across a phrase.",
            "Looks healthy. Hover any control to see what it does and what to listen for.",
            "Looks healthy. The lamp's lag gives the vibe its throb; Lamp Lag sets how much.",
            "Looks healthy. Sync the Rate to the song if the pulse fights the groove.",
            "Looks healthy. A little vibrato goes a long way: listen for tuning against the rest of the track.",
            "Looks healthy. Switch between Slow and Fast in time with the song; the rotors take their time, and that swell is the sound." };
        return { ok, fine[type] };
    }
};

//==============================================================================
/** Resets the effect to neutral, then applies a starting point.
    Bypass, input gain, output trim, wet-only and the tempo are left alone. */
inline void applyStartingPoint (juce::AudioProcessor& processor, const guide::Source& source)
{
    static const juce::StringArray untouched { "bypass", "inGain", "outGain", "wetOnly", "bpm" };

    for (auto* base : processor.getParameters())
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (base);
        if (p == nullptr || untouched.contains (p->paramID)) continue;

        float target = p->getDefaultValue();
        for (auto& [id, value] : source.settings)
            if (p->paramID == id) target = p->convertTo0to1 (value);

        if (std::abs (target - p->getValue()) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (target);
            p->endChangeGesture();
        }
    }
}

//==============================================================================
/** The teaching strip along the bottom: choose what you're working on, get tips and a
    starting point for it, and read about whichever control is under the pointer. */
class GuidePanel final : public juce::Component
{
public:
    GuidePanel (juce::AudioProcessor& p, APVTS& s) : processor (p), state (s)
    {
        for (auto& src : guide::sources) sourceBox.addItem (src.name, sourceBox.getNumItems() + 1);
        sourceBox.setSelectedItemIndex (juce::jlimit (0, (int) guide::sources.size() - 1, (int) state.state.getProperty ("guide_source", 0)), juce::dontSendNotification);
        sourceBox.onChange = [this] { state.state.setProperty ("guide_source", sourceBox.getSelectedItemIndex(), nullptr); repaint(); };

        for (auto& st : guide::styles) styleBox.addItem (st.name, styleBox.getNumItems() + 1);
        styleBox.setSelectedItemIndex (juce::jlimit (0, (int) guide::styles.size() - 1, (int) state.state.getProperty ("guide_style", 0)), juce::dontSendNotification);
        styleBox.onChange = [this] { state.state.setProperty ("guide_style", styleBox.getSelectedItemIndex(), nullptr); repaint(); };

        apply.onClick = [this] { applyStartingPoint (processor, source()); };
        setHelpId (apply, "guide.apply");

        addAndMakeVisible (sourceBox);
        addAndMakeVisible (styleBox);
        addAndMakeVisible (apply);
    }

    void setType (int t, int v)                     { if (t != type || v != voice) { type = t; voice = v; repaint(); } }
    void setHovered (const juce::String& id)        { if (id != hovered) { hovered = id; repaint(); } }
    void setCoach (const Coach::Advice& a)          { if (coach != a) { coach = a; repaint(); } }
    void refreshValue()                             { if (hovered.isNotEmpty()) repaint (lessonArea()); }

    void resized() override
    {
        auto left = getLocalBounds().reduced (12, 10).removeFromLeft (leftW - 24);
        left.removeFromTop (44);
        sourceBox.setBounds (left.removeFromTop (24));
        left.removeFromTop (19);
        styleBox.setBounds (left.removeFromTop (24));
        apply.setBounds (left.removeFromBottom (28));
    }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds().toFloat());
        g.setColour (col::wellEdge.withAlpha (0.6f));
        g.fillRect (leftW, 12, 1, getHeight() - 24);
        g.fillRect (getWidth() - rightW, 12, 1, getHeight() - 24);

        // ---- left: what are you working on? ----
        auto left = getLocalBounds().reduced (12, 10).removeFromLeft (leftW - 24);
        g.setColour (col::cream);
        g.setFont (font (12.5f, true, 0.16f));
        g.drawText ("GUIDE", left.removeFromTop (18), juce::Justification::centredLeft, false);
        g.setColour (col::gold);
        g.fillRect (left.removeFromTop (5).withTrimmedTop (3).toFloat().withHeight (1.5f));
        g.setColour (col::creamDim);
        g.setFont (font (9.0f, true, 0.12f));
        g.drawText ("SOURCE", left.removeFromTop (21).withTrimmedTop (6), juce::Justification::centredLeft, false);
        left.removeFromTop (24);
        g.drawText ("STYLE", left.removeFromTop (19).withTrimmedTop (4), juce::Justification::centredLeft, false);

        // ---- middle: tips for this source and style ----
        auto mid = getLocalBounds().reduced (0, 10).withTrimmedLeft (leftW + 14).withTrimmedRight (rightW + 14).toFloat();
        auto& src = source();
        auto& style = guide::styles[(size_t) juce::jlimit (0, (int) guide::styles.size() - 1, styleBox.getSelectedItemIndex())];

        auto head = mid.removeFromTop (20.0f);
        g.setColour (col::gold);
        g.setFont (font (12.5f, true, 0.16f));
        g.drawText (juce::String (src.name).toUpperCase(), head, juce::Justification::centredLeft, false);
        if (styleBox.getSelectedItemIndex() > 0)
        {
            g.setColour (col::creamDim);
            g.setFont (font (9.5f, true, 0.12f));
            g.drawText (juce::String (style.name).toUpperCase(), head, juce::Justification::centredRight, false);
        }
        mid.removeFromTop (3.0f);

        auto bullet = [&] (const char* text, juce::Colour textCol, juce::Colour dot)
        {
            g.setColour (dot);
            g.fillEllipse (mid.getX(), mid.getY() + 5.5f, 4.0f, 4.0f);
            const float h = drawWrapped (g, text, font (11.5f), textCol, mid.withTrimmedLeft (11.0f));
            mid.removeFromTop (h + 4.5f);
        };
        for (auto* tip : src.tips) bullet (tip, col::cream.withAlpha (0.92f), col::gold);
        if (style.note[0] != 0) bullet (style.note, col::gold.brighter (0.15f), col::cream);

        // ---- right: what does this control do? ----
        auto right = lessonArea().toFloat();
        guide::Help help = guide::typeOverview[type];
        juce::String value, title, what;

        if (hovered == "guide.apply")
        {
            applyHelp = "Dials in a sensible starting point for " + juce::String (src.name)
                      + ": the type, voice and every setting. Input Gain, Output Trim, Wet Only and Tempo are left alone.";
            help = { "", "Apply Starting Point", applyHelp.toRawUTF8(),
                     "It is a starting point, not a preset. Set Depth and Mix by ear in the full mix once the character is right." };
        }
        else if (auto* h = guide::findHelp (hovered.toRawUTF8()))
        {
            help = *h;
            if (auto* p = state.getParameter (hovered); p != nullptr && hovered != "type")
                value = dynamic_cast<juce::AudioParameterBool*> (p) != nullptr ? (p->getValue() > 0.5f ? "ON" : "OFF")
                                                                                 : p->getCurrentValueAsText();
            if (hovered == "voice") value = voiceNames[type][voice];
        }

        // Depth, Manual, Feedback and Character change their job with the type
        title = help.title;
        what = help.what;
        auto perType = [&] (const char* caption, const char* text)
        {
            if (caption[0] != 0) title = juce::String (caption).toLowerCase().replaceSection (0, 1, juce::String (caption).substring (0, 1));
            if (text[0] != 0) what = text;
            else              what = "Not used by the " + juce::String (typeNames[type]).toLowerCase() + ".";
        };
        const auto& cap = guide::captions[type];
        const auto& th = guide::typeHelp[type];
        if (hovered == "depth")     perType (cap.depth, th.depth);
        if (hovered == "manual")    perType (cap.manual, th.manual);
        if (hovered == "feedback")  perType (cap.feedback, th.feedback);
        if (hovered == "character") perType (cap.character, th.character);

        auto titleRow = right.removeFromTop (18.0f);
        g.setColour (col::gold);
        g.setFont (font (11.5f, true));
        g.drawText (value, titleRow, juce::Justification::centredRight, false);
        g.setColour (col::cream);
        g.setFont (font (11.5f, true, 0.12f));
        g.drawText (title.toUpperCase(), titleRow.withTrimmedRight (value.isEmpty() ? 0.0f : 90.0f), juce::Justification::centredLeft, true);
        right.removeFromTop (3.0f);

        auto coachBox = right.removeFromBottom (48.0f);
        right.removeFromBottom (4.0f);
        right.removeFromTop (drawWrapped (g, what, font (11.5f), col::cream.withAlpha (0.9f), right) + 5.0f);
        drawWrapped (g, help.tip, font (11.5f).italicised(), col::gold.brighter (0.15f), right);

        // ---- live coach ----
        const auto level = coach.level == Coach::warn ? col::red : (coach.level == Coach::note ? col::gold : col::green);
        g.setColour (col::well);
        g.fillRoundedRectangle (coachBox, 4.0f);
        g.setColour (level.withAlpha (0.6f));
        g.drawRoundedRectangle (coachBox.reduced (0.5f), 4.0f, 1.0f);
        auto inner = coachBox.reduced (7.0f, 6.0f);
        auto tag = inner.removeFromLeft (34.0f).removeFromTop (14.0f);
        g.setColour (level);
        g.fillRoundedRectangle (tag, 2.0f);
        g.setColour (col::ink);
        g.setFont (font (8.5f, true, 0.12f));
        g.drawText ("LIVE", tag, juce::Justification::centred, false);
        drawWrapped (g, coach.text, font (11.0f), col::cream, inner.withTrimmedLeft (8.0f));
    }

private:
    static constexpr int leftW = 214, rightW = 430;

    juce::Rectangle<int> lessonArea() const
    {
        return getLocalBounds().reduced (0, 10).removeFromRight (rightW).reduced (14, 0);
    }

    const guide::Source& source() const
    {
        return guide::sources[(size_t) juce::jlimit (0, (int) guide::sources.size() - 1, sourceBox.getSelectedItemIndex())];
    }

    /** Brass call-to-action button. */
    struct ApplyButton final : juce::Button
    {
        ApplyButton() : juce::Button ("APPLY STARTING POINT") {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const auto base = col::gold.brighter (over ? 0.2f : 0.0f).darker (down ? 0.2f : 0.0f);
            g.setGradientFill (juce::ColourGradient (base.brighter (0.25f), 0.0f, r.getY(), base.darker (0.25f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (col::ink);
            g.setFont (font (10.5f, true, 0.12f));
            g.drawText (getName(), getLocalBounds(), juce::Justification::centred, false);
        }
    };

    juce::AudioProcessor& processor;
    APVTS& state;
    juce::ComboBox sourceBox, styleBox;
    ApplyButton apply;
    juce::String hovered, applyHelp;
    Coach::Advice coach;
    int type = 0, voice = 0;
};
} // namespace tourbillon::ui
