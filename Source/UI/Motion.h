#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"

#include <complex>

namespace tourbillon::ui
{
/** A picture of what the circuit is doing right now.

    Left: the LFO's path for both sides over two cycles, the present moment marked, drawn from
    the same function the audio uses (so even S&H and Drift show the values you will hear).

    Right: what that does to the sound. For the chorus, flanger, phaser and vibe, the frequency
    response of the dry and wet signals together, at this instant. For the tremolo, the level of
    each side; for the vibrato, the pitch of each side; for the rotary speaker, the cabinet seen
    from above, with the rotors and the microphones. */
class MotionPanel final : public juce::Component
{
public:
    explicit MotionPanel (TourbillonProcessor& p) : proc (p), state (p.apvts)
    {
        setHelpId (*this, "motion");
    }

    void update()   { repaint(); }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds().toFloat());
        const int type = juce::jlimit (0, numTypes - 1, (int) raw ("type"));
        const int voice = juce::jlimit (0, 2, (int) raw ("voice"));

        auto area = getLocalBounds().reduced (12, 0);
        auto titleRow = area.removeFromTop (28);
        g.setColour (col::cream);
        g.setFont (font (12.5f, true, 0.16f));
        g.drawText ("MOTION", titleRow, juce::Justification::centredLeft, false);
        g.setColour (col::gold);
        g.setFont (font (10.0f, true, 0.12f));
        g.drawText (juce::String (typeNames[type]).toUpperCase() + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (voiceNames[type][voice]).toUpperCase(),
                    titleRow, juce::Justification::centredRight, false);
        g.setColour (col::teal.withAlpha (0.95f));
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), 26.5f, (float) area.getWidth(), 1.5f));

        area.removeFromTop (8);
        area.removeFromBottom (12);
        auto left = area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.38f));
        area.removeFromLeft (12);

        if (type == rotary) paintRotorSpeeds (g, left.toFloat());
        else                paintLfo (g, left.toFloat(), type, voice);

        switch (type)
        {
            case tremolo:   paintLevels (g, area.toFloat()); break;
            case vibrato:   paintPitch (g, area.toFloat()); break;
            case rotary:    paintCabinet (g, area.toFloat()); break;
            default:        paintResponse (g, area.toFloat(), type, voice); break;
        }
    }

