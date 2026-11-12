#!/usr/bin/env python3
"""Checks Tourbillon's bucket-brigade model (Source/DSP/BucketBrigade.h, shared with Reverie) against the same
signal path built as a circuit and solved by ngspice, with the chips Tourbillon fits: one 512-bucket chip and no
compander, clocked up to 200 kHz for the chorus and vibrato and up to 400 kHz for the flanger. Adapted from
Reverie's Tests/spice/check_bbd.py; the circuit and the method are the same.

    python3 Tests/spice/check_bbd.py            (needs clang++ and ngspice; looks for ngspice on the PATH and
                                                 in ~/Library/Caches/ngspice-*)

The circuit:

    (NE570 compressor, not fitted here) > 4th-order Butterworth anti-aliasing filter (two unity-gain Sallen-Key stages)
      > chip clipping > clocked sample-and-hold > N clock periods of delay (three taps for the charge-transfer
      smear) > the same Butterworth as reconstruction filter > (NE570 expander, not fitted) > output op-amp clipping

The filters, the sampling switch and the delay are real circuit elements solved in continuous time, driven by a
real clock, with an analog signal source; nothing is sampled at the host rate. So this checks the parts of the
model that could quietly be wrong: the discretisation of the filters, the timing of each clock tick between host
samples, the zero-order hold and its images, aliasing when the clock runs slow, and the compander's envelope
following. The NE570 is its block diagram (a full-wave rectifier into 10 kOhm x 1 uF, driving a gain cell), not a
transistor-level model, and the smear and clipping curves are the model's own: those are design choices, checked
by ear and by Tests/Main.cpp, not by this script.

Errors are given as the level of the difference between the model and the circuit, relative to the circuit's
output. The chips' hiss is random, so the linear cases subtract a run with no input (the path after the hiss is
linear at these levels); with the compander on, the hiss stays in and sets the floor.
"""
import glob, math, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DSP = os.path.join(HERE, "..", "..", "Source", "DSP")
SR = 48000.0
BUCKETS = 512
REF, FLOOR, RAIL = 0.25, 0.25 / 31.6, 2.0

# test signals, as ngspice expressions in time; BbdModel.cpp computes the same ones
TONES = "0.003*(sin(2*pi*220*time)+sin(2*pi*1700*time)+sin(2*pi*3900*time)+sin(2*pi*6300*time))"
BURSTS = "((time - 0.2*floor(time/0.2)) < 0.1 ? 0.3 : 0.01) * sin(2*pi*1000*time)"


def find_ngspice():
    found = shutil.which("ngspice")
    if found:
        return found
    for path in sorted(glob.glob(os.path.expanduser("~/Library/Caches/ngspice-*/bin/ngspice")), reverse=True):
        return path
    sys.exit("ngspice not found: install it (brew install ngspice) or put it on the PATH")


def chip_settings(compander, character, delay, clock_max):
    """The values BucketBrigade.h derives; kept in step with it by hand."""
    c = min(max(character, 0.0), 1.0)
    fc = 7000.0 - 4500.0 * c if compander else 9500.0 - 5000.0 * c
    smear = 2.0 * BUCKETS * (0.25e-4 + 1.75e-4 * c) / 8.0
    drive = 1.0 + 2.0 * c
    clock = min(max(BUCKETS * SR / delay, 10000.0), clock_max)
    tap = delay * clock / SR
    assert abs(tap - round(tap)) < 1e-6, "pick a delay that puts the tap on a whole bucket"
    return fc, smear, drive, clock, int(round(tap))


