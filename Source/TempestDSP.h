// Tempest engine: a circuit model of the SoloDallas Schaffer Replica Storm
// (version 2, 2019), from the trace Aion FX published as "Gale" and PedalPCB
// as "Closed Circuit".
//
// No JUCE dependency, so the offline test harness can compile it on its own.
//
// The circuit:
//
//   in --2k--+ GAIN (100k lin, divider) --1u--> IC1A + (biased, 1M)
//            |
//   IC1A (RC4558), non-inverting:
//       ground leg   2k + 10u                      -> gain falls to 1 below 8 Hz
//       feedback     15k || 120p || LDR (NSL-32)   -> gain 1 + (15k || Rldr) / 2k
//                                                     8.5x dark, ~1.1x fully lit
//   IC1B, inverting, gain -R(LIMITER)/10k           LIMITER: 100k log, as a rheostat
//   LED inside a 4x BAT42 bridge, between IC1A's output (through 1k) and IC1B's
//   output. The bridge sees V1 (1 + k), full-wave rectified, with no smoothing
//   cap fitted. The LED lights the LDR in IC1A's own feedback loop, so the
//   limiter is a feedback design and it acts on the gain stage itself.
//
//   IC1A out --1u--2k--+--8.2k--+--100n--> LM386 (gain 20, pins 1/8 open)
//                     1n        2k
//                                         -> fixed pad of about 0.16
//   LM386 out --10u--10R--> BOOST (1k lin, divider) --> out
//
// The LM386 saturates first: it sees V1 x 3.2, so with GAIN up it is the
// "harmonic clipping" stage, and the limiter's job is to hold V1 back from it.
//
// Signal scale: 1.0 is 1 volt, after the Input Level calibration.
//
// From the trace: every resistor, capacitor and pot value above, the topology,
// and the chips. From the NSL-32 datasheet: 160 ohm at 16 mA, >500 k dark.
// Estimates (see the README): op-amp and LM386 swing, clip shapes, the LED
// knee, the LDR's response curve and time constants, and the pickup models.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace tempest
{
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Part values.

namespace parts
{
    constexpr double rIn        = 2.0e3;     // series resistor before GAIN
    constexpr double potGain    = 100.0e3;   // GAIN, linear
    constexpr double rBias      = 1.0e6;     // IC1A input bias, loads the wiper
    constexpr double rGround    = 2.0e3;     // IC1A ground leg
    constexpr double cGround    = 10.0e-6;
    constexpr double rFeedback  = 15.0e3;    // IC1A feedback, in parallel with the LDR
    constexpr double rSideIn    = 10.0e3;    // IC1B input resistor
    constexpr double potLimiter = 100.0e3;   // LIMITER, log, IC1B feedback
    constexpr double rLed       = 1.0e3;     // series resistor into the bridge
    constexpr double cCouple1   = 1.0e-6;    // IC1A out coupling
    constexpr double rPad1      = 2.0e3;
    constexpr double rPad2      = 8.2e3;
    constexpr double rPad3      = 2.0e3;     // to ground
    constexpr double cLm386In   = 100.0e-9;
    constexpr double rLm386In   = 50.0e3;    // LM386 input resistance (datasheet)
    constexpr double lm386Gain  = 20.0;      // gain pins open
    constexpr double cOut       = 10.0e-6;
    constexpr double rOut       = 10.0;
    constexpr double potBoost   = 1.0e3;     // BOOST, linear
    constexpr double cIsolator  = 10.0e-6;   // the "isolator threshold" cap, not fitted on the traced unit

    // NSL-32 photocell.
    constexpr double ldrOnOhms  = 160.0;     // at 16 mA
    constexpr double ldrOnAmps  = 16.0e-3;
    constexpr double ldrDark    = 1.0e6;     // datasheet: >500 k after 10 s
    constexpr double ldrSlope   = 0.8;       // R ~ I^-slope (estimate, typical CdS)
}

// IC1A's gain with the LDR dark: 1 + 15k / 2k.
constexpr double kDarkGain = 1.0 + parts::rFeedback / parts::rGround;

// The pad between IC1A and the LM386, with the LM386's input resistance across the last 2k.
inline double padRatio()
{
    const double lower = parts::rPad3 * parts::rLm386In / (parts::rPad3 + parts::rLm386In);
    return lower / (parts::rPad1 + parts::rPad2 + lower);
}

// GAIN as a divider, with IC1A's 1M bias resistor loading the wiper.
inline double gainDivider (double position)
{
    position = std::clamp (position, 0.0, 1.0);
    const double lower = parts::potGain * position;
    const double upper = parts::potGain - lower;
    const double loaded = lower * parts::rBias / (lower + parts::rBias);
    return loaded / (parts::rIn + upper + loaded);
}

// A-taper (log) pot, 10% at half travel.
inline double audioTaper (double position)
{
    position = std::clamp (position, 0.0, 1.0);
    return (std::pow (81.0, position) - 1.0) / 80.0;
}

// IC1B's gain: LIMITER's resistance over 10k, 0 to 10.
inline double sidechainGain (double position)
{
    return parts::potLimiter * audioTaper (position) / parts::rSideIn;
}

// NSL-32: resistance against LED current.
inline double ldrConductanceFor (double amps)
{
    if (amps <= 1.0e-7)
        return 0.0;
    return std::pow (amps / parts::ldrOnAmps, parts::ldrSlope) / parts::ldrOnOhms;
}

// ---------------------------------------------------------------------------
// Supply. The pedal takes 9 to 12 V and biases everything at half supply.

struct Rails
{
    double opAmp;        // RC4558 swing either side of bias, into light loads
    double lm386Up;      // LM386 swing into the 1k BOOST pot
    double lm386Down;
};

inline Rails railsFor (int supply)   // 0 = 9 V, 1 = 12 V
{
    return supply == 1 ? Rails { 5.0, 4.8, 5.3 }
                       : Rails { 3.5, 3.3, 3.8 };
}

// ---------------------------------------------------------------------------
// Clippers.

// An op-amp in a feedback loop holds the line until the output stage runs
// out, then folds over hard.
inline double opAmpClip (double x, double limit)
{
    const double r = x / limit;
    return x / std::pow (1.0 + std::pow (std::abs (r), 6.0), 1.0 / 6.0);
}

// The LM386 has little open-loop gain to spare, so it bends earlier and
// softer, and its output stage reaches the top rail before the bottom one.
inline double lm386Clip (double x, double up, double down)
{
    const double limit = x >= 0.0 ? up : down;
    const double r = x / limit;
    return x / std::pow (1.0 + std::pow (std::abs (r), 2.5), 1.0 / 2.5);
}

// ---------------------------------------------------------------------------
// Pickup loading. The pedal's input is about 100k (the GAIN pot), where an
// audio interface's Hi-Z input is about 1M. A passive pickup is an inductor
// with a winding resistance, driving the cable's capacitance and whatever
// load it sees, so the 100k damps its resonant peak. This filter is the
// pickup's response into 100k divided by its response into 1M, which is
// what a recording made through a Hi-Z input is missing.

struct PickupSpec
{
    double henries, ohms, farads;   // farads: pickup plus about 15 ft of cable
};

inline PickupSpec pickupFor (int choice)   // 1 = single coil, 2 = humbucker
{
    return choice == 1 ? PickupSpec { 2.5, 6.0e3, 600.0e-12 }
                       : PickupSpec { 4.4, 8.5e3, 750.0e-12 };
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    // Analogue (B2 s^2 + B1 s + B0) / (A2 s^2 + A1 s + A0), bilinear, warped at f0.
    void setAnalog (double B2, double B1, double B0, double A2, double A1, double A0,
                    double sampleRate, double f0)
    {
        const double w0 = 2.0 * kPi * f0;
        const double k = w0 / std::tan (w0 / (2.0 * sampleRate));
        const double k2 = k * k;
        const double a0 = A2 * k2 + A1 * k + A0;
        b0 = (B2 * k2 + B1 * k + B0) / a0;
        b1 = (2.0 * B0 - 2.0 * B2 * k2) / a0;
        b2 = (B2 * k2 - B1 * k + B0) / a0;
        a1 = (2.0 * A0 - 2.0 * A2 * k2) / a0;
        a2 = (A2 * k2 - A1 * k + A0) / a0;
    }

    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0; }
};