private:
    float raw (const char* id) const    { return state.getRawParameterValue (id)->load(); }

    static void plotBox (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (col::well);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::wellEdge.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    static void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (col::creamDim.withAlpha (0.85f));
        g.setFont (font (8.5f, true, 0.06f));
        g.drawText (text, r, j, false);
    }

    //==========================================================================
    void paintLfo (juce::Graphics& g, juce::Rectangle<float> r, int type, int voice)
    {
        plotBox (g, r);
        auto plot = r.reduced (8.0f, 10.0f).withTrimmedBottom (12.0f);
        const int shape = type == chorus && voice == 2 ? (int) sine : juce::jlimit (0, numShapes - 1, (int) raw ("shape"));
        const double now = proc.meters.lfoPosition.load (std::memory_order_relaxed);
        const double offset = (double) raw ("spread") / 360.0;
        const double from = now - 1.5, span = 2.0;

        g.setColour (col::wellEdge.withAlpha (0.35f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
        for (int c = (int) std::ceil (from); (double) c <= from + span; ++c)
        {
            const float x = plot.getX() + plot.getWidth() * (float) (((double) c - from) / span);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }

        auto trace = [&] (double shift, juce::Colour c)
        {
            juce::Path p;
            const int steps = juce::roundToInt (plot.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const double pos = from + span * (double) i / (double) steps;
                const float v = Lfo::valueAt (shape, pos + shift);
                const float x = plot.getX() + (float) i, y = plot.getCentreY() - 0.45f * plot.getHeight() * v;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.6f));
        };
        trace (offset, col::teal.withAlpha (0.85f));
        trace (0.0, col::gold);

        const float xNow = plot.getX() + plot.getWidth() * 0.75f;
        g.setColour (col::cream.withAlpha (0.7f));
        g.drawVerticalLine (juce::roundToInt (xNow), plot.getY() - 4.0f, plot.getBottom() + 4.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float v = Lfo::valueAt (shape, now + (ch == 1 ? offset : 0.0));
            g.setColour (ch == 0 ? col::gold : col::teal);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ xNow, plot.getCentreY() - 0.45f * plot.getHeight() * v }));
        }

        // legend, rate and the envelope
        auto foot = r.reduced (8.0f, 4.0f).removeFromBottom (12.0f);
        const double hz = proc.lfoRateHz();
        label (g, "LFO  " + juce::String (hz, hz < 1.0 ? 2 : 1) + " Hz", foot);
        g.setColour (col::gold);   g.fillRect (foot.getCentreX() - 30.0f, foot.getCentreY() - 1.0f, 10.0f, 2.0f);
        label (g, "L", foot.withX (foot.getCentreX() - 17.0f));
        g.setColour (col::teal);   g.fillRect (foot.getCentreX() + 2.0f, foot.getCentreY() - 1.0f, 10.0f, 2.0f);
        label (g, "R", foot.withX (foot.getCentreX() + 15.0f));

        const float env = proc.meters.env.load (std::memory_order_relaxed);
        auto bar = foot.removeFromRight (70.0f);
        label (g, "ENV", bar.removeFromLeft (24.0f));
        bar = bar.withSizeKeepingCentre (bar.getWidth(), 5.0f);
        g.setColour (col::ledOff);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (std::abs (raw ("envAmt")) > 0.5f ? col::green : col::creamDim.withAlpha (0.5f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, env)), 2.0f);
    }

    //==========================================================================
    /** Response of dry and wet together, per side, from the circuit's state right now. */
    void paintResponse (juce::Graphics& g, juce::Rectangle<float> r, int type, int voice)
    {
        plotBox (g, r);
        auto plot = r.reduced (8.0f, 10.0f).withTrimmedBottom (12.0f);
        const double rate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        constexpr float topDb = 12.0f, bottomDb = -30.0f;
        auto xFor = [&plot] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
        auto yFor = [&plot] (float db) { return plot.getY() + plot.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };

        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            g.setColour (col::wellEdge.withAlpha (0.4f));
            g.drawVerticalLine (juce::roundToInt (xFor (f)), plot.getY(), plot.getBottom());
            label (g, f < 1000.0 ? "100" : (f < 5000.0 ? "1k" : "10k"), { xFor (f) + 3.0f, plot.getBottom() - 11.0f, 30.0f, 10.0f });
        }
        for (float db : { 0.0f, -12.0f, -24.0f })
        {
            g.setColour (col::wellEdge.withAlpha (db == 0.0f ? 0.6f : 0.3f));
            g.drawHorizontalLine (juce::roundToInt (yFor (db)), plot.getX(), plot.getRight());
            label (g, juce::String ((int) db), { plot.getRight() - 24.0f, yFor (db) - 11.0f, 22.0f, 10.0f }, juce::Justification::centredRight);
        }

        // the same mix law the processor uses
        const float mix = 0.01f * raw ("mix");
        const bool wetOnly = raw ("wetOnly") > 0.5f;
        float dg = wetOnly ? 0.0f : juce::jmin (1.0f, 2.0f * (1.0f - mix)), wg = wetOnly ? 1.0f : juce::jmin (1.0f, 2.0f * mix);
        const float norm = 1.0f / juce::jmax (1.0e-3f, dg + wg);
        dg *= norm; wg *= norm;
        const float fb = 0.01f * raw ("feedback");

        using C = std::complex<double>;
        auto wetAt = [&] (int ch, double w) -> C
        {
            const C z1 = std::exp (C (0.0, -w));
            if (type == chorus || type == flanger)
            {
                const double d = (double) proc.meters.delayMs[ch].load (std::memory_order_relaxed) * 0.001 * rate;
                const C del = std::exp (C (0.0, -w * d));
                if (type == flanger && voice == 1)
                {
                    const C ref = std::exp (C (0.0, -w * (double) Modulator::tzfReferenceMs * 0.001 * rate));
                    return 0.5 * (ref - del) / (1.0 - (double) fb * del);
                }
                const double loop = type == chorus ? 0.6 * (double) fb : (double) fb;
                return voice == 1 && type == chorus ? del : del / (1.0 - loop * del);
            }
            if (type == phaser)
            {
                const double hz = juce::jmax (1.0, (double) proc.meters.breakHz[ch].load (std::memory_order_relaxed));
                const C ap = std::pow (PhaserStage::response (hz, analogOmega (w, rate)), Modulator::phaserStages (voice));
                return ap / (1.0 - (double) fb * ap * z1);
            }
            // vibe: four stages with their own capacitors, gains and photocells, lit as they are now
            const double cell = (double) proc.meters.cell[ch].load (std::memory_order_relaxed);
            C h (Modulator::vibeTrim);
            for (int st = 0; st < 4; ++st)
                h *= VibeStage::response (st, VibeStage::ldr (st, voice, cell), analogOmega (w, rate));
            return h;
        };

        for (int ch = 2; --ch >= 0;)
        {
            juce::Path p;
            const int steps = juce::roundToInt (plot.getWidth() / 2.0f);
            for (int i = 0; i <= steps; ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, (double) i / (double) steps);
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / rate;
                const C h = (double) dg + (double) wg * wetAt (ch, w);
                const float db = 20.0f * std::log10 ((float) juce::jmax (1.0e-6, std::abs (h)));
                const float x = xFor (hz), y = yFor (db);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (ch == 0 ? col::gold : col::teal.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (ch == 0 ? 1.8f : 1.3f));
        }

        auto foot = r.reduced (8.0f, 4.0f).removeFromBottom (12.0f);
        juce::String text;
        if (type == chorus || type == flanger)
            text << "DELAY  " << juce::String (proc.meters.delayMs[0].load(), 2) << " / " << juce::String (proc.meters.delayMs[1].load(), 2) << " ms";
        else if (type == phaser)
            text << "STAGES AT  " << format::hz (proc.meters.breakHz[0].load(), 0) << " / " << format::hz (proc.meters.breakHz[1].load(), 0);
        else
            text << "PHOTOCELLS  " << juce::String (proc.meters.breakHz[0].load() / 1000.0f, 0) << " / " << juce::String (proc.meters.breakHz[1].load() / 1000.0f, 0) << " k" << juce::String::fromUTF8 ("\xce\xa9");
        label (g, text, foot);
        label (g, "RESPONSE, DRY + WET", foot, juce::Justification::centredRight);
    }

    //==========================================================================
    void paintLevels (juce::Graphics& g, juce::Rectangle<float> r)
    {
        plotBox (g, r);
        auto inner = r.reduced (16.0f, 14.0f).withTrimmedBottom (10.0f);
        const float rowH = inner.getHeight() / 2.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto row = inner.removeFromTop (rowH).reduced (0.0f, 10.0f);
            label (g, ch == 0 ? "LEFT" : "RIGHT", row.removeFromLeft (44.0f));
            const float gain = juce::jlimit (0.0f, 1.2f, proc.meters.position[ch].load (std::memory_order_relaxed));
            g.setColour (col::ledOff);
            g.fillRoundedRectangle (row, 3.0f);
            g.setColour ((ch == 0 ? col::gold : col::teal).withAlpha (0.9f));
            g.fillRoundedRectangle (row.withWidth (row.getWidth() * juce::jmin (1.0f, gain)), 3.0f);
            label (g, juce::String (juce::Decibels::gainToDecibels (gain, -60.0f), 1) + " dB", row.reduced (8.0f, 0.0f), juce::Justification::centredRight);
        }
        label (g, "LEVEL OF EACH SIDE", r.reduced (8.0f, 4.0f).removeFromBottom (12.0f), juce::Justification::centredRight);
    }

    void paintPitch (juce::Graphics& g, juce::Rectangle<float> r)
    {
        plotBox (g, r);
        auto inner = r.reduced (16.0f, 14.0f).withTrimmedBottom (10.0f);
        const float rowH = inner.getHeight() / 2.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto row = inner.removeFromTop (rowH).reduced (0.0f, 8.0f);
            label (g, ch == 0 ? "LEFT" : "RIGHT", row.removeFromLeft (44.0f));
            g.setColour (col::ledOff);
            g.fillRoundedRectangle (row, 3.0f);
            for (int c = -100; c <= 100; c += 25)
            {
                const float x = row.getX() + row.getWidth() * (0.5f + (float) c / 200.0f);
                g.setColour (col::wellEdge.withAlpha (c == 0 ? 0.9f : 0.4f));
                g.drawVerticalLine (juce::roundToInt (x), row.getY(), row.getBottom());
            }
            const float cents = juce::jlimit (-100.0f, 100.0f, proc.meters.position[ch].load (std::memory_order_relaxed));
            const float x = row.getX() + row.getWidth() * (0.5f + cents / 200.0f);
            g.setColour (ch == 0 ? col::gold : col::teal);
            g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, row.getHeight() + 6.0f).withCentre ({ x, row.getCentreY() }), 2.0f);
        }
        auto foot = r.reduced (8.0f, 4.0f).removeFromBottom (12.0f);
        label (g, "-100 CENTS", foot);
        label (g, "PITCH OF EACH SIDE", foot, juce::Justification::centred);
        label (g, "+100 CENTS", foot, juce::Justification::centredRight);
    }

    //==========================================================================
    void paintRotorSpeeds (juce::Graphics& g, juce::Rectangle<float> r)
    {
        plotBox (g, r);
        auto inner = r.reduced (16.0f, 14.0f).withTrimmedBottom (10.0f);
        const float rowH = inner.getHeight() / 2.0f;
        const int voice = juce::jlimit (0, 2, (int) raw ("voice"));
        struct Rotor { const char* name; float hz, slow, fast; };
        const Rotor rotors[2] = { { "HORN", proc.meters.hornHz.load(), 0.8f, 6.8f }, { "DRUM", proc.meters.drumHz.load(), 0.66f, 5.9f } };
        for (int i = 0; i < 2; ++i)
        {
            auto row = inner.removeFromTop (rowH).reduced (0.0f, 10.0f);
            label (g, rotors[i].name, row.removeFromLeft (40.0f));
            g.setColour (col::ledOff);
            g.fillRoundedRectangle (row, 3.0f);
            const float full = 7.5f;
            g.setColour ((i == 0 ? col::gold : col::teal).withAlpha (0.9f));
            g.fillRoundedRectangle (row.withWidth (row.getWidth() * juce::jlimit (0.0f, 1.0f, rotors[i].hz / full)), 3.0f);
            const float target = voice == 1 ? rotors[i].fast : (voice == 2 ? 0.0f : rotors[i].slow);
            g.setColour (col::cream);
            g.drawVerticalLine (juce::roundToInt (row.getX() + row.getWidth() * target / full), row.getY() - 3.0f, row.getBottom() + 3.0f);
            label (g, juce::String (rotors[i].hz, 2) + " Hz", row.reduced (8.0f, 0.0f), juce::Justification::centredRight);
        }
        label (g, "ROTOR SPEED  (LINE: WHERE IT IS HEADING)", r.reduced (8.0f, 4.0f).removeFromBottom (12.0f));
    }

    void paintCabinet (juce::Graphics& g, juce::Rectangle<float> r)
    {
        plotBox (g, r);
        auto box = r.reduced (10.0f).withTrimmedBottom (8.0f);
        const float size = juce::jmin (box.getWidth(), box.getHeight()) * 0.9f;
        auto cab = juce::Rectangle<float> (size, size).withCentre (box.getCentre());
        const auto c = cab.getCentre();

        g.setColour (col::panelHi.withAlpha (0.6f));
        g.drawRoundedRectangle (cab.reduced (size * 0.12f), 6.0f, 1.2f);

        const float hornA = proc.meters.hornAngle.load (std::memory_order_relaxed);
        const float drumA = proc.meters.drumAngle.load (std::memory_order_relaxed);
        const float rd = size * 0.3f, rh = size * 0.26f;

        // drum: a baffle that throws sound one way
        g.setColour (col::teal.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * rd, 2.0f * rd).withCentre (c));
        juce::Path wedge;
        wedge.addPieSegment (juce::Rectangle<float> (2.0f * rd, 2.0f * rd).withCentre (c), drumA - 0.5f, drumA + 0.5f, 0.0f);
        g.setColour (col::teal.withAlpha (0.8f));
        g.fillPath (wedge, juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, c.x, c.y));

        // horn: a double-ended horn, one mouth live
        const juce::Point<float> tip (c.x + rh * std::cos (hornA), c.y + rh * std::sin (hornA));
        const juce::Point<float> tail (c.x - rh * std::cos (hornA), c.y - rh * std::sin (hornA));
        g.setColour (col::gold.withAlpha (0.4f));
        g.drawLine ({ c, tail }, 3.0f);
        g.setColour (col::gold);
        g.drawLine ({ c, tip }, 4.0f);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (tip));

        // the microphones
        const float half = 0.5f * raw ("spread") * juce::MathConstants<float>::pi / 180.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float a = ch == 0 ? -half : half;
            const juce::Point<float> m (c.x + size * 0.47f * std::cos (a), c.y + size * 0.47f * std::sin (a));
            g.setColour (col::cream);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (m));
            label (g, ch == 0 ? "L" : "R", juce::Rectangle<float> (14.0f, 10.0f).withCentre (m.translated (12.0f, 0.0f)));
        }
        label (g, "CABINET FROM ABOVE", r.reduced (8.0f, 4.0f).removeFromBottom (12.0f), juce::Justification::centredRight);
    }

    TourbillonProcessor& proc;
    APVTS& state;
};
} // namespace tourbillon::ui