def sallen_key(name, inp, out, fc):
    """Fourth-order Butterworth low-pass: two unity-gain Sallen-Key stages, equal resistors, ideal op-amps."""
    lines, w, r = [], 2 * math.pi * fc, 10e3
    node = inp
    for k, q in enumerate((0.5411961, 1.3065630)):
        a, b = f"{name}a{k}", f"{name}b{k}"
        o = out if k == 1 else f"{name}o{k}"
        lines += [f"R{name}{k}1 {node} {a} {r}", f"R{name}{k}2 {a} {b} {r}",
                  f"C{name}{k}1 {a} {o} {2 * q / (w * r):.12g}", f"C{name}{k}2 {b} 0 {1 / (2 * q * w * r):.12g}",
                  f"E{name}{k} {o} 0 {b} 0 1"]
        node = o
    return lines


def netlist(compander, character, delay, clock_max, seconds, signal, outfile):
    fc, smear, drive, clock, tap = chip_settings(compander, character, delay, clock_max)
    # Sample and hold in two stages, as a real one is built. The first switch tracks the input while its pulse
    # is high and holds from the moment it opens, at k * period; the second then copies that held value while
    # the first stays open, so the output steps exactly at the tick. (One switch alone tracks the input for the
    # whole pulse, putting each step a pulse width early; and a pulse narrow enough for that not to matter is
    # occasionally stepped over by the solver, losing a tick.)
    period = 1.0 / clock
    width, edge = period / 50, period / 5000
    n = ["* Tourbillon bucket-brigade check", f"Bx x 0 V = {signal}"]
    if compander:
        n += [f"By y 0 V = v(x) * {REF} / max(v(ce), {FLOOR})",
              "Bce 0 ce I = (abs(v(y)) - v(ce)) / 10k", "Cce ce 0 1u"]
    else:
        n += ["By y 0 V = v(x)"]
    n += sallen_key("in", "y", "s", fc)
    n += [f"Bq q 0 V = v(s) >= 0 ? tanh({drive} * v(s)) / {drive} : v(s) / sqrt(1 + 0.6 * {drive}^2 * v(s)^2)",
          # the switch opens at each clock tick, k * period
          f"Vclk clk 0 PULSE(0 1 {period - width - 1.5 * edge:.12g} {edge} {edge} {width} {period:.12g})",
          "S1 q h clk 0 sw", ".model sw sw vt=0.5 vh=0 ron=0.1 roff=1e12", "Ch h 0 1n", "Eh h1 0 h 0 1",
          f"Vclk2 clk2 0 PULSE(0 1 {period + 2 * edge:.12g} {edge} {edge} {period / 4:.12g} {period:.12g})",
          "S2 h1 h2 clk2 0 sw", "Ch2 h2 0 1n", "Eh2 hb 0 h2 0 1"]
    for k, taps in (("m", tap - 1), ("c", tap), ("p", tap + 1)):
        n += [f"Ed{k} d{k} 0 hb 0 2", f"Rs{k} d{k} t{k} 50",
              f"T{k} t{k} 0 o{k} 0 Z0=50 TD={taps * period:.15g}", f"Rl{k} o{k} 0 50"]
    n += [f"Bz z 0 V = {1 - 2 * smear} * v(oc) + {smear} * (v(om) + v(op))"]
    n += sallen_key("out", "z", "u", fc)
    if compander:
        n += ["Bee 0 ee I = (abs(v(u)) - v(ee)) / 10k", "Cee ee 0 1u",
              f"Bout out 0 V = {RAIL} * tanh(v(u) * max(v(ee), {FLOOR}) / {REF} / {RAIL})",
              f".ic v(ce)={FLOOR} v(ee)={FLOOR}"]
    else:
        n += [f"Bout out 0 V = {RAIL} * tanh(v(u) / {RAIL})"]
    n += [".options reltol=1e-6 abstol=1e-12 vntol=1e-9",
          f".tran {min(1e-6, period / 20):.6g} {seconds} 0 {min(1e-6, period / 20):.6g} uic",
          ".control", "run", f"wrdata {outfile} v(out)", ".endc", ".end"]
    return "\n".join(n) + "\n"