inline void designPickupFilter (Biquad& f, int choice, double sampleRate)
{
    if (choice == 0)
    {
        f.setIdentity();
        return;
    }

    const auto p = pickupFor (choice);
    const double loaded = parts::rIn + parts::potGain;   // what the pedal presents
    const double hiZ = 1.0e6;

    // H(s) = 1 / (LC s^2 + (L/Rl + RC) s + 1 + R/Rl)
    const double lc = p.henries * p.farads;
    f.setAnalog (lc, p.henries / hiZ + p.ohms * p.farads, 1.0 + p.ohms / hiZ,
                 lc, p.henries / loaded + p.ohms * p.farads, 1.0 + p.ohms / loaded,
                 sampleRate, 1.0 / (2.0 * kPi * std::sqrt (lc)));
}

// First-order high-pass, bilinear.
struct HighPass
{
    double a = 0, x1 = 0, y1 = 0;

    void set (double cornerHz, double sampleRate)
    {
        const double k = std::tan (kPi * cornerHz / sampleRate);
        a = (1.0 - k) / (1.0 + k);
    }

    double process (double x)
    {
        const double y = 0.5 * (1.0 + a) * (x - x1) + a * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() { x1 = y1 = 0.0; }
};

// ---------------------------------------------------------------------------

struct Params
{
    double gain = 0.6;        // knob position, 0..1
    double limiter = 0.4;
    double boost = 0.35;
    bool on = true;
    int supply = 0;           // 0 = 9 V, 1 = 12 V
    bool isolator = false;    // 10 uF smoothing cap across the LED
    int pickup = 2;           // 0 = none, 1 = single coil, 2 = humbucker
    double inputVolts = 1.0;  // volts at 0 dBFS
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    void setParams (const Params& p) { params = p; }

