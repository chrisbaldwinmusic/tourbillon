// Runs Tourbillon's BucketBrigade on the test signal that check_bbd.py also feeds to ngspice, and prints one output
// sample per line. Built and run by check_bbd.py; see there.
//
//   BbdModel <compander 0|1> <character> <delay samples> <clock limit Hz> <seconds> <signal: tones|bursts|silence>
#include "BucketBrigade.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main (int argc, char** argv)
{
    if (argc != 7) { std::fprintf (stderr, "usage: BbdModel compander character delaySamples clockMax seconds signal\n"); return 1; }

    const double sr = 48000.0, T = 1.0 / sr;
    const bool compander = std::atoi (argv[1]) != 0;
    const float character = (float) std::atof (argv[2]);
    const float delaySamples = (float) std::atof (argv[3]);
    const double clockMax = std::atof (argv[4]);
    const int length = (int) (std::atof (argv[5]) * sr);
    const char* signal = argv[6];

    tourbillon::BbdChip chip;               // as Modulator::prepare fits them: 512 buckets, 10 kHz up to clockMax
    chip.buckets = 512;
    chip.clockMin = 10000.0;
    chip.clockMax = clockMax;
    chip.compander = compander;
    tourbillon::BucketBrigade bbd;
    bbd.setCharacter (character);
    bbd.prepare (sr, 0.06, chip);

    for (int n = 0; n < length; ++n)
    {
        const double t = n * T;
        double x = 0.0;
        if (std::strcmp (signal, "tones") == 0)         // must match TONES in check_bbd.py
            x = 0.003 * (std::sin (2 * tourbillon::kPi * 220.0 * t) + std::sin (2 * tourbillon::kPi * 1700.0 * t)
                         + std::sin (2 * tourbillon::kPi * 3900.0 * t) + std::sin (2 * tourbillon::kPi * 6300.0 * t));
        else if (std::strcmp (signal, "bursts") == 0)   // must match BURSTS in check_bbd.py
            x = (std::fmod (t, 0.2) < 0.1 ? 0.3 : 0.01) * std::sin (2 * tourbillon::kPi * 1000.0 * t);

        std::printf ("%.9g\n", (double) bbd.read (delaySamples));
        bbd.write ((float) x);
    }
    return 0;
}
