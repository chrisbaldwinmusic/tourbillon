#pragma once

#include "Common.h"

namespace tourbillon
{
//==============================================================================
/** An incandescent lamp shining on cadmium-sulphide photocells: the light source of the vibe and of the opto
    tremolo. Everything it does, it does in time; the photocells are plain resistors to the audio.

    Lamp: a tungsten filament, its temperature theta given relative to its temperature at full voltage (so 1 is
    fully lit and the room is about 0.11). It heats by the electrical power v^2 / R, its resistance rising with
    temperature (R ~ theta^1.2, as tungsten's does), and cools by radiation (~ theta^4). So a cold filament, whose
    resistance is low, heats quickly, and a dim one, which hardly radiates, cools slowly: the lamp is quicker to
    light than to fade. Its light in the band the cells see follows Wien's law, exp (-b (1/theta - 1)), so a
    filament below about half its temperature gives almost none.

    Cell: light frees charge carriers in the cadmium sulphide, and its conductance is proportional to how many
    are free. They disappear slowly on their own and faster by meeting one another, so the cell answers bright
    light quickly, lets go quickly at first, then slower and slower as it darkens; held at a light level, its
    conductance goes as about the square root of the light. (The same physics as Atelier's optical compressor,
    without its slow traps, with speeds for a cell that must follow an LFO.) Carriers are counted relative to
    the number a fully lit lamp holds steady, so n runs from 0 (dark) to about 1.

    The numbers are this project's own, chosen for the speeds vibes and opto tremolos are known for; they are
    not from a datasheet. Both are integrated by the trapezoidal rule, as ngspice does. */
struct LampCell
{
    static constexpr double ambient = 0.11, wien = 9.7;            // room temperature / full; hc / (lambda k T) at 550 nm, 2700 K
    static constexpr double lifetime = 0.25, pairRate = 100.0;     // cell: slow loss (s), loss by meeting (per s at n = 1)
    static constexpr double genRate = 1.0 / lifetime + pairRate;   // so full light holds n = 1
    static constexpr double radiate = 1.0 / (1.0 - ambient * ambient * ambient * ambient);   // so full voltage holds theta = 1

    /** Filament heating rate (per second, times the thermal time) at voltage v (0..1) and temperature th. */
    static double heat (double v, double th) noexcept       { return v * v * std::pow (th, -1.2) - radiate * (th * th * th * th - ambient * ambient * ambient * ambient); }
    static double heatSlope (double v, double th) noexcept  { return -1.2 * v * v * std::pow (th, -2.2) - 4.0 * radiate * th * th * th; }
    /** Light from the filament at temperature th, 1 at full voltage. */
    static double light (double th) noexcept                { return std::exp (-wien * (1.0 / th - 1.0)); }
    /** Carrier rate of change at light l and carriers n. */
    static double carriers (double l, double n) noexcept    { return genRate * l - n / lifetime - pairRate * n * n; }

    void prepare (double sampleRate) noexcept   { h = 1.0 / sampleRate; reset(); }
    void reset() noexcept                       { th = ambient; n = 0.0; v0 = 0.0; }

    /** The filament's thermal time constant (seconds): a bigger, slower lamp has more. */
    void setThermalTime (double seconds) noexcept   { tau = std::max (1.0e-3, seconds); }

    /** Advances lamp and cell by one sample with the lamp's voltage v (0..1 of full). */
    inline void step (double v) noexcept
    {
        v = std::clamp (v, 0.0, 1.5);

        // filament: Newton's method on th1 = th0 + h/2 (f0 + f1)
        const double k = 0.5 * h / tau, th0 = th, f0 = heat (v0, th0), l0 = light (th0);
        double t1 = th0;
        for (int it = 0; it < 20; ++it)
        {
            const double F = t1 - th0 - k * (f0 + heat (v, t1));
            const double step = F / (1.0 - k * heatSlope (v, t1));
            t1 = std::max (0.5 * ambient, t1 - step);
            if (std::abs (step) < 1.0e-13) break;
        }
        th = t1;
        v0 = v;

        // cell: the trapezoidal step is a quadratic in the new carrier count; take its positive root
        const double hh = 0.5 * h, l1 = light (th);
        const double a = hh * pairRate, b = 1.0 + hh / lifetime;
        const double c = n + hh * (carriers (l0, n) + genRate * l1);
        n = std::max (0.0, 2.0 * c / (b + std::sqrt (b * b + 4.0 * a * c)));
    }

    double temperature() const noexcept { return th; }
    /** How far the filament glows, 0 cold .. 1 full. */
    double glow() const noexcept        { return std::clamp ((th - ambient) / (1.0 - ambient), 0.0, 1.0); }
    double cell() const noexcept        { return n; }

    /** A cell's resistance at carrier count n, given its resistance unlit (rMax) and fully lit (rMin). */
    static double resistance (double rMin, double rMax, double cellN) noexcept
    {
        return 1.0 / (1.0 / rMax + (1.0 / rMin - 1.0 / rMax) * std::max (0.0, cellN));
    }

private:
    double h = 1.0 / 48000.0, tau = 0.025;
    double th = ambient, n = 0.0, v0 = 0.0;
};
} // namespace tourbillon