    void prepare (double rate)
    {
        sampleRate = rate;
        designedPickup = -1;
        smoothCoeff = coefficient (0.02);
        bypassCoeff = coefficient (0.008);

        // LDR time constants, as two layers of cell: a fast one that does most
        // of the work and a slow one that builds up under sustained light and
        // lingers after it (the "memory" CdS cells are known for). Estimates:
        // from full on, the fast layer passes 100k in about 100 ms (the NSL-32
        // datasheet gives 80 ms), and the slow layer leaves a tail of a few
        // hundred ms, the "medium-slow release" retailers describe.
        fastAttack  = coefficient (0.004);
        fastRelease = coefficient (0.016);
        slowAttack  = coefficient (0.15);
        slowRelease = coefficient (0.40);

        isoStep = 1.0 / (rate * parts::cIsolator);

        for (auto& c : channels)
        {
            c.groundLeg.set (1.0 / (2.0 * kPi * parts::rGround * parts::cGround), rate);
            c.couple1.set (1.0 / (2.0 * kPi * parts::cCouple1
                                  * (parts::rPad1 + parts::rPad2 + parts::rPad3)), rate);
            c.lm386In.set (1.0 / (2.0 * kPi * parts::cLm386In * parts::rLm386In), rate);
            c.output.set (1.0 / (2.0 * kPi * parts::cOut * (parts::rOut + parts::potBoost)), rate);
        }

        reset();
    }

    void reset()
    {
        for (auto& c : channels)
        {
            c.pickup.reset();
            c.groundLeg.reset();
            c.couple1.reset();
            c.lm386In.reset();
            c.output.reset();
            c.gFast = c.gSlow = 0.0;
            c.isolator = 0.0;
        }

        gainSmoothed = gainDivider (params.gain);
        sideSmoothed = sidechainGain (params.limiter);
        boostSmoothed = std::clamp (params.boost, 0.0, 1.0);
        inputSmoothed = params.inputVolts;
        wet = params.on ? 1.0 : 0.0;
    }

    // Process channels in place. Each channel is its own pedal.
    void process (float* const* audio, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, kMaxChannels);

        if (designedPickup != params.pickup)
        {
            for (auto& c : channels)
                designPickupFilter (c.pickup, params.pickup, sampleRate);
            designedPickup = params.pickup;
        }

        const Rails rails = railsFor (params.supply);
        const double gainTarget = gainDivider (params.gain);
        const double sideTarget = sidechainGain (params.limiter);
        const double boostTarget = std::clamp (params.boost, 0.0, 1.0);
        const double inputTarget = std::max (1.0e-3, params.inputVolts);
        const double wetTarget = params.on ? 1.0 : 0.0;
        const double pad = padRatio() * parts::lm386Gain;

        double maxReduction = 0.0, maxDrive = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            gainSmoothed  += (gainTarget - gainSmoothed) * smoothCoeff;
            sideSmoothed  += (sideTarget - sideSmoothed) * smoothCoeff;
            boostSmoothed += (boostTarget - boostSmoothed) * smoothCoeff;
            inputSmoothed += (inputTarget - inputSmoothed) * smoothCoeff;
            wet           += (wetTarget - wet) * bypassCoeff;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& c = channels[ch];
                const double dry = audio[ch][i];
                double v = dry * inputSmoothed;