def run_spice(ngspice, work, *args):
    out = os.path.join(work, "spice.txt")
    cir = os.path.join(work, "bbd.cir")
    with open(cir, "w") as f:
        f.write(netlist(*args, out))
    subprocess.run([ngspice, "-b", cir], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t, v = [], []
    with open(out) as f:
        for line in f:
            a, b = line.split()[:2]
            t.append(float(a)); v.append(float(b))
    return t, v


def at_samples(t, v, count):
    """The circuit's output at each host sample instant (linear interpolation between solver points)."""
    out, j = [], 0
    for i in range(count):
        ti = i / SR
        while j + 2 < len(t) and t[j + 1] < ti:
            j += 1
        f = 0.0 if t[j + 1] == t[j] else (ti - t[j]) / (t[j + 1] - t[j])
        out.append(v[j] + min(max(f, 0.0), 1.0) * (v[j + 1] - v[j]))
    return out


def run_model(exe, compander, character, delay, clock_max, seconds, signal):
    r = subprocess.run([exe, str(int(compander)), str(character), str(delay), str(clock_max), str(seconds), signal],
                       check=True, capture_output=True, text=True)
    return [float(x) for x in r.stdout.split()]


def db(x):
    return 20 * math.log10(max(x, 1e-12))


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / max(len(xs), 1))


def main():
    ngspice = find_ngspice()
    work = tempfile.mkdtemp(prefix="tourbillon-bbd-")
    exe = os.path.join(work, "BbdModel")
    subprocess.run(["clang++", "-std=c++17", "-O2", "-I", DSP, os.path.join(HERE, "BbdModel.cpp"), "-o", exe],
                   check=True)

    failures = 0
    # (what, compander, character, delay samples, seconds, signal, pass if the difference is below this, dB)
    # (what, compander, character, delay samples, clock limit, seconds, signal, pass if the difference is below this, dB)
    # the delays are chosen so the tap lands on a whole bucket
    cases = [
        ("chorus, 16 ms, clock 32 kHz",          False, 0.4, 768, 200000.0, 0.12, "tones", -50),
        ("chorus, 4 ms, clock 128 kHz",          False, 0.4, 192, 200000.0, 0.06, "tones", -50),
        ("chorus, 16 ms, dark character",        False, 1.0, 768, 200000.0, 0.12, "tones", -50),
        ("flanger, 2 ms, clock 256 kHz",         False, 0.4, 96,  400000.0, 0.04, "tones", -50),
        ("flanger, 1 ms, clock held at 400 kHz", False, 0.4, 48,  400000.0, 0.04, "tones", -50),
    ]
    for what, compander, character, delay, clock_max, seconds, signal, limit in cases:
        fc, smear, drive, clock, tap = chip_settings(compander, character, delay, clock_max)
        count = int(seconds * SR)
        t, v = run_spice(ngspice, work, compander, character, delay, clock_max, seconds, TONES if signal == "tones" else BURSTS)
        ref = at_samples(t, v, count)
        model = run_model(exe, compander, character, delay, clock_max, seconds, signal)
        if not compander:
            hiss = run_model(exe, compander, character, delay, clock_max, seconds, "silence")
            model = [a - b for a, b in zip(model, hiss)]
        start = int((tap / clock) * SR)                    # from the first echo on
        diff = db(rms([a - b for a, b in zip(model[start:], ref[start:])]) / rms(ref[start:]))
        # level agreement in 5 ms windows, to show the compander tracks through the attacks and releases
        win, worst = int(0.005 * SR), 0.0
        for i in range(start, count - win, win):
            a, b = rms(model[i:i + win]), rms(ref[i:i + win])
            if b > 1e-6:
                worst = max(worst, abs(db(a) - db(b)))
        ok = diff < limit
        failures += not ok
        print(f"  {'PASS' if ok else 'FAIL'}  {what}: clock {clock:.0f} Hz, {tap} buckets, corner {fc:.0f} Hz; "
              f"difference {diff:.1f} dB, level within {worst:.2f} dB in every 5 ms")
    shutil.rmtree(work, ignore_errors=True)
    print("All passed" if failures == 0 else f"{failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