                // Pickup into 100k, then GAIN.
                v = c.pickup.process (v) * gainSmoothed;

                // IC1A. The LDR is slow, so last sample's value stands in for
                // this one's without changing anything audible.
                const double rLdr = 1.0 / (c.gFast + c.gSlow + 1.0 / parts::ldrDark);
                const double rf = parts::rFeedback * rLdr / (parts::rFeedback + rLdr);
                const double v1 = opAmpClip (v + rf / parts::rGround * c.groundLeg.process (v),
                                             rails.opAmp);

                // Sidechain: IC1B inverts and amplifies, the bridge rectifies
                // what lies between the two outputs, and the LED conducts past
                // its knee plus two Schottky drops.
                const double bridge = std::abs (v1 + opAmpClip (sideSmoothed * v1, rails.opAmp));
                double amps;
                if (params.isolator)
                {
                    // The cap sits across the LED: it charges through the 1k and
                    // two Schottkys on each peak and feeds the LED in between.
                    const double charge = std::max (0.0, bridge - kSchottkyDrops - c.isolator) / parts::rLed;
                    amps = ledDiode (c.isolator);
                    c.isolator = std::max (0.0, c.isolator + (charge - amps) * isoStep);
                }
                else
                {
                    amps = ledCurrent (bridge);
                }
                amps = std::min (amps, 10.0e-3);

                // LDR.
                const double target = ldrConductanceFor (amps);
                c.gFast += (0.85 * target - c.gFast) * (0.85 * target > c.gFast ? fastAttack : fastRelease);
                c.gSlow += (0.15 * target - c.gSlow) * (0.15 * target > c.gSlow ? slowAttack : slowRelease);

                // Pad, LM386, output cap, BOOST.
                const double u = c.lm386In.process (c.couple1.process (v1)) * pad;
                const double out = lm386Clip (u, rails.lm386Up, rails.lm386Down);
                const double y = c.output.process (out) * boostSmoothed / inputSmoothed;

                audio[ch][i] = (float) (dry + (y - dry) * wet);

                maxReduction = std::max (maxReduction, kDarkGain / (1.0 + rf / parts::rGround));
                maxDrive = std::max (maxDrive, std::abs (u) / (u >= 0.0 ? rails.lm386Up : rails.lm386Down));
            }
        }

        gainReductionDb.store ((float) (20.0 * std::log10 (maxReduction)));
        drive.store ((float) maxDrive);
    }

    // Metering, for the UI.
    float getGainReductionDb() const { return gainReductionDb.load(); }
    float getDrive() const { return drive.load(); }   // LM386 input over its clip level; >1 is clipping

    // The LED (knee about 1.7 V, about 25 ohm above it) and the two BAT42s
    // that conduct with it (about 0.25 V each at these currents). Estimates.
    static constexpr double kLedKnee = 1.7, kLedOhms = 25.0, kSchottkyDrops = 0.5;

    static double softKnee (double volts)
    {
        constexpr double soft = 0.07;
        const double over = volts / soft;
        return over > 30.0 ? volts : soft * std::log1p (std::exp (over));
    }

    // LED current with the LED's own voltage given.
    static double ledDiode (double volts) { return softKnee (volts - kLedKnee) / kLedOhms; }

    // LED current against the voltage across the bridge, through the 1k.
    static double ledCurrent (double volts)
    {
        return softKnee (volts - kLedKnee - kSchottkyDrops) / (parts::rLed + kLedOhms);
    }

private:
    struct Channel
    {
        Biquad pickup;
        HighPass groundLeg, couple1, lm386In, output;
        double gFast = 0, gSlow = 0;
        double isolator = 0;
    };

    double coefficient (double seconds) const
    {
        return 1.0 - std::exp (-1.0 / (seconds * sampleRate));
    }

    Params params;
    Channel channels[kMaxChannels];
    double sampleRate = 48000.0;
    int designedPickup = -1;

    double smoothCoeff = 0, bypassCoeff = 0;
    double fastAttack = 0, fastRelease = 0, slowAttack = 0, slowRelease = 0;
    double isoStep = 0;

    double gainSmoothed = 0, sideSmoothed = 0, boostSmoothed = 0, inputSmoothed = 1, wet = 1;

    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> drive { 0.0f };
};
} // namespace tempest
